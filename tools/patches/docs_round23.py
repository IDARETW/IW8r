import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def one(s, a, b):
    assert s.count(a) == 1, (s.count(a), a[:70])
    return s.replace(a, b)

# ---------------- catalogue tool header
p = 'tools/build_scriptable_catalog.py'
s, nl = load(p)
s = one(s, "        f.write('# (each limit = its door instances), so physics_limit is known too. The editor builds only always_loaded + verified.\\n')",
        "        f.write('# (each limit = its door instances), so physics_limit is known too. The editor builds a verified definition:\\n')\n"
        "        f.write('# by name when always_loaded, else (with Real stock scripted objects on) carried from its first mp_ zone.\\n')".replace('\n', nl))
save(p, s)

# ---------------- door bundle wording
p = 'data/prefabs/bundles/doors/stock_door_wood_single.iw8prefab/prefab.json'
s, nl = load(p)
s = one(s, "Today it is built from the stock door's own parts (model, sounds, timing, prompt words) and a script; it becomes the native stock door when the converter can write scriptables.",
        "With Edit > Preferences > Real stock scripted objects turned on it is the game's real door, copied from the stock map into yours. With it off it is built from the stock door's own parts (model, sounds, timing, prompt words) and a script.")
s = one(s, "stock door built as a scripted door from stock parts (a map cannot carry the stock door definition yet)",
        "stock door built as a scripted door from stock parts (Real stock scripted objects is off in Preferences)")
save(p, s)
p = 'tests/prefab_bundle_tests.cpp'
s, nl = load(p)
s = one(s, 'stock door built as a scripted door from stock parts (a map cannot carry the stock "' + nl,
        'stock door built as a scripted door from stock parts (Real stock scripted objects is off "' + nl)
s = one(s, '"door definition yet) (missing build feature(s): scriptable_instances, scriptable_def_carry)"));',
        '"in Preferences) (missing build feature(s): scriptable_instances, scriptable_def_carry)"));')
save(p, s)

# ---------------- ROADMAP
p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def append(row, text, lead=None):
    assert '|' not in text
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    cells = lines[hit[0]].split(' | ')
    assert len(cells) == 5, (row, len(cells))
    cells[3] = cells[3].rstrip() + ' ' + text
    if lead:
        old, new = lead
        assert cells[3].startswith(old), cells[3][:60]
        cells[3] = new + cells[3][len(old):]
    lines[hit[0]] = ' | '.join(cells)
append('6.14', 'Update (2026-11-12, research 86 step F3): stock definitions that live in map zones can be carried. With Edit, Preferences, Real stock scripted objects turned on (off by default) the build names the stock zone per instance and the converter (fork 02182e8a, in the live converter) reads that zone in memory, '
       'takes the definition with everything it uses (models, model surfaces, physics asset, detail collision, images; and from the zone\'s techsets companion its materials and technique sets) and adds it when the map\'s zones are written again; the instance record points at the definition as in a stock map. '
       'Every name the copy still references is checked against the always-loaded zones (shaders in code_post_gfx, common technique sets); the streamed texture data is in the game\'s global packs, which are always mounted. '
       'Offline proof (tools/scriptable_carry_test.py): a door, a traffic cone and a car definition read back by ACTS from the built zone equal the stock ones field for field, all zones load with 0 failed, and a map without carried definitions still builds byte-identical zones. '
       'The map script starts the stock door script (scripts/engine/scriptable_door system_init) when a door is placed. 317 definitions are buildable this way. NOT SEEN in game: this is the one item where a single look in game is needed (does the map load, does the door open).',
       lead=('partial (instances built 2026-11-11, see the update at the end of this cell; definitions that live in map zones are not carried yet)',
             'done offline (2026-11-12: instances, and stock definitions carried into the map behind a Preferences switch; see the updates at the end of this cell; not seen in game)'))
append('6.24', 'Update (2026-11-12): the stock door prefab now builds the game\'s real door when Edit, Preferences, Real stock scripted objects is on (row 6.14): its definition, both models, physics asset and detail collision come from Talsik Backlot\'s zone, and the map script starts the stock door script. With the switch off it builds the scripted stand-in as before. NOT SEEN in game.')
append('3.11', 'Update (2026-11-12): brush faces inside placed prefabs are textured in the camera view (they carried no material before).')
save(p, nl.join(lines))

# ---------------- USER GUIDE
p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = ("What can be placed today is small: `hardpoint_chevron`, the marker of a Hardpoint zone. It is the only definition" + nl +
     "that the game always has loaded and whose build numbers are fully known. Doors, traffic cones and cars live inside" + nl +
     "stock map files; the build leaves such an entity out and says so in the log. The file" + nl +
     "`data/asset_catalog/iw8_scriptables.tsv` lists all 837 stock definitions and where each one lives." + nl +
     "Not seen in game.")
