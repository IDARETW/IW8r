#!/usr/bin/env python3
"""Catalogue of stock IW8 MP scripted objects (roadmap 6.13).

Reads the stock entity strings in data/stock_dumps/<map>/.../map_ents/.../*.asset.json (hex
`entityString.bytes`), decodes the `{ <key id> "<value>" }` blocks with data/generated/iw8_canonical_keys.tsv,
and counts how many stock entities each kind rule matches. Writes data/asset_catalog/iw8_scripted_objects.tsv.

Each entity is counted under the FIRST rule (in RULES order) that matches it, so the counts add up.
The TSV keeps every kind row that has count > 0. Evidence and the handler notes are in
docs/research/53_stock_scripted_objects.md. 'works_in_custom_map' is a reading of the stock scripts, not a game test.

Usage: py -3 -I tools/build_scripted_objects.py
"""
import json
import os
import re
import sys
from collections import Counter, OrderedDict

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
KEYS_TSV = os.path.join(ROOT, "data", "generated", "iw8_canonical_keys.tsv")
OUT_TSV = os.path.join(ROOT, "data", "asset_catalog", "iw8_scripted_objects.tsv")
STOCK = OrderedDict([
    ("mp_shipment", os.path.join(ROOT, "data", "stock_dumps", "mp_shipment", "mw19replay", "mp_shipment",
                                 "assets", "map_ents", "maps", "mp", "mp_shipment.d3dbsp.0.asset.json")),
    ("mp_backlot2", os.path.join(ROOT, "data", "stock_dumps", "mp_backlot2", "mw19replay", "mp_backlot2",
                                 "assets", "map_ents", "maps", "mp", "mp_backlot2.d3dbsp.0.asset.json")),
])

# Handler paths are relative to the stock GSC dump (8.19 build, see doc 53 section 0).
G = "scripts/mp/gametypes/"
GATE = "scripts/mp/gameobjects.gsc:17 gate"
UNK = "UNKNOWN"

