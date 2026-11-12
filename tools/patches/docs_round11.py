import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 7.14 |')]
assert len(hit) == 1
status = ('partial (2026-11-11, owner request: "a custom dvar to enable a debug overlay of objects and other things in the scene", "build it around the idea of using it for map development debugging"; research 78). '
          'Client side written and registered in the client source, compile-checked, NOT built and not run: DW_MapDebug.cpp adds the dvar iw8r_mapDebug (bit mask: 1 entity labels and axis markers, 2 trigger wire boxes, 4 a where-am-I panel with the position in map units, the map, entity counts and the audio zone, 8 a crosshair inspector with the surface type and flags of what you look at, 16 boxes for every entity with bounds, 32 players and AI too), '
          'iw8r_mapDebugDist, iw8r_mapDebugMax, iw8r_mapDebugFilter and the command iw8r_mapDebugDump (the entity list written to the client log). It reads the server entity array, so it works in a match you host. '
          'Editor side done offline: the Live Link dock has a Debug overlay row (five presets, a filter, Selected = filter to the selected entity, Compare with game = asks for the dump, reads it from the log and lists which named map entities are missing, in another place or in a different number, with a button that moves the editor camera to the player); core map_debug_dump, 3 tests; GUI checks. '
          'Not shown by the overlay: the material name and physics material under the crosshair, the visibility cell and the reflection probe. NOT SEEN in game')
assert '|' not in status
lines.insert(hit[0] + 1, '| 7.15 | In-game map debug overlay: entities, triggers, position, crosshair surface, and a game-to-map entity comparison | new | ' + status + ' | M, **U** |')
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: game modes of a custom map (2026-11-11), needs the new client build, not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: map debug overlay (2026-11-11), needs the new client build, not seen in game',
 '',
 'Host a private match on your map. The full list with pass conditions is in `docs/research/78_map_debug_overlay.md`,',
 'section 6. The short form, from the editor:',
 '',
 '- [ ] Live Link dock > Debug overlay > "Where am I": a panel in the game shows your position. The feet height',
 '      matches the floor height in the editor.',
 '- [ ] "Entities and triggers": labels sit on your script_models and triggers; triggers have an orange wire box of',
 '      the size of the trigger brush.',
 '- [ ] Select an entity that has a targetname in the editor, press Selected: only that entity is labelled in game.',
 '- [ ] "Entities and crosshair": the surface row changes with what you look at and matches the surface type you set',
 '      in the editor. If the game freezes or crashes when this is switched on, say so and do not use it again.',
 '- [ ] Compare with game: a window lists the game\'s entity count and any named map entity that is missing or at',
 '      another place. "Move editor camera to the player" puts the editor camera where you stand.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = '## 10. When something goes wrong'
assert s.count(a) == 1
sec = [
 '### Debug overlay in the game',
 '',
 'The Live Link dock has a **Debug overlay** row. It tells the game to draw what your map contains while you play it:',
 '',
 '| Preset | What the game shows |',
 '|---|---|',
 '| Where am I | A panel with your position in map units (the same numbers as in the editor), the map, entity counts and the audio zone |',
 '| Entities and triggers | A label and an axis marker on each entity near you (class, name, model, distance) and a wire box on each trigger |',
 '| Entities and crosshair | The same, and what the crosshair is on: surface type, flags and the entity hit |',
 '| Everything | A box on every entity that has bounds, players and AI included |',
 '',
 '- The filter box limits the labels to entities whose class, name, target or model contains one of the words. A word',
 '  that starts with `-` hides matches. **Selected** filters to the entity selected in the editor.',
 '- **Compare with game** asks the game for its entity list and shows which named entities of your map are missing in',
 '  the running match, at another place, or there in a different number. Its window can move the editor camera to',
 '  where you stand in the game.',
 '- In the game console the same things are the dvars `iw8r_mapDebug` (a number: 1 labels, 2 trigger boxes, 4 panel,',
 '  8 crosshair, 16 all boxes, 32 players; add them up), `iw8r_mapDebugDist`, `iw8r_mapDebugMax`,',
 '  `iw8r_mapDebugFilter`, and the command `iw8r_mapDebugDump`.',
 '',
 'You must host the match, and the game client must be one built with the overlay. Not seen in game yet.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - MAP DEBUG OVERLAY (roadmap 7.15, research 78): client DW_MapDebug.cpp (Opus agent; in E:\\DW\\IW8, registered,',
 '    compile-checked, not built). Editor: core map_debug_dump (presets, filter cleaning, dump parser,',
 '    compareDumpWithMap), LiveLinkPanel Debug overlay row + requestEntityDump / dumpReport. The overlay also is the',
 '    tool for the open questions: crosshair surface type (F.12), audio zone (F.7), entity survival (spawns, corners).',
 '    Cover card stall limit: devmap::kCoverStallMs (150 s). Tests: core 1077 / 0, GUI 656 / 0.',
 '  - WHAT THE OWNER MUST DO for all three client features (cold start, game modes, overlay): build the client from',
 '    E:\\DW\\IW8 and install it. This session cannot: the non-publishing build is blocked in prepare (memory',
 '    dw-client-build-rules) and installing writes into the game folder.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
