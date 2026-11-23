#!/usr/bin/env python3
"""A generated PSP/GAME/NFSU2VIEW folder as a PSP game ISO (ms0:/ISO), with no
outside tools.

The disc layout is the one tools/psp_iso_prototype.py makes with mkisofs and
that opened from the XMB of a PSP-3000 (0.4.1-rc.3, rc.22): UMD_DATA.BIN and
the PSP volume fields, PSP_GAME/PARAM.SFO (category UG, disc ID NFSU20001)
with the EBOOT's XMB art, PSP_GAME/SYSDIR/EBOOT.BIN (the EBOOT's DATA.PSP),
and the converted assets loose in SYSDIR beside it, pack.n2pk unpacked, long
names under the same short aliases the game looks for (disc_asset_path).
Here the ISO 9660 image is written directly (level 3 names kept as they are,
CD-XA attributes on every record), so the players' generator needs no
mkisofs. After writing, the image is read back: the PSP volume fields, the
XA attributes, the zero after each name of the XMB boot path, and every
file's bytes against what was written.

    python3 tools/psp_iso.py GAME_FOLDER OUTPUT.iso
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import time
from pathlib import Path

PBP_HEADER = struct.Struct("<4s9I")
SFO_HEADER = struct.Struct("<4s4I")
SFO_ENTRY = struct.Struct("<HHIII")
PACK_HEADER = struct.Struct("<4sIII")
PACK_ENTRY = struct.Struct("<IIH")
DISC_ID = b"NFSU20001\0"
DISC_LABEL = b"NFSU-20001|2D850DBE191C203A|0001"
UMD_DATA = (DISC_LABEL + b"|G").ljust(47, b"\0") + b"|"
ISO_PATHS_MARKER = "iso_paths.v1"
SECTOR = 2048
PVD_SECTOR = 16
PAD_SECTORS = 150                  # trailing zero sectors, as mkisofs adds
PSP_VOLUME_FIELDS = {
    8: b"PSP GAME".ljust(32, b" "),
    40: b"NFSU2_PSP".ljust(32, b" "),
    574: b"PSP GAME".ljust(128, b" "),
    883: DISC_LABEL,
    1024: b"CD-XA001" + bytes(18),
}
XA_DIR = bytes.fromhex("000000008d555841000000000000")
XA_FILE = bytes.fromhex("0000000009115841000000000000")
DATA_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.&-]{0,36}")
EXTRA_DATA = {"music.mp3", "music.idx", "nis.big", "speech.big", "icon0.png", "pic1.png",
              "credits.txt", "data-id.txt", "names.txt", "movies.txt", "help.txt",
              "help_de.txt", "help_es.txt", "help_fr.txt", "help_it.txt", "help_nl.txt"}
CHUNK = 4 * 1024 * 1024


# ---- the disc's contents (the same rules as tools/psp_iso_prototype.py) ----

def read_pbp(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 32 * 1024**2:
        raise ValueError("EBOOT.PBP must be a regular PSP executable of at most 32 MiB")
    raw = path.read_bytes()
    magic, _, *offsets = PBP_HEADER.unpack_from(raw)
    if (magic != b"\0PBP" or offsets != sorted(offsets)
            or offsets[0] < PBP_HEADER.size or offsets[-1] > len(raw)):
        raise ValueError("invalid PBP section offsets")
    ends = offsets[1:] + [len(raw)]
    sections = [raw[start:end] for start, end in zip(offsets, ends)]
    if not sections[6].startswith((b"\x7fELF", b"~PSP")):
        raise ValueError("PBP DATA.PSP does not contain a supported PSP executable")
    return sections


def disc_sfo(raw):
    sfo = bytearray(raw)
    magic, _, keys, data, count = SFO_HEADER.unpack_from(sfo)
    if magic != b"\0PSF" or not SFO_HEADER.size + count * SFO_ENTRY.size <= keys <= data <= len(sfo):
        raise ValueError("invalid PARAM.SFO header")
    fields = {b"CATEGORY": b"UG\0", b"DISC_ID": DISC_ID}
    for i in range(count):
        key_offset, kind, length, capacity, offset = SFO_ENTRY.unpack_from(sfo, SFO_HEADER.size + i * SFO_ENTRY.size)
        key = bytes(sfo[keys + key_offset:sfo.index(0, keys + key_offset, data)])
        start = data + offset
        if key == b"TITLE" and kind == 0x204 and length <= capacity <= len(sfo) - start:
            if sfo[start:start + 10] == b"NFSU2 PSP ":
                sfo[start:start + 10] = b"NFSU2 ISO "
        value = fields.pop(key, None)
        if value is None:
            continue
        if kind != 0x204 or length != len(value) or not length <= capacity <= len(sfo) - start:
            raise ValueError(f"invalid PARAM.SFO field {key!r}")
        sfo[start:start + length] = value
    if fields:
        raise ValueError(f"missing PARAM.SFO fields {list(fields)!r}")
    return bytes(sfo)


def is_asset(name):
    return name in EXTRA_DATA or re.search(r"\.n2[a-z0-9]+$", name) is not None


def converted_files(folder):
    """Only the release's assets: never saves, logs or other files the player keeps there."""
    files = []
    for path in sorted(folder.iterdir()):
        if not is_asset(path.name):
            continue
        if not DATA_NAME.fullmatch(path.name) or path.is_symlink() or not path.is_file():
            raise ValueError(f"unsupported asset path: {path}")
        files.append(path)
    names = {path.name for path in files}
    if "city.n2cy" not in names or not {"pack.n2pk", "title.n2ui"} & names:
        raise ValueError("expected a converted game folder with city.n2cy and title data")
    if len({name.casefold() for name in names}) != len(names):
        raise ValueError("converted asset names must be unique regardless of case")
    return files


