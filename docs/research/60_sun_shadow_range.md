# 60. Sun shadow range, per-placement sun-shadow off, and the hot-sun crash (2026-11-10)

Read-only research. Nothing was edited, built or run. The fork (`E:\mw124\x64-zt`) and the stock GSC dump were only read.
Every claim cites file:line. Items marked **UNVERIFIED** were not confirmed by running the game or by reading Replay
(the game binary is not in the repo).

## 0. State changes found while checking (read first)

- **The sun channel is already removed from the editor.** The brief describes `iw8r_hot_sun` calling
  `lerpsunangles` and `setsuncolorandintensity`. That code is gone. `src/core/hot_channels.cpp:166` now says
  "No sun channel: lerpsunangles / setsuncolorandintensity from a map script crashed Replay MP, owner test 2026-11-10."
  `tests/hot_channels_tests.cpp:208` asserts `iw8r_sun` is absent and `:214` asserts neither builtin appears in the GSC.
  `hotSunFromKeys` (`hot_channels.cpp:258`) and the sun encode/decode (`:102-107`, `:145-150`) still exist but are not
  sent to any GSC. The file (mtime 19:56) changed after the first grep in this task, so the brief's line numbers were stale.
- **Research 54 has one wrong claim.** It says the sun-shadow bake "takes world casters only" (section 4, edit 5
  context). The fork adds every static-model instance as a sun caster (`iw3_fastfile.cpp:5324-5347`, see section 2.3).
  A per-placement sun-off therefore needs one more gate than research 54 lists.
- **Editor line numbers moved since research 39 and 54.** Current references:
  worldspawn writer `src/core/compile_iw8.cpp:1616-1619` (was 1333-1337); misc_model reader `:1518-1525`
  (was 1442-1485); placement writer `:1837-1846` (was 1791-1798); `shadow_lights: []` write `:1843`.
  The fork references in research 54 (`iw3_fastfile.cpp:194-206`, `3788-3789`, `5247-5254`, `replay_render.h:247-259`,
  `replay_render.cpp:2350`) were re-checked and are correct.

## 1. Shadow distance

### 1.1 What stock maps use

Source: research 39 (`docs/research/39_sun_shadows.md:22-24`), read from the stock `mp_shipment` MapEnts dump in the
cache. That dump is hex-encoded; the values were **not re-decoded** for this note.

- `csm_enable` = `1`, `csm_max_resolution` = `256`, `csm_sample_size` = `2` on the stock worldspawn.
- No `_csm_bound_*` keys on `mp_shipment` (research 39, line 46).

### 1.2 Where the range comes from in Replay (what the converter writes)

The fork has no code that reads a worldspawn key to set the cascade range. `grep` for `csm_` in
`src/convert/iw3_iw8` and `src/games/iw8` finds only the GSC token `csm_alg`
(`src/games/iw8/assets/gsc_tool_iw8_tokens.inc:2745`, per research 39) and an unrelated opaque script
(`tools/dvar_search_web/.../opaque/58229.gsc:931`). Three range sources exist:

**(a) Native sun cascades: runtime dvars, not data.** The dvars are decoded in
`E:\mw124\tools\dvar_search_web\dvar_comments.txt` and described in `E:\mw124\Dvars.txt` (names in Dvars.txt are the
hashed form; the readable name is in the comment):

