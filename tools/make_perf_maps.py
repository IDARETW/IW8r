"""Scenes for the viewport benchmark (docs/research/85_viewport_performance.md).

Writes under out/perf/ (nothing else is touched):
- maps/synth_<N>.map for N = 2000, 10000, 50000 brushes: small buildings on a grid, a few hundred point entities
  (spawns, lights, script_origin, sound_emitter) and misc_models of stock models the fix1 test map uses;
- fix1/: a copy of the owner's test map folder out/agent/mp_iw8r_fix1 (the original is only read);
- cache/model_cache and cache/material_cache: copies of just the stock models and materials these maps use, read
  from the editor's caches (which are only read). The benchmark points the editor at these copies with
  IW8R_MODEL_CACHE_ROOT / IW8R_MATERIAL_CACHE_ROOT, so the real caches are never written.

Usage: python -I tools/make_perf_maps.py [--out out/perf] [--no-fix1] [--no-cache]
"""
import argparse
import json
import os
import random
import re
import shutil

TAIL = " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"
# Stock materials the fix1 map uses; more cached ones are added for a realistic number of batches.
BASE_MATERIALS = [
    "tm2co/me_plaster_barrier_fence_wall",
    "tm2co/ee_trim_beam_airport_concrete",
    "tm2co/me_curb_stone_01",
    "tm2co/destruction_debris_plaster_concrete_edge_01",
]
# (model, share): stock models placed in the fix1 map. The traffic cone is light (1,004 triangles), the others are
# 9,000 to 11,000 triangles each.
MODELS = [
    ("com_trafficcone_01_dyn", 0.90),
    ("ee_food_fruit_cardboard_box_01", 0.05),
    ("tm/ladder_old_wood_01_160", 0.03),
    ("opforce_juggernaut_prop_static", 0.02),
]
SIZES = {2000: (200, 500), 10000: (300, 1500), 50000: (500, 3000)}  # brushes: (point entities, misc_models)
USER_CACHE = os.path.join(os.environ.get("LOCALAPPDATA", ""), "IW8 Radiant", "IW8 Radiant")


def box(x0, y0, z0, x1, y1, z1, mat, top=None):
    """Six Radiant face lines with outward normals (as tools/make_iw8_sample_room.py)."""
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


def building(rng, ox, oy, mats):
    """16 brushes: floor, four walls, roof, four pillars, two beams, four crates."""
    wall, trim, floor, crate = (rng.choice(mats) for _ in range(4))
    h = rng.choice([128, 160, 192, 256])
    s = 192  # half size
    out = [box(ox - s, oy - s, -16, ox + s, oy + s, 0, floor)]
    out.append(box(ox - s, oy - s - 16, 0, ox + s, oy - s, h, wall))
    out.append(box(ox - s, oy + s, 0, ox + s, oy + s + 16, h, wall))
    out.append(box(ox - s - 16, oy - s, 0, ox - s, oy + s, h, wall))
    out.append(box(ox + s, oy - s, 0, ox + s + 16, oy + 32, h, wall, top=trim))
    out.append(box(ox - s - 16, oy - s - 16, h, ox + s + 16, oy + s + 16, h + 16, trim))
    for px, py in ((-96, -96), (96, -96), (96, 96), (-96, 96)):
        out.append(box(ox + px - 12, oy + py - 12, 0, ox + px + 12, oy + py + 12, h, trim))
    out.append(box(ox - s, oy - 8, h - 24, ox + s, oy + 8, h, trim))
    out.append(box(ox - 8, oy - s, h - 24, ox + 8, oy + s, h, trim))
    for _ in range(4):
        cx, cy = ox + rng.randrange(-160, 161, 16), oy + rng.randrange(-160, 161, 16)
        c = rng.choice([16, 24, 32])
        out.append(box(cx - c, cy - c, 0, cx + c, cy + c, 2 * c, crate))
    return out


