#!/usr/bin/env python3
"""Generate the entity class reference from the IW8 entity definitions.

Reads:
  data/iw8_entity_defs.json                       the editor's entity table (schema 1)
  data/iw8_entity_inventory.md                    stock usage counts (Top 40 classes table)
Writes:
  docs/ENTITY_REFERENCE.md                        the reference (Markdown)
  data/generated/entity_reference.html            the same content for the editor's help viewer (QTextBrowser)

Run from any folder:
  py -3 -I tools\\build_entity_reference.py

The outputs are generated. Do not edit them by hand. Change the JSON or this script and run it again.
Output is deterministic: running it twice gives identical files.
"""

import html
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFS_JSON = REPO / "data" / "iw8_entity_defs.json"
INVENTORY_MD = REPO / "data" / "iw8_entity_inventory.md"
OUT_MD = REPO / "docs" / "ENTITY_REFERENCE.md"
OUT_HTML = REPO / "data" / "generated" / "entity_reference.html"

# Category rules. A class gets the category of the FIRST rule whose regex matches its classname
# (re.search). Classes that match no rule fall through to the last rule, Miscellaneous.
# The order of this list is the order of the categories on the page.
CATEGORY_RULES = [
    ("Spawns", r"^info_player_start$|^mp_.*_spawn"),
    ("Game modes", r"^script_struct_(mp_|infil_)|^trigger_multiple_mp_"),
    ("Triggers", r"^trigger_"),
    ("Script", r"^script_(struct|origin)"),
    ("Lights and atmosphere", r"^light|^reflection_probe$|^worldspawn$"),
    ("Effects and sound", r"^(fx_|sound|snd_|misc_sound)"),
    ("Navigation", r"^node_"),
    ("Models and prefabs", r"^(misc_model|misc_prefab|script_model|script_brushmodel|collmap_scriptable|triggermodeldummydefault)$"),
    ("Miscellaneous", r""),
]
CATEGORY_RULES_COMPILED = [(name, re.compile(rx)) for name, rx in CATEGORY_RULES]

# Key types, worded the way src/core/entity_defs.cpp (checkValue) checks them.
TYPE_TEXT = {
    "int": "int: a whole number",
    "float": "float: a number",
    "bool": "bool: 0, 1, true or false",
    "vec3": "vec3: three numbers, x y z",
    "angles": "angles: three numbers",
    "model": "model: a model name, or a brush model index written as *N",
    "material": "material: a material name",
    "string": "string: any text",
    "targetname": "targetname: any text, the name other entities use",
}

# Sentences of a class description that talk about the build are repeated as the build note.
BUILD_SENTENCE = re.compile(r"converter|build|accepted|dropped|not built|editor", re.IGNORECASE)

# One row of the "Top 40 classes" table: | # | `class` | count | maps | ...
INVENTORY_ROW = re.compile(r"^\|\s*\d+\s*\|\s*`([^`]+)`\s*\|\s*(\d+)\s*\|\s*([^|]*?)\s*\|")


def category_of(classname):
    for name, rx in CATEGORY_RULES_COMPILED:
        if rx.search(classname):
            return name
    return "Miscellaneous"


def fmt_num(value):
    f = float(value)
    return str(int(f)) if f.is_integer() else "%g" % f


def hex_colour(rgb):
    return "#" + "".join("%02X" % round(max(0.0, min(1.0, float(x))) * 255) for x in rgb)


