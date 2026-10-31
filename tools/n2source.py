"""Which console's disc the converters read: --source xbox|ps2.

Every converter takes --game, the disc's NFSUNDER folder (ZDIR.BIN and
the ZZDATA archives). With --source ps2 (or NFSU2_SOURCE=ps2 in the
environment, which tools/build.sh passes down) its default is
game/ps2/NFSUNDER, the PS2 disc tools/extract_ps2.py extracted, instead
of game/xbox/NFSUNDER. The two discs share the container, the byte order
and most chunks byte for byte; the readers that differ (meshes, textures,
audio codecs, the executable's tables) tell the console from the data or
from platform() below, never from a guess.

A folder of one console under --source of the other stops the converter
(check()): nothing mixes the two discs without saying so.
"""
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('xbox', 'ps2')
EXECUTABLES = {'xbox': 'default.xbe', 'ps2': 'SLUS_210.65'}


def selected():
    """--source on the command line, else NFSU2_SOURCE, else xbox."""
    argv = sys.argv[1:]
    for i, a in enumerate(argv):
        if a == '--source' and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith('--source='):
            return a.split('=', 1)[1]
    return os.environ.get('NFSU2_SOURCE', 'xbox')


def default_game(source=None):
    return ROOT / 'game' / (source or selected()) / 'NFSUNDER'


def game_args(ap):
    """--source and --game on a converter's parser (--game's default
    follows --source). After parsing, the disc must be the source's
    (check()) and the output folder must not hold another source's files
    (mark_out())."""
    src = selected()
    ap.add_argument('--source', choices=SOURCES, default=src,
                    help='source disc: xbox (game/xbox) or ps2 (game/ps2, tools/extract_ps2.py); defaults to NFSU2_SOURCE or xbox')
    ap.add_argument('--game', type=Path, default=default_game(src) if src in SOURCES else None)
    parse = ap.parse_args

    def parse_checked(*a, **k):
        ns = parse(*a, **k)
        explicit = '--source' in ' '.join(sys.argv[1:]) or 'NFSU2_SOURCE' in os.environ
        ns.source = check(ns.game, ns.source if explicit else None)
        import n2formats
        n2formats.PLATFORM = os.environ['NFSU2_PLATFORM'] = ns.source
        out = getattr(ns, 'out', None)
        if isinstance(out, Path) and (out.is_dir() or not out.suffix):
            mark_out(out, ns.source)
        return ns
    ap.parse_args = parse_checked


def mark_out(out, source):
    """The dist's source.txt: the console its converted files came from.
    A converter of the other source stops there instead of mixing them."""
    marker = Path(out) / 'source.txt'
    if marker.exists():
        have = marker.read_text().split()[0] if marker.read_text().split() else ''
        if have != source:
            raise SystemExit(f'{out} already contains files converted from {have}; this conversion uses {source}. '
                             f'Choose another folder (--out) or remove dist: sources cannot be mixed')
        return
    Path(out).mkdir(parents=True, exist_ok=True)
    marker.write_text(f'{source}\n')


def platform(game):
    """'ps2' or 'xbox' from the disc's own files beside NFSUNDER: the PS2's
    SYSTEM.CNF, the Xbox's default.xbe."""
    disc = Path(game).resolve().parent
    if (disc / 'SYSTEM.CNF').exists():
        return 'ps2'
    if (disc / 'default.xbe').exists():
        return 'xbox'
    raise SystemExit(f'{game}: unknown disc platform (no SYSTEM.CNF or default.xbe next to NFSUNDER)')


def check(game, source=None):
    """The console of --game; stops when it is not --source's (None: no
    source asked for, the disc's own)."""
    have = platform(game)
    if source is not None and have != source:
        raise SystemExit(f'{game} is a {have} disc, but {source} was requested: '
                         f'use --source {have} or another --game (sources cannot be mixed)')
    return have


def executable(game):
    """The game's executable beside NFSUNDER (the tables the converters
    read from it: default.xbe on the Xbox, SLUS_210.65 on the PS2)."""
    p = Path(game).parent / EXECUTABLES[platform(game)]
    if not p.exists():
        raise SystemExit(f'{p} does not exist: incomplete disc extraction')
    return p


def require(game, *platforms, what='este conversor'):
    """Stop a converter (or one of its parts) the PS2 can't feed yet,
    with the reason, instead of writing something half from nowhere."""
    have = platform(game)
    if have not in platforms:
        raise SystemExit(f'{what}: cannot read {have} discs yet (missing reader; see tools/PS2_SOURCE.md)')
    return have
