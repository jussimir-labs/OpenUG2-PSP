# Beta fechado: escopo e limitações

A candidata 13 (`e7ce75f8`) tem provas locais no PPSSPP e testes nativos, descritos nas [notas](NOTAS-NOTURNAS.md). A referência dos arquivos confere identidade; não aprova toda a jogabilidade, todos os aparelhos nem o desempenho.

## Relatos ainda abertos

Confira novamente no novo executável, informando aparelho, fonte, carro, pista e passos. Uma correção de código não encerra automaticamente a observação de quem reportou.

- [Logo/borda EA TRAX (#1)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1): ordem de desenho e proporção circular corrigidas e conferidas localmente; confirme durante o jogo.
- [Cortes/cliques no Drift (#2)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2): bancos separados e cursores de loops completos corrigem defeitos confirmados. O sintoma ouvido ainda precisa da confirmação do testador; a causa física não foi totalmente isolada.
- [Loop na pausa (#3)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3): testes locais silenciaram pedidos do motor; limpeza do ambiente e rampas pendentes do limitador foram corrigidas. Isso não identifica todos os sons do relato original.
- [Som de frenagem (#4)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4): aceleração e sinais no referencial do pneu foram corrigidos. O Drift humano fornece agora estado nativo de rodas/corpo; outros caminhos mantêm estimativas do port. Audição e paridade de todos os ramos continuam pendentes.
- [Música após Drift (#5)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5): a recuperação no menu passou nos trechos locais atuais. Confirme no seu aparelho.

## Dirigibilidade e progresso

- **Drift humano:** Core derivado do original integrado para os 29 registros americanos suportados, peças de desempenho e nitro. A prova dirigida usou trechos curtos com 206 original: 4312 automático/manual e 4174. Não aprova todos os carros, peças ou paridade de pista inteira. Comparações históricas de yaw de candidatas antigas não medem este executável.
- **Dyno personalizado e presets especiais:** usam o modelo anterior durante o evento inteiro, escolhido antes da largada. Os cinco sliders antigos não são convertidos por adivinhação para o formato original. Uma falha do Core não troca a física no meio da corrida.
- **Contatos e casos especiais:** colisões horizontais permanecem uma adaptação 2D. Comportamentos extremos/wild, hidráulica e demais ramos não qualificados continuam limitados.
- **Carreira/SAVE:** os testes privados atuais cobrem fixtures já existentes Xbox/PS2 e uma vitória natural na corrida Xbox 4101, prêmio $250/REP 300, SAVE nativo e um processo frio restaurando esse novo progresso. Isso não aprova carreira completa, todas as corridas ou save físico. O gerador não substitui automaticamente seus saves.
- **4401:** rivais podem ficar presos e se recuperar repetidamente em Tunnel Construction/drag. Trabalho mantido adiado.

## Aparelhos, fonte e apresentação

- **Hardware:** não houve instalação nem execução e7ce no PSP ou Memory Stick. As observações bf7f no PSP-3000/cartão são históricas. PSP-1000/2000/Go e saves físicos seguem sem qualificação.
- **Desempenho:** uma cena calibrada de Drift tem registro separado; não aprova 30 fps globais ou memória/desempenho de todos os aparelhos.
- **Visual:** tom, materiais, UVs, visibilidade/culling e portas precisam de revisão. Rivais do Free Roam já recebem neon pela tabela original de probabilidades por nível; alguns corretamente ficam sem neon. PRNG, escolha de packs e renderização mantêm adaptações.
- **Filmes/áudio:** os 28 filmes estão estruturalmente presentes por fonte. Essa conferência não aprova reprodução integral, todas as famílias de áudio ou audição no aparelho. Pular filmes continua disponível.
- **Imagem PS2 USA:** o suporte SLUS_210.65 e a conferência retida dos 179 arquivos não verificam a imagem original do disco, que segue desconhecida. PAL/outras revisões são recusadas. Veja [diferenças da fonte](../tools/PS2_SOURCE.md).
- **PSPLink:** parar a bf7f exigiu recuperação humana. Não foi provada uma correção geral do encerramento PSPLink/Bus error antigo.

Em falhas do gerador, anexe `relatorio.json` e o log da etapa. O [manual ilustrado](manual-reportar-bugs.pdf) foi preservado. [English](../KNOWN_ISSUES.md).

Uma única prova final na mesma build completou as três voltas de Stadium Drift 2 (4312), automático e controles normais, em 123,4318 segundos de simulação, com 1.880 pontos bancados em sete drifts. O jogador terminou em quarto: a derrota real foi preservada. Não houve reset do jogador/IA, NaN ou erro do Core. Pausa/retomada, liberação dos dados do evento e retorno ao menu com música avançando passaram. Os arquivos nativos ficaram iguais. Isso qualifica esse evento completo e a pontuação, sem aprovar vitória no Drift, dirigibilidade de todos os carros ou áudio físico.
