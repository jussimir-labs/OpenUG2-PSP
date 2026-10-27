#!/bin/sh
# Prepare this computer for build_game.py (macOS or Linux, including Windows
# WSL): Python converter modules go in .venv/ and the movie encoder cjpegli
# in .tools/jpegli, both inside this folder. System tools (ffmpeg, lame,
# a C compiler and cmake) come from Homebrew on macOS or apt on Linux.
# The script shows the command and asks before installing. It can be rerun;
# already prepared tools are kept.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"
# the jpegli of the reference build (its movies' bytes)
JPEGLI_COMMIT=031a0077f5799a6041004267fc12b956c1f52a20

ask() {   # ask "question": 0 for yes (always no without a terminal)
  [ "${NFSU2_SETUP_APPROVED:-0}" = 1 ] && return 0
  [ -t 0 ] || return 1
  printf '%s [y/N] ' "$1"
  read -r answer || return 1
  case "$answer" in s|S|sim|y|Y|yes) return 0 ;; *) return 1 ;; esac
}

os=$(uname -s)
missing=""
for tool in ffmpeg lame cc git cmake; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ "$os" = Darwin ]; then
  if ! xcode-select -p >/dev/null 2>&1; then
    echo "Apple command-line tools are missing. Run: xcode-select --install, then run this script again."
    exit 1
  fi
  pkgs="ffmpeg lame cmake ninja giflib jpeg-turbo libpng"   # (zlib: the system's)
  if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is missing (https://brew.sh). Install it, then run this script again."
    exit 1
  fi
  need=""
  for p in $pkgs; do brew list --versions "$p" >/dev/null 2>&1 || need="$need $p"; done
  if [ -n "$need" ]; then
    echo "Missing Homebrew packages:$need"
    if ask "Install now (brew install$need)?"; then brew install $need; else echo "Run: brew install$need"; exit 1; fi
  fi
else
  pkgs="python3 python3-venv ffmpeg lame build-essential git cmake ninja-build libgif-dev libjpeg-turbo8-dev libpng-dev zlib1g-dev"
  if [ -n "$missing" ] || ! command -v ninja >/dev/null 2>&1; then
    echo "Missing programs:$missing"
    if command -v apt-get >/dev/null 2>&1 && ask "Install now (sudo apt-get install $pkgs)?"; then
      sudo apt-get update && sudo apt-get install -y $pkgs
    else
      echo "Install: $pkgs (or the equivalent packages for your distribution)"; exit 1
    fi
  fi
fi

# Python 3.12 (the reference build's) in .venv: uv fetches it when present,
# else the system's python3.12/3.13
if [ ! -x .venv/bin/python ]; then
  if command -v uv >/dev/null 2>&1; then
    uv venv -q --python 3.12 .venv
  else
    py=""
    for c in python3.12 python3.13 python3; do
      if command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys; sys.exit(not (3, 12) <= sys.version_info[:2] <= (3, 13))'; then py=$c; break; fi
    done
    [ -n "$py" ] || { echo "Python 3.12 or 3.13 is missing (macOS: brew install python@3.12; or install uv: https://docs.astral.sh/uv/)."; exit 1; }
    "$py" -m venv .venv
  fi
fi
if command -v uv >/dev/null 2>&1; then
  pipi() { VIRTUAL_ENV="$here/.venv" uv pip install -q "$@"; }
else
  .venv/bin/python -m pip install -q --upgrade pip
  pipi() { .venv/bin/python -m pip install -q "$@"; }
fi
echo "Installing Python modules in .venv"
pipi -r requirements.txt
# TUM's racing-line helpers pin a quadprog that builds broken on Apple
# silicon; the newer one of requirements.txt does the same job
pipi --no-deps trajectory-planning-helpers==0.79

if [ ! -x .tools/jpegli/build/tools/cjpegli ]; then
  echo "Building cjpegli (the movie encoder; this takes a few minutes)"
  # that one commit and its submodules, without their history
  if [ ! -d .tools/jpegli/.git ]; then
    git init -q .tools/jpegli
    git -C .tools/jpegli remote add origin https://github.com/google/jpegli.git
  fi
  git -C .tools/jpegli fetch -q --depth 1 origin "$JPEGLI_COMMIT"
  git -C .tools/jpegli checkout -q FETCH_HEAD
  git -C .tools/jpegli submodule update -q --init --recursive --depth 1
  if [ "$os" = Darwin ]; then
    export CMAKE_PREFIX_PATH="$(brew --prefix giflib):$(brew --prefix jpeg-turbo):$(brew --prefix libpng):$(brew --prefix zlib)"
  fi
  cmake -S .tools/jpegli -B .tools/jpegli/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DJPEGXL_ENABLE_BENCHMARK=OFF > .tools/jpegli-cmake.log
  ninja -C .tools/jpegli/build -j "${JPEGLI_JOBS:-4}" cjpegli > .tools/jpegli-build.log
fi
.venv/bin/python build_game.py --help > /dev/null
echo "Ready. Next: .venv/bin/python build_game.py PATH/TO/YOUR/ISO"
