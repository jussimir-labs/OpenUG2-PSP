# Fonte PS2: gerar o jogo do disco do PS2

Os conversores leem o disco do PS2 (Need for Speed Underground 2, NTSC-U,
`SLUS_210.65`) no lugar do disco do Xbox. Para isso:

1. `tools/extract_ps2.py CAMINHO.iso` extrai a ISO 9660 para `game/ps2`.
2. `NFSU2_SOURCE=ps2 tools/build.sh` gera a dist. Em um conversor isolado,
   use `--source ps2` ou `--game game/ps2/NFSUNDER`.

O `tools/n2source.py` confere se o disco é o da fonte pedida e grava
`source.txt` na dist. Um conversor da outra fonte para ali e não mistura os
dois discos. Os caches de índice em `.tools/cache` (objetos da cidade,
vinis) ficam separados por disco.

## O que muda do Xbox para o PS2

O container é o mesmo: ZDIR de 24 bytes, ZZDATA0-3 e chunks bChunk
little-endian. Os 1.217 arquivos em comum têm o mesmo hash.

| Classe | No PS2 | Leitor |
|---|---|---|
| Tabelas do GLOBALB (carreira, física, IA, trânsito, câmeras), ROUTES/PATHS, colisão e instâncias da cidade, LANGUAGES | iguais byte a byte | os mesmos |
| Música (`MUSICSFX.MUS`), motores GIN | iguais byte a byte | os mesmos |
| Malhas (cidade, pistas, carros, rodas, peças, marcador do GPS) | pacotes VIF do VU1 (`0x134a00-a02`) | `n2formats.ps2_mesh` |
| Texturas | CLUT4/CLUT8 e RGBA no swizzle PSMT8 do GS | `n2formats.decode_ps2_texture` |
| Pessoas dos NIS (malhas com pele) | VIF com posições e UVs em f32 cujo byte baixo traz os ossos (x 4) e os pesos (/255) | `n2formats.ps2_skinned` |
| Envmap do showroom | um sphere map (`CARSELECTENVMAP`) | `convert_car.sphere_face` |
| Tabelas do executável (som, rotações dos NIS, zonas e reverbs, ajuda, pilotos, flag girls) | no `SLUS_210.65`, parte montada por código | `n2exe` + `n2mips` |
| Efeitos, câmbio, turbo, derrapagens, stitches, laços AEMS | VAG (ADPCM do SPU), 24–32 kHz contra os 48 do Xbox | `convert_audio.decode_vag`, `encode_xa` |
| Áudio dos NIS | EA-XA a 32 kHz (Xbox: 36) | `nis.n2sp` versão 2, com a taxa |
| Filmes | `MOVIES/*.MPC` (MPEG-2 512×192 anamórfico, EA ADPCM R3 estéreo) | ffmpeg |

### Malhas (`ps2_mesh`)

Cada objeto traz:

- em `0x134a01`, um registro de 16 bytes por lote, com vértices, triângulos,
  submalha, textura e material;
- em `0x134a02`, um lote VIF por MSCAL.

Os endereços do VU dentro de um lote:

| Endereço | Conteúdo |
|---|---|
| 0 | cabeçalho com o número de vértices |
| 1 | palavras de flags; a palavra 1 é a escala das posições |
| 2–3 | caixa |
| 4 | cone de culling |
| 5–28 | posições |
| 29–44 | UVs |
| 45–76 | cores |
| 77 em diante | normais |

Cada componente:

- **Posições:** u16 × a escala (0,0002 nos carros, 1/256 nas peças grandes
  da cidade), somado ao mínimo da caixa truncado menos 1. Há também lotes
  em f32. Ficam em blocos de 4 vértices (4 x, 4 y, 4 z).
- **UVs:** valor/4096, na ordem u0 v0 v1 u1 u2 v2 v3 u3.
- **Normais:** s8/127, nos mesmos blocos das posições.
- **Cores:** 0x80 = 1,0.
- **Tiras:** cada lote é uma tira, reiniciada pelas flags ADC (`ps2_adc`).

Comparado com o Xbox, com o mesmo triângulo, a mesma orientação e a mesma
textura:

| Malha | Iguais |
|---|---|
| Carros | 99,94–99,99% |
| Aros | 100% |
| Cidade | 99,5–100% |

As UVs e normais dos carros batem 100%, e as UVs da cidade 99,1%.

### Texturas

- **Resolução:** na cidade, 1.580 de 1.611 iguais, 24 com metade e 7 maiores.
  No GLOBALB, 37 de 181 têm metade. Os carros vêm em 8 bits contra o DXT1 do
  Xbox.
- **Formato:** as paletas já são o CLUT8 que o port usa, então não há
  requantização.
- **Mipmaps:** o PS2 traz só o nível 0, e o `convert_mips` gera os outros.

## Lacunas conhecidas (o PS2 não tem, ou o port ainda não lê)

- **Falas da Rachel:** a conversão offline agora lê EA MicroTalk v3 com blocos PCM (22.050 Hz), usando o decoder fixado em `vendor/utk/PROVENANCE.json`, e gera EA-XA mono a 32 kHz para o leitor existente. Os 857 índices e blocos foram conferidos na saída isolada de 25/10; reprodução no jogo PS2 convertido e no PSP ainda precisa de validação.
- **Estalos do escape (`Sparkchatter_00`):** o banco do PS2 tem 84 amostras
  por som, e o efeito vira silêncio.
- **ROADNOISE:** o PS2 não tem o slot 4. O som do slot 3 toca no lugar dele.
- **Cores pré-iluminadas da cidade:** o PS2 corta em 128 os ~20% de vértices
  mais claros, que no Xbox vão até 255.
- **Seções G1, O13, O6 e R12 da cidade:** só existem no Xbox.
- **Áudio em taxas menores:** efeitos de 24 a 32 kHz em vez de 48, e filmes
  com 192 linhas em vez de 480.
- **Ícones de botão:** `XBOX_*` não existem no PS2 (o port usa os próprios
  ícones do PSP).
