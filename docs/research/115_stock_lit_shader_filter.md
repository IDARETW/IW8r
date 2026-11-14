# 115. A custom shader on a stock map: a colour filter spliced into the game's lit pixel shaders

**Why.** The owner wants a stock map (Shipment) to use the toon shader, delivered as a patch fastfile. A stock map's
surfaces are lit by the game's own pixel shaders, which exist only as compiled bytecode; research 114's route
(compile our own lit shader) fits the converter's own maps, not 372 stock technique sets with 214 different lit
shaders. This note records what was measured and the route that was built instead: keep every stock shader as it is
and pass its final colour through a small filter function.

**Answer in one paragraph.** Every lit pixel shader of `techsets_mp_shipment.ff` is DXBC shader model 5.0, writes its
colour output once (two instructions in 17 of them) and ends in a single `ret`. A filter function compiled on its own
(`float3 iw8rFilter(float3 colour)`, 20 to 40 instructions) can be moved into each of them right before that `ret`,
reading the stock result from a new temp register. The container's checksum is recomputed. The converter's
`editzone "lit_filter"` does this for a whole techset zone in about 6 seconds and gives everything it changes a new
identity, as research 114 section 7 requires. Offline, the edited zone loads in the game's own loaders (ACTS), and a
Direct3D 11 device accepts all 217 spliced shaders. **Not seen in game.**

**Labels.** VERIFIED = measured here (files, tools, device). INFERRED = from usage or general Direct3D behaviour.

## 1. Where things live (VERIFIED, ACTS exports of the stock zones)

- `techsets_mp_shipment.ff` holds the map's **materials** (945), **technique sets** (372) and **shaders** (218 pixel
  shaders), 3,622 assets in all. `mp_shipment.ff` holds 2 materials; it names the others.
- A material points straight at its technique set when the set is in the same zone (691 of 945 after the edit use a
  changed set). 237 materials name a set of another zone (`,name`): 182 `mo/` (characters and gear), 6 `mco/`, 2
  `mso/`, effects (`elcq/`, `ecf/`) and UI. Those keep their stock shaders.
- Technique sets by prefix: `tmo` 101, `mo` 67, `twc4` 57, `tm2o` 34, `tw` 26, `tmco` 24, `twc` 17, `tm2co` 13,
  `mco` 10, `elcq` 10, `tw4` 7, `m2o` 4, `elc` 1.
- Lit techniques: 360 of type 34 (`TECHNIQUE_LIT_FORWARDPLUS_BITMASK`), one each of 35 and 36 (its VRS forms) and
  151 (`TECHNIQUE_WORLDTESS_LIT_FORWARDPLUS_BITMASK`). They use 214 different pixel shaders.
- 189 of those shaders have their program in the map's techset zone. 25 are only named there (`,name`); their
  programs are in the zones every multiplayer map loads. Reading `techsets_common_mp`, `techsets_common_core_mp`,
  `techsets_common_base_mp`, `techsets_common`, `techsets_common_stream_mp`, `techsets_global_mp`,
  `techsets_global_core_mp`, `techsets_global`, `techsets_global_stream_mp` and `techsets_common_br_mp` finds 24 of
  them; `6aa6c9de86ba1b3e192cff937ea7b885` is in none (its one technique stays stock).

## 2. How a stock lit pixel shader ends (VERIFIED, fxc disassembly of the exported `.cso` files)

World shader `01631f08...` (research 95 and 114) and model shader `bce5a6cd...` end the same way:

```
mul r0.xyz, r0.xyzx, cb7[42].xxxx      // the exposure
movc r0.xyz, r1.wwww, r1.xyzx, r0.xyzx
mov r0.w, l(1.000000)
min o0.xyzw, r0.xyzw, l(32255.0, 32255.0, 32255.0, 32255.0)
ret
```

So the output is the lit, fogged colour with the exposure applied (research 114: "pre-exposed HDR radiance"),
clamped, written once. Over all 218 pixel shaders of the zone: 187 write `o0` in one instruction, 17 in two, 14 never
(depth and shadow shaders, not lit). All have exactly one `ret`, at the end; none has `retc`.

## 3. The splice (core `dxbc_splice`, the same code as a header in the converter)

```
stock:   ... ; min o0.xyzw, r0.xyzw, l(32255...) ; ret
spliced: ... ; min r31.xyzw, r0.xyzw, l(32255...) ; <filter: v0 -> r31, o0 -> o0, rk -> r(32 + k)> ; ret
```

