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
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True   # (nothing written beside the tool: only in the output folder)
sys.path.insert(0, str(HERE))
from sources import SOURCES, SourceError  # noqa: E402

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


def say(*args):
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
def steps(game_dir, movies=True):
    out = []
    add = lambda name, secs, *cmd: out.append((name, secs, list(cmd)))  # noqa: E731
    add('carro da vitrine', 20, 'tools/convert_car.py')
    add('cidade', 900, 'tools/convert_city.py')
    add('visibilidade da cidade', 10, 'tools/convert_pvs.py')
    add('objetos derrubáveis', 15, 'tools/convert_smokeables.py')
    add('ruas', 20, 'tools/convert_routes.py')
    add('GPS', 10, 'tools/convert_gps.py')
    add('faixas', 20, 'tools/convert_lanes.py')
    add('corridas da cidade', 400, 'tools/convert_races.py')
    add('zonas de reset', 5, 'tools/convert_resetzones.py')
    for track in ('L4RB', 'L4RC', 'L4RD', 'L4RF', 'L4RG'):
        add(f'pista {track}', 150, 'tools/convert_city.py', '--track', track)
        add(f'ruas da pista {track}', 10, 'tools/convert_routes.py', '--track', track)
        add(f'corridas da pista {track}', 60, 'tools/convert_races.py', '--track', track)
    add('câmeras do replay', 60, 'tools/convert_tvcams.py')
    add('limites da geometria', 60, ('glob', 'tools/n2cy_bounds.py', '--if-missing'))
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
    add('áudio', 300, 'tools/convert_audio.py')
    if movies:
        add('filmes', 600, 'tools/convert_movies.py', '--missing')
        add('conferência dos filmes', 5, 'tools/movie_release.py', 'dist/PSP/GAME/NFSU2VIEW')
    for preset in sorted((HERE / 'presets/opponents').glob('*.json')):
        add(f'carro rival {preset.stem}', 30, 'tools/convert_car.py', '--preset', f'presets/opponents/{preset.name}',
            '--tag', f'opp-{preset.stem}')
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
    for cand in [explicit, os.environ.get('NFSU2_CJPEGLI'), HERE / '.tools/jpegli/build/tools/cjpegli',
                 HERE / '.tools/jpegli/build/tools/cjpegli.exe', shutil.which('cjpegli')]:
        if cand and Path(cand).is_file():
            return Path(cand).resolve()
    return None


def preflight(args):
    """Everything the conversion needs, checked before it starts: the
    player sees every missing piece at once."""
    problems = []
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
        if not shutil.which(tool):
            problems.append(f'falta o programa {tool} (macOS: brew install {tool}; Linux: apt install {tool}).')
    if not shutil.which('cc'):
        problems.append('falta um compilador C chamado cc (macOS: xcode-select --install; Linux: apt install build-essential).')
    cjpegli = None
    if not args.sem_filmes:
        cjpegli = find_cjpegli(args.cjpegli)
        if not cjpegli:
            problems.append('falta o cjpegli, o codificador dos filmes (rode ./setup.sh, que o compila em .tools/jpegli; '
                            'ou use --sem-filmes).')
    if not EBOOT.is_file():
        problems.append(f'falta o EBOOT pré-compilado ({EBOOT}).')
    return problems, cjpegli


def prepare_out(out, fresh):
    """The output folder: new, or one of ours (resumed). Never a folder
    with other things in it: the tool writes only inside its own."""
    marker = out / '.nfsu2-psp-build'
    if out.exists() and any(out.iterdir()):
        if not marker.exists():
            raise Failure(f'a pasta de saída {out} já existe e não é desta ferramenta. Escolha uma pasta vazia ou nova.')
        if fresh:
            for p in out.iterdir():
                if p.name != marker.name:
                    shutil.rmtree(p) if p.is_dir() and not p.is_symlink() else p.unlink()
    out.mkdir(parents=True, exist_ok=True)
    marker.write_text('Pasta gerada pelo build_game.py do NFSU2 PSP.\n')
    if (out / 'PSP').exists():
        raise Failure(f'{out / "PSP"} já existe: o jogo já foi gerado nessa pasta. Use --do-zero para gerar de novo.')


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


