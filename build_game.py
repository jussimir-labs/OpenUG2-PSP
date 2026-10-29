#!/usr/bin/env python3
"""NFSU2 PSP: gera a pasta PSP/GAME/NFSU2VIEW a partir da ISO do próprio jogador.

    python3 build_game.py CAMINHO/DA/ISO [--saida PASTA] [--fonte xbox|ps2]

O EBOOT vem pronto (eboot/EBOOT.PBP); daqui saem só os dados do jogo,
convertidos da ISO. A ferramenta confere a ISO, extrai os arquivos,
roda os conversores (tools/) e monta o pacote do cartão em
PASTA/PSP/GAME/NFSU2VIEW. Tudo o que ela escreve fica dentro de PASTA:
a extração e a conversão em PASTA/.trabalho (apagada no fim, a não ser
com --manter-trabalho), os registros de cada etapa em PASTA/registros.

Se uma etapa falhar, rode o mesmo comando de novo: as etapas já feitas
não se repetem (--do-zero recomeça tudo).
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True   # (nothing written beside the tool: only in the output folder)
sys.path.insert(0, str(HERE))
from sources import SOURCES, SourceError  # noqa: E402
sys.path.insert(0, str(HERE / 'tools'))
import native_runtime
import step_graph

EBOOT = HERE / 'eboot/EBOOT.PBP'
RELEASE = HERE / 'eboot/release.json'
# What the conversion needs on disk beyond the output itself, in GB:
# the disc's files, the converted folder and the package (hard links into
# it where the file system has them). Measured on the reference build;
# see LEIA-ME.md.
DISK_NEEDED_GB = 12
MIN_PYTHON = (3, 12)
MODULES = {'numpy': 'numpy', 'scipy': 'scipy', 'PIL': 'pillow', 'trajectory_planning_helpers': 'trajectory-planning-helpers',
           'quadprog': 'quadprog'}


class Failure(Exception):
    """A clear stop: the message is for the player."""


SAYING = threading.Lock()   # steps running side by side: one whole line at a time


def say(*args):
    with SAYING:
        print(*args, flush=True)


def human(seconds):
    seconds = int(seconds)
    if seconds < 60:
        return f'{seconds} s'
    if seconds < 3600:
        return f'{seconds // 60} min {seconds % 60:02d} s'
    return f'{seconds // 3600} h {seconds // 60 % 60:02d} min'


# The conversion, in tools/build.sh's order (the reference build's): each
# step is (name, expected seconds on the reference Mac, arguments to the
# work folder's Python, or a callable). The expected times only drive the
# progress bar.
def steps(game_dir, movies=True, jobs=4):
    out = []
    add = lambda name, secs, *cmd: out.append((name, secs, list(cmd)))  # noqa: E731
    add('carro da vitrine', 20, 'tools/convert_car.py')
    # the city and the tracks without the preculler's rows, as the
    # candidates' (on the PSP-3000 the split lists cost more than they hide;
    # tools/build.sh still converts with them when a city is missing)
    add('cidade', 900, 'tools/convert_city.py', '--no-precull')
    add('visibilidade da cidade', 10, 'tools/convert_pvs.py')
    add('objetos derrubáveis', 15, 'tools/convert_smokeables.py')
    add('ruas', 20, 'tools/convert_routes.py')
    add('GPS', 10, 'tools/convert_gps.py')
    add('faixas', 20, 'tools/convert_lanes.py')
    add('corridas da cidade', 400, 'tools/convert_races.py')
    add('zonas de reset', 5, 'tools/convert_resetzones.py')
    for track in ('L4RB', 'L4RC', 'L4RD', 'L4RF', 'L4RG'):
        add(f'pista {track}', 150, 'tools/convert_city.py', '--track', track, '--no-precull')
        add(f'ruas da pista {track}', 10, 'tools/convert_routes.py', '--track', track)
        add(f'corridas da pista {track}', 60, 'tools/convert_races.py', '--track', track)
    add('câmeras do replay', 60, 'tools/convert_tvcams.py')
    add('limites da geometria', 60, ('glob', 'tools/n2cy_bounds.py', '--if-missing'))
    add('ordem das texturas da cidade', 30, 'tools/n2cy_texorder.py', '--if-unordered', 'dist/PSP/GAME/NFSU2VIEW/city.n2cy')
    add('horizonte distante', 60, 'tools/convert_landmarks.py', '--lod', '--towers', '60', '--tile', '256', '--game', str(game_dir),
        '--city', 'dist/PSP/GAME/NFSU2VIEW/city.n2cy', '--out', 'dist/PSP/GAME/NFSU2VIEW/city-landmarks.n2cy')
    add('mipmaps', 120, ('glob', 'tools/convert_mips.py', '--if-stale'))
    add('linhas de corrida', 900, 'tools/racing_line.py')
    add('velocidade das linhas', 40, 'tools/line_speeds.py')
    add('desvios das linhas', 30, 'tools/line_forks.py')
    add('carreira', 30, 'tools/convert_career.py')
    add('carros', 120, 'tools/convert_cars.py')
    add('pacotes de desempenho', 10, 'tools/convert_perf.py')
    add('regras do drift e do drag', 5, 'tools/convert_modes.py')
    add('estilo', 5, 'tools/convert_style.py')
    add('dificuldade', 5, 'tools/convert_difficulty.py')
    add('regras do trânsito', 5, 'tools/convert_traffic_rules.py')
    add('pinturas', 5, 'tools/convert_paints.py')
    add('garagem', 400, 'tools/convert_garage.py')
    add('rodas', 30, 'tools/convert_rims.py')
    add('nomes das rodas', 5, 'tools/convert_rims.py', '--styles')
    add('nomes das peças', 5, 'tools/convert_partnames.py')
    add('vinis', 500, 'tools/convert_vinyls.py')
    add('pontos do visual', 10, 'tools/convert_rating.py')
    add('revistas', 10, 'tools/convert_magazines.py')
    add('telas e HUD', 90, 'tools/convert_hud.py')
    add('marcadores de evento', 5, 'tools/convert_trigfx.py')
    add('tela de controles', 5, 'tools/convert_psp_pad.py')
    add('chuva', 5, 'tools/convert_weather.py')
    add('som ambiente', 10, 'tools/convert_envsound.py')
    add('neblina', 10, 'tools/convert_fog.py')
    add('luzes', 40, 'tools/convert_lights.py')
    add('mundo animado', 20, 'tools/convert_worldanim.py')
    add('cena dos menus', 10, 'tools/convert_frontend.py')
    add('cenas de largada', 120, 'tools/convert_intro_nis.py')
    add('cenas de chegada', 60, 'tools/convert_ending_nis.py')
    add('cenas das lojas', 30, 'tools/convert_shop_nis.py')
    add('trânsito', 60, 'tools/convert_traffic.py')
    add('céu', 10, 'tools/convert_sky.py')
    add('panoramas', 20, 'tools/convert_vista.py')
    add('física', 10, 'tools/convert_physics.py')
    add('fontes originais da física', 2, 'tools/convert_original_data.py', '--game', str(game_dir))
    add('áudio', 300, 'tools/convert_audio.py')
    if movies:
        add('filmes', 600, 'tools/convert_movies.py', '--missing')
        add('conferência dos filmes', 5, 'tools/movie_release.py', 'dist/PSP/GAME/NFSU2VIEW')
    for group in ('opponents', 'rivals'):
        for preset in sorted((HERE / 'presets' / group).glob('*.json')):
            add(f'carro rival {preset.stem}', 30, 'tools/convert_car.py', '--preset', f'presets/{group}/{preset.name}',
                '--tag', f'opp-{preset.stem}')
    for _, _, command in out:
        if command[0] in ('tools/convert_city.py', 'tools/convert_garage.py', 'tools/racing_line.py', 'tools/convert_races.py',
                          'tools/convert_hud.py'):
            command.extend(['--jobs', str(jobs)])
    return out


def iso_size_ok(iso):
    if not iso.is_file():
        raise Failure(f'ISO não encontrada: {iso}')
    return iso.stat().st_size


def sha256(path, label=None):
    """The file's SHA-256, with a progress line for big files."""
    size = path.stat().st_size
    h = hashlib.sha256()
    done, last = 0, 0
    with open(path, 'rb') as f:
        while chunk := f.read(8 << 20):
            h.update(chunk)
            done += len(chunk)
            if label and time.monotonic() - last > 2:
                last = time.monotonic()
                print(f'\r  {label}: {done * 100 // max(size, 1)}%', end='', flush=True)
    if label:
        print(f'\r  {label}: 100%')
    return h.hexdigest()


