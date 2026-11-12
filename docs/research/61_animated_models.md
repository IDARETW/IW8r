# 61. Animated models: stock xanim on a script_model (roadmap 4.21)

Date: 2026-11-10. Research only: no code, fork, zone or game file was changed or built. Roadmap 4.21 asks for stock
animations played by name on a `script_model`, with playback in the editor camera.

Path conventions:
- `GSC:` is the stock IW8 8.19 GSC dump, `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\`, cited
  as `mp/...` relative to that folder (the same convention as research 49). Its README says it is 8.19, not Replay 1.20,
  so every stock citation carries that caveat.
- `FORK:` is `E:\mw124\x64-zt\` (read only here).
- `EDITOR:` is `E:\IW8-Radiant\`.

## 0. Summary

1. Stock MP scripts play an anim on a script model with the method `self scriptmodelplayanim( anim [, notify] )`. No
   `useanimtree` call is needed for a script_model (`GSC: mp/animatedmodels.gsc:37`). The anim argument is always an
   encrypted `_encstr_` string or a `%_encstr_` reference, never a plain name, in every stock call we could read.
2. The IW3 translator in x64-zt drops every `%name` / `%_encstr_` anim reference (`FORK: src/convert/iw3_iw8/iw3_scripts.cpp:279-286, 571-575`), so the map script cannot use `%name`. A plain string survives the translation.
3. The converter only puts an anim name in the level `anm` table (the table script anims resolve through) for xanims it
   carries itself (`FORK: src/convert/iw3_iw8/convert.cpp:2022-2027`). Nothing collects names from
   `scriptmodelplayanim("...")` in the map script (the regex at `convert.cpp:1939-1953` only reads precachemodel, setmodel
   and loadfx). A stock xanim named only in the script is therefore not wired in. **This needs fork changes.**
4. Cheapest working route (UNVERIFIED end to end, no game run yet): a **name-only reference** to a stock xanim that lives
   in an always-loaded zone (`common_mp`, `common_base_mp`, `common_core_mp` or `code_post_gfx`, candidates only), played
   from the map script with `scriptmodelplayanim("<name>")`. Fork changes: three small edits in `convert.cpp` (section
   4). Carrying a copy of a stock xanim that lives only in a map zone is a larger job and is not recommended first.
5. Blocker that no code can fix: the repo has **no xanim name list**. The per-zone counts exist (section 3); the names
   do not. A name catalog needs an ACTS dump that has not been run and is UNVERIFIED to export names (section 3.3).
6. The editor-camera playback part of 4.21 is a separate, larger item (it needs the xanim bytes and a decoder in the
   editor). It is not needed to make a model animate in game.

## 1. How stock MP scripts play anims on script models

| Form | Where (GSC) | Notes |
|---|---|---|
| `self scriptmodelplayanim( var )` | `mp/animatedmodels.gsc:37` | Whole stock prop-anim path: `var` comes from `level.anim_prop_models[model][key]` (`:32-34`). Only one argument. |
| `ent scriptmodelplayanim( anim, "_encstr_..." )` | `mp/gametypes/br_gametype_payload.gsc:729`, `:5186`, `:5187`, `:8062` | Two arguments. The second is an encrypted string. Its meaning (likely a notify name) is UNVERIFIED. |
| `ent scriptmodelplayanim( "_encstr_...", "_encstr_...", undefined, 1.5 )` | `mp/gametypes/br_gametype_prop.gsc:1243` | Four arguments: anim, a string, an unused slot, a number (blend or rate: UNVERIFIED). |
| `scriptmodelplayanim( "_encstr_..." )` (string literal) | `mp/gametypes/br_ai_encounters.gsc:1872`, `:1917`, `:1962`; `mp/killstreaks/remotetank.gsc:337`, `:476`, `:478` | Encrypted strings, not plain names. |
| `level.scr_anim[...][...] = %_encstr_...` | `mp/equipment/binoculars.gsc:49`, `:53` | The `%` form: an anim reference assigned into a table. The translator drops it (section 3.1). |
| `scriptmodelplayanimdeltamotion( anim )` | `mp/broshot.gsc:1207`, `:1930`; `mp/killstreaks/plane.gsc:106` | Delta-motion variant (root motion). Not needed for a plain prop anim. |
| `scriptmodelclearanim()` / `scriptmodelpauseanim()` | `mp/broshot.gsc:1912`, `:1927`; `mp/broshot_utilities.gsc:541` | Stop or pause the current anim. |
| `precachempanim( anim )` | `mp/animatedmodels.gsc:16` (in `main()`); function pointer `mp/load.gsc:39` | Precache before play. |
| `getanimlength( anim )` | `mp/motiondetectors.gsc:554`: `getanimlength( level.scr_anim[...][...] )` | Used for the wait after a one-shot anim. |
| `self useanimtree( #animtree )` | `mp/motiondetectors.gsc:552`, `:594`; `mp/door.gsc:2524`, `:2698`; `mp/destructible.gsc:30`, `:463` | Only for animated actors and rigs (`player_rig`). Not used for script_model in the stock prop path. |
| `#using_animtree( "_encstr_..." )` | many, e.g. `mp/motiondetectors.gsc`, `cp_mp/auto_ascender.gsc:67-105` | Top-level directive. The translator skips `#` directives (`FORK: iw3_scripts.cpp:224-236`). |

Stock example for the first test: `mp/animatedmodels.gsc` shows the whole recipe (a script model, one anim by name,
`precachempanim` first, no animtree). That script's anim list is data from the level table, so the names are not in the
GSC.

Stock scripts also set `self.animname` and call `anim_single_solo` for actor animation (`mp/motiondetectors.gsc:551-553`).
That is the actor path and is not used here.

## 2. Builtins in the editor's list

`EDITOR: data/generated/iw8_gsc_builtins.txt` (generated; do not edit):

| Line | Kind | Name |
|---|---|---|
| 250 | f | `getanimlength` |
| 747 | f | `precachempanim` |
| 2332 | m | `scriptmodelclearanim` |
| 2333 | m | `scriptmodelpauseanim` |
| 2334 | m | `scriptmodelplayanim` |
| 2335 | m | `scriptmodelplayanimdeltamotion` |
| 2336 | m | `scriptmodelplayanimdeltamotionfrompos` |
| 2854 | m | `stopuseanimtree` |
| 2908 | m | `useanimtree` |

There is no builtin for xanim lookup by name, and none for `#animtree`. `scriptmodelplayanim` is a method (`m`), so the
call form is `ent scriptmodelplayanim(...)`, as in the stock code.

## 3. Assets: xanim in x64-zt and in the stock zones

### 3.1 The translator and `%` references

- `FORK: src/convert/iw3_iw8/iw3_scripts.cpp:217-220`: `#animtree` inside an expression becomes an `Anim` token.
- `FORK: iw3_scripts.cpp:279-286`: `%name` / `%_encstr_...` becomes an `Anim` token.
- `FORK: iw3_scripts.cpp:571-575`: every `Anim` token is replaced by `undefined` with the note "IW3 animation reference
  dropped". So `scriptmodelplayanim(%name)` becomes `scriptmodelplayanim(undefined)`.
- String tokens are kept and recorded (`FORK: iw3_scripts.cpp:580`), so `scriptmodelplayanim("name")` keeps its
  string. This is the route the recipe uses.

### 3.2 Carried xanims (what a map zone can hold)

- `FORK: src/convert/iw3_iw8/convert.cpp:1773-1774`: carried xanims are written with `iw8::xanim::Register` (the
  `prepared->xanims` loop).
- Where `prepared->xanims` comes from: (a) IW7 maps through `world.xanims` (written by `FORK:
  src/convert/iw7_iw8/export_world.cpp:2357-2360, 2407`), and (b) IW3 vehicle destroy anims extracted from IW3
  `common_mp.ff` (`FORK: src/convert/iw3_iw8/iw3_fastfile.cpp:11243-11292`, with the IW3 Unlinker at `:11281`).
- The editor does not write a `xanims` key (grep of `EDITOR/src` finds none), and path (b) only reads IW3 `common_mp.ff`
  (`FORK: iw3_fastfile.cpp` `FindIw3CommonFastfile`). Neither path dumps a stock Replay 1.20 xanim, so a custom map
  cannot carry a stock IW8 xanim today.
- The Unlinker xanim filter (`FORK: iw3_fastfile.cpp:477-496, 585-591`, env `IW3_REPLAY_XANIMS`) dumps XAnimParts by
  name from whichever fastfile it is given. It is only called for IW3 sources (`:10775-10812`, `:11281`).

### 3.3 Name references (the cheap route)

- `FORK: src/games/iw8/assets/replay_stock.cpp:1007-1024` (`RegisterReference`): writes a zeroed asset header with
  `,name` in a virtual block. The loader resolves the name against the zone that owns it at load time (research 51).
  It handles any type through `Stub(type)` (`replay_stock.cpp:946`). `ASSET_TYPE_XANIM = 7` (`FORK:
  src/games/iw8/iw8_structs.h:14`). **UNVERIFIED** that `Stub(ASSET_TYPE_XANIM)` returns the 0xA0 layout that
  `replay_xanim.cpp` documents (`replay_xanim.cpp:21-24`).
- `FORK: replay_stock.cpp:1421-1424` (`RegisterNameReference`) is the public entry. `convert.cpp:1797-1801` already calls it
  for vfx materials and models, so the same call works for xanim.
- The level string table `anm` (index 16) is written at `FORK: convert.cpp:2022-2027`. The comment says script-played
  anims resolve through it, "like script models through mdl" (`convert.cpp:2017`). The `mdl` table is also limited to
  models the zone carries (`convert.cpp:2017-2018`), and research 18 §4.1 lists common-zone props by name as a follow-up
  that is not done. So the `anm` path is UNVERIFIED for a stock name in a common zone until a test shows it.

### 3.4 Which zones hold stock xanims

Source: `E:\IW8-Radiant\out\x64zt-verify\coverage.json` (per-zone pool counts from x64-zt verifyzone), and
`docs/research/14_x64zt_verifyzone_coverage.md:121` (row 7, xanim, truncated there to 6 zones "+11 more").

| Zone | xanim assets | Always loaded? (from research 51) |
|---|---|---|
| `common_base_mp` | 8023 | `common_*` family: UNVERIFIED for MP (research 51 names `common_mp`, `code_*`, `techsets_common*`, `ww_common*`) |
| `common_mp` | 5731 | yes (research 51 §"What the showcase map would gain") |
| `mp_shipment` | 4115 | no (stock map) |
| `global_mp` | 294 | UNVERIFIED |
| `dw_weapon_inspect_mp_1f59659a14a316b6` | 56 | no (weapon inspect dw zone) |
| `common_br_mp` | 167 | Warzone common; MP match load UNVERIFIED (research 51) |
| `common_core_mp` | 141 | UNVERIFIED |
| `mp_aniyah_tac` | 71 | no |
| `mp_infil_br_*_chopper_tr` | 75 each | no |
| `mp_hideout` | 86 | no |
| `mp_backlot2` | 91 | no |
| `mp_emporium` | 48 | no |
| `mp_village2` | 57 | no |
| `mp_rust` | 14 | no |
| `mp_m_king` | 13 | no |
| `code_post_gfx` | 14 | yes (research 51) |

Also in `coverage.json`: `global_stream_mp` has xanimcurve only (4), not xanim.

The fan props the fork comments name as shipped prop anims (`FORK: replay_xanim.cpp:33` `equipment_box_fan_anim_stop`;
`FORK: replay_xanim.h:41` `desktop_fan_fn_01_loop`) are not in always-loaded zones by the catalog: `equipment_box_fan_anim`
is in `mp_backlot2, mp_hideout, mp_village2` and `desktop_fan_fn_01` only in `mp_hideout`
(`EDITOR: data/asset_catalog/iw8_models.tsv`). Their xanims' zones are UNVERIFIED (the xanim may sit in the model's map
zone). So they are good test subjects only if carried (route B below), not for the name-only route.

