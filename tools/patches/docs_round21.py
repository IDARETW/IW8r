import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

# ---------------- ROADMAP
p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def append(row, text):
    assert '|' not in text
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    cells = lines[hit[0]].split(' | ')
    assert len(cells) == 5, (row, len(cells))
    cells[3] = cells[3].rstrip() + ' ' + text
    lines[hit[0]] = ' | '.join(cells)
append('5.8', 'Update (2026-11-11 night): the decal producer is in the live converter (fork 8ff480fa, old exe kept in out/zt_backup). A decal that names a material of the custom library now gets a twin material in the form the producer accepts '
       '(written as <name>_dcl: effect-decal metadata, technique set wc_l_sm_b0c0 with n0 / s0 for a normal / specular image, camera region decal, colour tint; the picture keeps its alpha). '
       'Showcase 2 builds its decal station: the converter log says 1 decal volume, 1 material built from the extraction, 0 failed. Core test compile_decal_volume_custom_material_is_decal_form. NOT SEEN in game.')
append('6.14', 'Update (2026-11-11 night, research 86): scriptable instances are built. Decoded from all 21 stock multiplayer maps: every dynamic entity of a stock map belongs to a scriptable (one instance entry for most definitions, one physics entry per dynamic entity the definition requires); '
       'the map-wide counts follow from the definitions (server instances, fixed parts and usable parts reproduce the stock values on all 21 maps). '
       'Converter (fork 04995144, in the live converter; old exe in out/zt_backup): instance records, counts, script strings, name-only definition references and the dynamic-entity entries, from scriptable_instances in the collision JSON; a map without scriptables builds byte-identical zones. '
       'Editor: point class scriptable (key scriptable = the definition name), catalogue data/asset_catalog/iw8_scriptables.tsv (837 stock definitions with their numbers; tools/build_scriptable_catalog.py). '
       'What can be placed today: hardpoint_chevron only (the one definition that is in an always-loaded zone and fully known; stock maps reference it the same way). Doors, cones and cars live in stock map zones: the build leaves them out with a warning until a definition can be carried into a map (step F3, not done). '
       'Offline proof: tools/scriptable_offline_test.py (38 checks, ACTS reads back the records, counts and chains, 0 failed); core tests compile_iw8_scriptable_instances and scriptable_catalog_shipped_file. NOT SEEN in game.')
save(p, nl.join(lines))

# ---------------- USER GUIDE
p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = ('The build log says how many decals were written. Decals need a converter with the decal producer, which is not' + nl +
     'installed yet: until then the map builds and plays, without the decals. Not seen in game.')
assert s.count(a) == 1
b = ('The build log says how many decals were written. A material of your library is built a second time in the form' + nl +
     'decals need (the build names it `<material>_dcl`); you do not have to prepare anything. Decals need a converter' + nl +
     'with the decal producer (the one this project uses has it since 2026-11-11). Not seen in game.' + nl + nl +
     '### Stock scripted objects (scriptables)' + nl + nl +
     'A scriptable is one of the game\'s own objects with built-in behaviour. Place the point entity `scriptable` and' + nl +
     'set its `scriptable` key to the name of the stock definition. Optional keys: `angles`, `targetname`, `target`,' + nl +
     '`script_noteworthy`, `script_linkname`, `script_linkto`.' + nl + nl +
     'What can be placed today is small: `hardpoint_chevron`, the marker of a Hardpoint zone. It is the only definition' + nl +
     'that the game always has loaded and whose build numbers are fully known. Doors, traffic cones and cars live inside' + nl +
     'stock map files; the build leaves such an entity out and says so in the log. The file' + nl +
     '`data/asset_catalog/iw8_scriptables.tsv` lists all 837 stock definitions and where each one lives.' + nl +
     'Not seen in game.')
s = s.replace(a, b)
save(p, s)

# ---------------- TEST CHECKLIST
p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: stock maps show their geometry (2026-11-11 night), editor only, not seen'
assert s.count(a) == 1
sec = [
 '## In game: decals and hardpoint markers (2026-11-11 late), not seen in game',
 '',
 '- [ ] Showcase 2 (`samples/dev/mp_iw8r_show2.map`), the decal station: the picture is projected on the floor.',
 '- [ ] Optional, only if you want to try scriptables: place the entity `scriptable` with `scriptable` set to',
 '      `hardpoint_chevron`, build and load the map. Pass: the map loads. If it does not load, say what the game',
 '      shows; removing the entity gives the previous build back.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

# ---------------- PROGRESS
p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - LATE 2026-11-11 (goal: finished product, no more game tests):',
 '    DECALS: the editor writes a decal-form twin (<name>_dcl) of a library material named by an iw8r_decal',
 '    (compile_iw8.cpp customDecalMaterialJson, decalCustom); live converter = fork 8ff480fa then 04995144.',
 '    SCRIPTABLES (research 86): converter commit 04995144 on iw8radiant/scriptables, fast-forwarded into the live',
 '    iw8radiant/build-extraction and built (old exes: out/zt_backup/zonetool_062ec6b4.exe, zonetool_8ff480fa.exe).',
 '    Editor: src/core/scriptable_catalog.*, CompileOptions::scriptables, class scriptable in',
 '    data/iw8_entity_defs_overrides.json, catalogue data/asset_catalog/iw8_scriptables.tsv. Tools:',
 '    tools/acts_export.py, build_scriptable_catalog.py, scriptable_counts.py, scriptable_dynents.py,',
 '    scriptable_offline_test.py (exports kept in out/scriptable_study, about 21 maps). Only hardpoint_chevron is',
 '    placeable; F3 (carry a definition into a map: doors, cones, cars) is NOT done and needs game tests.',
 '    FIXES: stock model used only by dynamic_prop is now fetched for the build (compile_panel.cpp); File > Package',
 '    packs placed prefab bundles whole (map_package.cpp resolveBundle / addBundle); a duplicated block in',
 '    compile_panel.cpp removed. build_cache needs no change: the converter cache key hashes the whole extraction.',
 '    Shell note: in this Bash tool a heredoc turns two backslashes into one and can fail on quotes; write patch',
 '    scripts with the Write tool (tools/patches/*.py).',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
