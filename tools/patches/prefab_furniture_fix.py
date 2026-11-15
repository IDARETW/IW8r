import io
import re

# Map-agent report 2026-11-14 ("Warehouse shelf prefab does not match its listed arrangement"; "other prefab groups
# require the same bounds and orientation review") and tools/prefab_bounds_audit.py: the furniture prefabs of the
# bundled library placed their small items at guessed heights (40 / 60 / 80) that float, and two used a stand-in for
# their main piece (props_office_metal_desk: a 7 x 12 tablet; props_kitchen_counter: a propane stove). Each item now
# sits on its support's top (the model cache's bounds) inside its footprint; the two get a real desk / cabinet with a
# countertop.

LIB = 'E:/IW8-Radiant/data/prefabs/iw8r/props/'


def model_block(n, model, origin, angles='0 0 0'):
    return '// entity %d\n{\n"classname" "misc_model"\n"model" "%s"\n"origin" "%s"\n"angles" "%s"\n}\n' % (n, model, origin, angles)


def rewrite(name, items):
    p = LIB + name + '.map'
    text = io.open(p, encoding='utf-8').read()
    head = text[:text.index('// entity 1')] if '// entity 1' in text else text
    body = ''.join(model_block(i + 1, m, o) for i, (m, o) in enumerate(items))
    io.open(p, 'w', encoding='utf-8', newline='\n').write(head + body)


# A teacher's desk, top at z 35.2, footprint x -17..15, y -36.5..36.5 (origin -1 0 0).
rewrite('props_office_desk_set', [
    ('furniture_teachers_desk_01', '-1 0 0'),
    ('desktop_fan_fn_01', '-9 -24 35.2'),
    ('lag_portable_radio', '5 20 35.2'),
    ('accessory_table_vintage_ashtray', '-1 0 35.2'),
    ('me_kitchen_coffee_set_01', '8 -22 35.2'),
])
# A metal office desk (tm/un_office_desk_01_short: x 0..60, y -32..0, top z 36) centred on the origin; the tablet on it.
rewrite('props_office_metal_desk', [
    ('tm/un_office_desk_01_short', '-30 16 0'),
    ('desktop_fan_fn_01', '-15 0 36'),
    ('accessory_table_vintage_ashtray', '12 4 36'),
    ('p7_desk_metal_military_03_tablet', '2 -8 36'),
])
# A kitchen cabinet (top z 38) with a countertop (top z 38.8 + 0.77); the stove, the pot on it, the rest beside.
rewrite('props_kitchen_counter', [
    ('tm/me_kitchen_cabinet_long_01', '0 0 0'),
    ('tm/kitchen_countertop_01_64', '0 0 38.87'),
    ('equipment_propane_burner_stove_01_white', '0 -18 39.6'),
    ('kitchen_01_pot_copper_01', '0 -18 53.2'),
    ('kitchen_01_pan_01', '-6 14 39.6'),
    ('kitchen_knife_block_01', '10 0 39.6'),
    ('kitchen_plastic_syrup_bottle_01', '10 26 39.6'),
    ('me_kitchen_coffee_set_01', '-8 27 39.6'),
])
# The pallet stack's top is z 33.
rewrite('props_warehouse_pallets', [
    ('tm/com_pallet_stack', '-10 0 0'),
    ('crate_plastic_sml_01_red', '-10 0 33'),
    ('tm/container_water_jug_01_blue', '70 0 0'),
    ('container_paint_can_01', '-70 0 0'),
])
# The plank shelf: x -63..0, y -65..62, top z 1.5.
rewrite('props_storage_wood_shelf', [
    ('storage_shelf_beam_wood_plank', '0 0 0'),
    ('container_paint_can_01', '-20 0 1.5'),
    ('tm/container_water_jug_01_blue', '-40 20 1.5'),
])
print('ok')
