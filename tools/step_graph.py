#!/usr/bin/env python3
"""The generator's steps as a graph: what each one reads and writes (maintainers).

build_game.py runs its steps in parallel and updates a previous output by
this graph (manifests/steps.json): a step waits for the steps whose files
it reads, and a step whose code, parameters and inputs are those of the
previous version's is not converted again. The graph comes from a traced
complete generation (tools/step_trace/sitecustomize.py): every Python
process of every step logs the files it opens, probes, lists, writes and
removes in the work folder.

    step_graph.py build OUT TRACE [TRACE...] --steps manifests/steps.json --source S
        the graph of OUT (an output folder generated with NFSU2_TRACE_LOG=TRACE,
        NFSU2_TRACE_ROOTS=OUT and PYTHONPATH=tools/step_trace; or TRACE a
        folder: build_game.py's NFSU2_STEP_TRACE, a trace per step, whose
        parallel order it checks); with an existing --steps, merged into it
        (the other source's generation adds its own reads)
    step_graph.py prior TREE --bundle BUNDLE.json [...] --out manifests/update-from/CANDIDATE.json
        what an update needs of a published version: the hashes of its
        conversion code (TREE: its folder), its steps' commands and the
        runtimes of its native packages (their bundle.json). Hashes of the
        project's own files only: no game data.

The paths are relative to the work folder: tools/, presets/ and art/ are
code, game/ is the disc, dist/ and .tools/cache/ the data the steps pass on.
"""
import argparse
from fnmatch import fnmatchcase
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

CODE = ('tools/', 'presets/', 'art/')
IGNORE = ('tmp/', 'mpl/', 'pycache/', 'registros/', '.tools/jpegli/')
NEUTRAL = {'tools/mac_slots.py', 'tools/step_graph.py'}   # the queue does not change what a converter writes
# written by build_game.py before the steps (setup_work): no step's output
SETUP = {'dist/PSP/GAME/NFSU2VIEW/source.txt'}


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def code_sha256(path):
    """A code file's hash for the stamps: a text file with its line ends as
    LF (the Windows package's text files are CRLF; the same code), any
    other file as it is."""
    data = Path(path).read_bytes()
    if b'\0' not in data:
        try:
            data.decode('utf-8')
        except UnicodeDecodeError:
            pass
        else:
            data = data.replace(b'\r\n', b'\n')
    return hashlib.sha256(data).hexdigest()


def strip_jobs(command):
    """A step's command without --jobs N: the processes it uses, not what it writes."""
    out, skip = [], False
    for a in command:
        if skip:
            skip = False
        elif a == '--jobs':
            skip = True
        else:
            out.append(a)
    return out


def generic(command, game_dir):
    """The command with the disc's folder as {game}: the same for both sources' folders."""
    return [a.replace(str(game_dir), '{game}') if game_dir else a for a in strip_jobs(command)]


def template(command, game_dir=None):
    """A step's command as the graph and the stamps keep it: a glob step
    (build_game.expand) as ['glob', tool, flag], --jobs out, the disc's
    folder as {game}."""
    if command and isinstance(command[0], (list, tuple)):
        return ['glob'] + [str(a) for a in command[0][1:]] + generic(command[1:], game_dir)
    return generic([str(a) for a in command], game_dir)


def load(path):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None
    return data if data.get('schema') == 1 else None


