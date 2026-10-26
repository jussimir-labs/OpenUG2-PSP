# Candidata 13: 26 de outubro de 2026

Kit preparado localmente para o beta fechado. Ainda não foi publicado, instalado no PSP nem aprovado no aparelho físico.

- SHA256 do executável: `e7ce75f8b2e35949fdd04525baf9d6a114c177d822cfbe2e4189f20f7e68fa63` (`e7ce75f8`).
- Fonte da build: `5a3e4729d57e728f620da7adca1b4f1b97ae6b69`.
- Configuração: PGO=use da 4u, LTO=1, RELEASE=0, PSP_LARGE_MEMORY=1. Mantidos os mesmos 63 perfis e `hot.ld`, sem novo treinamento.
- Pacotes gerados: Xbox com 162 arquivos e ID `78eb`; PS2 com 159 arquivos e ID `4111`. A biblioteca da física original e dois arquivos de pista passam a ser gerados a partir dos dados do seu jogo. A qualificação local reaproveitou os demais dados convertidos, sem reconversão completa.

## O que mudou

O Drift controlado pelo jogador usa agora um modelo derivado das regras originais de rodas, suspensão, motor, embreagem, transmissão, direção e corpo. A configuração vem dos registros dos 29 carros americanos suportados, das peças de desempenho e do nitro. Esse é o domínio da configuração; não significa que todos os carros e todas as combinações de peças foram dirigidos e aprovados.

Sliders personalizados do dyno e carros de presets especiais continuam com o modelo de dirigibilidade anterior durante o evento inteiro. A escolha acontece antes da largada; uma falha no cálculo não troca o modelo no meio da corrida. Os contatos horizontais continuam sendo uma adaptação 2D do port. Comportamentos extremos/wild e hidráulica não estão plenamente qualificados. Não é aprovação de paridade completa com o original.

Foram mantidas as correções do logo e da borda circular EA TRAX, dos bancos separados de turbo/pneus, dos loops longos, da recuperação da música, dos sinais do som de frenagem e da pausa/limitador. Os relatos #1–#5 continuam abertos para confirmação no novo executável, principalmente pela audição no aparelho do testador.

## Conferências deste executável

Três trechos curtos no PPSSPP usaram menus e controles normais: Stadium Drift 2 (4312) com câmbio automático e manual, e Drift de cidade 4174. Mostraram aceleração, estado físico finito, pausa/retomada e retorno ao menu com música. O trecho manual completou a troca da primeira para a segunda marcha (índices nativos 2→3). Não são corridas completas de Drift nem comparação de todas as trajetórias originais.

Os registros brutos mantêm a falha de um assert de limpeza que exigia um contador privado de passos positivo. Uma segunda limpeza zerou esse contador; a posse do jogador estava desativada no menu. Os trechos manual e de cidade também registraram ponteiros liberados. A reconciliação foi feita offline, sem repetir a execução ou modificar o jogo para esconder a falha. Uma tentativa anterior de abertura usou o prefixo de arquivos errado para o pacote; as execuções qualificadas usaram `ms0:/PSP/GAME/NFSU2VIEW`.

SAVE nativo e a primeira leitura em um processo novo passaram com cópias privadas de fixtures já existentes das duas fontes: Xbox $250/REP 200/uma corrida e PS2 $250/REP 300/uma corrida. Os originais foram preservados.

Outra execução da carreira Xbox completou naturalmente a sprint 4101 em 139,1676 segundos de simulação, em primeiro lugar, ordem [0,1,3,2], sem resets registrados do jogador ou da IA. Dinheiro passou de $0 para $250, REP de 0 para 300 e corridas concluídas de 0 para 1. Após o SAVE nativo, um único processo novo restaurou exatamente esse progresso recém-conquistado. Não houve vitória forçada, corrida encurtada ou injeção de estado do jogo. Isso confere essa sequência de corrida/prêmio/save, não uma carreira nova completa nem todos os modos.

Os 28 filmes estão estruturalmente presentes nas duas fontes, mantendo a opção de pular. Não é conferência de reprodução integral ou de áudio físico. Um diagnóstico calibrado de 1200 quadros no Drift teve 99,42% dos quadros em até dois vblanks, com sete quadros mais lentos/engasgos; é uma cena, sem aprovação de 30 fps globais ou de hardware.

## O que ainda falta

Não houve execução e7ce no PSP nem no Memory Stick. As observações históricas do PSP-3000/cartão pertencem ao executável anterior. PSP-1000/2000/Go, saves físicos, visual completo, todos os carros/pistas, carreira completa e desempenho global continuam sem aprovação. A imagem original do disco PS2 USA segue sem verificação; a prova retida dos 179 arquivos usou uma fonte reconstruída. As recuperações repetidas de rivais na drag 4401 permanecem adiadas. Veja [LIMITACOES.md](LIMITACOES.md).

O [recibo de qualificação](../docs/qualification/OVERNIGHT-QUALIFICATION.json) separa as provas atuais das históricas e dos limites. [English](../NIGHTLY_NOTES.md).

Uma única prova final na mesma build completou as três voltas de Stadium Drift 2 (4312), automático e controles normais, em 123,4318 segundos de simulação, com 1.880 pontos bancados em sete drifts. O jogador terminou em quarto: a derrota real foi preservada. Não houve reset do jogador/IA, NaN ou erro do Core. Pausa/retomada, liberação dos dados do evento e retorno ao menu com música avançando passaram. Os arquivos nativos ficaram iguais. Isso qualifica esse evento completo e a pontuação, sem aprovar vitória no Drift, dirigibilidade de todos os carros ou áudio físico.
