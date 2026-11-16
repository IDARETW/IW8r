#!/usr/bin/env python3
"""Generate the IW8 Radiant prefab library under data/prefabs/iw8r/.

Each prefab is an iwmap 4 .map file (same text format as tests/data/iw8_sample_room.map)
placed in a category folder. Walls, floors, ramps, pillars, roofs and the trigger / ladder /
clip brushes are generated brushes. Doors, windows, railings, fences, stairs, ladders, lamps,
cover and props are stock models (misc_model), following the owner's guidance. Openable
doors use a script_brushmodel with iw8r_door plus a trigger_use brush.

Run from anywhere:   py -3 -I tools/build_prefab_library.py
Output is deterministic (no randomness, fixed order). Nothing outside data/prefabs/iw8r is written.

Outputs:
  data/prefabs/iw8r/<category>/<slug>.map   one prefab each (origin = floor centre)
  data/prefabs/iw8r/index.tsv               path, category, title, size, materials, models
  data/prefabs/iw8r/README.md               categories, items, conventions and caveats
"""
from __future__ import annotations

import itertools
import math
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "data" / "asset_catalog"
MATERIAL_TSVS = [CATALOG / "iw8_materials_status.tsv", CATALOG / "iw8_materials_status_b.tsv"]
MODEL_TSV = CATALOG / "iw8_models_status.tsv"
SAMPLE_MAP = ROOT / "tests" / "data" / "iw8_sample_room.map"
OUT_DIR = ROOT / "data" / "prefabs" / "iw8r"

# ----------------------------------------------------------------------------- materials
# Stock materials are all `ok` in iw8_materials_status.tsv / _b.tsv (asserted at run time).
# Tool materials are accepted by isIw8ToolMaterial (src/core/compile_iw8.cpp): substring
# caulk / clip / nodraw / trigger / hint / skip / portal / origin / sky, or basename "ladder".
MAT_CONCRETE = "mo/concrete_cinder_block_base_tan"   # cinder block: plain walls, cover, pillars (ok, plain grey block)
MAT_PLASTER = "tm2co/plaster_beige_01"              # interior wall plaster (ok)
MAT_BRICK = "tm2co/un_wall_set_brick_01"            # brick wall set, clean (ok, not an lod variant)
MAT_WOOD = "twc/wood_boards_weathered_04"          # exterior wood walls, fences (ok)
MAT_WOOD_DOOR = "twc/wood_varnished_01"            # wood door leaves, wood platforms (ok)
MAT_METAL = "tmco/metal_painted_base_01a"          # metal walls, low walls, metal pillars (ok)
MAT_METAL_PLATE = "tmco/metal_painted_diamond_plate_02a"  # catwalk / platform walking surface (ok)
MAT_FLOOR = "tmco/concrete_tile_ribbed_01"          # floor slabs (ok)
MAT_ROOF = "tmco/metal_bare_roof_01"               # roof slabs and slanted roofs (ok)
MAT_GLASS = "tmo/ee_light_fixture_lamp_short_glass"  # blended glass: the only `_bad_` techset glass in material_cache index.json (ok)
MAT_CAULK = "tw/caulk_shadow"                      # caulk brush for invisible supports (ok, tool-like)
TOOL_TRIGGER = "trigger"   # trigger_use brush face, same as trigger_templates.cpp kTriggerSuffix (tool, not drawn)
TOOL_LADDER = "ladder"     # climbable volume, exact basename match in isIw8ToolMaterial (tool, not drawn)

# ----------------------------------------------------------------------------- stock models
# Every name is `ok` in iw8_models_status.tsv (asserted at run time). Grouped by the piece they make.
M = {
    # doors: frames and static leaves (misc_model)
    "door_frame_wood": "tm/building_door_wood_worn_out_frame_02",
    "door_frame_wood_double": "tm/building_door_wood_worn_out_frame_02_double",
    "door_frame_metal": "tm/door_frame_metal_panel_01_single_mp_gray",
    "door_frame_metal_double": "tm/construction_doors_metal_frame_double_b_02_mp",
    "door_frame_industrial_double": "tm/door_frame_double_industrial_metal_mp_01",
    "door_frame_plaster": "tm/building_doorframe_woodplaster_01",
    "door_leaf_wood": "door_wooden_panel_mp_01",          # static single wood leaf
    "door_leaf_metal": "door_metal_panel_01_mp_tan",      # static single metal leaf
    "door_ornate_l": "door_wood_ornate_01_blue_double_l",  # static ornate double, left leaf
    "door_ornate_r": "door_wood_ornate_01_blue_double_r",  # static ornate double, right leaf
    "door_garage": "building_metal_sliding_door_01",       # static garage door
    "door_slide_frame": "tm/door_automatic_sliding_frame",
    "door_slide_left": "tm/door_automatic_sliding_left",
    "door_slide_right": "tm/door_automatic_sliding_right",
    # windows
    "window_brick": "tm/building_wall_window_brick_01",
    "window_brick_broken": "tm/building_wall_window_brick_broken_02",
    "window_wood_double": "tm/me_window_double_wood_frame_01",
    "window_shutter_left": "tm/me_window_double_wood_shutter_left_01",
    "window_shutter_right": "tm/me_window_double_wood_shutter_right_01",
    "window_painted_frame": "tm/ee_window_wood_painted_worn_frame",
    "window_bars": "tm/ee_window_bars_02",
    "window_metal_frame": "tm/ee_window_frame_metal_white_medium_clean_c",
    "window_sill": "tm/ee_window_sill_small_02",
    # railings
    "rail_metal_128": "tm/barrier_industrial_railing_128",
    "rail_metal_64": "tm/barrier_industrial_railing_64",
    "rail_metal_corner": "tm/barrier_industrial_railing_32_corner_out",
    "rail_wood_handrail": "tm/barrier_handrail_wooden_01",
    "rail_concrete_balcony": "tm/balcony_ribbed_concrete_railing_01",
    # fences
    "fence_chain_160": "tm/barrier_chain_link_fence_160_01",
    "fence_chain_96": "tm/barrier_chain_link_fence_96_01_rusty",
    "fence_chain_64": "tm/barrier_chain_link_fence_64_01_rusty",
    "fence_chain_32": "tm/barrier_chain_link_fence_32_01_rusty",
    "fence_chain_gate": "tm/barrier_chain_link_fence_door_01_rusty",
    "fence_chain_gate_frame": "tm/barrier_chain_link_fence_door_frame_128_01_rusty",
    "fence_chain_disco": "tm/cp_disco_chainlink_fence",
    "fence_wood_01": "tm/barrier_wooden_fence_01_mp",
    "fence_wood_02": "tm/barrier_wooden_fence_02_mp",
    "fence_wood_03": "tm/barrier_wooden_fence_03_mp",
    "fence_slats": "tm/barrier_wooden_slats_01_woodslats_kit_single_128_slat_01",
    # stairs (modular 2-step module is the only stock piece that chains; see STEP_RUN/STEP_RISE)
    "stairs_module_2step": "tm/ee_stairs_modular_2step",
    "stairs_exterior": "tm/building_exterior_concrete_stairs_01",
    "stairs_exterior_half": "tm/building_exterior_concrete_stairs_01_half",
    "stairs_wood_96": "tm/mout_stairs_wood_96",
    "stairs_steps_64x56": "tm/ee_stairs_concretesteps_64x56",
    "stairs_catwalk_bottom": "tm/mout_catwalk_stairs_bottom",
    "stairs_catwalk_middle": "tm/mout_catwalk_stairs_middle",
    "stairs_catwalk_top": "tm/mout_catwalk_stairs_top",
    "stairs_wood_rail_bottom": "tm/mout_stairs_wood_rail_bottom",
    "stairs_wood_rail_middle": "tm/mout_stairs_wood_rail_middle",
    "stairs_industrial_rail": "tm/stairs_industrial_handrail_bars_01a",
    # ramps
    "ramp_loading": "automotive_truck_loading_ramp_short",  # stock truck loading ramp (already in the sample map)
    # ladders (paired with a TOOL_LADDER brush)
    "ladder_steel": "tm/com_steel_ladder",
    "ladder_wood_construction": "tm/construction_ladder_wood_01",
    "ladder_old_72": "tm/ladder_old_wood_01_72",
    "ladder_old_144": "tm/ladder_old_wood_01_144",
    "ladder_old_160": "tm/ladder_old_wood_01_160",
    "ladder_radio_tower": "tm/ee_buildings_industrial_radiotower_ladder_128_02",
    # lamps (paired with a light entity)
    "lamp_cage": "tm/lighting_fixtures_security_lamp_withcage_01",
    "lamp_security": "tm/lighting_fixtures_security_lamp_01",
    "lamp_tall": "tm/ee_light_fixture_lamp_tall_01",
    "lamp_short": "tm/ee_light_fixture_lamp_short_02",
    "lamp_floodlight": "tm/com_floodlight_em",
    "lamp_ceiling": "tm/light_ceiling_bulb_01",
    "lamp_wall": "tm/lighting_wall_lamp_picture_01",
    # cover
    "sandbag_long": "sandbag_wall_pieces_arrey_mp_long_01_brown",
    "sandbag_short": "sandbag_wall_pieces_arrey_mp_short_01_brown",
    "block_concrete": "tm/barrier_concrete_block_01",
    "block_traffic": "tm/barrier_traffic_concrete_block_01",
    "bollard": "tm/barrier_plastic_bollard_large",
    "crate_large": "tm/ch_crate48x64",
    "crate_wood_tall": "tm/cp_crate_wood_01_tall",
    "crate_plastic_large": "tm/crate_plastic_lrg_01_dirt",
    "crate_plastic_small_red": "crate_plastic_sml_01_red",
    "crate_mil_stack": "military_crate_large_stackable_01",
    "barrel_plastic": "tm/container_barrel_plastic_01_closed",
    "barrel_wood": "tm/decor_barrel_wood_01",
    "barrel_bio": "tm/com_barrel_biohazard_rust",
    "car": "tm/ee_periph_city_ukraine_car_01",
    # roofs
    "roof_lip_concrete": "tm/building_roof_lip_concrete_01_128",
    "roof_lip_tan": "tm/building_roof_lip_01_tan_128",
    # pillars and beams
    "pillar_40": "tm/building_support_pillar_40x40_01",
    "pillar_24": "tm/building_support_pillar_24x24_01_tan",
    "pillar_16": "tm/building_support_pillar_16x16_01",
    "beam_wood": "tm/building_support_wood_beam_03",
    # props
    "desk_teacher": "furniture_teachers_desk_01",
    "desk_metal": "p7_desk_metal_military_03_tablet",
    "desk_fan": "desktop_fan_fn_01",
    "radio": "lag_portable_radio",
    "ashtray": "accessory_table_vintage_ashtray",
    "stove": "equipment_propane_burner_stove_01_white",
    "pot": "kitchen_01_pot_copper_01",
    "pan": "kitchen_01_pan_01",
    "knife_block": "kitchen_knife_block_01",
    "syrup": "kitchen_plastic_syrup_bottle_01",
    "coffee_set": "me_kitchen_coffee_set_01",
    "shelf_metal": "tm/decor_misc_props_01_shelf_metal",
    "shelf_wood": "storage_shelf_beam_wood_plank",
    "pallet_stack": "tm/com_pallet_stack",
    "jug": "tm/container_water_jug_01_blue",
    "paint_can": "container_paint_can_01",
}