def find_cjpegli(explicit):
    for cand in [native_runtime.binary('cjpegli'), explicit, os.environ.get('NFSU2_CJPEGLI'), HERE / '.tools/jpegli/build/tools/cjpegli',
                 HERE / '.tools/jpegli/build/tools/cjpegli.exe', shutil.which('cjpegli')]:
        if cand and Path(cand).is_file():
            return Path(cand).resolve()
    return None


def recipe_problems():
    """Reject an incomplete exported kit before expensive conversion."""
    problems=[]
    for name, _, command in steps(Path('game/xbox/NFSUNDER')):
        tool=command[0][1] if isinstance(command[0],tuple) else command[0]
        if not (HERE/tool).is_file():problems.append(f'conversor ausente para {name}: {tool}')
    # New source-data producers are stdlib-only; their canonical tables and
    # standalone archive reader must be present before any expensive stage.
    for name in ('export_physics.py', 'export_road.py', 'source_data.py',
                 'nitro_constants.json', 'road_constants.json', 'native_runtime.py', 'process_control.py'):
        if not (HERE / 'tools' / name).is_file():
            problems.append(f'fonte da física original ausente: tools/{name}')
    if not (HERE / 'tools/nos_bank.py').is_file():
        problems.append('Required native NOS/PURGE exporter missing: tools/nos_bank.py')
    for name in ('AL_RX8','CALEB_GTO','MARCUS_CELICA','NIGEL_3000GT','NIKKI_MUSTANGGT','SCOTT_TT','TOM_G35'):
        if not (HERE/'presets/rivals'/f'{name}.json').is_file():problems.append(f'carro da carreira ausente: presets/rivals/{name}.json')
    return problems


def preflight(args):
    """Everything the conversion needs, checked before it starts: the
    player sees every missing piece at once."""
    problems = recipe_problems()
    if sys.version_info < MIN_PYTHON:
        problems.append(f'Python {".".join(map(str, MIN_PYTHON))} ou mais novo (este é {sys.version.split()[0]}).')
    missing = []
    for mod, pkg in MODULES.items():
        try:
            __import__(mod)
        except ImportError:
            missing.append(pkg)
    if missing:
        problems.append(f'faltam módulos do Python: {", ".join(missing)} (rode o ./setup.sh, que os instala em .venv, e use '
                        f'o Python de lá: .venv/bin/python build_game.py).')
    for tool in ('ffmpeg', 'lame'):
        if not native_runtime.binary(tool) and not shutil.which(tool):
            problems.append(f'falta o programa {tool} (macOS: brew install {tool}; Linux: apt install {tool}).')
    if not native_runtime.bundled() and not shutil.which('cc'):
        problems.append('falta um compilador C chamado cc (macOS: xcode-select --install; Linux: apt install build-essential).')
    if native_runtime.bundled():
        for tool in native_runtime.NAMES:
            try:
                native_runtime.binary(tool)
            except RuntimeError as exc:
                problems.append(str(exc))
    cjpegli = None
    if not args.sem_filmes:
        cjpegli = find_cjpegli(args.cjpegli)
        if not cjpegli:
            problems.append('falta o cjpegli, o codificador dos filmes (rode ./setup.sh, que o compila em .tools/jpegli; '
                            'ou use --sem-filmes).')
    if not EBOOT.is_file():
        problems.append(f'falta o EBOOT pré-compilado ({EBOOT}).')
    return problems, cjpegli


def validate_release():
    if not RELEASE.is_file():
        raise Failure('release.json ausente: o pacote do gerador está incompleto.')
    release = json.loads(RELEASE.read_text())
    if not EBOOT.is_file() or sha256(EBOOT) != release.get('eboot_sha256') or EBOOT.stat().st_size != release.get('eboot_bytes'):
        raise Failure('EBOOT não corresponde a release.json. Baixe novamente o pacote completo do beta.')
    return release


