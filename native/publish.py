#!/usr/bin/env python3
"""Verify every release attachment before uploading an existing beta release.

This command never creates a tag or release. It checks all files before reading
credentials or contacting the server. Use --check-only for a local rehearsal.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from release_guard import ReleaseCheckError, require_clean

REPOSITORY = 'jussimir-labs/OpenUG2-PSP'
# Public, package-only release page the launcher reads without a sign-in
# (native/launcher.json). Its release for each tag is created by hand, and
# its files carry neutral names: <UPDATES_PREFIX>-<platform>.zip.
UPDATES = 'jussimir-labs/cache-2'
UPDATES_PREFIX = 'pack'
ACCOUNT = 'jussimir'


def validate_assets(paths):
    paths = [Path(p) for p in paths]
    if not paths or len({p.name for p in paths}) != len(paths):
        raise ReleaseCheckError('Provide release attachments with unique filenames')
    if not all(p.is_file() for p in paths):
        raise ReleaseCheckError('Every attachment must be an existing file')
    packages = [p for p in paths if p.suffix.lower() == '.zip']
    if not packages:
        raise ReleaseCheckError('Provide at least one ZIP package')
    result = require_clean(*paths)
    digests = {}
    for package in packages:
        with package.open('rb') as stream:
            digests[package.name] = hashlib.file_digest(stream, 'sha256').hexdigest()
    verified = set()
    for path in paths:
        if path.suffix.lower() != '.sha256' and path.name != 'SHA256SUMS.txt':
            continue
        for line in path.read_text().splitlines():
            if not line.strip():
                continue
            match = re.fullmatch(r'([0-9a-fA-F]{64})\s+\*?(.+)', line)
            if not match or match[2] not in digests or match[1].lower() != digests[match[2]]:
                raise ReleaseCheckError('Checksum does not match an attached ZIP: ' + path.name)
            verified.add(match[2])
    if verified != set(digests):
        raise ReleaseCheckError('Attach a matching checksum for every ZIP package')
    return result


def updates_assets(paths, folder):
    """Neutral copies of the ZIPs, each with its own checksum file."""
    out = []
    for path in paths:
        match = re.fullmatch(r'NFSU2-Generator-([a-z0-9]+-[a-z0-9]+)\.zip', path.name)
        if not match:
            continue
        name = f'{UPDATES_PREFIX}-{match[1]}.zip'
        target = Path(folder) / name
        try:
            os.link(path, target)
        except OSError:
            shutil.copyfile(path, target)
        with target.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        with path.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != digest:
                raise ReleaseCheckError('The neutral copy differs from ' + path.name)
        checksum = target.with_name(name + '.sha256')
        checksum.write_text(f'{digest}  {name}\n', newline='\n')
        out += [target, checksum]
    if not out:
        raise ReleaseCheckError('No generator package to copy to the update page')
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('assets', nargs='+', type=Path)
    parser.add_argument('--repo', default=REPOSITORY)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--clobber', action='store_true', help='replace existing attachments after all checks pass')
    parser.add_argument('--updates', action='store_true', help='also upload the same ZIP and checksum files to ' + UPDATES)
    args = parser.parse_args(argv)
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:[-.][A-Za-z0-9.]+)?', args.tag):
        parser.error('Provide a version tag')
    staging = tempfile.TemporaryDirectory(prefix='nfsu2-updates-')
    try:
        result = validate_assets(args.assets)
        print(f"Release content check passed ({result['files_checked']} files)")
        updates = updates_assets(args.assets, staging.name) if args.updates else []
        if updates:
            print('Update page files: ' + ', '.join(p.name for p in updates))
        if args.check_only:
            return 0
        token = subprocess.check_output(['gh', 'auth', 'token', '-h', 'github.com', '-u', ACCOUNT], text=True).strip()
        env = dict(os.environ, GH_TOKEN=token)
        login = subprocess.check_output(['gh', 'api', 'user', '--jq', '.login'], env=env, text=True).strip()
        if login != ACCOUNT:
            raise ReleaseCheckError('Unexpected publishing account')
        command = ['gh', 'release', 'upload', args.tag, *map(str, args.assets), '--repo', args.repo]
        if args.clobber:
            command.append('--clobber')
        subprocess.run(command, env=env, check=True)
        if updates:
            command = ['gh', 'release', 'upload', args.tag, *map(str, updates), '--repo', UPDATES]
            if args.clobber:
                command.append('--clobber')
            subprocess.run(command, env=env, check=True)
        return 0
    except (ReleaseCheckError, OSError, subprocess.SubprocessError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        staging.cleanup()


if __name__ == '__main__':
    raise SystemExit(main())
