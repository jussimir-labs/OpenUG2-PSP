#!/bin/sh
# Abre a mesma interface local, com português disponível no seletor de idioma.
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec sh "$here/generate-game.sh"
