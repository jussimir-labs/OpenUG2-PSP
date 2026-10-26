> Revisão noturna: veja [NOTAS-NOTURNAS.md](NOTAS-NOTURNAS.md). A candidata atual ainda não foi instalada nem testada no PSP; provas físicas históricas não se transferem automaticamente.

## Como reportar bugs

Veja o [manual visual em PDF](manual-reportar-bugs.pdf) ([English](../docs/bug-reporting-guide-EN.pdf)), consulte o [roadmap](ROADMAP.md) e abra um [relato de bug](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues).

# NFSU2 PSP: beta fechado

*(English: [README.md](../README.md))*

Um port nativo de *Need for Speed Underground 2* para o PSP, feito por fã.
Este repositório **não traz nenhum dado do jogo**: você gera o jogo a
partir da **sua própria cópia**, com a ferramenta daqui. O executável do
PSP (`eboot/EBOOT.PBP`) já vem compilado.

Por favor, não compartilhe este repositório nem a pasta gerada: o acesso é
pessoal.

## Candidata 13 em poucas palavras

Este kit traz o executável **e7ce75f8**. O Drift humano usa regras derivadas do original nas configurações suportadas; dyno personalizado e presets especiais mantêm o modelo anterior durante o evento inteiro. As provas locais incluem três trechos curtos de Drift, SAVE/processo novo privados das duas fontes e uma vitória natural completa na Xbox 4101, com o prêmio salvo e restaurado em um processo frio.

São conferências locais com escopo definido. Não há aprovação deste executável no PSP/Memory Stick, de carreira completa ou de 30 fps globais. Os relatos de áudio/EA TRAX continuam abertos para confirmação dos testadores. Leia [NOTAS-NOTURNAS.md](NOTAS-NOTURNAS.md) e [LIMITACOES.md](LIMITACOES.md). A interface abre em inglês; selecione Português. Arquivos em português ficam em `ptbr/`.

## Do que você precisa

- **A ISO do Xbox** de *Need for Speed Underground 2* (versão americana,
  NTSC-U), tirada do seu disco. A ferramenta confere o hash da ISO, ou,
  se a imagem for de outro formato (um dump completo, uma xiso refeita),
  confere os 185 arquivos do jogo um a um.
  Também há uma fonte **PS2 americana NTSC-U (SLUS_210.65)**. ISOs europeias e outras revisões são recusadas. A fonte PS2 usa somente os arquivos desse disco; diferenças conhecidas estão em [tools/PS2_SOURCE.md](../tools/PS2_SOURCE.md). As 857 falas MicroTalk foram convertidas offline e conferidas estruturalmente; a conferência das falas durante o jogo ainda é parcial. Veja também [LIMITACOES.md](LIMITACOES.md).
- **Um PSP com firmware customizado** (ARK-4/ARK-5 ou similar) que rode
  homebrew do Memory Stick. O desenvolvimento usa o **PSP-3000**. Esta
  candidata noturna passou em testes dirigidos no PPSSPP, mas ainda não
  foi instalada nem testada no aparelho. As provas humanas e a instalação
  anteriores pertencem à build bf7f. PSP-2000, Go, PSP-1000 e comportamento
  do save físico continuam sem aprovação. Veja [NOTAS-NOTURNAS.md](NOTAS-NOTURNAS.md)
  e [LIMITACOES.md](LIMITACOES.md).
- **Um Memory Stick** (ou microSD no adaptador) com **2,2 GB livres**.
- **Um Mac** (Apple Silicon ou Intel) ou um PC com **Linux**, ou com
  **Windows e o WSL**. Precisa de **12 GB livres** no disco e de cerca de
  **1h30 para Xbox ou 3h30 para PS2** na primeira vez neste Mac de
  referência; o tempo depende do computador e da fila de processamento.

## 1. Gerar o jogo

Baixe e extraia o repositório completo (botão *Code > Download ZIP*, ou
`git clone`). O lançador do seu sistema inicia um auxiliar local e abre o
gerador no navegador. No seletor **Idioma**, escolha **Português**.

### macOS

Dê dois cliques em [Gerar-jogo.command](Gerar-jogo.command), nesta pasta
`ptbr/`, ou em [Generate-game.command](../Generate-game.command), na raiz.
Ambos abrem a mesma interface. Se o macOS bloquear, clique com o botão
direito, escolha *Abrir* e confirme.

