#!/usr/bin/env python3
"""Group stock IW8 models and materials into kits (roadmap 4.23).

Run:  py -3 -I tools\\build_asset_kits.py

Reads (data/asset_catalog/):
  iw8_models.tsv, iw8_models_status.tsv        stock xmodels, ok / missing per model
  iw8_materials.tsv, iw8_materials_status.tsv,
  iw8_materials_status_b.tsv                   stock materials, ok / grey / missing
  iw8_stock_maps.tsv                           map ids and kind (mp / cp / other)
Writes:
  iw8_kits.tsv   columns: kit, kind (model|material), name, buildable (yes|no|unknown)

Classification is the RULES table below. Rules are checked top to bottom and the
first rule whose pattern matches (and whose exclude pattern does not) wins, so a
name lands in exactly one kit. Anything no rule matches is "unclassified" and is
left that way on purpose: a wrong kit is worse than no kit.

Name handling:
  * Names are matched lower-cased, with the full path (folder prefix included).
  * A leading "~" or "#" in a catalog line is a marker, not part of the name. It is
    stripped for the output name and for status lookups. When the plain and the
    marked form of the same name both exist they become one row.

Buildable (from the status files):
  models:    ok -> yes, missing -> no, not listed -> unknown
  materials: ok -> yes, grey or missing -> no ("grey" = present but not exported),
             not listed -> unknown
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAT = os.path.join(ROOT, "data", "asset_catalog")
OUT = os.path.join(CAT, "iw8_kits.tsv")

UNCLASSIFIED = "unclassified"


def W(*words):
    """Whole keyword: not preceded or followed by a letter. Digits, '_', '/', '-', '.' are boundaries."""
    return r"(?<![a-z])(?:" + "|".join(words) + r")(?![a-z])"


def S(*words):
    """Keyword at the start of the name or of its basename (after a folder 'x/')."""
    return r"(?:^|/)(?:" + "|".join(words) + r")(?![a-z])"


# (kit, scope, include pattern, exclude pattern or None)
# scope: "both", "model" or "material". Order matters: first match wins.
RULES = [
    # ---- excluded: kept visible in the output but kept out of the prop kits
    ("excluded_ui", "both",
     W("ui", "hud", "snapshot", "waypoint", "reticle", "crosshair", "minimap", "icon", "icons"), None),
    ("excluded_engine", "both",
     W("prepass", "shadowcaster", "depthtest", "debug", "techset", "techsets", "gfx", "fontcache",
       "rasterized", "msaa", "culling")
     + r"|(?:^|/)(?:white|black)(?:_|$)", None),
    ("excluded_characters", "both",
     S("body", "head", "hat", "hero", "sas", "cosmetic", "parts_wristwear", "charm", "clothing",
       "vm_mp", "mp_western", "mp_eastern", "mp_codl", "dw_operator", "viewhands", "zmb")
     + "|" + W("helmet", "helmets"), None),
    ("excluded_weapons", "both",
     S("attachment", "attachments", "weapon", "iw8_cw", "offhand", "perk", "ks", "lm_heal", "tool_execution",
       "killstreak")
     + "|" + W("vm", "wm", "viewmodel", "grenade", "dropclip", "magazine", "mag", "mags", "xmags", "ammobelt",
               "bipod", "silencer", "suppressor", "compensator", "muzzlebrake", "muzzle", "flashhider",
               "reflex", "holo", "scope", "scopes", "receiver", "reciever", "pistolgrip", "trigcust",
               "stubby", "vertgrip", "griptape", "minireddot", "execution", "bomb", "bombs"),
     W("vfx", "fontcache", "wall", "walls")),
    ("gameplay_objects", "both",
     S("art", "dz", "carepackage", "trophy") + "|" + W("loot", "flare", "flares", "respawn", "scriptable",
                                                       "explode"), None),

    # ---- map geometry and street-level clutter
    ("world_chunks", "both", S("donetsk"), None),
    ("decals", "both", S("decal"), None),
    ("debris", "both",
     W("debris", "rubble", "chunk", "chunks", "shard", "shards", "splinter", "splinters", "casing", "casings",
       "gib", "gibs", "dismembered", "junk", "litter", "scrap", "fragment", "fragments",
       "trash"), W("food", "container", "trash_can", "trashcan")),

    # ---- vehicles and effects (before street furniture so veh8 bench parts stay with the vehicle)
    ("vehicles_military", "both", S("veh8_mil") + "|" + W("helicopter", "humvee", "apc"), None),
    ("vehicles_civilian", "both",
     S("veh8_civ") + "|" + W("car", "cars", "truck", "trucks", "van", "vans", "sedan", "taxi",
                             "motorcycle", "scooter", "trike", "bicycle", "bicycles"), None),
    ("vehicles_other", "both",
     S("veh8") + "|" + W("vehicle", "vehicles", "wreck", "plane", "planes", "airplane", "aircraft", "boat",
                         "boats", "yacht", "ship", "ships", "wheel", "wheels", "tire", "tires", "tyre",
                         "tyres"), None),
    ("effects_vfx", "both", W("vfx"), None),
    ("street_furniture", "both",
     W("hydrant", "hydrants", "bollard", "bollards", "bench", "benches", "parking", "meter", "meters",
       "bus_stop", "bikerack", "mailbox", "postbox", "trash_can", "trashcan", "trashcans", "bin", "bins",
       "newspaper", "vending", "kiosk", "atm", "streetlamp", "recycling"), W("food")),

    # ---- decals, lights, electronics, pipes
    ("decals", "both",
     W("decal", "decals", "graffiti", "stencil", "stencils", "sticker", "stickers", "mural", "splat",
       "splatter", "label", "labels"), None),
    ("lights_fixtures", "both",
     W("light", "lights", "lighting", "lamp", "lamps", "lantern", "lanterns", "sconce", "chandelier",
       "bulb", "bulbs", "spotlight", "spotlights", "floodlight", "floodlights"),
     W("pallet", "boxes", "box", "window", "frame", "frames")),
    ("electronics", "both",
     W("television", "tv", "monitor", "monitors", "screen", "screens", "computer", "computers", "router",
       "routers", "phone", "phones", "radio", "speaker", "speakers", "server", "servers", "printer",
       "keyboard", "laptop", "tablet", "electronics", "electronic", "spy", "camera", "cameras", "console",
       "outlet", "outlets", "switch", "switches", "keypad", "alarm", "alarms"), None),
    ("pipes_wires", "both",
     W("pipe", "pipes", "pipework", "conduit", "conduits", "wire", "wires", "cable", "cables", "hose",
       "hoses", "duct", "ducts", "gutter", "gutters"), W("barbed")),

    # ---- vegetation and terrain
    ("water", "both",
     W("water", "puddle", "puddles", "splash", "splashes", "river", "rivers", "ocean", "lake", "sea",
       "wave", "waves", "pool", "pools", "fountain"), W("cooler")),
    ("foliage_bushes", "both",
     W("bush", "bushes", "shrub", "shrubs", "hedge", "hedges", "flower", "flowers", "grass", "fern", "ferns",
       "vine", "vines", "ivy", "plant", "plants", "planter", "planters", "camomile", "poppy", "cactus",
       "weed", "weeds", "reed", "reeds", "willow"), None),
    ("foliage_trees", "both",
     W("tree", "trees", "palm", "palms", "spruce", "birch", "pine", "pines", "oak", "cypress", "poplar",
       "sapling", "saplings"), W("bush", "bushes", "shrub", "flower", "planter")),
    ("foliage_misc", "both", W("foliage", "leaf", "leaves", "moss", "bark", "mulch"), None),
    # ---- doors, glass, signs, barriers, fences, containers
    ("doors_windows", "both",
     W("door", "doors", "gate", "gates", "window", "windows", "shutter", "shutters", "hatch", "hatches",
       "garage", "blinds", "curtain", "curtains"), W("cabinet", "cabinets", "shelf")),
    ("glass", "both", W("glass", "windshield"),
     W("sunglasses", "sun_glasses", "glasses", "bottle", "bottles", "shooting")),
    ("signs", "both",
     W("sign", "signs", "signage", "billboard", "billboards", "banner", "banners"), None),
    ("barriers", "both",
     W("barrier", "barriers", "sandbag", "sandbags", "jersey", "trafficcone", "traffic", "cone", "cones",
       "barricade", "barricades"), W("stair", "stairs")),
    ("fences", "both",
     W("fence", "fences", "railing", "railings", "handrail", "handrails", "picket", "pickets", "chainlink",
       "barbed"),
     W("stair", "stairs", "staircase")),
    ("containers", "both",
     W("container", "containers", "dumpster", "dumpsters", "barrel", "barrels", "drum", "drums", "canister",
       "canisters", "jerrycan", "jerrycans", "tank", "tanks"), W("cinder", "cinderblock")),
    ("crates_boxes", "both",
     W("crate", "crates", "box", "boxes", "pallet", "pallets", "tote", "totes", "cardboard", "carton",
       "cartons", "wrapped"), W("cinder", "cinderblock")),

    # ---- interiors and machinery
    ("furniture", "both",
     W("chair", "chairs", "table", "tables", "desk", "desks", "sofa", "sofas", "couch", "couches", "bed",
       "beds", "shelf", "shelves", "shelving", "cabinet", "cabinets", "stool", "stools", "wardrobe",
       "bookcase", "bookshelf", "rug", "rugs", "carpet", "carpets", "mattress", "dresser", "counter",
       "counters", "furniture", "appliance", "appliances", "fridge", "refrigerator", "stove", "oven",
       "microwave", "sink", "sinks", "toilet", "bathtub", "shower", "radiator", "locker", "lockers"), None),
    ("machinery_industrial", "both",
     W("machinery", "machine", "machines", "pulley", "hoist", "crane", "cranes", "generator", "generators",
       "compressor", "compressors", "reservoir", "forklift", "pump", "pumps", "valve", "valves", "conveyor",
       "conveyors"), None),

    # ---- soft goods, range targets, tools (before small props)
    ("terrain", "both",
     W("terrain", "mountain", "mountains", "mud", "ground", "asphalt", "pavement", "road",
       "roads", "sidewalk", "sidewalks", "curb", "curbs", "hill", "hills", "trench", "trenches"),
     W("wall", "walls", "buildup", "plaster")),
    ("rocks", "both", W("rock", "rocks", "rocky", "boulder", "boulders", "cliff", "cliffs", "pebble",
                        "pebbles", "riverbed"), None),
    ("soft_goods", "model",
     W("fabric", "tarp", "tarps", "blanket", "blankets", "flag", "flags", "sweater", "clothes", "clothing",
       "sportswear"), None),
    ("range_targets", "both", W("shooting", "targets", "target"), None),
    ("tools_hardware", "both",
     W("hardhat", "hardhats", "saw", "saws", "broom", "mop", "bucket", "buckets", "ladder", "ladders",
       "hammer", "shovel", "drill", "wrench", "wrenches", "tool", "tools", "crowbar", "axe", "hatchet"), None),

    # ---- materials: surface families (material scope only, before building parts)
    ("surface_concrete_masonry", "material",
     W("concrete", "masonry", "brick", "bricks", "cinder", "cinderblock", "stone", "stones", "marble"), None),
    ("surface_wood", "material",
     W("wood", "wooden", "timber", "plywood", "boards", "board", "bamboo"), None),
    ("surface_metal", "material",
     W("metal", "metals", "steel", "iron", "aluminum", "aluminium", "corrugated", "rust", "rusty", "rusted",
       "chrome"), None),
    ("surface_plaster_paint", "material",
     W("plaster", "stucco", "drywall", "wallpaper", "paint", "painted", "paper", "ceramic"), None),
    ("surface_fabric", "material",
     W("fabric", "cloth", "canvas", "tarp", "tarps", "leather", "upholstery", "denim", "cotton", "wool"),
     None),
    ("surface_plastic_rubber", "material",
     W("plastic", "rubber", "vinyl", "pvc", "foam"), None),

    # ---- building parts and small props (last)
    ("building_parts", "both",
     W("wall", "walls", "roof", "roofs", "floor", "floors", "ceiling", "ceilings", "stair", "stairs",
       "staircase", "pillar", "pillars", "column", "columns", "beam", "beams", "trim", "catwalk", "catwalks",
       "balcony", "balconies", "brick", "bricks", "cinder", "cinderblock", "plaster", "stucco", "drywall",
       "siding", "shingles", "shingle", "lip", "cornice", "corner", "support", "supports", "plank", "planks",
       "scaffold", "scaffolding", "facade", "foundation", "slab", "slabs", "pier", "piers", "bridge",
       "building", "buildings", "house", "houses", "parapet", "ledge", "porch", "eave", "eaves", "tile",
       "tiles", "masonry", "concrete"), None),
    ("small_props", "both",
     W("binder", "binders", "stapler", "food", "bottle", "bottles", "cup", "cups", "mug", "mugs", "can",
       "cans", "jar", "jars", "toy", "toys", "ball", "balls", "bag", "bags", "knife", "knives", "utensil",
       "utensils", "plate", "plates", "book", "books", "magazine", "magazines", "pen", "pens", "clock",
       "clocks", "medical", "syringe", "bread", "snack", "soda", "candy", "cookie", "pot", "pots", "pan",
       "pans", "kettle", "vase", "basket", "baskets", "shoe", "shoes", "dish", "dishes", "kitchen", "office",
       "toiletries", "toiletry", "cigarette", "cigarettes", "mask", "masks"), None),
]

_COMPILED = [(kit, scope, re.compile(inc), re.compile(exc) if exc else None)
             for kit, scope, inc, exc in RULES]
KIT_ORDER = list(dict.fromkeys([kit for kit, _, _, _ in RULES] + [UNCLASSIFIED]))
EXCLUDED_KITS = {k for k in KIT_ORDER if k.startswith("excluded_")}


def classify(name, kind):
    """Return the kit for one stripped name. kind is 'model' or 'material'."""
    low = name.lower()
    for kit, scope, inc, exc in _COMPILED:
        if scope != "both" and scope != kind:
            continue
        if inc.search(low) and not (exc and exc.search(low)):
            return kit
    return UNCLASSIFIED


def strip_marker(raw):
    return raw.lstrip("~#")


def read_rows(path):
    """Yield the data rows (list of columns) of a catalog TSV. '# ' lines are comments, '#name' rows are data."""
    if not os.path.exists(path):
        return
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\r\n")
            if not line:
                continue
            if line.startswith("#") and (len(line) == 1 or line[1] in " \t"):
                continue
            yield line.split("\t")


def load_status(paths):
    """Map stripped name -> status. A plain name wins over a marked one; the first file wins over later files."""
    plain, marked = {}, {}
    for path in paths:
        for cols in read_rows(path):
            if len(cols) < 2:
                continue
            raw, status = cols[0], cols[1].strip()
            stripped = strip_marker(raw)
            target = plain if raw == stripped else marked
            target.setdefault(stripped, status)
    merged = dict(marked)
    merged.update(plain)
    return merged


def load_names(path):
    """Map stripped name -> zone list (empty when the file has no zone column). Plain name wins."""
    plain, marked = {}, {}
    for cols in read_rows(path):
        if not cols or not cols[0].strip():
            continue
        raw = cols[0].strip()
        zones = [z for z in (cols[1].split(",") if len(cols) > 1 else []) if z]
        stripped = strip_marker(raw)
        target = plain if raw == stripped else marked
        target.setdefault(stripped, zones)
    merged = dict(marked)
    merged.update(plain)
    return merged


def load_map_kinds():
    kinds = {}
    for cols in read_rows(os.path.join(CAT, "iw8_stock_maps.tsv")):
        if len(cols) >= 2:
            kinds[cols[0].strip()] = cols[1].strip()
    return kinds


def buildable_value(kind, status):
    if status is None:
        return "unknown"
    if kind == "model":
        return {"ok": "yes", "missing": "no"}.get(status, "unknown")
    return {"ok": "yes", "grey": "no", "missing": "no"}.get(status, "unknown")


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    models = load_names(os.path.join(CAT, "iw8_models.tsv"))
    materials = load_names(os.path.join(CAT, "iw8_materials.tsv"))
    model_status = load_status([os.path.join(CAT, "iw8_models_status.tsv")])
    material_status = load_status([os.path.join(CAT, "iw8_materials_status.tsv"),
                                   os.path.join(CAT, "iw8_materials_status_b.tsv")])
    map_kinds = load_map_kinds()

    rows = []  # (kit, kind, name, buildable, zones)
    for kind, names, status in (("model", models, model_status),
                                ("material", materials, material_status)):
        for name in sorted(names):
            kit = classify(name, kind)
            buildable = buildable_value(kind, status.get(name))
            rows.append((kit, kind, name, buildable, names[name]))

    rows.sort(key=lambda r: (KIT_ORDER.index(r[0]) if r[0] in KIT_ORDER else 999, r[1], r[2]))

    os.makedirs(CAT, exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# IW8 Radiant stock kits (roadmap 4.23). Generated by tools/build_asset_kits.py\n")
        fh.write("# Rules: first match wins in RULES (tools/build_asset_kits.py). buildable from the status files.\n")
        fh.write("# Re-run: py -3 -I tools\\build_asset_kits.py\n")
        fh.write("kit\tkind\tname\tbuildable\n")
        for kit, kind, name, buildable, _ in rows:
            fh.write(f"{kit}\t{kind}\t{name}\t{buildable}\n")

    # ---- diagnostics
    per_kit = collections.defaultdict(lambda: collections.Counter())
    buildable_by_kit = collections.defaultdict(lambda: collections.Counter())
    maps_by_kit = collections.defaultdict(set)
    zone_by_kit = collections.defaultdict(collections.Counter)
    for kit, kind, name, buildable, zones in rows:
        per_kit[(kit, kind)][name] = 1
        buildable_by_kit[(kit, kind)][buildable] += 1
        if kind == "model":
            for z in zones:
                if z in map_kinds:
                    maps_by_kit[kit].add(z)
                    zone_by_kit[kit][z] += 1

    totals = {"model": len(models), "material": len(materials)}
    print(f"wrote {OUT}")
    print(f"rows: {len(rows)} (models {totals['model']}, materials {totals['material']})")
    for kind in ("model", "material"):
        unc = sum(1 for r in rows if r[1] == kind and r[0] == UNCLASSIFIED)
        tot = totals[kind]
        share = (100.0 * unc / tot) if tot else 0.0
        print(f"{kind}s unclassified: {unc} of {tot} ({share:.1f}%)")
    print()
    print(f"{'kit':<26}{'kind':<10}{'count':>8}   yes/no/unknown")
    for kit in KIT_ORDER:
        for kind in ("model", "material"):
            key = (kit, kind)
            if key not in per_kit:
                continue
            c = buildable_by_kit[key]
            print(f"{kit:<26}{kind:<10}{len(per_kit[key]):>8}   "
                  f"{c['yes']}/{c['no']}/{c['unknown']}")
    print()
    print("stock-map usage (models only, maps that list the kit's models):")
    for kit in KIT_ORDER:
        if kit in maps_by_kit:
            top = ", ".join(z for z, _ in zone_by_kit[kit].most_common(3))
            print(f"  {kit:<26}{len(maps_by_kit[kit]):>3} maps   top: {top}")


if __name__ == "__main__":
    main()
