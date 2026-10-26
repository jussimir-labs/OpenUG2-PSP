# Candidata noturna: 26 de outubro de 2026

Pacote local, sem publicação nem aprovação física. Executável c25f1172,
fonte 8f7ec4df1. Mantidos PGO/LTO da 4u, os 63 perfis e hot.ld.
Os dados não foram reconvertidos; os manifestos alteram somente o EBOOT.

Inclui correções do card EA TRAX, bancos/cursor de áudio, recuperação da música,
loops de ambiente na pausa, sinais de frenagem e aceleração angular do drift.
Também cancela rampas pendentes do limitador ao pausar, evitando que ele volte
indevidamente ao retomar em baixa rotação.

Na única execução local da c25f, Freemont 4011/206 original e controles normais:
freio reto de 14,9835 para 0,38615 m/s em 1,533 s, amostra skid_lock válida,
ganho positivo e cursor avançando; pausa/retomada e retorno ao menu passaram.
Press START e logo EA foram conferidos nas capturas. A música selecionada 34
avançou 167→187 blocos. O assert inicial sobre mus_want foi reconciliado offline:
é um pedido já consumido; mus_shown identifica a faixa. Falha bruta preservada,
sem repetir a execução. Não houve entrada real no limitador; essa transição
continua coberta pelo teste nativo, sem afirmar reprodução no aparelho.

A predecessora 73972acf passou drift 4312, liberação da ambiência ativa e
SAVE→primeiro processo novo com fixtures Xbox/PS2, preservando as originais.
Também passou três diagnósticos calibrados de mixer. O delta binário atual muda
somente engsnd_player, sua tabela de saltos e a identificação da build. Código
de save, carro, mixer e filmes permaneceu igual; isso delimita reuso das provas,
sem inventar novo PASS de save/desempenho/aparelho para a c25f.

A física completa do drift continua pendente. Manobra de 1,2 s: original
79,431°, port anterior 68,147°, candidata protegida 7397 com 70,025°. O modelo
original de forças/embreagem/corpo segue isolado e ainda não entrou no jogo.

Não houve instalação nem teste desta candidata no PSP. As provas humanas e a
instalação anteriores pertencem à bf7f. IA 4401 segue adiada. PSP-1000/demais
aparelhos, 30 fps globais, carreira/corridas completas, prêmio natural, visual
completo e ISO PS2 USA original permanecem sem aprovação. Relatos físicos de
áudio continuam abertos para confirmação pelo testador.
