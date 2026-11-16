import io

# Compact showcase fixes from the owner's checklist (2026-11-15):
#  B1 "some objects dont belong in a cube shape, like the grass": ground materials as low pads, wall materials as
#     upright panels, the container metal stays a box;
#  B2 "the flat patch clips with the ground": the flat patch and the grass bump stand above the floor;
#  B5 "only one door pane": the double door's right leaf beside the left one;
#  B6 "lamp floats in the air": the tall floor lamp prefab instead of the table lamp;
#  B9 "ladder model not seen, just the ladder climbing area": the tower's ladder is a ladder prefab (model + volume).

P = 'E:/IW8-Radiant/tools/make_compact_showcase.py'
s = io.open(P, encoding='utf-8', newline='').read()


def sub(a, b):
    global s
    if b in s and a not in s:
        return
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


sub('''    # 1 materials: 64-unit blocks.
    label(1, "materials")
    for i, m in enumerate(MATERIALS):
        x, y = at(1, i)
        if "chainlink" in m:
            world.append(box(x, y, 0, x + 64, y + 8, 96, m))  # a fence panel (cut-out)
        else:
            world.append(box(x, y, 0, x + 64, y + 64, 64, m))''', '''    # 1 materials, each in the shape it is used for: ground as a low pad, walls and fences as upright panels, the
    # container metal and the custom checker as boxes.
    label(1, "materials")
    GROUND = ("grass", "sand", "terrain", "dirt", "gravel")
    for i, m in enumerate(MATERIALS):
        x, y = at(1, i)
        if any(g in m for g in GROUND):
            world.append(box(x, y, 0, x + 64, y + 64, 4, m))       # a ground pad
        elif "container" in m or m.startswith("iw8r_dev/"):
            world.append(box(x, y, 0, x + 64, y + 64, 64, m))      # a box
        else:
            world.append(box(x, y, 0, x + 64, y + 12, 96, m))      # a wall or fence panel''')
sub('''    world.append(mesh([[(x + c * 32, y + r * 32, round(16 * math.sin(c) * math.cos(r))) for r in range(4)] for c in range(4)],
                      "tmo/ee_foliage_grass_lm_short_green", 4, 4))  # terrain bump''', '''    world.append(mesh([[(x + c * 32, y + r * 32, 2 + round(8 * (1 + math.sin(c * 1.3) * math.cos(r * 1.1)))) for r in range(4)] for c in range(4)],
                      "tmo/ee_foliage_grass_lm_short_green", 4, 4))  # terrain bump, above the floor''')
sub('''    world.append(mesh([[(x + c * 48, y + r * 48, 0) for r in range(2)] for c in range(2)], FLOOR, 2, 2))  # flat''',
    '''    world.append(mesh([[(x + c * 48, y + r * 48, 2) for r in range(2)] for c in range(2)], FLOOR, 2, 2))  # flat, 2 units up''')
sub('''    for i, d in enumerate(SCRIPTABLES):
        x, y = at(5, i * 1.6)
        entity([("classname", "scriptable"), ("scriptable", d), ("origin", f"{x + 32} {y + 32} 0"), ("angles", "0 90 0")])''',
    '''    for i, d in enumerate(SCRIPTABLES):
        x, y = at(5, i * 1.6)
        entity([("classname", "scriptable"), ("scriptable", d), ("origin", f"{x + 32} {y + 32} 0"), ("angles", "0 90 0")])
        if d.endswith("_double_l"):
            # Its right leaf: hinged at the other jamb (each leaf is 27 units wide along its own +x), turned round.
            entity([("classname", "scriptable"), ("scriptable", d[:-2] + "_r"), ("origin", f"{x + 32} {y + 32 + 54} 0"), ("angles", "0 270 0")])''')
sub('''"iw8r/lamps/lamp_short_table.map"''', '''"iw8r/lamps/lamp_tall_floor.map"''')
sub('''    world.append(box(x, y, 0, x + 96, y + 96, 192, WALL))           # tower
    world.append(box(x - 8, y + 32, 0, x, y + 64, 192, "ladder"))   # its ladder (-x face)''',
    '''    world.append(box(x, y, 0, x + 96, y + 96, 160, WALL))           # tower
    # Its ladder on the -x face: the 160-unit wooden ladder prefab (model and climbing volume; the prefab's wall side
    # is its +y, turned to face the tower).
    entity([("classname", "misc_prefab"), ("model", "iw8r/ladders/ladder_old_wood_160.map"), ("origin", f"{x - 10} {y + 48} 0"),
            ("angles", "0 270 0")])''')
s = s.replace('  1 materials       stock surfaces, a cut-out fence, a custom material, a custom cut-out grate',
              '  1 materials       stock surfaces in their shapes (ground pads, wall panels, a container box), a cut-out fence,\n'
              '                    a custom material, a custom cut-out grate')
s = s.replace('  5 scriptables     stock scripted objects: a door, a traffic cone, a radio, a tyre',
              '  5 scriptables     stock scripted objects: a double door (both leaves), a traffic cone, a radio, a tyre')
s = s.replace('  6 prefabs         misc_prefab placements from data/prefabs (cover, lamp, door, stock barrel)',
              '  6 prefabs         misc_prefab placements from data/prefabs (cover, floor lamp, door, stock barrel)')
s = s.replace('  9 gameplay        spawns (TDM and DM), a trigger, a scripted brush model, a ladder tower and mantle ledges',
              '  9 gameplay        spawns (TDM and DM), a trigger, a scripted brush model, a tower with a ladder prefab, mantle ledges')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
