# 114. Custom pixel shaders: the stock lit PS interface a replacement must match

**Why.** The owner wants custom shaders on custom maps that render in game. The plan: clone a stock technique set and
replace only its main lit pixel shader with one we compile ourselves. The vertex shader, the root signature and the
pipeline setup stay stock. This note lists what that replacement must match, gives a working HLSL skeleton
(`114_custom_ps_skeleton.hlsl`), and shows that fxc builds it into a blob with the same interface as the stock one.

**Answer in one paragraph.** Replace only technique 34 (`TECHNIQUE_LIT_FORWARDPLUS_BITMASK`). Compile with
`fxc /T ps_5_0 /E main /O3 /Qstrip_reflect`. Use the stock input signature (5 inputs for the world, 7 for opaque
static models). Write only `SV_TARGET0`: alpha 1, colour = pre-exposed HDR radiance, clamped to 32255. Declare only
registers the stock shader declares. Give the new shader, technique and technique set new names, and give the
technique new pipeline-state hashes. The skeleton compiles. Its input and output signatures are byte-for-byte
identical to the stock blobs, and so is its link to the stock vertex shader (VERIFIED, section 6). **The x64-zt
converter already does the same swap** (clone a stock techset, put a D3DCompile'd `ps_5_0` in the lit technique) for
every editor-built map, and those maps draw in game. That is the strongest evidence the approach works. The converter
also swaps the vertex shader. Keeping the stock vertex shader, as this plan does, has not been tried in game.

**Labels.** VERIFIED = read in the disassembly, the ACTS techset export or the converter source, or measured here.
INFERRED = meaning taken from usage or general D3D12 behaviour. **W:line** = the world PS disassembly
`out/agent/post109/asm/W_01631f08.asm`, NUL bytes stripped (identical to
`shader_research/shw/.../01631f08...0.asm`). **M:line** = the model PS disassembly
`out/agent/ps114/st/mw19replay/techsets_mp_backlot2/assets/pixelshader/bce5a6cd941c127203a92ac3bf9427f2.0.asm`, NULs
stripped.

## 1. Which techsets

What a built custom map uses (VERIFIED, `x64zt_output/mp_cinderrail.iw8-assets.tsv` and an ACTS export of its
`techsets_mp_cinderrail.ff`): all 24 of its techsets are converter-generated `tw/mw120r_mp_cinderrail_*`. There is no
stock name among them. Each is a clone of one of two stock layouts:

| Use | Stock techset cloned | Stock lit VS / PS | Source |
|---|---|---|---|
| **World brush surfaces** (one atlas material `w/mw120r_<map>` for nearly all; glass, foliage and sky variants) | `tw/lit_3_lit_rpl_ta1_804000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0` (MTL_TYPE_WORLD) | VS `d9ff726d22c611ae381bfa6786d6c24c`, PS `01631f08811f0f1923c11aaa9718d79b` in techsets_mp_backlot2 | `resources/techset_template.json` ("Exact Replay static BSP material tw/rubber_trim_black_baseboard_01") |
| **Opaque static models** (141 `w/mw120r_<map>_model_*` materials) | static-model layout of `tm2co/lit_3_lit_rpl_ta1_800800_..._13810015b_0_0_1_0_0` (the curb stone of doc 95: PS `033abcdb`, adds a `COLOR` input) | | `ApplyStaticModelTechniqueLayout` (iw3_render_assets.cpp:3392); its state hash `e5d7c9c4...` matches that stock set |

The two stock pixel shaders documented here:

- **World:** PS `01631f08` (doc 95's world shader). It is the lit PS of 13 of the 34 `tw/` techsets in
  techsets_mp_backlot2, the most of any.
  - Variant: the converter's template carries `a3f486861f1e2821a746e87d59f61a92`. It is the same shader plus one
    `Texture2DArray t113` holding the lightmap ambient fraction (the br_tut-style 3-array lightmap of doc 97). Its
    signatures are identical (VERIFIED by `diff`).
- **Opaque static model:** PS `bce5a6cd941c127203a92ac3bf9427f2` with VS `546804181c1e1a558af9968848e40b7b`, in
  `tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0` (MTL_TYPE_STATIC_MODEL_SELFVIS).
  - It is the most-used static-model lit PS in techsets_mp_backlot2: 34 of 200 `tmo/` techsets and 11 of 71 `tm2o/`.
  - The curb shader `033abcdb` (tm2co/tmco) has the same interface plus `COLOR` at v1, which pushes the other inputs
    one register up.

## 2. Techniques of a lit techset, and which keep the stock shader

VERIFIED from the ACTS techset JSON (`techniqueMask` 0x418000001 = bits 0, 27, 28, 34):

| Type | Technique | VS / PS | Colour RTs | Constant layout | Keep stock? |
|---|---|---|---|---|---|
| 0 | `TECHNIQUE_DEPTH_PREPASS` | `,5dea237a` / `,b7acbe2c` | 2 | WORLD_AUX_QUERY | **Yes.** |
| 27 | `TECHNIQUE_BUILD_SHADOWMAP_DEPTH` | `c0ba5f11` / `null.hlsl` | 0 | WORLD | **Yes.** |
| 28 | `..._DEPTH_BACK_FACE` | same | 0 | WORLD | **Yes.** |
| 34 | `TECHNIQUE_LIT_FORWARDPLUS_BITMASK` | `d9ff726d` / `01631f08` | 2 | WORLD_LIT_LMAP | Replace the PS only |

Notes on the table:

- **Depth prepass PS (VERIFIED).** The stock prepass PS `b7acbe2c` (504 bytes, from the converter template) writes
  **only `SV_TARGET1`**. That output is the world normal packed 10:10:10 into a uint (`ftou`, `imad` by 1024 and 2^20).
  This is the normal buffer that GTAO and SSR read (doc 104). The prepass therefore owns RT1, and the lit pass must
  not write it.
- **Depth prepass, name-only reference (INFERRED).** The leading comma in `,b7acbe2c` / `,5dea237a` marks a name-only
  reference to a shader held in another zone. ACTS found neither shader in techsets_mp_backlot2 or the common
  techset zones.
- **Why the lit pass depends on the prepass (INFERRED from the converter comment at iw3_render_assets.cpp:3646).** The
  lit pass shades only where its prepass wrote depth. Keeping the stock VS in both passes keeps the positions
  identical, so the depth test passes. The replacement must not `discard` (the stock lit PS has none: W and M contain
  0 `discard`). It must not write `SV_Depth` either.
- **Static models.** The `tmo/` set has the same four technique types (VS `,09426b61` / PS `,063bfb76` prepass,
  `a5b58ec1` / `null.hlsl` shadows).

## 3. Interface of the stock lit pixel shaders (VERIFIED)

Both stock lit pixel shaders are **DXBC `ps_5_0`**, never DXIL. The blobs hold only three chunks: `ISGN`, `OSGN` and
`SHEX`. There is no `RDEF`, `STAT` or `RTS0`, so the shader embeds no root signature. Both declare
`dcl_globalFlags refactoringAllowed`, and all registers are in space 0 (SM 5.0 has no spaces).

### Input signature

All inputs are `float` and use `linear` interpolation. `SV_POSITION` is `linear noperspective`.

| Reg | World PS 01631f08 (W:9-13, 67-71) | Mask | Model PS bce5a6cd | Mask |
|---|---|---|---|---|
| v0 | SV_POSITION (xy pixel, w = linear view depth, W:86) | xyzw | SV_POSITION | xyzw |
| v1 | TEXCOORDS (xy read, zw written by the VS) | xyzw | TEXCOORDS | xyzw |
| v2 | LMAPCOORDS | xy | NORMAL | xyz |
| v3 | NORMAL (world, not normalised) | xyz | TANGENT (w = bitangent sign) | xyzw |
| v4 | TANGENT (w = bitangent sign) | xyzw | AMBIENT_COLOR (rgb, a = ambient fraction) | xyzw |
| v5 | | | HIGHLIGHT_DIR_VIS | xyz |
| v6 | | | SELFVIS | xyzw |

The stock vertex shaders write exactly these (`d9ff726d` and `546804181c` output signatures). Both vertex shaders
take only `SV_VertexID` and pull vertices from buffers (world: t13 stride 88, t14/t15 raw). **There is no input
layout to match.**

### Output signature

There is one output, `SV_TARGET0` (`float4`, register 0, xyzw), in both shaders. It holds:

- `.rgb`: HDR radiance times `cb7[42].x` (radiometricScale, the pre-exposure).
- `.a`: 1.
- All four channels go through `min(., 32255)` (W:1215-1216, M:1103).

The techset says `colorRtCount 2`, `colorRtFormats [129, 1, 0, 0]` (world). The format numbers are a game enum and
are not decoded. RT1 is the prepass normal buffer (section 2), which the lit PS does not write.

### Declarations

| | World PS 01631f08 (W:24-66) | Model PS bce5a6cd |
|---|---|---|
| Constant buffers | **b2**[8], **b3**[4], **b7**[213], **b9**[1] | b2[8], b3[4], b7[187] |
| Samplers | s3, s5, s8, s11, s14 | same |
| Material/code SRVs | t0 struct(8), t1 2D, t2 2D, t3 2D, t4 2D, t5 struct(8) | t0, t1, t2, t3 2D, t4 struct(8) |
| Engine SRVs (same in both) | t41 s96, t44 s68, t45 s60, t46 s4, t47 s4, t49 s96, t50 s4, t52 s4, t56 s256, t57 s4, t85 2D, t93 2DArray, t94 2D uint, t95 2D, t116-t119 2D, t121 3D, t122 1D, t123 2D, t124 2DArray | same set |
| World-only SRVs | t102 2D, t103 2D, t104 3D, t105 2DArray (light grid), t114 / t115 2DArray (lightmaps) | none |
| UAVs | none | none |

(`s96` = structured, stride 96.) What each register holds is in doc 95 section 1.

## 4. What the technique arguments bind (VERIFIED)

An argument is 6 bytes: `u8 type, u8 stage mask, u16 dest, u16 index`. The stage mask is 2 = VS, 16 = PS, 18 = both.
This layout is VERIFIED by matching the hex `args` of `techset_template.json` against the ACTS JSON of the same
technique. The world lit technique has 17 arguments: 13 stable and 4 "never" (INFERRED: the never ones are the 4
material constants). `stableArgSize 128` = 8 rows of b2.

| Type | Stage | Dest | Index | What it fills (use in the world PS) |
|---|---|---|---|---|
| CODE_CONST (0) | VS | b2 row 0 | 164 | VS view/projection data |
| CODE_CONST | PS | b2 row 4 | 0 | camera origin: `worldPos = P*v0.w + cb2[4].xyz` (W:461, 540) |
| CODE_CONST | VS | b2 row 5 | 1 | VS data |
| CODE_CONST | PS | b2 row 6 | 4 | `.zw` = screen UV scale: `screenUV = v0.xy*cb2[7].x*cb2[6].zw` (W:77-81) |
| CODE_CONST | PS | b2 row 7 | 103 | `.x` pixel scale (W:77), `.y` view mip bias for `sample_b` (W:107, 122, 130) |
| CODE_TEXTURE (1) | PS | t4 | 79 | screen mask, read only in thermal mode `cb7[13].x == 2` (W:87-91) |
| CODE_BUFFER (3) | PS | t0 | 40 | lightmap rectangles, read at `t0[cb9[0].y]` (W:485) |
| CODE_BUFFER | PS | t5 | 44 | 8x8 tile records; `.y >> 28` = tile class (W:846-848) |
| CODE_BUFFER | VS | t13 | 45 | vertex records |
| MATERIAL_TEXTURE (5) | PS | t1 / t2 / t3 | slots 0 / 9 / 59 | albedo / normal-gloss-cavity / tint mask |
| CODE_CBUFFER (8) | PS | b7 | 0 | per-view lighting constants |
| MATERIAL_CONST (9) | PS | b3 rows 1 / 2 / 3 / 0 | slots 0 / 9 / 54 / 72 | the material's 64-byte psData |

Not bound by any argument:

- `b9`. The world VS and PS read it; `.y` is the surface's lightmap-rectangle index. INFERRED: per-draw root
  constants.
- Every t41-t124 engine SRV. INFERRED: view-level descriptor tables.
- All samplers. INFERRED: static samplers. In use: s3 material, s5 sun visibility, s8 clamp (lightmaps, GTAO,
  lookups), s11 probes, s14 tint mask.

The constant layout (`precompiledIndex` MTL_CONSTANT_LAYOUT_WORLD_LIT_LMAP / SMODEL_LIT_PROBE) is INFERRED to select
the root signature. Keep it unchanged.

**Opaque model** (`tmo/`, same argument types) differs from the world as follows:

- Row 4 goes to both stages (stage mask 18).
- Code texture 79 → t3, and code buffer 44 → PS t4.
- VS buffers 31 / 29 / 30 → VS t0 / t1 / t2.
- Material textures go to **t0** / t1 / t2 (slots 0 / 9 / 59). The albedo is t0 (M:112).
- b7 goes to both stages (18). There is no b9.

## 5. The skeleton (docs/research/114_custom_ps_skeleton.hlsl)

The skeleton declares the stock input struct, `float4 main(...) : SV_TARGET`, and registers b2 / b3 / b7 / b9, s3 /
s5 / s8, and t0 / t1 / t5 / t85 / t114 / t115 (world). With `/D STATIC_MODEL_OPAQUE`, it uses the model inputs and
t0 / t4 / t85 with no b9. It computes:

- **Albedo.** `t1` (model: `t0`) through the psData UV transform (half floats in `cb3[1]`, W:96-106), sampled with
  `SampleBias(s3, uv, cb2[7].y)`, times the tint bytes of `cb3[2].x`.
- **Baked light (world).** The rectangle comes from `t0[cb9[0].y]` scaled by `cb7[212].y`. The layer is `w0 >> 26`.
  `t114` gives the colour (times `cb7[43].z`) and `t115` the hemi-octahedral direction and ambient fraction, giving
  `E = Ldir*sat(N.dW) + Lamb` (W:483-530) with the geometric normal.
- **Baked light (model).** From `AMBIENT_COLOR` / `HIGHLIGHT_DIR_VIS` (M:478-491). There is no `cb7[43].z` factor.
- **Sun.** `cb7[57].rgb * |t85(screenUV)| * sat(N.cb7[56].xyz)` when `cb7[56].w != 0`. It is zero for tile class 11
  when `cb7[83].w > 0` (W:837-852, M:726-739).
- **Output.** `min(float4(albedo*(E + sun)*cb7[42].x, 1), 32255)`: alpha 1 and the stock clamp. RT1 is not written.

Left out: the normal map, specular, probes, GTAO, local lights, decal volumes, the light-grid fallback
(`cb7[194].x == 1`) and the thermal / night-vision path (`cb7[13].x` = 1 or 2). In thermal view a surface with this
shader shows its normal colour.

## 6. Compiling, and the comparison with the stock blob (VERIFIED, run 2026-11-13)

```
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /nologo /T ps_5_0 /E main /O3 /Qstrip_reflect /Qstrip_debug /Fo world50.cso 114_custom_ps_skeleton.hlsl
... /D STATIC_MODEL_OPAQUE=1 /Fo model50.cso ...
```

The outputs are in `out/agent/ps114/`, with the checker `sig.py`.

| Check | Result |
|---|---|
| Container | DXBC, `ps_5_0`, chunks `ISGN OSGN SHEX`: the same chunk set as the stock blobs (without `/Qstrip_reflect` fxc adds `RDEF` and `STAT`) |
| World skeleton `ISGN` vs PS 01631f08 | identical (name, index, register, system value, format, mask) |
| World skeleton `OSGN` vs PS 01631f08 | identical |
| Stock VS d9ff726d `OSGN` → world skeleton `ISGN` | identical (the same holds for the stock pair) |
| Model skeleton vs PS bce5a6cd, and VS 546804181c → model skeleton | identical |
| `dcl_globalFlags` | `refactoringAllowed`, as stock. `/Ges` gives a byte-identical blob, because D3DCompile's `ENABLE_STRICTNESS`, which the converter uses, does not change it. |

Remaining differences, all allowed:

- **Used masks.** Only the "used" column differs, e.g. `SV_POSITION` used xy against stock xyzw. Usage is not part
  of linkage.
- **Fewer registers.**
  - The skeleton declares a subset of the stock registers: 3 of 5 samplers and 6 of 34 SRVs.
  - Its declared row counts can be smaller (`b3[3]` against `[4]`; model `b7[84]` against `[187]`). fxc sizes them
    by the highest row read.
  - Every declared register has the same dimension and stride as in the stock blob.
- **Size and code.** 3,364 bytes against 35,524, and 6 temps against 31.
- **`ps_5_1`** also compiles. Its signatures are identical (VERIFIED), but it encodes resources as ranges with
  `space=0`. Stock Replay draw shaders are all 5.0 (compute shaders are `cs_5_1`, doc 95), so use **5.0**.
  dxc / DXIL (`ps_6_x`) is a different container. Nothing in Replay's draw path is known to take it, so avoid it.

What the converter's in-game shaders add to this evidence:

- Its world surface PS `mw120r_iw3_surface_259f3da0...` (ACTS export of the cinderrail techsets zone) is 59,992
  bytes. It keeps `RDEF` and `STAT`, declares only a subset of the stock registers, and adds t9 (with its own
  material-texture argument).
- It declares t41 / t49 as `ByteAddressBuffer`, where the stock declares them structured.
- So the game accepts reflection chunks, any subset of registers, and extra material registers that come with
  matching arguments.

## 7. What can make the game reject or mis-draw a replaced shader

1. **A stale pipeline-state hash. This is the main trap.**
   - Every technique carries `pipelineStates[].hash`, and each zone ships a pipeline-state bundle `main/<zone>.psob`
     (magic `PSO`).
   - Measured: `techsets_mp_backlot2.psob` (46.8 MB, 1,661 embedded DXBC blobs) holds each backlot2 world technique
     hash exactly once.
   - `techsets_mp_shipment.psob` holds the prepass and shadow hashes of the same techset name, which use the same
     shaders, but **not** the lit hash, whose PS differs there. So the hash follows the shaders (INFERRED).
   - A clone that keeps a stock hash can be matched to the stock pipeline state and draw with the stock shader, or a
     clone can be matched to another clone. Give every changed technique a new hash. The converter does this in
     `PatchStateIdentity` (iw3_render_assets.cpp:3242): SHA-256 of seed + technique header + shader names, 8 bytes per
     state.
2. **Asset name collisions (INFERRED).** A shader, technique or techset that keeps a stock name can resolve to the
   already-loaded stock asset. Use new names, as the converter does: `mw120r_<label>_<sha256 of program>`, techset
   `tw/mw120r_<map>_<hash>`, and lit technique `TECHNIQUE_LIT_FORWARDPLUS_BITMASK_<...>`. Point the material's
   `techset` at the clone.
3. **The shader asset header (VERIFIED field, INFERRED meaning).** It is 40 bytes: `+32` is the program size and
   `+36` a 4-byte identity. The stock value is not a CRC or SHA of the program. The converter writes the first 4
   bytes of the program's SHA-256 (`Shader()`, iw3_render_assets.cpp:3224), and the game loads it. So no name or
   bytecode hash is checked.
4. **The DXBC checksum (INFERRED, D3D12 runtime).** D3D12 refuses a DXBC blob whose checksum (bytes 4-19) does not
   match. Never patch bytes after fxc; recompile instead.
5. **Signature mismatch (INFERRED, D3D12 runtime).** If the PS input signature does not link to the VS output (a
   semantic, register or mask that differs, or an input the VS does not write), pipeline creation fails. Keep the
   section 3 structs exactly. Never add interpolants without also replacing the VS, as the converter does.
6. **Registers outside the root signature (INFERRED, D3D12 runtime).**
   - The blob has no root signature, so the engine's applies. A PS that declares a register the root signature does
     not cover fails pipeline creation.
   - Safe: the stock set in section 3, plus the extra material registers the converter adds with their own arguments.
     The cinderrail build uses t9 (world) and t6 / t9 (models); converter options add t7-t13. The converter source
     also declares t110 / t111 under `MAP_VOLUMETRIC_EXTINCTION`, but that use is not confirmed in game.
   - Declaring a register with another dimension than the engine binds (Texture2D on a 2D-array slot, for example)
     gives undefined results. Keep the stock dimensions.
7. **Extra material textures (VERIFIED by the converter).** An extra register (t6-t13) is bound only when a matching
   argument exists. Add a `MATERIAL_TEXTURE` argument (`05 10 <reg> 00 <slot> 00`, kept sorted) and bump the technique's
   argument count, header byte `0x7A` (`AppendTextureArgument`, iw3_render_assets.cpp:3348-3376).
8. **Render targets (VERIFIED and INFERRED).**
   - Write `SV_TARGET0` only. RT1 belongs to the prepass normal buffer: a write there would corrupt GTAO and SSR,
     and the RT holds integer data.
   - Do not output `SV_Depth`, and do not `discard` in an opaque technique. The lit pass relies on the prepass depth
     (section 2).
   - Keep alpha at 1 and keep the 32255 clamp, so the radiance stays in the range the post chain expects
     (doc 95 section 3).
9. **Pre-exposure.** Multiply the colour by `cb7[42].x`, or the surface turns black or white as the exposure adapts
   (doc 95).

## Not checked

- No skeleton-based techset has been built or drawn in game. Section 6 proves interface compatibility offline only.
- The meaning of `colorRtFormats` 129 / 1, the 5 state-bit bytes, and the exact record layout of `.psob` are not
  decoded.
- Whether the engine actually reuses a `.psob` record for a custom techset that kept a stock hash is not tested. The
  converter's behaviour suggests it is avoided on purpose.
