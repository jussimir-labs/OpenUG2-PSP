"""The Xbox disc (XDVDFS), the source the converters were written for.

The whole disc is extracted as it is into game/xbox (the converters read
game/xbox/NFSUNDER and game/xbox/default.xbe) and each file is checked
against xbox-files.json, the sizes and SHA-256 of the NTSC-U disc. An
image of another layout (a full Redump dump with the video partition, a
rebuilt xiso) has another hash but the same files, and passes.
"""
import hashlib
import json
import sys
import time
from pathlib import Path

from . import SourceError

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'third_party/xdvdfs'))
from xdvdfs import Xiso, XisoError  # noqa: E402

FILES = HERE / 'xbox-files.json'


class Source:
    name = 'xbox'
    available = True
    note = ''
    known_isos = {
        '769d86d47798d8702cccd5d74743456aca7bac74b2042c5ed67ac6647df0fdbd':
            'Need for Speed Underground 2 (USA), Xbox, game partition image (2,724,986,880 bytes)',
    }

    def __init__(self):
        self.expected = json.loads(FILES.read_text())['files']
        self.file_count = len(self.expected)

    def recognise(self, iso):
        try:
            with Xiso(str(iso)):
                return True
        except (XisoError, OSError):
            return False

    def extract(self, iso, dest, say):
        try:
            image = Xiso(str(iso))
        except XisoError as e:
            raise SourceError(f'{iso.name} is not an Xbox disc image ({e}).')
        with image:
            entries = list(image.walk())
            total = sum(e.size for _, e in entries) or 1
            size = iso.stat().st_size
            done, last = 0, 0
            for directory, entry in entries:
                rel = Path(directory) / entry.name
                if rel.is_absolute() or '..' in rel.parts:
                    raise SourceError(f'unsafe ISO path: {rel}')
                if entry.sector * 2048 + entry.size + image.base > size:
                    raise SourceError(f'{rel} extends past the end of the ISO: incomplete image.')
                out = dest / rel
                image.extract(entry, str(out))
                if out.stat().st_size != entry.size:
                    raise SourceError(f'{rel} has the wrong extracted size: corrupted image.')
                done += entry.size
                if time.monotonic() - last > 2:
                    last = time.monotonic()
                    print(f'\r  extracting: {done * 100 // total}% ({len(entries)} files)', end='', flush=True)
        say(f'\r  extracted {len(entries)} files, {total / 2**30:.1f} GB')

    def verify(self, dest):
        bad = []
        for rel, (size, digest) in sorted(self.expected.items()):
            p = dest / rel
            if not p.is_file() or p.stat().st_size != size:
                bad.append(rel)
                continue
            h = hashlib.sha256()
            with open(p, 'rb') as f:
                while chunk := f.read(8 << 20):
                    h.update(chunk)
            if h.hexdigest() != digest:
                bad.append(rel)
        return bad

    def game_root(self, dest):
        return dest / 'NFSUNDER'
