#!/bin/sh
# Launch the local browser UI. Conversion tools are prepared from the page.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"
py=""
for candidate in "$here/.venv/bin/python" python3.12 python3.13 python3; do
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; sys.exit(sys.version_info < (3, 9))'; then
    py=$candidate
    break
  fi
done
if [ -z "$py" ]; then
  echo "Python 3.9+ is needed to open the local interface (3.12+ for conversion)."
  echo "Install Python from https://www.python.org/downloads/ and open this file again."
  printf 'Press Enter to close. '; read -r _
  exit 1
fi
# Keep the helper alive if the launcher window closes. The page has a Close button.
mkdir -p .tools
nohup "$py" -u "$here/generator.py" > .tools/browser-launch.log 2>&1 < /dev/null &