def load_defs(warnings):
    with open(DEFS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    if data.get("schema") != 1:
        raise SystemExit("unsupported schema in %s (expected 1)" % DEFS_JSON)
    classes = data.get("classes")
    if not isinstance(classes, dict) or not classes:
        raise SystemExit("no classes in %s" % DEFS_JSON)
    return classes


def load_inventory():
    usage = {}
    with open(INVENTORY_MD, encoding="utf-8") as f:
        for line in f:
            m = INVENTORY_ROW.match(line.rstrip("\n"))
            if m:
                usage[m.group(1)] = (int(m.group(2)), m.group(3))
    return usage


def build_note(description):
    sentences = re.split(r"(?<=\.)\s+", description)
    return " ".join(s for s in sentences if BUILD_SENTENCE.search(s))


def build_model(classes, usage, warnings):
    entries = []
    for name in sorted(classes):
        c = classes[name]
        kind = c.get("kind", "")
        if kind not in ("point", "brush"):
            warnings.append("%s: unknown kind %r" % (name, kind))
        desc = (c.get("description") or "").strip()
        if not desc:
            warnings.append("%s: empty description" % name)
        size = c.get("size")
        if kind == "point" and size is None:
            warnings.append("%s: point class has no size" % name)
        keys = []
        for kname in sorted(c.get("keys", {})):
            k = c["keys"][kname]
            ktype = k.get("type", "")
            if ktype not in TYPE_TEXT:
                warnings.append("%s.%s: unknown type %r (shown as written)" % (name, kname, ktype))
            keys.append({
                "name": kname,
                "type": ktype,
                "default": k.get("default", "") or "",
                "required": bool(k.get("required")),
                "description": (k.get("description") or "").strip(),
                "examples": list(k.get("examples") or []),
            })
        kept = bool(c.get("buildAccepted"))
        entries.append({
            "name": name,
            "category": category_of(name),
            "kind": kind,
            "description": desc,
            "kept": kept,
            "note": build_note(desc),
            "size": size,
            "colour": c.get("color", [0.8, 0.8, 0.8]),
            "keys": keys,
            "seen": usage.get(name),
        })
    return entries


# ---------- Markdown ----------

def md_prose(s):
    return (s.replace("\\", "\\\\").replace("*", "\\*").replace("<", "&lt;")
            .replace(">", "&gt;").replace("|", "\\|"))


def md_code(s):
    return "`" + s.replace("`", "'") + "`"


def md_slug(title):
    s = title.strip().lower()
    s = re.sub(r"[^a-z0-9 _-]", "", s)
    return s.replace(" ", "-")


def md_class(e):
    out = ["### " + md_code(e["name"]), ""]
    out.append(md_prose(e["description"]))
    out.append("")
    if e["kind"] == "point":
        out.append("- **Kind:** point entity.")
    else:
        out.append("- **Kind:** brush entity, no box.")
    if e["size"]:
        mins = ", ".join(fmt_num(v) for v in e["size"][0])
        maxs = ", ".join(fmt_num(v) for v in e["size"][1])
        out.append("- **Box:** from (%s) to (%s), in map units." % (mins, maxs))
    rgb = ", ".join(fmt_num(v) for v in e["colour"])
    out.append("- **Editor colour:** %s (RGB %s)." % (md_code(hex_colour(e["colour"])), rgb))
    build = "kept by the converter." if e["kept"] else "dropped by the converter."
    if e["note"]:
        build += " " + md_prose(e["note"])
    out.append("- **Build:** " + build)
    if e["seen"]:
        count, maps = e["seen"]
        out.append("- **Seen in stock maps:** %d (%s)." % (count, md_prose(maps)))
    out.append("")
    if not e["keys"]:
        out.append("This class has no keys.")
    else:
        out.append("| Key | Type / values | Default | Required | Help |")
        out.append("|---|---|---|---|---|")
        for k in e["keys"]:
            default = md_code(k["default"]) if k["default"] else "none"
            help_text = md_prose(k["description"])
            if k["examples"]:
                help_text += " Examples: " + ", ".join(md_code(x) for x in k["examples"][:3]) + "."
            typ = md_prose(TYPE_TEXT.get(k["type"], k["type"]))
            out.append("| %s | %s | %s | %s | %s |" % (
                md_code(k["name"]), typ, default, "yes" if k["required"] else "no", help_text))
    out.append("")
    return out


def render_md(entries, categories, counts, kept_n, dropped_n, usage_n):
    out = [
        "# IW8 Radiant entity reference",
        "",
        "This page lists the entity classes the IW8 Radiant editor knows. A class is the name you give an entity "
        "in a map, such as `light` or `mp_dm_spawn`. For each class, this page says what it does, whether the build "
        "keeps it, and which keys it takes.",
        "",
        "**This file is generated. Do not edit it by hand.** To change it, edit `data/iw8_entity_defs.json` or "
        "`tools/build_entity_reference.py`, then run this from the repo root:",
        "",
        "    py -3 -I tools\\build_entity_reference.py",
        "",
        "The same content is written to `data/generated/entity_reference.html` for the editor's help viewer.",
        "",
        "Sources: `data/iw8_entity_defs.json` (classes and keys) and `data/iw8_entity_inventory.md` "
        "(how often the stock maps use each class).",
        "",
        "## Summary",
        "",
        "- %d classes in %d categories." % (len(entries), len(categories)),
        "- %d kept by the build, %d dropped." % (kept_n, dropped_n),
        "- Stock usage is listed for %d classes, from the two zones in the inventory (mp_shipment and mp_backlot2)."
        % usage_n,
        "",
        "## Reading the entries",
        "",
        "- **Kind:** a point entity has a box in the editor. A brush entity is drawn from brushes.",
        "- **Build:** kept means the converter accepts the class. Dropped means the converter ignores it.",
        "- **Required:** yes means Check Map reports the entity when that key is empty.",
        "- **Type / values:** the editor checks each value against the type. A value that does not fit is reported.",
        "- **Default:** the default listed in the definitions. None means the definitions give no default.",
        "",
        "## Contents",
        "",
    ]
    for cat in categories:
        members = [e for e in entries if e["category"] == cat]
        links = ", ".join("[%s](#%s)" % (md_code(e["name"]), e["name"]) for e in members)
        if not members:
            out.append("**[%s](#%s)** (0 classes): none in the entity data." % (cat, md_slug(cat)))
        else:
            out.append("**[%s](#%s)** (%d classes): %s" % (cat, md_slug(cat), len(members), links))
        out.append("")
    for cat in categories:
        members = [e for e in entries if e["category"] == cat]
        out.append("## " + cat)
        out.append("")
        if not members:
            out.append("No classes in the entity data fall into this category.")
            out.append("")
            continue
        out.append("%d classes." % len(members))
        out.append("")
        for e in members:
            out.extend(md_class(e))
    return "\n".join(out).rstrip("\n") + "\n"


# ---------- HTML (basic tags only: h1-h3, p, table, tr, th, td, code, a) ----------

def h_esc(s):
    return html.escape(s, quote=False)


def html_class(e):
    out = ['<h3><a name="%s"></a><code>%s</code></h3>' % (h_esc(e["name"]), h_esc(e["name"]))]
    out.append("<p>%s</p>" % h_esc(e["description"]))
    kind = "point entity." if e["kind"] == "point" else "brush entity, no box."
    out.append("<p>Kind: %s</p>" % kind)
    if e["size"]:
        mins = ", ".join(fmt_num(v) for v in e["size"][0])
        maxs = ", ".join(fmt_num(v) for v in e["size"][1])
        out.append("<p>Box: from (%s) to (%s), in map units.</p>" % (mins, maxs))
    rgb = ", ".join(fmt_num(v) for v in e["colour"])
    out.append("<p>Editor colour: <code>%s</code> (RGB %s).</p>" % (hex_colour(e["colour"]), rgb))
    build = "kept by the converter." if e["kept"] else "dropped by the converter."
    if e["note"]:
        build += " " + e["note"]
    out.append("<p>Build: %s</p>" % h_esc(build))
    if e["seen"]:
        count, maps = e["seen"]
        out.append("<p>Seen in stock maps: %d (%s).</p>" % (count, h_esc(maps)))
    if not e["keys"]:
        out.append("<p>This class has no keys.</p>")
    else:
        out.append('<table border="1">')
        out.append("<tr><th>Key</th><th>Type / values</th><th>Default</th><th>Required</th><th>Help</th></tr>")
        for k in e["keys"]:
            default = "<code>%s</code>" % h_esc(k["default"]) if k["default"] else "none"
            help_text = h_esc(k["description"])
            if k["examples"]:
                help_text += " Examples: " + ", ".join("<code>%s</code>" % h_esc(x) for x in k["examples"][:3]) + "."
            out.append("<tr><td><code>%s</code></td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
                h_esc(k["name"]), h_esc(TYPE_TEXT.get(k["type"], k["type"])), default,
                "yes" if k["required"] else "no", help_text))
        out.append("</table>")
    return out


