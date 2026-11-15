# IW8 Radiant Suite: handover

What you are given, how to install it, what each part does, how it was proven, and what only a look in the game can
settle. Scope, as decided on 2026-11-12: stock map editing, Warzone editing, and map and mode creation, with a
viewport that shows the map as the game draws it. Ground War scale is outside that scope.

State on the evening of 2026-11-14: everything below is built and checked without the game. Section 4 says what was
seen in the game that day, what was reported from it and is not settled, and what has not been seen. Section 5 lists
what is not built.

Update, morning of 2026-11-15: the viewport's Game Lighting gained the night's rendering work, all proven offline
(CHANGELOG 2026-11-15): props lit per vertex from the light grid (Warzone tiles too), self-visibility, lightmapped
props, the game's GTAO, every layer kind of layered props (colour, normal, gloss, tint masks, clear coats), layer masks
read as the game reads them, and static-model displacement. Each of these can be turned off for a comparison in View >
Game Lighting Options > Stock Map Detail. Installer and zip rebuilt 07:19 from commit c01358d (effect preview included). Frame times with
everything on: Speedball 6.7, Hideout 9.9, Aniyah 13.5, Verdansk 22.2 ms (25.5 ms from above the town).

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
(Compile > HUD Images Folder) and show your own text (Compile > Map Text File). The build marks where a weapon can
be mounted (open wall corners and ledge tops of your brushes and placed models; View > Show Compiled Collision draws
them in yellow).

**Light it.** Compile > Bake lightmaps bakes the world's lightmap (sky light, bounce, where the sun reaches);
Bake Settings sets the sky and the detail. Without a bake the game lights the map with a flat sky light and the sun.

**See it as the game draws it.** View > Game Lighting (Ctrl+Alt+L) shows your map the way the built map is shaded:
the baked look when Bake lightmaps is on, the flat sky light when it is off. On a stock map it draws the map's own
lightmaps, probes, lamps, sun, exposure and white balance (the warm cast of Hideout or Verdansk); brushes you add to
a stock map take that map's light grid. Stock props change detail level and disappear at the distances the game
uses.

**Your own look.** Tools > Shader Editor: a shader file for your map's surfaces (the toon shader is the example), and
a filter for a stock map's surfaces. A shader can read the scene's depth, so outlines work on your own maps and on
stock maps alike. File > First-Person Look (weapon and hands) builds the same filter into the weapon and arm
materials; they belong to a zone every map loads, so the look is on every map while it is installed (Install Into
Game / Restore In Game). The toon look is built and waits there for Install.

**Air and sky.** Compile > Atmosphere: a haze preset (desert, clear, overcast, dusk, fog) with an amount sets the
game's own fog, sky blend and volumetric air for a built map; `iw8r_volume` boxes add local air, a lamp with
`iw8r_volumetric` glows in it, and Compile > Sun & Sky has the sky's brightness. The viewport shows all of it.

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
walls. Off by default. Its first game test (2026-11-14) lost surfaces and models; the cause was found and fixed the
same day (research 129), and Cinder Rail Toon built again with the fixed bake worked in game that evening. View >
Umbra Visibility shows what a stock map's own occlusion data hides.

**From a browser.** Tools > Web Server: the editor's menus, twelve dialogs and docks as forms, the editor's own 3D picture with
fly controls, drawing, resizing and reshaping brushes (corner and edge handles), placing assets, prefabs and
entities, the inspector with key help.

## 3. How it was proven

None of this needs the game running:
- `bin\release\iw8r_tests.exe`: about 182,800 checks in 1,384 tests of the core (map format, geometry, builds, bakes,
  decoders, the install logic, the occlusion bake, the mount edges).
- `IW8Radiant.exe --gui-test tests\data\iw8_sample_room.map --out <dir>`: about 1,090 checks of the editor itself,
  the web server and its page included.
- `tools\run_all_tests.ps1`: both of those plus a start-up test, in one run.
- `py -3 -I tools\run_offline_proofs.py <work dir>`: 27 end-to-end proofs; all passed on the last build of 2026-11-14. Each one edits or builds a real map with the
  live converter, reads the result back with ACTS, compares it with what was asked, and checks that the game's own
  loaders accept the zone (about 11 minutes).
- Among those proofs: the light planes of a built map, read back from the zone, equal the editor's bake texel by
  texel (0.23 % apart on average), and a map's own HUD picture and text are read back from its zones.
- `IW8Radiant.exe --viewport-bench <map> --out <dir>`: frame times and pictures of every view mode.
- The web page was driven in a real browser against a running editor for every feature listed above.