| Readable name (hash) | Dvars.txt line | Description (verbatim, short) |
|---|---|---|
| `sm_sunCascadeSizeMultiplier1` (LSNRQTOKRR) | 5772 | Far cascade size multiplier: 2^n times near cascade size |
| `sm_sunCascadeSizeMultiplier2` (NTLKNLNPLK) | 5773 | Distant cascade size multiplier: 2^n times far cascade size |
| `sm_sunDistantShadows` (TMNTMTQRM) | 5775 | Enable 3rd shadow cascade for flagged objects |
| `sm_sunFarShadows` (SONQQSQRQ) | 5778 | Enable 2nd shadow cascade for flagged objects |
| `sm_sunSampleSizeNear` (NPONLLLSPL) | 5781 | Shadow sample size |
| `sm_sunEnable` (MQRQQONQSL) | 5777 | Enable sun shadow mapping from script |
| `sm_sunClip` (LTPMKRMKKQ) | 5774 | Enable sun shadow pancakes |
| `cg_playersmp_CoarseCullSystem_SunShadowDist` (RLPNQQRPR) | 1025 | Distance beyond which we don't check for sun shadows in the coarse cull system |
| `r_volumetricDepth` (MPOKKOPMTN) | 5436 | Volumetric cascade depth ranges [nearest, near, far, distant] |
| `r_compressedSunShadowClipPlanes` (LQLMTQMMKQ) | 4284 | Use compressed sun shadow to compute sun shadow clip planes |

Dvars.txt gives no default values, so every default is **UNVERIFIED**.

**(b) Compressed sun-shadow bake (the converter writes it).** `src/convert/iw3_iw8/convert.cpp:1600-1661`:
- Budget 96 MiB (`:1600`). The bake uses `renderMesh.sceneBounds` as the receiver bounds (`:1606-1608`).
- Sample size: 1.0 first (`:1626`), then 2.0 (`:1628`). The Replay bake accepts only 1 or 2
  (`src/games/iw8/common/replay_sunshadow.cpp:1105-1106`).
- A play-space crop is tried only when `compassBounds` exists (`:1634`). Margins are 4096, 2048, 1024, then 0
  (`:1631-1632`). An editor map with no compass gets no crop.
- Written parameters (`replay_sunshadow.cpp:1120-1127`): `sunDirection`, `resolution`, `sampleSize`, `centerX/Y`,
  `forestSize`, `reserved`. **`nearPlane` and `farPlane` are never written** (struct defaults `0`,
  `replay_sunshadow.h:43-49`). What Replay does with a 0 far plane is **UNVERIFIED**.

**(c) Static-world baked sun depth (converter writes it).** `src/convert/iw3_iw8/iw3_fastfile.cpp:12106-12149`:
- Region: the compass box plus a 512 margin (`:12109-12116`) if `iw3CompassBounds(entities, ...)` succeeds, else
  X/Y = ±16384 (`:12108`). Z is limited to |z| ≤ 100000 (`:12123`).
- Compass parsing (`src/games/iw8/dump/maps.cpp:366-420`) needs exactly two `script_origin` entities with
  `targetname minimap_corner` (`maps.cpp:398-412`). The editor's minimap corners are read at
  `src/core/compile_iw8.cpp:1709-1722`. Whether the editor writes them into the `.ents` text is **UNVERIFIED**.
- Image: `replaysunshadow::BakeDepthImage` (`replay_sunshadow.h`, `DepthImageWidth = 4096`, constants after line 110). Texel size
  is region / 4096 (**UNVERIFIED**; the exact choice in `BakeDepthImage` was not read).
- The fork's own comment (`iw3_fastfile.cpp:12106-12107`) says native cascades "drop casters outside their range and
  view; this does not." The HLSL comment (`resources/map_surface_realtime.hlsl:1370-1374`) says static casters use the
  baked depth "at any distance", while moving casters (players, script models, dynamic props) use the native cascade.

**(d) The editor writes none of the range keys.** The worldspawn line is hand-written: only `classname`,
`sundirection` and optionally `suncolor` (`compile_iw8.cpp:1616-1619`). `src/core/sun_keys.h:6` says `csm_*` keys are
not passed through. Research 39's claim that `csm_*` is dropped still holds (`checkSunKeys`, `sun_keys.cpp:116`).

### 1.3 What the stock scripts do with the range

Stock SP scripts set the cascade dvars during lighting changes. The MP scripts do not. The MP grep found no hits for any
of the hashed names above (`tools/dvar_search_web/src/combined/rawfile_source/scripts/mp`, all sun-related names).
Evidence:

