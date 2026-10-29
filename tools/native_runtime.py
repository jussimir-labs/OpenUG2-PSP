"""Resolve shipped executables; native bundles never compile or install tools."""

import os
from pathlib import Path
import subprocess
import sys

NAMES = ("ffmpeg", "lame", "cjpegli", "movie_audio", "eaxa2pcm", "mt2pcm", "xaencode")


def bundled():
    return bool(os.environ.get("NFSU2_BUNDLE_ROOT"))


def fma_capable():
    """Bundled x86-64 LAME uses FMA, matching the Apple Silicon reference MP3s.
    Every AVX2 CPU also has FMA3; older CPUs get the generic build."""
    if os.name != "nt":
        try:
            with open("/proc/cpuinfo", encoding="ascii", errors="replace") as f:
                for line in f:
                    if line.startswith("flags"):
                        return {"fma", "avx2"} <= set(line.split(":", 1)[1].split())
        except OSError:
            pass
        return False
    import ctypes

    PF_AVX2_INSTRUCTIONS_AVAILABLE = 40
    return bool(
        ctypes.windll.kernel32.IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)
    )


def binary(name):
    root = os.environ.get("NFSU2_BUNDLE_ROOT")
    if not root:
        return None
    if name not in NAMES:
        raise ValueError("Unknown bundled converter: " + name)
    if name == "lame" and (os.name == "nt" or sys.platform == "linux") and not fma_capable():
        name = "lame-compat"
    path = Path(root) / "runtime/bin" / (name + (".exe" if os.name == "nt" else ""))
    if not path.is_file():
        raise RuntimeError(
            "The download is incomplete: missing "
            + name
            + ". Extract the complete native package again."
        )
    return path.resolve()


def helper(name, destination, sources, flags=()):
    shipped = binary(name)
    if shipped:
        return shipped
    # Compatibility for developer/source-only kits. Native end users never take
    # this branch, and missing packaged helpers must not fall back to a compiler.
    command = ["cc", "-O2", *flags, "-o", str(destination), *(str(p) for p in sources)]
    if os.name != "nt":
        command.append("-lm")
    subprocess.run(command, check=True)
    return Path(destination)


def tool(name):
    return str(binary(name) or name)


def local_path(path):
    """Canonical comparison paths, including Rust's Windows verbatim prefix."""
    resolved = Path(path).expanduser().resolve()
    if os.name == "nt":
        text = str(resolved)
        if text.startswith("\\\\?\\UNC\\"):
            return Path("\\\\" + text[8:])
        if text.startswith("\\\\?\\"):
            return Path(text[4:])
    return resolved
