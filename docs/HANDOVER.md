# IW8 Radiant Suite: handover

What you are given, how to install it, what each part does, how it was proven, and what only a look in the game can
settle. Scope, as decided on 2026-11-12: stock map editing, Warzone editing, and map and mode creation, with a
viewport that shows the map as the game draws it. Ground War scale is outside that scope.

State on 2026-11-14: everything below is built and checked without the game. **Nothing in section 4 has been seen in
the game.** Section 5 lists what is not built.

## 1. What to install

| Piece | Where | Notes |
|---|---|---|
| Editor installer | `out\installer\IW8RadiantSuite-Setup-0.1.0.exe` | Or the portable zip `out\package\IW8RadiantSuite-<date>.zip`. Neither contains game files or your settings. The packaged editor passes its own start-up test. |
| Converter (x64-zt fork) | `E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe` | Branch `iw8radiant/build-extraction`, built from the work branch `iw8radiant/scriptables` (worktree `C:\zsw-iw8r\scriptables`). Earlier builds are kept in `out\zt_backup`. |
| ACTS, Oodle, the game | your existing paths | The editor asks for them once (Tools > Tool Paths). The editor itself only reads the game folder; the two install actions in section 2 are the exception and are yours to run. |

Start the editor, open a map (or File > New), and read `docs\USER_GUIDE.md` (the packaged copy is in `docs\`).
`docs\KEYS_REFERENCE.md` lists every worldspawn and entity key the build understands.

## 2. What it does

**Make your own maps and modes.** Brushes, patches, terrain, prefabs and prefab bundles, stock models and effects,
lights, probes, spawns, game type templates and your own game type scripts, mode rules, Warzone objects (loot points,
clusters and zones, buy stations, vehicles), traversals for bots, map scripts. F7 builds the game's zones and checks
them with the game's own loaders (ACTS). Brush > Paint Mesh on Faces lays a paintable mesh on brush surfaces for
blended materials (Patch > Vertex Paint Mode paints it). A map script can draw your own pictures on the HUD
(Compile > HUD Images Folder) and show your own text (Compile > Map Text File).

**Light it.** Compile > Bake lightmaps bakes the world's lightmap (sky light, bounce, where the sun reaches);
Bake Settings sets the sky and the detail. Without a bake the game lights the map with a flat sky light and the sun.

**See it as the game draws it.** View > Game Lighting (Ctrl+Alt+L) shows your map the way the built map is shaded:
the baked look when Bake lightmaps is on, the flat sky light when it is off. On a stock map it draws the map's own
lightmaps, probes, lamps, sun, exposure and white balance (the warm cast of Hideout or Verdansk); brushes you add to
a stock map take that map's light grid. Stock props change detail level and disappear at the distances the game
uses.

**Your own look.** Tools > Shader Editor: a shader file for your map's surfaces (the toon shader is the example), and
a colour filter for a stock map's surfaces.

**Edit stock maps.** File > Open IW8 Stock Map, edit, File > Rebuild Stock Zone:
- entities, spawns, added brushes and models;
- hide stock props and remove their collision; remove stock world geometry with `iw8r_remove_world` boxes;
- move, copy and delete placed objects (doors, signs, props, extraction sites);
- relight (sun, sky, fog, vision), retexture, a shader filter.

File > Install Edited Stock Map Into Game puts the rebuilt zones into the game: the stock zone files move to
`iw8r_stock` in the game folder and the edited ones take their place. File > Restore Stock Map In Game moves them
back. (The game does not take a small patch file for its own zones: research 116.)

**Edit Warzone maps.** The same tools on mp_br_tut, mp_br_quarry and mp_donetsk, plus loot points, loot clusters and
zones, buy stations, and the streamed world that follows the camera. Limit: the streamed tile files themselves cannot
be replaced (the client refuses unsigned tiles), so edits live in the main map zone.

**Occlusion (optional).** Compile > Occlusion bake gives your map cells and portals so the game skips what is behind
walls. Off by default; View > Umbra Visibility shows what a stock map's own occlusion data hides.

**From a browser.** Tools > Web Server: the editor's menus, twelve dialogs and docks as forms, the editor's own 3D picture with
fly controls, drawing, resizing and reshaping brushes (corner and edge handles), placing assets, prefabs and
entities, the inspector with key help.

## 3. How it was proven

None of this needs the game running:
- `bin\release\iw8r_tests.exe`: about 181,500 checks in 1,346 tests of the core (map format, geometry, builds, bakes,
  decoders, the install logic, the occlusion bake).
- `IW8Radiant.exe --gui-test tests\data\iw8_sample_room.map --out <dir>`: about 1,080 checks of the editor itself,
  the web server and its page included.
- `tools\run_all_tests.ps1`: both of those plus a start-up test, in one run.
- `py -3 -I tools\run_offline_proofs.py <work dir>`: 25 end-to-end proofs; all passed on the last build of 2026-11-14. Each one edits or builds a real map with the
  live converter, reads the result back with ACTS, compares it with what was asked, and checks that the game's own
  loaders accept the zone (about 11 minutes).
- Among those proofs: the light planes of a built map, read back from the zone, equal the editor's bake texel by
  texel (0.23 % apart on average), and a map's own HUD picture and text are read back from its zones.
- `IW8Radiant.exe --viewport-bench <map> --out <dir>`: frame times and pictures of every view mode.
- The web page was driven in a real browser against a running editor for every feature listed above.

The state of each item, the research behind it and every proof are in `docs\ROADMAP.md`, `docs\research\`
(more than 110 notes, the latest numbered 120) and `CHANGELOG.md`.

## 4. Only the game can settle these

Everything was built and checked offline. `docs\TEST_CHECKLIST.md` has the steps; its first "In game" section, "the
short list", is these in order:
1. **An installed stock edit loads.** The toon filter on Shipment (`out\agent\toon_ship\edit`, installed with File >
   Install Edited Stock Map Into Game) is the smallest case. This is the one check everything in "Edit stock maps"
   depends on: the first install method (patch files) stopped the game, and the new one has not been run.
2. **Your own map looks like View > Game Lighting**, with Bake lightmaps off and on.
3. **A stock door carried into your own map opens** (real stock scripted objects).
4. **A stock map rebuilt with removed world geometry**: the object is gone and you walk through where it was.
5. **Copied loot, doors and extraction sites on Quarry** work like the originals.
6. **The occlusion bake** (only if you want it): nothing is missing anywhere on a map built with it.

## 5. Not built, or built in part

- Occlusion: thin slanted walls, terrain and models as occluders (other slanted and many-sided brushes hide by the
  box inside them). Removing stock world geometry opens the whole map's occlusion: everything is drawn, which
  costs frame time on that map and nothing else. A local update was decided against (a mistake there makes objects
  vanish in the game, and only a playtest shows it).
- Viewport: volumetric fog is uncalibrated and off. The normal and specular maps of your own materials are drawn by
  the build shader's rule, which has not been compared with the game.
- Streamed Warzone tiles cannot be edited (a client matter, see section 2).
- A turned stock sun keeps the old sun in the baked bounce light.
- Items the game must answer before more work makes sense (reverb zones, the UAV's minimap sweep, intro cameras) are
  listed with their state in `docs\ROADMAP.md`.
- Ground War scale maps are out of scope.
- The smaller points that were left on purpose, each with what is there now and why, are one table in
  `docs\PLAN_REMAINING.md` ("Open points closed by decision").