def render_html(entries, categories, kept_n, dropped_n, usage_n):
    out = [
        "<!DOCTYPE html>",
        "<html><head><meta charset=\"utf-8\">",
        "<title>IW8 Radiant entity reference</title>",
        "<style>table { border-collapse: collapse; } th, td { border: 1px solid #888888; padding: 4px; "
        "vertical-align: top; }</style>",
        "</head><body>",
        "<h1>IW8 Radiant entity reference</h1>",
        "<p>This page lists the entity classes the IW8 Radiant editor knows. A class is the name you give an entity "
        "in a map, such as <code>light</code> or <code>mp_dm_spawn</code>. For each class, this page says what it "
        "does, whether the build keeps it, and which keys it takes.</p>",
        "<p>This file is generated. Do not edit it by hand. Regenerate it with "
        "<code>py -3 -I tools\\build_entity_reference.py</code> from the repo root. The Markdown version is "
        "<code>docs/ENTITY_REFERENCE.md</code>.</p>",
        "<h2>Summary</h2>",
        "<p>%d classes in %d categories. %d kept by the build, %d dropped. Stock usage is listed for %d classes, "
        "from the two zones in the inventory (mp_shipment and mp_backlot2).</p>"
        % (len(entries), len(categories), kept_n, dropped_n, usage_n),
        "<h2>Reading the entries</h2>",
        "<p>Kind: a point entity has a box in the editor. A brush entity is drawn from brushes. "
        "Build: kept means the converter accepts the class. Dropped means the converter ignores it.</p>",
        "<p>Required: yes means Check Map reports the entity when that key is empty. Type / values: the editor "
        "checks each value against the type. Default: the default listed in the definitions. None means the "
        "definitions give no default.</p>",
        "<h2>Contents</h2>",
    ]
    for cat in categories:
        members = [e for e in entries if e["category"] == cat]
        if not members:
            out.append("<p><a href=\"#cat-%s\">%s</a> (0 classes): none in the entity data.</p>"
                       % (md_slug(cat), h_esc(cat)))
            continue
        links = ", ".join('<a href="#%s"><code>%s</code></a>' % (h_esc(e["name"]), h_esc(e["name"]))
                          for e in members)
        out.append("<p><a href=\"#cat-%s\">%s</a> (%d classes): %s</p>"
                   % (md_slug(cat), h_esc(cat), len(members), links))
    for cat in categories:
        members = [e for e in entries if e["category"] == cat]
        out.append('<h2><a name="cat-%s"></a>%s</h2>' % (md_slug(cat), h_esc(cat)))
        if not members:
            out.append("<p>No classes in the entity data fall into this category.</p>")
            continue
        out.append("<p>%d classes.</p>" % len(members))
        for e in members:
            out.extend(html_class(e))
    out.append("</body></html>")
    return "\n".join(out) + "\n"