class Graph:
    """The steps of one generation (names in build_game.steps' order) over
    steps.json: each step's predecessors (it reads what they write, or
    writes what they read or write), its producers (the steps whose output
    it reads), and each file's last writer."""

    def __init__(self, nodes, names):
        self.names = names
        self.node = [nodes[n] for n in names]
        n = len(names)
        written = {}
        for i, node in enumerate(self.node):
            for p in node['writes'] + node['removes']:
                written.setdefault(p, []).append(i)
        self.writers = written
        self.final = {p: w[-1] for p, w in written.items()}
        children = {}
        for p in written:
            children.setdefault(p.rsplit('/', 1)[0] if '/' in p else '', set()).add(p)
        self.preds = [set() for _ in range(n)]
        self.inputs = [dict() for _ in range(n)]   # path -> its producer (the last writer before the step)
        last, readers = {}, {}
        for i, node in enumerate(self.node):
            seen = set(node['reads']) | {c for d in node['lists'] for c in children.get(d, ())}
            for g in node.get('globs', ()):   # a one-level glob: the names it matches
                d, pattern = g.rsplit('/', 1)
                seen |= {c for c in children.get(d, ()) if fnmatchcase(c.rsplit('/', 1)[1], pattern)}
            for p in seen:
                if p in last:
                    self.inputs[i][p] = last[p]
                    self.preds[i].add(last[p])
            for p in node['writes'] + node['removes']:
                if p in last:
                    self.preds[i].add(last[p])
                self.preds[i].update(readers.get(p, ()))
            for p in seen:
                readers.setdefault(p, set()).add(i)
            for p in node['writes'] + node['removes']:
                last[p] = i
                readers[p] = set()
            self.preds[i].discard(i)

    def producers(self, i):
        return sorted(set(self.inputs[i].values()))

    def stamps(self, code_hash, commands, runtime, disc):
        """Each step's stamp: its code (each file it reads, and those it
        found absent), its command, the runtime, the disc when it reads it
        and its producers' stamps. The same stamp: the same bytes out."""
        out = []
        for i, node in enumerate(self.node):
            h = hashlib.sha256(json.dumps([
                node['name'], commands[i],
                [[p, code_hash(p)] for p in node['code']], [[p, code_hash(p)] for p in node['absent_code']],
                runtime, disc if node['game'] else None, [out[j] for j in self.producers(i)],
            ], separators=(',', ':')).encode()).hexdigest()
            out.append(h)
        return out


class Step:
    def __init__(self, name):
        self.name = name
        self.commands = []
        self.first = {}       # path -> the first event's kind
        self.writes, self.removes, self.lists, self.globs = set(), set(), set(), set()
        self.code, self.absent_code, self.game, self.programs = set(), set(), False, set()

    def event(self, kind, rel):
        if rel.startswith(IGNORE) or not rel or rel in SETUP:
            return
        if rel.startswith('game/'):
            self.game = True
            return
        if rel.startswith(CODE):
            if rel in NEUTRAL or rel.endswith('/sitecustomize.py'):
                return
            if kind in 'rs':
                self.code.add(rel)
            elif kind == 'a':
                self.absent_code.add(rel)
            elif kind in 'lg':
                self.lists.add(rel)
            return
        if kind == 'g':
            self.globs.add(rel)
            return
        self.first.setdefault(rel, kind)
        if kind == 'w':
            self.writes.add(rel)
            self.removes.discard(rel)
        elif kind == 'm':
            self.removes.add(rel)
            self.writes.add(rel)
        elif kind == 'l':
            self.lists.add(rel)

    def record(self, seconds=None):
        reads = sorted(p for p, k in self.first.items() if k in 'rsa' and p not in self.lists)
        out = dict(name=self.name, commands=self.commands, code=sorted(self.code), absent_code=sorted(self.absent_code - self.code),
                   game=self.game, reads=reads, writes=sorted(self.writes - self.removes), removes=sorted(self.removes),
                   lists=sorted(self.lists), globs=sorted(self.globs), programs=sorted(self.programs))
        if seconds is not None:
            out['seconds'] = seconds
        return out


def trace_lines(paths):
    for path in paths:
        with open(path, encoding='utf-8', errors='surrogateescape') as f:
            for line in f:
                parts = line.rstrip('\n').split('\t')
                if len(parts) >= 3 and parts[1].isdigit():
                    yield parts[0], int(parts[1]), parts[2:]


def expected_commands(out):
    """The steps' commands as build_game.py ran them, in order: from the
    logs in OUT/registros ($ python COMMAND lines)."""
    order = []
    for log in sorted((out / 'registros').glob('[0-9][0-9]-*.log'), key=lambda p: int(p.name.split('-', 1)[0])):
        name = log.stem.split('-', 1)[1].replace('-', ' ')
        cmds = [line[len('$ python '):].split(' ') for line in log.read_text(errors='replace').splitlines()
                if line.startswith('$ python ')]
        order.append((name, cmds))
    return order