### 3.5 Name list: none in the repo

- `data/stock_assets/code_post_gfx/.../manifest.json` and `data/stock_dumps/mp_backlot2/.../manifest.json` give xanim
  counts (14 and 91) under `loaded_by_type`, not names.
- Their `assets.jsonl` has no xanim rows (checked: zero `"type":"xanim"` lines in
  `data/stock_assets/code_post_gfx/mw19replay/code_post_gfx/assets.jsonl`).
- `script_strings.json` (280 entries in code_post_gfx) has bone-like names (`trim_char_f_1_1`) and no anim names.
- `data/stock_dumps/*/acts_run.log` contain no xanim lines.
- `E:\IW8-Radiant\out\x64zt-verify\runs\log_*.txt` only log counts.
- The stock `_encstr_` anim names are not decoded: research 40 and 49 say the key table is not in the repo.

To make a catalog: ACTS export (research 04 §3.1, line 74): `acts.exe --noUpdater fastfile -r mw19replay -g <game.exe>
--oodle <dll> -a xanim -o <outdir> <zone.ff>`, run on `code_post_gfx.ff` and `common_mp.ff` from `E:\IW8\Builds\1.20-replay\zone\`.
The Replay binding for type 7 exists (`E:\mw124\atian-cod-tools\src\core\acts\tools\mw19\mw19_replay_bindings.hpp:157`,
`mw19.cpp:543`, `mw19.hpp:11`), but whether the `-a xanim` export writes names is **UNVERIFIED**: the existing stock
dumps were made without it. Test on `code_post_gfx` first (14 assets, the smallest). Not run here.

## 4. Recipe

### 4.1 Editor: entity keys on the script_model

These are new keys (not in `EDITOR/data/generated/iw8_canonical_keys.tsv`; the file has no anim key for script_model).
Keys the editor reads itself, so they are not written to the zone:

| Key | Example | Meaning |
|---|---|---|
| `classname` | `script_model` | Live entity. |
| `model` | a stock prop with its own anim (pairing UNVERIFIED) | Stock xmodel the anim was made for. The `model` key survives to the extraction: `EDITOR: src/core/compile_iw8.cpp:1667, 1693-1697` (only brush models drop it). |
| `targetname` | `iw8r_anim_1` | Unique; the thread finds the entity by it. |
| `target` | on a `script_origin` that targets `iw8r_anim_1` | Keeps the script_model live. Without it x64-zt promotes it to a static prop (`EDITOR: scripted_objects.cpp:57-60`; `FORK: iw3_fastfile.cpp:4342-4390`). The editor already adds this for scripted props. |
| `iw8r_anim` (new) | `equipment_box_fan_anim_stop` | Stock xanim name (plain text). Validate it against section 3.4 and warn when it is not in an always-loaded zone. |
| `iw8r_anim_loop` (new) | `1` | 1 = replay the anim forever (a thread waits `getanimlength` and plays again). 0 = once. The loop flag inside the xanim (`FORK: replay_xanim.h:36-48`, `replay_xanim.cpp:29-35`) is not reliable to read here. |

### 4.2 Editor: GSC the map script emits

Add to `EDITOR: src/core/map_script.cpp` (the function `mapScriptGsc` starts at `:85`; the `extras` block is at `:115-118`)
a new `MapScriptExtras::animPlacements` (struct in `EDITOR: src/core/map_script.h`, `MapScriptExtras` at `:44-60`). For
each entry the load script gets:

```
// in main(), after the existing level thread lines:
precachempanim( "equipment_box_fan_anim_stop" );
level thread iw8r_play_anim( "iw8r_anim_1", "equipment_box_fan_anim_stop", 1 );