def step_env(work):
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


def run_step(i, name, secs, cmd, work, logs, env):
    log = logs / f'{i:02d}-{name.replace(" ", "-")}.log'
    t0 = time.monotonic()
    with open(log, 'w') as f:
        for c in expand(cmd, work):
            f.write('$ ' + ' '.join(['python'] + c) + '\n')
            f.flush()
            p = subprocess.Popen(queued(name, [sys.executable] + c, env), cwd=work, env=env, stdout=f, stderr=subprocess.STDOUT)
            last = time.monotonic()
            while p.poll() is None:
                time.sleep(0.5)
                if time.monotonic() - last > 30:
                    last = time.monotonic()
                    say(f'           … {name}: {human(time.monotonic() - t0)} (esperado ~{human(secs)})')
            if p.returncode:
                tail = log.read_text(errors='replace').splitlines()[-25:]
                raise Failure(f'a etapa "{name}" falhou (código {p.returncode}). Últimas linhas do registro {log}:\n'
                              + '\n'.join('    ' + t for t in tail))
    return time.monotonic() - t0


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
                            '--release', '--pack'], cwd=work, env=env, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        raise Failure(f'a montagem do pacote falhou: veja {logs / "pacote.log"}')
    shutil.move(str(stage / 'PSP'), str(out / 'PSP'))


