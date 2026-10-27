#!/usr/bin/env python3
"""Release builder, run by maintainers/CI, never on a tester's computer.

Downloads pinned portable CPython, installs pinned wheels, compiles tiny helper
programs, vendors native tools and their dynamic libraries, and seals a ZIP.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PYTHON = {
    "macos-arm64": (
        "aarch64-apple-darwin",
        "4572133a5542f306b9bdb155da5800f9e38950cd0a98d469b832ce256fe299ea",
    ),
    "macos-x64": (
        "x86_64-apple-darwin",
        "1a94c83264731e9603fbea78e57e7ca8f20e7d91eb866627ac2304621b0f6f1f",
    ),
    "windows-x64": (
        "x86_64-pc-windows-msvc",
        "7330282b47cd43a66b702d39078d2e5a88e580cee351d82f95045f21f5ee042a",
    ),
}


def sha(path):
    with Path(path).open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def run(command, **kw):
    print("+", " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kw)


def download(url, path, expected):
    if path.exists() and sha(path) == expected:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".part")
    with urllib.request.urlopen(url, timeout=120) as src, tmp.open("wb") as out:
        shutil.copyfileobj(src, out)
    if sha(tmp) != expected:
        tmp.unlink()
        raise RuntimeError("Download hash mismatch: " + url)
    tmp.replace(path)


def macho(path):
    try:
        with path.open("rb") as f:
            return f.read(4) in (
                b"\xcf\xfa\xed\xfe",
                b"\xfe\xed\xfa\xcf",
                b"\xca\xfe\xba\xbe",
                b"\xbe\xba\xfe\xca",
            )
    except OSError:
        return False


def vendor_macos(package, originals):
    """Relocate the dependency closure; no /opt/homebrew paths remain loadable."""
    libdir = package / "runtime/lib"
    libdir.mkdir(exist_ok=True)
    pending = list(originals.items())
    done = set()
    relocated = {}
    notices = package / "runtime/licenses/native-tools"
    notices.mkdir(parents=True, exist_ok=True)
    while pending:
        dest, src = pending.pop()
        if dest in done:
            continue
        done.add(dest)
        links = subprocess.check_output(
            ["otool", "-L", str(src)], text=True
        ).splitlines()
        links = [line for line in links if line.startswith("\t")]
        own = subprocess.run(
            ["otool", "-D", str(src)], capture_output=True, text=True
        ).stdout.splitlines()
        own = set(s.strip() for s in own if s.strip() and not s.endswith(":"))
        if own:
            run(
                ["install_name_tool", "-id", "@rpath/" + dest.name, dest],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        raw = subprocess.check_output(["otool", "-l", str(src)], text=True).splitlines()
        rpaths = []
        for i, line in enumerate(raw):
            if line.strip() == "cmd LC_RPATH":
                rpaths.append(raw[i + 2].strip().split(" ", 2)[1])
        for line in links:
            old = line.strip().split(" (", 1)[0]
            if old in own:
                continue
            if old.startswith(("/usr/lib/", "/System/Library/")):
                continue
            options = []
            if old.startswith("@loader_path/"):
                options = [src.parent / old[len("@loader_path/") :]]
            elif old.startswith("@executable_path/"):
                options = [src.parent / old[len("@executable_path/") :]]
            elif old.startswith("@rpath/"):
                for rp in rpaths:
                    rp = rp.replace("@loader_path", str(src.parent)).replace(
                        "@executable_path", str(src.parent)
                    )
                    options.append(Path(rp) / old[len("@rpath/") :])
            else:
                options = [Path(old)]
            dependency = next((p.resolve() for p in options if p.is_file()), None)
            if dependency is None:
                raise RuntimeError(f"Unresolved native dependency {old} of {src}")
            # Existing portable-relative dependency remains inside its copied subtree.
            if src.is_relative_to(package) and dependency.is_relative_to(package):
                continue
            filename = dependency.name
            target = relocated.get(dependency)
            if target is None:
                target = libdir / filename
                if target.exists():
                    target = libdir / (
                        hashlib.sha256(str(dependency).encode()).hexdigest()[:8]
                        + "-"
                        + filename
                    )
                relocated[dependency] = target
                shutil.copy2(dependency, target)
            relative = os.path.relpath(target, dest.parent)
            new = "@loader_path/" + relative
            run(
                ["install_name_tool", "-change", old, new, dest],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            pending.append((target, dependency))
        # Save local formula source/notice metadata when distributing Homebrew tools.
        if "/Cellar/" in str(src):
            parts = src.parts
            i = parts.index("Cellar")
            prefix = Path(*parts[: i + 3])
            formula = parts[i + 1]
            for pattern in [
                "LICENSE*",
                "COPYING*",
                ".brew/*.rb",
                "share/doc/*/COPYING*",
                "share/doc/*/LICENSE*",
            ]:
                for notice in prefix.glob(pattern):
                    if notice.is_file():
                        shutil.copy2(notice, notices / (formula + "-" + notice.name))
    for dest in done:
        if macho(dest):
            run(
                ["codesign", "--force", "--sign", "-", dest],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
    # Assert every library reference is system-owned or resolves inside the bundle.
    for f in (package / "runtime").rglob("*"):
        if f.is_file() and macho(f):
            lines = subprocess.check_output(
                ["otool", "-L", str(f)], text=True
            ).splitlines()[1:]
            own = set(
                s.strip()
                for s in subprocess.run(
                    ["otool", "-D", str(f)], capture_output=True, text=True
                ).stdout.splitlines()[1:]
            )
            for line in lines:
                if not line.startswith("\t"):
                    continue
                ref = line.strip().split(" (", 1)[0]
                if ref in own:
                    continue
                if ref.startswith(("/opt/", "/usr/local/", "/Users/")):
                    raise RuntimeError(f"External library in bundle: {f}: {ref}")


def vendor_windows(package):
    """MinGW helper dependencies are shipped beside every executable using them."""
    roots = [package / "runtime/bin", package / "runtime/python"]
    search = [
        *roots,
        *[Path(p) for p in os.environ.get("PATH", "").split(os.pathsep) if p],
    ]
    objdump = shutil.which("objdump")
    if not objdump:
        raise RuntimeError("Build-time objdump is required to audit Windows DLLs.")
    # The root supervisor must use only the Windows system ABI: its C runtime
    # is linked statically, so a clean Windows install needs no redistributable.
    root_imports = subprocess.check_output(
        [objdump, "-p", str(package / "nfsu2-generator.exe")],
        text=True,
        errors="replace",
    )
    for line in root_imports.splitlines():
        if "DLL Name:" in line and any(
            name in line.lower()
            for name in ["vcruntime", "msvcp", "libgcc", "libstdc++", "msys-"]
        ):
            raise RuntimeError(
                "Supervisor requires an external runtime: " + line.strip()
            )
    if objdump:
        pending = list((package / "runtime/bin").glob("*.exe"))
        seen = set()
        while pending:
            exe = pending.pop()
            if exe.name.lower() in seen:
                continue
            seen.add(exe.name.lower())
            text = subprocess.check_output(
                [objdump, "-p", str(exe)], text=True, errors="replace"
            )
            for line in text.splitlines():
                if "DLL Name:" not in line:
                    continue
                name = line.split("DLL Name:", 1)[1].strip()
                # Windows 10 system ABI, not an optional redistributable.
                if name.lower().startswith(
                    ("api-ms-win-", "ext-ms-")
                ) or name.lower() in {
                    "kernel32.dll",
                    "user32.dll",
                    "advapi32.dll",
                    "msvcrt.dll",
                    "ucrtbase.dll",
                    "ole32.dll",
                    "shell32.dll",
                    "ws2_32.dll",
                    "bcrypt.dll",
                    "ntdll.dll",
                    "gdi32.dll",
                    "crypt32.dll",
                    "secur32.dll",
                    "winmm.dll",
                    "version.dll",
                    "shlwapi.dll",
                    "imm32.dll",
                    "comdlg32.dll",
                    "comctl32.dll",
                }:
                    continue
                source = next((p / name for p in search if (p / name).is_file()), None)
                if not source:
                    raise RuntimeError("Missing Windows DLL: " + name)
                target = package / "runtime/bin" / name
                if not target.exists():
                    shutil.copy2(source, target)
                    pending.append(target)
    # CPython's redistributable DLLs must be beside binaries that need them.
    for name in ["vcruntime140.dll", "vcruntime140_1.dll"]:
        src = package / "runtime/python" / name
        if src.exists():
            shutil.copy2(src, package / "runtime/bin" / name)


def windows_utf8(package):
    # CLI programs using the Windows CRT must receive UTF-8 paths too. This is
    # embedded at build time, not a system locale change on the tester's machine.
    sdk = (
        Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
        / "Windows Kits/10/bin"
    )
    mt = shutil.which("mt.exe") or next(
        (str(p) for p in sorted(sdk.glob("*/x64/mt.exe"), reverse=True)), None
    )
    if not mt:
        raise RuntimeError("Windows SDK manifest tool is required at build time.")
    for exe in (package / "runtime/bin").glob("*.exe"):
        run(
            [
                mt,
                "-nologo",
                "-manifest",
                ROOT / "native/utf8.manifest",
                "-outputresource:" + str(exe) + ";#1",
            ]
        )


def build(args):
    target = (
        "windows-x64"
        if os.name == "nt"
        else "macos-" + ("arm64" if platform.machine() == "arm64" else "x64")
    )
    triple, digest = PYTHON[target]
    cache = Path(args.cache).resolve()
    out = Path(args.output).resolve()
    if out.exists():
        raise RuntimeError("Use a new empty bundle destination: " + str(out))
    out.mkdir(parents=True)
    bin_dir = out / "runtime/bin"
    bin_dir.mkdir(parents=True)
    # Only project files intended for distribution, no local datasets/credentials.
    names = (
        subprocess.check_output(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
            cwd=ROOT,
        )
        .decode()
        .split("\0")
    )
    for name in names:
        if not name or name.startswith(
            ("native/target/", "native/dist/", "native/.cache/", ".git/")
        ):
            continue
        src = ROOT / name
        if not src.is_file() or src.is_symlink():
            continue
        dst = out / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    archive = cache / f"python-{target}.tar.gz"
    url = f"https://github.com/astral-sh/python-build-standalone/releases/download/20260814/cpython-3.12.14%2B20260814-{triple}-install_only.tar.gz"
    download(url, archive, digest)
    with tarfile.open(archive) as tar:
        tar.extractall(out / "runtime", filter="data")
    # Ordinary ZIP extractors must not need symlink privileges, including Python's
    # executable aliases. Resolve only links contained within the portable tree.
    for link in list((out / "runtime").rglob("*")):
        if link.is_symlink():
            resolved = link.resolve()
            if not resolved.is_relative_to(out):
                raise RuntimeError("Portable runtime link escapes package.")
            if resolved.is_file():
                link.unlink()
                shutil.copy2(resolved, link)
    py = out / (
        "runtime/python/python.exe" if os.name == "nt" else "runtime/python/bin/python3"
    )
    env = dict(
        os.environ,
        PYTHONNOUSERSITE="1",
        PYTHONDONTWRITEBYTECODE="1",
        PYTHONUTF8="1",
        UV_NO_CACHE="1",
    )
    for key in ["PYTHONHOME", "PYTHONPATH", "VIRTUAL_ENV"]:
        env.pop(key, None)
    run(
        ["uv", "pip", "install", "--python", py, "-r", ROOT / "requirements.txt"],
        env=env,
    )
    run(
        [
            "uv",
            "pip",
            "install",
            "--python",
            py,
            "--no-deps",
            "trajectory-planning-helpers==0.79",
        ],
        env=env,
    )
    source_archive = cache / "quadprog-0.1.13.tar.gz"
    download(
        "https://files.pythonhosted.org/packages/9e/60/fb9b56d425c5b3495bb92683c28f954aedb609ae62f14c81a1b5621ad759/quadprog-0.1.13.tar.gz",
        source_archive,
        "9d6dd32f2762f29b840fb83741d11e527ddf48745f63b79caad0e530b4a6a0ff",
    )
    (out / "runtime/sources").mkdir(exist_ok=True)
    shutil.copy2(source_archive, out / "runtime/sources" / source_archive.name)
    ext = ".exe" if os.name == "nt" else ""
    originals = {}
    tool_dir = Path(args.tools).resolve() if args.tools else None
    for name in ["ffmpeg", "lame", "cjpegli"]:
        src = (tool_dir / (name + ext)) if tool_dir else Path(shutil.which(name) or "")
        if not src.is_file():
            raise RuntimeError("Build-time native tool missing: " + name)
        dest = bin_dir / (name + ext)
        shutil.copy2(src, dest)
        originals[dest] = src.resolve()
    cc = os.environ.get("CC", "gcc" if os.name == "nt" else "cc")
    for name, files in {
        "eaxa2pcm": ["eaxa2pcm.c"],
        "movie_audio": ["movie_audio.c"],
        "mt2pcm": ["mt2pcm.c", "vendor/utk/utkdec.c"],
        "xaencode": ["xaencode.c"],
    }.items():
        command = [
            cc,
            "-O2",
            "-std=c11",
            "-o",
            str(bin_dir / (name + ext)),
            *[str(ROOT / "tools" / f) for f in files],
            "-lm",
        ]
        if os.name == "nt":
            command += ["-static", "-static-libgcc"]
        run(command)
    # Keep exact copyright/license notices for every statically linked Rust crate.
    metadata = json.loads(
        subprocess.check_output(
            [
                "cargo",
                "metadata",
                "--locked",
                "--format-version",
                "1",
                "--manifest-path",
                str(ROOT / "native/Cargo.toml"),
            ],
            text=True,
        )
    )
    for package in metadata["packages"]:
        source = Path(package["manifest_path"]).parent
        directory = (
            out / "runtime/licenses/rust" / (package["name"] + "-" + package["version"])
        )
        for pattern in ["LICENSE*", "COPYING*", "COPYRIGHT*", "NOTICE*"]:
            for notice in source.glob(pattern):
                if notice.is_file():
                    directory.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(notice, directory / notice.name)
    helper = Path(args.helper).resolve()
    shutil.copy2(helper, out / ("nfsu2-generator" + ext))
    if tool_dir:
        for directory in ["licenses", "sources"]:
            if (tool_dir / directory).is_dir():
                shutil.copytree(
                    tool_dir / directory,
                    out / "runtime" / directory,
                    dirs_exist_ok=True,
                )
    if sys.platform == "darwin":
        vendor_macos(out, originals)
    elif os.name == "nt":
        vendor_windows(out)
        windows_utf8(out)
    # Remove caches generated by build-time pip/imports, never user files.
    for p in list(out.rglob("__pycache__")):
        if p.is_dir():
            shutil.rmtree(p)
    files = {
        p.relative_to(out).as_posix(): sha(p)
        for p in sorted(out.rglob("*"))
        if p.is_file() and not p.is_symlink()
    }
    manifest = {
        "schema": 1,
        "generator_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip(),
        "generator_version": "0.1.0",
        "platform": target,
        "python": py.relative_to(out).as_posix(),
        "candidate": json.loads((out / "eboot/release.json").read_text())["candidate"],
        "files": files,
        "runtime_source": {"url": url, "sha256": digest},
        "runtime_installation_required": False,
    }
    (out / "bundle.json").write_text(json.dumps(manifest, indent=2) + "\n")
    executable = out / ("nfsu2-generator" + ext)
    clean = dict(env, NFSU2_BUNDLE_ROOT=str(out))
    clean["PATH"] = str(bin_dir) + (
        os.pathsep + os.environ.get("SystemRoot", "C:\\Windows") + "\\System32"
        if os.name == "nt"
        else ":/usr/bin:/bin:/usr/sbin:/sbin"
    )
    run([executable, "doctor"], env=clean)
    (out / "PACKAGE-INFO.json").write_text(
        json.dumps(
            {
                "platform": target,
                "runtime": "CPython 3.12.14",
                "candidate": manifest["candidate"],
                "files": len(files),
            },
            indent=2,
        )
        + "\n"
    )
    # Include the new metadata in the sealed manifest too.
    manifest["files"]["PACKAGE-INFO.json"] = sha(out / "PACKAGE-INFO.json")
    (out / "bundle.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(
        json.dumps(
            {
                "package": str(out),
                "platform": target,
                "files": len(files),
                "bytes": sum(p.stat().st_size for p in out.rglob("*") if p.is_file()),
            }
        ),
        flush=True,
    )
    return out


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True)
    p.add_argument("--cache", default=ROOT / "native/.cache")
    p.add_argument("--tools")
    p.add_argument("--helper", required=True)
    args = p.parse_args()
    build(args)
