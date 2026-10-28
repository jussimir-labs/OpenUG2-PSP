> **RC21:** este documento descreve uma candidata anterior. Use as [instruções atuais em inglês](../README.md), as [notas da RC21](../RELEASE_NOTES.md) e as [limitações atuais](../KNOWN_ISSUES.md).

# Beta fechado: escopo e limitações

**Rascunho RC18 (`da37c112`).** Correções de falha de carregamento do sponsor e posse dos bancos de áudio têm provas host/revisão dirigidas. Passaram4602 normal de três voltas/SAVE/frio e SAVE/reabertura privados PS2. Interface/SAVE/reabertura do sponsor sintético e conferências normais de estado/frenagem/pausa/menu Focus/Rachel passaram; isso não aprova estágio2 normal ou audição. O progresso conquistado está no estágio1, sem aprovar sponsor normal do estágio2. Identidade continua separada de qualidade. Veja o [recibo atual](../docs/qualification/RC18-QUALIFICATION.json).

## Relatos ainda abertos

Confira novamente no novo executável, informando aparelho, fonte, carro, pista e passos. Uma correção de código não encerra automaticamente a observação de quem reportou.

- [Logo/borda EA TRAX (#1)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/1): ordem de desenho e proporção circular corrigidas e conferidas localmente; confirme durante o jogo.
- [Cortes/cliques no Drift (#2)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/2): bancos separados e cursores de loops completos corrigem defeitos confirmados. O sintoma ouvido ainda precisa da confirmação do testador; a causa física não foi totalmente isolada.
- [Loop na pausa (#3)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/3): testes locais silenciaram pedidos do motor; limpeza do ambiente e rampas pendentes do limitador foram corrigidas. Isso não identifica todos os sons do relato original.
- [Som de frenagem (#4)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/4): aceleração e sinais no referencial do pneu foram corrigidos. O Drift humano fornece agora estado nativo de rodas/corpo; outros caminhos mantêm estimativas do port. Audição e paridade de todos os ramos continuam pendentes.
- [Música após Drift (#5)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/5): a recuperação anterior no menu é histórica. Drift/Drag seguem a política original de ambiência; não se declara nova correção de música de corrida. Informe a transição exata.

## Dirigibilidade e progresso

- **Drift humano:** Core derivado do original integrado para os 29 registros americanos suportados, peças de desempenho e nitro. A prova histórica e7ce usou trechos curtos com 206 original: 4312 automático/manual e 4174. Não aprova todos os carros, peças ou paridade de pista inteira. Comparações históricas de yaw de candidatas antigas não medem este executável.
- **Dyno personalizado e presets especiais:** usam o modelo anterior durante o evento inteiro, escolhido antes da largada. Os cinco sliders antigos não são convertidos por adivinhação para o formato original. Uma falha do Core não troca a física no meio da corrida.
- **Contatos e casos especiais:** colisões horizontais permanecem uma adaptação 2D. Comportamentos extremos/wild, hidráulica e demais ramos não qualificados continuam limitados.
- **Carreira/SAVE:** os testes privados históricos daRC13 e7ce cobrem fixtures já existentes Xbox/PS2 e uma vitória natural na corrida Xbox 4101, prêmio $250/REP 300, SAVE nativo e um processo frio restaurando esse novo progresso. Isso não aprova carreira completa, todas as corridas ou save físico. O gerador não substitui automaticamente seus saves.
- **4401:** rivais podem ficar presos e se recuperar repetidamente em Tunnel Construction/drag. Trabalho mantido adiado.

## Aparelhos, fonte e apresentação

- **Hardware:** não houve instalação nem execução da37 no PSP ou Memory Stick. As observações bf7f no PSP-3000/cartão são históricas. PSP-1000/2000/Go e saves físicos seguem sem qualificação.
- **Desempenho:** uma cena histórica e7ce calibrada de Drift tem registro separado; não aprova 30 fps globais ou memória/desempenho de todos os aparelhos.
- **Visual:** tom, materiais, UVs, visibilidade/culling e portas precisam de revisão. Rivais do Free Roam já recebem neon pela tabela original de probabilidades por nível; alguns corretamente ficam sem neon. PRNG, escolha de packs e renderização mantêm adaptações.
- **Filmes/áudio:** os 28 filmes estão estruturalmente presentes por fonte. Essa conferência não aprova reprodução integral, todas as famílias de áudio ou audição no aparelho. Pular filmes continua disponível.
- **Imagem PS2 USA:** o suporte SLUS_210.65 e a conferência retida dos 179 arquivos não verificam a imagem original do disco, que segue desconhecida. PAL/outras revisões são recusadas. Veja [diferenças da fonte](../tools/PS2_SOURCE.md).
- **PSPLink:** parar a bf7f exigiu recuperação humana. Não foi provada uma correção geral do encerramento PSPLink/Bus error antigo.

Em falhas do gerador, anexe `relatorio.json` e o log da etapa. O [manual ilustrado](manual-reportar-bugs.pdf) foi preservado. [English](../KNOWN_ISSUES.md).

Uma prova histórica da RC13 e7ce completou as três voltas de Stadium Drift 2 (4312), automático e controles normais, em 123,4318 segundos de simulação, com 1.880 pontos bancados em sete drifts. O jogador terminou em quarto: a derrota real foi preservada. Não houve reset do jogador/IA, NaN ou erro do Core. Pausa/retomada, liberação dos dados do evento e retorno ao menu com música avançando passaram. Os arquivos nativos ficaram iguais. Isso qualifica esse evento completo e a pontuação, sem aprovar vitória no Drift, dirigibilidade de todos os carros ou áudio físico.

## Estado atual e provas anteriores

A implementação NOS/PURGE da RC17 precisa de confirmação auditiva no PSP; outros perfis de câmera continuam não qualificados. O ciclo histórico RC17 da4101 registrou 141,3679 segundos de simulação, $250/REP200/uma corrida, SAVE nativo e processo novo restaurando todo o estado preservado no título e no menu da carreira. A amostra do título após a espera mostra Press START; a reabertura não fez novo SAVE e os arquivos nativos de origem ficaram iguais. Os relatos permanecem abertos.

Na RC15 adb5, entrada da carreira via Up/Accept e entrega da apresentação/primeiro movimento do Drift passaram; três capturas foram revistas. Não houve corrida completa, prêmio ou novo SAVE/frio nesses dois gates. O heap permaneceu desconhecido; nenhuma falha viva de alocação foi reproduzida. Essas observações não são uma nova execução RC17. A implementação NOS/PURGE atual substitui a investigação ainda aberta do timbre na RC15, sem dispensar a audição do testador.

## Escopo do nível sonoro RC17

NOS/PURGE usa a aplicação de SFX e o arredondamento de nível derivados do original antes da ativação. Master Volume mantém o controle final compartilhado do port; curva Master original e outros perfis de câmera não estão aprovados. Efeitos dirigidos históricos RC17 8ef4 das duas fontes nativas não registraram erros, recusas ou pedidos descartados. Trecho normal 4311/retorno ao menu e SAVE/reabertura privados das duas fontes passaram, sem aprovar um Drift inteiro ou todas as câmeras. A RC16 a258 concluiu vitória normal na 4101, prêmio $250/REP300, SAVE nativo e reabertura em processo novo; são provas [somente históricas](../docs/qualification/RC16-HISTORICAL-ACCEPTANCE.json).

## Limites atuais RC18

A tela de sponsor que falha não assina nem salva automaticamente;12 casos host da fonte e revisão cobrem o reparo. O teste concluído de cinco vitórias sintéticas/interface/SAVE/reabertura é separado do progresso real estágio1. Os reparos sonoros cobrem três leituras inválidas reproduzidas e cinco casos positivos de posse, mantendo16 loops/361434 amostras PCM. Isso não comprova a causa do sintoma ouvido pelo testador. Conferências de estado/frenagem/pausa/menu Focus/Rachel passaram num processo, sem PCM ou aprovação sonora. A audição humana continua pendente.

O ciclo atual4602/vitória/SAVE/frio preserva $500/REP400/duas corridas e melhores5+6 no estágio1. Não aprova estágio2 normal, carreira inteira, todas as trocas de banco ou aparelho.
