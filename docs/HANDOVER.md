# IW8 Radiant Suite: handover

What you are given, how to install it, what each part does, how it was proven, and the few things only a look in the
game can settle. Scope, as decided on 2026-11-12: stock map editing, Warzone editing, and map and mode creation, plus
lightmap baking with a viewport that shows the map as the game draws it. Ground War scale is outside that scope.

## 1. What to install

| Piece | Where | Notes |
|---|---|---|
| Editor installer | `out\installer\IW8RadiantSuite-Setup-0.1.0.exe` | Or the portable zip `out\package\IW8RadiantSuite-<date>.zip`. Neither contains game files or your settings. |
| Converter (x64-zt fork) | `E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe` | Branch `iw8radiant/build-extraction`, built from the editor work branch `iw8radiant/scriptables` (latest commit in `docs/PROGRESS.md`). Older builds are kept in `out\zt_backup`. Local only, never pushed. |
| ACTS, Oodle, the game | your existing paths | The editor asks for them once (Tools > Tool Paths). The game folder is only read. |

Start the editor, open a map (or File > New), and read `docs\USER_GUIDE.md` (the packaged copy is in `docs\`).
`docs\KEYS_REFERENCE.md` lists every worldspawn and entity key the build understands.

## 2. What it does

**Make your own maps and modes.** Brushes, patches, terrain, prefabs and prefab bundles, stock models and effects,
lights, probes, spawns, game type templates and your own game type scripts, mode rules (health, speed, one weapon,
infinite ammo), Warzone objects (loot points, clusters and zones, buy stations, vehicles), traversals for bots, map
scripts. Compile > Compile + Build + Validate makes the game's zones and checks them with ACTS.

**Bake lighting.** Compile panel > Bake lightmaps (worldspawn `iw8r_bake_lightmaps`): the build bakes the world's
lightmap (sky light, bounce of the sun and lights, sun visibility); the game draws the sun and lights live on top.

**See it as the game draws it.** View > Game Lighting (Ctrl+Alt+L): the same bake, made in the background and again
after edits, shaded with the converter's world shader and the game's exposure and tone curve. On a stock map: the map's
own baked lightmap, reflection probes, sun, auto exposure and the stock world shader's specular. Measured cost: about
0.5 to 2.2 ms per frame at 1920 x 1080 on stock worlds (Shipment runs about 200 fps in game).

**Edit stock maps.** File > Open stock zone, edit, File > Rebuild Stock Zone (and Install / Restore, which you run):
- entities, spawns, added geometry and models (overlay builds);
- hide stock props and remove their collision; remove stock world geometry with `iw8r_remove_world` boxes (drawing,
  collision, sun shadow, occlusion);
- move, copy and delete placed objects, including doors, signs, props and extraction sites (File > Edit Placed Objects);
- relight: sun colour, intensity and direction (the sun shadow is baked again), sky from another map, fog and vision,
  retexture.

**Edit Warzone maps.** The same stock tools on mp_br_tut, mp_br_quarry and mp_donetsk, plus loot points (File > Edit Loot
Points), new loot clusters and zones, buy stations and the streamed world around the camera.

## 3. How it was proven

Nothing below needs the game running:
- `bin\release\iw8r_tests.exe`: about 140,000 checks of the core (map format, geometry, builds, bakes, decoders).
- `IW8Radiant.exe --gui-test tests\data\iw8_sample_room.map --out <dir>`: about 960 checks of the editor itself.
- `py -3 -I tools\run_offline_proofs.py <work dir>`: 19 end-to-end proofs. Each one edits or builds a real map with the
  live converter, reads the result back with ACTS, compares it field by field with what was asked, and checks the game's
  own loaders accept the zone like the stock one (about 12 minutes).
- `IW8Radiant.exe --viewport-bench <map> --out <dir>`: frame times and pictures of every view mode.

The state of each item, the research behind it and every proof are in `docs\ROADMAP.md`, `docs\research\` and
`CHANGELOG.md`.

## 4. Only the game can settle these

Everything was built and checked offline; none of the following has been seen in the game. `docs\TEST_CHECKLIST.md`
lists them as optional checks, most important first:
1. A stock door carried into your own map opens (real stock scripted objects).
2. A map with Bake lightmaps next to View > Game Lighting from the same spot: brightness and colours should match. The
   exposure constants are read from the game's shaders but a few are inferred.
3. A stock map rebuilt with removed world geometry: the object is gone and you walk through where it was; frame rate
   is a little lower there (occlusion is opened for that map).
4. A stock map with a turned sun: shadows follow; the baked bounce light keeps the old sun.
5. Copied loot rocks, doors and extraction sites on Quarry work like the originals.

## 5. Known limits

- Game Lighting on Warzone maps: in progress at handover time (their lightmaps are streamed); see `docs\PROGRESS.md`.
- Stock world removal opens the whole map's occlusion when the removed geometry was an occluder (a local fix needs the
  occlusion data's format decoded).
- A turned stock sun keeps the old sun in the baked lightmap's bounce light; cut-out props (fences, leaves) do not cast
  in the re-baked sun shadow.
- Ground War scale maps are out of scope.