def compare(game, manifest_path):
    """The package against the reference build's hashes (manifests/):
    the same bytes prove the same game. Returns (same, differ, missing, extra)."""
    ref = json.loads(manifest_path.read_text())['files']
    have = {p.name: p for p in game.iterdir() if p.is_file()}
    same, differ = [], []
    for name, digest in sorted(ref.items()):
        if name in have:
            (same if sha256(have[name]) == digest else differ).append(name)
    missing = sorted(set(ref) - set(have))
    extra = sorted(set(have) - set(ref))
    return same, differ, missing, extra


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('iso', nargs='?', type=Path, help='a ISO do jogo (sem ela, a ferramenta pergunta)')
    ap.add_argument('--saida', type=Path, default=HERE / 'saida', help='pasta de saída (padrão: saida/ ao lado desta ferramenta)')
    ap.add_argument('--fonte', choices=sorted(SOURCES), help='de qual console é a ISO (padrão: reconhecida pela ISO)')
    ap.add_argument('--do-zero', action='store_true', help='recomeça tudo, sem aproveitar as etapas já feitas')
    ap.add_argument('--manter-trabalho', action='store_true', help='não apaga a pasta .trabalho no fim (extração e conversão)')
    ap.add_argument('--sem-filmes', action='store_true', help='não converte os 28 filmes (dispensa o cjpegli; o jogo os pula)')
    ap.add_argument('--cjpegli', type=Path, help='caminho do cjpegli, se não estiver em .tools/jpegli')
    ap.add_argument('--aceitar-iso-desconhecida', action='store_true',
                    help='segue com uma ISO de hash desconhecido se os arquivos extraídos baterem')
    ap.add_argument('--so-conferir', action='store_true', help='só confere a ISO e o que falta instalar, sem converter')
    args = ap.parse_args()
    t_start = time.monotonic()
    try:
        say('NFSU2 PSP: gerar o jogo a partir da sua ISO\n')
        if args.iso is None:
            entered = input('Arraste a ISO para esta janela e tecle Enter: ').strip().strip('"\'')
            args.iso = Path(entered.replace('\\ ', ' ')).expanduser()
        iso = args.iso.expanduser().resolve()
        iso_size_ok(iso)
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
        prepare_out(out, args.do_zero)
        free = disk_free_gb(out)
        if free < DISK_NEEDED_GB:
            raise Failure(f'pouco espaço em disco: {free:.0f} GB livres em {out}, a conversão precisa de ~{DISK_NEEDED_GB} GB.')
        work, logs = out / '.trabalho', out / 'registros'
        work.mkdir(exist_ok=True)
        logs.mkdir(exist_ok=True)
        done_file = work / 'etapas-feitas.txt'
        done = set(done_file.read_text().splitlines()) if done_file.exists() else set()
        env = step_env(work)

        say('\n2. Extraindo os arquivos da ISO')
        game_dir = work / 'game' / source.name
        if 'extração' not in done:
            if game_dir.exists():
                shutil.rmtree(game_dir)
            source.extract(iso, game_dir, say)
            bad = source.verify(game_dir)
            if bad:
                if not known and not args.aceitar_iso_desconhecida:
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

        todo = steps(source.game_root(game_dir).relative_to(work), movies=not args.sem_filmes)
        total = sum(s for _, s, _ in todo)
        say(f'\n3. Convertendo ({len(todo)} etapas, ~{human(total)} no Mac de referência)')
        spent = 0.0
        times = {}
        for i, (name, secs, cmd) in enumerate(todo, 1):
            pct = spent * 100 // total
            if name in done:
                say(f'  [{i:2d}/{len(todo)}] {pct:3.0f}% {name}: já feita')
                spent += secs
                continue
            say(f'  [{i:2d}/{len(todo)}] {pct:3.0f}% {name}')
            took = run_step(i, name, secs, cmd, work, logs, env)
            times[name] = round(took, 1)
            spent += secs
            with open(done_file, 'a') as f:
                f.write(name + '\n')
        dist = work / 'dist/PSP/GAME/NFSU2VIEW'
        # files the converters once wrote one per option and per vinyl (tools/build.sh deletes them as well)
        for p in list(dist.glob('opt-*.n2pv')) + list(dist.glob('vinyl-*.n2sk')):
            if not p.name.endswith('-decal.n2pv'):
                p.unlink()
        subprocess.run([sys.executable, 'tools/data_id.py'], cwd=work, env=env, stdout=subprocess.DEVNULL, check=True)

        say('\n4. Montando a pasta do cartão')
        package(work, out, logs, env)
        game = out / 'PSP/GAME/NFSU2VIEW'
        size = sum(p.stat().st_size for p in game.iterdir())
        work_bytes = sum(p.stat().st_size for p in work.rglob('*') if p.is_file() and not p.is_symlink())

        say('\n5. Conferindo contra a build de referência')
        release = json.loads(RELEASE.read_text()) if RELEASE.exists() else {}
        manifest = HERE / 'manifests' / f'{release.get("candidate", "?")}.json'
        report = {'iso_sha256': digest, 'iso_known': known, 'source': source.name, 'candidate': release.get('candidate'),
                  'files': len(list(game.iterdir())), 'bytes': size, 'work_bytes_peak': work_bytes,
                  'seconds': round(time.monotonic() - t_start), 'step_seconds': times}
        if manifest.exists():
            same, differ, missing, extra = compare(game, manifest)
            report.update(same=len(same), differ=differ, missing=missing, extra=extra)
            say(f'  {len(same)} de {len(same) + len(differ) + len(missing)} arquivos idênticos à build de referência.')
            if differ or missing or extra:
                say(f'  diferentes: {", ".join(differ) or "nenhum"}; faltando: {", ".join(missing) or "nenhum"}; '
                    f'a mais: {", ".join(extra) or "nenhum"}')
                say('  (diferenças pequenas podem vir de versões diferentes do ffmpeg, lame ou numpy; o relatório tem a lista)')
        (out / 'relatorio.json').write_text(json.dumps(report, indent=1, ensure_ascii=False) + '\n')
        if not args.manter_trabalho:
            shutil.rmtree(work)
        say(f'\nPronto em {human(time.monotonic() - t_start)}: {game}')
        say(f'  {report["files"]} arquivos, {size / 2**30:.1f} GB. Para instalar no PSP, veja LEIA-ME.md, "Instalar no PSP".')
        return 0
    except (Failure, SourceError) as e:
        say(f'\nERRO: {e}')
        return 1
    except KeyboardInterrupt:
        say('\nInterrompido. Rode o mesmo comando para continuar de onde parou.')
        return 130


if __name__ == '__main__':
    sys.exit(main())
