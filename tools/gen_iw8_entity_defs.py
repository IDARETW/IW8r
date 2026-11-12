#!/usr/bin/env python3
"""Generate data/iw8_entity_defs.json: the IW8 Radiant entity definitions for MW2019 Replay 1.20.

This is the equivalent of CoD Radiant's entities.def. Each class has a description, a box (point
classes), an editor colour, whether the build accepts it, and its documented keys.

Usage (from any folder):
    py -3.11 -I tools/gen_iw8_entity_defs.py

Inputs (read only):
    data/iw8_entity_classes.json            46 stock classes with keys and example values (3 stock maps)
    src/core/compile_iw8.cpp                acceptedPointClass(): the converter's closed class list
    data/generated/iw8_canonical_keys.tsv   canonical key names (used only to warn about unknown key names)
Output:
    data/iw8_entity_defs.json

Provenance tags used in the descriptions:
    gsc <path>:<lines>      decompiled Replay 1.20.4 scripts, relative to
                            E:\\mw124\\tools\\dvar_search_web\\src\\combined\\gsc\\1.20.4-replay\\Init\\scripts\\mp\\
    gsc-raw <path>:<lines>  scripts relative to E:\\mw124\\tools\\dvar_search_web\\src\\combined\\rawfile_source\\scripts\\mp\\
                            (the build version of this tree is not confirmed)
    contract 15 sN.N        docs/research/15_iw8_extraction_contract.md section N.N
    inferred from stock values
                            no source found; the meaning is a guess from the stock map values
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STOCK_PATH = ROOT / "data" / "iw8_entity_classes.json"
COMPILE_PATH = ROOT / "src" / "core" / "compile_iw8.cpp"
KEYS_TSV_PATH = ROOT / "data" / "generated" / "iw8_canonical_keys.tsv"
OUT_PATH = ROOT / "data" / "iw8_entity_defs.json"

# Box corners relative to the origin. Player spawns sit at the feet (z from 0 to 72).
PLAYER_BOX = ([-16, -16, 0], [16, 16, 72])
SMALL_BOX = ([-8, -8, -8], [8, 8, 8])

GREEN = (0.2, 0.8, 0.2)
MAGENTA = (0.8, 0.2, 0.8)
BLUE = (0.4, 0.7, 1.0)
CYAN = (0.2, 0.9, 0.9)
YELLOW = (1.0, 0.85, 0.2)
RED = (0.9, 0.3, 0.3)
ORANGE = (1.0, 0.5, 0.2)
PURPLE = (0.6, 0.3, 0.9)
LIGHT = (1.0, 1.0, 0.5)
GREY = (0.5, 0.5, 0.5)
WHITE = (0.8, 0.8, 0.8)

# key -> (type, default, description). Types: string int float vec3 angles bool targetname model material.
KEY_INFO = {
    "fx": ("string", "", "Stock effect name (vfx/...), from the Effects browser."),
    "delay": ("float", "", "Seconds between repeats; empty plays once (effects) or loops (sounds with loop 1)."),
    "soundalias": ("string", "", "Stock sound alias, from the Sounds browser."),
    "loop": ("bool", "0", "1 = a looping sound on a script_origin; 0 = a one-shot repeated every delay seconds."),
    # common
    "origin": ("vec3", "0 0 0", "Position x y z in map units (inferred from stock values)."),
    "angles": ("angles", "0 0 0", "Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values)."),
    "_color": ("vec3", "", "Editor draw colour r g b, 0 to 1 (inferred from stock values)."),
    "model": ("model", "", "xmodel name for point models; '*N' selects brush model N and is written by the compiler (inferred from stock values)."),
    "footprint_mask_group": ("string", "default", "Footprint mask group name (inferred from stock values; stock value 'default')."),
    "spawnflags": ("int", "0", "Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced."),
    "targetname": ("targetname", "", "Name that other entities use to find this one (gsc ammorestock.gsc:8)."),
    "target": ("targetname", "", "targetname of the entity or struct this one points to (gsc ammorestock.gsc:12)."),
    "physasset": ("string", "", "Physics asset name (inferred from stock values)."),
    "nophysics": ("bool", "", "Disables physics on the entity (inferred from stock values)."),
    "radius": ("float", "", "Radius in map units. gsc gametypes/ball.gsc:163 passes it to spawn('trigger_radius'); inferred for other classes."),
    "height": ("float", "", "Height in map units. gsc gametypes/ball.gsc:163 passes it as the height argument of spawn('trigger_radius')."),
    "dmg": ("int", "", "Damage applied to what is inside the trigger (inferred from stock values)."),
    "index": ("int", "", "Index value (inferred from stock values)."),
    "name": ("string", "", "Name key on infil structs (inferred from stock values)."),
    "animscript": ("string", "", "Animation name for a negotiation node (inferred from stock values)."),
    "lgvmerge": ("string", "", "LGV merge group (inferred from stock values)."),
    "lgvthinstable": ("bool", "", "LGV thin-stable flag (inferred from stock values)."),
    "modelscale": ("float", "", "Model scale factor (inferred from stock values)."),
    "receivevolumedecals": ("bool", "", "Receives volume decals (inferred from stock values)."),
    "sound_csv_include": ("string", "", "Sound CSV include path (inferred from one stock value)."),
    "filterpreset": ("string", "", "Filter preset name (inferred from one stock value)."),
    "volume_intersection_test": ("string", "", "Volume intersection test mode (inferred from one stock value, BOUNDING_BOX)."),
    "all_modes": ("bool", "", "Applies in all game modes (inferred from one stock value)."),
    "dom": ("int", "", "Flag value (inferred from one stock value)."),
    "__smuid": ("string", "", "Stable entity id written by the tooling (inferred from stock values)."),
    # script_* keys
    "script_linkto": ("string", "", "Name this entity links to: getent() for a trigger link target (gsc-raw door.gsc:283-285); a list of node names for node links (gsc-raw utility/entity.gsc:192-194)."),
    "script_linkname": ("string", "", "Link name read by gsc-raw animation_suite.gsc:124; role inferred from stock values."),
    "script_noteworthy": ("string", "", "Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25)."),
    "script_gameobjectname": ("string", "", "Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values."),
    "script_label": ("string", "", "Label for a gameobject trigger (gsc gameobjects.gsc:4432-4435; gametypes/arena.gsc:2165-2177 compares '1', '2', '3')."),
    "script_team": ("string", "", "Team side, axis or allies (gsc gametypes/arm.gsc:2292, 2300)."),
    "script_exploder": ("string", "", "Exploder id, stored as exploderindex (gsc gametypes/btm.gsc:1830-1832)."),
    "script_accel": ("float", "", "Acceleration value kept by the IW3 converter (contract 15 s3.2); meaning inferred."),
    "script_airspeed": ("float", "", "Air speed value kept by the IW3 converter (contract 15 s3.2); meaning inferred."),
    "script_delay": ("float", "", "Delay value (inferred from stock values; no use found in the decompiled 1.20.4 scripts)."),
    "script_bombmode_original": ("bool", "", "Bomb-mode flag kept by the IW3 converter (contract 15 s3.2); meaning inferred."),
    "script_goalyaw": ("bool", "", "Helicopter path goal-yaw flag (inferred from stock values)."),
    "script_stopnode": ("bool", "", "Helicopter path stop-node flag (inferred from stock values)."),
    "script_targetoffset_z": ("float", "", "Vertical target offset (inferred from stock values)."),
    # light and sun settings on worldspawn
    "ambient": ("float", "", "Ambient light level (inferred from stock values; kept by the IW3 converter, contract 15 s3.2)."),
    "audio_compile_umbra": ("bool", "", "Audio umbra compile flag (inferred from stock values)."),
    "csm_enable": ("bool", "", "Cascaded shadow maps enable (inferred from stock values)."),
    "csm_max_resolution": ("int", "", "Cascaded shadow map max resolution (inferred from stock values)."),
    "csm_sample_size": ("int", "", "Cascaded shadow map sample size (inferred from stock values)."),
    "_csm_bound_center": ("vec3", "", "Cascaded shadow map bounds centre (inferred from stock values)."),
    "_csm_bound_halfsize": ("vec3", "", "Cascaded shadow map bounds half size (inferred from stock values)."),
    "_primarylightcount": ("string", "", "Primary light counts, four integers (inferred from stock values)."),
    "dustmaterial": ("material", "", "Dust material (inferred from stock values)."),
    "forwardplus": ("bool", "", "Forward+ lighting enable (inferred from stock values)."),
    "generatenavmesh": ("bool", "", "Generate a navmesh at compile (inferred from stock values)."),
    "highresgpulightgrid": ("bool", "", "High-resolution GPU light grid (inferred from stock values)."),
    "lightmapcount": ("int", "", "Lightmap count (inferred from stock values)."),
    "lmapblurpass": ("int", "", "Lightmap blur passes (inferred from stock values)."),
    "northyaw": ("float", "", "North yaw. The IW3 converter drops it and always writes 90 (contract 15 s3.2, s3.4)."),
    "objectgroupcost": ("int", "", "Object group cost (inferred from stock values)."),
    "outdoor_only_maxs": ("vec3", "", "Outdoor-only bounds maximum (inferred from stock values)."),
    "outdoor_only_mins": ("vec3", "", "Outdoor-only bounds minimum (inferred from stock values)."),
    "precomputed_sky_illumination_bounce_count": ("int", "", "Precomputed sky illumination bounce count (inferred from stock values)."),
    "precomputed_sky_illumination_diffuse_reflectance": ("float", "", "Precomputed sky illumination diffuse reflectance (inferred from stock values)."),
    "rayguidemode": ("int", "", "Ray guide mode (inferred from stock values)."),
    "skycolor": ("vec3", "", "Sky colour r g b (kept by the IW3 converter, contract 15 s3.2; inferred from stock values)."),
    "skydomelighting": ("bool", "", "Sky dome lighting enable (inferred from stock values)."),
    "skylight": ("bool", "", "Sky light enable (inferred from stock values)."),
    "skyrotation": ("angles", "", "Sky rotation (inferred from stock values)."),
    "smallesthole": ("float", "", "Smallest hole size (inferred from stock values)."),
    "smallestoccluder": ("float", "", "Smallest occluder size (inferred from stock values)."),
    "sunlight": ("float", "", "Sun intensity (kept by the IW3 converter, contract 15 s3.2; inferred from stock values)."),
    "sundirection": ("angles", "", "Sun direction (kept by the IW3 converter, contract 15 s3.2; inferred from stock values)."),
    "suncolor": ("vec3", "", "Sun colour r g b (kept by the IW3 converter, contract 15 s3.2; inferred from stock values)."),
    "sunisprimarylight": ("bool", "", "Sun is the primary light (inferred from stock values)."),
    "tu0bakechanges": ("bool", "", "Stock flag, meaning unknown (inferred from one stock value)."),
    "uselmapcompression": ("bool", "", "Lightmap compression enable (inferred from stock values)."),
    "usemightconnectflag": ("bool", "", "Might-connect flag (inferred from stock values)."),
}

# Stock keys that the build never reads back, or that are classname-only, are not listed here.
SKIP_KEYS = {"classname"}

# Classes that are not in the stock dump but are needed by the rest of the table.
# name -> keys beyond those in the stock dump. Spawn and bcs classes are handled by rules below.
EXTRA_KEYS = {
    "trigger_damage": ["dmg", "footprint_mask_group", "model", "physasset", "script_exploder", "targetname", "target"],
    "triggermodeldummydefault": ["origin"],
    "misc_prefab": ["origin", "angles", "model"],
    "misc_model": ["origin", "angles", "model", "modelscale"],
    "fx_origin": ["origin", "angles", "fx", "delay"],
    "sound_emitter": ["origin", "soundalias", "loop", "delay"],
    "light": ["origin", "_color", "radius"],
    "light_spot": ["origin", "angles", "_color", "radius"],
}

# Keys that must be present on a point entity, or on a trigger for the build to keep it.
REQUIRED = {
    "info_player_start": ["origin"],
    "mp_global_intermission": ["origin"],
    "script_model": ["origin", "model"],
    "script_origin": ["origin"],
    "script_struct": ["origin"],
    "script_struct_heli": ["origin"],
    "script_struct_infil_lbravo_alpha": ["origin"],
    "script_struct_infil_van_hackney_alpha": ["origin"],
    "script_struct_mp_rugby_endzone": ["origin"],
    "script_struct_mp_rugby_jugg_crate": ["origin"],
    "node_pathnode": ["origin"],
    "node_3d_pathnode": ["origin"],
    "node_negotiation_begin": ["origin"],
    "node_negotiation_end": ["origin"],
    "reflection_probe": ["origin"],
    "trigger_radius": ["origin", "radius"],
    "light": ["origin"],
    "light_spot": ["origin"],
    "misc_prefab": ["origin"],
    "misc_model": ["origin", "model"],
    "triggermodeldummydefault": ["origin"],
    # contract 15 s3.3: the converter projects trigger_damage away without script_exploder
    "trigger_damage": ["script_exploder"],
}

# Descriptions and colours. Stock classes listed here; trigger_multiple_bcs_* and spawns are derived.
CLASS_INFO = {
    "worldspawn": ("World geometry and global map settings. One per map (contract 15 s3.1).", WHITE),
    "func_group": ("Editor-only grouping of brushes. Not a game entity.", GREY),
    "misc_prefab": ("Editor-only prefab placement (inferred from the class name). Not built.", (0.5, 0.4, 0.9)),
    "misc_model": ("Static model placement: built as a world static model (custom or exported stock model) with convex hull collision.", (0.6, 0.6, 0.9)),
    "fx_origin": ("Stock effect placement: the map script plays the 'fx' (loadfx + playfx / triggerfx), repeating every 'delay' seconds.", ORANGE),
    "sound_emitter": ("Stock sound placement: the map script plays the 'soundalias' as a loop or a repeating one-shot ('delay').", CYAN),
    "light": ("Omni point light. Editor only; not built yet.", LIGHT),
    "light_spot": ("Spot light. Editor only; not built yet. Aim comes from angles (inferred).", LIGHT),
    "collmap_scriptable": ("Collision-map brush for scriptable objects (inferred from stock values). Not accepted by the converter.", (0.7, 0.7, 0.7)),
    "export_volume": ("Animation export volume (inferred from stock values). Not accepted by the converter.", (0.3, 0.9, 0.5)),
    "info_player_start": ("Player start point (inferred from stock values). Accepted by the converter; not required (contract 15 s3.4).", GREEN),
    "mp_global_intermission": ("Intermission camera point (inferred from stock values).", MAGENTA),
    "node_3d_pathnode": ("3D pathnode (inferred from stock values). Not accepted by the converter.", GREY),
    "node_negotiation_begin": ("Start of a traversal; target matches a node_negotiation_end targetname; animscript picks the move (stock: jump_*, ladder_up). Used by the generated bot navigation.", GREY),
    "node_negotiation_end": ("End of a traversal; targetname matches a node_negotiation_begin target. Used by the generated bot navigation.", GREY),
    "node_pathnode": ("AI pathnode: the converter places it in the generated bot navigation (research 45).", GREY),
    "reflection_probe": ("Reflection probe: built into world.json with its optional mins / maxs volume, priority and feather; captured in the editor (Compile > Capture Reflection Probes).", PURPLE),
    "script_brushmodel": ("Brush model whose surfaces scripts use, referenced as '*N' (contract 15 s3.5).", BLUE),
    "script_model": ("Point model placed by script. The model must exist as xmodel/<name>.json (contract 15 s3.6).", BLUE),
    "script_origin": ("Point used by scripts for positions and effects (inferred from stock values). Spawned in script at gsc gametypes/cyber.gsc:598.", CYAN),
    "script_struct": ("Point struct found by name with getstruct(name, \"targetname\") (gsc gametypes/ball.gsc:376).", YELLOW),
    "script_struct_heli": ("Helicopter path point (inferred from stock values: script_linkto, script_goalyaw).", YELLOW),
    "script_struct_infil_lbravo_alpha": ("Infil aircraft start struct (inferred from stock values).", YELLOW),
    "script_struct_infil_van_hackney_alpha": ("Infil van start struct (inferred from stock values).", YELLOW),
    "script_struct_mp_rugby_endzone": ("Rugby endzone struct (inferred from stock values). Not accepted by the converter.", YELLOW),
    "script_struct_mp_rugby_jugg_crate": ("Rugby juggernaut crate struct (inferred from stock values). Not accepted by the converter.", YELLOW),
    "trigger_damage": ("Brush trigger with damage. The converter drops it without script_exploder (contract 15 s3.3).", RED),
    "trigger_hurt": ("Brush trigger that damages what is inside it (inferred from stock values).", RED),
    "trigger_multiple": ("Brush trigger volume. Scripts check its classname (gsc bots/bots_gametype_common.gsc:148).", RED),
    "trigger_multiple_mp_rugby_endzone": ("Brush trigger for a rugby endzone (inferred from the class name). Not accepted by the converter.", RED),
    "trigger_radius": ("Radius trigger point. Spawned in script with radius and height (gsc gametypes/ball.gsc:163).", ORANGE),
    "trigger_use": ("Brush trigger for player use (inferred from stock values).", RED),
    "trigger_use_touch": ("Brush trigger for use on touch. Its classname is removed by gsc gametypes/arm.gsc:312.", RED),
    "triggermodeldummydefault": ("Listed by the converter as an accepted class (compile_iw8.cpp). In stock data it appears only as a physasset name (inferred). Unclear mapper use.", WHITE),
}

BRUSH_CLASSES = {"worldspawn", "func_group", "script_brushmodel", "collmap_scriptable", "export_volume"}


def is_spawn(name):
    return name == "info_player_start" or (name.startswith("mp_") and "_spawn" in name)


def spawn_info(name):
    """Spawn description, derived from the class name."""
    if name == "mp_tdm_spawn":
        return "Team deathmatch spawn point. The build needs two or more (contract 15 s3.4)."
    if name == "mp_tdm_spawn_allies_start":
        return "Team deathmatch allies start spawn. The build requires it with the axis start (contract 15 s3.4)."
    if name == "mp_tdm_spawn_axis_start":
        return "Team deathmatch axis start spawn. The build requires it with the allies start (contract 15 s3.4)."
    if name == "info_player_start":
        return CLASS_INFO[name][0]
    parts = name.split("_")
    mode = parts[1].upper() if len(parts) > 1 else "?"
    role = " ".join(parts[3:]) if len(parts) > 3 else "default"
    return f"Spawn point for {mode} ({role}). Description inferred from the class name."


BCS_PREFIX = "trigger_multiple_bcs_"


def describe(name):
    """(description, colour) for a class, or None when the table has no text for it."""
    if name in CLASS_INFO:
        return CLASS_INFO[name]
    if is_spawn(name):
        return spawn_info(name), GREEN
    if name.startswith(BCS_PREFIX):
        area = name[len(BCS_PREFIX):]
        return f"Brush trigger for the '{area}' area (inferred from the class name). Accepted by prefix in the converter.", RED
    return None


def kind_of(name):
    if name == "trigger_radius":
        return "point"
    if name.startswith("trigger_") or name in BRUSH_CLASSES:
        return "brush"
    return "point"


def accepted_rules(compile_text):
    """Parses acceptedPointClass() in compile_iw8.cpp: the explicit set and the startsWith prefixes."""
    m = re.search(r"bool acceptedPointClass\(.*?\n\}", compile_text, re.S)
    if not m:
        raise SystemExit("acceptedPointClass() not found in " + str(COMPILE_PATH))
    body = m.group(0)
    block = re.search(r"known\s*=\s*\{(.*?)\};", body, re.S)
    if not block:
        raise SystemExit("acceptedPointClass() known-set not found")
    known = set(re.findall(r'"([^"]+)"', block.group(1)))
    prefixes = set(re.findall(r'startsWith\(\s*lcls\s*,\s*"([^"]+)"\s*\)', body))
    return known, prefixes


def load_key_names(path):
    names = set()
    if not path.exists():
        return names
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            names.add(parts[1])
    return names


def main():
    stock = json.loads(STOCK_PATH.read_text(encoding="utf-8"))
    stock_classes = stock["classes"]
    known, prefixes = accepted_rules(COMPILE_PATH.read_text(encoding="utf-8"))

    # Classes the compiler builds by its own route rather than as converter entities (world static models, probes, map
    # script placements, primary lights, collision-json dynamic models, world volumetrics).
    built_by_editor = {"misc_model", "reflection_probe", "fx_origin", "sound_emitter", "light", "light_spot", "light_omni",
                       "dynamic_prop", "iw8r_volume"}

    def accepted(name):
        return name in built_by_editor or name in known or any(name.startswith(p) for p in prefixes)

    # Coverage: every stock class, every explicitly accepted class, and the editor classes.
    names = set(stock_classes) | set(known) | set(CLASS_INFO) | set(EXTRA_KEYS)
    errors = []
    for n in sorted(names):
        if describe(n) is None:
            errors.append("no description for class " + n)
    for n in sorted(CLASS_INFO):
        if n not in names:
            errors.append("CLASS_INFO entry " + n + " is not a stock, accepted or editor class")

    # Every stock key needs a KEY_INFO row (type, default, description).
    stock_key_names = set()
    for c in stock_classes.values():
        stock_key_names.update(c["keys"])
    for k in sorted(stock_key_names - SKIP_KEYS):
        if k not in KEY_INFO:
            errors.append("no KEY_INFO entry for stock key " + k)
    for n, ks in EXTRA_KEYS.items():
        for k in ks:
            if k not in KEY_INFO:
                errors.append("no KEY_INFO entry for extra key " + k + " on " + n)
    for n, ks in REQUIRED.items():
        for k in ks:
            if k not in KEY_INFO:
                errors.append("no KEY_INFO entry for required key " + k + " on " + n)

    if errors:
        for e in errors:
            print("error: " + e, file=sys.stderr)
        return 1

    classes = {}
    for name in sorted(names):
        in_stock = name in stock_classes
        spawn = is_spawn(name)
        description, color = describe(name)
        kind = kind_of(name)

        key_names = set()
        examples = {}
        if in_stock:
            for k, v in stock_classes[name]["keys"].items():
                if k in SKIP_KEYS:
                    continue
                key_names.add(k)
                seen = []
                for ex in v.get("examples", []):
                    if ex not in seen:
                        seen.append(ex)
                examples[k] = seen[:4]
        key_names.update(EXTRA_KEYS.get(name, []))
        if spawn:
            key_names.update(["origin", "angles"])
        required = set(REQUIRED.get(name, []))
        if spawn:
            required.add("origin")

        entry = {"description": description, "kind": kind}
        if kind == "point":
            box = PLAYER_BOX if spawn else SMALL_BOX
            entry["size"] = [list(box[0]), list(box[1])]
        entry["color"] = list(color)
        entry["buildAccepted"] = accepted(name)
        keys = {}
        for k in sorted(key_names):
            typ, default, desc = KEY_INFO[k]
            keys[k] = {
                "type": typ,
                "default": default,
                "description": desc,
                "required": k in required,
                "examples": examples.get(k, []),
            }
        entry["keys"] = keys
        classes[name] = entry

    tsv_names = load_key_names(KEYS_TSV_PATH)
    unknown = set()
    for c in classes.values():
        for k in c["keys"]:
            if tsv_names and k not in tsv_names:
                unknown.add(k)
    if unknown:
        print("note: keys not in iw8_canonical_keys.tsv (quoted spelling still valid): " + ", ".join(sorted(unknown)),
              file=sys.stderr)

    # Editor-authored classes (data/iw8_entity_defs_overrides.json): lights as the build handles them, editor-only
    # classes such as dynamic_prop and iw8r_volume. Each replaces the generated class whole.
    overrides_path = OUT_PATH.parent / "iw8_entity_defs_overrides.json"
    if overrides_path.exists():
        for name, entry in json.loads(overrides_path.read_text(encoding="utf-8")).get("classes", {}).items():
            classes[name] = entry
        classes = dict(sorted(classes.items()))

    doc = {"schema": 1, "classes": classes}
    OUT_PATH.write_text(json.dumps(doc, indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")

    accepted_count = sum(1 for c in classes.values() if c["buildAccepted"])
    key_count = sum(len(c["keys"]) for c in classes.values())
    print(f"wrote {OUT_PATH}: {len(classes)} classes, {accepted_count} buildAccepted, {key_count} key rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
