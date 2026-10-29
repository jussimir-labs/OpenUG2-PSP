> **RC21:** este documento descreve uma candidata anterior. Use as [instruções atuais em inglês](../README.md), as [notas da RC21](../RELEASE_NOTES.md) e as [limitações atuais](../KNOWN_ISSUES.md).

> Revisão noturna: veja [NOTAS-NOTURNAS.md](NOTAS-NOTURNAS.md). A candidata atual ainda não foi instalada nem testada no PSP; provas físicas históricas não se transferem automaticamente.

## Gerador nativo: Windows, Mac e Linux

O gerador atual vem em pacotes nativos, com Python e todos os conversores
dentro: não é preciso instalar nada. Baixe o ZIP do seu computador na
[página da versão](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/releases),
extraia a pasta inteira e abra o lançador. As instruções completas estão no
[README em inglês](../README.md).

| Computador | Pacote | Abrir |
| --- | --- | --- |
| Windows x64 | `NFSU2-Generator-windows-x64.zip` | `Generate-game.bat` ou `ptbr/Gerar-jogo.bat` |
| Mac Apple Silicon | `NFSU2-Generator-macos-arm64.zip` | `Generate-game.command` ou `ptbr/Gerar-jogo.command` |
| Mac Intel | `NFSU2-Generator-macos-x64.zip` | `Generate-game.command` ou `ptbr/Gerar-jogo.command` |
| Linux x86-64 (SteamOS, Steam Deck, Steam Machine) | `NFSU2-Generator-linux-x64.zip` | `Generate-game.sh` ou `ptbr/Gerar-jogo.sh` |

O Linux precisa de glibc 2.35 ou mais nova: SteamOS 3, Ubuntu 22.04+,
Debian 12+, Fedora 36+, Arch e parecidas. Use o pacote Linux, não o do
Windows pelo Proton ou Wine (não foi testado).

### No SteamOS (Steam Deck ou Steam Machine)

O gerador roda no **Modo Desktop**: botão Steam → **Ligar/Desligar → Mudar
para o Desktop**. Ele não roda pelo Modo Jogo.

1. Baixe `NFSU2-Generator-linux-x64.zip` com um navegador, na pasta
   **Downloads**.
2. No gerenciador de arquivos **Dolphin**, clique com o botão direito no ZIP
   → **Extrair → Extrair aqui** (*Extract → Extract here*, com o sistema em inglês).
3. Abra a pasta `NFSU2-Generator-linux-x64` e dê dois cliques em
   `Generate-game.sh` (ou em `ptbr/Gerar-jogo.sh`). Se o Dolphin perguntar o
   que fazer, escolha **Executar**, não *Abrir*. Uma janela de terminal abre com o auxiliar:
   mantenha-a aberta. A página do gerador abre no navegador padrão; no
   seletor **Idioma**, escolha **Português**.
4. Se nenhum navegador abrir, copie a linha `Local interface:
   http://127.0.0.1:…` do terminal para um navegador. O endereço só funciona
   neste computador.

**Permissão de execução:** extraído pelo Ark (o extrator do Dolphin), pelo
File Roller do GNOME ou pelo `unzip`, o pacote mantém a permissão. Se o
clique abrir o `Generate-game.sh` como texto, clique com o botão direito →
**Propriedades → Permissões**, marque **É executável** (no GNOME:
*Executável como programa*) e abra de novo. Ou rode pelo terminal (Konsole):

```sh
cd ~/Downloads/NFSU2-Generator-linux-x64
sh Generate-game.sh
```

O lançador repõe sozinho a permissão das ferramentas incluídas. Escolha uma
pasta de saída num disco com 12 GB livres, como `~/NFSU2-PSP`. Para copiar
ao PSP, ligue o aparelho por USB (**Configurações → Conexão USB**) e junte a
pasta `PSP` gerada com a pasta `PSP` do cartão pelo Dolphin.

## Como reportar bugs

Veja o [manual visual em PDF](manual-reportar-bugs.pdf) ([English](../docs/bug-reporting-guide-EN.pdf)), consulte o [roadmap](ROADMAP.md) e abra um [relato de bug](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues).

# NFSU2 PSP: beta fechado

*(English: [README.md](../README.md))*

Um port nativo de *Need for Speed Underground 2* para o PSP, feito por fã.
Este repositório **não traz nenhum dado do jogo**: você gera o jogo a
partir da **sua própria cópia**, com a ferramenta daqui. O executável do
PSP (`eboot/EBOOT.PBP`) já vem compilado.

Por favor, não compartilhe este repositório nem a pasta gerada: o acesso é
pessoal.

## Rascunho RC18 em poucas palavras

Este rascunho local traz o executável **da37c112**, fonte `68566e7bc`. Uma tela de sponsor que falha ao carregar não assina nem salva mais o primeiro contrato automaticamente. A troca de bancos de áudio preserva os dados antigos até o mixer poder trocar com segurança; três leituras inválidas de memória reproduzidas foram corrigidas. Isso não comprova a causa do sintoma sonoro relatado pelo testador.

As conferências headless atuais limitadas incluem uma **vitória normal de três voltas na Street X4602**, SAVE nativo e processo novo restaurando **$500, REP400, duas corridas e os melhores eventos preservados**, além de SAVE/reabertura privados PS2. A carreira realmente conquistada permanece no **estágio1**, com melhores eventos5 e6. Um teste separado de interface/SAVE/reabertura de sponsor passou com **fixture sintética de cinco vitórias**. Conferências normais Focus/Rachel de bancos, frenagem, pausa/retomada e menu também passaram em um processo, sem capturar PCM ou aprovar audição. Não há aprovação de progressão normal ao sponsor do estágio2; o rascunho local aguarda empacotamento final.

