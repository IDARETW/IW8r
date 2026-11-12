#!/usr/bin/env python3
"""Build data/stock_assets/<zone>/index.json from an ACTS fastfile export.

Reads only the ACTS output tree (mw19replay/<zone>/manifest.json, assets.jsonl,
assets/<type>/...). It never touches the game folder and never writes anything
except index.json (and an optional stdout summary).

Run with:  python -I E:\\IW8-Radiant\\tools\\acts_asset_index.py
Defaults regenerate the code_post_gfx index. Paths in index.json are relative to
the zone output root (the folder that contains mw19replay/).
"""

import argparse
import collections
import json
import os
import struct
import sys

DEFAULT_ROOT = r"E:\IW8-Radiant\data\stock_assets\code_post_gfx"
DEFAULT_ZONE = "code_post_gfx"
DEFAULT_BUILD = "1.20.4.7623265"

# DXGI_FORMAT values that appear in DDS DX10 headers (subset; others print as numeric).
DXGI_NAMES = {
    0: "UNKNOWN", 2: "R32G32B32A32_FLOAT", 10: "R16G16B16A16_FLOAT", 11: "R16G16B16A16_UNORM",
    16: "R32G32_FLOAT", 24: "R10G10B10A2_UNORM", 28: "R8G8B8A8_UNORM", 29: "R8G8B8A8_UNORM_SRGB",
    41: "R32_FLOAT", 42: "R32_UINT", 49: "R8G8_UNORM", 54: "R16_FLOAT", 56: "R16_UNORM", 61: "R8_UNORM",
    71: "BC1_UNORM", 72: "BC1_UNORM_SRGB", 74: "BC2_UNORM", 75: "BC2_UNORM_SRGB",
    77: "BC3_UNORM", 78: "BC3_UNORM_SRGB", 80: "BC4_UNORM", 81: "BC4_SNORM",
    83: "BC5_UNORM", 84: "BC5_SNORM", 87: "B8G8R8A8_UNORM", 91: "B8G8R8A8_UNORM_SRGB",
    95: "BC6H_UF16", 96: "BC6H_SF16", 98: "BC7_UNORM", 99: "BC7_UNORM_SRGB",
}
LEGACY_FOURCC = {
    b"DXT1": "BC1_UNORM", b"DXT3": "BC2_UNORM", b"DXT5": "BC3_UNORM",
    b"BC4U": "BC4_UNORM", b"BC5U": "BC5_UNORM", b"ATI1": "BC4_UNORM", b"ATI2": "BC5_UNORM",
}

# Engine placeholder images are named with a leading '$' (for example $white, $black, $default).
def is_placeholder(name):
    return not name or name.startswith("$")


def strip_hash(name):
    """Image names look like 'white&defaultspec~3185927163104134690'. Keep the text before '~'."""
    return name.split("~", 1)[0] if name else name