b = ("There are two kinds:" + nl + nl +
     "- **Always loaded**: `hardpoint_chevron`, the marker of a Hardpoint zone. It is placed by name and always builds." + nl +
     "- **From a stock map**: doors, traffic cones, cars, tyres, radios and other world objects (317 definitions). Their" + nl +
     "  definitions live inside stock map files. Turn on **Edit > Preferences > Real stock scripted objects** and the" + nl +
     "  build copies the definition with its models, collision and materials from the stock map into yours (it reads the" + nl +
     "  game folder, a few seconds per stock map). With the switch off such an entity is left out and the log says so." + nl + nl +
     "The switch is off by default because nothing built this way has been seen in game yet. The built data is checked" + nl +
     "against the stock map's own (the definition read back from your map equals the stock one), but whether the game" + nl +
     "accepts it can only be seen by loading the map once." + nl + nl +
     "`data/asset_catalog/iw8_scriptables.tsv` lists all 837 stock definitions, where each lives and whether it can be" + nl +
     "built (column `verified`). Check Map lists every scriptable that would be left out, with the reason.")
s = one(s, a, b)
a = ("  from the stock door's own model and sounds plus a script, because a map cannot carry the game's real door object" + nl +
     "  yet (its definition lives inside stock map files). The build log says so every time.")
b = ("  from the stock door's own model and sounds plus a script. Turn on Edit > Preferences > Real stock scripted objects" + nl +
     "  and the badge goes away: the prefab then builds the game's real door, copied from the stock map (not seen in game" + nl +
     "  yet). The build log says which one was built.")
s = one(s, a, b)
save(p, s)

# ---------------- TEST CHECKLIST
p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: camera view speed and prefab models (2026-11-11 late), editor only, not seen'
sec = [
 '## In game: real stock doors and objects (2026-11-12), not seen in game: the one look that is needed',
 '',
 'Everything else in this list can wait. This one cannot be settled without the game, and one load answers it.',
 '',
 '- [ ] Edit > Preferences: tick "Real stock scripted objects". In the Prefabs dock the stock door no longer has',
 '      the FALLBACK badge.',
 '- [ ] In a small map place the prefab "Stock door, painted wood, single" and build (F7). The log says "main',
 '      contents" for the door and "carry their definition from the stock zone mp_backlot2".',
 '- [ ] Load the map. Say which of these happens: (a) the map loads and the door opens with the "Open Door" prompt,',
 '      (b) the map loads but the door is missing, does not open or has no collision, (c) the map does not load',
 '      (what does the game show).',
 '- [ ] If (c): untick the switch and build again to get the previous behaviour back.',
 '',
]
s = one(s, a, nl.join(sec) + nl + a)
save(p, s)

# ---------------- PROGRESS
p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
add = [
 '  - 2026-11-12, REAL STOCK SCRIPTED OBJECTS (research 86 step F3 done offline):',
 '    Converter commit 02182e8a (branch iw8radiant/scriptables, fast-forwarded into the live iw8radiant/build-extraction,',
 '    live zonetool.exe rebuilt; old exe out/zt_backup/zonetool_04995144.exe). New in the fork: zone/writer',
 '    asset_closure / zone_asset_name / rewrite_fastfile with a prepare hook; convert.cpp CarryScriptableDefinitions,',
 '    NewAdditions, writeZone prepare; ScriptableInstance::carryZone (JSON carry_zone).',
 '    Editor: setting build/realScriptables (Preferences checkbox, env IW8R_REAL_SCRIPTABLES=1/0 overrides for one',
 '    run), PrefabBrowser::realScriptables / buildSupport, CompileOptions::carryScriptables,',
 '    ScriptableDefinition::carryZone / carryable / door, MapScriptExtras::stockDoors (system_init call).',
 '    Proofs: tools/scriptable_carry_test.py (door, cone, car equal the stock definitions read back with ACTS),',
 '    tools/scriptable_offline_test.py (no-carry maps byte-identical), out/scr-door (the door prefab end to end,',
 '    switch off = fallback, on = main contents, 0 failed).',
 '    Also: brush faces inside placed prefabs are textured in the camera (scene.cpp addPrefabContents).',
 '    NEEDS ONE GAME LOOK: docs/TEST_CHECKLIST.md, first "In game" section about real stock doors.',
]
s = one(s, a, nl.join(add) + nl + a)
save(p, s)

