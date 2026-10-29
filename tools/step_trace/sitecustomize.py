"""The generator's step tracer: what a converter reads and writes.

build_game.py, with NFSU2_STEP_TRACE set (maintainers only), runs each step
with this folder on PYTHONPATH and NFSU2_TRACE_LOG set, so every Python
process of the step
(the converter, its subprocesses, its pools' workers: they inherit the
environment) loads this module at startup. It logs one line per event,
appended to that file:

  r PATH   opened for reading (open, io.open_code, os.open, the .pyc of an
           imported module as its source .py)
  w PATH   opened for writing (also r+ and in place)
  s PATH   probed with os.stat/os.lstat (exists, is_file, stat...) and there
  a PATH   probed and absent
  l PATH   listed (os.listdir, os.scandir, so iterdir and the globs below)
  g DIR/PATTERN  a glob of the names of DIR matching PATTERN (one level:
           glob.glob, glob.iglob, Path.glob; its listing is not logged).
           Under the tracer those return their whole result at once
  m PATH   moved away (os.rename/os.replace's source) or removed
  x EXE    a program started (and its arguments, tab-separated)
  p EXE    a Python started (sys.executable, its version, its site-packages)
  c PPID   that Python's parent process and command line (tab-separated)

Only paths under NFSU2_TRACE_ROOTS (colon-separated) are logged, less
those holding a piece of NFSU2_TRACE_SKIP unless they hold one of
NFSU2_TRACE_KEEP. tools/step_graph.py turns the logs into the steps'
dependencies (manifests/steps.json). Nothing else changes: the hook only writes to the log. Afterwards it runs the
sitecustomize it hides (Homebrew's), as Python would have.
"""
import os
import sys
import threading


