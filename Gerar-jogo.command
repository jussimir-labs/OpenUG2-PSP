#!/bin/sh
# macOS: dois cliques neste arquivo. Prepara o computador (setup.sh) na
# primeira vez, pede a ISO (arraste-a para a janela) e gera o jogo em
# saida/PSP/GAME/NFSU2VIEW, ao lado deste arquivo.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
if [ ! -x .venv/bin/python ] || [ ! -x .tools/jpegli/build/tools/cjpegli ]; then
  sh ./setup.sh || { echo; echo "A preparação não terminou. Veja a mensagem acima."; printf 'Enter para fechar. '; read -r _; exit 1; }
fi
nice -n 10 .venv/bin/python build_game.py "$@"
status=$?
if [ $status -eq 0 ]; then open "$here/saida" 2>/dev/null; fi
printf '\nEnter para fechar. '
read -r _
exit $status
