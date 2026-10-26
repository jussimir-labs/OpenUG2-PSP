"""PS2 NTSC-U disc source. Uses only files from the user's own ISO."""
import hashlib
import json
import sys
from pathlib import Path

from . import SourceError

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
from extract_ps2 import Iso9660, boot_file, extract as extract_disc

class Source:
    name = 'ps2'
    available = True
    note = ''
    known_isos = {}
    executable = 'SLUS_210.65'

    def __init__(self):
        self.expected = json.loads((HERE / 'ps2-files.json').read_text())['files']
        self.file_count = len(self.expected)

    def recognise(self, iso):
        try:
            with Iso9660(iso) as image:
                return bool(boot_file(image))
        except (OSError, ValueError, UnicodeError):
            return False

    def extract(self, iso, dest, say):
        try:
            with Iso9660(iso) as image:
                boot = boot_file(image)
            if boot != self.executable:
                raise SourceError(f'PS2: executável {boot!r} não suportado. Use a edição americana NTSC-U ({self.executable}).')
            dest.mkdir(parents=True, exist_ok=True)
            extract_disc(iso, dest, say)
        except (OSError, ValueError) as exc:
            raise SourceError(f'ISO PS2 inválida ou incompleta: {exc}') from exc

    def verify(self, dest):
        bad = []
        for name, (size, digest) in sorted(self.expected.items()):
            path = dest / name
            if not path.is_file() or path.is_symlink() or path.stat().st_size != size:
                bad.append(name)
                continue
            h = hashlib.sha256()
            with path.open('rb') as f:
                while chunk := f.read(8 << 20):
                    h.update(chunk)
            if h.hexdigest() != digest:
                bad.append(name)
        return bad

    def game_root(self, dest):
        return dest / 'NFSUNDER'
