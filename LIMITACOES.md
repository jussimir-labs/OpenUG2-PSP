# Limitações do primeiro beta fechado

Executável `bf7f21e3`, com PGO, LTO e a configuração da 4u restaurados.
A única alteração nova de código restaura a visibilidade do texto Press START.
A identidade conferida pelo gerador não aprova todas as corridas ou aparelhos.

- **Tunnel Construction (drag 4401):** rivais podem ficar presos e ser
  reposicionados repetidamente. Correção adiada ao roadmap por decisão de
  Jussimir. A limitação foi observada na build anterior; não foi retestada aqui.
- **Desempenho e aparelhos:** 30 fps globais e compatibilidade com PSP1000,
  PSP-2000 e Go continuam sem aprovação. A checagem humana no PSP-3000 via
  PSPLink confirmou o texto, condução breve na Freemont e retorno ao menu.
  Ela não comprova corrida completa, instalação no cartão ou save físico.
- **PSPLink:** após a checagem, o encerramento automático exigiu reabrir
  o PSPLink manualmente. Em seguida, a saída do jogo e a liberação da sessão
  foram confirmadas. O relato humano de retorno ao menu foi preservado; a
  leitura posterior não confirmou independentemente essa tela. Não há garantia de que
  a causa do Bus error da build anterior tenha sido eliminada globalmente.
- **Visual:** diferenças de tom, materiais e visibilidade permanecem em revisão.
- **Fonte PS2:** os 179 arquivos americanos foram conferidos a partir de uma
  ISO reconstruída. A ISO original USA continua sem conferência. Outras regiões
  e revisões são recusadas. Veja [tools/PS2_SOURCE.md](tools/PS2_SOURCE.md).

Neste executável passaram no PPSSPP: texto do título, trecho inicial da 4011,
save nativo e reabertura com dados Xbox e PS2 usando perfis de teste existentes.
O teste separado de diálogos passou por perfil novo, save de carreira e garagem
e leitura em processo novo. Os registros técnicos preservam uma limitação de encerramento do teste
automatizado após sua primeira fase.

Os testes anteriores de aeroporto/350Z, primeiro 206, primeira garagem e prêmio
real de vitória pertencem à RELEASE03 anterior. Eles não são uma repetição
completa desses percursos neste executável. Nenhuma carreira inteira foi aprovada.

Para relatar falhas, informe o SHA da build, aparelho/emulador, fonte Xbox/PS2,
pista e passos. Para o gerador, inclua relatorio.json e os registros da etapa.
