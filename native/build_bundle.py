#!/usr/bin/env python3
"""Release builder, run by maintainers/CI, never on a tester's computer.

Downloads pinned portable CPython, installs pinned wheels, compiles tiny helper
programs, vendors native tools and their dynamic libraries, and seals a ZIP.

With --runtime-from PACKAGE (an extracted earlier package of the same platform),
the runtime and the helper are that package's, byte for byte, and only the
project's files are new: a generator update whose runtime testers already have,
so an update of their output reuses every step whose code did not change
(build_game.py --atualizar). It also builds another platform's package.
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
    "linux-x64": (
        "x86_64-unknown-linux-gnu",
        "3297691ae34f75fed81ac424e040145fccb0bafe8e581cd5cadbddfa1c0766c0",
    ),
}
# Linux wheels no newer than this glibc, whatever the build machine has.
MANYLINUX = "x86_64-manylinux_2_28"


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
    """Audit every loaded native module; never depend on CI's installed VC tools."""
    system = set(
        """kernel32 kernelbase user32 advapi32 msvcrt ucrtbase msvcp_win ole32 oleaut32
        shell32 ws2_32 wsock32 bcrypt ntdll gdi32 gdi32full crypt32 cryptbase secur32
        winmm version shlwapi imm32 comdlg32 comctl32 rpcrt4 normaliz userenv psapi
        dbghelp imagehlp wintrust setupapi cfgmgr32 winhttp wininet urlmon netapi32
        wtsapi32 powrprof propsys dwmapi uxtheme msimg32 opengl32 glu32 combase
        ncrypt sspicli cabinet pdh ntmarta synchronization dnsapi iphlpapi dhcpcsvc
        hid winspool wldap32 sensapi wsock32 oleacc usp10 avrt mswsock authz""".split()
    )
    objdump = shutil.which("objdump")
    if not objdump:
        raise RuntimeError("Build-time objdump is required to audit Windows DLLs.")

    def imports(path):
        output = subprocess.check_output(
            [objdump, "-p", str(path)], text=True, errors="replace"
        )
        return [
            line.split("DLL Name:", 1)[1].strip()
            for line in output.splitlines()
            if "DLL Name:" in line
        ]

    def os_owned(name):
        low = name.lower()
        return (
            low.startswith(("api-ms-win-", "ext-ms-"))
            or low.removesuffix(".dll").removesuffix(".drv") in system
        )

    root_imports = imports(package / "nfsu2-generator.exe")
    if any(not os_owned(name) for name in root_imports):
        raise RuntimeError(
            "Supervisor requires a non-system runtime: " + ", ".join(root_imports)
        )

    python = package / "runtime/python"
    bins = package / "runtime/bin"
    private = {f.name.lower(): f for f in (package / "runtime").rglob("*.dll")}
    paths = [Path(p) for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    redist = []
    for prefix in [
        os.environ.get("ProgramFiles", "C:/Program Files"),
        os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"),
    ]:
        redist.extend(
            Path(prefix).glob(
                "Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
            )
        )
    if os.environ.get("VCToolsRedistDir"):
        redist.extend(
            Path(os.environ["VCToolsRedistDir"]).glob("x64/Microsoft.VC*.CRT")
        )
    redist = sorted(set(redist), reverse=True)
    pending = [
        package / "nfsu2-generator.exe",
        *bins.glob("*.exe"),
        *python.glob("*.exe"),
        *python.rglob("*.pyd"),
        *python.rglob("*.dll"),
    ]
    seen = set()
    copied = {}
    count = 0
    while pending:
        module = pending.pop()
        if module in seen:
            continue
        seen.add(module)
        count += 1
        for name in imports(module):
            if os_owned(name):
                continue
            low = name.lower()
            vc = low.startswith(("vcruntime", "msvcp140", "concrt140", "vccorlib140"))
            source = private.get(low)
            if source is None:
                search = redist if vc else paths
                source = next(
                    (folder / name for folder in search if (folder / name).is_file()),
                    None,
                )
                if source is None:
                    raise RuntimeError(
                        f"Missing private Windows DLL: {name} required by {module.relative_to(package)}"
                    )
                source = source.resolve()
            # Put CRT/GCC dependencies next to both Python and the native tools.
            # Wheels keep their own hashed library layouts and bootstrap loaders.
            if vc or low not in private:
                for folder in [python, bins]:
                    dest = folder / name
                    if dest.resolve() != source.resolve():
                        shutil.copy2(source, dest)
                    pending.append(dest)
                    private[low] = dest
                copied[name] = (
                    str(source.relative_to(package))
                    if source.is_relative_to(package)
                    else str(source)
                )
    audit = {
        "native_modules": count,
        "copied_dll_sources": copied,
        "system_dlls_only_outside_package": True,
        "source": "private Python wheels / portable CPython / MinGW toolchain / Visual Studio release redistributable directory",
    }
    (package / "runtime/windows-library-audit.json").write_text(
        json.dumps(audit, indent=2) + "\n"
    )


def elf(path):
    try:
        with path.open("rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def vendor_linux(package):
    """Audit every ELF file: only glibc and the loader come from the system.
    Wheels carry their own libraries (auditwheel, $ORIGIN); a library outside
    the package or a C++ runtime from the build machine fails the build."""
    system = {
        "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
        "libutil.so.1", "ld-linux-x86-64.so.2", "libcrypt.so.1",
        # GCC's unwinder: glibc itself loads it, every distribution has it (Rust std).
        "libgcc_s.so.1",
        # Left to the system by the manylinux wheels (SciPy's C++, Pillow's zlib),
        # as their policy allows; part of every distribution's base.
        "libstdc++.so.6", "libz.so.1",
    }
    private = {f.name for f in (package / "runtime").rglob("*.so*") if f.is_file()}
    glibc = (0, 0)
    count = 0
    static = []
    foreign_rpaths = {}
    for f in sorted(package.rglob("*")):
        if not f.is_file() or f.is_symlink() or not elf(f):
            continue
        count += 1
        dynamic = subprocess.check_output(["readelf", "-d", str(f)], text=True, stderr=subprocess.DEVNULL)
        if "(NEEDED)" not in dynamic and "(SONAME)" not in dynamic:
            static.append(f.relative_to(package).as_posix())   # needs nothing at all
            continue
        for line in dynamic.splitlines():
            if "(NEEDED)" in line:
                name = line.split("[", 1)[1].split("]", 1)[0]
                if "/" in name:   # by path ($ORIGIN): must stay inside the package
                    target = Path(name.replace("$ORIGIN", str(f.parent))).resolve()
                    if not target.is_file() or not target.is_relative_to(package.resolve()):
                        raise RuntimeError(f"Library path outside the bundle: {f.relative_to(package)}: {name}")
                elif name not in system and name not in private:
                    raise RuntimeError(f"Non-system library outside the bundle: {f.relative_to(package)}: {name}")
            if "(RPATH)" in line or "(RUNPATH)" in line:
                for entry in line.split("[", 1)[1].split("]", 1)[0].split(":"):
                    if not entry or entry.startswith("$ORIGIN"):
                        continue
                    # CPython's Tcl/Tk keep their build folder (/tools/deps/lib):
                    # tkinter is unused, and the folder exists on no machine.
                    if Path(entry).exists() or not f.name.startswith(("libtcl", "libtk")):
                        raise RuntimeError(f"Absolute library path in bundle: {f.relative_to(package)}: {entry}")
                    foreign_rpaths[f.relative_to(package).as_posix()] = entry
        symbols = subprocess.check_output(["objdump", "-T", str(f)], text=True, errors="replace",
                                          stderr=subprocess.DEVNULL)
        for word in symbols.split():
            word = word.strip("()")
            if word.startswith("GLIBC_2."):
                version = tuple(int(x) for x in word[6:].split(".")[:2] if x.isdigit())
                if len(version) == 2:
                    glibc = max(glibc, version)
    audit = {
        "elf_files": count,
        "system_libraries_only_outside_package": sorted(system),
        "minimum_glibc": "%d.%d" % glibc,
        "without_dependencies": static,
        "unused_build_rpaths": foreign_rpaths,
        "wheels_platform": MANYLINUX,
    }
    (package / "runtime/linux-library-audit.json").write_text(json.dumps(audit, indent=2) + "\n")


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


def project_files(out):
    """Only project files intended for distribution, no local datasets/credentials."""
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


def seal(out, target, py, runtime_source):
    """bundle.json and PACKAGE-INFO.json over the package's files."""
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
        "python": py,
        "candidate": json.loads((out / "eboot/release.json").read_text())["candidate"],
        "files": files,
        "runtime_source": runtime_source,
        "runtime_installation_required": False,
    }
    (out / "PACKAGE-INFO.json").write_text(
        json.dumps(
            {
                "platform": target,
                "runtime": "CPython 3.12.14",
                "candidate": manifest["candidate"],
                "files": len(files) + 1,
            },
            indent=2,
        )
        + "\n"
    )
    manifest["files"]["PACKAGE-INFO.json"] = sha(out / "PACKAGE-INFO.json")
    (out / "bundle.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def build_from(args):
    """A package over an earlier package's runtime and helper."""
    prev = Path(args.runtime_from).resolve()
    old = json.loads((prev / "bundle.json").read_text())
    target = old["platform"]
    out = Path(args.output).resolve()
    if out.exists():
        raise RuntimeError("Use a new empty bundle destination: " + str(out))
    out.mkdir(parents=True)
    project_files(out)
    shutil.copytree(prev / "runtime", out / "runtime", symlinks=False)
    ext = ".exe" if target.startswith("windows") else ""
    shutil.copy2(prev / ("nfsu2-generator" + ext), out / ("nfsu2-generator" + ext))
    for p in list(out.rglob("__pycache__")):
        if p.is_dir():
            shutil.rmtree(p)
    manifest = seal(out, target, old["python"], old["runtime_source"])
    same = sum(1 for k, v in manifest["files"].items() if k.startswith("runtime/") and old["files"].get(k) == v)
    if same != sum(1 for k in old["files"] if k.startswith("runtime/")):
        raise RuntimeError("The copied runtime differs from the earlier package's.")
    print(json.dumps({"package": str(out), "platform": target, "files": len(manifest["files"]),
                      "runtime_from": str(prev)}), flush=True)
    return out


def build(args):
    if args.runtime_from:
        return build_from(args)
    if os.name == "nt":
        target = "windows-x64"
    elif sys.platform == "linux":
        if platform.machine() != "x86_64":
            raise RuntimeError("Linux packages are built on x86-64.")
        target = "linux-x64"
    else:
        target = "macos-" + ("arm64" if platform.machine() == "arm64" else "x64")
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
    pin = ["--python-platform", MANYLINUX] if target == "linux-x64" else []
    run(
        ["uv", "pip", "install", "--python", py, *pin, "-r", ROOT / "requirements.txt"],
        env=env,
    )
    run(
        [
            "uv",
            "pip",
            "install",
            "--python",
            py,
            *pin,
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
    compat = tool_dir / ("lame-compat" + ext) if tool_dir else None
    if os.name == "nt" or target == "linux-x64":
        if not (compat and compat.is_file()):
            raise RuntimeError("Build-time native tool missing: lame-compat")
        shutil.copy2(compat, bin_dir / compat.name)
        originals[bin_dir / compat.name] = compat.resolve()
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
    else:
        vendor_linux(out)
    # Some vendored libraries arrive read-only (0444). Testers must be able to
    # delete/replace the folder, and macOS cannot clear quarantine on them.
    for p in out.rglob("*"):
        if p.is_file() and not p.is_symlink():
            p.chmod(p.stat().st_mode | 0o200)
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
    p.add_argument("--helper")
    p.add_argument("--runtime-from", help="an extracted earlier package of the same platform: its runtime and helper")
    args = p.parse_args()
    if not args.helper and not args.runtime_from:
        p.error("--helper is required without --runtime-from")
    build(args)