def build_identity(digest, source, args, release):
    # Bind every resumed step to its actual inputs, tools, executable and options.
    inputs = {}
    for root in ('tools', 'presets', 'sources', 'art'):
        for path in sorted((HERE / root).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts and path.name != '.DS_Store':
                inputs[path.relative_to(HERE).as_posix()] = sha256(path)
    inputs['build_game.py'] = sha256(Path(__file__))
    if native_runtime.bundled():
        # A resumed output must use the exact included binaries/libraries too.
        inputs['native-bundle.json'] = sha256(Path(os.environ['NFSU2_BUNDLE_ROOT']) / 'bundle.json')
    return {'schema': 2, 'iso_sha256': digest, 'source': source.name,
            'candidate': release['candidate'], 'eboot_sha256': release['eboot_sha256'],
            'movies': not args.sem_filmes, 'jobs': getattr(args, 'jobs', 4),
            'accept_unknown_iso': bool(getattr(args, 'aceitar_iso_desconhecida', False)),
            'tools_sha256': hashlib.sha256(json.dumps(inputs, sort_keys=True, separators=(',', ':')).encode()).hexdigest()}


def prepare_out(out, fresh, identity):
    """Only our output, bound to the same disc and exact conversion recipe."""
    marker = out / '.nfsu2-psp-build'
    if out == HERE or out in HERE.parents:
        raise Failure('a saída não pode ser a ferramenta nem uma pasta que a contém.')
    if out.exists() and any(out.iterdir()):
        if not marker.is_file() or marker.is_symlink():
            raise Failure(f'a pasta de saída {out} não é desta ferramenta; escolha uma pasta nova.')
        try:
            previous = json.loads(marker.read_text())
        except (ValueError, UnicodeError):
            previous = None
        if not fresh and previous != identity:
            raise Failure('a ISO, fonte, executável, opções ou conversores mudaram. Use outra saída ou --do-zero; nenhuma etapa antiga foi reutilizada.')
        if fresh:
            if any(p.is_dir() for p in out.rglob('SAVEDATA')) or any(out.rglob('*.sav')):
                raise Failure('a saída contém saves. Preserve-os e escolha outra pasta de saída.')
            for path in out.iterdir():
                if path.name != marker.name:
                    shutil.rmtree(path) if path.is_dir() and not path.is_symlink() else path.unlink()
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'PSP').exists():
        raise Failure(f'{out / "PSP"} já existe: use outra saída ou --do-zero para gerar novamente.')
    marker.write_text(json.dumps(identity, sort_keys=True) + '\n')


def disk_free_gb(path):
    return shutil.disk_usage(path).free / 1e9