def disc_asset_name(name):
    """The game's disc_asset_path (diagnostic_storage.c): short names kept, others hashed."""
    if len(name) < 32 and "-" not in name and "&" not in name:
        return name
    value = 2166136261
    for byte in name.lower().encode("ascii"):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return f"n2_{value:08x}.bin"


def packed_entries(pack, loose):
    """(name, offset, length) of each asset inside pack.n2pk (psp-viewer's pack_load)."""
    size = pack.stat().st_size
    with pack.open("rb") as f:
        magic, version, count, index_size = PACK_HEADER.unpack(f.read(PACK_HEADER.size))
        data_start = PACK_HEADER.size + index_size
        if ((magic, version) != (b"N2PK", 1) or count > 16384
                or not count * 12 <= index_size <= min(2**20, size - PACK_HEADER.size)):
            raise ValueError("invalid pack.n2pk header")
        index = f.read(index_size)
    pos, previous_end, entries = 0, data_start, []
    for _ in range(count):
        offset, length, name_length = PACK_ENTRY.unpack_from(index, pos)
        start = pos + PACK_ENTRY.size
        name = index[start:start + name_length].decode("ascii")
        pos = (start + name_length + 3) & ~3
        key = name.casefold()
        if pos > index_size or not DATA_NAME.fullmatch(name) or not is_asset(name) or key in loose or key == "pack.n2pk":
            raise ValueError(f"invalid or duplicate packed asset {name!r}")
        if not previous_end <= offset <= offset + length <= size or length >= 2**20:
            raise ValueError(f"invalid data range for packed asset {name!r}")
        previous_end = offset + length
        loose.add(key)
        entries.append((name, offset, length))
    if pos != index_size:
        raise ValueError("invalid pack.n2pk index size")
    return entries


