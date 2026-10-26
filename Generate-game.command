#!/bin/sh
# Open the browser generator on macOS.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
exec sh ./generate-game.sh
