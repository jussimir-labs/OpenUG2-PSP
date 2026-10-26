# Terceiros e direitos

Este repositório não contém nenhum dado do jogo. Tudo o que o jogo
mostra e toca (cidade, carros, texturas, sons, músicas, filmes, telas) é
convertido pela ferramenta a partir da ISO do próprio jogador, no
computador dele. *Need for Speed* e *Need for Speed Underground 2* são
marcas da Electronic Arts. Este é um projeto de fã, sem ligação com a EA
nem com a Sony.

## O que está neste repositório

| Item | Origem | Licença | Redistribuir |
|---|---|---|---|
| `eboot/EBOOT.PBP` | o jogo do port, compilado | código do projeto, mais as bibliotecas da linha abaixo | sim, com os avisos de `licenses/` |
| dentro do EBOOT: física portada do [OpenUG2](https://github.com/whoismept/OpenUG2) (`physics_ug2.c`) | OpenUG2 contributors | MIT | sim, com o aviso (`licenses/OpenUG2.txt`) |
| dentro do EBOOT: ORCA portado do [RVO2](https://github.com/snape/RVO2) | Univ. of North Carolina | Apache 2.0 | sim, com a licença (`licenses/RVO2-Apache-2.0.txt`) |
| dentro do EBOOT: [PSPSDK](https://github.com/pspdev/pspsdk) (libpspgu, libpspaudio, libpspmp3, libpspjpeg…) | pspdev | BSD | sim, com o aviso (`licenses/PSPSDK.txt`) |
| dentro do EBOOT: newlib (libc, libm) | vários | BSD e similares | sim, com os avisos (`licenses/newlib.txt`) |
| dentro do EBOOT: zlib | Jean-loup Gailly, Mark Adler | zlib | sim (`licenses/zlib.txt`) |
| `tools/`, `build_game.py`, `sources/`, `presets/` | o projeto | do projeto | decisão do autor |
| `third_party/xdvdfs/xdvdfs.py` (leitor do disco do Xbox) | xboxrecomp, de sp00nz, no [nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw) | MIT | sim, com a licença ao lado |
| `tools/vendor/utk/` | Andrew D’Addesio, adaptação vgmstream fixada em `PROVENANCE.json` | domínio público/Unlicense e aviso permissivo do vgmstream preservados ao lado | sim, com ambos os avisos; apenas conversão offline |
| `art/reference/psp-controls/psp-isolated-v1.png` | imagem do projeto de um PSP-3000, para a tela de controles | do projeto | sim |
| `manifests/*.json`, `sources/xbox-files.json` | SHA-256 dos arquivos do disco e do pacote | só hashes | sim |
| `tools/race_reference.json` | trechos de corrida medidos jogando o original | dados de medida do projeto | sim |

Os formatos dos arquivos do jogo foram lidos com a ajuda das notas
públicas do OpenUG2, do [nfsu2-re](https://github.com/yugecin/nfsu2-re) e
do [FEngLib](https://github.com/NFSTools/FEngLib); nenhum código deles
está nos conversores.

## O que o jogador instala (não vem no repositório)

| Item | Para quê | Licença |
|---|---|---|
| Python 3.12, numpy, scipy, Pillow, matplotlib | os conversores | PSF, BSD, HPND |
| [trajectory_planning_helpers](https://github.com/TUMFTM/trajectory_planning_helpers) (TUM) | as linhas de corrida dos rivais | LGPL-3.0 |
| quadprog | dependência do anterior | GPL-2.0 ou posterior |
| ffmpeg, lame | áudio e filmes | LGPL/GPL |
| [jpegli](https://github.com/google/jpegli) (`cjpegli`, compilado pelo `setup.sh`) | os quadros dos filmes | BSD-3 |

Nada disso é copiado para o repositório nem para o pacote do PSP: os
programas rodam no computador do jogador e só os arquivos convertidos vão
para o cartão.
