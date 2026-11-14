# 113. Material tint: why the speedball containers draw pink

**Why.** On mp_m_speedball the grey shipping containers (`tm2o/storage_shipping_container_*`,
`tm2co/mout_container_wall_single*`) draw pink in the editor but light grey in the game. Their blue `_blue` variants
draw orange in the editor.

**Answer in one line.** The game's tint rule is the one in research 95. Flag bit 3 does nothing to the tint. The
editor reverses the tint's four bytes when it rebuilds psData from the material constants. This happens whenever the
constant word is a NaN, which is every tint with byte 3 = 0xFF and blue byte >= 0x80.

**Labels.** VERIFIED = read in the disassembly, the techset arguments or the decoded data. INFERRED = meaning taken
from usage.

## How it was obtained

ACTS (`mw19replay`) exports from `techsets_mp_m_speedball.ff`, plus one material from `techsets_mp_aniyah_tac.ff`.
Work files are in `out/agent/tint113/`:

- The 617 speedball materials (deleted after the comparison). The kept scripts are `cmp.py`, `bits.py` and `tint.py`.
- Techset `tm2o/lit_3_lit_rpl_ta1_800800_1002000000000010_0_2_1_0_0_13810015b_0_0_1_0_0_0_0_3818015f_0_1_2_0_0`.
  This is the 2-layer static-model set used by `storage_shipping_container_metal_painted_01_second` and its `_blue`.
  Its `TECHNIQUE_LIT_FORWARDPLUS_BITMASK` pass uses PS `7e79ed155eb795dc26e7056b4159bc5a`
  (`MTL_CONSTANT_LAYOUT_SMODEL_LIT_PROBE`).
- The PS disassembly is `out/agent/tint113/ps.asm`, cited below as **P:line**.

## 1. Bindings of the lit pass (VERIFIED, techset `args`)

| PS register | Source |
|---|---|
| cb3[0] | constant 72 (layer count float, probe flag) |
| cb3[1] | constant 0: layer 0 flags (`.x`) and UV transform |
| cb3[2] | constant 9: layer 0 tint (`.x`), heat (`.y`) |
| cb3[3] | constant 54: spec / gloss remap |
| cb3[4], cb3[5], cb3[6] | constants 1, 10, 55: the same three rows for layer 1 |
| t0, t1, t2 | slots 0 (colour), 9 (normal + gloss), **59 (layer 0 tint mask)** |
| t4, t5, t6, t7, t3 | slots 1, 10, 28 (layer 1 colour, normal, reveal), 60 (layer 1 tint mask), 123 |

## 2. The tint rule (VERIFIED, P:110, P:129-143)

```
uint  flags = cb3[1].x;                                  // P:110 only `& 64` is read; cb3[1] has no other use
float3 tint = float3(cb3[2].x & 255,                     // P:129-131 byte 0 = R
                     (cb3[2].x >> 8) & 255,              // P:132-134 byte 1 = G
                     (cb3[2].x >> 16) & 255) / 255;      //           byte 2 = B; byte 3 is never read
if (flags & 64)                                          // P:135
    tint = lerp(1, tint, t2.Sample(uv).r);               // P:136-139 slot 59, red channel
albedo = t0.Sample(uv).rgb * tint;                       // P:143 t0 is an sRGB format, so this is linear
```

This is the same rule as research 95 (W:118-128 in the world PS).

- **Bit 3 (0x08) of layer 0's flags is not read by this shader.** cb3[1] appears only at P:110-111: the `& 64` mask and
  the UV transform. The plain containers have flags 0x4F and the `_blue` / `_wet` variants have 0x47. Both go through
  the identical path. VERIFIED.
- In layer 1's flag word (cb3[4].x), bit 3 picks the reveal from t6.x (P:178, P:185). Bit 4 adds the t6.y height
  reveal (P:184). Bit 5 multiplies by t3 (P:188). Bit 6 is the layer-1 tint mask on t7 (P:201-206). Bits 7-8 are the
  blend mode (P:230). VERIFIED. The layer-0 word probably uses the same layout. Bits that only matter for a revealed
  layer are then meaningless on layer 0. INFERRED.
- Layer 1's tint (cb3[5].x) uses the same byte order (P:195-200). Its byte 3 *is* read, as a reveal sharpness (P:179).

## 3. The real bytes versus the editor's (VERIFIED)