iw8r_play_anim( name, anim, loop )
{
    wait 0.1;
    ents = getentarray( name, "targetname" );
    if ( !ents.size )
        return;
    ent = ents[0];
    for ( ;; )
    {
        ent scriptmodelplayanim( anim );
        if ( !loop )
            return;
        wait getanimlength( anim );
    }
}
```

Points that are UNVERIFIED and may need changing after a test:
- `scriptmodelplayanim( "plain string" )`: stock never does this. Runtime acceptance of a string (vs an anim handle)
  is UNVERIFIED. If it fails, the fallback is the stock shape `scriptmodelplayanim( level.scr_anim[...] )` which needs
  an animtree table; not designed here.
- `getanimlength( "plain string" )`: same caveat.
- `precachempanim` before the play: follows stock (`GSC: mp/animatedmodels.gsc:16`); whether it is required is UNVERIFIED.
- Do not write `%name` or `useanimtree( #animtree )` in the map script. The translator turns both into `undefined`
  (`FORK: iw3_scripts.cpp:571-575`).
- The `getentarray` pattern is the same one the editor already emits for `iw8r_make_solid` and `iw8r_hot_update`
  (`EDITOR: map_script.cpp:122-131`).

### 4.3 Converter (fork): the three changes

Each change is small. None has been written or built.

1. **Collect the names.** `FORK: src/convert/iw3_iw8/convert.cpp:1939-1953`: add a regex next to `scriptModel`, for
   example `scriptmodelplayanim\s*\(\s*"([^"]+)"` and `precachempanim\s*\(\s*"([^"]+)"`, into a new set `scriptAnims`.
   The existing regexes are `precachemodel|setmodel`, `setshader|precacheshader` and `loadfx`
   (`convert.cpp:1939-1953`, `:1941-1946`).
2. **Make the `anm` table see them.** `FORK: convert.cpp:2022-2027`: `levelStrings[16]` currently gets only
   `prepared->xanims`. Add `scriptAnims` (dedupe) so the names resolve, the same way `mdl` and `mat` do.
3. **Write the name references.** `FORK: convert.cpp:1797-1801` (the `vfxReferences` loop): for each name in
   `scriptAnims` that the always-loaded catalog says is in a common zone, call
   `iw8::stock::RegisterNameReference( writer, ASSET_TYPE_XANIM, name )`. Names not in a common zone need a carried copy
   (route B) and are reported as a build warning instead of a silent failure.

Route B (carry a stock xanim from a map zone) is the larger job: a dumper for stock Replay xanims (ACTS output parsed by
`iw8::xanim::LoadIw3`-like code, or the Unlinker filter at `iw3_fastfile.cpp:477-496`, which only reads IW3 today), and
the `world.xanims` / `prepared->xanims` path (`convert.cpp:1773-1774`). Not recommended until route A is tested.

No change is needed in: the editor's entity compile (the model key survives), the scripted-prop `target` logic (already
exists), or the GSC translator's string handling (strings already survive).

