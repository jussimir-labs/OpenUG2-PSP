#!/usr/bin/env python3
"""Native package integration tests. No game data or network downloads required."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
import zipfile

from release_guard import require_clean
from build_bundle import package_member


def fixture_probe(root):
    sys.path.insert(0, str(root / "tools"))
    import native_runtime as n
    import convert_audio

    # Execute the compiled numerical libraries used by racing-line conversion.
    # Imports alone do not exercise their BLAS/solver DLLs on a clean Windows host.
    import numpy as np
    from scipy import linalg
    import quadprog

    np.testing.assert_allclose(
        np.linalg.svd(np.diag([3.0, 1.0]), compute_uv=False),
        [3.0, 1.0],
        rtol=0,
        atol=1e-12,
    )
    np.testing.assert_allclose(
        linalg.svdvals(np.diag([3.0, 1.0])), [3.0, 1.0], rtol=0, atol=1e-12
    )
    optimum = quadprog.solve_qp(
        np.eye(2), np.array([1.0, 2.0]), np.eye(2), np.zeros(2)
    )[0]
    np.testing.assert_allclose(optimum, [1.0, 2.0], rtol=0, atol=1e-12)
    from PIL import Image

    def execute(name, *args, **kw):
        return subprocess.run(
            [str(n.binary(name)), *map(str, args)],
            check=True,
            capture_output=True,
            timeout=30,
            **kw,
        )

    # Deliberately include the bytes Windows text-mode streams transform/terminate.
    pcm = [-32768, -10, 10, 13, 26, 32767, 0, 257] * 19
    raw = struct.pack("<" + "h" * len(pcm), *pcm)
    encoded = execute("xaencode", input=raw).stdout
    assert encoded == convert_audio.encode_xa(pcm), (
        "EA-XA differs from the Python reference"
    )
    with tempfile.TemporaryDirectory(prefix="native QA ☃ ") as tmp:
        tmp = Path(tmp).resolve()
        wave = tmp / "input audio.raw"
        wave.write_bytes(raw)
        resampled = execute(
            "ffmpeg",
            "-v",
            "error",
            "-f",
            "s16le",
            "-ar",
            "32000",
            "-ac",
            "1",
            "-i",
            wave,
            "-ar",
            "44100",
            "-f",
            "s16le",
            "-",
        ).stdout
        assert len(resampled) > len(raw)
        # The converters stream PCM through stdin/stdout ("-": FFmpeg's fd:
        # protocol); a file-only check missed a build without it.
        piped = execute(
            "ffmpeg",
            "-v",
            "error",
            "-f",
            "s16le",
            "-ar",
            "32000",
            "-ac",
            "1",
            "-i",
            "-",
            "-ar",
            "44100",
            "-f",
            "s16le",
            "-",
            input=raw,
        ).stdout
        assert piped == resampled, "FFmpeg stdin/stdout output differs"
        lame_args = [
            "--quiet",
            "-r",
            "-s",
            "32",
            "--bitwidth",
            "16",
            "--signed",
            "--little-endian",
            "-m",
            "m",
            "-",
            "-",
        ]
        mp3 = execute("lame", *lame_args, input=raw).stdout
        assert len(mp3) > 100 and mp3[:1] == b"\xff", "LAME did not produce an MP3"
        # The RC21 reference MP3s were encoded on Apple Silicon (FMA). Every
        # package must reproduce those bytes, not merely produce an MP3.
        assert (
            hashlib.sha256(mp3).hexdigest()
            == "0a37bf99e6443473672e9058bad29e190e64b45af1b36441f711fd9c30ccf7be"
        ), "LAME output differs from the Apple Silicon reference encoder"
        if os.name == "nt" or sys.platform == "linux":
            compat = subprocess.run(
                [str(Path(root) / "runtime/bin" / ("lame-compat" + (".exe" if os.name == "nt" else ""))), *lame_args],
                input=raw,
                check=True,
                capture_output=True,
                timeout=30,
            ).stdout
            assert len(compat) > 100 and compat[:1] == b"\xff", "LAME fallback failed"
        im = Image.new("RGB", (32, 32))
        im.putdata([(i * 19 % 256, i * 41 % 256, i * 17 % 256) for i in range(1024)])
        ppm = tmp / "image ☃.ppm"
        jpg = tmp / "image ☃.jpg"
        im.save(ppm)
        execute("cjpegli", ppm, jpg, "-p", "0", "--chroma_subsampling=420", "-d", "1.0")
        with Image.open(jpg) as result:
            assert result.size == (32, 32)
        jpg_data = jpg.read_bytes()

        def chunk(tag, data=b""):
            return tag + struct.pack("<I", 8 + len(data)) + data

        samples = pcm[:28]
        frame = b"\xee" + b"\0" * 4 + struct.pack(">28h", *samples)
        sound = chunk(
            b"SCDl", struct.pack(">III", 28, 0, len(frame)) + frame + frame
        ) + chunk(b"SCEl")
        sch = tmp / "music ☃.sch"
        sch.write_bytes(sound)
        decoded = execute("eaxa2pcm", sch, 0).stdout
        assert decoded == struct.pack(
            "<56h", *[x for sample in samples for x in (sample, sample)]
        ), "EA-XA binary output changed"
        header = b"GSTR" + b"\0" * 4 + b"\x82\x01\x02\x84\x02\x7d\x00\xff"
        movie = tmp / "movie ☃.vp6"
        out = tmp / "audio ☃.raw"
        movie.write_bytes(chunk(b"SCHl", header) + sound)
        execute("movie_audio", movie, out, 32000)
        movie_data = out.read_bytes()
        assert len(movie_data) == 28 * 4, "Movie audio size differs"
        bad = subprocess.run(
            [str(n.binary("mt2pcm")), "28"],
            input=b"invalid\r\n\x1a",
            capture_output=True,
            timeout=10,
        )
        assert bad.returncode != 0, "Truncated MicroTalk stream was accepted"
        # Exercise final RC21 packaging with synthetic data and the actual frozen EBOOT.
        import data_id, ms_package
        from unittest.mock import patch

        stage = tmp / "stage"
        stage.mkdir()
        (stage / "fixture.n2ph").write_bytes(b"N2PH synthetic fixture\0\x0a\x1a")
        store = tmp / "hash-cache"
        store.mkdir()
        with patch.object(data_id, "store", return_value=store):
            ident = data_id.write(stage, stage)
            assert (stage / "data-id.txt").read_bytes() == (ident + "\n").encode(), (
                "Windows newline translation changed game data"
            )
            ms_package.make(
                tmp / "output",
                stage,
                root / "eboot/EBOOT.PBP",
                [],
                patch_bars=False,
                pack=True,
                release=True,
            )
            final = tmp / "output/PSP/GAME/NFSU2VIEW"
            data_id.normalize_package(final, owned=True)
            pack_digest = hashlib.sha256((final / "pack.n2pk").read_bytes()).hexdigest()
    return {
        "xaencode_sha256": hashlib.sha256(encoded).hexdigest(),
        "ffmpeg_pcm_sha256": hashlib.sha256(resampled).hexdigest(),
        "lame_mp3_sha256": hashlib.sha256(mp3).hexdigest(),
        "eaxa_pcm_sha256": hashlib.sha256(decoded).hexdigest(),
        "jpeg_sha256": hashlib.sha256(jpg_data).hexdigest(),
        "movie_pcm_sha256": hashlib.sha256(movie_data).hexdigest(),
        "pack_sha256": pack_digest,
        "native_numerical_libraries": True,
        "synthetic_fixtures_only": True,
    }


def archive_package(original):
    original = Path(original)
    manifest = json.loads((original / "bundle.json").read_text())
    manifest["files"] = {name: digest for name, digest in manifest["files"].items()
                         if package_member(name)}
    manifest["files"]["native-qa.json"] = hashlib.sha256((original / "native-qa.json").read_bytes()).hexdigest()
    (original / "bundle.json").write_text(json.dumps(manifest, indent=2) + "\n")
    require_clean(original)
    zipped = original.with_suffix(".zip")
    staged = zipped.with_suffix(".zip.partial")
    try:
        with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED, compresslevel=6, strict_timestamps=False) as archive:
            for file in sorted(original.rglob("*")):
                if file.is_file() and package_member(file.relative_to(original)):
                    archive.write(file, original.name + "/" + file.relative_to(original).as_posix())
        require_clean(staged)
        staged.replace(zipped)
    finally:
        staged.unlink(missing_ok=True)
    with zipped.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    zipped.with_suffix(".zip.sha256").write_text(digest + "  " + zipped.name + "\n", newline="\n")
    return zipped


def test(root, archive=False):
    # Relocation tests every absolute library reference, Python prefix, and quoting.
    original = root.resolve()
    root = original.with_name(original.name + " QA ☃")
    if root.exists():
        raise RuntimeError("Relocation destination already exists")
    original.rename(root)
    results = {}
    proc = None
    try:
        manifest = json.loads((root / "bundle.json").read_text())
        exe = root / ("nfsu2-generator.exe" if os.name == "nt" else "nfsu2-generator")
        clean = dict(
            os.environ,
            PYTHONHOME="invalid-global-python",
            PYTHONPATH="invalid-global-modules",
            VIRTUAL_ENV="invalid-global-venv",
            # Package QA never asks GitHub for a newer release.
            NFSU2_UPDATES="0",
        )
        clean["PATH"] = (
            str(root / "runtime/bin")
            + os.pathsep
            + (
                str(Path(os.environ["SystemRoot"]) / "System32")
                if os.name == "nt"
                else "/usr/bin:/bin:/usr/sbin:/sbin"
            )
        )
        doctor = subprocess.run(
            [exe, "doctor"], env=clean, capture_output=True, text=True, timeout=120
        )
        assert doctor.returncode == 0, doctor.stdout + doctor.stderr
        results["doctor"] = json.loads(doctor.stdout.strip().splitlines()[-1])
        assert results["doctor"]["ready"]
        # Execute fixtures using the shipped interpreter and tools, with no compiler.
        env = dict(
            clean,
            NFSU2_BUNDLE_ROOT=str(root),
            PYTHONNOUSERSITE="1",
            PYTHONUTF8="1",
            PYTHONDONTWRITEBYTECODE="1",
        )
        for key in ["PYTHONHOME", "PYTHONPATH", "VIRTUAL_ENV"]:
            env.pop(key, None)
        fixture = subprocess.run(
            [
                root / manifest["python"],
                root / "native/test_bundle.py",
                "--fixture-probe",
                root,
            ],
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=120,
        )
        assert fixture.returncode == 0, fixture.stdout + fixture.stderr
        results["converters"] = json.loads(fixture.stdout.strip().splitlines()[-1])
        tests = subprocess.run(
            [
                root / manifest["python"],
                "-m",
                "unittest",
                "discover",
                "-s",
                str(root / "tests"),
                "-v",
            ],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=90,
        )
        assert tests.returncode == 0, tests.stdout + tests.stderr
        results["python_tests"] = tests.stderr.strip().splitlines()[-4:]
        proc = subprocess.Popen(
            [exe, "serve", "--no-browser", "--port", "0"],
            env=clean,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
        )
        url = None
        for line in proc.stdout:
            if line.startswith("Local interface: "):
                url = line.strip().split("Local interface: ", 1)[1]
                break
        assert url, "Native launcher did not publish a local interface"
        base, token = url.split("/#token=")
        headers = {"X-NFSU2-Token": token}

        def request(path, body=None, extra=None):
            return urllib.request.urlopen(
                urllib.request.Request(
                    base + path,
                    data=json.dumps(body).encode() if body is not None else None,
                    headers={**headers, **(extra or {})},
                ),
                timeout=15,
            )

        with request("/api/state") as response:
            assert json.load(response)["status"] == "idle"
        try:
            request("/api/state", extra={"Origin": "https://foreign.example"})
            raise AssertionError("Foreign origin accepted")
        except urllib.error.HTTPError as exc:
            assert exc.code == 403
            exc.close()
        with request("/") as response:
            assert (
                b"<!DOCTYPE html" in response.read()
                or b"<!doctype html" in request("/").read()
            )
        def update_state():
            # launcher.lock is the running launcher's lease: Windows keeps it
            # locked against reads, and its bytes are not update state.
            return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (root / '.updates').rglob('*') if p.is_file() and p.name != 'launcher.lock'}
        before_duplicate = update_state()
        # A listening local sentinel records even a connection attempt; it
        # serves no credentials and requires no external network access.
        with socket.socket() as sentinel:
            sentinel.bind(('127.0.0.1', 0)); sentinel.listen(); sentinel.settimeout(.05)
            endpoint = 'http://127.0.0.1:' + str(sentinel.getsockname()[1])
            duplicate = subprocess.run(
                [exe, "serve", "--no-browser"],
                env=dict(clean, NFSU2_UPDATES='1', NFSU2_UPDATE_API=endpoint,
                         NFSU2_UPDATE_WEB=endpoint, NFSU2_GITHUB_CLIENT_ID='test-client'),
                capture_output=True, text=True, timeout=30,
            )
            try:
                connection, _ = sentinel.accept()
            except socket.timeout:
                pass
            else:
                connection.close()
                raise AssertionError('Busy second instance attempted an update connection')
        assert duplicate.returncode == 76 and "BUSY:" in duplicate.stderr, (
            duplicate.stdout + duplicate.stderr
        )
        assert update_state() == before_duplicate, 'Busy second instance mutated update state'
        results['second_instance'] = 'BUSY/76; update state unchanged; no update connection'
        with request("/api/prepare", {}) as response:
            assert response.status == 200
        until = time.monotonic() + 90
        while time.monotonic() < until:
            with request("/api/state") as response:
                state = json.load(response)
            if not state["busy"]:
                break
            time.sleep(0.1)
        assert state["status"] == "idle" and state["preflight"]["ready"], state
        with request("/api/shutdown", {}) as response:
            assert response.status == 200
        assert proc.wait(timeout=15) == 0
        proc.stdout.close()
        proc = None
        # Damaged/missing bundled files must fail before opening the UI or compiling.
        victim = (
            root / "runtime/bin" / ("xaencode.exe" if os.name == "nt" else "xaencode")
        )
        saved = victim.read_bytes()
        try:
            victim.write_bytes(saved + b"corrupt")
            result = subprocess.run(
                [exe, "doctor"], env=clean, capture_output=True, text=True, timeout=30
            )
            assert result.returncode != 0 and "corrupted" in result.stderr
            victim.unlink()
            result = subprocess.run(
                [exe, "doctor"], env=clean, capture_output=True, text=True, timeout=30
            )
            assert result.returncode != 0 and "missing" in result.stderr.lower()
        finally:
            victim.write_bytes(saved)
            victim.chmod(0o755)
        results.update(
            platform=manifest["platform"],
            generator_commit=manifest.get("generator_commit"),
            generator_version=manifest.get("generator_version"),
            eboot_sha256=manifest["files"]["eboot/EBOOT.PBP"],
            relocated_unicode_path=True,
            restricted_path=True,
            global_python_ignored=True,
            browser_api=True,
            foreign_origin_rejected=True,
            single_instance=True,
            offline_tools_probe=True,
            corruption_rejected=True,
            missing_converter_rejected=True,
            full_iso_generation=False,
        )
    finally:
        if proc:
            if os.name != "nt":
                proc.send_signal(signal.SIGINT)
            else:
                proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            proc.stdout.close()
        root.rename(original)
    (original / "native-qa.json").write_text(json.dumps(results, indent=2) + "\n")
    if archive:
        archive_package(original)
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("root", type=Path)
    p.add_argument("--archive", action="store_true")
    p.add_argument("--fixture-probe", action="store_true")
    a = p.parse_args()
    if a.fixture_probe:
        print(json.dumps(fixture_probe(a.root)))
    else:
        test(a.root, a.archive)
