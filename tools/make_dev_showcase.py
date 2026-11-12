"""Write samples/dev/iw8r_dev_showcase.map: a dev map with one row per general asset variation, for checking the
editor, the compiler and the BUILT package (tools/verify_map_output.py) against every feature at once.

Rows (west to east along +x, rows stacked along +y, each row labelled by a script_origin "row_<name>"):
  stock materials by surface type, tool materials, brush flags, brush shapes and slopes, patches, stock models
  (untargeted -> static + collision box), scripted models (targetname), misc_model static models, lights, reflection
  probes, effects (fx_origin), bot path nodes + a traversal, a ladder tower, mantle ledges, spawns (TDM/DM/DOM),
  game-mode objects (DOM / Hardpoint / S&D, doc 32), triggers and a scripted brush model.
Worldspawn: sun, stock sky, exponential fog and a film grade.

Names are Replay 1.20 stock assets from data/asset_catalog (zones mp_shipment / mp_backlot2 / their techsets).
Usage: py -3 -I tools/make_dev_showcase.py [-o samples/dev/iw8r_dev_showcase.map]
"""
import argparse
import os

TAIL = " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"
FLOOR = "tm2co/me_curb_stone_01"
WALL = "tm2co/me_plaster_barrier_fence_wall"
ROW = 448          # distance between rows (y)
COL = 192          # distance between items in a row (x)
X0, Y0 = -1920, -3072

STOCK_MATERIALS = [
    "tm2co/storage_shipping_container_metal_painted_01_second_green_wet", "tm2co/construction_wood_form_beams_worn_oak_single",
    "tmo/building_wall_corner_brick_01", "tm2co/concrete_cracked_wall_damaged_01_rvl", "tmo/ee_foliage_grass_lm_short_green",
    "tmo/buildup_dirt_01", "tmo/periph_terrain_sand_billow_01", "tmo/concrete_tile_interlocking_01", "tmo/uk_asphalt_pothole_01",
    "tm2co/me_plaster_barrier_fence_wall", "tmo/destruction_debris_rock_concrete_edge_01", "tmo/cp_disco_metal_rusted_01",
    "tmo/gravel_medium_01", "tmo/building_metal_corrugated_set_01", "tmo/cardboard_box_damaged_01", "tmo/debris_fabric_pile_01",
    "tm2co/storage_shipping_container_metal_painted_01_second_red", "tm2co/me_curb_stone_01", "tmo/building_roof_lip_01_grey",
    "tmo/cp_disco_chainlink_fence_01_onesided",  # cut-out (sdf mask)
]
TOOL_MATERIALS = ["caulk", "nodraw", "clip", "clip_player", "clip_weapon", "clip_nosight", "ladder", "water", "hint", "skip"]
STOCK_MODELS = [
    "military_hq_crate_01_proxy", "tm/container_barrel_plastic_01_closed", "tm/furniture_cafe_chair_01", "tm/furniture_cafe_table_02",
    "tm/ee_light_fixture_lamp_short_02", "tm/ee_sign_frame_02_128_black", "com_junktire", "tm/destruction_debris_rock_concrete_edge_01",
    "tm/foliage_alder_bush_large_01", "tm/com_pallet_2", "tm/accessory_plant_pot_fake_2", "tm/ee_appliance_dryer",
    "tm/cp_disco_chainlink_fence", "accessory_money_pile_large", "tm/cinderblock_pallet_01",
]
EFFECTS = [
    ("vfx/iw8/veh/scriptables/vfx_veh_explosion_civ", "6"),
    ("vfx/iw8/veh/scriptables/shared/vfx_veh_fire_linger_med", None),
    ("vfx/iw8/veh/scriptables/vfx_smoke_light_ch", None),
    ("vfx/core/expl/spark_death", "2"),  # any effect of data/asset_catalog/iw8_common_vfx.tsv
]


