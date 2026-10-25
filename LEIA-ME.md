# NFSU2 PSP: beta fechado

*(English: [README.md](README.md))*

Um port nativo de *Need for Speed Underground 2* para o PSP, feito por fã.
Este repositório **não traz nenhum dado do jogo**: você gera o jogo a
partir da **sua própria cópia**, com a ferramenta daqui. O executável do
PSP (`eboot/EBOOT.PBP`) já vem compilado.

Por favor, não compartilhe este repositório nem a pasta gerada: o acesso é
pessoal.

## Do que você precisa

- **A ISO do Xbox** de *Need for Speed Underground 2* (versão americana,
  NTSC-U), tirada do seu disco. A ferramenta confere o hash da ISO, ou,
  se a imagem for de outro formato (um dump completo, uma xiso refeita),
  confere os 185 arquivos do jogo um a um.
  A conversão a partir do disco do **PS2** está em preparação.
- **Um PSP com firmware customizado** (ARK-4/ARK-5 ou similar) que rode
  homebrew do Memory Stick. O beta é medido no **PSP-3000**. No PSP-2000
  e no Go o jogo usa o mesmo perfil de memória; no PSP-1000 ele roda num
  perfil compacto, ainda pouco testado.
- **Um Memory Stick** (ou microSD no adaptador) com **2,2 GB livres**.
- **Um Mac** (Apple Silicon ou Intel) ou um PC com **Linux**, ou com
  **Windows e o WSL**. Precisa de **12 GB livres** no disco e de cerca de
  **1h30** na primeira vez (o tempo exato depende do computador).

## 1. Gerar o jogo

### macOS

1. Baixe o repositório (botão *Code > Download ZIP*, ou `git clone`).
2. Dê dois cliques em **`Gerar-jogo.command`**. Se o macOS disser que não
   pode abrir, clique com o botão direito nele, escolha *Abrir* e confirme.
3. Na primeira vez, ele prepara o computador (`setup.sh`): pede o
   [Homebrew](https://brew.sh) e as ferramentas da Apple
   (`xcode-select --install`) se faltarem, pergunta antes de instalar os
   programas (ffmpeg, lame, cmake…) e compila o codificador dos filmes.
   Tudo o que ele instala de Python fica em `.venv/`, dentro desta pasta.
4. Arraste a ISO para a janela e tecle Enter.

### Linux

```sh
sh ./gerar-jogo.sh /caminho/da/sua.iso
```

Na primeira vez ele roda o `setup.sh`, que usa o `apt` (Debian, Ubuntu); em outra distribuição, ele
mostra a lista de pacotes para você instalar.

### Windows

O Windows usa o WSL (o Linux que vem com ele). Na primeira vez, abra um
PowerShell como administrador, rode `wsl --install` e reinicie. Depois,
arraste a ISO sobre **`Gerar-jogo.bat`** (ou dê dois cliques e arraste a ISO
para a janela).

### O que acontece

A ferramenta mostra cada etapa com a porcentagem. Ela:

1. confere a ISO;
2. extrai os arquivos do disco;
3. converte a cidade, as pistas, os carros, as telas, o áudio e os filmes
   (cerca de 80 etapas);
4. monta a pasta do cartão em **`saida/PSP/GAME/NFSU2VIEW`**;
5. confere o resultado contra a build de referência (hash de cada arquivo).

Ela só escreve dentro de `saida/`. Se parar no meio (falta de espaço,
computador desligado), rode de novo: o que já foi feito não se repete.
`--do-zero` recomeça do começo. Cada etapa tem seu registro em
`saida/registros/`; se der erro, mande esses registros com o relato.

Outras opções: `.venv/bin/python build_game.py --help`.

## 2. Instalar no PSP

1. Ligue o PSP ao computador pelo cabo USB e, no XMB, abra
   *Ajustes > Conexão USB*.
2. Copie a pasta:
   - **macOS:** não copie pelo Finder (ele deixa um arquivo `._` ao lado de
     cada um, e o jogo fica mais lento no cartão). No Terminal, dentro desta
     pasta, rode:
     ```sh
     .venv/bin/python tools/ms_package.py push /Volumes/<nome do cartão> saida --fresh
     ```
   - **Windows e Linux:** copie a pasta `saida/PSP` para a raiz do cartão,
     juntando com a pasta `PSP` que já existe lá.
3. Ejete o cartão e saia da Conexão USB (botão O).
4. No XMB, em *Jogo > Memory Stick*, abra **NFSU2 PSP**.

Os saves ficam em *Dados salvos* do XMB e sobrevivem a uma nova cópia.

## Problemas comuns

| Mensagem | O que fazer |
|---|---|
| `ISO não encontrada` | confira o caminho; arraste o arquivo de novo |
| `não parece uma ISO de NFSU2` | é uma ISO de outro jogo ou console, ou está incompleta |
| `arquivos extraídos diferem do jogo esperado` | outra região ou versão; por enquanto, só a NTSC-U |
| `falta preparar o computador` | rode `sh ./setup.sh` e siga o que ele pedir |
| `pouco espaço em disco` | libere espaço ou use `--saida` num disco com mais espaço |
| `a etapa "…" falhou` | rode de novo; se repetir, mande a pasta `saida/registros` |

## Créditos e licenças

Veja [THIRD_PARTY.md](THIRD_PARTY.md) e a pasta `licenses/`.