def setup_work(work, cjpegli):
    """The work folder: a copy of the converters (they write next to
    themselves: dist/, game/, .tools/cache), so nothing lands in this
    repository or anywhere else."""
    for d in ('tools', 'presets', 'art'):
        if (work / d).exists():
            shutil.rmtree(work / d)
        shutil.copytree(HERE / d, work / d, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    (work / 'dist/PSP/GAME/NFSU2VIEW').mkdir(parents=True, exist_ok=True)
    if cjpegli:
        dest = work / '.tools/jpegli/build/tools/cjpegli'
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.is_symlink() or dest.exists():
            dest.unlink()
        try:
            dest.symlink_to(cjpegli)
        except OSError:
            shutil.copy2(cjpegli, dest)


def step_env(work, source):
    env = dict(os.environ)
    # git must not find a repository above the work folder (data_id.py and
    # mac_slots.py ask it where to keep their files)
    env['GIT_CEILING_DIRECTORIES'] = str(work)
    # The Mac queue of the developers' machine (tools/mac_slots.py): off
    # unless its folder is given.
    if not env.get('NFSU2_MAC_SLOTS_DIR'):
        env['NFSU2_MAC_SLOTS'] = '0'
    env.pop('NFSU2_DIST', None)
    env['PYTHONUNBUFFERED'] = '1'
    env['NFSU2_SOURCE'] = env['NFSU2_PLATFORM'] = source.name
    # The pad icons come from the PS2 disc (tools/ps2_buttons.py): the
    # extracted one when it is the source, else the port's own drawings.
    if source.name == 'ps2':
        env['NFSU2_PS2_GAME'] = str(work / 'game' / 'ps2' / 'NFSUNDER')
    else:
        env.pop('NFSU2_PS2_GAME', None)
    # the converters' temporary files and matplotlib's cache (imported by
    # the racing-line helpers) inside the work folder too
    # and the bytecode Python caches for every module it imports
    for var, sub in (('TMPDIR', 'tmp'), ('TEMP', 'tmp'), ('TMP', 'tmp'), ('MPLCONFIGDIR', 'mpl'), ('PYTHONPYCACHEPREFIX', 'pycache')):
        (work / sub).mkdir(exist_ok=True)
        env[var] = str(work / sub)
    return env


# the long single-core steps tools/build.sh sends through the developers'
# Mac queue (the pooled converters ask for their own places)
QUEUED = ('corridas', 'limites da geometria', 'horizonte distante')


def queued(name, cmd, env):
    if env.get('NFSU2_MAC_SLOTS_DIR') and name.startswith(QUEUED):
        return [sys.executable, 'tools/mac_slots.py', 'run', '--weight', '1', '--label', f'build_game {name}', '--'] + cmd
    return cmd


def expand(cmd, work):
    """A ('glob', tool, flag) step: the tool once per world archive."""
    if isinstance(cmd[0], tuple):
        _, tool, flag = cmd[0]
        dist = work / 'dist/PSP/GAME/NFSU2VIEW'
        arcs = ([dist / 'city.n2cy'] if (dist / 'city.n2cy').exists() else []) + sorted(dist.glob('track-*.n2cy'))
        return [[tool, flag, str(a.relative_to(work))] for a in arcs]
    return [cmd]


# Maintainers only: NFSU2_STEP_TRACE=DIR logs what each step reads and writes
# (tools/step_trace) for tools/step_graph.py, the steps' dependencies.
TRACE = os.environ.get('NFSU2_STEP_TRACE')
NOT_DATA = ('game', 'tools', 'presets', 'art', 'tmp', 'mpl', 'pycache', 'registros')


def work_state(work):
    """Every data file of the work folder by (size, mtime): what a step's
    non-Python programs wrote shows as a difference."""
    out = {}
    for root, dirs, files in os.walk(work):
        rel = Path(root).relative_to(work)
        if rel.parts and rel.parts[0] in NOT_DATA:
            dirs[:] = []
            continue
        for f in files:
            st = os.lstat(os.path.join(root, f))
            out[(rel / f).as_posix()] = (st.st_size, st.st_mtime_ns)
    return out


def tree_rss(pid):
    """The resident memory of pid and its descendants, in KB."""
    try:
        rows = [r.split() for r in subprocess.run(['ps', '-A', '-o', 'pid=,ppid=,rss='], capture_output=True,
                                                  text=True).stdout.splitlines()]
    except OSError:
        return 0
    kids, rss = {}, {}
    for r in rows:
        if len(r) == 3 and r[0].isdigit():
            kids.setdefault(int(r[1]), []).append(int(r[0]))
            rss[int(r[0])] = int(r[2])
    total, todo = 0, [pid]
    while todo:
        q = todo.pop()
        total += rss.get(q, 0)
        todo += kids.get(q, [])
    return total


def run_step(i, name, secs, cmd, work, logs, env):
    log = logs / f'{i:02d}-{name.replace(" ", "-")}.log'
    t0 = time.monotonic()
    # each step its own temporary folder: steps run side by side
    tmp = work / 'tmp' / f'{i:02d}'
    tmp.mkdir(parents=True, exist_ok=True)
    env = dict(env, TMPDIR=str(tmp), TEMP=str(tmp), TMP=str(tmp))
    if TRACE:
        trace = Path(TRACE) / f'{i:02d}'
        trace.parent.mkdir(parents=True, exist_ok=True)
        env = dict(env, NFSU2_TRACE_LOG=str(trace) + '.trace', NFSU2_TRACE_ROOTS=f'{work}:{os.path.realpath(work)}',
                   NFSU2_TRACE_SKIP='/pycache/:/mpl/:/registros/',
                   PYTHONPATH=os.pathsep.join([str(work / 'tools/step_trace')] + [p for p in [env.get('PYTHONPATH')] if p]))
        before, peak, commands, began = work_state(work), 0, [], time.time()
    with open(log, 'w') as f:
        for c in expand(cmd, work):
            f.write('$ ' + ' '.join(['python'] + c) + '\n')
            f.flush()
            p = subprocess.Popen(queued(name, [sys.executable] + c, env), cwd=work, env=env, stdout=f, stderr=subprocess.STDOUT)
            last = time.monotonic()
            while p.poll() is None:
                time.sleep(0.5)
                if TRACE:
                    peak = max(peak, tree_rss(p.pid))
                if time.monotonic() - last > 30:
                    last = time.monotonic()
                    say(f'           … {name}: {human(time.monotonic() - t0)} (esperado ~{human(secs)})')
            if p.returncode:
                tail = log.read_text(errors='replace').splitlines()[-25:]
                raise Failure(f'a etapa "{name}" falhou (código {p.returncode}). Últimas linhas do registro {log}:\n'
                              + '\n'.join('    ' + t for t in tail))
            if TRACE:
                commands.append(c)
    if TRACE:
        after = work_state(work)
        changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
        (Path(str(trace) + '.json')).write_text(json.dumps(dict(
            index=i, name=name, commands=commands, seconds=round(time.monotonic() - t0, 2), peak_rss_kb=peak,
            began=began, ended=time.time(),
            changed=changed, removed=sorted(set(before) - set(after)), work=str(work)), indent=1) + '\n')
    return time.monotonic() - t0


GRAPH = HERE / 'manifests/steps.json'
UPDATE_FROM = HERE / 'manifests/update-from'
STATE = '.nfsu2-psp-steps.json'     # in the output: each step's stamp, the base of a later update
CACHE = '.nfsu2-psp-cache'          # in the output: the steps' files a later step reads that the card's folder leaves out
DIST = 'dist/PSP/GAME/NFSU2VIEW/'


def runtime_identity():
    """What runs the converters: the native package's runtime, or this Python and its libraries."""
    if native_runtime.bundled():
        return step_graph.runtime_id(json.loads((Path(os.environ['NFSU2_BUNDLE_ROOT']) / 'bundle.json').read_text()))
    import importlib.metadata as md
    versions = []
    for pkg in ('numpy', 'scipy', 'pillow', 'quadprog', 'trajectory-planning-helpers'):
        try:
            versions.append(pkg + '=' + md.version(pkg))
        except md.PackageNotFoundError:
            versions.append(pkg + '=?')
    return 'dev:' + hashlib.sha256(json.dumps([sys.version, versions, [shutil.which(t) for t in ('ffmpeg', 'lame')]]).encode()).hexdigest()


def graph_for(todo, source):
    """manifests/steps.json's graph of these steps; None when it does not
    cover them or was not traced from this source's generation (then one
    step after another, and no update)."""
    data = step_graph.load(GRAPH)
    if not data or source.name not in data.get('sources', ()):
        return None
    nodes = {n['name']: n for n in data['steps']}
    if any(name not in nodes for name, _, _ in todo):
        return None
    return step_graph.Graph(nodes, [name for name, _, _ in todo])


def free_memory_gb():
    """The memory free for another step, None where unknown."""
    try:
        if sys.platform == 'darwin':
            text = subprocess.run(['vm_stat'], capture_output=True, text=True, timeout=5).stdout
            page = int(text.split('page size of ')[1].split()[0])
            pages = {k.strip(): int(v.strip().rstrip('.')) for k, v in (l.split(':', 1) for l in text.splitlines()[1:] if ':' in l)}
            return (pages.get('Pages free', 0) + pages.get('Pages inactive', 0) + pages.get('Pages purgeable', 0)) * page / 1e9
        if os.name == 'nt':
            import ctypes

            class Status(ctypes.Structure):
                _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong), ('ullTotalPhys', ctypes.c_ulonglong),
                            ('ullAvailPhys', ctypes.c_ulonglong), ('ullTotalPageFile', ctypes.c_ulonglong),
                            ('ullAvailPageFile', ctypes.c_ulonglong), ('ullTotalVirtual', ctypes.c_ulonglong),
                            ('ullAvailVirtual', ctypes.c_ulonglong), ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
            st = Status()
            st.dwLength = ctypes.sizeof(Status)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st))
            return st.ullAvailPhys / 1e9
        with open('/proc/meminfo') as f:
            info = {l.split(':')[0]: int(l.split()[1]) for l in f}
        return info.get('MemAvailable', 0) * 1024 / 1e9
    except Exception:
        return None


# the steps that run processes or threads of their own (--jobs or the cores):
# each counts as half the processes allowed
POOLED = {'tools/convert_city.py', 'tools/convert_garage.py', 'tools/racing_line.py', 'tools/convert_races.py',
          'tools/convert_hud.py', 'tools/convert_vinyls.py', 'tools/convert_rims.py', 'tools/convert_movies.py',
          'tools/convert_audio.py', 'tools/convert_cars.py'}