# Placement assumptions for stock models (the catalog has no bounds). Verify in the editor.
STEP_RUN = 64.0          # horizontal run of one modular 2-step module (assumed)
STEP_RISE = 32.0         # rise of one modular 2-step module (assumed)
MODEL_FOOTPRINT = (64.0, 64.0, 128.0)  # bounds estimate for a single stock model

WALL_H = 128.0
WALL_T = 16.0            # wall thickness, y in [-8, 8] (same as the sample room walls)
DOOR_TARGET = "door1"

# ----------------------------------------------------------------------------- geometry
# Box face table, derived from tests/data/iw8_sample_room.map: each row is the three corners
# of one face, in the sample's order and winding. "h"/"l" pick hi/lo per axis (x, y, z).
BOX_FACES = [
    ("hhl", "lhl", "lll"),  # bottom  -z
    ("llh", "lhh", "hhh"),  # top     +z
    ("llh", "hlh", "hll"),  # -y
    ("hhh", "lhh", "lhl"),  # +y
    ("hlh", "hhh", "hhl"),  # +x
    ("lhh", "llh", "lll"),  # -x
]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def length(a):
    return math.sqrt(dot(a, a))


def fmt(v) -> str:
    if isinstance(v, str):
        return v
    v = float(v)
    if abs(v - round(v)) < 1e-6:
        return str(int(round(v)))
    return ("%.4f" % v).rstrip("0").rstrip(".")


def vec(p) -> str:
    return " ".join(fmt(c) for c in p)


def face_line(a, b, c, mat: str) -> str:
    # Same tail as the sample: texture 128 128 0 0 0 0, then lightmap_gray 16384 16384 0 0 0 0.
    return f" ( {vec(a)} ) ( {vec(b)} ) ( {vec(c)} ) {mat} 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"


def box_faces(lo, hi, mat: str, top: str | None = None):
    """Six faces of an axis-aligned box, sample winding. top overrides the +z face material."""

    def pt(code):
        return tuple(hi[i] if ch == "h" else lo[i] for i, ch in enumerate(code))

    faces = []
    for idx, (a, b, c) in enumerate(BOX_FACES):
        m = top if (top and idx == 1) else mat
        faces.append((pt(a), pt(b), pt(c), m))
    return faces


def hull_faces(points, mat: str):
    """Convex brush from points: one face per supporting plane, outward winding (same as the box)."""
    pts = [tuple(float(v) for v in p) for p in points]
    seen = set()
    faces = []
    for i, j, k in itertools.combinations(range(len(pts)), 3):
        a, b, c = pts[i], pts[j], pts[k]
        n = cross(sub(c, a), sub(b, a))
        ln = length(n)
        if ln < 1e-9:
            continue
        n = scale(n, 1.0 / ln)
        d = [dot(n, sub(p, a)) for p in pts]
        if all(x <= 1e-6 for x in d):
            pass
        elif all(x >= -1e-6 for x in d):
            n = scale(n, -1.0)
            b, c = c, b
        else:
            continue
        key = (tuple(round(v, 5) for v in n), round(dot(n, a), 3))
        if key in seen:
            continue
        seen.add(key)
        faces.append((a, b, c, mat))
    if len(faces) < 4:
        raise ValueError("hull has fewer than 4 faces")
    return faces


def outward_ok(faces) -> bool:
    """Every face normal, cross(c-a, b-a), points away from the brush centre."""
    pts = [p for f in faces for p in f[:3]]
    centre = scale(tuple(sum(p[i] for p in pts) for i in range(3)), 1.0 / len(pts))
    for a, b, c, _ in faces:
        n = cross(sub(c, a), sub(b, a))
        fc = scale(add(add(a, b), c), 1.0 / 3.0)
        if dot(n, sub(fc, centre)) <= 0:
            return False
    return True


# ----------------------------------------------------------------------------- prefab builder
class Prefab:
    def __init__(self, category: str, slug: str, title: str):
        self.category = category
        self.slug = slug
        self.title = title
        self.world = []     # list of brushes (each a list of faces)
        self.ents = []      # dicts: keys (list of (k, v)), pos, yaw, brushes
        self.pts = []       # points for bounds

    def box(self, lo, hi, mat, top=None):
        self.world.append(box_faces(lo, hi, mat, top))
        self.pts += [tuple(lo), tuple(hi)]

    def hull(self, points, mat):
        self.world.append(hull_faces(points, mat))
        self.pts += [tuple(p) for p in points]

    def ent(self, classname, pos=(0, 0, 0), yaw=0, keys=()):
        self.ents.append({"keys": [("classname", classname)] + list(keys), "pos": tuple(pos), "yaw": yaw, "brushes": []})
        self.pts.append(tuple(pos))
        return self.ents[-1]

    def brush_ent(self, classname, lo, hi, mat, keys=()):
        self.ents.append({"keys": [("classname", classname)] + list(keys), "pos": None, "yaw": 0,
                          "brushes": [box_faces(lo, hi, mat)]})
        self.pts += [tuple(lo), tuple(hi)]

    def model(self, name, pos=(0, 0, 0), yaw=0, footprint=MODEL_FOOTPRINT):
        self.ent("misc_model", pos, yaw, [("model", name)])
        x, y, z = pos
        fx, fy, fz = footprint
        self.pts += [(x - fx / 2, y - fy / 2, z), (x + fx / 2, y + fy / 2, z + fz)]

    def light(self, pos, color: str, radius=256, intensity=1):
        self.ent("light", pos, 0, [("_color", color), ("radius", fmt(radius)), ("intensity", fmt(intensity))])

    def ladder(self, x, y, h):
        # Climbable face sits 8..12 units in front of the ladder model (+y), full height.
        self.box((x - 12, y + 8, 0), (x + 12, y + 12, h), TOOL_LADDER)