- Every operand of the stock shader that names output 0 is turned into a new temp (the stock temp count); the filter's
  input register becomes that temp, its temps are moved above it, its output stays `o0`, its `ret` is dropped.
- `dcl_temps` grows by 1 + the filter's temps. Nothing else in the container changes: signatures, resource
  declarations and every stock instruction stay byte for byte.
- The filter may declare only its input, its output and temps: a texture, a constant buffer, an array or a constant
  table in it is refused, because the stock shader's root signature and declarations cannot grow.
- **Checksum (VERIFIED).** Bytes 4..19 of a DXBC container are a modified MD5 over bytes 20..end: full 64-byte
  blocks as MD5; then one last block `bits | rest | 0x80 | zeros | (bits >> 2) | 1` (or, when 56 or more bytes
  remain, the rest padded with `0x80` and zeros, then a block of `bits`, zeros and `(bits >> 2) | 1`); the result is
  the raw MD5 state. `dxbcChecksum` reproduces the stored checksum of all 218 stock shaders and of fxc's own output.
- fxc `/dumpbin` disassembles the spliced blobs; a Direct3D 11 device creates a pixel shader from each of the 217
  (GUI test "shader filter on stock shaders"), and refuses one with a changed byte and a stale checksum.

## 4. What the converter changes (`editzone "lit_filter"`, x64-zt worktree commit 14bff0d5)

```
"lit_filter": {"program": "<compiled filter .cso>", "shared": ["techsets_common_mp", ...]}
"techsets_only": true      // optional: do not write the map zone (a filter alone changes nothing in it)
```

For every lit technique (types 34, 35, 36, 151) of every technique set the techset zone owns:

| What | Change | Why (research 114 section 7) |
|---|---|---|
| Pixel shader | program = spliced; name `iw8rf_<26 hex of its checksum>`; identity bytes at +36 | a stock name can resolve to the stock asset |
| Shader only named in this zone | a new shader asset with the program from a shared zone, added to the techset zone | the program must be in a loaded zone |
| Technique | name: its 16-hex suffix replaced; every pipeline-state hash replaced, `pso` cleared | a stock hash can match a stock pipeline state |
| Technique set | name `<prefix>/iw8rf_<24 hex>` | as the shader |
| Materials | none needed: they point at the set (same zone) | |

Shipment: 366 technique sets, 369 techniques, 217 shaders (24 from shared zones), 1 shader without a program. The
edited zone is 2.1 MB (stock 1.7 MB); its patch `techsets_mp_shipment.fp` is 140 KB. **The patch does not load in
the game** (research 116: a patch of a signed zone needs the game's signature); the whole edited zone is installed
instead.

Read back from the edited zone (ACTS): 242 pixel shaders, all with a valid checksum; the 217 filtered ones are named
after their checksum; 366 sets have new names, 13 keep theirs; of 227 lit pipeline-state hashes one equals a stock
lit hash (the technique whose shader has no program).

## 5. The editor side

- A shader file may hold `float3 iw8rFilter(float3 colour)` beside or instead of `iw8rSurface` (core custom_shader).
  `customShaderFilterProgramSource` wraps it in a pixel shader that reads `TEXCOORD0` and writes the result;
  `SceneRenderer::compileProgram` builds it (`ps_5_0`, O3, reflection stripped).
- The viewport runs the same function on the stock reference (world and props): a variant of the textured shader
  with `IW8R_FILTER`, applied to `colour x exposure` right before the tone map and divided back.
- File > Rebuild Stock Zone compiles the filter of the open stock map's shader (worldspawn `iw8r_surface_shader`)
  and writes the `lit_filter` edit. `IW8Radiant.exe --shader-filter-program <shader file> --out <file.cso>` does the
  compile alone.

## 6. Limits and open questions

- **Only the finished colour.** The filter does not see the texture, the normal or the light separately, and stock
  geometry has no edge data: no ink lines, and the light's steps follow texture brightness too.
- **Shared shaders stay stock:** characters, weapons, vehicles, effects (their technique sets are in shared zones).
- **Blended surfaces** (decals, glass) pass their premultiplied colour through the filter. The toon filter is a
  ratio on brightness, so it stays bounded; a filter that adds light would not.
- **In game (not verified):** that the engine builds pipeline states for the new hashes of a stock map's techset
  zone the way it does for a converter-built map; that nothing else names the renamed sets (INFERRED from section 1:
  only materials do, by pointer); the cost of building 369 pipeline states at load.
- The exposure constant is assumed the same in the viewport (`game0.y`) and in game
  (`cb7[42].x`): INFERRED from research 95's tone map port.