def build(out, traces):
    work = (out / '.trabalho').resolve()
    roots = [str(work) + '/', os.path.realpath(work) + '/']
    order = expected_commands(out)
    steps = [Step(n) for n, _ in order]
    queue = [(i, c) for i, (_, cmds) in enumerate(order) for c in cmds]
    owner, pending, main = {}, {}, None

    def rel(path):
        for r in roots:
            if path.startswith(r):
                return path[len(r):]
        return None

    for kind, pid, rest in trace_lines(traces):
        if kind == 'c':
            ppid, argv = int(rest[0]), rest[1:]
            if main is None and any(a.endswith('build_game.py') for a in argv):
                main = pid
                continue
            if ppid == main:
                args = argv[1:]
                hit = next((k for k, (i, c) in enumerate(queue) if c == args), None)
                owner[pid] = queue.pop(hit)[0] if hit is not None else None   # data_id, the package: not steps
                if hit is not None:
                    steps[owner[pid]].commands.append(args)
            else:
                owner[pid] = owner.get(ppid)
            for k, p in pending.pop(pid, []):
                if owner[pid] is not None:
                    steps[owner[pid]].event(k, p)
            continue
        if pid == main or kind == 'p':
            continue
        if kind == 'x':
            if pid in owner and owner[pid] is not None:
                steps[owner[pid]].programs.add(Path(rest[0]).name)
            continue
        r = rel(rest[0])
        if r is None:
            continue
        if pid not in owner:
            pending.setdefault(pid, []).append((kind, r))
        elif owner[pid] is not None:
            steps[owner[pid]].event(kind, r)
    if queue:
        sys.exit(f'{len(queue)} commands of the logs not found in the trace (first: {queue[0]})')
    report = json.loads((out / 'relatorio.json').read_text()) if (out / 'relatorio.json').is_file() else {}
    seconds = report.get('step_seconds', {})
    return [s.record(seconds.get(s.name)) for s in steps]


def build_per_step(folder, work):
    """The graph from build_game.py's NFSU2_STEP_TRACE folder: NN.trace (every
    process of step NN) and NN.json (its name, commands, time, memory)."""
    roots = [str(work) + '/', os.path.realpath(work) + '/']
    out = []
    for meta in sorted(Path(folder).glob('[0-9][0-9]*.json'), key=lambda p: int(p.stem)):
        info = json.loads(meta.read_text())
        step = Step(info['name'])
        step.commands = [[str(a) for a in c] for c in info['commands']]
        trace = meta.with_suffix('.trace')
        for kind, pid, rest in trace_lines([trace] if trace.is_file() else []):
            if kind in 'pc':
                continue
            if kind == 'x':
                step.programs.add(Path(rest[0]).name)
                continue
            path = rest[0]
            r = next((path[len(x):] for x in roots if path.startswith(x)), None)
            if r is not None:
                step.event(kind, r)
        rec = step.record(info.get('seconds'))
        rec['peak_rss_mb'] = round(info.get('peak_rss_kb', 0) / 1024)
        rec['began'], rec['ended'] = info.get('began'), info.get('ended')
        out.append(rec)
    return out


def check_order(graph):
    """In a parallel run (build_per_step's began/ended): the steps that read
    a file before its producer, by the graph, had finished. [] when none."""
    by = {s['name']: s for s in graph}
    g = Graph(by, [s['name'] for s in graph])
    bad = []
    for i, s in enumerate(graph):
        for p, j in g.inputs[i].items():
            if s.get('began') and graph[j].get('ended') and graph[j]['ended'] > s['began']:
                bad.append((s['name'], p, graph[j]['name']))
    return bad


def merge(old, new):
    """Two sources' graphs of the same steps: the union of what each read and wrote."""
    by = {s['name']: s for s in old}
    out = []
    for s in new:
        o = by.get(s['name'])
        if o:
            for k in ('code', 'absent_code', 'reads', 'writes', 'removes', 'lists', 'globs', 'programs'):
                s[k] = sorted(set(s.get(k, ())) | set(o.get(k, ())))
            s['absent_code'] = sorted(set(s['absent_code']) - set(s['code']))
            s['game'] = s['game'] or o['game']
            if o.get('seconds') and s.get('seconds'):
                s['seconds'] = max(s['seconds'], o['seconds'])
        out.append(s)
    return out