def wall_run(p: Prefab, x0, x1, mat, z1=WALL_H, thick=WALL_T, opening=None):
    """Wall along x from x0 to x1, centred on y, base at z=0. opening = (xa, xb, za, zb)."""
    y0, y1 = -thick / 2, thick / 2
    if not opening:
        p.box((x0, y0, 0), (x1, y1, z1), mat)
        return
    xa, xb, za, zb = opening
    p.box((x0, y0, 0), (xa, y1, z1), mat)
    p.box((xb, y0, 0), (x1, y1, z1), mat)
    p.box((xa, y0, zb), (xb, y1, z1), mat)
    if za > 0:
        p.box((xa, y0, 0), (xb, y1, za), mat)


def row(p: Prefab, model, n, spacing, yaw=0, axis="x", pos=(0, 0, 0), footprint=None):
    start = -(n - 1) * spacing / 2.0
    fp = footprint or (spacing, 32, 128)
    for i in range(n):
        off = start + i * spacing
        q = (pos[0] + off, pos[1], pos[2]) if axis == "x" else (pos[0], pos[1] + off, pos[2])
        p.model(model, q, yaw, fp)


def modular_run(p: Prefab, steps: int, start, axis: str):
    """Chain of stock 2-step modules; one module per two steps. axis 'x' (yaw 0) or 'y' (yaw 90)."""
    sx, sy, sz = start
    for i in range(steps // 2):
        if axis == "x":
            q = (sx + i * STEP_RUN, sy, sz + i * STEP_RISE)
            yaw = 0
        else:
            q = (sx, sy + i * STEP_RUN, sz + i * STEP_RISE)
            yaw = 90
        p.model(M["stairs_module_2step"], q, yaw, (STEP_RUN, STEP_RUN, STEP_RISE))


# ----------------------------------------------------------------------------- door / window helpers
def door_single(p, wall_mat, panel_mat, frame=None, length=256, ow=96, oh=96):
    wall_run(p, -length / 2, length / 2, wall_mat, opening=(-ow / 2, ow / 2, 0, oh))
    if frame:
        p.model(frame, (0, 0, 0))
    p.brush_ent("script_brushmodel", (-ow / 2 + 1, -3, 0), (ow / 2 - 1, 3, oh - 2), panel_mat,
                [("targetname", DOOR_TARGET), ("iw8r_door", "rotate 90 1 3")])
    p.brush_ent("trigger_use", (-ow / 2 - 16, 24, 0), (ow / 2 + 16, 56, oh + 16), TOOL_TRIGGER,
                [("targetname", "door1_use"), ("target", DOOR_TARGET)])


def door_double(p, wall_mat, panel_mat, frame=None):
    wall_run(p, -192, 192, wall_mat, opening=(-96, 96, 0, 112))
    if frame:
        p.model(frame, (0, 0, 0))
    p.brush_ent("script_brushmodel", (-95, -3, 0), (-1, 3, 110), panel_mat,
                [("targetname", "door1"), ("iw8r_door", "rotate 90 1 3")])
    p.brush_ent("script_brushmodel", (1, -3, 0), (95, 3, 110), panel_mat,
                [("targetname", "door2"), ("iw8r_door", "rotate -90 1 3")])
    p.brush_ent("trigger_use", (-112, 24, 0), (0, 56, 126), TOOL_TRIGGER,
                [("targetname", "door1_use"), ("target", "door1")])
    p.brush_ent("trigger_use", (0, 24, 0), (112, 56, 126), TOOL_TRIGGER,
                [("targetname", "door2_use"), ("target", "door2")])


def door_slide(p, wall_mat, panel_mat, frame=None):
    wall_run(p, -192, 192, wall_mat, opening=(-128, 128, 0, 128))
    if frame:
        p.model(frame, (0, 0, 0))
    p.brush_ent("script_brushmodel", (-127, -3, 0), (127, 3, 126), panel_mat,
                [("targetname", DOOR_TARGET), ("iw8r_door", "slide 0 0 112 1 3")])
    p.brush_ent("trigger_use", (-144, 24, 0), (144, 56, 140), TOOL_TRIGGER,
                [("targetname", "door1_use"), ("target", DOOR_TARGET)])


def door_static(p, wall_mat, models, length=256, ow=96, oh=96):
    wall_run(p, -length / 2, length / 2, wall_mat, opening=(-ow / 2, ow / 2, 0, oh))
    for name, pos in models:
        p.model(name, pos, 0, (96, 32, 112))


def window(p, wall_mat, ow, zb, zt, frame=None, glass=True, extras=()):
    length = max(256, ow + 128)
    wall_run(p, -length / 2, length / 2, wall_mat, opening=(-ow / 2, ow / 2, zb, zt))
    if glass:
        p.box((-ow / 2, -2, zb), (ow / 2, 2, zt), MAT_GLASS)
    mid = (zb + zt) / 2.0
    if frame:
        p.model(frame, (0, 0, mid), 0, (ow + 16, 16, zt - zb + 16))
    for name, pos in extras:
        p.model(name, pos, 0, (32, 32, 64))


# ----------------------------------------------------------------------------- registry
REG: list[tuple[str, str, str, callable]] = []


def reg(category, slug, title):
    def deco(fn):
        REG.append((category, slug, title, fn))
        return fn
    return deco


# ---- doors (11)
reg("doors", "door_single_wood_bare", "Single wood door in plaster wall, no frame")(
    lambda p: door_single(p, MAT_PLASTER, MAT_WOOD_DOOR))
reg("doors", "door_single_wood_frame", "Single wood door with stock wood frame")(
    lambda p: door_single(p, MAT_PLASTER, MAT_WOOD_DOOR, M["door_frame_wood"]))
reg("doors", "door_single_metal_frame", "Single metal door with stock metal frame")(
    lambda p: door_single(p, MAT_METAL, MAT_METAL, M["door_frame_metal"]))
reg("doors", "door_single_plaster_frame", "Single wood door in plaster wall with plaster frame")(
    lambda p: door_single(p, MAT_PLASTER, MAT_WOOD_DOOR, M["door_frame_plaster"]))
reg("doors", "door_double_metal_frame", "Double metal doors with stock double frame")(
    lambda p: door_double(p, MAT_CONCRETE, MAT_METAL, M["door_frame_metal_double"]))
reg("doors", "door_double_wood_frame", "Double wood doors with stock wood frame")(
    lambda p: door_double(p, MAT_PLASTER, MAT_WOOD_DOOR, M["door_frame_wood_double"]))
reg("doors", "door_sliding_garage_frame", "Sliding garage door (slide up) with industrial frame")(
    lambda p: door_slide(p, MAT_CONCRETE, MAT_METAL, M["door_frame_industrial_double"]))
reg("doors", "door_static_ornate_double", "Static ornate double doors (stock leaves, closed)")(
    lambda p: door_static(p, MAT_PLASTER, [(M["door_ornate_l"], (-48, 0, 0)), (M["door_ornate_r"], (48, 0, 0))],
                          length=384, ow=192, oh=112))
reg("doors", "door_static_metal_leaf", "Static metal door (stock leaf) in plaster frame")(
    lambda p: door_static(p, MAT_PLASTER, [(M["door_frame_plaster"], (0, 0, 0)), (M["door_leaf_metal"], (0, 0, 0))]))
reg("doors", "door_static_wood_leaf", "Static wood door (stock leaf) in concrete wall")(
    lambda p: door_static(p, MAT_CONCRETE, [(M["door_leaf_wood"], (0, 0, 0))]))
reg("doors", "door_automatic_sliding", "Automatic sliding double doors (stock frame and leaves)")(
    lambda p: door_static(p, MAT_METAL, [(M["door_slide_frame"], (0, 0, 0)), (M["door_slide_left"], (-48, 0, 0)),
                                         (M["door_slide_right"], (48, 0, 0))], length=384, ow=192, oh=112))
reg("doors", "door_garage_static", "Static garage door (stock sliding door) in 256 opening")(
    lambda p: door_static(p, MAT_CONCRETE, [(M["door_garage"], (0, 0, 0))], length=384, ow=256, oh=128))


# ---- windows (7)
reg("windows", "window_single_brick", "Single brick window with stock frame")(
    lambda p: window(p, MAT_BRICK, 96, 48, 112, M["window_brick"]))
reg("windows", "window_double_wood", "Double wood-frame window")(
    lambda p: window(p, MAT_PLASTER, 160, 48, 112, M["window_wood_double"]))
reg("windows", "window_shutters_wood", "Wood window with left and right shutters")(
    lambda p: window(p, MAT_WOOD, 96, 48, 112, M["window_painted_frame"],
                     extras=[(M["window_shutter_left"], (-56, 0, 80)), (M["window_shutter_right"], (56, 0, 80))]))
reg("windows", "window_bars_plaster", "Barred window in plaster wall")(
    lambda p: window(p, MAT_PLASTER, 96, 48, 112, None, extras=[(M["window_bars"], (0, 0, 80))]))
reg("windows", "window_metal_frame_concrete", "Metal-frame window in concrete wall")(
    lambda p: window(p, MAT_CONCRETE, 96, 40, 120, M["window_metal_frame"]))
reg("windows", "window_broken_brick", "Broken brick window, no glass")(
    lambda p: window(p, MAT_BRICK, 96, 48, 112, M["window_brick_broken"], glass=False))
reg("windows", "window_sill_wood", "Wood window with stock sill")(
    lambda p: window(p, MAT_WOOD, 96, 48, 112, M["window_painted_frame"], extras=[(M["window_sill"], (0, 0, 48))]))


# ---- walls (13)
for _n, _m in [("concrete", MAT_CONCRETE), ("plaster", MAT_PLASTER), ("wood", MAT_WOOD),
               ("metal", MAT_METAL), ("brick", MAT_BRICK)]:
    reg("walls", f"wall_plain_128_{_n}", f"Plain 128 wide wall, {_n}")(
        lambda p, _m=_m: wall_run(p, -64, 64, _m))
for _n, _m in [("concrete", MAT_CONCRETE), ("plaster", MAT_PLASTER), ("wood", MAT_WOOD)]:
    reg("walls", f"wall_plain_256_{_n}", f"Plain 256 wide wall, {_n}")(
        lambda p, _m=_m: wall_run(p, -128, 128, _m))
reg("walls", "wall_door_opening_256_plaster", "256 wall with door opening, no door")(
    lambda p: wall_run(p, -128, 128, MAT_PLASTER, opening=(-48, 48, 0, 96)))
reg("walls", "wall_window_opening_256_concrete", "256 concrete wall with window opening")(
    lambda p: wall_run(p, -128, 128, MAT_CONCRETE, opening=(-48, 48, 48, 112)))
reg("walls", "half_wall_cover_128_concrete", "Half-height concrete cover wall, 128 wide, 64 high")(
    lambda p: wall_run(p, -64, 64, MAT_CONCRETE, z1=64))
reg("walls", "low_wall_128_metal", "Low metal wall, 128 wide, 40 high")(
    lambda p: wall_run(p, -64, 64, MAT_METAL, z1=40))
reg("walls", "corner_wall_128_brick", "L-shaped brick corner wall, 128 per leg")(
    lambda p: (p.box((-64, -8, 0), (64, 8, WALL_H), MAT_BRICK), p.box((-8, -64, 0), (8, 64, WALL_H), MAT_BRICK)))
reg("walls", "thick_wall_256_concrete", "Thick 32-unit concrete wall, 256 wide")(
    lambda p: wall_run(p, -128, 128, MAT_CONCRETE, thick=32))


# ---- stairs (11)
def _stairs_straight(n):
    def build(p):
        modular_run(p, n, (0, 0, 0), "x")
    return build


reg("stairs", "stairs_straight_4", "Straight 4-step stair (2 modular modules)")(_stairs_straight(4))
reg("stairs", "stairs_straight_8", "Straight 8-step stair (4 modular modules)")(_stairs_straight(8))
reg("stairs", "stairs_straight_12", "Straight 12-step stair (6 modular modules)")(_stairs_straight(12))


def _stairs_l(p):
    modular_run(p, 4, (0, 0, 0), "x")
    p.box((96, -64, 0), (192, 64, 32), MAT_CONCRETE)  # landing at the top of run one
    modular_run(p, 4, (160, 64, 32), "y")


reg("stairs", "stairs_L_shaped_8", "L-shaped stair: 4 steps, landing, 4 steps turned 90 degrees")(_stairs_l)
reg("stairs", "stairs_concrete_exterior", "Exterior concrete stair (stock)")(
    lambda p: p.model(M["stairs_exterior"], (0, 0, 0), 0, (128, 128, 96)))
reg("stairs", "stairs_concrete_half", "Half exterior concrete stair (stock)")(
    lambda p: p.model(M["stairs_exterior_half"], (0, 0, 0), 0, (128, 128, 64)))
reg("stairs", "stairs_wood_96", "Wood stair, 96 wide (stock)")(
    lambda p: p.model(M["stairs_wood_96"], (0, 0, 0), 0, (96, 128, 96)))
reg("stairs", "stairs_with_railing", "Straight 4-step stair with industrial handrails both sides")(
    lambda p: (modular_run(p, 4, (0, 0, 0), "x"),
               p.model(M["stairs_industrial_rail"], (0, -48, 0)),
               p.model(M["stairs_industrial_rail"], (0, 48, 0))))
reg("stairs", "stairs_catwalk_set", "Catwalk stair set: bottom, middle, top (stock)")(
    lambda p: (p.model(M["stairs_catwalk_bottom"], (0, 0, 0)),
               p.model(M["stairs_catwalk_middle"], (128, 0, 0)),
               p.model(M["stairs_catwalk_top"], (256, 0, 0))))
reg("stairs", "stairs_wood_railed", "Railed wood stair, bottom and middle sections")(
    lambda p: (p.model(M["stairs_wood_rail_bottom"], (0, 0, 0)), p.model(M["stairs_wood_rail_middle"], (96, 0, 0))))
reg("stairs", "stairs_concrete_steps_64x56", "Concrete steps 64x56 (stock)")(
    lambda p: p.model(M["stairs_steps_64x56"], (0, 0, 0), 0, (64, 64, 56)))


# ---- ramps (4)
reg("ramps", "ramp_brush_concrete_128", "Concrete wedge ramp, 128 long, 64 rise")(
    lambda p: p.hull([(-64, -64, 0), (-64, 64, 0), (64, -64, 0), (64, 64, 0), (64, -64, 64), (64, 64, 64)], MAT_CONCRETE))
reg("ramps", "ramp_brush_metal_256", "Metal wedge ramp, 256 long, 96 rise")(
    lambda p: p.hull([(-128, -64, 0), (-128, 64, 0), (128, -64, 0), (128, 64, 0), (128, -64, 96), (128, 64, 96)], MAT_METAL))
reg("ramps", "ramp_brush_steep_wood", "Steep wood wedge, 96 long, 96 rise")(
    lambda p: p.hull([(-48, -48, 0), (-48, 48, 0), (48, -48, 0), (48, 48, 0), (48, -48, 96), (48, 48, 96)], MAT_WOOD_DOOR))
reg("ramps", "ramp_loading_stock", "Stock truck loading ramp (model)")(
    lambda p: p.model(M["ramp_loading"], (0, 0, 0), 0, (256, 128, 64)))


# ---- railings and fences (16)
reg("railings_fences", "rail_metal_row_128", "Metal industrial railing, 2 x 128")(
    lambda p: row(p, M["rail_metal_128"], 2, 128))
reg("railings_fences", "rail_metal_row_64", "Metal industrial railing, 4 x 64")(
    lambda p: row(p, M["rail_metal_64"], 4, 64))
reg("railings_fences", "rail_metal_corner", "Metal railing outside corner")(
    lambda p: p.model(M["rail_metal_corner"], (0, 0, 0)))
reg("railings_fences", "rail_wood_handrail_row", "Wood handrail, 2 x 128")(
    lambda p: row(p, M["rail_wood_handrail"], 2, 128))
reg("railings_fences", "rail_concrete_balcony_row", "Ribbed concrete balcony railing, 2 x 128")(
    lambda p: row(p, M["rail_concrete_balcony"], 2, 128))
reg("railings_fences", "fence_chain_row_160", "Chain-link fence, 2 x 160")(
    lambda p: row(p, M["fence_chain_160"], 2, 160))
reg("railings_fences", "fence_chain_row_96", "Rusty chain-link fence, 3 x 96")(
    lambda p: row(p, M["fence_chain_96"], 3, 96))
reg("railings_fences", "fence_chain_row_64", "Rusty chain-link fence, 4 x 64")(
    lambda p: row(p, M["fence_chain_64"], 4, 64))
reg("railings_fences", "fence_chain_row_32", "Rusty chain-link fence, 4 x 32")(
    lambda p: row(p, M["fence_chain_32"], 4, 32))
reg("railings_fences", "fence_chain_gate", "Chain-link gate with frame")(
    lambda p: (p.model(M["fence_chain_gate_frame"], (0, 0, 0)), p.model(M["fence_chain_gate"], (0, 0, 0))))
reg("railings_fences", "fence_chain_disco_row", "Chain-link fence (disco), 2 x 128")(
    lambda p: row(p, M["fence_chain_disco"], 2, 128))
reg("railings_fences", "fence_wood_01_row", "Wood fence 01, 3 x 96")(
    lambda p: row(p, M["fence_wood_01"], 3, 96))
reg("railings_fences", "fence_wood_02_row", "Wood fence 02, 3 x 96")(
    lambda p: row(p, M["fence_wood_02"], 3, 96))
reg("railings_fences", "fence_wood_03_row", "Wood fence 03, 3 x 96")(
    lambda p: row(p, M["fence_wood_03"], 3, 96))
reg("railings_fences", "fence_slats_row", "Wood slat fence, 2 x 128")(
    lambda p: row(p, M["fence_slats"], 2, 128))
reg("railings_fences", "fence_chain_gate_frame_row", "Chain-link gate frames, 2 x 128")(
    lambda p: row(p, M["fence_chain_gate_frame"], 2, 128))


# ---- cover (11)
reg("cover", "cover_sandbag_long_line", "Sandbag line, 3 long pieces")(
    lambda p: row(p, M["sandbag_long"], 3, 96))
reg("cover", "cover_sandbag_mixed", "Sandbag line, long and short rows")(
    lambda p: (row(p, M["sandbag_long"], 2, 96, pos=(0, -48, 0)), row(p, M["sandbag_short"], 2, 96, pos=(0, 48, 0))))
reg("cover", "cover_sandbag_corner", "Sandbag L-corner")(
    lambda p: (p.model(M["sandbag_long"], (0, -48, 0)), p.model(M["sandbag_short"], (-48, 0, 0), 90)))
reg("cover", "cover_block_concrete_line", "Concrete block barrier line, 3 x 128")(
    lambda p: row(p, M["block_concrete"], 3, 128))
reg("cover", "cover_block_traffic_line", "Traffic block barrier line, 3 x 128")(
    lambda p: row(p, M["block_traffic"], 3, 128))
reg("cover", "cover_bollard_row", "Plastic bollard row, 4 x 64")(
    lambda p: row(p, M["bollard"], 4, 64))
reg("cover", "cover_crate_stack_large", "Two large crates stacked")(
    lambda p: (p.model(M["crate_large"], (0, 0, 0)), p.model(M["crate_large"], (0, 0, 64))))
reg("cover", "cover_crate_mixed", "Mixed crates: wood tall, plastic large, small red")(
    lambda p: (p.model(M["crate_wood_tall"], (0, 0, 0)), p.model(M["crate_plastic_large"], (96, 0, 0)),
               p.model(M["crate_plastic_small_red"], (-64, 0, 0))))
reg("cover", "cover_barrel_group", "Group of three barrels: plastic, wood, biohazard")(
    lambda p: (p.model(M["barrel_plastic"], (0, 0, 0)), p.model(M["barrel_wood"], (48, 0, 0)),
               p.model(M["barrel_bio"], (24, 40, 0))))
reg("cover", "cover_car_with_blocks", "Parked car with traffic blocks either side")(
    lambda p: (p.model(M["car"], (0, 0, 0), 0, (240, 120, 140)),
               p.model(M["block_traffic"], (-190, 0, 0)), p.model(M["block_traffic"], (190, 0, 0))))
reg("cover", "cover_military_crate_stack", "Two stackable military crates, side by side")(
    lambda p: (p.model(M["crate_mil_stack"], (0, 0, 0)), p.model(M["crate_mil_stack"], (64, 0, 0))))


# ---- floors and platforms (6)
reg("floors", "floor_concrete_256", "Concrete floor slab, 256 x 256")(
    lambda p: p.box((-128, -128, -16), (128, 128, 0), MAT_FLOOR))
reg("floors", "floor_concrete_512", "Concrete floor slab, 512 x 512")(
    lambda p: p.box((-256, -256, -16), (256, 256, 0), MAT_FLOOR))
reg("floors", "floor_wood_256", "Wood floor slab, 256 x 256")(
    lambda p: p.box((-128, -128, -16), (128, 128, 0), MAT_WOOD_DOOR))


def _platform(mat, plate):
    def build(p):
        p.box((-128, -128, WALL_H - 16), (128, 128, WALL_H), plate)
        for x in (-112, 112):
            for y in (-112, 112):
                p.box((x - 8, y - 8, 0), (x + 8, y + 8, WALL_H - 16), mat)
    return build


reg("floors", "platform_raised_256_metal", "Raised metal platform, 256 square, four legs")(
    _platform(MAT_METAL, MAT_METAL_PLATE))
reg("floors", "platform_raised_256_wood", "Raised wood platform, 256 square, four legs")(
    _platform(MAT_WOOD_DOOR, MAT_WOOD_DOOR))


def _catwalk(p):
    p.box((-128, -64, WALL_H - 16), (128, 64, WALL_H), MAT_METAL_PLATE)
    for x in (-112, 112):
        for y in (-48, 48):
            p.box((x - 8, y - 8, 0), (x + 8, y + 8, WALL_H - 16), MAT_METAL)
    for x in (-64, 64):
        p.model(M["rail_metal_128"], (x, -56, WALL_H), 0, (128, 32, 128))
        p.model(M["rail_metal_128"], (x, 56, WALL_H), 0, (128, 32, 128))


reg("floors", "catwalk_256_rail", "Metal catwalk, 256 x 128, with railings both long sides")(_catwalk)


# ---- pillars and beams (8)
reg("pillars_beams", "pillar_square_32_concrete", "Square concrete pillar, 32 x 32 x 128")(
    lambda p: p.box((-16, -16, 0), (16, 16, WALL_H), MAT_CONCRETE))
reg("pillars_beams", "pillar_square_48_concrete", "Square concrete pillar, 48 x 48 x 128")(
    lambda p: p.box((-24, -24, 0), (24, 24, WALL_H), MAT_CONCRETE))


def _octagon(p):
    pts = []
    for k in range(8):
        a = k * math.pi / 4
        pts.append((20 * math.cos(a), 20 * math.sin(a), 0))
        pts.append((20 * math.cos(a), 20 * math.sin(a), WALL_H))
    p.hull(pts, MAT_CONCRETE)


reg("pillars_beams", "pillar_octagon_concrete", "Octagonal concrete pillar (convex hull), 128 high")(_octagon)
reg("pillars_beams", "pillar_model_40", "Stock 40 x 40 support pillar")(
    lambda p: p.model(M["pillar_40"], (0, 0, 0), 0, (40, 40, WALL_H)))
reg("pillars_beams", "pillar_model_24_quad", "Four stock 24 x 24 pillars in a square")(
    lambda p: [p.model(M["pillar_24"], (x, y, 0), 0, (24, 24, WALL_H)) for x in (-60, 60) for y in (-60, 60)])
reg("pillars_beams", "pillar_model_16_row", "Row of three stock 16 x 16 pillars at 96 spacing")(
    lambda p: row(p, M["pillar_16"], 3, 96, footprint=(16, 16, WALL_H)))
reg("pillars_beams", "beam_wood_brush_256", "Wood beam 256 long on two posts")(
    lambda p: (p.box((-128, -8, 112), (128, 8, 128), MAT_WOOD_DOOR),
               p.box((-120, -8, 0), (-104, 8, 112), MAT_WOOD_DOOR),
               p.box((104, -8, 0), (120, 8, 112), MAT_WOOD_DOOR)))
reg("pillars_beams", "beam_wood_model_pair", "Two stock wood beams, side by side on top")(
    lambda p: (p.model(M["beam_wood"], (0, -48, WALL_H)), p.model(M["beam_wood"], (0, 48, WALL_H))))


# ---- ladders (6): stock ladder model + climbable ladder brush. The stock ladder models are wide along their own y
# and thin along x (model cache bounds below: x min, x max, half width, z min, z max): each is turned 90 degrees so the
# rungs run along x, its back on the wall line (y = 0; the wall side is -y), the climbable brush over the rungs'
# front (+y), as tall as the model (the steel ladder hangs from its top: raised to stand on the floor). The files in
# data/prefabs/iw8r/ladders were rewritten this way by a one-off script (2026-11-15).
def _ladder(model, mx0, mx1, half, mz0, mz1):
    def build(p):
        depth, h = mx1 - mx0, round(mz1 - mz0)
        p.ent("misc_model", (0, -mx0, -mz0), 90, [("model", model)])
        p.pts += [(-half, 0, 0), (half, depth + 4, h)]
        p.box((-half, depth, 0), (half, depth + 4, h), TOOL_LADDER)
    return build


reg("ladders", "ladder_steel_128", "Steel ladder, 200 high, climbable brush")(
    _ladder(M["ladder_steel"], 0.0, 8.2, 16, -200.0, 0.0))
reg("ladders", "ladder_wood_construction_128", "Construction wood ladder, 193 high")(
    _ladder(M["ladder_wood_construction"], -2.0, 2.0, 12, -0.3, 192.3))
reg("ladders", "ladder_old_wood_72", "Old wood ladder, 76 high")(_ladder(M["ladder_old_72"], 0.3, 6.3, 12, 0.0, 76.3))
reg("ladders", "ladder_old_wood_144", "Old wood ladder, 148 high")(_ladder(M["ladder_old_144"], 0.3, 6.3, 12, 0.0, 148.3))
reg("ladders", "ladder_old_wood_160", "Old wood ladder, 160 high")(_ladder(M["ladder_old_160"], 0.2, 6.2, 12, 0.0, 160.3))
reg("ladders", "ladder_radio_tower_128", "Radio tower ladder section, 128 high")(
    _ladder(M["ladder_radio_tower"], -6.3, 1.6, 13, 0.5, 128.0))


# ---- roofs (6)
def _roof_flat(size, mat, lip):
    def build(p):
        half = size / 2
        p.box((-half, -half, WALL_H), (half, half, WALL_H + 16), mat)
        if lip:
            for x in (-half + 64, half - 64):
                p.model(M["roof_lip_concrete"], (x, -half + 8, WALL_H + 16), 0, (128, 16, 32))
                p.model(M["roof_lip_concrete"], (x, half - 8, WALL_H + 16), 0, (128, 16, 32))
    return build


reg("roofs", "roof_flat_256_concrete_lip", "Flat roof 256, with roof lips on the front and back edges")(
    _roof_flat(256, MAT_ROOF, True))
reg("roofs", "roof_flat_512_metal", "Flat metal roof 512 square, no lips")(_roof_flat(512, MAT_ROOF, False))


def _roof_slant(half, rise):
    def build(p):
        pts = []
        for x in (-half, half):
            pts += [(x, -half, WALL_H), (x, half, WALL_H), (x, half, WALL_H + rise)]
        p.hull(pts, MAT_ROOF)
    return build


reg("roofs", "roof_slant_256", "Slanted metal roof, 256 square, rising 64 toward +y")(_roof_slant(128, 64))
reg("roofs", "roof_slant_128", "Slanted metal roof, 128 square, rising 32 toward +y")(_roof_slant(64, 32))
reg("roofs", "roof_overhang_256", "Flat roof 320 square overhanging a 256 wall box")(
    lambda p: p.box((-160, -160, WALL_H), (160, 160, WALL_H + 16), MAT_ROOF))


def _parapet(p):
    p.box((-128, -128, WALL_H), (128, 128, WALL_H + 16), MAT_ROOF)
    p.box((-128, -128, WALL_H + 16), (128, -120, WALL_H + 48), MAT_CONCRETE)
    p.box((-128, 120, WALL_H + 16), (128, 128, WALL_H + 48), MAT_CONCRETE)
    p.box((-128, -128, WALL_H + 16), (-120, 128, WALL_H + 48), MAT_CONCRETE)
    p.box((120, -128, WALL_H + 16), (128, 128, WALL_H + 48), MAT_CONCRETE)


reg("roofs", "roof_parapet_256", "Flat roof 256 with a 32-unit concrete parapet")(_parapet)


# ---- lamps (8): stock lamp model + light entity
def _lamp(model, lamp_pos, light_pos, color, radius, intensity, footprint=(48, 48, 128)):
    def build(p):
        p.model(model, lamp_pos, 0, footprint)
        p.light(light_pos, color, radius, intensity)
    return build


reg("lamps", "lamp_cage_wall", "Caged security lamp on a wall, warm light")(
    _lamp(M["lamp_cage"], (0, 0, 96), (0, -24, 96), "1 0.85 0.6", 256, 1))
reg("lamps", "lamp_security_pole", "Security lamp, warm light")(
    _lamp(M["lamp_security"], (0, 0, 0), (0, -16, 128), "1 0.85 0.6", 256, 1))
reg("lamps", "lamp_tall_floor", "Tall floor lamp, warm light")(
    _lamp(M["lamp_tall"], (0, 0, 0), (0, 0, 160), "1 0.85 0.6", 256, 1, (48, 48, 176)))
reg("lamps", "lamp_short_table", "Short table lamp, cool light")(
    _lamp(M["lamp_short"], (0, 0, 40), (0, 0, 80), "0.8 0.9 1", 192, 1, (48, 48, 80)))
reg("lamps", "lamp_floodlight_pole", "Floodlight on a pole, bright white")(
    _lamp(M["lamp_floodlight"], (0, 0, 0), (0, -24, 192), "1 1 0.9", 512, 2, (64, 64, 192)))
reg("lamps", "lamp_ceiling_bulb", "Ceiling bulb, warm white")(
    _lamp(M["lamp_ceiling"], (0, 0, WALL_H - 16), (0, 0, WALL_H - 24), "1 0.9 0.8", 192, 1, (32, 32, 32)))
reg("lamps", "lamp_wall_picture", "Wall lamp over a picture, warm amber")(
    _lamp(M["lamp_wall"], (0, 0, 96), (0, -16, 96), "1 0.8 0.5", 128, 1))
reg("lamps", "lamp_emergency_red", "Caged lamp with red emergency light")(
    _lamp(M["lamp_cage"], (0, 0, 96), (0, -24, 96), "1 0.2 0.1", 192, 1))


# ---- props (6): stock clusters, positions approximate table / shelf heights
reg("props", "props_office_desk_set", "Office desk set: teacher desk, fan, radio, ashtray, coffee set")(
    lambda p: (p.model(M["desk_teacher"], (0, 0, 0), 0, (120, 60, 80)), p.model(M["desk_fan"], (-20, 0, 40)),
               p.model(M["radio"], (20, 10, 40)), p.model(M["ashtray"], (0, -10, 40)),
               p.model(M["coffee_set"], (30, -10, 40))))
reg("props", "props_office_metal_desk", "Metal military desk with fan and ashtray")(
    lambda p: (p.model(M["desk_metal"], (0, 0, 0), 0, (120, 60, 80)), p.model(M["desk_fan"], (-30, 0, 40)),
               p.model(M["ashtray"], (10, 0, 40))))
reg("props", "props_kitchen_counter", "Kitchen counter set: stove, pots, knives, syrup, coffee")(
    lambda p: (p.model(M["stove"], (0, 0, 0)), p.model(M["pot"], (0, 0, 60)), p.model(M["pan"], (-30, 0, 60)),
               p.model(M["knife_block"], (40, 0, 40)), p.model(M["syrup"], (20, -20, 40)),
               p.model(M["coffee_set"], (-20, 20, 40))))
reg("props", "props_warehouse_shelf", "Warehouse metal shelf with red crate and paint can")(
    lambda p: (p.model(M["shelf_metal"], (0, 0, 0)), p.model(M["crate_plastic_small_red"], (-20, 0, 80)),
               p.model(M["paint_can"], (20, 0, 80))))
reg("props", "props_warehouse_pallets", "Pallet stack with crate, jug and paint can")(
    lambda p: (p.model(M["pallet_stack"], (0, 0, 0)), p.model(M["crate_plastic_small_red"], (0, 0, 40)),
               p.model(M["jug"], (80, 0, 0)), p.model(M["paint_can"], (-60, 0, 0))))
reg("props", "props_storage_wood_shelf", "Wood storage shelf with paint can and jug")(
    lambda p: (p.model(M["shelf_wood"], (0, 0, 0)), p.model(M["paint_can"], (-20, 0, 40)),
               p.model(M["jug"], (20, 0, 40))))


# ---- gameplay markers (5): mp spawns and minimap corners, on a floor slab
def _spawn_floor(p, size):
    p.box((-size, -size, -16), (size, size, 0), MAT_FLOOR)


reg("gameplay", "gameplay_tdm_spawn_pair", "TDM spawn pair facing each other on a 512 floor")(
    lambda p: (_spawn_floor(p, 256), p.ent("mp_tdm_spawn", (-192, 0, 16), 0),
               p.ent("mp_tdm_spawn", (192, 0, 16), 180)))
reg("gameplay", "gameplay_team_spawn_pair", "Allies and axis start spawns facing each other")(
    lambda p: (_spawn_floor(p, 256), p.ent("mp_tdm_spawn_allies_start", (-192, -128, 16), 0),
               p.ent("mp_tdm_spawn_axis_start", (192, 128, 16), 180)))
reg("gameplay", "gameplay_dm_spawn_quad", "Four DM spawns at the corners of a 512 floor, facing centre")(
    lambda p: (_spawn_floor(p, 256),
               p.ent("mp_dm_spawn", (-160, -160, 16), 45), p.ent("mp_dm_spawn", (160, -160, 16), 135),
               p.ent("mp_dm_spawn", (160, 160, 16), 225), p.ent("mp_dm_spawn", (-160, 160, 16), 315)))
reg("gameplay", "gameplay_sd_spawn_pair", "Search-and-destroy attacker and defender spawns facing each other")(
    lambda p: (_spawn_floor(p, 256), p.ent("mp_sd_spawn_attacker", (-192, 0, 16), 0),
               p.ent("mp_sd_spawn_defender", (192, 0, 16), 180)))
reg("gameplay", "gameplay_minimap_corner_pair", "Two minimap corner markers at opposite corners of a 512 floor")(
    lambda p: (_spawn_floor(p, 256), p.ent("minimap_corner", (-240, -240, 0), 0),
               p.ent("minimap_corner", (240, 240, 0), 180)))


# ----------------------------------------------------------------------------- self tests
def sample_faces(index_brush: int):
    """Face lines of brush N in the sample map (text, stripped), for the sample box check."""
    lines = SAMPLE_MAP.read_text(encoding="utf-8").splitlines()
    marker = f"// brush {index_brush}"
    start = None
    for i, ln in enumerate(lines):
        if ln.strip() == marker and start is None:
            start = i
    if start is None:
        raise SystemExit(f"sample brush {index_brush} not found")
    out = []
    for ln in lines[start + 2:]:
        if ln.strip() == "}":
            break
        if ln.startswith(" ("):
            out.append(ln.strip())
    return out


def self_test():
    errors = []
    # Box generator must reproduce sample brush 0 (floor 512 x 512 x 16) and brush 5 (32 x 32 x 96 post).
    checks = [(0, (-256, -256, -16), (256, 256, 0)), (5, (-32, -32, 0), (32, 32, 96))]
    for idx, lo, hi in checks:
        want = sample_faces(idx)
        got = []
        for a, b, c, _ in box_faces(lo, hi, "X"):
            got.append(face_line(a, b, c, "X").strip())
        # Sample lines carry their own material; compare with that material substituted.
        mats = [re.search(r" (\S+) 128 128 0 0 0 0 lightmap_gray", w).group(1) for w in want]
        got = []
        for (a, b, c, _), m in zip(box_faces(lo, hi, "X"), mats):
            got.append(face_line(a, b, c, m).strip())
        if got != want:
            errors.append(f"box generator differs from sample brush {idx}")
    for brush in [box_faces((0, 0, 0), (10, 20, 30), "m")]:
        if not outward_ok(brush):
            errors.append("box faces not outward")
    pyr = hull_faces([(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0), (0, 0, 2)], "m")
    if len(pyr) != 5 or not outward_ok(pyr):
        errors.append("hull (pyramid) faces wrong")
    if errors:
        raise SystemExit("self-test failed: " + "; ".join(errors))


# ----------------------------------------------------------------------------- catalogue
def load_status(paths):
    names = {}
    for path in paths:
        for ln in path.read_text(encoding="utf-8").splitlines():
            if not ln or ln.startswith("#"):
                continue
            parts = ln.split("\t")
            if len(parts) >= 2:
                names[parts[0]] = parts[1]
    return names


TOOL_SUBSTRINGS = ("caulk", "clip", "nodraw", "trigger", "hint", "skip", "portal", "origin", "sky")


def is_tool(material: str) -> bool:
    m = material.lower()
    if any(t in m for t in TOOL_SUBSTRINGS):
        return True
    return m.rsplit("/", 1)[-1] == "ladder"


# ----------------------------------------------------------------------------- render and validate
def recentre(p: Prefab):
    """Shift x/y so the bounds centre sits on the origin; the floor (z=0) stays where it is."""
    xs = [q[0] for q in p.pts]
    ys = [q[1] for q in p.pts]
    dx = -(min(xs) + max(xs)) / 2.0
    dy = -(min(ys) + max(ys)) / 2.0
    d = (dx, dy, 0.0)

    def mv(faces):
        return [(add(a, d), add(b, d), add(c, d), m) for a, b, c, m in faces]

    p.world = [mv(br) for br in p.world]
    for e in p.ents:
        if e["pos"] is not None:
            e["pos"] = add(e["pos"], d)
        e["brushes"] = [mv(br) for br in e["brushes"]]
    p.pts = [add(q, d) for q in p.pts]


def bounds(p: Prefab):
    lo = tuple(min(q[i] for q in p.pts) for i in range(3))
    hi = tuple(max(q[i] for q in p.pts) for i in range(3))
    return lo, hi


def render(p: Prefab) -> str:
    out = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ']
    ents = [{"keys": [("classname", "worldspawn")], "pos": None, "yaw": 0, "brushes": p.world}] + p.ents
    for i, e in enumerate(ents):
        out.append(f"// entity {i}")
        out.append("{")
        for k, v in e["keys"]:
            out.append(f'"{k}" "{v}"')
        if e["pos"] is not None:
            out.append(f'"origin" "{vec(e["pos"])}"')
            out.append(f'"angles" "0 {fmt(e["yaw"])} 0"')
        for j, faces in enumerate(e["brushes"]):
            out.append(f"// brush {j}")
            out.append("{")
            for a, b, c, m in faces:
                out.append(face_line(a, b, c, m))
            out.append("}")
        out.append("}")
    return "\n".join(out) + "\n"


FACE_RE = re.compile(
    r"^ \( (\S+) (\S+) (\S+) \) \( (\S+) (\S+) (\S+) \) \( (\S+) (\S+) (\S+) \) (\S+) "
    r"(\S+) (\S+) (\S+) (\S+) (\S+) (\S+) lightmap_gray 16384 16384 0 0 0 0$"
)
KEY_RE = re.compile(r'^"([^"]+)" "([^"]*)"$')


def validate(text: str, name: str, ok_mats: set, ok_models: set):
    """Return (errors, materials_used, models_used)."""
    errs = []
    mats, models = set(), set()
    depth = 0
    if text.splitlines()[0] != "iwmap 4":
        errs.append("missing iwmap 4 header")
    entities = []     # (keys dict, brush count)
    cur = None
    for ln in text.splitlines():
        s = ln.strip()
        if s == "{":
            depth += 1
            if depth == 1 and cur is not None:
                cur["keys"] = {}
            continue
        if s == "}":
            depth -= 1
            if depth < 0:
                errs.append("brace underflow")
            if depth == 0 and cur is not None:
                entities.append(cur)
                cur = None
            continue
        if s.startswith("// entity"):
            cur = {"keys": {}, "brushes": 0}
            continue
        if s.startswith("// brush"):
            if cur is not None:
                cur["brushes"] += 1
            continue
        if ln.startswith(" ("):
            m = FACE_RE.match(ln)
            if not m:
                errs.append(f"unparseable face line: {ln[:90]}")
                continue
            try:
                [float(m.group(i)) for i in range(1, 10)]
            except ValueError:
                errs.append(f"non-numeric point: {ln[:90]}")
            mat = m.group(10)
            mats.add(mat)
            if not (is_tool(mat) or mat in ok_mats):
                errs.append(f"material not ok: {mat}")
            continue
        km = KEY_RE.match(s)
        if km and depth == 1 and cur is not None:
            cur["keys"][km.group(1)] = km.group(2)
            if km.group(1) == "model":
                models.add(km.group(2))
                if km.group(2) not in ok_models:
                    errs.append(f"model not ok: {km.group(2)}")
            continue
        if s in ("", "layer") or s.startswith("layer ") or s.startswith('"') or s.startswith("iwmap") or s.startswith("contents"):
            continue
        errs.append(f"unexpected line: {ln[:90]}")
    if depth != 0:
        errs.append(f"unbalanced braces (depth {depth})")
    # entity semantics
    targets = {e["keys"].get("targetname") for e in entities if e["keys"].get("classname") == "script_brushmodel"}
    seen_names = set()
    for e in entities:
        k = e["keys"]
        cls = k.get("classname")
        if cls == "trigger_use" and k.get("target") not in targets:
            errs.append(f"trigger_use target {k.get('target')} has no script_brushmodel")
        if cls == "script_brushmodel":
            if "iw8r_door" not in k or "targetname" not in k or e["brushes"] == 0:
                errs.append("script_brushmodel missing iw8r_door/targetname/brush")
        if cls == "light":
            for need in ("_color", "radius", "intensity"):
                if need not in k:
                    errs.append(f"light missing {need}")
            if "_color" in k and len(k["_color"].split()) != 3:
                errs.append("light _color must be r g b")
        if cls == "misc_model" and ("model" not in k or "origin" not in k):
            errs.append("misc_model missing model/origin")
        if "targetname" in k and cls in ("script_brushmodel", "trigger_use"):
            if k["targetname"] in seen_names:
                errs.append(f"duplicate targetname {k['targetname']}")
            seen_names.add(k["targetname"])
    if not entities or entities[0]["keys"].get("classname") != "worldspawn":
        errs.append("entity 0 must be worldspawn")
    return errs, mats, models


# ----------------------------------------------------------------------------- main
def main() -> int:
    self_test()
    status = load_status(MATERIAL_TSVS)
    ok_mats = {n for n, s in status.items() if s == "ok"}
    ok_models = {n for n, s in load_status([MODEL_TSV]).items() if s == "ok"}
    for name in set(M.values()):
        if name not in ok_models:
            raise SystemExit(f"stock model not ok: {name}")
    for name in (MAT_CONCRETE, MAT_PLASTER, MAT_BRICK, MAT_WOOD, MAT_WOOD_DOOR, MAT_METAL, MAT_METAL_PLATE,
                 MAT_FLOOR, MAT_ROOF, MAT_GLASS, MAT_CAULK):
        if name not in ok_mats:
            raise SystemExit(f"stock material not ok: {name}")

    slugs = set()
    built = []  # (category, slug, title, path_rel, text, size, mats, models)
    all_errors = []
    for category, slug, title, fn in REG:
        if slug in slugs:
            raise SystemExit(f"duplicate slug {slug}")
        slugs.add(slug)
        p = Prefab(category, slug, title)
        fn(p)
        recentre(p)
        lo, hi = bounds(p)
        size = "x".join(fmt(hi[i] - lo[i]) for i in range(3))
        text = render(p)
        errs, mats, models = validate(text, slug, ok_mats, ok_models)
        mats = {m for m in mats}
        for e in errs:
            all_errors.append(f"{category}/{slug}: {e}")
        rel = f"data/prefabs/iw8r/{category}/{slug}.map"
        built.append((category, slug, title, rel, text, size, sorted(mats), sorted(models)))

    if all_errors:
        for e in all_errors:
            print("ERROR", e, file=sys.stderr)
        print(f"{len(all_errors)} validation error(s); nothing written", file=sys.stderr)
        return 1

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    for category, slug, title, rel, text, size, mats, models in built:
        path = ROOT / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")

    index = ["path\tcategory\ttitle\tsize\tmaterials\tmodels"]
    for category, slug, title, rel, text, size, mats, models in built:
        index.append("\t".join([rel.replace("data/prefabs/iw8r/", ""), category, title, size,
                                ";".join(mats), ";".join(models)]))
    (OUT_DIR / "index.tsv").write_text("\n".join(index) + "\n", encoding="utf-8", newline="\n")

    cats = []
    for category, *_ in built:
        if category not in cats:
            cats.append(category)
    readme = [
        "# IW8 Radiant prefab library",
        "",
        "Generated by `tools/build_prefab_library.py` (run `py -3 -I tools/build_prefab_library.py`).",
        "Each `.map` is an `iwmap 4` file in the sample-room format. Origin is the floor centre (x/y centred, floor top at z=0).",
        "",
        "Conventions and caveats:",
        "- Walls, floors, ramps, pillars, roofs and the tool brushes (`trigger`, `ladder`) are generated brushes.",
        "- Doors, windows, railings, fences, stairs, ladders, lamps, cover and props are stock `misc_model` entities.",
        "- Openable doors use `script_brushmodel` with `iw8r_door` (`rotate 90 1 3`, or `slide 0 0 112 1 3` for the garage),",
        "  the door panel is a brush, and a `trigger_use` brush (material `trigger`) with `target` pointing at it.",
        "  Door panels are brushes, not stock door models on `script_model`; static doors use stock leaves.",
        "- Glass is `tmo/ee_light_fixture_lamp_short_glass` (blended, `_bad_` techset in the material cache).",
        "- Stock model placement is an estimate: the catalog has no bounds, so footprints and offsets (stair module",
        f"  run {fmt(STEP_RUN)} x rise {fmt(STEP_RISE)}, pillar rows, stacked crates, prop heights) must be checked in the editor.",
        "- Model facing (yaw) for frames and stairs is assumed; verify orientation before use.",
        "- Targetnames are unique per file (`door1`, `door1_use`, ...); the editor's misc_prefab overrides may rename them.",
        "",
        "## Categories",
        "",
    ]
    for cat in cats:
        items = [b for b in built if b[0] == cat]
        readme.append(f"### {cat} ({len(items)})")
        readme.append("")
        for _, slug, title, rel, _, size, _, _ in items:
            readme.append(f"- `{cat}/{slug}.map`: {title} (size {size})")
        readme.append("")
    readme.append(f"Total: {len(built)} prefabs. Full list with materials and models: `index.tsv`.")
    (OUT_DIR / "README.md").write_text("\n".join(readme) + "\n", encoding="utf-8", newline="\n")

    print(f"wrote {len(built)} prefabs under {OUT_DIR}")
    for cat in cats:
        print(f"  {cat}: {sum(1 for b in built if b[0] == cat)}")
    used_mats = sorted({m for b in built for m in b[6]})
    used_models = sorted({m for b in built for m in b[7]})
    print(f"materials used: {len(used_mats)}; models used: {len(used_models)}")
    print("validation: all prefabs passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