def box(x0, y0, z0, x1, y1, z1, mat, top=None, body=()):
    top = top or mat
    f = lambda a, b, c, m: f" ( {a[0]} {a[1]} {a[2]} ) ( {b[0]} {b[1]} {b[2]} ) ( {c[0]} {c[1]} {c[2]} ) {m}{TAIL}"
    return list(body) + [
        f((x1, y1, z0), (x0, y1, z0), (x0, y0, z0), mat),
        f((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), top),
        f((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), mat),
        f((x1, y1, z1), (x0, y1, z1), (x0, y1, z0), mat),
        f((x1, y0, z1), (x1, y1, z1), (x1, y1, z0), mat),
        f((x0, y1, z1), (x0, y0, z1), (x0, y0, z0), mat),
    ]


def wedge(x0, y0, x1, y1, h, mat):
    """A ramp rising along +x from 0 to h (5 faces)."""
    f = lambda a, b, c: f" ( {a[0]} {a[1]} {a[2]} ) ( {b[0]} {b[1]} {b[2]} ) ( {c[0]} {c[1]} {c[2]} ) {mat}{TAIL}"
    return [
        f((x1, y1, 0), (x0, y1, 0), (x0, y0, 0)),           # bottom
        f((x0, y0, 0), (x1, y0, h), (x1, y0, 0)),           # -y side
        f((x1, y1, 0), (x1, y1, h), (x0, y1, 0)),           # +y side
        f((x1, y0, h), (x1, y1, h), (x1, y1, 0)),           # +x back
        f((x0, y1, 0), (x1, y1, h), (x1, y0, h)),           # slope (normal up and -x)
    ]


def mesh(points, mat, rows, cols):
    """A patch mesh: `points[c][r]` = (x, y, z); columns of rows, as the editor writes them."""
    lines = ["mesh", "{", f"{mat}", "lightmap_gray", f"{cols} {rows} 16 8"]
    for c in range(cols):
        lines.append("(")
        for r in range(rows):
            x, y, z = points[c][r]
            # File units: texture repeats x 1024 (a 128-unit repeat: 8 per unit); lightmap 1 per 16 units.
            lines.append(f"v {x} {y} {z} t {c * 512} {r * 512} {c * 4} {r * 4}")
        lines.append(")")
    lines.append("}")
    return lines


def write_png(path, w, h, pixel):
    """Minimal RGBA PNG writer (no third-party modules): pixel(x, y) -> (r, g, b, a)."""
    import struct
    import zlib
    raw = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    head = bytes([0x89]) + b"PNG" + bytes([13, 10, 26, 10])
    data = head + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


SHOWCASE_GSC = """// IW8 Radiant dev showcase map script (worldspawn iw8r_script): the live script_models of the
// live_script_models_and_misc_model row, driven the way stock map scripts drive theirs.
user_main()
{
    level thread showcase_spin("scripted_0");
    level thread showcase_bob("scripted_1");
    level thread showcase_blink("scripted_2");
}

showcase_spin(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        ent rotateyaw(360, 4);
        wait 4;
    }
}

showcase_bob(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        ent movez(32, 1.5, 0.5, 0.5);
        wait 1.5;
        ent movez(-32, 1.5, 0.5, 0.5);
        wait 1.5;
    }
}

showcase_blink(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        wait 2;
        ent hide();
        wait 1;
        ent show();
    }
}
"""


