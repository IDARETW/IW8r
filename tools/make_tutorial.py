"""Writes the tutorial map and sample prefabs (roadmap 9.7).

samples/tutorial/iw8r_tutorial.map: a small playable arena (TDM / FFA / Domination) that shows the usual parts of a map:
a building to walk through and climb (ramp to the roof), cover, stock props (misc_model), prefabs (misc_prefab), a light,
a reflection probe, an ambient sound, an effect, spawns, game-mode objects and a matching sky and sun.
samples/tutorial/prefabs/*.map: crate_stack, lamp_post, cover_wall, spawn_pair (reusable through the Prefabs dock).
docs/TUTORIAL.md walks through building and testing it.

Usage: py -3 -I tools/make_tutorial.py [-o samples/tutorial]
"""
import argparse
import os

TAIL = " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"
FLOOR = "tmo/concrete_tile_interlocking_01"
WALL = "tmo/building_wall_corner_brick_01"
TRIM = "tm2co/me_curb_stone_01"
METAL = "tmo/building_metal_corrugated_set_01"


def box(x0, y0, z0, x1, y1, z1, mat, top=None):
    top = top or mat
    f = lambda a, b, c, m: f" ( {a[0]} {a[1]} {a[2]} ) ( {b[0]} {b[1]} {b[2]} ) ( {c[0]} {c[1]} {c[2]} ) {m}{TAIL}"
    return [
        f((x1, y1, z0), (x0, y1, z0), (x0, y0, z0), mat),
        f((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), top),
        f((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), mat),
        f((x1, y1, z1), (x0, y1, z1), (x0, y1, z0), mat),
        f((x1, y0, z1), (x1, y1, z1), (x1, y1, z0), mat),
        f((x0, y1, z1), (x0, y0, z1), (x0, y0, z0), mat),
    ]


def wedge(x0, y0, x1, y1, z0, h, mat):
    """A ramp rising along +x from z0 to z0 + h."""
    f = lambda a, b, c: f" ( {a[0]} {a[1]} {a[2]} ) ( {b[0]} {b[1]} {b[2]} ) ( {c[0]} {c[1]} {c[2]} ) {mat}{TAIL}"
    t = z0 + h
    return [
        f((x1, y1, z0), (x0, y1, z0), (x0, y0, z0)),
        f((x0, y0, z0), (x1, y0, t), (x1, y0, z0)),
        f((x1, y1, z0), (x1, y1, t), (x0, y1, z0)),
        f((x1, y0, t), (x1, y1, t), (x1, y1, z0)),
        f((x0, y1, z0), (x1, y1, t), (x1, y0, t)),
    ]


class MapWriter:
    def __init__(self):
        self.lines = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ']
        self.count = 0

    def entity(self, kvs, prims=()):
        L = self.lines
        L.append(f"// entity {self.count}")
        L.append("{")
        for k, v in kvs:
            L.append(f'"{k}" "{v}"')
        for i, p in enumerate(prims):
            L.append(f"// brush {i}")
            L.append("{")
            L.extend(p)
            L.append("}")
        L.append("}")
        self.count += 1

    def save(self, path):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(self.lines) + "\n")