- `sp/maps/highway/highway_lighting.gsc:21` sets `TMNTMTQRM` (sm_sunDistantShadows) to 1.
- `:22` sets `MPOKKOPMTN` (r_volumetricDepth) to `"128 384 640 1024"`.
- `:25` sets `LSNRQTOKRR` (sm_sunCascadeSizeMultiplier1) to 3 and `:27` sets `NTLKNLNPLK` (multiplier2) to 2.
  `:67-68` restore the saved values at ride end.
- `sp/maps/captive/captive_lighting.gsc:149-150, 156-157, 163-164` set the pair to (2,1) and `:170-171` to (2,2).
- `sp/maps/estate/estate_lighting.gsc:25-26` sets the pair to (2,2). Other SP lighting scripts follow the same pattern.
  The MP scripts do not set these names (section 1.3 grep).

The IW7 Breakneck main sets multiplier 1 to 3 and multiplier 2 to 2 (`src/convert/iw3_iw8/resources/iw3_compat.gsc:216-217`,
comment quoting `mp_breakneck.gsc:14-22`). The converter's `iw3compat_setdvar` shim passes exactly
`sm_sunCascadeSizeMultiplier1/2` through to the game (`iw3_compat.gsc:211-221`). The shim comment says Replay
"registers under the same names". That is a claim about Replay that was not checked.

### 1.4 Proposed patches (choose A first; B only if the static range is too short)

**Option A: no fork change (try first).** The editor's generated map script sets the dvars at map start, the way the SP
scripts do. Values to test: multiplier1 = 3, multiplier2 = 2 (Breakneck and highway values), `sm_sunDistantShadows` = 1.
Lowest risk, since no builtin is involved. Each `sm_` value's effect in game is **UNVERIFIED**.

```gsc
// in the editor-generated map script's main(), before the first wait
setsaveddvar( "sm_sunCascadeSizeMultiplier1", 3 );  // readable form as in iw3_compat.gsc:219; stock uses hashed LSNRQTOKRR
setsaveddvar( "sm_sunCascadeSizeMultiplier2", 2 );
setsaveddvar( "sm_sunDistantShadows", 1 );
```

Open question: stock scripts pass the hashed names (`highway_lighting.gsc:25`). The converter shim passes readable names
(`iw3_compat.gsc:219`). Which form the engine accepts in `setsaveddvar` on an editor map is **UNVERIFIED**; test both on
a throwaway map.

**Option A2: the coarse-cull distance (candidate for the symptom "shadows cut off too short from the player").**
`cg_playersmp_CoarseCullSystem_SunShadowDist` (RLPNQQRPR, Dvars.txt:1025) is named as the distance beyond which sun
shadows are not checked for the player-side coarse cull. That matches the owner's wording. No stock MP script sets it
(the MP grep above). Its default and its effect are **UNVERIFIED**. Test: `setsaveddvar( "RLPNQQRPR", <large value> )` in
the same place as option A. Do not ship without an in-game check.

**Option B: a worldspawn key for the static sun-depth region (fork change, small).** Only useful if the static world
shadow stops before the map edge (the ±16384 default, or the compass box plus 512). Add after
`src/convert/iw3_iw8/iw3_fastfile.cpp:12116` (the closing brace of the compass block), inside the block starting at
`:12106`:

```cpp
// Editor override: worldspawn iw8r_sun_bake_range "R" (world units) sets the static sun-depth region to +-R.
if (const std::string range = EntityValue(entities, "iw8r_sun_bake_range"); !range.empty())
{
    const float r = ParseEntityFloat(range, 0.0f);
    if (r >= 1.0f && r <= 100000.0f)
        minX = -r, maxX = r, minY = -r, maxY = r;
}
```

- `EntityValue` (`iw3_fastfile.cpp:8140-8153`) returns an empty string when the key is absent, and `ParseEntityFloat`
  (`:8169`) takes a fallback, so this compiles against the existing helpers and does nothing when the key is absent.