Em relação à saída exata correspondente RC17, **somente EBOOT.PBP muda**. Pack, SFX e bancos NOS nativos são iguais; IDs **9576 (Xbox)** e **8729 (PS2)**. Gere a pasta correspondente, preserve os saves e use outra saída se a receita mudar. Identidade dos arquivos é separada de aprovação sonora/jogável. O gerador não instala automaticamente.

Configuração4u mantida. Audição humana no PSP, outros perfis de câmera, curva Master original, demais modelos e30fps globais continuam sem aprovação. Veja [notas](NOTAS-NOTURNAS.md), [limitações](LIMITACOES.md) e o [recibo de qualificação](../docs/qualification/RC18-QUALIFICATION.json).

## Do que você precisa

- **A ISO do Xbox** de *Need for Speed Underground 2* (versão americana,
  NTSC-U), tirada do seu disco. A ferramenta confere o hash da ISO, ou,
  se a imagem for de outro formato (um dump completo, uma xiso refeita),
  confere os 185 arquivos do jogo um a um.
  Também há uma fonte **PS2 americana NTSC-U (SLUS_210.65)**. ISOs europeias e outras revisões são recusadas. A fonte PS2 usa somente os arquivos desse disco; diferenças conhecidas estão em [tools/PS2_SOURCE.md](../tools/PS2_SOURCE.md). As 857 falas MicroTalk foram convertidas offline e conferidas estruturalmente; a conferência das falas durante o jogo ainda é parcial. Veja também [LIMITACOES.md](LIMITACOES.md).
- **Um PSP com firmware customizado** (ARK-4/ARK-5 ou similar) que rode
  homebrew do Memory Stick. O desenvolvimento usa o **PSP-3000**. Esta
  candidata passou em conferências locais headless limitadas e não foi instalada
  nem testada no aparelho. As provas humanas e a instalação
  anteriores pertencem à build bf7f. PSP-2000, Go, PSP-1000 e comportamento
  do save físico continuam sem aprovação. Veja [NOTAS-NOTURNAS.md](NOTAS-NOTURNAS.md)
  e [LIMITACOES.md](LIMITACOES.md).
- **Um Memory Stick** (ou microSD no adaptador) com **2,2 GB livres**.
- **Um Mac** (Apple Silicon ou Intel) ou um PC com **Linux**, ou com
  **Windows e o WSL**. Precisa de **12 GB livres** no disco e de cerca de
  **1h30 para Xbox ou 3h30 para PS2** na primeira vez neste Mac de
  referência; o tempo depende do computador e da fila de processamento.

## 1. Gerar o jogo

> Esta seção descreve o kit de código-fonte antigo. Com os pacotes nativos,
> siga [Gerador nativo: Windows, Mac e Linux](#gerador-nativo-windows-mac-e-linux)
> acima.

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

No kit de código-fonte da RC21, no Terminal, dentro da raiz do repositório,
rode `sh ./generate-game.sh`. (No pacote nativo, o lançador é o
`Generate-game.sh` descrito acima.)

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
   referência ausente produz diagnóstico e preserva os arquivos. No
   Windows e em Macs Intel, alguns filmes, áudios, geometrias e linhas de
   corrida aparecem como **equivalentes**: mesma estrutura, com bytes
   arredondados de outro jeito pelo processador x86. Isso é sucesso. Isso
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

### Atualizar para uma versão nova do gerador

Quando sair uma versão nova do gerador, não é preciso gerar tudo de novo.
Extraia o download novo, abra-o e escolha **a mesma ISO** e **a pasta de
saída da geração anterior** (a que contém `PSP`). O gerador atualiza essa
pasta:

- converte outra vez só as etapas cujo conversor, opções ou entradas
  mudaram desde a versão que gerou a pasta, e só extrai a ISO de novo se uma
  delas lê o disco; o resto é reaproveitado da própria pasta. Uma versão que
  só troca o `EBOOT.PBP` do jogo atualiza em segundos; uma que muda
  conversores leva o tempo dessas etapas. A primeira atualização a partir da
  RC21 é a exceção: essa versão mudou os conversores mais demorados, e ela
  leva quase o tempo de uma geração completa;
- instala o `EBOOT.PBP` novo e confere o resultado inteiro contra a nova
  referência, como numa geração completa;
- a pasta `PSP` anterior fica guardada (em `.anterior`, dentro da saída) até
  a nova passar na conferência, e volta para o lugar se ela não passar.

A atualização vira uma geração completa, e diz o motivo no registro, quando
a saída anterior não pode ser aproveitada: outra ISO ou fonte, pasta `PSP`
alterada ou incompleta, versão do gerador que ele não conhece ou outra
opção de filmes. Nada derivado do jogo é baixado: todo arquivo convertido
continua saindo da sua ISO, no seu computador. Depois de atualizar, copie de
novo a pasta `PSP/GAME/NFSU2VIEW` inteira para o cartão.

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

A prova histórica da RC13 e7ce inclui um Drift 4312 completo de três voltas: 1.880 pontos, sete drifts e quarto lugar (derrota real), com pausa/retomada e retorno ao menu/música. Veja as notas noturnas para o escopo.