# (kind, classname pattern, key or '-', value pattern, handled_by, works_in_custom_map)
# Patterns use '*' only. Key '-' means the classname alone decides.
RULES = [
    ("dom_capture_trigger", "trigger_radius", "script_gameobjectname", "dom onslaught",
     "scripts/mp/gametypes/dom.gsc:351,367 (targetname flag_primary); " + GATE, "unknown"),
    ("hardpoint_zone", "trigger_multiple", "script_gameobjectname", "hardpoint",
     G + "koth.gsc:805-838; " + G + "hq.gsc:848-886; " + GATE, "unknown"),
    ("hq_radio", "script_model", "targetname", "hqloc",
     G + "hq.gsc:885 (getentarray targetname hqloc)", "unknown"),
    ("hq_radio", "script_model", "script_gameobjectname", "hqloc",
     G + "hq.gsc:11-14 gate; " + GATE, "unknown"),
    ("hq_radio_brush", "script_brushmodel", "script_gameobjectname", "hqloc",
     GATE + " only (brush role UNVERIFIED)", "unknown"),
    ("sd_site", "trigger_use_touch", "targetname", "bombzone",
     G + "sd.gsc:470; " + G + "obj_bombzone.gsc:17,118,124 (reads .target chain)", "unknown"),
    ("sd_site", "trigger_use_touch", "script_gameobjectname", "bombzone",
     G + "sd.gsc:470; " + G + "obj_bombzone.gsc:17,118,124", "unknown"),
    ("sd_site", "trigger_use_touch", "targetname", "bombtrigger",
     G + "obj_bombzone.gsc (targetname encrypted; UNVERIFIED)", "unknown"),
    ("sd_site", "trigger_use_touch", "script_bombmode_original", "1",
     G + "sd.gsc:439 (mode flag meaning UNVERIFIED)", "unknown"),
    ("sd_site", "script_model", "script_gameobjectname", "bombzone",
     G + "obj_bombzone.gsc (model role UNVERIFIED)", "unknown"),
    ("sd_site", "script_brushmodel", "script_gameobjectname", "bombzone",
     G + "sd.gsc:439-456 removes a bombzone brushmodel if present (UNVERIFIED)", "unknown"),
    ("sd_bomb", "script_model", "targetname", "sd_bomb",
     G + "sd.gsc:11-14 gate; bomb lookups encrypted (UNVERIFIED)", "unknown"),
    ("sd_bomb", "script_model", "script_gameobjectname", "sd",
     G + "sd.gsc:11-14 gate", "unknown"),
    ("sd_pickup", "trigger_multiple", "targetname", "sd_bomb_pickup_trig",
     G + "sd.gsc (lookup encrypted; UNVERIFIED)", "unknown"),
    ("sd_pickup", "trigger_multiple", "script_gameobjectname", "sd",
     G + "sd.gsc:11-14 gate", "unknown"),
    ("ctf_flag", "script_model", "targetname", "ctf_flag_*",
     G + "ctf.gsc:509,662 (getent ctf_flag_ + team)", "unknown"),
    ("ctf_flag", "script_model", "script_gameobjectname", "ctf",
     G + "ctf.gsc:9-10 gate; ctf.gsc:355", "unknown"),
    ("ctf_trigger", "trigger_radius", "targetname", "ctf_trig_*",
     G + "ctf.gsc:502,524 (getent ctf_trig_ + team)", "unknown"),
    ("ctf_trigger", "trigger_radius", "script_gameobjectname", "ctf",
     G + "ctf.gsc:9-10 gate", "unknown"),
    ("ctf_zone", "trigger_radius", "targetname", "ctf_zone_*",
     G + "ctf.gsc (zone lookup encrypted; UNVERIFIED)", "unknown"),
    ("flag_goal", "trigger_radius", "targetname", "flag_goal_*",
     "scripts/mp/gametypes/arena.gsc or ball/blitz (lookup UNVERIFIED)", "unknown"),
    ("flag_goal", "trigger_radius", "targetname", "flag_arena",
     "scripts/mp/gametypes/arena.gsc or ball/blitz (lookup UNVERIFIED)", "unknown"),
    ("cyber_emp", "script_model", "targetname", "cyber_emp",
     G + "cyber.gsc:460-492 (getent of encrypted zone/defuse names); gate cyber.gsc:11-13", "unknown"),
    ("cyber_emp_pickup", "trigger_use", "targetname", "cyber_emp_pickup_trig",
     G + "cyber.gsc:345,388 (encrypted lookups)", "unknown"),
    ("cyber_zone", "trigger_use_touch", "targetname", "cyber_empzone_*",
     G + "cyber.gsc:460-492", "unknown"),
    ("cyber_defuse", "trigger_use_touch", "targetname", "cyber_emp_defuse_*",
     G + "cyber.gsc:460-492", "unknown"),
    ("cyber_gameobject", "trigger_use", "script_gameobjectname", "cyber",
     G + "cyber.gsc:11-13 gate", "unknown"),
    ("cyber_gameobject", "trigger_use_touch", "script_gameobjectname", "cyber",
     G + "cyber.gsc:11-13 gate", "unknown"),
    ("cyber_gameobject", "script_model", "script_gameobjectname", "cyber",
     G + "cyber.gsc:11-13 gate", "unknown"),
    ("cyber_gameobject", "script_brushmodel", "script_gameobjectname", "cyber",
     G + "cyber.gsc:11-13 gate (brush role UNVERIFIED)", "unknown"),
    ("dd_bombzone", "trigger_use_touch", "targetname", "dd_bombzone",
     G + "dd.gsc:527; gate dd.gsc:11-14", "unknown"),
    ("dd_bombzone", "trigger_use_touch", "script_gameobjectname", "dd_bombzone",
     G + "dd.gsc:527; gate dd.gsc:11-14", "unknown"),
    ("dd_bombzone", "script_model", "script_gameobjectname", "dd_bombzone",
     G + "dd.gsc:11-14 gate (model role UNVERIFIED)", "unknown"),
    ("dd_bombzone_clip", "script_brushmodel", "targetname", "dd_bombzone_clip_*",
     G + "dd.gsc (UNVERIFIED)", "unknown"),
    ("grind_location", "trigger_radius", "targetname", "grind_location",
     G + "grind.gsc:375,712", "unknown"),
    ("grind", "trigger_radius", "script_gameobjectname", "grind",
     G + "grind.gsc:8-10 gate", "unknown"),
    ("grnd_zone", "trigger_multiple", "script_gameobjectname", "grnd",
     G + "grnd.gsc:11-13 gate; grnd.gsc:322", "unknown"),
    ("grnd_zone", "trigger_multiple", "targetname", "grnd",
     G + "grnd.gsc:322 (getentarray)", "unknown"),
    ("callout_area", "trigger_multiple", "targetname", "callout_area",
     "scripts/mp/callouts.gsc:13", "unknown"),
    ("callout_area", "trigger_multiple", "script_noteworthy", "*",
     "scripts/mp/callouts.gsc:79,97 (area name in script_noteworthy)", "unknown"),
    ("heli_route", "script_struct_heli", "targetname", "ks_heli_*",
     "scripts/mp/killstreaks/helicopter.gsc:207,221,1182,1239 (target chain)", "unknown"),
    ("heli_mesh", "script_brushmodel", "script_noteworthy", "heli_height_mesh",
     "scripts/mp/killstreaks/helicopter.gsc (name lookup encrypted; UNVERIFIED)", "unknown"),
    ("heli_mesh", "script_brushmodel", "targetname", "heli_pilot_mesh",
     "scripts/mp/killstreaks/helicopter.gsc (name lookup encrypted; UNVERIFIED)", "unknown"),
    ("killstreak_marker", "script_origin", "targetname", "airstrikeheight",
     "scripts/mp/events.gsc:2828 getkillstreakairstrikeheightent", "unknown"),
    ("killstreak_marker", "script_origin", "targetname", "heli_leave", UNK, "unknown"),
    ("killstreak_marker", "script_struct", "targetname", "nuke_expl_pos",
     UNK + " (encrypted name; cp/killstreaks/nuke_cp.gsc may read it)", "unknown"),
    ("killstreak_marker", "script_brushmodel", "targetname", "care_package_col", UNK, "unknown"),
    ("airdrop", "script_model", "targetname", "airdrop_crate", UNK, "unknown"),
    ("airdrop", "script_brushmodel", "script_gameobjectname", "airdrop_pallet",
     UNK + " (in none of the gate lists read, doc 40)", "unknown"),
    ("minimap_corner", "script_origin", "targetname", "minimap_corner",
     "scripts/mp/compass.gsc:12 (generic MP script, not map-specific)", "yes"),
    ("camera_intro", "script_struct", "targetname", "camera_intro",
     "scripts/mp/spawncamera.gsc:32 (fallback cameras if none)", "unknown"),
    ("dz_flare", "script_struct", "targetname", "dz_flare", UNK, "unknown"),
    ("weapon_pickup_floor", "script_struct", "script_noteworthy", "floor", UNK, "unknown"),
    ("exploder", "script_model", "script_exploder", "*",
     "scripts/common/exploder.gsc:52-101 (script_exploder index; generic common script)", "unknown"),
    ("exploder", "script_model", "targetname", "exploder",
     "scripts/common/exploder.gsc:82-83 (encrypted key; UNVERIFIED)", "unknown"),
    ("pf_chain_node", "script_struct", "targetname", "pf*_auto*",
     "target chain read by obj_bombzone.gsc:118,124 and dd (UNVERIFIED)", "unknown"),
    ("pf_chain_node", "script_brushmodel", "targetname", "pf*_auto*",
     "target chain (UNVERIFIED)", "unknown"),
    ("pf_chain_node", "script_model", "targetname", "pf*_auto*",
     "target chain (UNVERIFIED)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "clip*", UNK + " (stock clip volume)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "player*", UNK + " (stock clip volume)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "mantle*", UNK + " (stock climb volume)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "mount*", UNK + " (stock climb volume)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "ladder*", UNK + " (stock ladder volume)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "nosight*", UNK + " (stock sight clip)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "stairs*", UNK + " (stock stairs volume)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "tactical_*_col",
     UNK + " (tactical equipment clip)", "unknown"),
    ("nav_clip_brush", "script_brushmodel", "targetname", "smoke_grenade_sight_clip_*",
     UNK + " (smoke sight clip)", "unknown"),
    ("out_of_bounds", "trigger_multiple", "targetname", "OutOfBounds",
     "scripts/mp/outofbounds.gsc (lookup encrypted); X64 iw3_compat.gsc:53-78 supplies its own volume", "unknown"),
    ("out_of_bounds", "trigger_multiple", "targetname", "kill_border_trigger",
     "scripts/mp/outofbounds.gsc (lookup encrypted); X64 iw3_compat.gsc:53-78", "unknown"),
    ("dcover_blocker", "trigger_multiple", "targetname", "dcover_blocker", UNK, "unknown"),
    ("white_phosphorus_damage", "trigger_multiple", "targetname", "white_phosphorus_damage", UNK, "unknown"),
    ("hurt_trigger", "trigger_hurt", "-", "*",
     "scripts/mp/gameobjects.gsc:4458 (level.hurttriggers by classname; UNVERIFIED call site)", "unknown"),
    ("battle_chatter_location", "trigger_multiple_bcs_*", "-", "*",
     "scripts/mp/battlechatter_trigs.gsc (location names encrypted)", "unknown"),
    ("rugby_endzone", "script_struct_mp_rugby_endzone", "targetname", "rugby_endzone",
     "scripts/mp/gametypes/rugby.gsc:174,185,240", "no"),
    ("rugby_endzone", "trigger_multiple_mp_rugby_endzone", "targetname", "*_endzone",
     "scripts/mp/gametypes/rugby.gsc:174,185,240", "no"),
    ("rugby_endzone", "trigger_multiple_mp_rugby_endzone", "targetname", "pf*_auto*",
     "pf chain on the endzone trigger (UNVERIFIED use); rugby.gsc:174,185,240", "no"),
    ("tactical_ladder_visual", "script_struct", "model", "misc_vm_tactical_ladder",
     UNK + " (visual marker for tactical ladder clip)", "unknown"),
    ("rugby_jugg_crate", "script_struct_mp_rugby_jugg_crate", "targetname", "rugby_jugg_crate",
     "scripts/mp/gametypes/rugby.gsc (rugby only)", "no"),
    ("infil", "script_struct_infil_*", "targetname", "mp_infil",
     "scripts/mp/infilexfil/infilexfil.gsc:105,272-274,1214-1229 (map-specific infil scripts)", "no"),
    ("infil", "script_model", "targetname", "infil_door_*",
     "scripts/mp/infilexfil/infilexfil.gsc (map-specific)", "no"),
    ("export_volume", "export_volume", "-", "*",
     "not an accepted class (doc 40 section 1; X64 maps.cpp:179-216)", "no"),
    ("reflection_probe", "reflection_probe", "-", "*",
     "not an accepted class (doc 40 section 1; X64 maps.cpp:179-216)", "no"),
    ("collmap_scriptable", "collmap_scriptable", "-", "*",
     "not read by the IW8 Replay route (doc 41 section 1.4)", "no"),
    ("intermission", "mp_global_intermission", "-", "*", UNK, "unknown"),
    ("label_struct", "script_struct", "script_label", "*",
     UNK + " (labelled structs 1-8 and dz_flare; role not in doc 40)", "unknown"),
    ("stock_delete_marker", "script_struct", "targetname", "delete_on_load",
     "scripts/engine/utility.gsc:1604 (identity UNVERIFIED); do not author", "unknown"),
]

