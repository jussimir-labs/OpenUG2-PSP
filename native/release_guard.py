#!/usr/bin/env python3
"""Fail-closed checks for English project text and neutral release metadata.

Check an extracted distribution or the actual ZIP and every attached text file.
Policy signatures avoid reproducing restricted vocabulary in published source.
This is a deterministic vocabulary check, not an unrestricted language model.
"""
import argparse
import ast
import hashlib
from functools import lru_cache
import html
import io
import json
from pathlib import Path, PurePosixPath
import re
import struct
import tarfile
import unicodedata
import zipfile
import zlib
import lzma
import bz2

POLICY = Path(__file__).with_name('release_policy.json')
MAX_FILE = 512 * 1024 * 1024
MAX_TOTAL = 8 * 1024 * 1024 * 1024
MAX_DEPTH = 4
TEXT = {'.py', '.rs', '.c', '.h', '.cpp', '.js', '.ts', '.css', '.html', '.md',
        '.txt', '.json', '.yaml', '.yml', '.toml', '.sh', '.bat', '.command', '.xml', '.csv'}
WORDS = re.compile(r'[^\W_]+', re.UNICODE)
PRINTABLE = re.compile(rb'[\x20-\x7e]{4,}')


@lru_cache(maxsize=131072)
def fingerprint(text):
    return hashlib.sha256(unicodedata.normalize('NFKC', text).casefold().encode()).hexdigest()


class ReleaseCheckError(RuntimeError):
    pass


