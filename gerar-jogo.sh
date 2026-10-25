#!/bin/sh
# Linux (e o WSL do Windows, pelo Gerar-jogo.bat): prepara o computador na
# primeira vez (setup.sh) e gera o jogo em saida/ a partir da ISO.
#   sh ./gerar-jogo.sh /caminho/da/sua.iso [opções do build_game.py]
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
if [ ! -x .venv/bin/python ] || [ ! -x .tools/jpegli/build/tools/cjpegli ]; then
  sh ./setup.sh || { echo; echo "A preparação não terminou. Veja a mensagem acima."; exit 1; }
fi
exec nice -n 10 .venv/bin/python build_game.py "$@"