def synth(n_brushes, n_points, n_models, mats, seed):
    rng = random.Random(seed)
    n_build = (n_brushes + 15) // 16
    side = int(n_build ** 0.5 + 0.999)
    cells = [(i, j) for j in range(side) for i in range(side)][:n_build]
    rng.shuffle(cells)  # document order: together inside a building, buildings in no spatial order
    pitch = 512
    centre = lambda c: ((c[0] - side / 2 + 0.5) * pitch, (c[1] - side / 2 + 0.5) * pitch)
    L = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ', "// entity 0", "{", '"classname" "worldspawn"',
         '"sundirection" "-50 35 0"', '"suncolor" "1 0.95 0.85"']
    brushes = []
    for c in cells:
        ox, oy = centre(c)
        brushes.extend(building(rng, int(ox), int(oy), mats))
    brushes = brushes[:n_brushes]
    for i, b in enumerate(brushes):
        L.append(f"// brush {i}")
        L.append("{")
        L.extend(b)
        L.append("}")
    L.append("}")
    ent = [1]

    def entity(kvs, prims=()):
        L.append(f"// entity {ent[0]}")
        L.append("{")
        for k, v in kvs:
            L.append(f'"{k}" "{v}"')
        for i, p in enumerate(prims):
            L.append(f"// brush {i}")
            L.append("{")
            L.extend(p)
            L.append("}")
        L.append("}")
        ent[0] += 1

    def spot():
        ox, oy = centre(rng.choice(cells))
        return int(ox) + rng.randrange(-160, 161, 8), int(oy) + rng.randrange(-160, 161, 8)

    entity([("classname", "info_player_start"), ("origin", "0 0 8"), ("angles", "0 90 0")])
    kinds = ["mp_tdm_spawn", "script_origin", "sound_emitter", "mp_dm_spawn_start", "script_struct"]
    n_lights = min(40, n_points // 5)
    for k in range(n_points - 1):
        x, y = spot()
        if k < n_lights:
            col = rng.choice(["1 0.9 0.7", "0.7 0.8 1", "1 0.6 0.4", "0.8 1 0.8"])
            entity([("classname", "light"), ("origin", f"{x} {y} 96"), ("radius", str(rng.choice([300, 400, 600]))),
                    ("_color", col), ("intensity", "1")])
        else:
            cls = kinds[k % len(kinds)]
            kv = [("classname", cls), ("origin", f"{x} {y} 8")]
            if "spawn" in cls:
                kv.append(("angles", f"0 {rng.randrange(0, 360, 45)} 0"))
            entity(kv)
    # A few tool volumes (drawn as wire boxes, never cast shadows).
    for k in range(max(4, n_build // 100)):
        x, y = spot()
        entity([("classname", "trigger_multiple"), ("targetname", f"perf_trig_{k}")],
               [box(x - 48, y - 48, 0, x + 48, y + 48, 96, "trigger")])
    names = [m for m, _ in MODELS]
    weights = [w for _, w in MODELS]
    for _ in range(n_models):
        x, y = spot()
        m = rng.choices(names, weights)[0]
        entity([("classname", "misc_model"), ("model", m), ("origin", f"{x} {y} 0"),
                ("angles", f"0 {rng.randrange(0, 360, 15)} 0")])
    return "\r\n".join(L) + "\r\n"


def safe_file_name(name):
    return "".join(c if (ord(c) < 128 and c.isalnum()) or c in "_-." else "_" for c in name)


def copy_file(src_root, rel, dst_root):
    if not rel:
        return 0
    src, dst = os.path.join(src_root, rel), os.path.join(dst_root, rel)
    if not os.path.isfile(src):
        return 0
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.path.isfile(dst) or os.path.getsize(dst) != os.path.getsize(src):
        shutil.copyfile(src, dst)
    return os.path.getsize(dst)


def map_materials(text):
    # A brush face line: three points, then the material and six numbers.
    return set(re.findall(r" \) ([A-Za-z0-9_/#.&-]+) -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ ", text))


def map_models(text):
    return set(re.findall(r'"model" "([^"]+)"', text))


def build_caches(out, map_texts, extra_materials):
    """Subset copies of the model and material caches (the user's caches are only read)."""
    mdl_src, mat_src = os.path.join(USER_CACHE, "model_cache"), os.path.join(USER_CACHE, "material_cache")
    mdl_dst, mat_dst = os.path.join(out, "cache", "model_cache"), os.path.join(out, "cache", "material_cache")
    models, materials = set(), set(extra_materials)
    for t in map_texts:
        models |= map_models(t)
        materials |= map_materials(t)
    total = 0
    mdl_index = {"format": "", "models": {}}
    if os.path.isfile(os.path.join(mdl_src, "index.json")):
        src = json.load(open(os.path.join(mdl_src, "index.json"), encoding="utf-8"))
        mdl_index["format"] = src.get("format", "")
        for name in sorted(models):
            e = src.get("models", {}).get(name)
            if not e:
                continue
            mdl_index["models"][name] = e
            total += copy_file(mdl_src, e.get("glb", ""), mdl_dst)
            total += copy_file(mdl_src, os.path.join("detail", safe_file_name(name) + ".json"), mdl_dst)
            materials |= set(e.get("materials", []))
    os.makedirs(os.path.join(mdl_dst, "glb"), exist_ok=True)
    json.dump(mdl_index, open(os.path.join(mdl_dst, "index.json"), "w", encoding="utf-8"))
    mat_index = {"format": "", "materials": {}}
    if os.path.isfile(os.path.join(mat_src, "index.json")):
        src = json.load(open(os.path.join(mat_src, "index.json"), encoding="utf-8"))
        mat_index["format"] = src.get("format", "")
        for name in sorted(materials):
            e = src.get("materials", {}).get(name)
            if not e:
                continue
            mat_index["materials"][name] = e
            for v in e.values():
                if isinstance(v, str) and v.startswith("images/"):
                    total += copy_file(mat_src, v, mat_dst)
    os.makedirs(os.path.join(mat_dst, "images"), exist_ok=True)
    json.dump(mat_index, open(os.path.join(mat_dst, "index.json"), "w", encoding="utf-8"))
    print(f"cache copies: {len(mdl_index['models'])} models, {len(mat_index['materials'])} materials, {total / 1048576:.0f} MB")


def cached_extra_materials(count):
    """More stock world materials that are already cached (sorted, so the pick is stable)."""
    idx = os.path.join(USER_CACHE, "material_cache", "index.json")
    if not os.path.isfile(idx):
        return []
    mats = json.load(open(idx, encoding="utf-8")).get("materials", {})
    ok = [m for m, e in sorted(mats.items())
          if m.startswith("tm2co/") and not e.get("failed") and e.get("color") and not e.get("mask")
          and os.path.isfile(os.path.join(USER_CACHE, "material_cache", e["color"])) and m not in BASE_MATERIALS]
    step = max(1, len(ok) // count)
    return ok[::step][:count]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join("out", "perf"))
    ap.add_argument("--no-fix1", action="store_true")
    ap.add_argument("--no-cache", action="store_true")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, "maps"), exist_ok=True)
    extra = cached_extra_materials(20)
    mats = BASE_MATERIALS + extra
    texts = []
    for n, (points, models) in SIZES.items():
        text = synth(n, points, models, mats, seed=n)
        with open(os.path.join(a.out, "maps", f"synth_{n}.map"), "w", newline="") as f:
            f.write(text)
        texts.append(text)
        print(f"synth_{n}.map: {n} brushes, {points} point entities, {models} models, {len(text) / 1048576:.1f} MB")
    for src in (os.path.join("tests", "data", "iw8_sample_room.map"),):
        if os.path.isfile(src):
            texts.append(open(src, encoding="utf-8", errors="replace").read())
    fix1_src = os.path.join("out", "agent", "mp_iw8r_fix1")
    if not a.no_fix1 and os.path.isdir(fix1_src):
        dst = os.path.join(a.out, "fix1")
        shutil.copytree(fix1_src, dst, dirs_exist_ok=True)
        print("fix1 copied to", dst)
    fix1_map = os.path.join(a.out, "fix1", "mp_iw8r_fix1.map")
    if os.path.isfile(fix1_map):
        texts.append(open(fix1_map, encoding="utf-8", errors="replace").read())
    if not a.no_cache:
        build_caches(a.out, texts, mats)


if __name__ == "__main__":
    main()
