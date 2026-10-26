# Roadmap do beta fechado

Itens abertos; esta lista não representa correções concluídas.

- [ ] **Motor em loop durante pausa ([#3](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3)).** Relato físico aberto. O ganho do motor zerou ao pausar na rodada local; isso não reproduz nem invalida o sintoma no aparelho.
- [ ] **Som de frenagem aparentemente ausente ([#4](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4)).** Conferir freio normal × freio de mão, estado das rodas e referência original. A entrada de slip no port não representa o freio normal; causa do caso humano ainda não comprovada.

- [ ] **Drift: estalos/cortes no motor ([#2](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2)) e música que não retorna ([#5](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5)).**
  Relato humano de 25/10/2026, em teste do beta pelo Memory Stick: o som do
  carro corta/estala e a música de fundo também travou depois de um drift.
  Falhas abertas. A retenção de ambiência na volta ao menu foi reproduzida localmente; os estalos do motor permanecem sem causa demonstrada. Investigar o
  mixer/saída e a transição entre EA TRAX e ambiência, sem confundir o mute
  previsto durante drift com uma música que permanece travada depois dele.
  Critério: reproduzir com carro/pista/versão identificados, discriminar o
  subsistema e validar motor e retorno da música em transições repetidas.

- [ ] **Drift: auditar equivalência da dirigibilidade com o original.**
  O port usa PlayerSteering e tuning por modo derivados do original, mas
  também contém drift_grip (ajuste de aderência de até10% após esterço
  sustentado), calibrado com medições do PS2, e um modelo físico adaptado.
  Não declarar paridade integral. Conferir contra PlayerSteering,
  RidePhysicsInfo/CompleatCarPhysicsInfo::TuneForDrift e os dados originais;
  registrar regras reproduzidas, aproximações e campos não implementados.

- [ ] **Card EA TRAX: opacidade da logo EA Games e contorno circular ([#1](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1)).**
  Relato de Jussimir em 25/10/2026: a logo aparece escura/transparente e o
  arredondamento da lateral esquerda está elíptico demais. Comparar com a
  referência visual fornecida: preservar a logo circular e a opacidade correta,
  com o contorno do card acompanhando o círculo. Falha visual aberta, sem
  correção nesta build e sem causa técnica atribuída. Fechar somente após
  comparar os mesmos elementos em escala/proporção equivalentes e validar
  logo, alpha e borda sem regressão no texto e no restante do card.
  Evidências: [imagem do port](../docs/bug-evidence/ea-trax-port.png) e
  [referência](../docs/bug-evidence/ea-trax-referencia.png) no repositório do beta.

- [ ] **Tunnel Construction (4401):** corrigir rivais presos e recuperações repetidas. Limitação conhecida adiada ao roadmap.
- [ ] Continuar validação de desempenho, aparelhos, visibilidade e fonte PS2 original, conforme [LIMITACOES.md](LIMITACOES.md).
