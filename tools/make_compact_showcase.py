"""Write samples/dev/iw8r_compact_showcase.map: a small test map (1536 x 1408 units, a quarter of the dev showcase's
floor) with nine labelled rows of objects, for the editor checklist (docs/EDITOR_TEST_CHECKLIST.md).

Rows, south (row 1, beside the player start) to north, items west to east; each row has a script_origin
"row_<n>_<name>" at its west end:
  1 materials       stock surfaces in their shapes (ground pads, wall panels, a container box), a cut-out fence,
                    a custom material, a custom cut-out grate
  2 shapes          tool brushes (caulk, clip, ladder), ramps, stairs, three patches
  3 models          stock models as misc_model (static props), one scaled, one rotated
  4 live_models     script_models the map script spins / bobs / blinks, a custom model, a destructible pallet
  5 scriptables     stock scripted objects: a double door (both leaves), a traffic cone, a radio, a tyre
  6 prefabs         misc_prefab placements from data/prefabs (cover, floor lamp, door, stock barrel)
  7 lights          point lights (white, red, blue), a spot light, a reflection probe
  8 effects_sound   fire, smoke, sparks, heat haze (distortion), two sound emitters
  9 gameplay        spawns (TDM and DM), a trigger, a scripted brush model, a tower with a ladder prefab, mantle ledges

Reuses the dev showcase's custom assets beside it (materials/, models/) and writes its map script
(iw8r_compact_showcase.gsc, the dev showcase's spin / bob / blink). Stock names are Replay 1.20 assets from
data/asset_catalog.
Usage: py -3 -I tools/make_compact_showcase.py [-o samples/dev/iw8r_compact_showcase.map]
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_dev_showcase import FLOOR, WALL, box, mesh, wedge, write_custom_assets  # noqa: E402

ROW = 144           # distance between rows (y)
COL = 128           # distance between items in a row (x)
X0, Y0 = -640, -560  # first item of row 1
W, H = 768, 704     # room half-extents
CEIL = 448

MATERIALS = [
    "tm2co/storage_shipping_container_metal_painted_01_second_green_wet", "tmo/building_wall_corner_brick_01",
    "tm2co/concrete_cracked_wall_damaged_01_rvl", "tmo/ee_foliage_grass_lm_short_green", "tmo/periph_terrain_sand_billow_01",
    "tmo/building_metal_corrugated_set_01", "tmo/cp_disco_chainlink_fence_01_onesided",  # cut-out
    "iw8r_dev/checker",  # custom (materials/iw8r_dev_checker.iw8mat.json)
]
MODELS = ["military_hq_crate_01_proxy", "tm/container_barrel_plastic_01_closed", "tm/furniture_cafe_chair_01",
          "tm/ee_light_fixture_lamp_short_02", "tm/accessory_plant_pot_fake_2", "tm/com_pallet_2", "com_junktire"]
SCRIPTABLES = ["scriptable_door_wood_ornate_01_blue_double_l", "com_trafficcone_01", "equipment_military_radio_old_02", "com_junktire"]
PREFABS = ["iw8r/cover/cover_barrel_group.map", "iw8r/lamps/lamp_tall_floor.map", "iw8r/doors/door_single_metal_frame.map",
           "stock/barrel/tm_container_barrel_plastic_01_closed_v01.map"]
EFFECTS = ["vfx/iw8/veh/scriptables/shared/vfx_veh_fire_linger_med", "vfx/iw8/veh/scriptables/vfx_smoke_light_ch",
           "vfx/core/expl/spark_death", "vfx/iw8/level/safehouse/vfx_safehouse_finale_drone_heat_dist"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=os.path.join("samples", "dev", "iw8r_compact_showcase.map"))
    a = ap.parse_args()
    out_dir = os.path.dirname(os.path.abspath(a.output))
    stem = os.path.splitext(os.path.basename(a.output))[0]
    world, ents = [], []

    def entity(kvs, prims=()):
        ents.append((kvs, prims))

    def at(row, i):
        return X0 + i * COL, Y0 + (row - 1) * ROW

    def label(row, name):
        entity([("classname", "script_origin"), ("targetname", f"row_{row}_{name}"), ("origin", f"{X0 - 96} {Y0 + (row - 1) * ROW + 32} 16")])

    # The room: floor, four walls, a sky ceiling (sealed).
    world.append(box(-W, -H, -16, W, H, 0, FLOOR))
    world.append(box(-W - 16, -H, 0, -W, H, CEIL, WALL))
    world.append(box(W, -H, 0, W + 16, H, CEIL, WALL))
    world.append(box(-W, -H - 16, 0, W, -H, CEIL, WALL))
    world.append(box(-W, H, 0, W, H + 16, CEIL, WALL))
    world.append(box(-W - 16, -H - 16, CEIL, W + 16, H + 16, CEIL + 16, "sky"))

    # 1 materials, each in the shape it is used for: ground as a low pad, walls and fences as upright panels, the
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
            world.append(box(x, y, 0, x + 64, y + 12, 96, m))      # a wall or fence panel
    x, y = at(1, 8)
    world.append(box(x, y, 0, x + 64, y + 8, 96, "iw8r_dev/grate"))  # custom cut-out

    # 2 shapes: tool brushes, ramps, stairs, patches.
    label(2, "shapes")
    for i, m in enumerate(["caulk", "clip"]):
        x, y = at(2, i)
        world.append(box(x, y, 0, x + 64, y + 64, 64, m))
    x, y = at(2, 2)
    world.append(box(x, y, 0, x + 64, y + 16, 128, "ladder"))
    for i, h in enumerate([32, 64]):
        x, y = at(2, 3 + i)
        world.append(wedge(x, y, x + 96, y + 64, h, FLOOR))
    x, y = at(2, 5)
    for k in range(6):
        world.append(box(x + k * 16, y, 0, x + k * 16 + 16, y + 64, (k + 1) * 12, FLOOR))
    x, y = at(2, 6)
    world.append(mesh([[(x + c * 48, y, 0), (x + c * 48, y + 48, 96 if c % 2 == 0 else 72), (x + c * 48, y + 96, 0)] for c in range(3)],
                      WALL, 3, 3))  # arch
    x, y = at(2, 7.2)
    world.append(mesh([[(x + c * 32, y + r * 32, 2 + round(8 * (1 + math.sin(c * 1.3) * math.cos(r * 1.1)))) for r in range(4)] for c in range(4)],
                      "tmo/ee_foliage_grass_lm_short_green", 4, 4))  # terrain bump, above the floor
    x, y = at(2, 8.3)
    world.append(mesh([[(x + c * 48, y + r * 48, 2) for r in range(2)] for c in range(2)], FLOOR, 2, 2))  # flat, 2 units up

    # 3 models: static props.
    label(3, "models")
    for i, m in enumerate(MODELS):
        x, y = at(3, i * 1.2)
        kv = [("classname", "misc_model"), ("model", m), ("origin", f"{x + 32} {y + 32} 0"), ("angles", f"0 {i * 30} 0")]
        if i == 1:
            kv.append(("modelscale", "1.5"))
        entity(kv)

    # 4 live models: spun / bobbed / blinked by the map script, a custom model, a destructible.
    label(4, "live_models")
    for i, m in enumerate(MODELS[:3]):
        x, y = at(4, i * 1.3)
        entity([("classname", "script_model"), ("model", m), ("targetname", f"scripted_{i}"), ("origin", f"{x + 32} {y + 32} 0")])
        entity([("classname", "script_origin"), ("target", f"scripted_{i}"), ("origin", f"{x + 32} {y - 16} 8")])
    x, y = at(4, 5)
    entity([("classname", "script_model"), ("model", "iw8r_dev_crate"), ("targetname", "scripted_crate"), ("origin", f"{x + 32} {y + 32} 0"),
            ("angles", "0 30 0")])
    entity([("classname", "script_origin"), ("target", "scripted_crate"), ("origin", f"{x + 32} {y - 16} 8")])
    x, y = at(4, 6.5)
    entity([("classname", "script_model"), ("model", "tm/com_pallet_2"), ("targetname", "destructable"), ("origin", f"{x + 32} {y + 32} 0")])

    # 5 scriptables: stock scripted objects (the build copies their definitions from the stock maps).
    label(5, "scriptables")
    for i, d in enumerate(SCRIPTABLES):
        x, y = at(5, i * 1.6)
        entity([("classname", "scriptable"), ("scriptable", d), ("origin", f"{x + 32} {y + 32} 0"), ("angles", "0 90 0")])
        if d.endswith("_double_l"):
            # Its right leaf: hinged at the other jamb (each leaf is 27 units wide along its own +x), turned round.
            entity([("classname", "scriptable"), ("scriptable", d[:-2] + "_r"), ("origin", f"{x + 32} {y + 32 + 54} 0"), ("angles", "0 270 0")])

    # 6 prefabs.
    label(6, "prefabs")
    for i, p in enumerate(PREFABS):
        x, y = at(6, i * 1.8)
        entity([("classname", "misc_prefab"), ("model", p), ("origin", f"{x + 48} {y + 32} 0"), ("angles", "0 0 0")])

    # 7 lights and a probe.
    label(7, "lights")
    lights = [[("_color", "1 1 1"), ("radius", "256"), ("intensity", "1")],
              [("_color", "1 0.25 0.2"), ("radius", "192"), ("intensity", "1.5")],
              [("_color", "0.3 0.5 1"), ("radius", "192"), ("intensity", "1.5")],
              [("_color", "1 0.9 0.7"), ("radius", "320"), ("intensity", "2"), ("fov_outer", "60"), ("fov_inner", "35"), ("angles", "90 0 0")]]
    for i, kv in enumerate(lights):
        x, y = at(7, i * 1.7)
        entity([("classname", "light"), ("origin", f"{x + 32} {y + 32} 112")] + kv)
    x, y = at(7, 8)
    entity([("classname", "reflection_probe"), ("origin", f"{x + 32} {y + 32} 64")])

    # 8 effects and sounds.
    label(8, "effects_sound")
    for i, fx in enumerate(EFFECTS):
        x, y = at(8, i * 1.5)
        entity([("classname", "fx_origin"), ("fx", fx), ("origin", f"{x + 32} {y + 32} 8")])
    for i, (alias, loop) in enumerate([("emt_smokestack_lp", "1"), ("emt_light_fluorescent_hum1_int", "1")]):
        x, y = at(8, 6.5 + i * 1.5)
        entity([("classname", "sound_emitter"), ("soundalias", alias), ("loop", loop), ("origin", f"{x + 32} {y + 32} 32")])

    # 9 gameplay: spawns, a trigger, a scripted brush model, a ladder tower and mantle ledges.
    label(9, "gameplay")
    for i, cls in enumerate(["mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start", "mp_tdm_spawn", "mp_tdm_spawn", "mp_dm_spawn", "mp_dm_spawn"]):
        x, y = at(9, i * 0.5)
        entity([("classname", cls), ("origin", f"{x + 24} {y + 24} 16"), ("angles", "0 270 0")])
    x, y = at(9, 3.2)
    entity([("classname", "trigger_multiple"), ("targetname", "dev_trigger")], [box(x, y, 0, x + 96, y + 96, 96, "trigger")])
    x, y = at(9, 4.4)
    entity([("classname", "script_brushmodel"), ("targetname", "dev_door")], [box(x, y, 0, x + 16, y + 96, 112, WALL)])
    x, y = at(9, 5.2)
    world.append(box(x, y, 0, x + 96, y + 96, 160, WALL))           # tower
    # Its ladder on the -x face: the 160-unit wooden ladder prefab (model and climbing volume; the prefab's wall side
    # is its -y, turned 90 degrees to face the tower; the prefab is 10 deep, so its centre is 5 out from the face).
    entity([("classname", "misc_prefab"), ("model", "iw8r/ladders/ladder_old_wood_160.map"), ("origin", f"{round(x - 5, 2):g} {y + 48} 0"),
            ("angles", "0 90 0")])
    for i, h in enumerate([32, 56]):
        x, y = at(9, 6.6 + i)
        world.append(box(x, y, 0, x + 96, y + 96, h, FLOOR))         # mantle ledges

    # The start: south of row 1, looking north along the rows.
    entity([("classname", "info_player_start"), ("origin", f"0 {-H + 64} 16"), ("angles", "0 90 0")])
    entity([("classname", "mp_global_intermission"), ("origin", f"0 {-H + 96} 320"), ("angles", "35 90 0")])

    L = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ']
    count = 0

    def emit(kvs, prims):
        nonlocal count
        L.append(f"// entity {count}")
        L.append("{")
        for k, v in kvs:
            L.append(f'"{k}" "{v}"')
        for i, p in enumerate(prims):
            L.append(f"// brush {i}")
            L.append("{")
            L.extend(p)
            L.append("}")
        L.append("}")
        count += 1

    emit([("classname", "worldspawn"), ("iw8r_title", "IW8 Radiant compact showcase"), ("sundirection", "-40 235 0"),
          ("suncolor", "1 0.95 0.85"), ("skyname", "sky_mp_shipment"), ("iw8r_fog", "512 6000 0.6 0.65 0.72"),
          ("iw8r_script", stem + ".gsc")], world)
    for kvs, prims in ents:
        emit(kvs, prims)
    os.makedirs(out_dir, exist_ok=True)
    write_custom_assets(out_dir, stem + ".gsc")
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print(f"wrote {a.output}: {count} entities, {len(world)} world primitives, 9 rows, {2 * W} x {2 * H} units")


if __name__ == "__main__":
    main()