- Editor side: `compile_iw8.cpp:1616-1619` must write `{"iw8r_sun_bake_range", value}` when the user sets it.
- Trade-off: the image stays 4096 texels, so a larger R coarsens the texels (UNVERIFIED how much).
- This does NOT change the compressed bake (`convert.cpp:1606-1608` still uses `sceneBounds`), nor the native cascades.

**Not recommended: writing `nearPlane` / `farPlane`** (`replay_sunshadow.h:48`, `structs.hpp:24035-24036`). The bake
does not compute depth in those units (`replay_sunshadow.cpp:1120-1127`), and what Replay reads from them is UNVERIFIED.

### 1.5 Ranking

1. Option A (dvars in the map script), with the readable and hashed names both tested.
2. Option A2 (`RLPNQQRPR`) if the cut-off is close to the player rather than at the far edge.
3. Option B only if the static world shadow itself ends at the region edge (test first by walking the map far from the origin).

## 2. Per-placement "no sun shadow" (corrected fork patch set)

### 2.1 Current state (verified)

- The editor writes `"shadow_lights":[]` when `no_shadow` is set (`compile_iw8.cpp:1843`), and the converter reads it
  (`iw3_fastfile.cpp:3788-3789`). That removes local-light shadows only. Sun shadows are still on, as the owner saw.
- `smodel_flags.h:27` documents `noShadow` as "no sun shadow and no local-light shadow", but only the local part is
  emitted. The sun part is not written anywhere.

### 2.2 Where the sun shadow is decided (verified)

Two independent sun-shadow paths exist for a placed model:

1. **Native cascade** (dynamic-ish): one byte per placement. `src/games/iw8/common/replay_render.cpp:2340-2350`:
   `sunShadowMask` is computed once, then `collection[0x0E] = sunShadowMask;` for every placement (`:2350`).
2. **Baked sun depth** (static): `BuildShadowScene` adds each static-model instance as a caster
   (`iw3_fastfile.cpp:5324-5347`, loop over `staticModels.instances`, transform by `instance->origin/axis/scale`).
   That scene is `result.shadowScene` (`:12100`), used for the baked depth image (`:12136-12137`) and for the
   compressed bake (`convert.cpp:1606-1607`, `prepared->shadowScene`). The 'nonrendering' models
   (`iw3_fastfile.cpp:12091-12098`) go through the same loop.

Research 54's edit 5 changes only path 1. A model with `no_sun_shadow` would still cast into the baked sun depth and the
compressed bake, so the shadow stays. Path 2 must be gated too.

### 2.3 Patch list (apply in order; line numbers as of this read)

1. `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp`, struct `SourceStaticModelInstance` (`:194-206`). Insert
   after line 205 (`std::uint8_t lodScaleIndex{6};`):
   ```cpp
   // misc_model no_shadow / no_sun_shadow: no sun cascade bit and no caster in the baked sun scene.
   bool noSunShadow{};
   ```
2. Same file, after line 3789 (`converted.shadowLights = ...`):
   ```cpp
   if (const auto noSun = instance.find("no_sun_shadow"); noSun != instance.end() && !noSun->is_null())
       converted.noSunShadow = noSun->is_boolean() ? noSun->get<bool>() : noSun->get<int>() != 0;
   ```
3. `E:\mw124\x64-zt\src\games\iw8\common\replay_render.h`, struct `StaticModelInstance` (`:246-259`). Insert after
   line 258 (`uint8_t lodScaleIndex{6};`), at the end so the existing brace-init order still works:
   ```cpp
   // Placement opted out of sun shadows: the sun collection byte (replay_render.cpp:2350) is 0.
   bool noSunShadow{};
   ```
4. `iw3_fastfile.cpp:5247-5248` (`staticModels.instances.push_back({...})`): add `, sourceInstance.noSunShadow` as
   the seventh element after `sourceInstance.lodScaleIndex`.
5. **New (not in research 54):** `iw3_fastfile.cpp`, `BuildShadowScene`, first line of the instance loop at `:5324`.
   Add after the `{` of the loop:
   ```cpp
   if (instance.noSunShadow)
       continue; // no caster in the baked sun depth or the compressed sun bake
   ```
   Put it after the `instance.model` range check at `:5326-5327` if you prefer to keep the validation first.
