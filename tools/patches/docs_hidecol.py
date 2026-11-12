import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def rep(s, nl, a, b, p):
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    return s.replace(a, b)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''   leave the view and the rebuilt map draws them at size zero; their collision stays. File > Show All Stock Props
''', '''   leave the view and the rebuilt map draws them at size zero; their collision is taken out too (on the small maps;
   a Warzone map keeps prop collision in its collision tiles, which stay as they are). File > Show All Stock Props
''', p)
save(p, s)

p = 'docs/research/88_stock_map_editing.md'
s, nl = load(p)
s = rep(s, nl, '''5. The Warzone maps with the same command (research 87).
''', '''5. The Warzone maps with the same command (research 87).
6. Done 2026-11-12: a hidden prop loses its collision too. `editzone` `remove_static_model_collision`
   [{model, origin}] (fork 63d4f338) removes the instance of that model at that place (half a unit) from the
   collision map's static model collision lists (model names compared without their folder: the world says
   `tm/<name>`, the lists `<name>`); the editor sends the hidden props of the stock reference. Proof:
   `tools/smodel_collision_remove_test.py` on mp_shipment (3 of 4085 instances removed, exactly those, the props at
   scale 0, ACTS like stock). The big maps keep this collision in their collision tiles (research 90), not edited.
''', p)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 11.5 |'):
        a = 'partial (hide built offline 2026-11-12, see the update; removal and collision open)'
        assert a in line, line[:300]
        lines[i] = line.replace(a, 'done offline (2026-11-12): hide (scale 0) and the hidden props\' collision removed on the small maps (research 88 next step 6, `tools/smodel_collision_remove_test.py`); the big maps keep prop collision in their tiles. Not seen in game. Earlier: partial (hide built offline 2026-11-12, see the update; removal and collision open)')
save(p, nl.join(lines))

p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', '''    HIDDEN PROPS LOSE COLLISION (research 88 step 6): converter live at 63d4f338 (old exe zonetool_9f1472de.exe):
    editzone remove_static_model_collision; editor sends the hidden props of the stock reference
    (StockPropCollision). Proof tools/smodel_collision_remove_test.py.
    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', p)
save(p, s)
print('ok')