def disc_tree(folder):
    """{directory: {name: source}}; a source is bytes, a Path, or (Path, offset, length)."""
    files = converted_files(folder)
    sections = read_pbp(folder / "EBOOT.PBP")
    root, game, sysdir = {}, {}, {}
    root["UMD_DATA.BIN"] = UMD_DATA
    game["PARAM.SFO"] = disc_sfo(sections[0])
    for name, data in zip(("ICON0.PNG", "ICON1.PMF", "PIC0.PNG", "PIC1.PNG", "SND0.AT3"), sections[1:6]):
        if data:
            game[name] = data
    sysdir["EBOOT.BIN"] = sections[6]
    sysdir[ISO_PATHS_MARKER] = b""
    taken = {"eboot.bin", ISO_PATHS_MARKER}

    def put(name, source):
        alias = disc_asset_name(name)
        if alias.casefold() in taken:
            raise ValueError(f"disc asset name collision for {name!r}: {alias}")
        taken.add(alias.casefold())
        sysdir[alias] = source

    loose = [p for p in files if p.name != "pack.n2pk"]
    for path in loose:
        put(path.name, path)
    count = len(loose)
    pack = folder / "pack.n2pk"
    if pack in files:
        for name, offset, length in packed_entries(pack, {p.name.casefold() for p in loose}):
            put(name, (pack, offset, length))
            count += 1
    return {"": root, "PSP_GAME": game, "PSP_GAME/SYSDIR": sysdir}, count


# ---- ISO 9660 writer ----

def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def rec_date(t):
    g = time.gmtime(t)
    return bytes((g.tm_year - 1900, g.tm_mon, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec, 0))


def vol_date(t):
    return time.strftime("%Y%m%d%H%M%S00", time.gmtime(t)).encode() + b"\0"


def record(name, lba, size, is_dir, when):
    pad = b"" if len(name) % 2 else b"\0"
    body = (b"\0" + both32(lba) + both32(size) + rec_date(when) + bytes((2 if is_dir else 0, 0, 0))
            + both16(1) + bytes((len(name),)) + name + pad + (XA_DIR if is_dir else XA_FILE))
    return bytes((len(body) + 1,)) + body


def source_size(source):
    if isinstance(source, bytes):
        return len(source)
    if isinstance(source, Path):
        return source.stat().st_size
    return source[2]


def source_chunks(source):
    if isinstance(source, bytes):
        yield source
        return
    path, offset, length = (source, 0, source.stat().st_size) if isinstance(source, Path) else source
    with path.open("rb") as f:
        f.seek(offset)
        while length:
            data = f.read(min(CHUNK, length))
            if not data:
                raise ValueError(f"{path} shorter than expected")
            length -= len(data)
            yield data


