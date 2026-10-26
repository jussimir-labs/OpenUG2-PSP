# Beta fechado: limitações e escopo

A candidata noturna mantém PGO/LTO da4u e inclui correções dirigidas de EA TRAX,
bancos e loops de áudio, recuperação da música, sinais de frenagem, pausa/retomada
e aceleração angular na direção do drift. Veja as [notas](NOTAS-NOTURNAS.md) com
identidade e provas. Conferência de hashes não significa aprovação do jogo todo.

Os cinco relatos continuam abertos para confirmação pelo testador no novo
executável. Há correções verificadas em código e provas locais; não foi isolada
no aparelho a contribuição de cada defeito para o motor cortando ou em loop.

A física completa do drift ainda é adaptada: na manobra de 1,2 s, original 79,431°,
port anterior 68,147°, candidata protegida 70,025°. A direção melhorou, mas isso
não aprova a dinâmica completa. O modelo nativo de forças permanece separado.

IA4401 continua adiada: rivais podem ficar presos e se recuperar repetidamente.
A nova candidata não foi testada nem instalada no PSP. As provas humanas e a
instalação anteriores pertencem somente à bf7f. PSP-1000/2000/Go,30 fps globais,
visual completo, carreira completa e ISO PS2 USA original seguem sem aprovação.
O stop do PSPLink na bf7f exigiu recuperação humana; a causa geral não foi provada.

SAVE e reabertura locais usam cópias privadas de fixtures nativas de ambas as
fontes. Provas históricas de aeroporto/350Z, primeiro 206, garagem, corrida inteira
e prêmio natural pertencem aos executáveis registrados; não se transferem por
suposição para uma candidata nova. Saves físicos não foram tocados nesta revisão.

Ao reportar, informe executável, aparelho, fonte, carro, pista e passos exatos.
Para falha do gerador, anexe relatorio.json e o log da etapa.