def drop_citations(text):
    """The entity definitions cite internal design notes ("(contract 15 s3.4)", "(doc 31)", "(research 63)"); the
    published reference leaves those citations out."""
    cite = r"(?:contract \d+ s[\d.]+(?:, s[\d.]+)*|docs?/research/[\w./-]*|research \d+(?: / \d+)*|roadmap [\w.]+|doc \d+)"
    text = re.sub(r"\s*\(" + cite + r"\)", "", text)  # a bracket that only cites
    text = re.sub(r"[,;] " + cite + r"(?=[);])", "", text)  # a citation at the end of a longer bracket
    text = re.sub(r"\(" + cite + r"; ", "(", text)  # a citation at the start of a longer bracket
    text = re.sub(r" in " + cite, "", text)
    return text


def main():
    warnings = []
    classes = load_defs(warnings)
    usage = load_inventory()
    for name in sorted(usage):
        if name not in classes:
            warnings.append("inventory lists %s, which is not in the JSON" % name)

    entries = build_model(classes, usage, warnings)
    categories = [name for name, _ in CATEGORY_RULES]
    counts = {cat: 0 for cat in categories}
    for e in entries:
        counts[e["category"]] += 1
    kept_n = sum(1 for e in entries if e["kept"])
    dropped_n = len(entries) - kept_n
    usage_n = sum(1 for e in entries if e["seen"])

    md_text = drop_citations(render_md(entries, categories, counts, kept_n, dropped_n, usage_n))
    html_text = drop_citations(render_html(entries, categories, kept_n, dropped_n, usage_n))

    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_HTML.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_MD, "w", encoding="utf-8", newline="\n") as f:
        f.write(md_text)
    with open(OUT_HTML, "w", encoding="utf-8", newline="\n") as f:
        f.write(html_text)

    print("classes: %d (kept %d, dropped %d)" % (len(entries), kept_n, dropped_n))
    for cat in categories:
        print("  %-22s %3d" % (cat, counts[cat]))
    print("stock usage listed for %d classes" % usage_n)
    for w in warnings:
        print("warning: " + w)
    print("wrote " + str(OUT_MD))
    print("wrote " + str(OUT_HTML))
    return 0


if __name__ == "__main__":
    sys.exit(main())