def layout(tree):
    """Sectors of every directory and file, in mkisofs's order: directories after the path
    tables, then each directory's files in turn (root, PSP_GAME, SYSDIR)."""
    dirs = ["", "PSP_GAME", "PSP_GAME/SYSDIR"]
    children = {"": ["PSP_GAME"], "PSP_GAME": ["SYSDIR"], "PSP_GAME/SYSDIR": []}
    names = {d: sorted(list(tree[d]) + children[d], key=lambda n: n.encode()) for d in dirs}

    def dir_bytes(d):          # the records of a directory, none across a sector's end
        sizes = [48, 48] + [len(record(n.encode(), 0, 0, n in children[d], 0)) for n in names[d]]
        used = 0
        for s in sizes:
            if used % SECTOR + s > SECTOR:
                used += SECTOR - used % SECTOR
            used += s
        return -(-used // SECTOR) * SECTOR

    lba = PVD_SECTOR + 7       # PVD, terminator, a spare, L table, its copy, M table, its copy
    where = {}
    for d in dirs:
        where[d] = (lba, dir_bytes(d))
        lba += where[d][1] // SECTOR
    files = {}
    for d in dirs:
        for n in names[d]:
            if n in children[d]:
                continue
            size = source_size(tree[d][n])
            files[(d, n)] = (lba if size else 0, size)
            lba += -(-size // SECTOR)
    return dirs, children, names, where, files, lba


def path_table(dirs, where, big):
    out = b""
    for i, d in enumerate(dirs):
        name = d.rsplit("/", 1)[-1].encode() or b"\0"
        parent = 1 if i < 2 else 2
        lba = struct.pack(">I" if big else "<I", where[d][0])
        out += bytes((len(name), 0)) + lba + struct.pack(">H" if big else "<H", parent) + name + (b"\0" if len(name) % 2 else b"")
    return out


def directory(d, dirs, children, names, where, files, when):
    parent = "" if d in ("", "PSP_GAME") else "PSP_GAME"
    recs = [record(b"\0", *where[d], True, when), record(b"\1", *where[parent], True, when)]
    for n in names[d]:
        if n in children[d]:
            sub = f"{d}/{n}".strip("/")
            recs.append(record(n.encode(), *where[sub], True, when))
        else:
            recs.append(record(n.encode(), *files[(d, n)], False, when))
    out = bytearray()
    for r in recs:
        if len(out) % SECTOR + len(r) > SECTOR:
            out += bytes(SECTOR - len(out) % SECTOR)
        out += r
    return bytes(out).ljust(where[d][1], b"\0")


def pvd(total, pt_size, l_pt, m_pt, root, when):
    v = bytearray(SECTOR)
    v[0:8] = b"\x01CD001\x01\x00"
    v[8:40] = PSP_VOLUME_FIELDS[8]
    v[40:72] = PSP_VOLUME_FIELDS[40]
    v[80:88] = both32(total)
    v[120:124] = both16(1)
    v[124:128] = both16(1)
    v[128:132] = both16(SECTOR)
    v[132:140] = both32(pt_size)
    v[140:144] = struct.pack("<I", l_pt)
    v[148:152] = struct.pack(">I", m_pt)
    v[156:190] = root
    for start, end in ((190, 574), (702, 813)):
        v[start:end] = b" " * (end - start)
    v[574:702] = PSP_VOLUME_FIELDS[574]
    stamp = vol_date(when)
    v[813:830] = stamp
    v[830:847] = stamp
    v[847:864] = b"0" * 16 + b"\0"
    v[864:881] = stamp
    v[881] = 1
    v[883:1395] = b" " * 512       # application use, blank as mkisofs leaves it
    v[883:883 + len(DISC_LABEL)] = DISC_LABEL
    v[1024:1050] = PSP_VOLUME_FIELDS[1024]
    return bytes(v)


def write_iso(tree, out, when=None):
    """The image of tree at out; returns {path: sha256} of every file written."""
    when = int(time.time() if when is None else when)
    dirs, children, names, where, files, end = layout(tree)
    total = end + PAD_SECTORS
    l_table, m_table = path_table(dirs, where, False), path_table(dirs, where, True)
    if len(l_table) > SECTOR:
        raise ValueError("path table larger than a sector")
    l_pt, m_pt = PVD_SECTOR + 3, PVD_SECTOR + 5
    root = record(b"\0", *where[""], True, when)[:34]
    root = bytes((34,)) + root[1:33] + b"\0"
    digests = {}
    with out.open("wb") as f:
        f.write(bytes(PVD_SECTOR * SECTOR))
        f.write(pvd(total, len(l_table), l_pt, m_pt, root, when))
        f.write(b"\xffCD001\x01".ljust(SECTOR, b"\0"))
        f.write(bytes(SECTOR))
        f.write(l_table.ljust(SECTOR, b"\0") + bytes(SECTOR))
        f.write(m_table.ljust(SECTOR, b"\0") + bytes(SECTOR))
        for d in dirs:
            if f.tell() != where[d][0] * SECTOR:
                raise AssertionError("directory out of place")
            f.write(directory(d, dirs, children, names, where, files, when))
        for d in dirs:
            for n in names[d]:
                if n in children[d]:
                    continue
                lba, size = files[(d, n)]
                if not size:
                    digests[f"/{d}/{n}".replace("//", "/")] = hashlib.sha256().hexdigest()
                    continue
                if f.tell() != lba * SECTOR:
                    raise AssertionError("file out of place")
                h, written = hashlib.sha256(), 0
                for data in source_chunks(tree[d][n]):
                    f.write(data)
                    h.update(data)
                    written += len(data)
                if written != size:
                    raise ValueError(f"{n}: size changed while writing")
                f.write(bytes(-size % SECTOR))
                digests[f"/{d}/{n}".replace("//", "/")] = h.hexdigest()
        f.write(bytes(PAD_SECTORS * SECTOR))
        if f.tell() != total * SECTOR:
            raise AssertionError("image size")
    return digests


# ---- reading the image back ----

def entries(f, lba, size):
    f.seek(lba * SECTOR)
    data = f.read(size)
    pos = 0
    while pos < len(data):
        if data[pos] == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = data[pos:pos + data[pos]]
        if len(rec) < 34 or 33 + rec[32] > len(rec) or pos % SECTOR + len(rec) > SECTOR:
            raise ValueError("invalid ISO directory record")
        name = rec[33:33 + rec[32]]
        following = data[pos + 33 + rec[32]:pos + 34 + rec[32]]
        yield name, rec, following
        pos += len(rec)


def verify(image, digests):
    """The PSP fields, XA attributes, boot path and every file's bytes."""
    with image.open("rb") as f:
        f.seek(PVD_SECTOR * SECTOR)
        v = f.read(SECTOR)
        if (not v.startswith(b"\x01CD001\x01")
                or any(v[o:o + len(x)] != x for o, x in PSP_VOLUME_FIELDS.items())):
            raise ValueError("incorrect PSP primary volume metadata")
        if struct.unpack_from("<I", v, 80)[0] * SECTOR != image.stat().st_size:
            raise ValueError("volume size differs from the image")
        found = {}

        def walk(path, rec):
            lba, size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            for name, child, following in entries(f, lba, size):
                su = child[33 + len(name) + (1 - len(name) % 2):]
                is_dir = bool(child[25] & 2)
                if su != (XA_DIR if is_dir else XA_FILE):
                    raise ValueError(f"incorrect CD-XA attributes: {path}/{name!r}")
                if name in (b"\0", b"\1"):
                    continue
                if following != b"\0" and len(name) % 2:
                    raise ValueError(f"name lacks a zero terminator: {path}/{name.decode()}")
                full = f"{path}/{name.decode('ascii')}"
                if is_dir:
                    walk(full, child)
                else:
                    found[full] = child

        walk("", v[156:190])
        boot = "/PSP_GAME/SYSDIR/EBOOT.BIN"
        if boot not in found:
            raise ValueError("missing XMB boot path")
        if set(found) != set(digests):
            raise ValueError("ISO file names differ from the prepared tree")
        for path, rec in found.items():
            lba, size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            f.seek(lba * SECTOR)
            h, left = hashlib.sha256(), size
            while left:
                data = f.read(min(CHUNK, left))
                if not data:
                    raise ValueError(f"truncated file in the ISO: {path}")
                h.update(data)
                left -= len(data)
            if h.hexdigest() != digests[path]:
                raise ValueError(f"file differs in the ISO: {path}")
        f.seek(struct.unpack_from("<I", found["/UMD_DATA.BIN"], 2)[0] * SECTOR)
        if f.read(len(UMD_DATA)) != UMD_DATA:
            raise ValueError("incorrect root PSP disc identifier")
    return len(found)


def export(folder, output, when=None):
    """GAME folder -> a new ISO at output (written to output.part, then renamed). Returns
    (assets, files on the disc, bytes)."""
    folder = Path(folder).resolve(strict=True)
    output = Path(output)
    output = output.parent.resolve(strict=True) / output.name
    if output.suffix.lower() != ".iso" or output.is_relative_to(folder):
        raise ValueError("choose a .iso outside the converted folder")
    tree, count = disc_tree(folder)
    part = output.with_name(output.name + ".part")
    part.unlink(missing_ok=True)
    try:
        digests = write_iso(tree, part, when)
        files = verify(part, digests)
        os.replace(part, output)
    except BaseException:
        part.unlink(missing_ok=True)
        raise
    return count, files, output.stat().st_size


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game_folder", type=Path, help="generated PSP/GAME/NFSU2VIEW folder")
    ap.add_argument("output", type=Path, help="the .iso to write (replaced if it exists)")
    ap.add_argument("--time", type=int, help=argparse.SUPPRESS)   # fixed timestamps (tests)
    args = ap.parse_args()
    try:
        count, files, size = export(args.game_folder, args.output, args.time)
    except (OSError, ValueError, struct.error) as exc:
        ap.exit(1, f"{ap.prog}: {exc}\n")
    print(f"{args.output}: {count} converted assets, {files} files, {size / 2**30:.2f} GB")


if __name__ == "__main__":
    main()