def tree_inputs(tree):
    """build_game.build_identity's inputs of a version's folder (without its native bundle)."""
    tree = Path(tree)
    inputs = {}
    for root in ('tools', 'presets', 'sources', 'art'):
        for path in sorted((tree / root).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts and path.name != '.DS_Store':
                inputs[path.relative_to(tree).as_posix()] = sha256(path)
    inputs['build_game.py'] = sha256(tree / 'build_game.py')
    return inputs


def runtime_id(bundle):
    """The included runtime of a native package (its bundle.json): Python,
    the libraries and the converters' programs."""
    files = bundle['files']
    return hashlib.sha256(json.dumps(sorted((k, v) for k, v in files.items() if k.startswith('runtime/')),
                                     separators=(',', ':')).encode()).hexdigest()


def prior(args):
    tree = Path(args.tree).resolve()
    release = json.loads((tree / 'eboot/release.json').read_text())
    # the version's own step list and commands, per source (its build_game.steps)
    code = ('import json, sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); import build_game as b; '
            'print(json.dumps({m: [[n, c] for n, _, c in b.steps(Path(sys.argv[2]), movies=m == "movies")] '
            'for m in ("movies", "none")}))')
    commands = {}
    for source in ('xbox', 'ps2'):
        game = f'game/{source}/NFSUNDER' if source == 'xbox' else f'game/{source}/NFSUNDER'
        listed = json.loads(subprocess.run([sys.executable, '-c', code, str(tree), game], check=True, capture_output=True,
                                           text=True, cwd=tree, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1')).stdout)
        commands[source] = {m: [[n, template(c, game)] for n, c in v] for m, v in listed.items()}
    inputs = tree_inputs(tree)
    code = {p: code_sha256(tree / p) for p in inputs}   # the stamps' hashes (line ends as LF)
    bundles = []
    for path in args.bundle:
        data = Path(path).read_bytes()
        bundle = json.loads(data)
        # the package's own files, as its generations' marker hashed them
        # (the Windows package's text files are CRLF)
        files = {p: bundle['files'][p] for p in inputs}
        bundles.append(dict(platform=bundle['platform'], bundle_sha256=hashlib.sha256(data).hexdigest(),
                            runtime=runtime_id(bundle), inputs={p: h for p, h in files.items() if h != inputs[p]}))
    table = dict(schema=2, candidate=release['candidate'], eboot_sha256=release['eboot_sha256'],
                 inputs=inputs, code=code, commands=commands, bundles=bundles)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(table, indent=1, sort_keys=True) + '\n')
    print(f'{args.out}: {len(table["inputs"])} files, {len(bundles)} packages')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('out', type=Path)
    b.add_argument('traces', nargs='+', type=Path)
    b.add_argument('--steps', type=Path, required=True)
    b.add_argument('--source', required=True)
    p = sub.add_parser('prior')
    p.add_argument('tree')
    p.add_argument('--bundle', action='append', default=[])
    p.add_argument('--out', required=True)
    args = ap.parse_args()
    if args.cmd == 'prior':
        return prior(args)
    if len(args.traces) == 1 and args.traces[0].is_dir():
        graph = build_per_step(args.traces[0], (args.out / '.trabalho').resolve())
    else:
        graph = build(args.out, args.traces)
    game = f'game/{args.source}/NFSUNDER'
    for s in graph:
        s['commands'] = [generic(c, game) for c in s['commands']]
    old = json.loads(args.steps.read_text()) if args.steps.is_file() else None
    sources = sorted(set((old or {}).get('sources', [])) | {args.source})
    if old:
        graph = merge(old['steps'], graph)
    # a parallel run: no step may have read a file before its producer (by
    # the merged graph) had finished; else its order was not the sources'
    for name, path, producer in check_order(graph):
        print(f'ORDER: {name} started before {producer} finished and reads {path}')
    for s in graph:
        s.pop('began', None), s.pop('ended', None)
    args.steps.write_text(json.dumps(dict(schema=1, sources=sources, steps=graph), indent=1) + '\n')
    print(f'{args.steps}: {len(graph)} steps, sources {", ".join(sources)}')


if __name__ == '__main__':
    main()