The state of each item, the research behind it and every proof are in `docs\ROADMAP.md`, `docs\research\`
(more than 130 notes, the latest numbered 138) and `CHANGELOG.md`.

## 4. Only the game can settle these

**Seen in game on 2026-11-14** (the owner's runs): an installed stock edit loads (the toon zone of Shipment); the toon
lines from the scene depth on Cinder Rail Toon and on Shipment's stock geometry; the haze; the bright sky.

**The Cinder Rail lobby crash (2026-11-14 evening)** is fixed in the converter (research 134: the game's asset stack
holds two levels, so the zone writer now lists any asset that would load three deep before its user, and refuses a
zone that still would). The package installed at 20:40 is byte for byte that build (checked 2026-11-15), its build log
shows the writer reordering all 12 zones without an error, and ACTS reads the installed zone back cleanly. Loading it
in the game would confirm it; nothing is known to be missing.

**Reported from those runs and not settled:**
- Shipment with the toon zone: "objects appear in and out". No cause found. Ruled out offline: missing pipeline
  states, the order of shader arguments, technique flags, shaders the driver refused, the image the lines take over
  (all 192 stock shaders read it only in thermal view). The zone changes shaders only, not what the game draws or
  hides. File > Restore Stock Map In Game takes the zone out.
- Cinder Rail Toon lost surfaces and models: that build had the first occlusion bake. Settled the same evening: the
  build without the bake is fine in game, and so is a build with the fixed bake.
- The minimap was a quarter turn off against the player on built maps (seen on Cinder Rail Toon). Cause: the picture
  was drawn for a map whose north is +X while the build writes north = +Y. Fixed (the picture is drawn with the
  map's north up) and proven offline on two built maps; rebuild a map to get it. Not seen in game since.

**Built and proven offline, not seen in game.** `docs\TEST_CHECKLIST.md` has the steps:
1. **The first-person look** after File > First-Person Look > Install Into Game.
2. **Weapon mounting** on a map built on or after 2026-11-14.
3. **Your own map looks like View > Game Lighting**, with Bake lightmaps off and on.
4. **A stock door carried into your own map opens** (real stock scripted objects).
5. **A stock map rebuilt with removed world geometry**: the object is gone and you walk through where it was.
6. **Copied loot, doors and extraction sites on Quarry** work like the originals.
7. **The minimap** on a map built on or after the evening of 2026-11-14: the player arrow sits where the player is.

## 5. Not built, or built in part

- Game Lighting: nothing left of the rendering audit (research 132 / 133). The last item, the world screen distortion
  (Shipment's water wall, its only user), was checked against the world data on 2026-11-15: its vertices are white and
  its strength caps at half a unit, so the game draws the scene behind it, untinted, moved by at most half a unit,
  which is what the viewport shows. Since the same day the vertex displacement of stock models and the "code" layers
  (clear coats, research 137) are drawn too.
- Effect preview (View > Effect Preview): since 2026-11-15 placed effects draw with their sprite images, atlas
  animation, size and colour curves, spawn shapes, forces and the game's lit / additive split (research 138). Left
  out: lens flares, heat-haze distortion, model debris, child effects and the game's per-particle lighting (lit smoke
  takes the map's ambient and sun instead).
- Occlusion: thin slanted walls, terrain and models as occluders (other slanted and many-sided brushes hide by the
  box inside them). Removing stock world geometry opens the whole map's occlusion: everything is drawn, which
  costs frame time on that map and nothing else. A local update was decided against (a mistake there makes objects
  vanish in the game, and only a playtest shows it).
- Viewport: the volumetric air of a haze follows the values the build writes, but its picture has not been compared
  with the game's. The normal and specular maps of your own materials are drawn by the build shader's rule, which
  has not been compared with the game either.
- Mounting: a placed model without a collision hull (it gets a plain box) gives no mount edges, and there are no
  mantle edges: mantling uses the game's own trace, as before.
- The first-person look is all maps or none: the weapon and hands cannot take a look for one map only.
- Streamed Warzone tiles cannot be edited (a client matter, see section 2).
- Map fonts are pictures drawn in your font (since 2026-11-15, `<map name>.hud/fonts.txt`, kept in the map's own
  files): the game's own text elements (`settext`) keep the game's fonts. Own menus (LUI) are not built.
- A turned stock sun keeps the old sun in the baked bounce light.
- Items the game must answer before more work makes sense (reverb zones, the UAV's minimap sweep, intro cameras) are
  listed with their state in `docs\ROADMAP.md`.
- Ground War scale maps are out of scope.
- The smaller points that were left on purpose, each with what is there now and why, are one table in
  `docs\PLAN_REMAINING.md` ("Open points closed by decision").