def write_custom_assets(map_dir, script_name):
    """Custom assets beside the map: materials/*.iw8mat.json + PNGs, models/*.obj (OBJ: Y-up, map units), and the
    map script (worldspawn iw8r_script)."""
    with open(os.path.join(map_dir, script_name), "w", encoding="utf-8", newline="\n") as f:
        f.write(SHOWCASE_GSC)
    import json
    mats = os.path.join(map_dir, "materials")
    models = os.path.join(map_dir, "models")
    os.makedirs(mats, exist_ok=True)
    os.makedirs(models, exist_ok=True)
    write_png(os.path.join(mats, "iw8r_dev_checker_c.png"), 128, 128,
              lambda x, y: (230, 160, 40, 255) if ((x // 16) + (y // 16)) % 2 else (40, 40, 48, 255))
    write_png(os.path.join(mats, "iw8r_dev_grate_c.png"), 128, 128,
              lambda x, y: (150, 150, 160, 255) if (x % 16 < 3 or y % 16 < 3) else (0, 0, 0, 0))
    json.dump({"name": "iw8r_dev/checker", "color": "iw8r_dev_checker_c.png", "surface": "metal"},
              open(os.path.join(mats, "iw8r_dev_checker.iw8mat.json"), "w"), indent=1)
    json.dump({"name": "iw8r_dev/grate", "color": "iw8r_dev_grate_c.png", "surface": "metal", "alphaTest": True,
               "twoSided": True}, open(os.path.join(mats, "iw8r_dev_grate.iw8mat.json"), "w"), indent=1)

    def obj(path, material, verts, faces):
        nl = chr(10)
        out = ["# IW8 Radiant dev showcase custom model (Y-up, map units)"]
        out += ["v %g %g %g" % v for v in verts]
        out += ["vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1", "usemtl " + material]
        out += ["f " + " ".join("%d/%d" % (i, t) for i, t in zip(face, (1, 2, 3, 4))) for face in faces]
        with open(path, "w") as f:
            f.write(nl.join(out) + nl)

    s = 24.0  # crate: 48 wide, 48 tall, sitting on y = 0
    v = [(-s, 0, -s), (s, 0, -s), (s, 2 * s, -s), (-s, 2 * s, -s), (-s, 0, s), (s, 0, s), (s, 2 * s, s), (-s, 2 * s, s)]
    faces = [(1, 4, 3, 2), (5, 6, 7, 8), (1, 2, 6, 5), (4, 8, 7, 3), (1, 5, 8, 4), (2, 3, 7, 6)]
    obj(os.path.join(models, "iw8r_dev_crate.obj"), "iw8r_dev/checker", v, faces)
    panel = [(-48, 0, 0), (48, 0, 0), (48, 96, 0), (-48, 96, 0)]
    obj(os.path.join(models, "iw8r_dev_grate.obj"), "iw8r_dev/grate", panel, [(1, 2, 3, 4)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=os.path.join("samples", "dev", "iw8r_dev_showcase.map"))
    a = ap.parse_args()
    L = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ']
    count = [0]
    world_prims = []
    later = []

    def entity(kvs, prims=()):
        later.append((kvs, prims))

    def label(row, name):
        entity([("classname", "script_origin"), ("targetname", "row_" + name), ("origin", f"{X0 - 160} {Y0 + row * ROW} 16")])

    def at(row, i, z=0):
        return X0 + i * COL, Y0 + row * ROW, z

    # Room: floor, walls, sky ceiling (sealed).
    W, Hh = 2560, 3584
    world_prims.append(box(-W, -Hh, -16, W, Hh, 0, FLOOR))
    world_prims.append(box(-W - 16, -Hh, 0, -W, Hh, 768, WALL))
    world_prims.append(box(W, -Hh, 0, W + 16, Hh, 768, WALL))
    world_prims.append(box(-W, -Hh - 16, 0, W, -Hh, 768, WALL))
    world_prims.append(box(-W, Hh, 0, W, Hh + 16, 768, WALL))
    world_prims.append(box(-W - 16, -Hh - 16, 768, W + 16, Hh + 16, 784, "sky"))

    row = 0
    label(row, "stock_materials")
    for i, m in enumerate(STOCK_MATERIALS):
        x, y, _ = at(row, i * 0.6)
        world_prims.append(box(x, y, 0, x + 96, y + 96, 128, m))
    row += 1
    label(row, "tool_materials")
    for i, m in enumerate(TOOL_MATERIALS):
        x, y, _ = at(row, i)
        world_prims.append(box(x, y, 0, x + 96, y + 32 if m == "ladder" else y + 96, 160 if m == "ladder" else 96, m))
    row += 1
    label(row, "brush_flags")
    for i, flag in enumerate(["detail", "nonColliding", "weaponClip"]):
        x, y, _ = at(row, i)
        world_prims.append(box(x, y, 0, x + 96, y + 96, 96, WALL, body=[f" contents {flag};"]))
    row += 1
    label(row, "shapes_and_slopes")
    for i, h in enumerate([32, 64, 96, 128]):  # ramps of 14, 27, 37, 45 degrees over 128
        x, y, _ = at(row, i)
        world_prims.append(wedge(x, y, x + 128, y + 96, h, FLOOR))
    for s, step in enumerate([8, 16]):          # stairs
        x, y, _ = at(row, 5 + s * 2)
        for k in range(8):
            world_prims.append(box(x + k * 16, y, 0, x + k * 16 + 16, y + 96, (k + 1) * step, FLOOR))
    row += 1
    label(row, "patches")
    x, y, _ = at(row, 0)
    world_prims.append(mesh([[(x + c * 64, y + r * 64, 0) for r in range(3)] for c in range(3)], FLOOR, 3, 3))  # flat
    x, y, _ = at(row, 2)
    world_prims.append(mesh([[(x + c * 64, y, 0), (x + c * 64, y + 64, 128 if c % 2 == 0 else 96), (x + c * 64, y + 128, 0)] for c in range(3)],
                            WALL, 3, 3))  # arch-like curve
    x, y, _ = at(row, 4)
    import math
    world_prims.append(mesh([[(x + c * 48, y + r * 48, round(24 * math.sin(c) * math.cos(r))) for r in range(5)] for c in range(5)],
                            "tmo/ee_foliage_grass_lm_short_green", 5, 5))  # terrain
    row += 1
    label(row, "stock_models_misc_model")
    for i, m in enumerate(STOCK_MODELS):
        x, y, _ = at(row, i * 0.8)
        entity([("classname", "misc_model"), ("model", m), ("origin", f"{x} {y} 0"), ("angles", f"0 {i * 24} 0")])
    row += 1
    label(row, "live_script_models_and_misc_model")
    for i, m in enumerate(STOCK_MODELS[:3]):
        x, y, _ = at(row, i)
        # A live script_model (doc 34): a targeting entity keeps it an entity instead of a static prop.
        entity([("classname", "script_model"), ("model", m), ("targetname", f"scripted_{i}"), ("origin", f"{x} {y} 0"),
                ("footprint_mask_group", "default")])
        entity([("classname", "script_origin"), ("target", f"scripted_{i}"), ("origin", f"{x} {y - 64} 8")])
    # A destructible prop (6.5): IW3 _destructables via the x64-zt load script; breaks after 40 damage.
    x, y, _ = at(row, 8)
    entity([("classname", "script_model"), ("model", "tm/com_pallet_2"), ("targetname", "destructable"), ("origin", f"{x} {y} 0"),
            ("footprint_mask_group", "default")])
    for i, m in enumerate(STOCK_MODELS[3:6]):
        x, y, _ = at(row, 4 + i)
        entity([("classname", "misc_model"), ("model", m), ("origin", f"{x} {y} 0"), ("modelscale", "1.25" if i == 1 else "1")])
    row += 1
    label(row, "lights_and_probes")
    lights = [("light", [("_color", "1 1 1"), ("radius", "384"), ("intensity", "1")]),
              ("light", [("_color", "1 0.25 0.2"), ("radius", "256"), ("intensity", "1.5")]),
              ("light", [("_color", "0.3 0.5 1"), ("radius", "256"), ("intensity", "1.5")]),
              ("light", [("_color", "1 0.9 0.7"), ("radius", "512"), ("intensity", "2"), ("fov_outer", "60"), ("fov_inner", "35"),
                         ("angles", "90 0 0")])]
    for i, (cls, kv) in enumerate(lights):
        x, y, _ = at(row, i * 1.5)
        entity([("classname", cls), ("origin", f"{x} {y} 128")] + kv)
    for i in range(2):
        x, y, _ = at(row, 7 + i * 2)
        kv = [("classname", "reflection_probe"), ("origin", f"{x} {y} 64")]
        if i == 0:  # an authored probe volume (5.5); the other probe's volume is derived by the converter
            kv += [("mins", "-256 -192 -64"), ("maxs", "256 192 192")]
        entity(kv)
    row += 1
    label(row, "effects")
    for i, (fx, delay) in enumerate(EFFECTS):
        x, y, _ = at(row, i * 1.5)
        kv = [("classname", "fx_origin"), ("fx", fx), ("origin", f"{x} {y} 8")]
        if delay:
            kv.append(("delay", delay))
        entity(kv)
    # Ambient sound on the east half of the effects row: sound_emitter (6.7) with stock aliases from
    # data/asset_catalog/iw8_common_sounds.tsv, played by the map script.
    for i, (alias, loop, delay) in enumerate([("emt_smokestack_lp", "1", None), ("emt_light_fluorescent_hum1_int", "1", None),
                                              ("emt_rock_med_debris_short", "0", "4")]):
        x, y, _ = at(row, 8 + i * 2)
        kv = [("classname", "sound_emitter"), ("soundalias", alias), ("loop", loop), ("origin", f"{x} {y} 32")]
        if delay:
            kv.append(("delay", delay))
        entity(kv)
    row += 1
    label(row, "navigation")
    for i in range(3):
        for j in range(3):
            x, y, _ = at(row, i)
            entity([("classname", "node_pathnode"), ("origin", f"{x} {y + j * 96} 0")])
    x, y, _ = at(row, 4)
    world_prims.append(box(x, y, 0, x + 128, y + 128, 40, FLOOR))  # a 40-unit ledge to drop from
    entity([("classname", "node_negotiation_begin"), ("origin", f"{x + 64} {y + 64} 40"), ("target", "drop40"),
            ("animscript", "traverse_jumpdown_40")])
    entity([("classname", "node_negotiation_end"), ("origin", f"{x + 192} {y + 64} 0"), ("targetname", "drop40")])
    row += 1
    label(row, "ladder_and_mantle")
    x, y, _ = at(row, 0)
    world_prims.append(box(x, y, 0, x + 128, y + 128, 256, WALL))               # tower
    world_prims.append(box(x - 8, y + 48, 0, x, y + 80, 256, "ladder"))        # ladder on its -x face
    for i, h in enumerate([24, 40, 56, 72]):
        x, y, _ = at(row, 3 + i * 1.5)
        world_prims.append(box(x, y, 0, x + 96, y + 128, h, FLOOR))
    row += 1
    label(row, "spawns")
    spawns = ["mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start"] + ["mp_tdm_spawn"] * 4 + ["mp_dm_spawn"] * 2 + \
             ["mp_dom_spawn_allies_start", "mp_dom_spawn_axis_start", "mp_dom_spawn", "mp_dm_spawn_start"]
    for i, cls in enumerate(spawns):
        x, y, _ = at(row, i)
        entity([("classname", cls), ("origin", f"{x} {y} 16"), ("angles", f"0 {90 * (i % 4)} 0")])
    entity([("classname", "info_player_start"), ("origin", f"{X0} {Y0 + row * ROW - 96} 16")])
    entity([("classname", "mp_global_intermission"), ("origin", "0 0 512"), ("angles", "30 45 0")])
    row += 1
    label(row, "game_modes")
    for i, lab in enumerate(["_a", "_b", "_c"]):  # Domination (doc 32)
        x, y, _ = at(row, i * 1.5)
        entity([("classname", "trigger_radius"), ("origin", f"{x} {y} 0"), ("radius", "120"), ("height", "128"),
                ("script_gameobjectname", "dom onslaught"), ("script_label", lab), ("targetname", "flag_primary")])
    x, y, _ = at(row, 5)
    entity([("classname", "trigger_multiple"), ("script_gameobjectname", "hardpoint"), ("script_label", "1"), ("targetname", "hardpoint_zone")],
           [box(x, y, 0, x + 192, y + 192, 128, "trigger")])
    x, y, _ = at(row, 7)
    entity([("classname", "trigger_use_touch"), ("script_gameobjectname", "bombzone"), ("script_label", "_a"),
            ("script_bombmode_original", "1")], [box(x, y, 0, x + 160, y + 160, 96, "trigger")])
    row += 1
    label(row, "custom_assets")
    x, y, _ = at(row, 0)
    world_prims.append(box(x, y, 0, x + 96, y + 96, 96, "iw8r_dev/checker"))
    x, y, _ = at(row, 1)
    world_prims.append(box(x, y, 0, x + 96, y + 8, 128, "iw8r_dev/grate"))
    x, y, _ = at(row, 3)
    entity([("classname", "script_model"), ("model", "iw8r_dev_crate"), ("origin", f"{x} {y} 0"), ("angles", "0 30 0")])
    x, y, _ = at(row, 4)
    entity([("classname", "script_model"), ("model", "iw8r_dev_crate"), ("targetname", "scripted_crate"), ("origin", f"{x} {y} 0")])
    entity([("classname", "script_origin"), ("target", "scripted_crate"), ("origin", f"{x} {y - 64} 8")])
    x, y, _ = at(row, 5)
    entity([("classname", "misc_model"), ("model", "iw8r_dev_grate"), ("origin", f"{x} {y} 0"), ("angles", "0 90 0")])
    row += 1
    label(row, "triggers_and_brushmodels")
    x, y, _ = at(row, 0)
    entity([("classname", "trigger_multiple"), ("targetname", "dev_trigger")], [box(x, y, 0, x + 128, y + 128, 128, "trigger")])
    x, y, _ = at(row, 2)
    entity([("classname", "trigger_radius"), ("origin", f"{x} {y} 0"), ("radius", "96"), ("height", "96"), ("targetname", "dev_radius")])
    x, y, _ = at(row, 4)
    entity([("classname", "trigger_hurt"), ("dmg", "5")], [box(x, y, 0, x + 128, y + 128, 64, "trigger")])
    x, y, _ = at(row, 6)
    entity([("classname", "script_brushmodel"), ("targetname", "dev_door")], [box(x, y, 0, x + 16, y + 128, 128, WALL)])

    # Write.
    def emit(kvs, prims):
        L.append(f"// entity {count[0]}")
        L.append("{")
        for k, v in kvs:
            L.append(f'"{k}" "{v}"')
        for i, p in enumerate(prims):
            L.append(f"// brush {i}")
            L.append("{")
            L.extend(p)
            L.append("}")
        L.append("}")
        count[0] += 1

    emit([("classname", "worldspawn"), ("iw8r_title", "IW8 Radiant dev showcase"), ("sundirection", "-40 235 0"),
          ("suncolor", "1 0.95 0.85"), ("skyname", "sky_mp_shipment"), ("iw8r_fog", "512 6000 0.6 0.65 0.72"),
          ("iw8r_film", "1.05 0 0.1 0 1.02 1 0.98 0.98 1 1.02"),
          ("iw8r_vision", "tonemapExposureAdjust=0.25; r_primaryLightTweakDiffuseStrength=1.1"),
          ("iw8r_script", os.path.splitext(os.path.basename(a.output))[0] + ".gsc")], world_prims)
    for kvs, prims in later:
        emit(kvs, prims)
    os.makedirs(os.path.dirname(os.path.abspath(a.output)), exist_ok=True)
    write_custom_assets(os.path.dirname(os.path.abspath(a.output)), os.path.splitext(os.path.basename(a.output))[0] + ".gsc")
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print(f"wrote {a.output}: {count[0]} entities, {len(world_prims)} world primitives, {row + 1} rows")


if __name__ == "__main__":
    main()