NODE_PREFIXES = ("node_",)
IGNORED_CLASSES = {"worldspawn", "info_player_start"}


def wild(pattern, text):
    """'*' wildcard match on the whole string (same rule as ScriptedObjectCatalog in C++)."""
    parts = pattern.split("*")
    if len(parts) == 1:
        return pattern == text
    if not text.startswith(parts[0]):
        return False
    pos = len(parts[0])
    end = len(text) - len(parts[-1])
    if end < pos or not text.endswith(parts[-1]):
        return False
    for mid in parts[1:-1]:
        idx = text.find(mid, pos, end)
        if idx < 0:
            return False
        pos = idx + len(mid)
    return pos <= end


def load_keys():
    keys = {}
    with open(KEYS_TSV, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#") or "\t" not in line:
                continue
            idx, name = line.rstrip("\n").split("\t", 1)
            keys[int(idx)] = name
    return keys


BLOCK = re.compile(r"\{([^{}]*)\}", re.S)
PAIR = re.compile(r'^\s*(\d+)\s+"(.*)"\s*$')


def decode_entities(path, keys):
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    raw = doc["asset"]["fields"]["entityString"]["bytes"]
    text = bytes.fromhex(raw).decode("latin-1")
    ents = []
    for block in BLOCK.finditer(text):
        ent = {}
        for line in block.group(1).splitlines():
            m = PAIR.match(line)
            if m:
                kid = int(m.group(1))
                ent.setdefault(keys.get(kid, "#%d" % kid), m.group(2))
        if ent:
            ents.append(ent)
    return ents


def rule_matches(rule, ent):
    _kind, cls_pat, key, val_pat, _h, _w = rule
    if not wild(cls_pat, ent.get("classname", "")):
        return False
    if key == "-":
        return True
    if key not in ent:
        return False
    return wild(val_pat, ent[key])


def main():
    keys = load_keys()
    counts = [0] * len(RULES)
    maps_of = [set() for _ in RULES]
    unmatched = Counter()
    total = Counter()
    for map_name, path in STOCK.items():
        if not os.path.isfile(path):
            print("missing stock entity dump: %s" % path, file=sys.stderr)
            return 1
        ents = decode_entities(path, keys)
        print("%s: %d entities" % (map_name, len(ents)))
        for ent in ents:
            cls = ent.get("classname", "?")
            total[cls] += 1
            if cls.startswith(NODE_PREFIXES) or cls in IGNORED_CLASSES:
                continue
            for i, rule in enumerate(RULES):
                if rule_matches(rule, ent):
                    counts[i] += 1
                    maps_of[i].add(map_name)
                    break
            else:
                unmatched[cls] += 1

    header = [
        "# IW8 stock scripted objects: kinds placed by stock MP maps. Generated by tools/build_scripted_objects.py; do not edit.",
        "# Source: data/stock_dumps/<map>/.../map_ents (entity strings), keys via data/generated/iw8_canonical_keys.tsv.",
        "# Handlers: stock GSC dump (8.19 build), paths relative to scripts/. works_in_custom_map: yes, no or unknown (reading only, not game-tested).",
        "# Pattern rules: classname and value support '*' wildcards; key '-' means the classname alone decides. First matching rule counts an entity.",
        "# kind<TAB>classname<TAB>key<TAB>value_pattern<TAB>handled_by<TAB>works_in_custom_map<TAB>maps<TAB>count",
    ]
    rows = []
    for i, rule in enumerate(RULES):
        if counts[i] == 0:
            continue
        kind, cls_pat, key, val_pat, handled, works = rule
        assert "\t" not in handled and "\n" not in handled
        rows.append("\t".join([kind, cls_pat, key, val_pat, handled, works,
                               ",".join(sorted(maps_of[i])), str(counts[i])]))
    os.makedirs(os.path.dirname(OUT_TSV), exist_ok=True)
    with open(OUT_TSV, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(header + rows) + "\n")

    works = Counter()
    for i, rule in enumerate(RULES):
        if counts[i]:
            works[rule[5]] += counts[i]
    print("rows written: %d -> %s" % (len(rows), OUT_TSV))
    print("entities by works_in_custom_map: %s" % dict(works))
    print("rules with zero stock matches: %d" % sum(1 for c in counts if c == 0))
    if unmatched:
        print("unmatched classnames (not in any rule):")
        for cls, n in unmatched.most_common():
            print("  %5d %s" % (n, cls))
    return 0


if __name__ == "__main__":
    sys.exit(main())