### 4.4 Test order (cheapest first)

1. Pick `code_post_gfx` (14 xanims) and dump its names with ACTS (section 3.5). Pick a prop that plays a loop.
2. Build a one-prop test map in the editor with one script_model, one `script_origin` targeting it, and the GSC from 4.2
   (no fork change yet). Build it and check the log: `scriptmodelplayanim("...")` should appear as a string, not
   `undefined`.
3. Apply the three fork changes (4.3) on a fork worktree, not on `E:\mw124\x64-zt` (that checkout is shared; research
   note in memory). Build the map, run `verifyzone` on the output, and check the `anm` table count.
4. In game: load the map, check the prop moves (Help > Test Checklist, or the Live Link dump). Record the result in
   `docs/TEST_CHECKLIST.md`.

## 5. Editor camera playback (the rest of 4.21)

Playing the anim in the editor viewport needs the xanim bytes (ACTS dump, section 3.5) and a decoder. The fork has one
(`FORK: src/core/assets/xanim_codec.hpp`, the XAnimParts stream codec; read `replay_xanim.h` for the layout). Whether the
editor can link that codec, and how it evaluates a delta-free prop anim per frame, is UNVERIFIED and not scoped here.
It is a separate L item; the game does not need it.

## 6. What is verified and what is not

Verified from files read here:
- Stock call forms and file:line (section 1); builtin names and kinds (section 2).
- The translator drops `%` anim references and keeps strings (section 3.1).
- The `anm` table is filled from carried xanims only (`convert.cpp:2022-2027`).
- Per-zone xanim counts (section 3.4).
- The editor keeps the `model` key on script_model (`compile_iw8.cpp:1667`).

UNVERIFIED (each needs a game or tool run):
- Whether `scriptmodelplayanim` accepts a plain string name at runtime, and what its second argument means.
- Whether a name in the `anm` table is enough for the runtime to find the xanim, and whether `RegisterNameReference`
  for `ASSET_TYPE_XANIM` produces a loadable header (`Stub` layout, section 3.3).
- Which zones are always loaded in MP (`common_base_mp`, `common_core_mp`, `global_mp`, `common_br_mp` are UNVERIFIED).
- Whether `ACTS -a xanim` writes names (section 3.5).
- Which stock prop model pairs with which stock anim (the `equipment_box_fan_anim_stop` / `desktop_fan_fn_01_loop`
  names come from fork comments, not from a stock script).
- The loop flag in shipped xanims (`replay_xanim.cpp:29-35` says flags 2 for non-looping prop anims).

## 7. Recommendation

Do route A (section 4.3, name-only reference) after an ACTS xanim name dump of `code_post_gfx`. It is three small fork
edits plus the editor's GSC and keys, and nothing else. Route B (carrying stock xanims) and editor-camera playback are
separate items and should wait for route A's result.
