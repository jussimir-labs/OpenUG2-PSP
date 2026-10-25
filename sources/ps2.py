"""The PS2 disc (ISO 9660), still to come.

The converters read the Xbox's files (game/xbox/NFSUNDER). A PS2 source
has to leave there files the converters take as the Xbox's, or the
converters have to learn the PS2's formats; either way, this module is
the place to plug it in. recognise() already tells a PS2 image apart
(SLUS/SLES in SYSTEM.CNF), so the player gets a clear message instead
of an unknown-image error.
"""
from . import SourceError  # noqa: F401  (for the module that replaces this one)

SECTOR = 2048


class Source:
    name = 'ps2'
    available = False
    note = ('a conversão a partir do disco do PS2 ainda está sendo feita. Por enquanto, use a ISO do Xbox '
            '(Need for Speed Underground 2, NTSC-U).')
    known_isos = {}
    file_count = 0

    def recognise(self, iso):
        """An ISO 9660 image whose root holds a SYSTEM.CNF naming a
        PS2 boot file (cdrom0:\\SLUS_..., SLES_...)."""
        try:
            with open(iso, 'rb') as f:
                f.seek(16 * SECTOR)
                pvd = f.read(SECTOR)
                if pvd[1:6] != b'CD001':
                    return False
                # the root directory's record (at 156): its extent and size
                lba, size = int.from_bytes(pvd[158:162], 'little'), int.from_bytes(pvd[166:170], 'little')
                f.seek(lba * SECTOR)
                root = f.read(min(size, 1 << 16))
                at = 0
                while at < len(root):
                    n = root[at]
                    if n == 0:   # the rest of this sector is padding
                        at = (at // SECTOR + 1) * SECTOR
                        continue
                    name = root[at + 33:at + 33 + root[at + 32]]
                    if name.upper().startswith(b'SYSTEM.CNF'):
                        f.seek(int.from_bytes(root[at + 2:at + 6], 'little') * SECTOR)
                        cnf = f.read(min(int.from_bytes(root[at + 10:at + 14], 'little'), 4096))
                        return b'BOOT2' in cnf and (b'SLUS' in cnf or b'SLES' in cnf)
                    at += n
            return False
        except OSError:
            return False

    def extract(self, iso, dest, say):
        raise NotImplementedError

    def verify(self, dest):
        raise NotImplementedError

    def game_root(self, dest):
        raise NotImplementedError
