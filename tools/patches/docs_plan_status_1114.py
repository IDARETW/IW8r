import io

# PLAN_REMAINING status lines after the work of 2026-11-14 (specular maps, fitted occluders) and the decision on
# plan 2.3 (local occlusion updates for edited stock maps).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


P = 'docs/PLAN_REMAINING.md'
patch(P, '''117 section 5). Open: specular-map materials, own-map models against the built light grid.''',
      '''117 section 5). Normal and specular maps of own materials DONE 2026-11-14 (the converter's SourceSunSpecular rule;
see PROGRESS). Open: own-map models against the built light grid.''')
patch(P, '''**2.3 Stock-map edits keep the tome correct.**
''', '''**2.3 Stock-map edits keep the tome correct.** DECISION 2026-11-14: not built. Removing stock world geometry keeps
opening the whole map's occlusion (everything is drawn, as it has been since research 96). That costs frame time on
such a map and nothing else. A local update would rewrite single tiles of a stock tome and their links to the
neighbouring tiles; an error there makes objects vanish in the game, which only a playtest shows, and the owner does
no playtests. The safe form stays until someone can look in the game. The decoder (2.1) and the bake (2.4) hold what
a later attempt needs.
''')
patch(P, '''the opaque box brushes, the conservativeness test, a real build read back). Open: non-box occluders and models as
occluders, the per-brush occluder key, gates,''', '''the opaque box brushes, the conservativeness test, a real build read back). 2026-11-14 also: slanted and many-sided
opaque brushes count by the box inside them (core occluder_box). Open: thin slanted walls, terrain and models as
occluders, the per-brush occluder key, gates,''')
patch('docs/HANDOVER.md', '''box inside them); local updates of a stock map's occlusion data (removing stock geometry opens the whole map's
  occlusion).''', '''box inside them). Removing stock world geometry opens the whole map's occlusion: everything is drawn, which
  costs frame time on that map and nothing else. A local update was decided against (a mistake there makes objects
  vanish in the game, and only a playtest shows it).''')
print('ok')