def read_dds(path):
    with open(path, "rb") as f:
        head = f.read(148)
    if len(head) < 128 or head[:4] != b"DDS ":
        return {"valid": False}
    height, width = struct.unpack_from("<II", head, 12)
    mips = struct.unpack_from("<I", head, 28)[0]
    fourcc = head[84:88]
    info = {"valid": True, "width": width, "height": height, "mips": mips or 1,
            "cube": False, "array_size": 1}
    if fourcc == b"DX10" and len(head) >= 148:
        dxgi, dim, misc, arr, _misc2 = struct.unpack_from("<IIIII", head, 128)
        info["dxgi"] = dxgi
        info["format"] = DXGI_NAMES.get(dxgi, "DXGI_%d" % dxgi)
        info["cube"] = bool(misc & 0x4)
        info["array_size"] = arr if arr else 1
        if info["cube"]:
            info["array_size"] = max(1, arr // 6)
    else:
        info["dxgi"] = None
        info["format"] = LEGACY_FOURCC.get(fourcc, "legacy_" + fourcc.decode("latin1", "replace"))
    return info


def load_jsonl(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def named(value):
    """Return the .name/.string of a loader field, or the raw value."""
    if isinstance(value, dict):
        if "string" in value:
            return value["string"]
        if "name" in value:
            return value["name"]
    return value


def material_record(root, row):
    doc = json.load(open(os.path.join(root, row["file"]), encoding="utf-8"))
    fields = doc["asset"]["fields"]
    tex_table = fields.get("textureTable") or {}
    textures = []
    for v in tex_table.get("values") or []:
        img = v.get("image") or {}
        textures.append({"slot": v.get("index"), "image": img.get("name")})
    draw = fields.get("drawSurf") or {}
    surf_type = None
    if isinstance(draw, dict):
        dfields = draw.get("fields") or {}
        surf_type = dfields.get("surfType")
    return {
        "name": named(fields.get("name")) or row["name"],
        "journal_name": row["name"],
        "file": row["file"].replace("\\", "/"),
        "techset": named(fields.get("techniqueSet")),
        "material_type": named(fields.get("materialType")),
        "surface_type": surf_type,
        "textures": textures,
    }


def pick_albedo(textures, image_files):
    """Albedo rule (see docs/research/16_acts_asset_exports.md).

    The material JSON stores each texture as {index, image}. In this zone the
    index is a small slot code. Slot 0 holds colour data (for example
    default_camo_color, white, shadow), and slot 9 holds normal maps
    (default_camo_normal, identitynormalmap). Slots 18, 27, 45, 59, 68 and 158
    hold effect, UI or placeholder images.

    Rule: choose the texture in slot 0 whose image is a real exported image,
    not an engine placeholder beginning with '$'. Materials with no such texture
    get null. No other slot is guessed.
    """
    for t in textures:
        if t["slot"] == 0 and not is_placeholder(t["image"]):
            img = t["image"]
            if img in image_files:
                return img
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=DEFAULT_ROOT, help="zone output root containing mw19replay/")
    ap.add_argument("--zone", default=DEFAULT_ZONE)
    ap.add_argument("--build", default=DEFAULT_BUILD)
    ap.add_argument("--out", default=None, help="default: <root>/index.json")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    zone_dir = os.path.join(root, "mw19replay", args.zone)
    out_path = args.out or os.path.join(root, "index.json")
    manifest = json.load(open(os.path.join(zone_dir, "manifest.json"), encoding="utf-8"))
    if not manifest.get("complete") or manifest.get("failed") or manifest.get("unavailable"):
        print("warning: manifest is not clean: complete=%s failed=%s unavailable=%s" % (
            manifest.get("complete"), manifest.get("failed"), manifest.get("unavailable")), file=sys.stderr)
    rows = load_jsonl(os.path.join(zone_dir, "assets.jsonl"))

    def rel(p):
        return os.path.relpath(os.path.join(zone_dir, p), root).replace("\\", "/") if p else None

    # Images
    images = []
    image_files = {}
    dxgi_counts = collections.Counter()
    image_failures = []
    for r in rows:
        if r["type"] != "image":
            continue
        if r.get("status") != "ok" or not r.get("file"):
            image_failures.append({"name": r["name"], "status": r.get("status"), "reason": r.get("reason")})
            continue
        path = os.path.join(zone_dir, r["file"])
        info = read_dds(path)
        if not info.get("valid"):
            image_failures.append({"name": r["name"], "status": "invalid_dds", "reason": r["file"]})
            continue
        dxgi_counts[info["format"]] += 1
        entry = {
            "name": r["name"],
            "file": rel(r["file"]),
            "format": info["format"],
            "width": info["width"],
            "height": info["height"],
            "mips": info["mips"],
            "bytes": r.get("payload_bytes"),
        }
        if info["cube"]:
            entry["cube"] = True
        if info["array_size"] != 1:
            entry["array_size"] = info["array_size"]
        images.append(entry)
        image_files[r["name"]] = entry["file"]

    # Materials
    materials = []
    missing_image_refs = collections.Counter()
    albedo_count = 0
    albedo_placeholder_only = 0
    materials_with_textures = 0
    for r in rows:
        if r["type"] != "material" or r.get("status") != "ok" or not r.get("file"):
            continue
        rec = material_record(zone_dir, r)
        if rec["textures"]:
            materials_with_textures += 1
        for t in rec["textures"]:
            if t["image"] and not is_placeholder(t["image"]) and t["image"] not in image_files:
                missing_image_refs[t["image"]] += 1
        albedo = pick_albedo(rec["textures"], image_files)
        if albedo is not None:
            albedo_count += 1
        elif any(t["slot"] == 0 for t in rec["textures"]):
            albedo_placeholder_only += 1
        albedo_file = image_files.get(albedo) if albedo else None
        images_map = {}
        for t in rec["textures"]:
            key = "slot_%s" % t["slot"]
            # Several textures can share a slot number. Keep them all in order.
            while key in images_map:
                key += "_"
            images_map[key] = t["image"]
        materials.append({
            "name": rec["name"],
            "techset": rec["techset"],
            "material_type": rec["material_type"],
            "surface_type": rec["surface_type"],
            "images": images_map,
            "color_image_file": albedo_file,
            "asset_file": rel(rec["file"]),
        })

    # Models (xmodel) with geometry from xmodelsurfs.
    surf_geom = {}
    for r in rows:
        if r["type"] == "xmodelsurfs" and r.get("geometry_file"):
            surf_geom[r["name"]] = rel(r["geometry_file"])
    models = []
    xmodel_rows = [r for r in rows if r["type"] == "xmodel" and r.get("status") == "ok"]
    missing_material_refs = collections.Counter()
    material_names = {m["name"] for m in materials}
    for r in xmodel_rows:
        doc = json.load(open(os.path.join(zone_dir, r["file"]), encoding="utf-8"))
        f = doc["asset"]["fields"]
        lods = []
        glb = None
        for lod in f.get("lodInfo") or []:
            staging = lod.get("modelSurfsStaging")
            sname = staging.get("name") if isinstance(staging, dict) else None
            entry = {"dist": lod.get("dist"), "surfs": sname,
                     "surface_count": lod.get("numsurfs"),
                     "glb": surf_geom.get(sname) if sname else None}
            if sname:
                lods.append(entry)
                if glb is None and entry["glb"]:
                    glb = entry["glb"]
        mats = []
        for h in (f.get("materialHandles") or {}).get("values") or []:
            if isinstance(h, dict) and h.get("name"):
                mats.append(h["name"])
                if h["name"] not in material_names:
                    missing_material_refs[h["name"]] += 1
        bounds = None
        b = f.get("bounds") or {}
        mid = (b.get("midPoint") or {}).get("v")
        half = (b.get("halfSize") or {}).get("v")
        if mid and half:
            bounds = [[mid[i] - half[i] for i in range(3)], [mid[i] + half[i] for i in range(3)]]
        models.append({
            "name": named(f.get("name")) or r["name"],
            "glb": glb,
            "lods": lods,
            "materials": mats,
            "bounds": bounds,
            "radius": f.get("radius"),
            "asset_file": rel(r["file"]),
        })

    index = {
        "zone": args.zone,
        "build": args.build,
        "source": "ACTS fastfile -r mw19replay (see docs/research/16_acts_asset_exports.md)",
        "bounds_units": "source units (inches, right-handed Z-up); GLB nodes apply a 0.0254 scale",
        "albedo_rule": ("texture slot 0 whose image is a non-placeholder exported image "
                        "(names starting with '$' are placeholders); otherwise null"),
        "counts": {
            "materials": len(materials),
            "materials_with_textures": materials_with_textures,
            "materials_with_albedo": albedo_count,
            "materials_slot0_placeholder_only": albedo_placeholder_only,
            "images": len(images),
            "images_unavailable": len(image_failures),
            "image_refs_missing_from_export": len(missing_image_refs),
            "models": len(models),
            "models_with_glb": sum(1 for m in models if m["glb"]),
            "material_refs_missing_from_export": len(missing_material_refs),
            "dxgi_format_counts": dict(sorted(dxgi_counts.items())),
        },
        "materials": materials,
        "images": images,
        "models": models,
    }
    if image_failures:
        index["counts"]["image_failures"] = image_failures
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, indent=1, ensure_ascii=False)
        f.write("\n")

    print("wrote", out_path)
    print(json.dumps(index["counts"], indent=1))
    if missing_image_refs:
        print("image names referenced but not exported:", dict(missing_image_refs), file=sys.stderr)
    if missing_material_refs:
        print("material names referenced by models but not exported:", dict(missing_material_refs), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