6. `E:\mw124\x64-zt\src\games\iw8\common\replay_render.cpp:2350`:
   ```cpp
   collection[0x0E] = instance.noSunShadow ? 0 : sunShadowMask;
   ```
   Stock uses byte 14 = 0 on 482 placements (research 54 section 5), so the 0 value is a stock shape.

Optional and unchanged from research 54: skipping the collision instance (`iw3_fastfile.cpp:5251-5254`) is a separate
`no_collision` change. Not needed for the sun shadow.

### 2.4 Editor side (not done here)

- `src/core/smodel_flags.h:14-27`: add `kSModelKeyNoSunShadow = "no_sun_shadow"` if you want the sun part separate. Simpler
  is to keep `no_shadow` as "both" and emit both keys (next line).
- `src/core/compile_iw8.cpp:1843`: change to
  `(p.noShadow ? std::string(",\"shadow_lights\":[],\"no_sun_shadow\":true") : std::string())`.
  The converter then reads both keys. `"no_sun_shadow":true` is read by `get<bool>()` in patch 2.

### 2.5 Unverified

- That the native mask (byte 14) and the baked scene are the only two sun gates. Not checked in Replay's binary.
- That `shadow_lights: []` (local lights) and `no_sun_shadow` do not interact.
- Whether the owner's sun shadow comes from path 1, path 2, or both. The fix is the same in both cases.

## 3. The crash (hot-sun channel)

### 3.1 Stock usage of the sun builtins (verified by grep of the stock dump)

Dump: `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts`. Names checked against gsc-tool
(`E:\mw124\x64-zt\deps\gsc-tool\src\gsc\engine\iw8_func.cpp:177-181, 312-313, 448-449, 494-496`).

| Builtin | Stock call sites (all in `sp/` or `engine/sp/`) | Arg shapes seen | MP use |
|---|---|---|---|
| `getmapsunangles()` | `hometown_lighting.gsc:8`, `highway_lighting.gsc:19`, `captive_lighting.gsc:66` (set at init) | 0 args, returns angles | none |
| `lerpsunangles(a, b, t [, x, y])` | `captive_lighting.gsc:68` (3); `highway_lighting.gsc:37,59` (3); `hometown_lighting.gsc:194` (3), `:204,:222,:225` (5) | vector, vector, time, optional two floats | none |
| `setsuncolorandintensity(...)` | `engine/sp/utility.gsc:110,113` (4 scalars: `var_5[0..3]`); `embassy_lighting.gsc:1435,1445` (1, literal `0.0`/`0.0045`); `estate_lighting.gsc:95`, `proxywar_lighting.gsc:76` (1, arithmetic) | scalar or 4 scalars | none |
| `setsundirection(...)` | **no call site in the dump** | unknown | none |
| `resetsundirection()` | `hometown_lighting.gsc:227` | 0 args | none |

- `grep` of `scripts/mp` for all of the above: **no hits.** The sun builtins appear only in `sp/` and `engine/sp/`.
- The builtin lists (`E:\IW8-Radiant\data\generated\iw8_gsc_builtins.txt:161,190-191,333-337,554,786,861-863`; the
  converter copy `E:\mw124\x64-zt\src\convert\iw3_iw8\iw8_gsc_builtins.inc:175,204-205,568,800,875-876`) list names
  with no mode flag, so whether the engine registers them in MP is **UNVERIFIED**.
- `src/convert/iw3_iw8/iw3_scripts.cpp:60-68` drops `setsunlight`, `resetsunlight` from IW3 scripts. It does NOT drop
  `setsuncolorandintensity`, `lerpsunangles` or `setsundirection`. This is about IW3 fog and vision ownership, not MP crashes.

### 3.2 What the editor sent (before removal)