MIN_FREE_GB = 1.5    # another step starts only with this much memory free (one runs anyway)


def run_steps(todo, graph, jobs, finished, run_one):
    """Runs the steps not in finished: with a graph, each once its
    predecessors are done, several at once (the longest remaining chain
    first; a step with processes of its own (POOLED) counts as jobs/2,
    the others as one), up to jobs; without a graph, in order. run_one(i)
    converts step i (or restores it) and returns its seconds."""
    n = len(todo)
    preds = graph.preds if graph else [{i - 1} if i else set() for i in range(n)]
    expected = [(graph.node[i].get('seconds') if graph else None) or todo[i][1] for i in range(n)]
    succ = [[] for _ in range(n)]
    for i, ps in enumerate(preds):
        for j in ps:
            succ[j].append(i)
    rank = [0.0] * n
    for i in reversed(range(n)):
        rank[i] = expected[i] + max((rank[k] for k in succ[i]), default=0.0)
    weight = [max(1, jobs // 2) if todo[i][2] and todo[i][2][0] in POOLED else 1 for i in range(n)]
    total = sum(expected) or 1
    done = set(i for i in range(n) if todo[i][0] in finished)
    pending = [i for i in range(n) if i not in done]
    for i in done:
        say(f'  [{i + 1:2d}/{n}] {sum(expected[j] for j in done) * 100 // total:3.0f}% {todo[i][0]}: já feita')
    lock = threading.Condition()
    running, errors, times = {}, [], {}
    started = len(done)

    def work(i):
        try:
            times[i] = run_one(i)
        except BaseException as e:
            errors.append((i, e))
        with lock:
            if not any(k == i for k, _ in errors):
                done.add(i)
            running.pop(i, None)
            lock.notify_all()

    with lock:
        while pending or running:
            if not errors:
                ready = sorted((i for i in pending if preds[i] <= done), key=lambda i: (-rank[i], i))
                for i in ready:
                    if running and (sum(running.values()) + weight[i] > jobs
                                    or (free_memory_gb() or MIN_FREE_GB) < MIN_FREE_GB):
                        break
                    pending.remove(i)
                    running[i] = weight[i]
                    started += 1
                    say(f'  [{started:2d}/{n}] {sum(expected[j] for j in done) * 100 // total:3.0f}% {todo[i][0]}')
                    threading.Thread(target=work, args=(i,), daemon=True).start()
            elif not running:
                break
            if not running and pending and not errors:
                raise Failure('as etapas restantes esperam umas pelas outras (manifests/steps.json inconsistente).')
            lock.wait(1)
    if errors:
        raise errors[0][1]
    return {todo[i][0]: round(t, 1) for i, t in times.items()}


def package_files(game):
    """A generated folder's files (pack.n2pk's entries too) by name: (file, offset, size); no EBOOT."""
    files = {}
    for p in game.iterdir():
        if p.is_file() and p.name not in ('EBOOT.PBP', 'pack.n2pk'):
            files[p.name] = (p, 0, None)
    pack = game / 'pack.n2pk'
    if pack.is_file():
        with open(pack, 'rb') as f:
            _, _, count, size = struct.unpack('<4sIII', f.read(16))
            index = f.read(size)
        at = 0
        for _ in range(count):
            off, length, nl = struct.unpack_from('<IIH', index, at)
            files[index[at + 10:at + 10 + nl].decode()] = (pack, off, length)
            at += 10 + nl + (-(10 + nl) % 4)
    return files


def identity_hash(inputs):
    return hashlib.sha256(json.dumps(inputs, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def previous_stamps(out, old, graph, source, movies, disc):
    """The previous generation's stamps of these steps: its own record
    (STATE), or worked out from manifests/update-from (the published
    version's code hashes and commands). (stamps, None) or (None, why)."""
    state = out / STATE
    if state.is_file():
        try:
            prev = json.loads(state.read_text())
        except ValueError:
            prev = {}
        if prev.get('tools_sha256') == old.get('tools_sha256') and prev.get('disc') == disc:
            return [prev['stamps'].get(name) for name in graph.names], None
    for path in sorted(UPDATE_FROM.glob('*.json')):
        table = json.loads(path.read_text())
        if table.get('candidate') != old.get('candidate'):
            continue
        for bundle in table['bundles']:
            inputs = dict(table['inputs'], **bundle.get('inputs', {}), **{'native-bundle.json': bundle['bundle_sha256']})
            if identity_hash(inputs) == old.get('tools_sha256'):
                listed = dict((n, c) for n, c in table['commands'][source.name]['movies' if movies else 'none'])
                commands = [listed.get(name) for name in graph.names]
                code = table.get('code', table['inputs'])
                return graph.stamps(lambda p: code.get(p), commands, bundle['runtime'], disc), None
    return None, 'esta versão do gerador não conhece a que gerou a pasta (sem os carimbos dela)'


def update_plan(out, old, graph, stamps, source, args, disc):
    """What an update of out reuses of its previous generation: (plan, None)
    or (None, why it cannot). The plan: the steps to convert again (with
    why) and the previous folder's files."""
    game = out / 'PSP/GAME/NFSU2VIEW'
    if old.get('source') != source.name:
        return None, f'a geração anterior é da fonte {old.get("source")}, não {source.name}'
    if old.get('iso_sha256') != disc.split(':', 1)[1]:
        return None, 'a geração anterior veio de outra ISO'
    if bool(old.get('movies')) != (not args.sem_filmes):
        return None, 'a geração anterior foi feita com outra opção de filmes'
    if not (game / 'EBOOT.PBP').is_file():
        return None, 'a pasta PSP/GAME/NFSU2VIEW da geração anterior está incompleta'
    if graph is None:
        return None, 'esta versão não tem o mapa das etapas (manifests/steps.json)'
    before, why = previous_stamps(out, old, graph, source, not args.sem_filmes, disc)
    if before is None:
        return None, why
    manifest = HERE / 'manifests' / f'{old.get("candidate")}-{source.name}.json'
    if not manifest.exists() and source.name == 'xbox':
        manifest = HERE / 'manifests' / f'{old.get("candidate")}.json'
    if not manifest.is_file():
        return None, 'sem a referência da versão anterior para conferir a pasta'
    say('  conferindo a pasta anterior contra a referência dela')
    _, _, differ, missing, extra = compare(game, manifest)
    if differ or missing or extra:
        return None, ('a pasta anterior foi alterada ou está incompleta (diferentes: ' + ', '.join(differ[:5] or ['nenhum'])
                      + '; faltando: ' + ', '.join(missing[:5] or ['nenhum']) + ')')
    files = package_files(game)
    cached = {p.relative_to(out / CACHE).as_posix(): (p, 0, None) for p in (out / CACHE).rglob('*') if p.is_file()} \
        if (out / STATE).is_file() and (out / CACHE).is_dir() else {}
    n = len(graph.names)
    rerun = {i: 'mudou' for i in range(n) if stamps[i] != before[i]}
    restorable = lambda p: p in cached or (p.startswith(DIST) and p[len(DIST):] in files)   # noqa: E731
    changed = True
    while changed:
        changed = False
        for i in sorted(rerun):
            for p, j in graph.inputs[i].items():
                if j not in rerun and not (restorable(p) and graph.final.get(p) == j):
                    rerun[j] = f'dá {p.rsplit("/", 1)[-1]} a {graph.names[i]}'
                    changed = True
    return dict(rerun=rerun, files=files, cached=cached, before=before), None


def restore(i, graph, plan, work):
    """A reused step: its files as the previous generation left them."""
    node = graph.node[i]
    rewritten = {p for j in plan['rerun'] for p in graph.node[j]['writes']}
    for p in node['writes']:
        if graph.final.get(p) != i:
            continue
        if p in plan['cached']:
            src, off, size = plan['cached'][p]
        elif p.startswith(DIST) and p[len(DIST):] in plan['files']:
            src, off, size = plan['files'][p[len(DIST):]]
        else:
            continue
        dst = work / p
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists() or dst.is_symlink():
            dst.unlink()
        if size is None:
            if p in rewritten:
                shutil.copyfile(src, dst)
            else:
                try:
                    os.link(src, dst)
                except OSError:
                    shutil.copyfile(src, dst)
        else:
            with open(src, 'rb') as f:
                f.seek(off)
                dst.write_bytes(f.read(size))
        # the date of now, in the steps' order, as a conversion gives it
        # (convert_mips.py --if-stale compares the archives' dates)
        os.utime(dst)
    for p in node['removes']:
        if (work / p).is_file():
            (work / p).unlink()
    return 0.0



def keep_inputs(graph, work, out, files):
    """CACHE: the files some step reads from another that the card's folder
    leaves out (converters' tables, .tools/cache), as the generation left
    them: a later update restores them instead of converting their step again."""
    keep = out / (CACHE + '.novo')
    if keep.exists():
        shutil.rmtree(keep)
    for p in sorted({p for i in range(len(graph.names)) for p in graph.inputs[i]}):
        if p.startswith(DIST) and p[len(DIST):] in files or not (work / p).is_file():
            continue
        (keep / p).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(work / p, keep / p)
    if (out / CACHE).exists():
        shutil.rmtree(out / CACHE)
    if keep.exists():
        keep.rename(out / CACHE)


def previous_generation(out):
    """The marker of the generation an update starts from (a folder half
    way through an earlier update gets its PSP folder back first)."""
    marker = out / '.nfsu2-psp-build'
    if (out / '.anterior/PSP').is_dir() and not (out / 'PSP').exists():
        shutil.move(str(out / '.anterior/PSP'), str(out / 'PSP'))
    if not marker.is_file() or marker.is_symlink() or not (out / 'PSP').is_dir():
        raise Failure(f'{out} não tem uma geração anterior desta ferramenta para atualizar; escolha a pasta da geração anterior.')
    if any(p.is_dir() for p in out.rglob('SAVEDATA')) or any(out.rglob('*.sav')):
        raise Failure('a saída contém saves. Preserve-os e escolha outra pasta de saída.')
    try:
        return json.loads(marker.read_text())
    except (ValueError, UnicodeError):
        raise Failure('o marcador da geração anterior está corrompido; gere de novo numa pasta nova.')


def set_aside(out):
    """The previous PSP folder kept in .anterior until the new one passes its check."""
    if (out / 'PSP').exists():
        (out / '.anterior').mkdir(exist_ok=True)
        if (out / '.anterior/PSP').exists():
            shutil.rmtree(out / '.anterior/PSP')
        shutil.move(str(out / 'PSP'), str(out / '.anterior/PSP'))


def package(work, out, logs, env):
    """The card's folder (tools/ms_package.py make --release --pack, as the
    reference package): no converters' reports, no test files, the small
    files in pack.n2pk, the barriers' collision as patches."""
    dist = work / 'dist/PSP/GAME/NFSU2VIEW'
    stage = work / 'pacote'
    if stage.exists():
        shutil.rmtree(stage)
    with open(logs / 'pacote.log', 'w') as f:
        r = subprocess.run([sys.executable, 'tools/ms_package.py', 'make', str(stage), '--dist', str(dist), '--eboot', str(EBOOT),
                            '--release', '--pack', '--whole-bars'], cwd=work, env=env, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        raise Failure(f'a montagem do pacote falhou: veja {logs / "pacote.log"}')
    shutil.move(str(stage / 'PSP'), str(out / 'PSP'))


def compare(game, manifest_path):
    """The package against the reference build's hashes (manifests/):
    verifies output identity; gameplay qualification is separate. A file
    whose bytes differ only by another CPU's rounding (tools/arch_tolerance.py:
    same kind, shape and size as the reference's) is equivalent.
    Returns (same, equivalent, differ, missing, extra)."""
    import arch_tolerance
    manifest = json.loads(manifest_path.read_text())
    ref, records = manifest['files'], manifest.get('equivalence', {})
    have = {p.name: p for p in game.iterdir() if p.is_file()}
    same, equivalent, differ = [], [], []
    for name, digest in sorted(ref.items()):
        if name not in have:
            continue
        if sha256(have[name]) == digest:
            same.append(name)
        elif arch_tolerance.equivalent(name, have[name].read_bytes(), records.get(name)):
            equivalent.append(name)
        else:
            differ.append(name)
    missing = sorted(set(ref) - set(have))
    extra = sorted(set(have) - set(ref))
    return same, equivalent, differ, missing, extra


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('iso', nargs='?', type=Path, help='a ISO do jogo (sem ela, a ferramenta pergunta)')
    ap.add_argument('--saida', type=Path, default=HERE / 'output', help='pasta de saída (padrão: output/ ao lado desta ferramenta)')
    ap.add_argument('--fonte', choices=sorted(SOURCES), help='de qual console é a ISO (padrão: reconhecida pela ISO)')
    ap.add_argument('--jobs', type=int, choices=range(1, 17), default=4, metavar='1..16', help='máximo de processos nas conversões paralelas (padrão: 4)')
    ap.add_argument('--do-zero', action='store_true', help='recomeça tudo, sem aproveitar as etapas já feitas')
    ap.add_argument('--manter-trabalho', action='store_true', help='não apaga a pasta .trabalho no fim (extração e conversão)')
    ap.add_argument('--sem-filmes', action='store_true', help='não converte os 28 filmes (dispensa o cjpegli; o jogo os pula)')
    ap.add_argument('--cjpegli', type=Path, help='caminho do cjpegli, se não estiver em .tools/jpegli')
    ap.add_argument('--aceitar-iso-desconhecida', action='store_true',
                    help='segue com uma ISO de hash desconhecido se os arquivos extraídos baterem')
    ap.add_argument('--so-conferir', action='store_true', help='só confere a ISO e o que falta instalar, sem converter')
    ap.add_argument('--atualizar', action='store_true',
                    help='atualiza a geração anterior em --saida: refaz só as etapas que mudaram (com a mesma ISO)')
    args = ap.parse_args()
    t_start = time.monotonic()
    try:
        say('NFSU2 PSP: gerar o jogo a partir da sua ISO\n')
        if args.iso is None:
            entered = input('Arraste a ISO para esta janela e tecle Enter: ').strip().strip('"\'')
            args.iso = Path(entered.replace('\\ ', ' ')).expanduser()
        iso = args.iso.expanduser().resolve()
        iso_size_ok(iso)
        release = validate_release()
        problems, cjpegli = preflight(args)
        if problems:
            raise Failure('falta preparar o computador:\n' + '\n'.join(f'  - {p}' for p in problems))
        say('1. Conferindo a ISO')
        source = SOURCES[args.fonte] if args.fonte else next((s for s in SOURCES.values() if s.recognise(iso)), None)
        if source is None:
            raise Failure(f'{iso.name} não parece uma ISO de NFSU2 de nenhuma fonte suportada ({", ".join(sorted(SOURCES))}).')
        if source.available and not source.recognise(iso):
            raise Failure(f'{iso.name} não parece uma imagem de disco do {source.name}.')
        if not source.available:
            raise Failure(f'a fonte {source.name} ainda não está disponível nesta versão: {source.note}')
        digest = sha256(iso, 'hash da ISO')
        known = source.known_isos.get(digest)
        if known:
            say(f'  ISO reconhecida: {known}')
        else:
            say(f'  hash {digest[:16]}… desconhecido: os arquivos extraídos serão conferidos um a um.')
        if args.so_conferir:
            say('\nTudo pronto para converter.')
            return 0
        out = args.saida.expanduser().resolve()
        identity = build_identity(digest, source, args, release)
        disc = f'{source.name}:{digest}'
        work, logs = out / '.trabalho', out / 'registros'
        old = None
        if args.atualizar:
            old = previous_generation(out)
            # an update starts afresh (a full generation's steps done are not this one's)
            if work.exists():
                shutil.rmtree(work)
            if logs.exists():
                shutil.rmtree(logs)
        else:
            prepare_out(out, args.do_zero, identity)
        free = disk_free_gb(out)
        if free < DISK_NEEDED_GB:
            raise Failure(f'pouco espaço em disco: {free:.0f} GB livres em {out}, a conversão precisa de ~{DISK_NEEDED_GB} GB.')
        work.mkdir(exist_ok=True)
        logs.mkdir(exist_ok=True)
        done_file = work / 'etapas-feitas.txt'
        done = set(done_file.read_text().splitlines()) if done_file.exists() else set()
        env = step_env(work, source)

        game_dir = work / 'game' / source.name
        game_rel = source.game_root(game_dir).relative_to(work)
        todo = steps(game_rel, movies=not args.sem_filmes, jobs=args.jobs)
        graph = graph_for(todo, source)
        runtime = runtime_identity()
        stamps = graph.stamps(lambda p: step_graph.code_sha256(HERE / p) if (HERE / p).is_file() else None,
                              [step_graph.template(cmd, game_rel) for _, _, cmd in todo], runtime, disc) if graph else None
        plan = None
        if old is not None:
            say('\n  Comparando com a geração anterior (atualização)')
            plan, why = update_plan(out, old, graph, stamps, source, args, disc)
            if plan is None:
                say(f'  Não dá para atualizar: {why}.\n  Gerando tudo de novo; a pasta anterior fica guardada até a nova ser conferida.')
                set_aside(out)
            else:
                reused = len(todo) - len(plan['rerun'])
                say(f'  {reused} de {len(todo)} etapas reaproveitadas da geração anterior; refazendo {len(plan["rerun"])}:')
                for i in sorted(plan['rerun']):
                    say(f'    - {todo[i][0]} ({plan["rerun"][i]})')
        say('\n2. Extraindo os arquivos da ISO')
        if plan is not None and not any(graph.node[i]['game'] for i in plan['rerun']):
            say('  dispensada: nenhuma etapa a refazer lê o disco (a ISO é a mesma pelo hash).')
        elif 'extração' not in done:
            if game_dir.exists():
                shutil.rmtree(game_dir)
            source.extract(iso, game_dir, say)
            bad = source.verify(game_dir)
            if bad:
                if not args.aceitar_iso_desconhecida:
                    raise Failure(f'{len(bad)} arquivos extraídos diferem do jogo esperado (ex.: {", ".join(bad[:5])}). '
                                  f'A ISO pode ser de outra região ou versão. Para tentar assim mesmo: --aceitar-iso-desconhecida.')
                say(f'  aviso: {len(bad)} arquivos diferem do esperado; seguindo por --aceitar-iso-desconhecida.')
            else:
                say(f'  {source.file_count} arquivos conferidos, iguais aos do jogo esperado.')
            with open(done_file, 'a') as f:
                f.write('extração\n')
            done.add('extração')
        else:
            say('  já feita.')
        setup_work(work, cjpegli)
        # the dist's source mark (n2source.mark_out), before any step: no step's output
        (work / DIST / 'source.txt').write_text(f'{source.name}\n')

        total = sum(s for _, s, _ in todo)
        mode = 'várias etapas ao mesmo tempo' if graph else 'uma etapa por vez'
        say(f'\n3. Convertendo ({len(todo)} etapas, {mode}, até {args.jobs} processos)')

        def run_one(i):
            name, secs, cmd = todo[i]
            if plan is not None and i not in plan['rerun']:
                return restore(i, graph, plan, work)
            took = run_step(i + 1, name, secs, cmd, work, logs, env)
            if plan is None:
                with open(done_file, 'a') as f:
                    f.write(name + '\n')
            return took
        times = run_steps(todo, graph, args.jobs, set() if plan is not None or old is not None else done, run_one)
        if plan is not None:
            times = {todo[i][0]: times[todo[i][0]] for i in sorted(plan['rerun']) if todo[i][0] in times}
        dist = work / 'dist/PSP/GAME/NFSU2VIEW'
        # files the converters once wrote one per option and per vinyl (tools/build.sh deletes them as well)
        for p in list(dist.glob('opt-*.n2pv')) + list(dist.glob('vinyl-*.n2sk')):
            if not p.name.endswith('-decal.n2pv'):
                p.unlink()
        subprocess.run([sys.executable, 'tools/data_id.py'], cwd=work, env=env, stdout=subprocess.DEVNULL, check=True)

        say('\n4. Montando a pasta do cartão')
        set_aside(out)
        package(work, out, logs, env)
        # Bind the marker to actual final logical payloads, including patched bars.
        subprocess.run([sys.executable, 'tools/data_id.py',
                        str(out / 'PSP/GAME/NFSU2VIEW'), '--normalize-package-owned'],
                       cwd=work, env=env, check=True)
        game = out / 'PSP/GAME/NFSU2VIEW'
        size = sum(p.stat().st_size for p in game.iterdir())
        work_bytes = sum(p.stat().st_size for p in work.rglob('*') if p.is_file() and not p.is_symlink())

        say('\n5. Conferindo contra a build de referência')
        manifest = HERE / 'manifests' / f'{release["candidate"]}-{source.name}.json'
        if not manifest.exists() and source.name == 'xbox':
            manifest = HERE / 'manifests' / f'{release["candidate"]}.json'
        report = {'iso_sha256': digest, 'iso_known': known, 'source': source.name, 'candidate': release.get('candidate'),
                  'files': len(list(game.iterdir())), 'bytes': size, 'work_bytes_peak': work_bytes,
                  'seconds': round(time.monotonic() - t_start), 'step_seconds': times,
                  'verification': 'no-reference', 'approved': False, 'eboot_sha256': release['eboot_sha256'],
                  'reference_verified': False,
                  'quality_approval': release.get('quality_approval', 'not-assessed'),
                  'known_issues': release.get('known_issues', [])}
        if manifest.exists():
            same, equivalent, differ, missing, extra = compare(game, manifest)
            report.update(same=len(same), equivalent=equivalent, differ=differ, missing=missing, extra=extra,
                          verification='mismatch' if differ or missing or extra else
                          'equivalent-reference' if equivalent else 'exact-reference',
                          approved=not (differ or missing or extra))
            # `approved` is retained for existing report readers: file identity,
            # or equivalence by another CPU's rounding. It never overrides known
            # issues or the release's quality status.
            report['reference_verified'] = report['approved']
            total = len(same) + len(equivalent) + len(differ) + len(missing)
            say(f'  {len(same)} de {total} arquivos idênticos à build de referência.')
            if equivalent:
                say(f'  {len(equivalent)} equivalentes: mesma estrutura, bytes arredondados de outro jeito por este processador '
                    f'(filmes, áudio, geometria ou linhas de corrida; normal fora de um Mac Apple Silicon).')
            if differ or missing or extra:
                say(f'  diferentes: {", ".join(differ) or "nenhum"}; faltando: {", ".join(missing) or "nenhum"}; '
                    f'a mais: {", ".join(extra) or "nenhum"}')
                say('  (diferenças pequenas podem vir de versões diferentes do ffmpeg, lame ou numpy; o relatório tem a lista)')
        if plan is not None:
            report['update'] = dict(from_candidate=old.get('candidate'), from_tools_sha256=old.get('tools_sha256'),
                                    reused=len(todo) - len(plan['rerun']),
                                    rerun={todo[i][0]: why for i, why in sorted(plan['rerun'].items())})
        elif old is not None:
            report['update'] = dict(from_candidate=old.get('candidate'), full_generation=True)
        (out / 'relatorio.json').write_text(json.dumps(report, indent=1, ensure_ascii=False) + '\n')
        if not report['approved']:
            if (out / '.anterior/PSP').is_dir():
                shutil.move(str(out / 'PSP'), str(work / 'PSP-nova'))
                shutil.move(str(out / '.anterior/PSP'), str(out / 'PSP'))
                say('A pasta nova não passou na conferência: ficou em .trabalho/PSP-nova, e a anterior voltou para PSP.')
            say('Pacote gerado para diagnóstico, mas NÃO validado contra a referência. Veja relatorio.json; o trabalho foi preservado.')
            return 2
        if (out / '.anterior').exists():
            shutil.rmtree(out / '.anterior')
        if old is not None:
            (out / '.nfsu2-psp-build').write_text(json.dumps(identity, sort_keys=True) + '\n')
        if stamps:
            keep_inputs(graph, work, out, package_files(game))
            (out / STATE).write_text(json.dumps(dict(schema=1, tools_sha256=identity['tools_sha256'], disc=disc, runtime=runtime,
                                                     stamps=dict(zip(graph.names, stamps))), indent=0) + '\n')
        if not args.manter_trabalho:
            shutil.rmtree(work)
        say(f'\nPronto em {human(time.monotonic() - t_start)}: {game}')
        say(f'  {report["files"]} arquivos, {size / 2**30:.1f} GB. Para instalar no PSP, veja LEIA-ME.md, "Instalar no PSP".')
        if report['known_issues']:
            say('  Limitações conhecidas deste beta: veja LIMITACOES.md.')
        return 0
    except (Failure, SourceError) as e:
        say(f'\nERRO: {e}')
        return 1
    except KeyboardInterrupt:
        say('\nInterrompido. Rode o mesmo comando para continuar de onde parou.')
        return 130


if __name__ == '__main__':
    sys.exit(main())