É preciso Python 3.9 ou mais novo para abrir a interface. A conversão exige
Python 3.12 ou mais novo e suas dependências; a preparação usa Python
3.12/3.13 ou `uv` para criar a `.venv/`. Instale o
[Homebrew](https://brew.sh) e as ferramentas de linha de comando da Apple
(`xcode-select --install`) antes de preparar os conversores. O lançador não
traz esses requisitos do sistema embutidos.

### Linux

No Terminal, dentro da raiz do repositório, rode:

```sh
sh ./generate-game.sh
```

Python 3.9+ abre a interface; a conversão precisa de Python 3.12+ e das
dependências dos conversores. No Debian/Ubuntu, a preparação usa `apt-get`
e pode exigir acesso de administrador. Em outras distribuições, instale os
pacotes equivalentes indicados pela preparação. `zenity` ou `kdialog` são
opcionais e habilitam o seletor de arquivos; sem eles, cole os caminhos
completos nos campos.

### Windows

Instale e inicialize o WSL com uma distribuição Linux primeiro. Depois, dê
dois cliques em [Gerar-jogo.bat](Gerar-jogo.bat), nesta pasta `ptbr/`, ou em
[Generate-game.bat](../Generate-game.bat), na raiz. Eles abrem a mesma
interface pelo WSL, onde também se aplicam os requisitos de Python e
pacotes Linux acima. Python, WSL e pacotes do sistema não vêm embutidos;
essa opção de abertura não representa aprovação em todas as configurações
de Windows.

### Gerar pelo navegador

1. Se faltarem requisitos, clique em **Preparar este computador**. O auxiliar
   local baixa e instala as dependências dos conversores e compila o
   codificador dos filmes. Os módulos Python ficam em `.venv/` e as
   ferramentas locais em `.tools/`. Pacotes do sistema podem exigir
   instalação separada ou acesso de administrador; siga os requisitos
   mostrados se a preparação parar.
2. Escolha sua ISO e a pasta de saída, ou cole os caminhos completos. O
   seletor de pastas cria `NFSU2-PSP` dentro da pasta escolhida; um caminho
   digitado é usado como foi informado. Use uma pasta nova com pelo menos
   12 GB livres. Deixe a fonte em **Automática**, a menos que precise
   selecionar Xbox ou PS2 explicitamente.
3. Clique em **Gerar jogo para PSP**. O auxiliar identifica a fonte, confere
   a ISO e os arquivos originais, converte os dados e monta
   `<saída>/PSP/GAME/NFSU2VIEW`. A interface inclui os **28 filmes** e não
   oferece opção de omiti-los. Sua ISO permanece no computador; não há
   upload dela.
4. Acompanhe as etapas e os registros. O resultado só é confirmado quando
   coincide com a referência da mesma fonte e executável. Divergência ou
   referência ausente produz diagnóstico e preserva os arquivos. Isso
   verifica a identidade dos arquivos; as
   [limitações do beta](LIMITACOES.md) continuam válidas.
5. Após o sucesso, use **Abrir pasta gerada**. O auxiliar não apaga pastas
   existentes do usuário nem saves, e não copia nada para o PSP. Faça a
   instalação separadamente, conforme a seção abaixo. Os exemplos dela
   usam a saída padrão `output`; substitua pela pasta escolhida se for outra.

Use **Baixar registros** ao relatar um problema e preserve os registros de
cada etapa em `<saída>/registros/`. Para interromper, clique em **Cancelar
processo** e espere os conversores encerrarem. Etapas parciais compatíveis
só podem ser retomadas com a mesma ISO, fonte, executável, conversores e
opções. Se a receita mudar, escolha outra pasta; a interface não apaga uma
saída antiga para recomeçar.

Fechar a aba do navegador não encerra o auxiliar local nem um trabalho em
andamento. Quando não houver trabalho rodando, clique em **Encerrar
auxiliar**; abra o lançador de novo quando precisar.

As opções avançadas por linha de comando continuam disponíveis na raiz:
`.venv/bin/python build_game.py --help`.

## 2. Instalar no PSP

1. Ligue o PSP ao computador pelo cabo USB e, no XMB, abra
   *Ajustes > Conexão USB*.
2. Faça a cópia manualmente. Confira o volume de destino do cartão e faça
   backup de `PSP/SAVEDATA` antes. O comando macOS abaixo atualiza os arquivos
   alterados; não acrescente `--fresh` automaticamente.
   - **macOS:** não copie pelo Finder (ele deixa um arquivo `._` ao lado de
     cada um, e o jogo fica mais lento no cartão). No Terminal, dentro desta
     pasta, rode:
     ```sh
     .venv/bin/python tools/ms_package.py push /Volumes/<nome do cartão> output
     ```
   - **Windows e Linux:** copie a pasta `output/PSP` para a raiz do cartão,
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
| `a etapa "…" falhou` | rode de novo; se repetir, mande a pasta `output/registros` |

## Créditos e licenças

Veja [TERCEIROS.md](TERCEIROS.md) e a pasta [`licenses/`](../licenses/).

A prova atual também inclui um Drift4312 completo de três voltas: 1.880 pontos, sete drifts e quarto lugar (derrota real), com pausa/retomada e retorno ao menu/música. Veja as notas noturnas para o escopo.