class Checker:
    def __init__(self, policy=POLICY, max_file=MAX_FILE, max_total=MAX_TOTAL):
        data = json.loads(Path(policy).read_text())
        if data.get('schema') != 1:
            raise ReleaseCheckError('Unsupported release text policy')
        for key in ('restricted_tokens', 'portuguese_strong', 'portuguese_context', 'blocked_path_components'):
            if not data.get(key) or any(not re.fullmatch(r'[0-9a-f]{64}', v) for v in data[key]):
                raise ReleaseCheckError('Missing or invalid release policy signatures')
        self.terms = set(data['restricted_tokens'])
        self.strong = set(data['portuguese_strong'])
        self.context = set(data['portuguese_context'])
        self.phrases = set(data['restricted_phrases'])
        self.hints = set(data['phrase_hints'])
        self.paths = set(data['blocked_path_components'])
        self.exceptions = data['reviewed_files']
        self.max_file, self.max_total = max_file, max_total
        self.total = self.files = 0
        self.issues, self.cache = [], set()

    def issue(self, path, kind, detail):
        record = {'path': path, 'kind': kind, 'detail': detail}
        if record not in self.issues:
            self.issues.append(record)

    def allowed(self, path, digest):
        path = path.replace('!', '/').replace('::', '/')
        return {token for row in self.exceptions
                if digest == row['sha256'] and (path == row['path'] or path.endswith('/' + row['path']))
                for token in row['tokens']}

    def text(self, path, text, allowed, project=True, binary=False):
        text = html.unescape(unicodedata.normalize('NFKC', text))
        for number, line in enumerate(text.splitlines(), 1):
            signatures = set()
            for token in WORDS.findall(line):
                lower = token.casefold()
                if binary and len(lower) < 4 and not re.search(r'powered|assistant|generat|model|chat|[0-9]', line, re.I):
                    continue
                signature = fingerprint(lower)
                versionless = fingerprint(re.sub(r'\d+$', '', lower))
                if (signature in self.terms and signature not in allowed) or (versionless in self.terms and versionless not in allowed):
                    self.issue(path, 'restricted-reference', f'Line {number}: restricted development reference')
                signatures.add(signature)
            tokens = [w.casefold() for w in WORDS.findall(line)]
            if signatures & self.hints:
                if any(fingerprint(' '.join(tokens[i:i+n])) in self.phrases
                       for n in (2, 3, 4) for i in range(len(tokens)-n+1)):
                    self.issue(path, 'restricted-reference', f'Line {number}: restricted development attribution')
            if project:
                # CLI compatibility spellings and filesystem identifiers are not prose.
                prose = re.sub(r'--[\w-]+|(?:[\w.-]+/)+[\w.-]*|[\w-]+\.[A-Za-z0-9]+|\.[\w-]+', '', line)
                terms = {fingerprint(w) for w in WORDS.findall(prose)} - allowed
                if terms & self.strong or len(terms & self.context) >= 4:
                    self.issue(path, 'language', f'Line {number}: project text must be English')

    def member(self, path, data, depth=0):
        self.files += 1
        self.total += len(data)
        if len(data) > self.max_file or self.total > self.max_total:
            self.issue(path, 'unreadable', 'Inspection size limit exceeded')
            return
        self.text(path + ':name', path, set(), False)
        for component in path.replace('!', '/').split('/'):
            if fingerprint(component) in self.paths or fingerprint(PurePosixPath(component).stem) in self.paths:
                self.issue(path, 'language', 'Localized project path is not allowed')
        if re.search(r'(?:^|[._/!-])pt[-_]?br(?:$|[._/!-])', path, re.I):
            self.issue(path, 'language', 'Localized project path is not allowed')
        digest = hashlib.sha256(data).hexdigest()
        allowed = self.allowed(path, digest)
        project = True
        suffix = PurePosixPath(path).suffix.lower()
        key = (digest, suffix, project, tuple(sorted(allowed)))
        if key in self.cache:
            return
        self.cache.add(key)
        try:
            archive = data.startswith(b'PK\x03\x04') or suffix in ('.zip', '.whl', '.docx', '.xlsx', '.pptx')
            tar = path.lower().endswith(('.tar', '.tar.gz', '.tar.xz', '.tgz', '.tar.bz2'))
            if archive or tar:
                if depth >= MAX_DEPTH:
                    raise ReleaseCheckError('Archive nesting limit exceeded')
                if archive:
                    with zipfile.ZipFile(io.BytesIO(data)) as z:
                        seen = set()
                        for info in z.infolist():
                            if info.filename in seen:
                                raise ReleaseCheckError('Duplicate archive member')
                            seen.add(info.filename)
                            self.safe_name(info.filename)
                            if info.flag_bits & 1:
                                raise ReleaseCheckError('Encrypted archive cannot be inspected')
                            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                                raise ReleaseCheckError('ZIP symlink requires explicit review')
                            if info.is_dir():
                                continue
                            self.check_size(info.file_size)
                            self.member(path + '!' + info.filename, z.read(info), depth + 1)
                else:
                    with tarfile.open(fileobj=io.BytesIO(data), mode='r:*') as t:
                        for info in t:
                            self.safe_name(info.name)
                            if not info.isfile() and not info.isdir():
                                raise ReleaseCheckError('TAR links and special members require explicit review')
                            if info.isfile():
                                self.check_size(info.size)
                                self.member(path + '!' + info.name, t.extractfile(info).read(), depth + 1)
                return
            if suffix in ('.7z', '.rar', '.zst'):
                raise ReleaseCheckError('Unsupported archive format')
            if suffix in ('.gz', '.xz', '.bz2'):
                if depth >= MAX_DEPTH:
                    raise ReleaseCheckError('Archive nesting limit exceeded')
                if suffix == '.gz':
                    decoder = zlib.decompressobj(31)
                    expanded = decoder.decompress(data, self.max_file + 1)
                elif suffix == '.xz':
                    decoder = lzma.LZMADecompressor()
                    expanded = decoder.decompress(data, max_length=self.max_file + 1)
                else:
                    decoder = bz2.BZ2Decompressor()
                    expanded = decoder.decompress(data, max_length=self.max_file + 1)
                if len(expanded) > self.max_file or not decoder.eof:
                    raise ReleaseCheckError('Compressed stream exceeds inspection limit')
                self.member(path[:-len(suffix)], expanded, depth + 1)
                return
            if data.startswith(b'\x89PNG\r\n\x1a\n'):
                self.png(path, data, allowed, project)
                return
            if data.startswith(b'\xff\xd8'):
                self.jpeg(path, data, allowed, project)
                return
            if data.startswith(b'%PDF-') or suffix == '.pdf':
                try:
                    from pypdf import PdfReader
                except ImportError as exc:
                    raise ReleaseCheckError('Install native/release-requirements.txt to inspect PDFs') from exc
                reader = PdfReader(io.BytesIO(data), strict=True)
                if reader.is_encrypted:
                    raise ReleaseCheckError('Encrypted PDF cannot be inspected')
                self.text(path + ':metadata', str(reader.metadata), allowed, project)
                for i, page in enumerate(reader.pages, 1):
                    self.text(f'{path}:page-{i}', page.extract_text() or '', allowed, project)
                for name, values in reader.attachments.items():
                    if depth >= MAX_DEPTH:
                        raise ReleaseCheckError('PDF attachment nesting limit exceeded')
                    for payload in values:
                        self.check_size(len(payload))
                        self.member(path + '!' + name, payload, depth + 1)
                return
            if suffix in TEXT or not suffix or b'\0' not in data[:8192]:
                try:
                    text = data.decode('utf-8-sig')
                except UnicodeDecodeError:
                    if suffix in TEXT:
                        raise ReleaseCheckError('Text is not valid UTF-8')
                else:
                    self.text(path, text, allowed, project)
                    if suffix == '.json':
                        def strings(obj):
                            if isinstance(obj, str):
                                self.text(path + ':decoded', obj, allowed, project)
                            elif isinstance(obj, dict):
                                for k, v in obj.items():
                                    strings(k)
                                    strings(v)
                            elif isinstance(obj, list):
                                for v in obj:
                                    strings(v)
                        strings(json.loads(text))
                    elif suffix == '.py':
                        for node in ast.walk(ast.parse(text)):
                            if isinstance(node, ast.Constant) and isinstance(node.value, str):
                                self.text(f'{path}:literal-{node.lineno}', node.value, allowed, project)
                    return
            # Binary strings include executable paths, EXIF/XMP and other metadata.
            for match in PRINTABLE.finditer(data):
                self.text(path + f':byte-{match.start()}', match.group().decode('ascii'), allowed, project, True)
            for encoding, pattern in (('utf-16-le', rb'(?:[\x20-\x7e]\0){4,}'), ('utf-16-be', rb'(?:\0[\x20-\x7e]){4,}')):
                for match in re.finditer(pattern, data):
                    self.text(path + f':byte-{match.start()}', match.group().decode(encoding), allowed, project, True)
        except Exception as exc:
            self.issue(path, 'unreadable', f'Cannot complete inspection: {exc}')

    def png(self, path, data, allowed, project):
        pos = 8
        ended = False
        while pos < len(data):
            if pos + 12 > len(data):
                raise ReleaseCheckError('Truncated PNG chunk')
            size, tag = struct.unpack('>I4s', data[pos:pos + 8])
            end = pos + 12 + size
            if end > len(data):
                raise ReleaseCheckError('Truncated PNG payload')
            chunk = data[pos + 8:pos + 8 + size]
            if zlib.crc32(tag + chunk) & 0xffffffff != struct.unpack('>I', data[pos + 8 + size:end])[0]:
                raise ReleaseCheckError('PNG chunk checksum mismatch')
            if tag == b'caBX':
                self.issue(path, 'provenance', 'Unreviewed content provenance metadata')
            elif tag == b'tEXt':
                self.text(path + ':text', chunk.decode('latin1'), allowed, project)
            elif tag == b'zTXt':
                _, compressed = chunk.split(b'\0', 1)
                if compressed[:1] != b'\0':
                    raise ReleaseCheckError('Unsupported PNG text compression')
                self.text(path + ':text', self.inflate(compressed[1:]).decode('latin1'), allowed, project)
            elif tag == b'iTXt':
                _, rest = chunk.split(b'\0', 1)
                flag, method = rest[0], rest[1]
                _, _, text = rest[2:].split(b'\0', 2)
                if flag not in (0, 1) or method:
                    raise ReleaseCheckError('Unsupported PNG international text encoding')
                self.text(path + ':text', (self.inflate(text) if flag else text).decode(), allowed, project)
            elif tag not in (b'IHDR', b'IDAT', b'IEND', b'PLTE'):
                for item in PRINTABLE.finditer(chunk):
                    self.text(path + ':metadata', item.group().decode('ascii'), allowed, project)
            pos = end
            if tag == b'IEND':
                ended = True
                break
        if not ended or pos != len(data):
            raise ReleaseCheckError('Invalid PNG termination')

    def jpeg(self, path, data, allowed, project):
        pos = 2
        while pos < len(data):
            if data[pos] != 255:
                raise ReleaseCheckError('Invalid JPEG metadata marker')
            while pos < len(data) and data[pos] == 255:
                pos += 1
            marker = data[pos]
            pos += 1
            if marker in (0xda, 0xd9):
                return  # Compressed image pixels are not textual metadata.
            if marker in range(0xd0, 0xd9) or marker == 1:
                continue
            if pos + 2 > len(data):
                raise ReleaseCheckError('Truncated JPEG metadata')
            size = int.from_bytes(data[pos:pos+2], 'big')
            if size < 2 or pos + size > len(data):
                raise ReleaseCheckError('Truncated JPEG segment')
            payload = data[pos+2:pos+size]
            if marker == 0xeb:
                self.issue(path, 'provenance', 'Unreviewed image provenance metadata')
            if marker == 0xfe:
                self.text(path + ':comment', payload.decode('utf-8', errors='replace'), allowed, project)
            elif 0xe0 <= marker <= 0xef:
                for item in PRINTABLE.finditer(payload):
                    self.text(path + ':metadata', item.group().decode('ascii'), allowed, project)
            pos += size
        raise ReleaseCheckError('Unterminated JPEG metadata')

    def inflate(self, data):
        decoder = zlib.decompressobj()
        out = decoder.decompress(data, self.max_file + 1)
        if len(out) > self.max_file or not decoder.eof or decoder.unused_data:
            raise ReleaseCheckError('Compressed metadata exceeds inspection limit')
        return out

    def check_size(self, size):
        if size > self.max_file or self.total + size > self.max_total:
            raise ReleaseCheckError('Archive exceeds inspection size limit')

    @staticmethod
    def safe_name(name):
        if '\\' in name or PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts:
            raise ReleaseCheckError('Unsafe archive path')

    def check(self, paths):
        if not paths:
            self.issue('', 'unreadable', 'No distribution inputs provided')
        for value in paths:
            path = Path(value)
            if path.is_dir():
                root = path.resolve()
                for item in sorted(path.rglob('*')):
                    relative = item.relative_to(path)
                    if any(p in ('.git', '.tools', '__pycache__') for p in relative.parts) or relative.parts[:2] in (('native', 'target'), ('native', 'dist'), ('native', '.cache')):
                        continue
                    if not item.is_file():
                        continue
                    try:
                        if not item.resolve().is_relative_to(root):
                            raise ReleaseCheckError('Linked file escapes distribution folder')
                        self.check_size(item.stat().st_size)
                        self.member((Path(path.name) / relative).as_posix(), item.read_bytes())
                    except (OSError, ReleaseCheckError) as exc:
                        self.issue(str(relative), 'unreadable', str(exc))
            else:
                try:
                    self.check_size(path.stat().st_size)
                    self.member(path.name, path.read_bytes())
                except (OSError, ReleaseCheckError) as exc:
                    self.issue(str(path), 'unreadable', str(exc))
        return {'schema': 1, 'passed': not self.issues, 'files_checked': self.files,
                'bytes_inspected': self.total, 'findings': self.issues}


def require_clean(*paths):
    result = Checker().check(paths)
    if not result['passed']:
        details = '\n'.join(f"{f['path']}: {f['detail']}" for f in result['findings'][:30])
        raise ReleaseCheckError('Release content check failed:\n' + details)
    return result


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('paths', nargs='*', type=Path)
    p.add_argument('--report', type=Path)
    p.add_argument('--fingerprint', metavar='TOKEN')
    args = p.parse_args(argv)
    if args.fingerprint is not None:
        print(fingerprint(args.fingerprint))
        return 0
    if not args.paths:
        p.error('Provide a package folder, ZIP or release attachment')
    result = Checker().check(args.paths)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    for issue in result['findings']:
        print(f"{issue['path']}: {issue['detail']}")
    print(f"Release content check: {'PASS' if result['passed'] else 'BLOCKED'} ({result['files_checked']} files)")
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