# ---------------- research 86
p = 'docs/research/86_scriptable_objects_plan.md'
s, nl = load(p)
lines = s.split(nl)
for step, text in (('F3', 'done offline, commit 02182e8a (carried from the stock zone in memory; see "Step F3: done" at the end)'),
                   ('F4', 'done with F3: the physics asset and detail collision of the door model are part of what is carried'),
                   ('E2', 'done: `scriptable_instances` with `carry_zone`, and the `system_init` call when a door is placed')):
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + step + ' |')]
    assert len(hit) == 1, step
    cells = lines[hit[0]].split(' | ')
    cells[-1] = text + ' |'
    lines[hit[0]] = ' | '.join(cells)
s = nl.join(lines)
add = '''
## Step F3: done offline (2026-11-12)

Nothing here has been seen in game.

The section "Step F3, for whoever continues" above is superseded: the fork already loads every map zone it builds
and writes it again with its generic writer (`writeZone` in `convert.cpp`, `rewrite_fastfile`), and that writer takes
additional definitions that are in memory. So a stock definition is carried like this:

1. The instance in the collision JSON names the stock zone (`carry_zone`, for example `mp_backlot2`). The native map
   body writes that instance with a null definition pointer and no name reference.
2. `CarryScriptableDefinitions` (`convert.cpp`) loads the stock zone with the fork's loader (about a second), finds
   the definition, and asks the new `asset_closure` for everything it uses that the zone holds in full: a scratch
   zone that lists only the definition is written with all of the stock zone's assets at hand, and what the writer
   emitted is the closure. For the wooden door: 1 scriptable, 2 models, 5 model surfaces, 1 physics asset, 1 detail
   collision. The definition names its two materials by reference; those are in the zone's techsets companion
   (`techsets_mp_backlot2`), whose closure gives 2 materials and 1 technique set and names 7 images, which are back
   in the map zone. The two closures are followed until neither names anything the other holds.
3. What is still only named must be in a zone that is always loaded. The build checks it: 12 shaders in
   `code_post_gfx`, technique sets in `techsets_common*`; a name found nowhere is a warning. Engine images
   (`$white`, `$black`) are skipped.
4. When the map zone is loaded to be written again, a prepare hook adds the carried assets that the zone does not
   already hold and points each carried instance's `initialDef` at the stock definition in memory; the writer then
   writes the definition at its first use, as stock zones have it. The techsets zone gets its additions the same
   way. The main zone gets a name reference entry per carried material (the converter's rule: every techsets-zone
   material has a peer in the main zone).
5. Streamed data: the carried images and model surfaces keep their stream keys. Their payload is in the game's
   global packs (`pak_launch*.xpak`, `pak_magma*.xpak`, `pak_tu*.xpak`), not in a per-map pack (those are 32 KB
   stubs), so it is available in any map.

Editor: off by default behind Edit > Preferences > Real stock scripted objects (`build/realScriptables`). A
definition is built when it is `verified` and either always loaded or held by an `mp_` zone (317 definitions). A
placed door (usable sparse occluder) makes the map script call `scripts\\engine\\scriptable_door::system_init()`
right after the load script. Prefab bundles get the features `scriptable_instances`, `scriptable_def_carry` and
`stock_script_calls` with the switch on, so the shipped stock door builds its main contents.

Proof:

- `tools/scriptable_carry_test.py`: for `scriptable_door_wooden_panel_03_painted_mp` (mp_backlot2),
  `com_trafficcone_01` and `veh8_civ_lnd_decho_rebel_darkblue` (mp_shipment; 150 assets with animations, effects
  and a light) the definition ACTS reads from the built map zone equals the stock one field for field (load
  addresses and zone-relative script-string indices masked); map and techsets zones load with 0 failed; the
  instances name their definition without a comma, as stock maps do for their own definitions.
- `tools/scriptable_offline_test.py --new <exe>`: a map without carried definitions gives byte-identical zones.
- `out/scr-door/mp_scrdoor.map`: the stock door prefab, a cone and a chevron through the editor's headless build:
  switch off gives the fallback door and leaves the cone out; switch on gives "main contents", two stock zones
  read in 4.7 s, all loader lines 0 failed, and the converted script keeps the `system_init` call.

What only the game can settle (one load of a map with the door prefab):

- whether the loader accepts the instances at all (single-leaf spatial tree, no reserved dynent tables);
- whether a door definition inside a custom map zone behaves (prompt, swing, collision);
- for cars and other large definitions: effects and sounds they name from common zones.
'''
s = s.rstrip() + nl + add.replace('\n', nl)
save(p, s)
print('ok')
