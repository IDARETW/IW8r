"""Write samples/iw8/iw8_sample_room.map: the milestone-1 IW8 source scene.

The scene uses the Radiant text grammar (brushes, patches, entities) with IW8 content:
- materials and the xmodel are real MW2019 Replay 1.20 stock names, read from the ACTS dump of
  mp_backlot2 (E:/mw124-conversion-builds/_stock_backlot_xmodels, materialHandles / asset names);
- classnames are IW8 ones: stock mp_hideout's entity string (info_player_start,
  mp_global_intermission, script_model, script_origin, reflection_probe, trigger_multiple) and its
  compiled spawnList names (mp_tdm_spawn_allies_start, mp_dm_spawn_start, ...).
It deliberately contains fields the editor does not interpret (a layer line, contents, toolFlags,
an unknown entity key) so the round-trip test can prove they survive.

Usage: python -I tools/make_iw8_sample_room.py [-o samples/iw8/iw8_sample_room.map]
"""
import argparse
import os

FLOOR = "tm2co/me_curb_stone_01"
WALL = "tm2co/me_plaster_barrier_fence_wall"
TRIM = "tm2co/ee_trim_beam_airport_concrete"
TRIGGER = "tm2co/destruction_debris_plaster_concrete_edge_01"
TAIL = " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"


def box(x0, y0, z0, x1, y1, z1, mat, top=None):
    """Six Radiant face lines with outward normals (n = (p0 - p1) x (p2 - p1))."""
    top = top or mat
    f = lambda a, b, c, m: f" ( {a[0]} {a[1]} {a[2]} ) ( {b[0]} {b[1]} {b[2]} ) ( {c[0]} {c[1]} {c[2]} ) {m}{TAIL}"
    return [
        f((x1, y1, z0), (x0, y1, z0), (x0, y0, z0), mat),  # bottom -z
        f((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), top),  # top +z
        f((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), mat),  # -y
        f((x1, y1, z1), (x0, y1, z1), (x0, y1, z0), mat),  # +y
        f((x1, y0, z1), (x1, y1, z1), (x1, y1, z0), mat),  # +x
        f((x0, y1, z1), (x0, y0, z1), (x0, y0, z0), mat),  # -x
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=os.path.join("samples", "iw8", "iw8_sample_room.map"))
    a = ap.parse_args()
    L = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ']
    ent = 0

    def entity(kvs, prims=()):
        nonlocal ent
        L.append(f"// entity {ent}")
        L.append("{")
        for k, v in kvs:
            L.append(f'"{k}" "{v}"')
        for i, p in enumerate(prims):
            L.append(f"// brush {i}")
            L.append("{")
            L.extend(p)
            L.append("}")
        L.append("}")
        ent += 1

    floor = ['layer "000_Global/Floors"'] + box(-256, -256, -16, 256, 256, 0, FLOOR)
    walls = [
        box(-256, -272, 0, 256, -256, 128, WALL),
        box(-256, 256, 0, 256, 272, 128, WALL),
        box(-272, -256, 0, -256, 256, 128, WALL),
        box(256, -256, 0, 272, 256, 128, WALL, top=TRIM),
    ]
    walls[0] = [" contents detail;"] + walls[0]
    pillar = box(-32, -32, 0, 32, 32, 96, TRIM)
    patch = [
        " mesh",
        " {",
        "  toolFlags splitGeo;",
        f"  {TRIM}",
        "  lightmap_gray",
        "  2 2 16 8",
        "  (",
        "\tv 64 128 0 t 0 0 0 0",
        "\tv 64 128 64 t 0 -64 0 4",
        "  )",
        "  (",
        "\tv 128 128 0 c 255 255 255 255 t 64 0 4 0",
        "\tv 128 128 64 t 64 -64 4 4",
        "  )",
        " }",
    ]
    entity([("classname", "worldspawn"), ("editor_note", "unknown keys must survive")],
           [floor] + walls + [pillar, patch])
    entity([("origin", "0 -192 16"), ("angles", "0 90 0"), ("classname", "info_player_start")])
    entity([("origin", "0 0 256"), ("angles", "45 0 0"), ("classname", "mp_global_intermission")])
    entity([("origin", "-192 -192 16"), ("angles", "0 45 0"), ("classname", "mp_tdm_spawn_allies_start")])
    entity([("origin", "192 192 16"), ("angles", "0 225 0"), ("classname", "mp_tdm_spawn_axis_start")])
    entity([("origin", "-192 192 16"), ("angles", "0 315 0"), ("classname", "mp_tdm_spawn")])
    entity([("origin", "192 -192 16"), ("angles", "0 135 0"), ("classname", "mp_dm_spawn_start")])
    entity([("origin", "160 -96 0"), ("angles", "0 180 0"), ("model", "automotive_truck_loading_ramp_short"),
            ("classname", "script_model"), ("targetname", "ramp")])
    entity([("origin", "0 0 64"), ("classname", "reflection_probe")])
    entity([("origin", "-128 0 32"), ("targetname", "room_center"), ("classname", "script_origin")])
    entity([("classname", "trigger_multiple"), ("targetname", "room_trigger")],
           [box(-96, 64, 0, -32, 128, 64, TRIGGER)])
    os.makedirs(os.path.dirname(os.path.abspath(a.output)), exist_ok=True)
    with open(a.output, "w", newline="\r\n", encoding="ascii") as f:
        f.write("\n".join(L) + "\n")
    print(f"wrote {a.output}: {ent} entities")


if __name__ == "__main__":
    main()
