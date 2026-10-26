# Candidata 14: áudio dos pneus dos rivais

Executável **8c51055e**, SHA256 `8c51055ec4443e09e7c58c45f00b82659e11529cee398da49a6cf9b0fd505049`. Rivais de corrida e encontros no free roam agora têm efeitos próprios de aceleração em falso, derrapagem e frenagem/travamento dos pneus. O original usa um emissor agregado por rival; trânsito comum fica fora desse caminho. A física e a IA não foram alteradas.

Configuração 4u preservada: PGO=use, LTO=1, RELEASE=0, PSP_LARGE_MEMORY=1, 63 perfis e hot.ld. Dados Xbox/PS2 iguais aos da RC13; apenas o EBOOT mudou. Não houve reconversão.

Passaram contratos nativos do adaptador/mixer com sanitizadores e bancos reais Xbox/PS2, além de três cenários dirigidos no PPSSPP headless: Circuit4011, Drift4312 e rivais no free roam. Nenhuma falha de banco, reset ou NaN foi registrada. São roteiros diagnósticos; o Drift foi encurtado. Ativação medida não equivale a aprovação de audição ou paridade sonora integral.

As provas anteriores de Drift por controles normais, carreira, prêmio e save/reabertura pertencem à RC13 e7ce, mantidas separadamente. Os relatos #1–#5 continuam abertos. Esta candidata não foi publicada, instalada nem executada no PSP ou Memory Stick. PSP-1000/outros modelos, 30 fps em todas as corridas, visual e carreira completos continuam pendentes; a correção da 4401 permanece adiada.

[Notas em inglês](../NIGHTLY_NOTES.md). [Recibo atual](../docs/qualification/OVERNIGHT-QUALIFICATION.json).
