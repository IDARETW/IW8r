# iwmap 4 (.map) text format: observed grammar

Scope: CoD4 Radiant map files in `E:\CoD4\game_build\Call of Duty 4 Modern Warfare\map_source\`.
Method: read-only survey with Python (`-I`) over the 8 top-level samples and the full tree.
The full tree is 2,182 `.map` files: 8 top-level, 2,169 under `prefabs\`, and 5 under `snapshots\`
(`mp_test.map.0` to `.4`). `prefabs\shadercache2\` holds 20 non-map binaries and was excluded.
Every one of the 2,182 files passes a single line grammar (section 8) with zero unrecognized lines.
Counts below are from that validation run unless a sample is named.

## 0. Grammar at a glance (EBNF, whitespace-insensitive unless noted)

```
file       := "iwmap 4" NL  layerdecl*  entity+
layerdecl  := '"' NAME '"' " flags" FLAGTAIL NL          ; FLAGTAIL = see section 1
entity     := [ "// entity " INT NL ]  "{" NL  entitem*  "}" NL
entitem    := layerdir | kv | brush                     ; layerdir only before any kv/brush
kv         := '"' KEY '"' ' "' VALUE '"' NL              ; VALUE: no quotes, no escapes
brush      := [ "// brush " INT NL ]  "{" NL  brushitem*  "}" NL
brushitem  := layerdir | contents | toolflags | face+ | patch   ; directives before faces
face       := "( x y z ) ( x y z ) ( x y z ) " MAT " " N6 " " LMMAT " " N6 [ " smoothing " NAME ] NL
patch      := ("mesh" | "curve") NL "{" NL  pitem*  "}" NL      ; patch is the sole body of its brush
pitem      := layerdir | contents | toolflags | MAT NL LMMAT NL [ "smoothing " NAME NL ] DIMS NL  group+
group      := "(" NL  vrow+  ")" NL
vrow       := TAB "v " x y z [ " c" r g b a ] " t" t1 t2 t3 t4 [ " f " 1 ] NL
DIMS       := W " " H " " A " " B                          ; four integers
```

Block type is decided by nesting depth, not by comments. Depth 1 inside the file is an entity. Depth 2
inside an entity is a brush. `mesh` or `curve` followed by `{` opens a patch. The `// entity N` and
`// brush N` comments are optional (see section 2).

## 1. Header and layer lines

**Header.** Line 1 is `iwmap 4` in all 2,182 files. No BOM. No other version string occurs.

**Layer declarations.** These are one line each, after the header and before the first entity. The 8
samples each declare `"000_Global"` and `"The Map"`. The forms seen (corpus counts, 4,387 lines in total,
including the 5 snapshot copies):

| form (exact bytes after the closing quote) | count | example |
|---|---|---|
| ` flags` + two spaces + `active` | 2,038 | `"000_Global" flags  active` |
| ` flags` + one trailing space (no flag) | 2,345 | `"The Map" flags ` |
| ` flags` + one space + `active` | 1 | `"000_Global" flags active` (`mp_test_mw2019.map`) |
| ` flags` with nothing after | 1 | `"The Map" flags` (`mp_test_mw2019.map`) |
| ` flags prefab expanded` + trailing space | 2 | `"blackout_geo.map" flags prefab expanded ` (`blackout.map`) |

So the separator is a single space before `flags`. The flag words follow with their own leading space, and
the spacing is not normalized by the tool that wrote it. Preserve the bytes. Layer names may contain
spaces and `/` (for example `"000_Global/sky"`, `"high town"`).

**Layer directive inside a block.** A line `layer "NAME"` assigns the element that owns it to a layer.
It appears only as the first line after an opening `{`. For an entity it sits before the key/value lines.
For a brush or patch it sits before the body. Corpus (prefab and top-level files): 26,043 directives. They occur only in
`blackout_geo.map` among the 8 samples (18,796 lines there). Layer names seen: `"high town"`, `"low town"`,
`"water"`, `"stream"`, `"000_Global/sky"`, `"glow"`. Example:

```
  mesh
  {
layer "low town"
   ch_grass_01
```

## 2. Entity block

**Entity header.** `// entity N` precedes the `{`. N counts from 0 and is sequential across the file.
Entity 0 is `worldspawn` in all 2,182 files. Files written without comments exist: `mp_test_mw2019.map` has
no `// entity` lines at all (17 entities), and its entities start directly with `{`.

**Key/value lines.** Each line is `"KEY" "VALUE"`, one pair per line, a single space between the two
quoted strings. Escaping rules, checked corpus-wide:

- No value contains an embedded `"`. The regex `^"[^"]*" "[^"]*"$` matches all 185,664 kv lines.
- Backslashes are not escaped. 496 lines contain a backslash, and all of them are
  `"destroyEfx" "props\\wallExp_concrete.efx"` (two literal backslash characters) in 18 files.
  Store and rewrite the raw text, do not decode or re-escape it.
- No value is empty. Zero `""` values appear.
- 237 values end with a space (for example `"script_color_allies" "r3 r13 "`). Four values start with a
  space. Eight contain a double space. Preserve them.
- Keys are not in a fixed order. `classname` can come first, in the middle, or last.
- Numbers are plain decimal text, sometimes with a leading dot (`".25"`) or trailing zeros (`"1.000000"`).
  Origins and angles are space-separated floats such as `"-10818.0 -6546.9 117.9"`.

Top keys across the 8 samples: `classname`, `origin`, `angles`, `model`, `modelscale`, `targetname`,
`spawnflags`, `radius`, `_color`, `target`, `script_*` (many), `export`, `def`, `intensity`, `type`.
Ninety-six distinct keys occur. Worldspawn keys seen include `contrastGain`, `sunRadiosity`, `sunlight`,
`sundirection`, `sundiffusecolor`, `suncolor`, `diffusefraction`, `ambient`, `_color`, `classname`.

Example (entity 0 of `test.map`, first lines):

```
// entity 0
{
"contrastGain" ".25"
"sunRadiosity" "1"
"sunlight" ".6"
```

**Brush comments.** `// brush N` appears before a brush's `{`. N restarts at 0 for each entity. It is not
global. Brushes without a comment also occur: `mp_test_mw2019.map` has 14 brushes but only 12 comments
(the two clip-material brushes after entity 0 have none). A parser must not rely on the comment.

**Ordering.** Within an entity, all kv lines (and the optional layer line) come first, then brushes.
Corpus check: no kv line appears after a brush inside an entity, and no layer line appears after a kv or
brush line in an entity. Brushes are the only children of an entity besides kv and layer lines.

## 3. Brush block

**Brush shape.** `{` then optional directives, then one or more face lines (or exactly one patch), then `}`.
Corpus: 67,595 brush blocks, of which 16,614 contain a patch and 50,981 are face-only brushes.

**Face line token layout.** Every face line has this structure (validated on 309,890 faces):

| tokens | meaning / observed range |
|---|---|
| `(` `x` `y` `z` `)` | plane point 1. Coordinates are decimal, e.g. `-263.5` or `-14933.823` |
| `(` `x` `y` `z` `)` | plane point 2 |
| `(` `x` `y` `z` `)` | plane point 3 |
| `MAT` | texture/material name. 81 distinct in the 8 samples (`caulk`, `clip`, `sky_chechnya`, `ch_tile_floor05`, ...) |
| `N1` `N2` `N3` `N4` `N5` `N6` | six numbers after the material (see below) |
| `LMMAT` | lightmap material name. `lightmap_gray` in the great majority of faces. Also `lightmap_blue`, `lightmap_cyan`, `lightmap_red`, `lightmap_green` in a few dozen faces |
| `L1` `L2` `L3` `L4` `L5` `L6` | six numbers after the lightmap material |
| `smoothing NAME` | optional, only on some faces (32 in `blackout_geo.map`, 21 `smoothing_smooth_other` plus 12 `smoothing_smooth` in the samples) |

The face count of numeric tokens is always 6 after the material and 6 after the lightmap material. The
validator matched no face with any other count. A face line has 9 + 6 + 6 = 21 numbers in total, plus the
optional smoothing tail.

**Numeric fields (interpretation is inferred from value distributions, not from a spec).**

| field | observed values | inferred meaning |
|---|---|---|
| N1 | `64` (most), `128`, `256`, `32`, and fractional values like `45.412487` | texture scale X (or texel size) |
| N2 | same family as N1, also negative: `-64`, `-128` | texture scale Y. Sign carries orientation |
| N3 | wide range, e.g. `0`, `-18`, `3072`, `-963`, `-6372.8945` | texture offset X (shift) |
| N4 | wide range, e.g. `0`, `-1024`, `1544`, `-434` | texture offset Y (shift) |
| N5 | `0` (most), `180`, `90`, `-90`, `-180`, `94.25`, `20.5` | rotation in degrees |
| N6 | `0` (almost always), tiny floats such as `-1.9788817e-005` in a few hundred faces | unknown. Always write it back |
| L1 | `16384` (most), `16384.5`, `16383.982`, `1024` | lightmap scale X |
| L2 | `16384`, `16383.939`, `16135.086`, `1024` | lightmap scale Y |
| L3 | `0` in every face | unknown |
| L4 | `0` in every face | unknown |
| L5 | `0`, `10`, `-10`, `-14`, `30`, `0.0093231201` | lightmap rotation or offset (inferred) |
| L6 | `0` (almost always), tiny floats like `1.1444266e-005` | unknown |

**Number formats.** Integers and decimals both occur. Exponents appear only with negative sign and three
exponent digits, as in `-9.6881202e-005`, `1.5258789e-005`, `d.dddde-ddd`. No `e+` forms were seen, and no
exponent appears with a two-digit exponent. Round-trip these tokens as text, not as parsed floats,
because the textual form (`16384.5`, `1.000000`, `.25`) is what the file contains.

**Example face (no smoothing tail):**

```
 ( -320 304 128 ) ( -320 320 128 ) ( 320 320 128 ) ch_tile_floor05 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0
```

**Example face (with smoothing tail, `blackout_geo.map`):**

```
( -10599 -7163 170 ) ( -10599 -7159 170 ) ( -10599 -7163 202 ) ch_concretewall02 128 -128 -6372.8945 -434 180 0 lightmap_gray 16384 16384 0 0 0 0 smoothing smoothing_smooth_other
```

**Indentation.** Faces are unindented or have one leading space. Brush `{` and `}` have one leading
space in the large files. Parsers must ignore leading whitespace.

**Brush-level directives (before the first face or the patch body):**

| line | observed forms (counts over prefab and top-level files) |
|---|---|
| `contents X;` | `contents detail;` (35,606), `contents weaponClip;` (3,727), `contents nonColliding;` (1,444) |
| `toolFlags X;` | `toolFlags splitGeo;` (926) |
| `layer "NAME"` | see section 1 |

The directive is always on its own line, ends with a semicolon, and has one space after the keyword.
Example from a brush body (the face line is cut short with `...`):

```
// brush 1
{
  contents detail;
 ( -264 656 64 ) ( -712 656 64 ) ( -712 640 64 ) me_brick_01 64 64 1288 1024 0 0 lightmap_gray ...
```

Corpus check: no directive line appears after a face line inside a brush.

## 4. Patch blocks (`mesh` and `curve`)

**Keyword line.** The line before the `{` is exactly `mesh` or `curve`. The keyword is not wrapped in a
brush-style comment. No `terrain` keyword and no `patchDef` keyword appear in any file.

**Body, in order** (all corpus-validated):

1. Optional directives, in any of the observed orders: `layer "NAME"`, then `toolFlags X;` or
   `contents X;`. They always come before the material line.
2. Material name line, for example `me_cinderblock_wall2`.
3. Lightmap material line, always `lightmap_gray` in the corpus, with no numbers on it.
4. Optional `smoothing NAME` line (65 in `mp_backlot_geo`, 3 in `blackout`, 353 in `blackout_geo`).
   It sits between the lightmap line and the dimension line.
5. Dimension line `W H A B`: four integers, nothing else on the line.
6. Groups of `(` ... `)`, one group per column.

**Dimension line.** The first integer `W` is the number of `( )` groups (columns). The second integer `H` is
the number of `v` rows in each group. This was checked on the 1,759 mesh and 145 curve patches with a non-square grid in the three large samples
(`mp_backlot_geo`, `blackout`, `blackout_geo`). A and B are integers with no documented meaning. In those
mesh patches B is `8` in every case, and A takes values such as `0`, `2`, `16`, `64`, `128`, `256`, `512`,
`1024`. In curve patches B is `1`, `2`, `3`, `4`, `5`, `8`, and A is `0` or `16`. Preserve A and B as written.

**Vertex row.** Each row is one line, indented with exactly one TAB character, in the form

```
<TAB>v X Y Z [c R G B A] t T1 T2 T3 T4 [f F]
```

- `v X Y Z`: position, 3 floats. Always present.
- `c R G B A`: 4 integers (0 to 255 in all rows). Optional. Interpreted as colour, inferred (`255 255 255 0`,
  `57 57 57 255`).
- `t T1 T2 T3 T4`: 4 numbers (ints or floats, and some with e-005 style exponent). Always present.
  Inferred as two texture-coordinate pairs.
- `f F`: 1 integer. Optional. The value is `1` in all 16,825 rows where it appears. The tag comes after `t`.

Tag order is fixed: `v`, then `c` (optional), then `t`, then `f` (optional). Tag-sequence counts in the three
large samples: `v t` 36,703; `v c t` 11,930; `v t f` 3,052; `v c t f` 1,035. The corpus-wide vertex row count
is 205,718, and every row matches the tag order above.

Example mesh body (`mp_backlot_geo.map`), with the vertex row shown separately:

```
  mesh
  {
   me_cinderblock_wall2
   lightmap_gray
   6 3 16 8
   (
```

```
	v -263.5 -1007.5 137.5 c 255 255 255 0 t 0 0 -62.96875 -8.59375
```

Curve example (no `c`, no `f`):

```
	v -385 -1711 392 t 0 0 -106.9375 -24.5
```

Vertex row with `f` (`blackout_geo.map`):

```
	v -256 -832 137 c 255 255 255 0 t -8192 106496 -16 52 f 1
```

**Groups.** Each group is `(` on its own line, the `v` rows, then `)` on its own line. Inside a block the
group lines are indented by three spaces, and the vertex rows by one TAB. Groups are not separated by any
other token.

**Patch as brush body.** A patch brush contains only the patch block (`{` `mesh` `{` ... `}` `}`). Its
enclosing brush has no faces.

## 5. Prefab references, models, groups and other entities

- **misc_prefab.** Entity with `"classname" "misc_prefab"` and `"model" "PATH.map"`. PATH is relative to
  `map_source\`, with forward slashes, for example `"model" "prefabs/misc_models/com_bottle1.map"`. The 8
  samples hold 1,737 such entities. In `blackout_geo.map`, 876 of 889 references resolve to a file in the
  tree. The 13 that do not include `prefabs/bog/general/pickup_no_doors.map` and
  `prefabs/village_assault/misc/shed_piece_plank2.map`.

  ```
  "model" "prefabs/misc_models/com_bottle1.map"
  "classname" "misc_prefab"
  ```

- **misc_model.** Entity with `"classname" "misc_model"` and `"model"` set to a bare asset name with no
  extension, for example `foliage_shrub_brownmid`, `body_complete_sp_spetsnaz`, `com_cardboardbox05`.
  The 8 samples hold 23,215 such entities. Many also carry `modelscale` (for example `"1.5"`).
- **func_group.** Entity with `"classname" "func_group"`. The 8 samples hold 115 entities. Some carry
  `"type" "patchCapped"` and contain patch blocks. Example: `"type" "patchCapped"` then `"classname" "func_group"`.
- **Other classnames** seen in the samples: `node_pathnode`, `script_origin`, `trigger_multiple`,
  `node_guard`, `light`, `reflection_probe`, `info_null`, `dyn_model`, `script_brushmodel`, `script_vehicle`,
  `actor_*`, `mp_*_spawn*`, `info_volume`, `script_struct`. None needs special parsing; they are kv entities.
- **Groups in the sense of layers.** Use the `layer "NAME"` directive and the layer declarations (section 1),
  not entities, to group elements.
- **Prefab files themselves** are ordinary `iwmap 4` files. The `prefabs\` tree does not use a different
  grammar.
- **Not seen in any file:** `terrain`, `patchDef`, `brushDef`, `"model"` on brushes, `group`-type comments,
  `//` comments other than `// entity N` and `// brush N`, and blank lines.

## 6. Count table

Corpus total for reference: 2,182 files, 42,419 entity blocks, 67,595 brush blocks, 309,890 faces,
16,614 patches, 205,718 vertex rows, 185,664 kv lines. Per-file counts for the 8 top-level samples:

| construct | mp_test | mp_test_mw2019 | test | test_traverses | mp_backlot | mp_backlot_geo | blackout | blackout_geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| layer declarations (`"x" flags`) | 2 | 2 | 2 | 2 | 2 | 2 | 4 | 8 |
| layer directives (`layer "x"`) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 18,796 |
| entity blocks (`{` at depth 1) | 44 | 17 | 29 | 116 | 68 | 3,424 | 3,378 | 22,098 |
| `// entity N` comments | 44 | 0 | 29 | 116 | 68 | 3,424 | 3,378 | 22,098 |
| kv lines (all depths) | 144 | 60 | 73 | 348 | 428 | 15,391 | 10,948 | 110,928 |
| brush blocks (incl. patch brushes) | 11 | 14 | 12 | 33 | 0 | 1,357 | 320 | 2,796 |
| `// brush N` comments | 11 | 12 | 12 | 33 | 0 | 1,357 | 320 | 2,796 |
| face lines | 66 | 84 | 72 | 196 | 0 | 4,515 | 1,940 | 4,468 |
| faces with trailing `smoothing` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 32 |
| `mesh` patches | 0 | 0 | 0 | 0 | 0 | 462 | 4 | 1,884 |
| `curve` patches | 0 | 0 | 0 | 0 | 0 | 158 | 0 | 192 |
| patch `( )` groups | 0 | 0 | 0 | 0 | 0 | 2,176 | 18 | 9,512 |
| patch vertex rows (`v`) | 0 | 0 | 0 | 0 | 0 | 9,481 | 36 | 43,203 |
| `contents X;` directives | 0 | 0 | 0 | 0 | 0 | 499 | 2 | 426 |
| `toolFlags X;` directives | 0 | 0 | 0 | 0 | 0 | 23 | 0 | 89 |
| `smoothing X` in patches | 0 | 0 | 0 | 0 | 0 | 65 | 3 | 353 |
| `misc_prefab` entities | 4 | 0 | 0 | 13 | 19 | 759 | 53 | 889 |
| `misc_model` entities | 24 | 2 | 0 | 0 | 0 | 2,222 | 3 | 20,964 |
| `func_group` entities | 0 | 0 | 0 | 0 | 0 | 78 | 0 | 37 |

Note on `mp_test_mw2019`: it has 17 entities and 14 brushes but only 12 `// brush` comments, which is the
comment-less case described in section 2.

## 7. Edge cases: whitespace, line endings, file ending

- **Line endings.** 2,175 files use CRLF on every line. 7 files use LF only (no CR at all):
  `mp_test.map` (the top-level sample), `snapshots\mp_test.map.0` through `.4`, and
  `prefabs\_iw3xo\dynamic_collision_bmodels.map`. `mp_test_mw2019.map`, `test.map`, `test_traverses.map`,
  `mp_backlot.map`, `mp_backlot_geo.map`, `blackout.map`, and `blackout_geo.map` are CRLF. No file mixes CR
  and LF. No stray CR appears anywhere. A rewriter should keep each file's own line-ending style.
- **File ending.** Every file ends with `}` followed by a newline (`}\r\n` or `}\n`). No file has a missing
  final newline.
- **BOM.** None. All bytes are ASCII (no byte above 0x7F in any file).
- **Blank lines.** None. No file has an empty line. A rewriter must not add any.
- **Tabs.** Used in exactly one place: one leading TAB on each patch vertex row (`v` line). The tab-led line
  count equals the vertex row count in every sample (for example 9,481 in `mp_backlot_geo.map`). Nothing
  else is tab-indented.
- **Other indentation.** Spaces only, and varying by writer: 0 to 3 spaces before brush braces, faces, and
  patch keywords. Patch groups use 3 spaces. Indentation is cosmetic. Parse with `strip()`.
- **Trailing whitespace.** Present only on layer declaration lines (for example `"The Map" flags ` has a
  trailing space, and `"blackout_geo.map" flags prefab expanded ` too). Corpus: 2,347 such lines, all layer
  declarations. Keep it.
- **Duplicate spaces.** Single space between tokens everywhere, except inside the layer declaration
  (`flags  active`) and eight kv values.

## 8. Notes for a lossless parser and writer

1. Read bytes, split on LF, strip one trailing CR per line, and remember whether the file is CRLF or LF.
   Write back the same ending.
2. Store each token as its original text (for example `16384.5`, `.25`, `1.000000`,
   `-9.6881202e-005`). Do not round-trip numbers through float formatting.
3. Keep kv values as raw strings. Do not unescape `\\`, and keep trailing spaces.
4. Keep the optional `// entity N` and `// brush N` comments as-is, and recompute nothing unless the editor
   deliberately renumbers. Brush numbers restart per entity.
5. Keep unknown numeric fields (face N6, L3 to L6, patch A and B, vertex `f`) verbatim.
6. Patches are a separate block kind inside a brush. Don't flatten them into faces.
7. Use the depth stack, not comments, to find entity and brush boundaries (two brushes in `mp_test_mw2019.map`
   have no comment).

The grammar was checked by a single validator over all 2,182 files, with zero unrecognized lines. The
inferred meanings in sections 3 and 4 are labelled as inferred. They come from value distributions and
should be confirmed against the compiler source before the editor relies on them semantically.