From the removed code (its last copy is still in the test fixture `tests/data/iw8build/mp_iw8_sample_r/extraction/extracted/maps/mp/mp_iw8_sample_r.gsc:128-140`,
and research 47's code block near line 246): `lerpsunangles(level.iw8r_sunang, ang, 0.1)` and
`setsuncolorandintensity((r, g, b), intensity)`, where the second call passes a **vector plus a float**.

### 3.3 Crash conclusion

The owner's crash is recorded (`hot_channels.cpp:166`). The crash log was not in the repo, so the cause is not pinned.
Ranked hypotheses, with what supports each:

1. **Argument shape (most likely, UNVERIFIED).** Stock never passes a vector to `setsuncolorandintensity`. The stock
   shapes are scalars (`engine/sp/utility.gsc:110,113`) or one literal. The editor's `((r,g,b), i)` is a form with no
   stock example. A builtin that reads its arguments by position and type can fault on this.
2. **SP-only builtins in an MP context (UNVERIFIED).** Every stock call is in an SP script, and the SP scripts
   initialise `level.sunangles = getmapsunangles()` at init (`hometown_lighting.gsc:8` and the two above). The editor
   called `getmapsunangles()` lazily, in a live thread, which stock never does.
3. **`setsundirection` untested.** It has no stock caller, so its argument shape is unknown.

### 3.4 Safe MP alternative

**None verified.** Options, all untested in MP:

- Keep the sun fixed at load: `sundirection` and `suncolor` on worldspawn, set through the sun dialog. This is the
  shipped path (`compile_iw8.cpp:1616-1619`) and avoids the builtins entirely.
- If live colour is wanted, use only the 4-scalar shape of `setsuncolorandintensity(r, g, b, i)` from
  `engine/sp/utility.gsc:110`. No `lerpsunangles`, no `setsundirection`, no vector arguments. Test on a throwaway map
  with the crash handler watched (Replay log). **UNVERIFIED.**
- A dvar-driven sun direction: `r_sun_from_dvars` (Dvars.txt:5246, "Set sun flare values from dvars rather than the
  level") is for the flare, not the direction. `r_lightTweakSunHeading` (Dvars.txt:4782) and `r_lightTweakSunLight`
  (Dvars.txt:4783) have no description and no stock use found. Their effect is **UNVERIFIED**; they are the only
  candidates found that might move the sun without a builtin.

## 4. Summary of proposed fork patches

- **Shadow range:** option A (map-script dvars, no fork change) first. Option B is an optional fork change at
  `iw3_fastfile.cpp:12116` reading worldspawn `iw8r_sun_bake_range` (patch in section 1.4). The owner's "too short from
  the player" symptom most likely comes from the native cascades (`sm_sunCascadeSizeMultiplier*`) or the coarse-cull
  distance `cg_playersmp_CoarseCullSystem_SunShadowDist`. UNVERIFIED in game.
- **Per-placement no sun shadow:** six fork edits in section 2.3, including the `BuildShadowScene` gate that research 54
  missed (`iw3_fastfile.cpp:5324`). Plus the editor write at `compile_iw8.cpp:1843`.
- **Crash:** the sun channel is removed. Stock MP never calls the sun builtins. The argument shape of the removed
  `setsuncolorandintensity` call (vector plus float) has no stock example and is the most likely fault. No safe MP
  replacement is verified.

## 5. Unverified (consolidated)

- Every in-game effect: cascade multiplier, `sm_sunDistantShadows`, `RLPNQQRPR`, the static-region key, the no-sun-shadow
  bytes. Nothing was run.
- Default values of all `sm_`, `r_` and `cg_` sun dvars (Dvars.txt has none).
- Whether Replay consumes `nearPlane`/`farPlane` (written as 0 by the fork).
- Whether Replay reads any `csm_*` worldspawn key from the MapEnts string (the converter does not; Replay binary not checked).
- Whether `setsaveddvar` accepts the readable names in IW8 MP, or only the hashed ones.
- Whether the engine registers `lerpsunangles`/`setsuncolorandintensity` in MP at all.
- The crash cause (no crash log was read).
- Texel size chosen by `BakeDepthImage` for a given region.