def write_prefabs(folder):
    """Prefab maps: contents around the origin; misc_prefab places them."""
    os.makedirs(folder, exist_ok=True)
    m = MapWriter()  # three crates in a stack
    m.entity([("classname", "worldspawn")])
    for i, (x, y, z, yaw) in enumerate([(0, 0, 0, 0), (40, 6, 0, 12), (18, 2, 36, 40)]):
        m.entity([("classname", "misc_model"), ("model", "military_hq_crate_01_proxy"), ("origin", f"{x} {y} {z}"), ("angles", f"0 {yaw} 0")])
    m.save(os.path.join(folder, "crate_stack.map"))

    m = MapWriter()  # a lamp post: a metal pole with a warm light at the top
    m.entity([("classname", "worldspawn")], [box(-4, -4, 0, 4, 4, 144, METAL), box(-12, -12, 144, 12, 12, 152, METAL)])
    m.entity([("classname", "light"), ("origin", "0 0 136"), ("_color", "1 0.85 0.6"), ("radius", "320"), ("intensity", "1.2")])
    m.save(os.path.join(folder, "lamp_post.map"))

    m = MapWriter()  # a waist-high cover wall: chest height 56, with a trim cap
    m.entity([("classname", "worldspawn")], [box(-96, -12, 0, 96, 12, 48, WALL), box(-100, -16, 48, 100, 16, 56, TRIM)])
    m.save(os.path.join(folder, "cover_wall.map"))

    m = MapWriter()  # a TDM spawn pair facing +x
    m.entity([("classname", "worldspawn")])
    m.entity([("classname", "mp_tdm_spawn"), ("origin", "0 -32 16"), ("angles", "0 0 0")])
    m.entity([("classname", "mp_tdm_spawn"), ("origin", "0 32 16"), ("angles", "0 0 0")])
    m.save(os.path.join(folder, "spawn_pair.map"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=os.path.join("samples", "tutorial"))
    a = ap.parse_args()
    write_prefabs(os.path.join(a.out, "prefabs"))

    m = MapWriter()
    R, H = 1024, 512  # arena half-size and wall height
    world = [
        box(-R, -R, -16, R, R, 0, FLOOR),
        box(-R - 16, -R, 0, -R, R, H, WALL), box(R, -R, 0, R + 16, R, H, WALL),
        box(-R, -R - 16, 0, R, -R, H, WALL), box(-R, R, 0, R, R + 16, H, WALL),
        box(-R - 16, -R - 16, H, R + 16, R + 16, H + 16, "sky"),
    ]
    # Central building (256 x 256, 160 tall): doorways on -x and +x, a flat roof; a ramp on the south side to the roof.
    bx, by, bh, t = 256, 256, 160, 16
    world += [
        box(-bx, -by, 0, bx, -by + t, bh, WALL),                 # south wall
        box(-bx, by - t, 0, bx, by, bh, WALL),                   # north wall
        box(-bx, -by + t, 0, -bx + t, -48, bh, WALL),            # west wall, left of the doorway
        box(-bx, 48, 0, -bx + t, by - t, bh, WALL),              # west wall, right of the doorway
        box(-bx, -48, 112, -bx + t, 48, bh, WALL),               # west lintel (doorway 96 wide, 112 tall)
        box(bx - t, -by + t, 0, bx, -48, bh, WALL),
        box(bx - t, 48, 0, bx, by - t, bh, WALL),
        box(bx - t, -48, 112, bx, 48, bh, WALL),
        box(-bx, -by, bh, bx, by, bh + t, TRIM, top=METAL),      # roof
        wedge(-bx - 448, -by - 128, -bx, -by, 0, bh + t, TRIM),  # ramp to the roof's south-west corner (about 20 degrees)
    ]
    # Cover around the building.
    for x, y in [(-640, 384), (640, -384), (-640, -640), (640, 640)]:
        world.append(box(x - 96, y - 12, 0, x + 96, y + 12, 48, WALL))
    m.entity([("classname", "worldspawn"), ("iw8r_title", "IW8 Radiant tutorial arena"), ("skyname", "sky_mp_shipment"),
              ("sundirection", "-40 235 0"), ("suncolor", "1 0.95 0.85"), ("iw8r_fog", "768 9000 0.62 0.66 0.72")], world)

    # Prefabs (Prefabs dock) and stock props (Models dock).
    for x, y, pf, yaw in [(-480, 0, "prefabs/crate_stack.map", 0), (480, 128, "prefabs/crate_stack.map", 90),
                          (-768, 768, "prefabs/lamp_post.map", 0), (768, -768, "prefabs/lamp_post.map", 0),
                          (0, 640, "prefabs/cover_wall.map", 0), (0, -640, "prefabs/cover_wall.map", 0)]:
        m.entity([("classname", "misc_prefab"), ("model", pf), ("origin", f"{x} {y} 0"), ("angles", f"0 {yaw} 0")])
    for i, (mdl, x, y, yaw) in enumerate([("tm/container_barrel_plastic_01_closed", -320, 320, 0),
                                          ("tm/container_barrel_plastic_01_closed", -290, 350, 40),
                                          ("tm/com_pallet_2", 320, -320, 15), ("tm/furniture_cafe_table_02", 0, 0, 0),
                                          ("tm/furniture_cafe_chair_01", 48, 0, 180)]):
        m.entity([("classname", "misc_model"), ("model", mdl), ("origin", f"{x} {y} 0"), ("angles", f"0 {yaw} 0")])

    # Lighting, reflections, sound, effect.
    m.entity([("classname", "light"), ("origin", "0 0 136"), ("_color", "1 0.92 0.8"), ("radius", "384"), ("intensity", "1.4")])
    m.entity([("classname", "reflection_probe"), ("origin", "0 0 64"), ("mins", "-240 -240 -64"), ("maxs", "240 240 96")])
    m.entity([("classname", "sound_emitter"), ("soundalias", "emt_light_fluorescent_hum1_int"), ("loop", "1"), ("origin", "0 0 140")])
    m.entity([("classname", "fx_origin"), ("fx", "vfx/iw8/veh/scriptables/vfx_smoke_light_ch"), ("origin", "640 640 8")])

    # Spawns: team starts on the west and east sides, general TDM and FFA spawns around the arena.
    for i in range(4):
        m.entity([("classname", "mp_tdm_spawn_allies_start"), ("origin", f"-880 {-192 + i * 128} 16"), ("angles", "0 0 0")])
        m.entity([("classname", "mp_tdm_spawn_axis_start"), ("origin", f"880 {-192 + i * 128} 16"), ("angles", "0 180 0")])
    ring = [(-800, -800, 45), (800, -800, 135), (800, 800, 225), (-800, 800, 315), (0, -880, 90), (0, 880, 270), (-880, 0, 0), (880, 0, 180)]
    for x, y, yaw in ring:
        m.entity([("classname", "mp_tdm_spawn"), ("origin", f"{x} {y} 16"), ("angles", f"0 {yaw} 0")])
        m.entity([("classname", "mp_dm_spawn"), ("origin", f"{x * 0.7:g} {y * 0.7:g} 16"), ("angles", f"0 {yaw} 0")])
    m.entity([("classname", "mp_dom_spawn_allies_start"), ("origin", "-880 448 16"), ("angles", "0 0 0")])
    m.entity([("classname", "mp_dom_spawn_axis_start"), ("origin", "880 -448 16"), ("angles", "0 180 0")])
    m.entity([("classname", "info_player_start"), ("origin", "-960 0 16"), ("angles", "0 0 0")])
    m.entity([("classname", "mp_global_intermission"), ("origin", "-900 -900 600"), ("angles", "30 45 0")])
    # Domination: A west, B in the building, C east.
    for lab, x in [("_a", -640), ("_b", 0), ("_c", 640)]:
        m.entity([("classname", "trigger_radius"), ("origin", f"{x} 0 0"), ("radius", "120"), ("height", "128"),
                  ("script_gameobjectname", "dom"), ("script_label", lab), ("targetname", "flag_primary")])

    out = os.path.join(a.out, "iw8r_tutorial.map")
    m.save(out)
    print(f"wrote {out}: {m.count} entities, prefabs in {os.path.join(a.out, 'prefabs')}")


if __name__ == "__main__":
    main()