def _install():
    log = os.environ.get('NFSU2_TRACE_LOG')
    if not log:
        return
    roots = tuple(r.rstrip('/') + '/' for r in os.environ.get('NFSU2_TRACE_ROOTS', '').split(':') if r)
    skip = tuple(s for s in os.environ.get('NFSU2_TRACE_SKIP', '').split(':') if s)
    keep = tuple(s for s in os.environ.get('NFSU2_TRACE_KEEP', '').split(':') if s)
    fd = os.open(log, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_CLOEXEC, 0o644)
    pid = os.getpid()
    fsdecode, abspath, write = os.fsdecode, os.path.abspath, os.write
    wflags = os.O_WRONLY | os.O_RDWR | os.O_APPEND | os.O_CREAT | os.O_TRUNC

    def emit(kind, path):
        write(fd, f'{kind}\t{pid}\t{path}\n'.encode('utf-8', 'surrogateescape'))

    def norm(path):
        if path is None or isinstance(path, int):
            return None
        try:
            path = abspath(fsdecode(path))
        except (TypeError, ValueError):
            return None
        if not path.startswith(roots):
            return None
        if any(s in path for s in skip) and not any(k in path for k in keep):
            return None
        return path

    def on_open(args):
        path, mode, flags = args
        p = norm(path)
        if p is None:
            return
        if p.endswith('.pyc') and '/__pycache__/' in p:   # an imported module: its source
            d, name = p.rsplit('/__pycache__/', 1)
            emit('r', f"{d}/{name.split('.', 1)[0]}.py")
            return
        if isinstance(flags, int) and flags >= 0:
            w = bool(flags & wflags)
        else:
            w = isinstance(mode, str) and any(c in mode for c in 'wax+')
        emit('w' if w else 'r', p)

    getframe = sys._getframe
    globbing = threading.local()

    def on_list(args):
        if getattr(globbing, 'on', False):
            return
        f = getframe(2)   # the import system's own listings (FileFinder) are not the step's
        for _ in range(3):
            if f is None:
                break
            if f.f_code.co_filename.startswith('<frozen importlib'):
                return
            f = f.f_back
        p = norm(args[0] if args and args[0] is not None else '.')
        if p is not None:
            emit('l', p)

    def on_rename(args):
        src, dst = norm(args[0]), norm(args[1])
        if src is not None:
            emit('m', src)
        if dst is not None:
            emit('w', dst)

    def on_remove(args):
        p = norm(args[0])
        if p is not None:
            emit('m', p)

    def on_popen(args):
        executable, argv, cwd, env = args
        argv = [argv] if isinstance(argv, (str, bytes)) else list(argv or ())
        exe = executable or (argv[0] if argv else '')
        emit('x', '\t'.join([fsdecode(exe)] + [fsdecode(a) if isinstance(a, (str, bytes)) else str(a) for a in argv[1:]]).replace('\n', ' '))
        if isinstance(env, dict):   # a child with its own environment still traced
            for k in ('NFSU2_TRACE_LOG', 'NFSU2_TRACE_ROOTS', 'PYTHONPATH'):
                if k in os.environ and k not in env:
                    env[k] = os.environ[k]

    table = {'open': on_open, 'os.listdir': on_list, 'os.scandir': on_list, 'os.rename': on_rename,
             'os.remove': on_remove, 'os.unlink': on_remove, 'subprocess.Popen': on_popen}

    def hook(event, args):
        f = table.get(event)
        if f is not None:
            try:
                f(args)
            except Exception:
                pass

    real_stat, real_lstat = os.stat, os.lstat

    def probe(real):
        def stat(path, *a, **k):
            try:
                r = real(path, *a, **k)
            except FileNotFoundError:
                p = norm(path)
                if p is not None:
                    emit('a', p)
                raise
            p = norm(path)
            if p is not None:
                emit('s', p)
            return r
        stat.__name__, stat.__doc__ = real.__name__, real.__doc__
        return stat

    os.stat, os.lstat = probe(real_stat), probe(real_lstat)

    def one_level(real, where, eager):
        """real (a glob) logged as its directory and pattern when the pattern
        is a single level: what it lists matters for those names only."""
        def glob(*a, **k):
            at = None if getattr(globbing, 'on', False) else where(*a, **k)
            p = norm(at[0]) if at else None
            if p is None:
                return real(*a, **k)
            emit('g', f'{p}/{at[1]}')
            globbing.on = True
            try:
                found = list(real(*a, **k))
            finally:
                globbing.on = False
            return found if eager else iter(found)
        glob.__name__, glob.__doc__ = real.__name__, real.__doc__
        return glob

    def path_glob(self, pattern, *a, **k):
        pattern = os.fspath(pattern)
        if '/' in pattern or os.sep in pattern or '**' in pattern:
            return None
        return os.fspath(self), pattern

    def glob_glob(pathname, *a, root_dir=None, dir_fd=None, **k):
        pathname = os.fsdecode(pathname)
        d, base = os.path.split(pathname)
        if root_dir is not None or dir_fd is not None or glob_module.has_magic(d) or '**' in base:
            return None
        return d or '.', base

    import glob as glob_module
    import pathlib
    glob_module.glob = one_level(glob_module.glob, glob_glob, True)
    glob_module.iglob = one_level(glob_module.iglob, glob_glob, False)
    pathlib.Path.glob = one_level(pathlib.Path.glob, path_glob, False)
    sys.addaudithook(hook)
    site = [p for p in sys.path if p.endswith('site-packages')]
    emit('p', '\t'.join([os.path.realpath(sys.executable), sys.version.split()[0]] + site))
    emit('c', '\t'.join([str(os.getppid())] + list(getattr(sys, 'orig_argv', sys.argv))).replace('\n', ' '))


def _chain():
    """The sitecustomize this one hides (Homebrew's Python has one)."""
    import importlib.machinery
    import importlib.util
    here = os.path.dirname(os.path.abspath(__file__))
    path = [p for p in sys.path if os.path.abspath(p or '.') != here]
    spec = importlib.machinery.PathFinder.find_spec('sitecustomize', path)
    if spec is None or spec.loader is None:
        return
    mod = importlib.util.module_from_spec(spec)
    sys.modules['sitecustomize'] = mod
    spec.loader.exec_module(mod)


_install()
_chain()