ACTS writes a non-finite constant word as `{"bits": "<hex>"}`. The hex is the word's bytes in memory order: for all
64 non-finite words across the 617 speedball materials, the string appears verbatim in psData and never reversed
(`bits.py`). `src/qtapp/material_cache.cpp:601` (and the copy in `tools/patches/material_consts.py:73`) parses that
string with `toUInt(16)` and `memcpy`s it, which reverses the four bytes. The single-layer rebuild of psData (rows
72/0/9/54, lines 611-618) then writes reversed bytes into row 2. `layerCount` is 0 in these material headers, so that
path is taken.

`cmp.py` compares the speedball psData with the editor cache (`%TEMP%\iwb\material_cache\index.json`): 265 entries
match and 93 differ. Every container difference is exactly this byte reversal of cb3[2].x:

| Material | Real cb3[2].x bytes | Editor cache | Word is NaN? |
|---|---|---|---|
| `..._metal_painted_01_second` (grey) | `c1 ad 96 ff` | `ff 96 ad c1` | yes, so reversed |
| `..._metal_painted_01_second_blue` | `22 40 87 ff` | `ff 87 40 22` | yes, so reversed |
| `tmo/storage_shipping_container_lod_blue` | `24 3e 84 ff` | `ff 84 3e 24` | yes, so reversed |
| `..._door_01_blue_02` | `20 37 66 ff` | same | no (finite -3.06e38) |
| `tm2co/..._metal_painted_01_second_green` (aniyah_tac) | `39 4a 3d ff` | same | no (finite) |

**When the bug hits.** Byte 3 = 0xFF (the usual value) plus blue byte >= 0x80 makes the float exponent all ones,
so the word is a NaN and gets reversed. Green and dark blues (blue byte < 0x80) come through correctly. Pale greys
(blue byte about 0x96) and saturated blues (0x84 to 0x87) do not.

The other differences in the 93 are unrelated: rows 3 to 6 of materials with transmission or emissive constants, and
zero-filled rows. (`tm2o/fence_metal_gate_spiked_01_white` and `tm2o/uk_siding_metal_corrugated_01` are reversed in
the same way.)

## 4. Numbers (VERIFIED, `tint.py`)

Inputs are slot 0 `storage_shipping_container_metal_painted_01_clean_c&defaultspec` (BC1 sRGB, 1024², mean sRGB
193/192/191) and slot 59 `..._clean_c` (BC4, mean 0.877). For each texel: `albedo = srgb_to_linear(c) * lerp(1,
tint, m)`. The table gives the mean of that, re-encoded to sRGB:

| Tint bytes (R G B) | Mean albedo, sRGB | Reads as |
|---|---|---|
| none (ff ff ff) | 195 194 192 | untinted |
| **c1 ad 96** (real grey) | **174 166 156** | light, slightly warm grey (game) |
| ff 96 ad (editor-reversed grey) | 195 157 165 | pink (editor today) |
| **22 40 87** (real `_blue`) | **97 116 150** | blue (game) |
| ff 87 40 (editor-reversed `_blue`) | 195 151 115 | orange (editor today) |
| 20 37 66 (`_blue_02`) | 95 110 135 | blue (editor already right) |
| 24 3e 84 (`lod_blue`) | 98 115 148 | blue |
| **39 4a 3d** (`_green`) | **112 121 113** | green-grey (editor already right) |
| 99 20 1d (`_red`) | 159 94 91 | red |

The editor's pink (195, 157, 164) is reproduced exactly by the reversed bytes.

## 5. What to change (for the implementer)

- Parse `"bits"` as four bytes in memory order: `QByteArray::fromHex(bits)`, not `toUInt(16)`. Apply this in
  `material_cache.cpp:601` and in `tools/patches/material_consts.py`. Then bump `tintRules` (or the psData rule
  version) so cached entries are resolved again. Every non-finite constant word is affected, not only tints.
- No new shader mode is needed: one rule covers flags 0x47 and 0x4F. To tell from psData which rule applies, only
  `cb3[1].x & 64` matters (mask on or off).
- `tools/build_material_tints.py` skips NaN tint words and requires byte 3 == 0. As a result it lists none of these
  containers. Byte 3 is not read by the lit shader, so the tool can drop that check and decode `bits` as bytes.

## Open

- What layer 0's flag bits 0-3 do in other shaders (vertex shader, depth / shadow, other techsets). In this lit PS
  they are not read.
