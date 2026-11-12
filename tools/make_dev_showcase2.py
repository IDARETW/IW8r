"""Write the dev showcase, version 2: one tight arena with a grid of numbered stations, one asset or object type each.

Output (all new names; nothing that already exists in samples/dev is touched):
  samples/dev/mp_iw8r_show2.map      the map. The build takes its map id from the file name: mp_iw8r_show2.
  samples/dev/mp_iw8r_show2.gsc      the map script (station messages, use / HUD messages, a blinking crate)
  samples/dev/materials/show2/       custom materials: sign plates, pad colours, shader variations
  samples/dev/models/                show2_crate.obj, iw8r_shape_cube_show2.obj
  samples/dev/sounds/                show2_hum.wav, show2_bed.wav, show2_chirp.wav (+ settings)
  samples/dev/fx/show2/              sparkle.iw8fx.json (an authored effect)
  samples/dev/lights/                show2_spot_narrow.ies (copy of data/ies/iw8r_spot_narrow.ies)
  docs/DEV_SHOWCASE.md               the test sheet, one row per station

Layout: rows of 192-unit cells (12 per row), a 96-unit walkway south of every row, Team Deathmatch spawns on the
south strip. Every station has a sign over its south edge (number in seven-segment brushes, name on a texture), a
trigger_radius named s<NN>_<name> that prints the number and name on screen, and entity names that start with s<NN>_.

Python 3 standard library only; the output is the same on every run.
Usage: py -3 -I tools/make_dev_showcase2.py [--out-dir samples/dev] [--sheet docs/DEV_SHOWCASE.md]
"""
import argparse
import json
import math
import os
import struct
import zlib

MAP_ID = "mp_iw8r_show2"
CELL, NCOL, WALK, STRIP = 192, 12, 96, 256
ROWP = CELL + WALK
MARGIN = 64
SIGN_Z0, SIGN_Z1 = 128, 192

FLOOR = "tm2co/me_curb_stone_01"
WALL = "tm2co/me_plaster_barrier_fence_wall"
WOOD = "tm2co/construction_wood_form_beams_worn_oak_single"
METAL = "tmo/cp_disco_metal_rusted_01"
GRASS = "tmo/ee_foliage_grass_lm_short_green"
DIGIT, BOARD = "show2/digit", "show2/board"

BARREL = "tm/container_barrel_plastic_01_closed"
CHAIR = "tm/furniture_cafe_chair_01"
TABLE = "tm/furniture_cafe_table_02"
CONE = "com_trafficcone_01_dyn"
CINDER = "tm/cinderblock_pallet_01"
BUSH = "tm/foliage_alder_bush_large_01"
DRYER = "tm/ee_appliance_dryer"
FAN = "desktop_fan_fn_01"
LADDER = "tm/ladder_old_wood_01_160"

SEEN, NOT = "seen working", "not seen"

# id, sign text, pad colour
GROUPS = [
    ("world", "WORLD", (200, 200, 200)), ("lighting", "LIGHTING", (240, 200, 60)), ("brushes", "BRUSHES", (150, 160, 175)),
    ("materials", "MATERIALS", (90, 190, 90)), ("models", "MODELS", (70, 130, 230)), ("scripted", "SCRIPTED", (240, 140, 40)),
    ("triggers", "TRIGGERS", (220, 70, 70)), ("fxsound", "FX + SOUND", (170, 90, 210)), ("nav", "NAV + CAMERAS", (150, 110, 70)),
    ("stock", "STOCK KINDS", (230, 120, 180)), ("modes", "GAME MODES", (40, 180, 180)),
]
GROUP_TEXT = {g[0]: g[1] for g in GROUPS}
GROUP_RGB = {g[0]: g[2] for g in GROUPS}


def n(v):
    """A number as the map text wants it: no trailing zeros."""
    if abs(v - round(v)) < 1e-9:
        return str(int(round(v)))
    return ("%.3f" % v).rstrip("0").rstrip(".")


# ---------------------------------------------------------------- brushes

def face(a, b, c, mat, tex=None):
    w, h, xo, yo, rot = tex or (128, 128, 0, 0, 0)
    p = lambda v: "( %s %s %s )" % (n(v[0]), n(v[1]), n(v[2]))
    if mat == "trigger":  # the editor's own trigger faces (gamemode_templates.cpp kTriggerSuffix)
        return " %s %s %s trigger 128 128 0 0 0 0" % (p(a), p(b), p(c))
    return " %s %s %s %s %s %s %s %s %s 0 lightmap_gray 16384 16384 0 0 0 0" % (p(a), p(b), p(c), mat, n(w), n(h), n(xo), n(yo), n(rot))


def box(x0, y0, z0, x1, y1, z1, mat, flags=(), top=None, south=None):
    """An axis-aligned brush. top / south: (material, tex) for that face. flags: detail, nonColliding, weaponClip."""
    tm, tt = top if top else (mat, None)
    sm, st = south if south else (mat, None)
    return [" contents %s;" % f for f in flags] + [
        face((x1, y1, z0), (x0, y1, z0), (x0, y0, z0), mat),
        face((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), tm, tt),
        face((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), sm, st),
        face((x1, y1, z1), (x0, y1, z1), (x0, y1, z0), mat),
        face((x1, y0, z1), (x1, y1, z1), (x1, y1, z0), mat),
        face((x0, y1, z1), (x0, y0, z1), (x0, y0, z0), mat),
    ]


def wedge(x0, y0, x1, y1, h, mat):
    """A ramp rising along +x from 0 to h."""
    return [
        face((x1, y1, 0), (x0, y1, 0), (x0, y0, 0), mat),
        face((x0, y0, 0), (x1, y0, h), (x1, y0, 0), mat),
        face((x1, y1, 0), (x1, y1, h), (x0, y1, 0), mat),
        face((x1, y0, h), (x1, y1, h), (x1, y1, 0), mat),
        face((x0, y1, 0), (x1, y1, h), (x1, y0, h), mat),
    ]


def mesh(points, mat):
    """A patch: points[c][r] = (x, y, z), columns of rows, as the editor writes them."""
    cols, rows = len(points), len(points[0])
    lines = [" mesh", " {", "  " + mat, "  lightmap_gray", "  %d %d 16 8" % (cols, rows)]
    for c in range(cols):
        lines.append("  (")
        for r in range(rows):
            x, y, z = points[c][r]
            lines.append("\tv %s %s %s t %d %d %d %d" % (n(x), n(y), n(z), c * 512, r * 512, c * 4, r * 4))
        lines.append("  )")
    lines.append(" }")
    return lines


SEGMENTS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgecd", "7": "abc",
            "8": "abcdefg", "9": "abcdfg"}


def digit_brushes(ch, x0, z0, y_face):
    """Seven-segment digit, 22 wide and 44 tall, standing 3 units proud of a south-facing board at y_face."""
    w, h, t = 22, 44, 5
    segs, out = set(SEGMENTS[ch]), []

    def bar(xa, za, xb, zb):
        out.append(box(xa, y_face - 3, za, xb, y_face, zb, DIGIT, flags=("detail",)))
    if "a" in segs:
        bar(x0 + t, z0 + h - t, x0 + w - t, z0 + h)
    if "d" in segs:
        bar(x0 + t, z0, x0 + w - t, z0 + t)
    if "g" in segs:
        bar(x0 + t, z0 + h / 2 - t / 2, x0 + w - t, z0 + h / 2 + t / 2)
    for up, lo, xa in (("f", "e", x0), ("b", "c", x0 + w - t)):
        if up in segs and lo in segs:
            bar(xa, z0, xa + t, z0 + h)
        elif up in segs:
            bar(xa, z0 + h / 2, xa + t, z0 + h)
        elif lo in segs:
            bar(xa, z0, xa + t, z0 + h / 2)
    return out


# ---------------------------------------------------------------- images

FONT = {
    "A": "0E11111F111111", "B": "1E11111E11111E", "C": "0E11101010110E", "D": "1E11111111111E", "E": "1F10101E10101F",
    "F": "1F10101E101010", "G": "0E11101711110F", "H": "1111111F111111", "I": "0E04040404040E", "J": "0702020202120C",
    "K": "11121418141211", "L": "1010101010101F", "M": "111B1515111111", "N": "11191513111111", "O": "0E11111111110E",
    "P": "1E11111E101010", "Q": "0E11111115120D", "R": "1E11111E141211", "S": "0F10100E01011E", "T": "1F040404040404",
    "U": "1111111111110E", "V": "11111111110A04", "W": "1111111515 1B11".replace(" ", ""), "X": "11110A040A1111",
    "Y": "11110A04040404", "Z": "1F01020408101F", "0": "0E11131519110E", "1": "040C040404040E", "2": "0E11010204081F",
    "3": "1F02040201110E", "4": "02060A121F0202", "5": "1F101E0101110E", "6": "0608101E11110E", "7": "1F010204080808",
    "8": "0E11110E11110E", "9": "0E11110F01020C", " ": "00000000000000", "+": "0004041F040400", "-": "0000001F000000",
    "/": "01010204081010", ".": "00000000000C0C", ":": "000C0C000C0C00", "&": "0C12140815120D", "(": "02040808080402",
    ")": "08040202020408", ",": "000000000C0408", "=": "00001F001F0000", "_": "0000000000001F",
}


class Img:
    """A small RGBA canvas (rectangles and 5x7 text), saved as PNG."""

    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.rows = [[bg] * w for _ in range(h)]

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h, y1)):
            row = self.rows[y]
            for x in range(max(0, x0), min(self.w, x1)):
                row[x] = c

    def text(self, x, y, s, c, sx, sy):
        for ch in s.upper():
            g = FONT.get(ch, FONT[" "])
            for r in range(7):
                bits = int(g[r * 2:r * 2 + 2], 16)
                for k in range(5):
                    if bits & (0x10 >> k):
                        self.rect(x + k * sx, y + r * sy, x + (k + 1) * sx, y + (r + 1) * sy, c)
            x += 6 * sx

    def png(self):
        raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in self.rows)
        chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        head = bytes([0x89]) + b"PNG" + bytes([13, 10, 26, 10])
        return (head + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def wrap(words, width, lines):
    out, cur = [], ""
    for wd in words.split():
        if cur and len(cur) + 1 + len(wd) > width:
            out.append(cur)
            cur = wd
        else:
            cur = (cur + " " + wd).strip()
    if cur:
        out.append(cur)
    return [l[:width] for l in out[:lines]]


def sign_image(group, title):
    """512 x 128 for a 192 x 64 unit board: the left third stays dark (the brush digits stand in front of it)."""
    rgb = GROUP_RGB[group]
    img = Img(512, 128, (18, 20, 24, 255))
    img.rect(0, 118, 512, 128, rgb + (255,))
    img.rect(170, 6, 173, 112, rgb + (255,))
    img.text(182, 8, GROUP_TEXT[group], rgb + (255,), 4, 3)
    y = 37
    for line in wrap(title, 13, 3):
        img.text(182, y, line, (240, 240, 240, 255), 4, 3)
        y += 27
    return img.png()


def checker(a, b, size=64, step=16):
    img = Img(size, size, a)
    for y in range(0, size, step):
        for x in range(0, size, step):
            if (x // step + y // step) % 2:
                img.rect(x, y, x + step, y + step, b)
    return img.png()


# ---------------------------------------------------------------- sounds and models

def wav(samples, rate=44100):
    data = struct.pack("<%dh" % len(samples), *[max(-32767, min(32767, int(s * 32767))) for s in samples])
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) +
            b"data" + struct.pack("<I", len(data)) + data)


def sound_hum():
    return wav([0.45 * (math.sin(2 * math.pi * 220 * i / 44100) + 0.3 * math.sin(2 * math.pi * 440 * i / 44100) +
                        0.15 * math.sin(2 * math.pi * 660 * i / 44100)) for i in range(88200)])


def sound_bed():
    return wav([0.3 * (0.6 + 0.4 * math.sin(2 * math.pi * 0.25 * i / 44100)) *
                (math.sin(2 * math.pi * 55 * i / 44100) + 0.6 * math.sin(2 * math.pi * 82.5 * i / 44100) +
                 0.4 * math.sin(2 * math.pi * 110 * i / 44100)) for i in range(176400)])


def sound_chirp():
    count, out, phase = 15435, [], 0.0
    for i in range(count):
        f = i / count
        phase += 2 * math.pi * (900 + 900 * f) / 44100
        out.append(0.6 * math.sin(phase) * (1 - f) * min(1.0, i / 400.0))
    return wav(out)


def cube_obj(half, height, material, comment):
    """OBJ cube (Y-up, map units, origin at the base centre), written like the editor's shape models."""
    s, h = half, height
    faces = [((0, -1, 0), [(-s, 0, s), (-s, 0, -s), (s, 0, -s), (s, 0, s)]), ((0, 1, 0), [(-s, h, s), (s, h, s), (s, h, -s), (-s, h, -s)]),
             ((1, 0, 0), [(s, 0, s), (s, 0, -s), (s, h, -s), (s, h, s)]), ((-1, 0, 0), [(-s, 0, s), (-s, h, s), (-s, h, -s), (-s, 0, -s)]),
             ((0, 0, 1), [(-s, 0, s), (s, 0, s), (s, h, s), (-s, h, s)]), ((0, 0, -1), [(-s, 0, -s), (-s, h, -s), (s, h, -s), (s, 0, -s)])]
    v, vt, vn, f = [], [], [], []
    for normal, quad in faces:
        base = len(v) + 1
        v += ["v %s %s %s" % tuple(n(c) for c in p) for p in quad]
        vt += ["vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1"]
        vn += ["vn %d %d %d" % normal] * 4
        f += ["f %d/%d/%d %d/%d/%d %d/%d/%d" % ((base,) * 3 + (base + 1,) * 3 + (base + 2,) * 3),
              "f %d/%d/%d %d/%d/%d %d/%d/%d" % ((base,) * 3 + (base + 2,) * 3 + (base + 3,) * 3)]
    return "\n".join(["# " + comment] + v + vt + vn + ["usemtl " + material] + f) + "\n"


# ---------------------------------------------------------------- the map

class Gen:
    def __init__(self):
        self.world = []     # worldspawn primitives
        self.ents = []      # (key/value list, primitives)
        self.stations = []  # St, in order
        self.script = []    # user_main lines
        self.col = self.row = 0

    def ent(self, cls, kvs, brushes=()):
        self.ents.append(([("classname", cls)] + list(kvs), list(brushes)))


class St:
    """One station: w cells wide, local coordinates from its south-west corner."""

    def __init__(self, g, num, group, slug, title, w, row, col):
        self.g, self.num, self.group, self.slug, self.title, self.w, self.row, self.col = g, num, group, slug, title, w, row, col
        self.x0, self.y0 = col * CELL, STRIP + row * ROWP
        self.what = self.keys = self.see = ""
        self.state = NOT

    def name(self, suffix, station=False):
        if suffix == self.slug and not station:
            raise SystemExit("station %d: entity name '%s' is the name of the station trigger" % (self.num, suffix))
        return "s%02d_%s" % (self.num, suffix)

    def pos(self, lx, ly, z=0):
        return "%s %s %s" % (n(self.x0 + lx), n(self.y0 + ly), n(z))

    def box(self, lx0, ly0, z0, lx1, ly1, z1, mat, **kw):
        return box(self.x0 + lx0, self.y0 + ly0, z0, self.x0 + lx1, self.y0 + ly1, z1, mat, **kw)

    def solid(self, lx0, ly0, z0, lx1, ly1, z1, mat, **kw):
        self.g.world.append(self.box(lx0, ly0, z0, lx1, ly1, z1, mat, **kw))

    def mark(self, lx0, ly0, lx1, ly1):
        """A bright 1-unit floor plate that shows where an invisible volume is."""
        self.solid(lx0, ly0, 0, lx1, ly1, 1, DIGIT, flags=("detail",))

    def trig(self, lx0, ly0, z0, lx1, ly1, z1):
        return self.box(lx0, ly0, z0, lx1, ly1, z1, "trigger")

    def ent(self, cls, kvs, brushes=()):
        self.g.ent(cls, kvs, brushes)

    def model(self, cls, model, lx, ly, z=0, extra=()):
        self.ent(cls, [("model", model), ("origin", self.pos(lx, ly, z))] + list(extra))

    def sheet(self, what, keys, see, state=NOT):
        self.what, self.keys, self.see, self.state = what, keys, see, state

    def say(self, trigger, text):
        self.g.script.append('    level thread show2_message("%s", "%s");' % (trigger, text))


def tmpl(S, cls, lx, ly, z, keys):
    """A point entity as gamemode_templates.cpp writes it: classname, origin, then the keys in its order."""
    S.ent(cls, [("origin", S.pos(lx, ly, z))] + keys)


def tmpl_brush(S, cls, lx, ly, hx, hy, z0, z1, keys):
    """A brush trigger as gamemode_templates.cpp writes it: origin at the box centre, trigger faces."""
    S.ent(cls, [("origin", S.pos(lx, ly, (z0 + z1) / 2))] + keys, [S.trig(lx - hx, ly - hy, z0, lx + hx, ly + hy, z1)])


FMG = ("footprint_mask_group", "default")


def spawn_set(S, generic, allies, axis, lx0, lx1, ly):
    """Spawn set as the editor template: two generic spawns (when the mode has them), an allies and an axis start."""
    if generic:
        tmpl(S, generic, lx0 + 40, ly, 8, [("angles", "0 0 0"), FMG])
        tmpl(S, generic, lx1 - 40, ly, 8, [("angles", "0 180 0"), FMG])
    tmpl(S, allies, lx0, ly, 8, [("angles", "0 0 0"), FMG])
    tmpl(S, axis, lx1, ly, 8, [("angles", "0 180 0"), FMG])


# ---- station builders ------------------------------------------------------------------------------------------------

def st_world(S):
    S.solid(88, 88, 0, 96, 96, 96, WALL, flags=("detail",))
    S.sheet("World settings: sun, stock sky, fog, film grade, sun sprite, map reverb",
            "worldspawn: sundirection, suncolor, skyname, iw8r_fog, iw8r_film, iw8r_sun_sprite, iw8r_reverb",
            "Shipment sky, light fog, a slight film grade. The thin post is a sun dial: its shadow points away from the bright "
            "spot in the sky (north-east). Sun disc and flare in the sky: not seen yet.", SEEN)


def st_spawns(S):
    tmpl(S, "mp_dm_spawn", 48, 96, 8, [("angles", "0 270 0"), FMG])
    tmpl(S, "mp_dm_spawn", 144, 96, 8, [("angles", "0 270 0"), FMG])
    tmpl(S, "mp_dm_spawn_start", 96, 140, 8, [("angles", "0 270 0"), FMG])
    tmpl(S, "info_player_start", 96, 60, 8, [("angles", "0 90 0"), FMG])
    S.sheet("Spawns: the Team Deathmatch set on the south strip (2 starts, 6 spawns), free-for-all spawns and a player start here",
            "mp_tdm_spawn, mp_tdm_spawn_allies_start, mp_tdm_spawn_axis_start, mp_dm_spawn, mp_dm_spawn_start, info_player_start: origin, angles",
            "You spawn on the south strip facing the stations. In free-for-all you spawn on this pad.", SEEN)


def st_omni(S):
    S.ent("light", [("targetname", S.name("light")), ("origin", S.pos(96, 96, 112)), ("_color", "1 1 1"), ("radius", "220"), ("intensity", "1")])
    S.model("misc_model", BARREL, 96, 130)
    S.sheet("Omni light, white", "light: _color, radius, intensity", "A round white pool of light on the floor and the barrel.", SEEN)


def st_omni_colour(S):
    S.ent("light", [("targetname", S.name("red")), ("origin", S.pos(48, 96, 80)), ("_color", "1 0.25 0.2"), ("radius", "130"), ("intensity", "1.5")])
    S.ent("light", [("targetname", S.name("blue")), ("origin", S.pos(144, 96, 80)), ("_color", "0.3 0.5 1"), ("radius", "130"), ("intensity", "1.5")])
    S.sheet("Omni lights, coloured", "light: _color 1 0.25 0.2 and 0.3 0.5 1, intensity 1.5", "A red pool on the left and a blue pool on the right.", SEEN)


def st_spot(S):
    S.ent("light", [("targetname", S.name("lamp")), ("origin", S.pos(96, 96, 200)), ("_color", "1 0.9 0.7"), ("radius", "360"), ("intensity", "2"),
                    ("fov_outer", "60"), ("fov_inner", "35"), ("angles", "90 0 0")])
    S.sheet("Spot light aimed by angles", "light: fov_outer 60, fov_inner 35, angles 90 0 0 (straight down)",
            "A warm round spot on the floor with a soft edge.", SEEN)


def st_spot_target(S):
    S.ent("light_spot", [("targetname", S.name("spot")), ("origin", S.pos(32, 48, 180)), ("_color", "0.8 1 0.8"), ("radius", "400"), ("intensity", "2"),
                         ("fov_outer", "50"), ("fov_inner", "30"), ("target", S.name("aim"))])
    S.ent("script_origin", [("targetname", S.name("aim")), ("origin", S.pos(150, 150, 0))])
    S.mark(142, 142, 158, 158)
    S.sheet("Spot light aimed at a target", "light_spot: target = a script_origin; fov_outer 50, fov_inner 30",
            "A greenish spot that falls slanted from the south-west corner onto the small bright plate.")


def st_spot_ies(S):
    common = [("_color", "1 1 1"), ("radius", "340"), ("intensity", "2"), ("fov_outer", "90"), ("fov_inner", "60"), ("angles", "90 0 0")]
    S.ent("light", [("targetname", S.name("plain")), ("origin", S.pos(48, 96, 200))] + common)
    S.ent("light", [("targetname", S.name("ies")), ("origin", S.pos(144, 96, 200))] + common + [("iw8r_ies", "lights/show2_spot_narrow.ies")])
    S.sheet("Spot light with an IES profile", "light: iw8r_ies lights/show2_spot_narrow.ies (the editor's stock narrow profile)",
            "Two spots that differ only in the profile: the left pool is plain, the right pool is narrower with a bright core.")


def st_spot_shadow(S):
    S.ent("light", [("targetname", S.name("spot")), ("origin", S.pos(24, 96, 150)), ("_color", "1 1 1"), ("radius", "420"), ("intensity", "2"),
                    ("fov_outer", "70"), ("fov_inner", "40"), ("target", S.name("aim")), ("iw8r_shadows", "1")])
    S.ent("script_origin", [("targetname", S.name("aim")), ("origin", S.pos(160, 96, 16))])
    S.solid(88, 88, 0, 104, 104, 96, WALL, flags=("detail",))
    S.sheet("Spot light that casts shadows", "light: iw8r_shadows 1 (spot lights only), target",
            "The pillar throws a sharp shadow to the east, away from the light. Walk through the beam: you throw one too.")


def st_light_keys(S):
    S.ent("light", [("targetname", S.name("units")), ("origin", S.pos(48, 96, 96)), ("_color", "1 1 1"), ("radius", "150"),
                    ("iw8r_intensity_units", "4"), ("iw8r_bulb_radius", "4")])
    S.ent("light", [("targetname", S.name("falloff")), ("origin", S.pos(144, 96, 96)), ("_color", "1 1 1"), ("radius", "150"),
                    ("iw8r_falloff", "0.6"), ("iw8r_fade", "0 300"), ("iw8r_shadow_bias", "1"), ("iw8r_uv_intensity", "0.5"), ("iw8r_ir_intensity", "0.5")])
    S.sheet("Light tuning keys", "light: iw8r_intensity_units 4 + iw8r_bulb_radius 4 (left); iw8r_falloff, iw8r_fade, iw8r_shadow_bias, "
            "iw8r_uv_intensity, iw8r_ir_intensity (right)",
            "Two white pools that differ in brightness and edge. The right one should fade out when you stand more than 300 units away.")


def st_spot_wide(S):
    common = [("_color", "1 0.8 0.6"), ("radius", "300"), ("intensity", "2"), ("angles", "90 0 0")]
    S.ent("light", [("targetname", S.name("wide140")), ("origin", S.pos(48, 96, 120)), ("fov_outer", "140"), ("fov_inner", "100")] + common)
    S.ent("light", [("targetname", S.name("wide160")), ("origin", S.pos(144, 96, 120)), ("fov_outer", "160"), ("fov_inner", "110")] + common)
    S.sheet("Wide spot lights (fix queue F.9)", "light: fov_outer 140 (a real spot) and 160 (150 or more is built as an omni light)",
            "Both sides are lit. If the left side is dark, a wide spot still does not show (F.9). The build log notes the right one.")


def st_probes(S):
    S.ent("reflection_probe", [("targetname", S.name("probe_box")), ("origin", S.pos(48, 96, 64)), ("mins", "-48 -96 -64"), ("maxs", "48 96 128"),
                               ("priority", "20"), ("feather", "16")])
    S.ent("reflection_probe", [("targetname", S.name("probe_auto")), ("origin", S.pos(144, 96, 64))])
    S.solid(8, 150, 0, 88, 176, 96, "show2/pad_triggers", flags=("detail",))
    S.solid(104, 150, 0, 184, 176, 96, "show2/pad_models", flags=("detail",))
    S.model("misc_model", DRYER, 48, 80)
    S.model("misc_model", DRYER, 144, 80)
    S.ent("light", [("targetname", S.name("light")), ("origin", S.pos(96, 60, 120)), ("radius", "220"), ("intensity", "1")])
    S.sheet("Reflection probe pair (fix queue F.10)", "reflection_probe: mins, maxs, priority, feather (left); origin only (right)",
            "The left dryer should reflect the red wall, the right dryer the blue wall. In the last playtest probes changed nothing.")


def st_volume(S):
    S.ent("iw8r_volume", [("targetname", S.name("fog_box")), ("origin", S.pos(96, 96, 64)), ("mins", "-80 -80 -64"), ("maxs", "80 80 96"),
                          ("density", "0.4"), ("falloff", "64"), ("color", "0.8 0.9 1")])
    S.ent("light", [("targetname", S.name("spot")), ("origin", S.pos(96, 96, 200)), ("radius", "360"), ("intensity", "2"), ("fov_outer", "50"),
                    ("fov_inner", "25"), ("angles", "90 0 0")])
    S.sheet("Volumetric box", "iw8r_volume: mins, maxs, density 0.4, falloff 64, color",
            "Lit haze inside a 160 unit box, with the spot light showing as a cone in it. Listed as not expected to work yet.")


def st_detail(S):
    S.solid(32, 72, 0, 80, 120, 64, WALL)
    S.solid(112, 72, 0, 160, 120, 64, WALL, flags=("detail",))
    S.sheet("Structural and detail brush", "worldspawn brush; brush line 'contents detail;' (Ctrl+M)",
            "Two blocks that look alike and both stop you and bullets. Editor: Camera Mode > Contents shows the right one green.")


def st_flags(S):
    S.solid(32, 72, 0, 80, 120, 64, WOOD, flags=("nonColliding",))
    S.solid(112, 72, 0, 160, 120, 64, METAL, flags=("weaponClip",))
    S.sheet("Brush flags: non-colliding and weapon clip", "brush lines 'contents nonColliding;' and 'contents weaponClip;'",
            "You walk through the wooden block (left). The metal block (right) is solid.")


def st_clip(S):
    for i, m in enumerate(["clip", "clip_player", "clip_weapon", "clip_nosight"]):
        S.mark(12 + i * 44, 76, 52 + i * 44, 116)
        S.solid(16 + i * 44, 80, 1, 48 + i * 44, 112, 72, m, flags=("detail",))
    S.sheet("Clip brushes", "tool materials clip, clip_player, clip_weapon, clip_nosight (west to east, one over each bright plate)",
            "All invisible. 1: stops you and bullets. 2: stops you only. 3: stops bullets only. 4: walk through; blocks AI sight.", SEEN)


def st_slopes(S):
    S.g.world.append(wedge(S.x0 + 16, S.y0 + 40, S.x0 + 112, S.y0 + 88, 48, FLOOR))
    for k in range(6):
        S.solid(16 + k * 16, 112, 0, 32 + k * 16, 160, (k + 1) * 8, FLOOR)
    S.sheet("Ramp and stairs", "worldspawn brushes: a 27 degree wedge and six 8-unit steps", "Both can be walked up.", SEEN)


def st_patch_curve(S):
    pts = [[(S.x0 + 32 + c * 64, S.y0 + 40, 0), (S.x0 + 32 + c * 64, S.y0 + 104, 128 if c % 2 == 0 else 96), (S.x0 + 32 + c * 64, S.y0 + 168, 0)]
           for c in range(3)]
    S.g.world.append(mesh(pts, WALL))
    S.sheet("Curved patch, drawn from both sides", "worldspawn patch (mesh 3 x 3). worldspawn iw8r_patch_one_sided 1 would turn back faces off",
            "An arch you can walk under from the side. It is visible from outside and from inside.", SEEN)


def st_patch_terrain(S):
    pts = [[(S.x0 + 16 + c * 40, S.y0 + 24 + r * 36, 12 + round(12 * math.sin(c * 1.3) * math.cos(r * 1.1))) for r in range(5)] for c in range(5)]
    S.g.world.append(mesh(pts, GRASS))
    S.sheet("Terrain patch", "worldspawn patch (mesh 5 x 5) with a stock grass material", "A bumpy grass sheet you can walk on.")


def st_glass(S):
    S.ent("script_brushmodel", [("targetname", S.name("pane"))], [S.box(32, 94, 0, 160, 98, 96, "iw8r_glass")])
    S.sheet("Glass pane", "script_brushmodel with the built-in material iw8r_glass (Primitives > Scripted Objects > Make Glass Pane)",
            "A see-through pane that shatters when shot.", SEEN)


def st_ladder(S):
    S.solid(24, 64, 0, 88, 128, 160, WALL)
    S.solid(90.2, 84, 0, 92.2, 108, 160, "ladder")
    S.model("script_model", LADDER, 88, 96, 0, [("targetname", S.name("ladder_model")), ("iw8r_no_collision", "1")])
    S.sheet("Ladder", "tool material 'ladder' on a thin slab + script_model with iw8r_no_collision 1 (Make Ladder layout)",
            "Walk into the ladder on the east face of the tower: you climb from the floor to the top, hands on the rungs.")


def st_mantle(S):
    for i, h in enumerate([40, 56, 72]):
        S.solid(16 + i * 56, 80, 0, 64 + i * 56, 144, h, FLOOR)
    S.sheet("Mantle ledges", "worldspawn brushes 40, 56 and 72 units high (there is no mantle key: any ledge can be mantled)",
            "Jump at each block: you pull yourself up onto it.", SEEN)


def st_water(S):
    S.solid(32, 48, 0, 160, 144, 40, "water")
    S.sheet("Water volume", "tool material 'water' (not solid; Replay has no swim volumes)", "A blue rippled box that you walk through.", SEEN)


def st_prefabs(S):
    S.ent("misc_prefab", [("targetname", S.name("prefab_stock")), ("model", "stock/barrel/tm_container_barrel_plastic_01_closed_v01.map"),
                          ("origin", S.pos(56, 100, 0))])
    S.ent("misc_prefab", [("targetname", S.name("prefab_lamp")), ("model", "iw8r/lamps/lamp_short_table.map"), ("origin", S.pos(140, 100, -40))])
    S.sheet("Prefabs", "misc_prefab: model = a prefab .map of the Prefabs dock (a stock barrel group, the editor's table lamp)",
            "A group of blue barrels on the left; a small table lamp on the floor on the right, with its own light.")


def st_decal(S):
    S.ent("iw8r_decal", [("targetname", S.name("decal_mark")), ("origin", S.pos(96, 96, 4)), ("angles", "90 0 0"), ("size", "8 96 96"),
                         ("material", "show2/decal"), ("opacity", "0.9"), ("feather", "0.2"), ("draw_order", "1")])
    S.sheet("Decal", "iw8r_decal: angles 90 0 0 (onto the floor), size 8 96 96, material, opacity, feather, draw_order",
            "Nothing yet. Later: a round orange mark on the floor.", "waiting: the converter has no decal producer yet")


def st_stock_mat(S):
    for i, m in enumerate(["tm2co/storage_shipping_container_metal_painted_01_second_green_wet", WOOD, "tmo/building_wall_corner_brick_01"]):
        S.solid(20 + i * 56, 76, 0, 60 + i * 56, 116, 96, m)
    S.sheet("Stock materials", "brush faces with stock names: painted metal, wood, brick", "Three textured pillars with normal maps and gloss.", SEEN)


def panel(S, mat):
    S.solid(32, 94, 0, 160, 98, 96, mat, flags=("detail",))


def st_stock_cutout(S):
    panel(S, "tmo/cp_disco_chainlink_fence_01_onesided")
    S.sheet("Stock cut-out material", "stock chainlink fence material (alpha-tested)", "A fence panel you can see through.", SEEN)


def st_custom(S):
    S.solid(64, 64, 0, 128, 128, 96, "show2/solid")
    S.sheet("Custom material", "materials/show2/solid.iw8mat.json: color, surface metal, scale", "An orange and dark checker block.", SEEN)


def st_cutout(S):
    panel(S, "show2/cutout")
    S.sheet("Custom alpha-tested material", "show2/cutout: alphaTest true, twoSided true", "A grate panel with holes, visible from both sides.", SEEN)


def st_blend(S):
    panel(S, "show2/blend")
    S.sheet("Custom alpha-blended material", "show2/blend: blend true, twoSided true", "A half see-through blue panel.")


def st_add(S):
    panel(S, "show2/add")
    S.sheet("Additive material", "show2/add: blendMode add, unlit true, twoSided true", "A soft orange glow you can see through, from both sides.")


def st_multiply(S):
    S.solid(48, 100, 0, 144, 116, 96, WALL)
    S.solid(48, 97, 16, 144, 99, 80, "show2/multiply", flags=("detail",))
    S.sheet("Multiply material", "show2/multiply: blendMode multiply", "A dark stain band in front of the wall: the wall shows through it, darker.")


def st_unlit(S):
    S.solid(64, 64, 0, 128, 128, 96, "show2/unlit")
    S.sheet("Unlit material", "show2/unlit: unlit true", "A checker block with the same brightness on every side, also on the shaded side.")


def st_emissive(S):
    S.solid(64, 64, 0, 128, 128, 96, "show2/emissive")
    S.sheet("Emissive material", "show2/emissive: emission 8", "A block that glows green.")


def st_tint(S):
    S.solid(24, 64, 0, 88, 128, 96, "show2/solid")
    S.solid(104, 64, 0, 168, 128, 96, "show2/tint")
    S.sheet("Tinted material", "show2/tint: the checker image with tint [1, 0.35, 0.35]", "Left: the plain checker. Right: the same checker, red.")


def st_layer(S):
    S.solid(48, 100, 0, 144, 116, 96, WALL)
    S.g.world.append(mesh([[(S.x0 + 64 + c * 32, S.y0 + 100, 16 + r * 32) for r in range(3)] for c in range(3)], "show2/decal"))
    S.sheet("Decal layer on a surface", "show2/decal on a flat patch that lies exactly on the wall: blend true, sortKey 12, polygonOffset 1",
            "A round orange mark on the south face of the wall, with no flicker.")


def st_tool_mats(S):
    for i, m in enumerate(["caulk", "nodraw", "hint", "skip"]):
        S.mark(12 + i * 44, 76, 52 + i * 44, 116)
        S.solid(16 + i * 44, 80, 1, 48 + i * 44, 112, 72, m, flags=("detail",))
    S.sheet("Tool materials", "caulk, nodraw, hint, skip (west to east, one over each bright plate)",
            "All invisible. caulk and nodraw are solid; hint and skip are walk-through.", SEEN)


def st_static(S):
    S.model("misc_model", BARREL, 48, 110)
    S.model("misc_model", TABLE, 100, 96)
    S.model("misc_model", CHAIR, 150, 110, 0, [("angles", "0 200 0")])
    S.sheet("Stock static models", "misc_model: model, origin, angles", "A barrel, a table and a chair. They block you and bullets.", SEEN)


def st_scaled(S):
    for i, sc in enumerate(["0.5", "1", "1.5"]):
        S.model("misc_model", BARREL, 40 + i * 56, 100, 0, [("modelscale", sc)])
    S.sheet("Scaled models", "misc_model: modelscale 0.5, 1, 1.5", "Three barrels: half size, normal, one and a half.")


def st_shape(S):
    S.model("misc_model", "iw8r_shape_cube_show2", 56, 100)
    S.model("misc_model", "iw8r_shape_cube_show2", 140, 100, 0, [("modelscale", ".5"), ("iw8r_material", "show2/solid")])
    S.sheet("Shape models", "misc_model: model iw8r_shape_cube_show2 (models folder); right: modelscale .5 + iw8r_material show2/solid",
            "A 64 unit plaster cube you can stand on, and a half-size cube wearing the checker.")


def st_custom_model(S):
    S.model("misc_model", "show2_crate", 60, 100)
    S.model("misc_model", "show2_crate", 136, 100, 0, [("angles", "0 30 0")])
    S.sheet("Custom model (OBJ)", "misc_model: model show2_crate (models/show2_crate.obj)", "Two checker crates, one turned 30 degrees.", SEEN)


def st_model_material(S):
    S.model("misc_model", BARREL, 40, 100)
    S.model("misc_model", BARREL, 96, 100, 0, [("iw8r_material", FLOOR)])
    S.model("misc_model", BARREL, 152, 100, 0, [("iw8r_material", "show2/solid")])
    S.sheet("Material on a model", "misc_model: iw8r_material = a stock material (middle) or a custom one (right)",
            "Blue barrel, stone barrel, checker barrel.")


def st_model_slot(S):
    S.model("misc_model", CINDER, 52, 100)
    S.model("misc_model", CINDER, 140, 100, 0, [("iw8r_mat:tmo/cinderblock_01", "show2/solid")])
    S.sheet("One material slot of a model replaced", "misc_model: iw8r_mat:tmo/cinderblock_01 = show2/solid",
            "Two pallets of cinder blocks. On the right one only the blocks wear the checker; pallet and wood stay.")


def st_dynamic(S):
    # The misc_model is also needed: the build only fetches a stock model that a misc_model or script_model uses.
    S.model("misc_model", CONE, 96, 150)
    S.model("dynamic_prop", CONE, 48, 90)
    S.model("dynamic_prop", CONE, 96, 90, 0, [("mass", "2"), ("material", "rubber")])
    S.model("dynamic_prop", CONE, 144, 90, 0, [("mass", "40"), ("material", "metal")])
    S.sheet("Dynamic props", "dynamic_prop: model, mass, material (default; mass 2 rubber; mass 40 metal). North cone: the same model as a misc_model",
            "The three cones in the row fall over and slide when shot or pushed; the right one is heavy. The cone behind them stands still.", SEEN)


def st_live(S):
    S.model("script_model", BARREL, 56, 100, 0, [("targetname", S.name("solid_barrel"))])
    S.ent("script_origin", [("targetname", S.name("live_owner")), ("target", S.name("solid_barrel")), ("origin", S.pos(56, 60, 8))])
    S.model("script_model", BARREL, 136, 100, 0, [("targetname", S.name("ghost")), ("iw8r_no_collision", "1")])
    S.ent("script_origin", [("targetname", S.name("ghost_owner")), ("target", S.name("ghost")), ("origin", S.pos(136, 60, 8))])
    S.sheet("Live script models", "script_model: targetname that a script_origin targets (left); iw8r_no_collision 1 (right)",
            "Left barrel is solid. You walk through the right barrel; bullets still hit it.")


def st_model_flags(S):
    S.model("misc_model", CHAIR, 40, 100, 0, [("lod_scale", "4")])
    S.model("misc_model", CHAIR, 96, 100, 0, [("no_shadow", "1")])
    S.model("misc_model", CHAIR, 152, 100, 0, [("ground_lighting", "0.8 0.2 0.2")])
    S.sheet("Static model flags", "misc_model: lod_scale 4; no_shadow 1; ground_lighting 0.8 0.2 0.2",
            "Three chairs. The left keeps its detail from far away, the middle casts no shadow from lights, the right is lit reddish.", SEEN)


def st_anim(S):
    S.model("misc_model", TABLE, 96, 100)
    S.model("script_model", FAN, 96, 100, 31, [("targetname", S.name("fan")), ("iw8r_anim", "desktop_fan_fn_01_loop"), ("iw8r_anim_loop", "2")])
    S.ent("script_origin", [("targetname", S.name("fan_owner")), ("target", S.name("fan")), ("origin", S.pos(96, 60, 8))])
    S.sheet("Animated model", "script_model: iw8r_anim desktop_fan_fn_01_loop, iw8r_anim_loop 2; a script_origin targets it",
            "A desk fan on the table. Its blades turn.")


def st_foliage(S):
    S.model("misc_model", BUSH, 96, 100, 0, [("modelscale", "0.6")])
    S.sheet("Foliage model (cut-out leaves)", "misc_model: a stock bush with alpha-tested leaf materials, modelscale 0.6",
            "A bush with separate leaves, not flat cards.")


def st_rotation(S):
    for i, a in enumerate(["0 45 0", "30 0 0", "0 0 30"]):
        S.model("misc_model", CHAIR, 40 + i * 56, 100, 8 if i else 0, [("angles", a)])
    S.sheet("Model angles", "misc_model: angles 0 45 0 (yaw), 30 0 0 (pitch), 0 0 30 (roll)",
            "Three chairs: turned, tipped forward, tipped sideways. Compare with the editor camera.")


def st_spin(S):
    S.model("misc_model", BARREL, 56, 100, 0, [("targetname", S.name("spin_yaw")), ("iw8r_spin", "4")])
    S.model("script_model", "show2_crate", 136, 100, 40, [("targetname", S.name("spin_roll")), ("iw8r_spin", "2 roll")])
    S.ent("script_origin", [("targetname", S.name("spin_roll_owner")), ("target", S.name("spin_roll")), ("origin", S.pos(136, 60, 8))])
    S.sheet("Spinners", "misc_model: iw8r_spin 4 (left); script_model: iw8r_spin '2 roll' (right)",
            "The barrel turns once every 4 seconds. The floating crate rolls once every 2 seconds.", SEEN)


def st_bob(S):
    S.model("script_model", BARREL, 96, 100, 0, [("targetname", S.name("bob_barrel")), ("iw8r_bob", "24 2")])
    S.ent("script_origin", [("targetname", S.name("bob_owner")), ("target", S.name("bob_barrel")), ("origin", S.pos(96, 60, 8))])
    S.sheet("Bobber", "script_model: iw8r_bob 24 2", "The barrel moves up 24 units and back every 2 seconds.", SEEN)


def st_brush_movers(S):
    S.ent("script_brushmodel", [("targetname", S.name("spin_brush")), ("iw8r_spin", "6")], [S.box(32, 92, 16, 80, 108, 64, "show2/solid")])
    S.ent("script_brushmodel", [("targetname", S.name("bob_brush")), ("iw8r_bob", "24 3")], [S.box(112, 76, 16, 160, 124, 32, METAL)])
    S.sheet("Moving brush models", "script_brushmodel: iw8r_spin 6 (left), iw8r_bob 24 3 (right); drawn by a generated model",
            "A checker slab that turns and a metal plate that moves up and down. Both are visible and solid.")


def st_lift(S):
    S.ent("script_brushmodel", [("targetname", S.name("lift_plate")), ("iw8r_lift", "128 2 2")], [S.box(48, 48, 0, 144, 144, 8, METAL)])
    S.solid(48, 152, 0, 144, 184, 136, WALL)
    S.sheet("Lift", "script_brushmodel: iw8r_lift 128 2 2", "Stand on the metal plate: it carries you up 128 units; step off onto the ledge.")


def door_walls(S):
    S.solid(16, 92, 0, 64, 100, 112, WALL)
    S.solid(128, 92, 0, 176, 100, 112, WALL)
    return [S.box(64, 94, 0, 128, 98, 96, WOOD)]


def st_door_slide(S):
    S.ent("script_brushmodel", [("targetname", S.name("door")), ("iw8r_door", "slide 0 0 96 1.5 3")], door_walls(S))
    S.ent("trigger_use", [("targetname", S.name("door_trig")), ("target", S.name("door"))], [S.trig(48, 48, 0, 144, 144, 80)])
    S.sheet("Sliding door", "script_brushmodel: iw8r_door 'slide 0 0 96 1.5 3'; a trigger_use with target = the door",
            "Hold Use at the wooden door: it slides up, waits 3 seconds and comes down. It is visible, and it blocks you when shut.")


def st_door_rotate(S):
    S.ent("script_brushmodel", [("targetname", S.name("door")), ("iw8r_door", "rotate 90 1 3")], door_walls(S))
    S.ent("trigger_multiple", [("targetname", S.name("door_trig")), ("target", S.name("door"))], [S.trig(64, 40, 0, 128, 92, 80)])
    S.sheet("Rotating door (closes by itself)", "script_brushmodel: iw8r_door 'rotate 90 1 3'; a trigger_multiple with target = the door",
            "Walk up to the door from the south: it swings open away from you and shuts after 3 seconds.")


def st_door_hinge(S):
    S.ent("script_brushmodel", [("targetname", S.name("door")), ("iw8r_door", "hinge 100 0.6")], door_walls(S))
    S.sheet("Hinged door", "script_brushmodel: iw8r_door 'hinge 100 0.6' (Make Hinged Door); no trigger; iw8r_hinge moves the hinge",
            "Press Use near the door: it swings away from you and stays open. Use again shuts it. Say if you can walk through.")


def st_teleport(S):
    S.mark(24, 80, 56, 112)
    S.ent("trigger_multiple", [("targetname", S.name("tp_in")), ("iw8r_teleport", S.name("tp_out"))], [S.trig(24, 80, 0, 56, 112, 64)])
    S.ent("script_origin", [("targetname", S.name("tp_out")), ("origin", S.pos(152, 152, 8)), ("angles", "0 225 0")])
    S.sheet("Teleporter pair", "trigger_multiple: iw8r_teleport = targetname of a script_origin (the exit)",
            "Step on the bright plate: you are moved to the north-east corner of the pad and turned to face back.", SEEN)


def st_jump(S):
    S.mark(80, 80, 112, 112)
    S.ent("trigger_multiple", [("targetname", S.name("jump_pad")), ("iw8r_jump", "0 0 700")], [S.trig(80, 80, 0, 112, 112, 24)])
    S.sheet("Jump pad", "trigger_multiple: iw8r_jump 0 0 700", "Step on the bright plate: you are thrown straight up.", SEEN)


def st_script(S):
    S.model("script_model", "show2_crate", 96, 100, 0, [("targetname", S.name("blink"))])
    S.ent("script_origin", [("targetname", S.name("blink_owner")), ("target", S.name("blink")), ("origin", S.pos(96, 60, 8))])
    S.g.script.append('    level thread show2_blink("%s");' % S.name("blink"))
    S.sheet("Map script (your own GSC)", "worldspawn iw8r_script mp_iw8r_show2.gsc: show2_blink() hides and shows a script_model",
            "The checker crate vanishes for 1 second every 3 seconds.")


def st_hud(S):
    S.mark(64, 64, 128, 128)
    S.ent("trigger_use", [("targetname", S.name("hud_trig"))], [S.trig(64, 64, 0, 128, 128, 72)])
    S.g.script.append('    level thread show2_hud("%s", "HUD message from the map script", 3);' % S.name("hud_trig"))
    S.sheet("HUD message", "trigger_use + the Script dock's HUD message code (newhudelem, settext) in the map script",
            "Stand on the plate and press Use: a line of text shows in the middle of the screen for 3 seconds.")


def st_vision(S):
    S.mark(32, 48, 160, 160)
    S.ent("trigger_multiple", [("targetname", S.name("vision_box")), ("iw8r_vision", "tonemapExposure=2.5")], [S.trig(32, 48, 0, 160, 160, 96)])
    S.sheet("Vision volume", "trigger_multiple: iw8r_vision tonemapExposure=2.5", "Step on the big plate: the picture gets much brighter. Step off: back to normal.")


def st_hurt(S):
    S.mark(64, 64, 128, 128)
    S.ent("trigger_hurt", [("targetname", S.name("hurt_trig")), ("dmg", "10"), ("spawnflags", "16"), FMG], [S.trig(64, 64, 0, 128, 128, 64)])
    S.sheet("Hurt trigger (fix queue F.1)", "trigger_hurt: dmg 10, spawnflags 16 (once a second)",
            "Stand on the plate: you lose 10 health every second. In the last playtest it fired but did no damage.")


def st_damage(S):
    S.mark(64, 64, 128, 128)
    S.ent("trigger_multiple", [("targetname", S.name("acid")), ("iw8r_damage", "10 1")], [S.trig(64, 64, 0, 128, 128, 64)])
    S.sheet("Damage zone (fix queue F.1)", "trigger_multiple: iw8r_damage 10 1",
            "Stand on the plate: you lose 10 health every second. In the last playtest it fired but did no damage.")


def st_use(S):
    S.mark(64, 64, 128, 128)
    S.ent("trigger_use", [("targetname", S.name("use_trig")), FMG], [S.trig(64, 64, 0, 128, 128, 72)])
    S.say(S.name("use_trig"), "trigger_use fired")
    S.sheet("Use trigger", "trigger_use (Primitives > Triggers > Use) + a message thread in the map script",
            "Stand on the plate: nothing. Press Use: the line 'trigger_use fired' shows.")


def st_touch(S):
    S.mark(64, 64, 128, 128)
    S.ent("trigger_multiple", [("targetname", S.name("touch_trig")), FMG], [S.trig(64, 64, 0, 128, 128, 72)])
    S.say(S.name("touch_trig"), "trigger_multiple fired")
    S.sheet("Touch trigger", "trigger_multiple (Primitives > Triggers > Multiple) + a message thread", "Step on the plate: 'trigger_multiple fired' shows.")


def st_radius(S):
    S.mark(72, 72, 120, 120)
    S.ent("trigger_radius", [("targetname", S.name("radius_trig")), ("origin", S.pos(96, 96, 0)), ("radius", "48"), ("height", "72"), FMG])
    S.say(S.name("radius_trig"), "trigger_radius fired")
    S.sheet("Radius trigger", "trigger_radius: origin, radius 48, height 72 + a message thread", "Walk to the middle of the pad: 'trigger_radius fired' shows.")


def st_trigger_classes(S):
    S.mark(24, 72, 72, 120)
    S.ent("trigger_use_touch", [("targetname", S.name("use_touch")), FMG], [S.trig(24, 72, 0, 72, 120, 72)])
    S.say(S.name("use_touch"), "trigger_use_touch fired")
    S.mark(120, 72, 168, 120)
    S.ent("trigger_damage", [("targetname", S.name("damage")), ("dmg", "5"), ("spawnflags", "16"), FMG], [S.trig(120, 72, 0, 168, 120, 72)])
    S.say(S.name("damage"), "trigger_damage fired")
    S.sheet("Other trigger classes", "trigger_use_touch (left); trigger_damage dmg 5 spawnflags 16 (right)",
            "Left plate: press Use for 'trigger_use_touch fired'. Right plate: 'trigger_damage fired' and 5 damage a second.")


FIRE = "vfx/iw8/veh/scriptables/shared/vfx_veh_fire_linger_med"


def st_fx_loop(S):
    S.ent("fx_origin", [("targetname", S.name("fire")), ("fx", FIRE), ("origin", S.pos(56, 100, 4))])
    S.ent("fx_origin", [("targetname", S.name("smoke")), ("fx", "vfx/iw8/veh/scriptables/vfx_smoke_light_ch"), ("origin", S.pos(140, 100, 4))])
    S.sheet("Stock effects that keep running", "fx_origin: fx = a stock vfx name, no delay", "A small fire on the left, light smoke on the right.", SEEN)


def st_fx_timed(S):
    S.ent("fx_origin", [("targetname", S.name("boom")), ("fx", "vfx/iw8/veh/scriptables/vfx_veh_explosion_civ"), ("delay", "6"), ("origin", S.pos(56, 100, 8))])
    S.ent("fx_origin", [("targetname", S.name("sparks")), ("fx", "vfx/core/expl/spark_death"), ("delay", "2"), ("origin", S.pos(140, 100, 8))])
    S.sheet("Stock effects on a timer", "fx_origin: delay 6 (explosion), delay 2 (sparks)", "An explosion every 6 seconds, sparks every 2 seconds.", SEEN)


def st_fx_sound(S):
    S.ent("fx_origin", [("targetname", S.name("hot_fire")), ("fx", FIRE), ("origin", S.pos(96, 100, 4)), ("soundalias", "show2_hum"),
                        ("iw8r_damage", "10 48 0.5")])
    S.sheet("Effect with sound and damage (fix queue F.6)", "fx_origin: soundalias show2_hum (the map's own loop), iw8r_damage '10 48 0.5'",
            "A fire that hums and hurts within 48 units (10 damage twice a second). The sound was not heard in the last playtest.")


def st_fx_authored(S):
    S.ent("fx_origin", [("targetname", S.name("sparkle")), ("fx", "show2/sparkle"), ("origin", S.pos(96, 100, 8))])
    S.sheet("Authored effect", "fx_origin: fx show2/sparkle (fx/show2/sparkle.iw8fx.json, made for Tools > Effect Editor)",
            "A fountain of orange sparks with a little grey smoke, replayed about once a second.")


def st_snd_loop(S):
    S.ent("sound_emitter", [("targetname", S.name("loop")), ("soundalias", "emt_smokestack_lp"), ("loop", "1"), ("origin", S.pos(96, 100, 32))])
    S.sheet("Stock sound, looping", "sound_emitter: soundalias emt_smokestack_lp, loop 1", "A smokestack rumble that gets louder as you walk in.", SEEN)


def st_snd_shot(S):
    S.ent("sound_emitter", [("targetname", S.name("oneshot")), ("soundalias", "emt_rock_med_debris_short"), ("loop", "0"), ("delay", "4"),
                            ("origin", S.pos(96, 100, 32))])
    S.sheet("Stock sound, repeating one-shot", "sound_emitter: soundalias emt_rock_med_debris_short, loop 0, delay 4", "Falling debris every 4 seconds.", SEEN)


def st_snd_custom(S):
    S.ent("sound_emitter", [("targetname", S.name("hum")), ("soundalias", "show2_hum"), ("loop", "1"), ("origin", S.pos(56, 100, 32))])
    S.ent("sound_emitter", [("targetname", S.name("chirp")), ("soundalias", "show2_chirp"), ("loop", "0"), ("delay", "3"), ("origin", S.pos(140, 100, 32))])
    S.sheet("Your own sounds (fix queue F.6 / F.7)", "sound_emitter: soundalias show2_hum loop 1 (sounds/show2_hum.wav); show2_chirp delay 3",
            "A steady 220 Hz hum on the left, a rising chirp every 3 seconds on the right.")


def st_reverb(S):
    S.ent("trigger_multiple", [("targetname", S.name("reverb_zone")), ("iw8r_audio_zone", "show2_tunnel"), ("iw8r_reverb", "iw8_lg_tunnel"),
                               ("iw8r_audio_priority", "40"), ("iw8r_audio_interior", "1")], [S.trig(8, 8, 0, 184, 184, 160)])
    S.sheet("Reverb zone (fix queue F.7)", "trigger_multiple: iw8r_audio_zone, iw8r_reverb iw8_lg_tunnel, iw8r_audio_priority 40, iw8r_audio_interior 1",
            "Fire a shot on this pad, then one on the walkway: on the pad it rings like a tunnel, outside it sounds open.")


def st_ambient(S):
    S.ent("trigger_multiple", [("targetname", S.name("ambient_zone")), ("iw8r_audio_zone", "show2_bedzone"), ("iw8r_reverb", "iw8_md_room"),
                               ("iw8r_audio_priority", "50"), ("iw8r_ambient", "show2_bed"), ("iw8r_ambient_oneshots", "3 6 show2_chirp")],
          [S.trig(8, 8, 0, 184, 184, 160)])
    S.sheet("Ambient zone", "trigger_multiple: iw8r_audio_zone, iw8r_ambient show2_bed, iw8r_ambient_oneshots '3 6 show2_chirp'",
            "On this pad only: a low drone, and a chirp every 3 to 6 seconds. Both stop when you leave.")


def st_nodes(S):
    for i in range(3):
        for j in range(3):
            S.ent("node_pathnode", [("origin", S.pos(40 + i * 56, 40 + j * 56, 0))])
    S.sheet("Path nodes", "node_pathnode: origin (nine nodes; fixed class, no targetname)", "With bots: they walk across this pad. The nodes themselves are invisible.", SEEN)


def st_traverse(S):
    S.solid(32, 64, 0, 96, 128, 40, FLOOR)
    S.ent("node_negotiation_begin", [("origin", S.pos(64, 96, 40)), ("target", S.name("drop")), ("animscript", "traverse_jumpdown_40")])
    S.ent("node_negotiation_end", [("origin", S.pos(136, 96, 0)), ("targetname", S.name("drop"))])
    S.sheet("Traversal (jump down)", "node_negotiation_begin: target, animscript traverse_jumpdown_40; node_negotiation_end: targetname",
            "With bots: they jump down from the 40 unit block.", SEEN)


def st_cameras(S):
    tmpl(S, "mp_global_intermission", 96, 96, 288, [("angles", "20 270 0"), FMG])
    tmpl(S, "script_struct", 16, 96, 232, [("_color", "0 .5 0"), ("angles", "10 0 0"), ("target", "camera_intro_path"), ("targetname", "camera_intro")])
    tmpl(S, "script_struct", 176, 96, 264, [("angles", "0 180 0"), ("targetname", "camera_intro_path")])
    S.sheet("Match cameras", "mp_global_intermission: angles; script_struct camera_intro -> camera_intro_path (Primitives > Game Modes > Match Cameras)",
            "At the end of a match the camera hangs 288 units over this pad and looks south over the arena.")


def st_minimap(S):
    S.sheet("Minimap corners", "two script_origin with targetname minimap_corner, at the south-west and north-east arena corners",
            "The minimap shows the whole arena, north up.")


def st_killstreak(S):
    # Kept inside the arena and under the sky: the build's map bounds include every point entity.
    cx, cy = NCOL * CELL / 2, 1400
    S.ent("script_origin", [("origin", "%s %s 440" % (n(cx), n(cy))), ("targetname", "airstrikeheight")])
    S.ent("script_origin", [("origin", "2300 2700 440"), ("targetname", "heli_leave")])
    S.ent("script_struct", [("origin", "2300 100 24"), ("angles", "0 150 0"), ("radius", "16000"), ("targetname", "nuke_expl_pos")])
    for i, (sx, sy) in enumerate(((-1, 1), (1, 1), (1, -1), (-1, -1))):
        heli = [("_color", "1.000000 1.000000 0.000000"), ("angles", "0 270 0"), FMG, ("radius", "256"), ("script_goalyaw", "true")]
        S.ent("script_struct_heli", [("origin", "%s %s 400" % (n(cx + sx * 600), n(cy + sy * 600)))] + heli +
              [("script_linkname", "show2_heli_%d" % i), ("script_linkto", "show2_heli_%d" % ((i + 1) % 4)), ("script_stopnode", "1"), ("targetname", "ks_heli_goal")])
        S.ent("script_struct_heli", [("origin", "%s %s 440" % (n(cx + sx * 1100), n(cy + sy * 1250)))] + heli +
              [("script_linkto", "show2_heli_%d" % i), ("script_stopnode", "1"), ("targetname", "ks_heli_entrance")])
    S.sheet("Killstreak markers (stock kinds, fix queue F.14)", "script_origin airstrikeheight and heli_leave; script_struct nuke_expl_pos; "
            "script_struct_heli ks_heli_entrance x4 -> ks_heli_goal x4 (script_linkto / script_linkname loop). All inside the arena at z 400 to 440: "
            "stock maps put them far outside, but the build's map bounds include every point entity",
            "Nothing on the pad. Call a helicopter or an air strike and say what flies where: both should stay low over the arena.")


def st_airdrop(S):
    S.ent("script_brushmodel", [("nophysics", "1"), ("script_gameobjectname", "airdrop_pallet"), ("targetname", S.name("pallet"))],
          [S.box(56, 76, 0, 136, 124, 2, WOOD)])
    S.model("script_model", "com_plasticcase_beige_big_iw6", 96, 100, 2, [("target", S.name("pallet")), ("targetname", "airdrop_crate")])
    S.ent("script_origin", [("targetname", S.name("crate_owner")), ("target", "airdrop_crate"), ("origin", S.pos(96, 50, 8))])
    S.sheet("Airdrop crate and pallet (stock kind)", "script_model targetname airdrop_crate, target = a script_brushmodel with script_gameobjectname airdrop_pallet",
            "A beige plastic case on a thin wooden plate. Stock maps keep this pair outside the map; what the game does with it is not known.")


def st_callouts(S):
    S.mark(24, 72, 72, 120)
    S.ent("trigger_multiple", [FMG, ("script_noteworthy", "center"), ("targetname", "callout_area")], [S.trig(24, 72, 0, 72, 120, 96)])
    S.mark(120, 72, 168, 120)
    tmpl_brush(S, "trigger_multiple_bcs_center", 144, 96, 24, 24, 0, 96, [FMG])
    S.sheet("Callout area and battle chatter trigger (stock kinds)", "trigger_multiple targetname callout_area, script_noteworthy center (left); "
            "trigger_multiple_bcs_center (right; all 21 trigger_multiple_bcs_* classes work the same way)",
            "Left plate: the location name under the minimap may read 'center'. Right plate: soldiers may call out 'center' in a match with bots.")


def st_bounds(S):
    for i, name in enumerate(["OutOfBounds", "kill_border_trigger", "dcover_blocker", "white_phosphorus_damage"]):
        lx, ly = 28 + (i % 2) * 96, 108 if i < 2 else 28
        S.mark(lx, ly, lx + 40, ly + 40)
        S.ent("trigger_multiple", [FMG, ("targetname", name)], [S.trig(lx, ly, 0, lx + 40, ly + 40, 72)])
    S.sheet("Boundary triggers (stock kinds)", "trigger_multiple with targetname OutOfBounds (north-west plate), kill_border_trigger (north-east), "
            "dcover_blocker (south-west), white_phosphorus_damage (south-east)",
            "Step on each plate for a moment and say what happens: an out-of-bounds warning, death, or nothing.")


def st_dom(S):
    for i, lab in enumerate(["_a", "_b", "_c"]):
        S.mark(116 + i * 256, 84, 140 + i * 256, 108)
        tmpl(S, "trigger_radius", 128 + i * 256, 96, 32, [FMG, ("height", "128"), ("nophysics", "true"), ("radius", "120"),
                                                              ("script_gameobjectname", "dom onslaught"), ("script_label", lab), ("targetname", "flag_primary")])
    spawn_set(S, "mp_dom_spawn", "mp_dom_spawn_allies_start", "mp_dom_spawn_axis_start", 24, S.w * CELL - 24, 176)
    S.sheet("Domination: flags A, B, C and the spawn set", "trigger_radius x3: script_gameobjectname 'dom onslaught', script_label _a/_b/_c, targetname "
            "flag_primary, radius 120, height 128, nophysics true; mp_dom_spawn x2, mp_dom_spawn_allies_start, mp_dom_spawn_axis_start",
            "In Domination: three flags to capture at the small plates. In Team Deathmatch: nothing (the game removes other modes' objects).")


def st_sd(S):
    for label, tag, lx in (("_a", "a", 96), ("_b", "b", 480)):
        crate, defuse, boom = "sd_site%s_crate" % tag, "sd_site%s_defuse" % tag, "sd_site%s_boom" % tag
        tmpl_brush(S, "trigger_use_touch", lx, 96, 80, 80, 0, 96, [FMG, ("script_bombmode_original", "1"), ("script_gameobjectname", "bombzone"),
                                                                    ("script_label", label), ("target", crate), ("targetname", "bombzone")])
        tmpl_brush(S, "trigger_use_touch", lx, 96, 80, 80, 0, 96, [FMG, ("script_gameobjectname", "bombzone"), ("targetname", defuse)])
        tmpl(S, "script_model", lx, 96, 12, [("angles", "0 135 0"), FMG, ("model", "sd_equipment_bombsite_crate"), ("script_exploder", boom),
                                             ("script_gameobjectname", "bombzone"), ("spawnflags", "4"), ("target", defuse), ("targetname", crate)])
        tmpl(S, "script_model", lx, 96, 12, [("angles", "0 135 0"), FMG, ("model", "sd_equipment_bombsite_crate_d"), ("script_exploder", boom),
                                             ("script_gameobjectname", "bombzone"), ("spawnflags", "4"), ("targetname", "exploder")])
    tmpl(S, "script_model", 288, 96, 16, [("angles", "0 45 0"), FMG, ("model", "offhand_wm_briefcase_bomb"), ("script_gameobjectname", "sd"),
                                          ("spawnflags", "4"), ("targetname", "sd_bomb")])
    tmpl_brush(S, "trigger_multiple", 288, 96, 48, 48, 0, 64, [FMG, ("script_gameobjectname", "sd"), ("targetname", "sd_bomb_pickup_trig")])
    tmpl(S, "mp_sd_spawn_attacker", 288, 172, 8, [("angles", "0 0 0"), FMG])
    tmpl(S, "mp_sd_spawn_defender", 288, 28, 8, [("angles", "0 180 0"), FMG])
    S.sheet("Search and Destroy: sites A and B, the bomb, spawns", "per site: trigger_use_touch bombzone (script_label, script_bombmode_original 1, target) -> "
            "script_model crate (script_exploder, target) -> trigger_use_touch defuse; destroyed crate (targetname exploder); script_model sd_bomb; "
            "trigger_multiple sd_bomb_pickup_trig; mp_sd_spawn_attacker, mp_sd_spawn_defender",
            "In Search and Destroy: a crate at each end to plant on, the briefcase bomb in the middle. In Team Deathmatch: nothing.")


def st_ctf(S):
    for side, lx, axis in (("allies", 96, False), ("axis", 288, True)):
        ang = [("angles", "0 180 0")] if axis else []
        tmpl(S, "trigger_radius", lx, 96, 32, [FMG, ("height", "128"), ("radius", "64"), ("script_gameobjectname", "ctf"),
                                               ("target", "ctf_flag_" + side), ("targetname", "ctf_trig_" + side)] + ang)
        tmpl(S, "script_model", lx, 96, 16, [FMG, ("model", "highway_flag0"), ("script_gameobjectname", "ctf"), ("spawnflags", "4"),
                                             ("targetname", "ctf_flag_" + side)] + ang)
        tmpl(S, "trigger_radius", lx, 96, 32, [FMG, ("height", "128"), ("radius", "64"), ("script_gameobjectname", "ctf"),
                                               ("targetname", "ctf_zone_" + side)] + ang)
    spawn_set(S, "mp_ctf_spawn", "mp_ctf_spawn_allies_start", "mp_ctf_spawn_axis_start", 24, S.w * CELL - 24, 176)
    S.sheet("Capture the Flag: both flags, zones, spawn set", "per team: trigger_radius ctf_trig_<team> (target), script_model ctf_flag_<team> (highway_flag0, "
            "spawnflags 4), trigger_radius ctf_zone_<team>; all script_gameobjectname ctf; mp_ctf_spawn x2 and both starts",
            "In Capture the Flag: a flag on each half of the pad. In Team Deathmatch: nothing.")


OTHER_SPAWNS = [
    "mp_tdm_spawn_secondary", "mp_dm_spawn_secondary", "mp_dom_spawn_secondary", "mp_ctf_spawn_secondary", "mp_arena_spawn_allies_start",
    "mp_arena_spawn_axis_start", "mp_ball_spawn", "mp_ball_spawn_allies_start", "mp_ball_spawn_axis_start", "mp_ball_spawn_secondary",
    "mp_cyber_spawn_allies", "mp_cyber_spawn_axis", "mp_dd_spawn_attacker", "mp_dd_spawn_attacker_a", "mp_dd_spawn_attacker_b",
    "mp_dd_spawn_attacker_start", "mp_dd_spawn_defender", "mp_dd_spawn_defender_a", "mp_dd_spawn_defender_b", "mp_dd_spawn_defender_start",
    "mp_front_spawn_allies", "mp_front_spawn_axis", "mp_koth_spawn", "mp_koth_spawn_allies_start", "mp_koth_spawn_axis_start",
    "mp_koth_spawn_secondary", "mp_rugby_spawn_allies", "mp_rugby_spawn_allies_secondary", "mp_rugby_spawn_allies_start", "mp_rugby_spawn_axis",
    "mp_rugby_spawn_axis_secondary", "mp_rugby_spawn_axis_start",
]


def st_other_spawns(S):
    for i, cls in enumerate(OTHER_SPAWNS):
        tmpl(S, cls, 21 + (i % 6) * 33, 24 + (i // 6) * 33, 8, [("angles", "0 270 0"), FMG])
    S.sheet("Spawn classes of the other modes", "one of each: " + ", ".join(OTHER_SPAWNS) + " (origin, angles)",
            "Nothing to see. In a mode that uses one of these classes you can spawn on this pad.")


def st_hq(S):
    S.ent("script_brushmodel", [("script_gameobjectname", "hqloc"), ("spawnflags", "1"), ("targetname", S.name("hq_use")), ("iw8r_brush_visual", "0")],
          [S.box(64, 64, 0, 128, 128, 64, "clip")])
    S.model("script_model", "military_hq_crate_01_proxy", 96, 96, 14, [("angles", "0 345 0"), ("script_gameobjectname", "hqloc"), ("spawnflags", "4"),
                                                                        ("target", S.name("hq_use")), ("targetname", "hqloc")])
    S.sheet("Headquarters radio (stock kind)", "script_model targetname hqloc (military_hq_crate_01_proxy, spawnflags 4, target) -> script_brushmodel; "
            "both script_gameobjectname hqloc",
            "In Headquarters: a radio crate to capture. In Team Deathmatch: nothing.")


def st_grind(S):
    S.mark(84, 84, 108, 108)
    tmpl(S, "trigger_radius", 96, 96, 16, [("angles", "0 270 0"), FMG, ("height", "128"), ("nophysics", "true"), ("radius", "88"),
                                           ("script_gameobjectname", "grind"), ("script_label", "a"), ("targetname", "grind_location")])
    S.sheet("Grind bank (stock kind)", "trigger_radius: targetname grind_location, script_gameobjectname grind, script_label a, nophysics true",
            "In Grind: a place to bank tags. In Team Deathmatch: nothing.")


def st_hardpoint(S):
    for i in range(5):
        tmpl_brush(S, "trigger_multiple", 96 + i * CELL, 96, 96, 96, 0, 128, [FMG, ("script_gameobjectname", "hardpoint"), ("script_label", str(i + 1)),
                                                                                ("targetname", "hardpoint_zone")])
    S.sheet("Hardpoint: five zones", "trigger_multiple x5: script_gameobjectname hardpoint, script_label 1..5, targetname hardpoint_zone "
            "(each zone is one 192 x 192 x 128 cell of this pad)",
            "In Hardpoint: the zone moves along the five cells of this pad, west to east. In Team Deathmatch: nothing.")


def st_cyber(S):
    tmpl(S, "script_model", 192, 150, 20, [("model", "offhand_wm_emp_bomb"), ("modelscale", "2"), ("script_gameobjectname", "cyber"), ("spawnflags", "0"),
                                           ("targetname", "cyber_emp")])
    S.ent("trigger_use", [("script_gameobjectname", "cyber"), ("targetname", "cyber_emp_pickup_trig")], [S.trig(168, 126, 0, 216, 174, 64)])
    for side, lx in (("allies", 96), ("axis", 288)):
        crate = S.name("emp_" + side)
        S.ent("trigger_use_touch", [("script_gameobjectname", "cyber"), ("target", crate), ("targetname", "cyber_empzone_" + side)],
              [S.trig(lx - 48, 32, 0, lx + 48, 112, 96)])
        S.ent("trigger_use_touch", [("script_gameobjectname", "cyber"), ("targetname", "cyber_emp_defuse_" + side)], [S.trig(lx - 48, 32, 0, lx + 48, 112, 96)])
        S.model("script_model", "misc_carepackage_emp", lx, 72, 16, [("script_gameobjectname", "cyber"), ("spawnflags", "4"), ("targetname", crate)])
    S.sheet("Cyber Attack: EMP, pickup, two zones (stock kind)", "script_model cyber_emp (modelscale 2) + trigger_use cyber_emp_pickup_trig; per team: "
            "trigger_use_touch cyber_empzone_<team> (target = crate), trigger_use_touch cyber_emp_defuse_<team>, script_model crate; all script_gameobjectname cyber",
            "In Cyber Attack: an EMP device at the north edge and a crate on each half. In Team Deathmatch: nothing.")


def st_dd(S):
    for label, tag, lx in (("_a", "a", 96), ("_b", "b", 288)):
        crate, defuse = S.name("dd_crate_" + tag), S.name("dd_defuse_" + tag)
        S.ent("trigger_use_touch", [("script_bombmode_original", "1"), ("script_gameobjectname", "dd_bombzone"), ("script_label", label), ("target", crate),
                                    ("targetname", "dd_bombzone")], [S.trig(lx - 64, 32, 0, lx + 64, 160, 96)])
        S.ent("trigger_use_touch", [("script_gameobjectname", "dd_bombzone"), ("targetname", defuse)], [S.trig(lx - 64, 32, 0, lx + 64, 160, 96)])
        S.model("script_model", "sd_equipment_bombsite_crate", lx, 96, 16, [("angles", "0 270 0"), ("script_exploder", S.name("dd_boom_" + tag)),
                                                                            ("script_gameobjectname", "dd_bombzone"), ("target", defuse), ("targetname", crate)])
    S.sheet("Demolition: two bomb sites (stock kind)", "per site: trigger_use_touch targetname dd_bombzone (script_label, script_bombmode_original 1, target) -> "
            "script_model crate (script_exploder, target) -> trigger_use_touch defuse; all script_gameobjectname dd_bombzone",
            "In Demolition: a bomb site crate on each half. In Team Deathmatch: nothing.")


def st_goals(S):
    for name, lx in (("flag_goal_defender", 64), ("flag_arena", 192), ("flag_goal_attacker", 320)):
        S.mark(lx - 12, 84, lx + 12, 108)
        tmpl(S, "trigger_radius", lx, 96, 28, [FMG, ("height", "128"), ("nophysics", "true"), ("radius", "60"), ("targetname", name)])
    S.ent("trigger_multiple", [("script_gameobjectname", "grnd"), ("script_label", "1"), ("targetname", "grnd")], [S.trig(136, 136, 0, 248, 184, 96)])
    S.sheet("Flag goals and a ground zone (stock kinds)", "trigger_radius targetname flag_goal_defender, flag_arena, flag_goal_attacker (radius, height 128, "
            "nophysics true); trigger_multiple script_gameobjectname grnd, script_label 1, targetname grnd",
            "Used by the arena and drop-zone style modes; which one reads them is not known. In Team Deathmatch: nothing.")


# group, slug, sign title, cells, builder
STATIONS = [
    ("world", "world", "SUN SKY FOG FILM GRADE", 1, st_world),
    ("world", "spawns", "SPAWNS TDM + DM", 1, st_spawns),
    ("lighting", "omni", "OMNI LIGHT", 1, st_omni),
    ("lighting", "omni_colour", "OMNI RED + BLUE", 1, st_omni_colour),
    ("lighting", "spot", "SPOT BY ANGLES", 1, st_spot),
    ("lighting", "spot_target", "SPOT WITH TARGET", 1, st_spot_target),
    ("lighting", "spot_ies", "SPOT PLAIN / IES PROFILE", 1, st_spot_ies),
    ("lighting", "spot_shadow", "SPOT SHADOW CASTER", 1, st_spot_shadow),
    ("lighting", "light_keys", "LIGHT TUNING KEYS", 1, st_light_keys),
    ("lighting", "spot_wide", "WIDE SPOT 140 / 160", 1, st_spot_wide),
    ("lighting", "probes", "REFLECTION PROBE PAIR", 1, st_probes),
    ("lighting", "volume", "VOLUMETRIC BOX", 1, st_volume),
    ("brushes", "detail", "STRUCTURAL / DETAIL", 1, st_detail),
    ("brushes", "flags", "NON-COLLIDING / WEAPON CLIP", 1, st_flags),
    ("brushes", "clip", "CLIP PLAYER WEAPON NOSIGHT", 1, st_clip),
    ("brushes", "slopes", "RAMP + STAIRS", 1, st_slopes),
    ("brushes", "patch_curve", "PATCH CURVED BOTH SIDES", 1, st_patch_curve),
    ("brushes", "patch_terrain", "PATCH TERRAIN", 1, st_patch_terrain),
    ("brushes", "glass", "GLASS PANE", 1, st_glass),
    ("brushes", "ladder", "LADDER", 1, st_ladder),
    ("brushes", "mantle", "MANTLE 40 / 56 / 72", 1, st_mantle),
    ("brushes", "water", "WATER VOLUME", 1, st_water),
    ("brushes", "prefabs", "PREFABS", 1, st_prefabs),
    ("brushes", "decal", "DECAL (WAITING)", 1, st_decal),
    ("materials", "stock_mat", "STOCK MATERIALS", 1, st_stock_mat),
    ("materials", "stock_cutout", "STOCK CUT-OUT", 1, st_stock_cutout),
    ("materials", "custom", "CUSTOM MATERIAL", 1, st_custom),
    ("materials", "cutout", "CUSTOM ALPHA TEST", 1, st_cutout),
    ("materials", "blend", "ALPHA BLEND", 1, st_blend),
    ("materials", "add", "ADDITIVE", 1, st_add),
    ("materials", "multiply", "MULTIPLY", 1, st_multiply),
    ("materials", "unlit", "UNLIT", 1, st_unlit),
    ("materials", "emissive", "EMISSIVE", 1, st_emissive),
    ("materials", "tint", "PLAIN / TINTED", 1, st_tint),
    ("materials", "layer", "DECAL LAYER ON A WALL", 1, st_layer),
    ("materials", "tool_mats", "CAULK NODRAW HINT SKIP", 1, st_tool_mats),
    ("models", "static", "STOCK STATIC MODELS", 1, st_static),
    ("models", "scaled", "SCALE 0.5 / 1 / 1.5", 1, st_scaled),
    ("models", "shape", "SHAPE MODELS", 1, st_shape),
    ("models", "custom_model", "CUSTOM OBJ MODEL", 1, st_custom_model),
    ("models", "model_material", "MATERIAL ON A MODEL", 1, st_model_material),
    ("models", "model_slot", "ONE SLOT REPLACED", 1, st_model_slot),
    ("models", "dynamic", "DYNAMIC PROPS", 1, st_dynamic),
    ("models", "live", "LIVE SOLID / NO COLLISION", 1, st_live),
    ("models", "model_flags", "LOD / NO SHADOW / GROUND LIT", 1, st_model_flags),
    ("models", "anim", "ANIMATED FAN", 1, st_anim),
    ("models", "foliage", "FOLIAGE", 1, st_foliage),
    ("models", "rotation", "YAW / PITCH / ROLL", 1, st_rotation),
    ("scripted", "spin", "SPINNER YAW / ROLL", 1, st_spin),
    ("scripted", "bob", "BOBBER", 1, st_bob),
    ("scripted", "brush_movers", "BRUSH SPIN / BOB", 1, st_brush_movers),
    ("scripted", "lift", "LIFT", 1, st_lift),
    ("scripted", "door_slide", "SLIDING DOOR (USE)", 1, st_door_slide),
    ("scripted", "door_rotate", "ROTATING DOOR (TOUCH)", 1, st_door_rotate),
    ("scripted", "door_hinge", "HINGED DOOR (USE)", 1, st_door_hinge),
    ("scripted", "teleport", "TELEPORTER", 1, st_teleport),
    ("scripted", "jump", "JUMP PAD", 1, st_jump),
    ("scripted", "script", "MAP SCRIPT BLINK", 1, st_script),
    ("scripted", "hud", "HUD MESSAGE (USE)", 1, st_hud),
    ("scripted", "vision", "VISION VOLUME", 1, st_vision),
    ("triggers", "hurt", "HURT TRIGGER", 1, st_hurt),
    ("triggers", "damage", "DAMAGE ZONE", 1, st_damage),
    ("triggers", "use", "USE TRIGGER", 1, st_use),
    ("triggers", "touch", "TOUCH TRIGGER", 1, st_touch),
    ("triggers", "radius", "RADIUS TRIGGER", 1, st_radius),
    ("triggers", "trigger_classes", "USE TOUCH / DAMAGE", 1, st_trigger_classes),
    ("fxsound", "fx_loop", "STOCK FX FIRE + SMOKE", 1, st_fx_loop),
    ("fxsound", "fx_timed", "STOCK FX ON A TIMER", 1, st_fx_timed),
    ("fxsound", "fx_sound", "FX + SOUND + DAMAGE", 1, st_fx_sound),
    ("fxsound", "fx_authored", "AUTHORED EFFECT", 1, st_fx_authored),
    ("fxsound", "snd_loop", "STOCK SOUND LOOP", 1, st_snd_loop),
    ("fxsound", "snd_shot", "STOCK SOUND EVERY 4 S", 1, st_snd_shot),
    ("fxsound", "snd_custom", "OWN SOUNDS HUM + CHIRP", 1, st_snd_custom),
    ("fxsound", "reverb", "REVERB ZONE", 1, st_reverb),
    ("fxsound", "ambient", "AMBIENT ZONE", 1, st_ambient),
    ("nav", "nodes", "PATH NODES", 1, st_nodes),
    ("nav", "traverse", "TRAVERSAL JUMP DOWN", 1, st_traverse),
    ("nav", "cameras", "MATCH CAMERAS", 1, st_cameras),
    ("nav", "minimap", "MINIMAP CORNERS", 1, st_minimap),
    ("stock", "killstreak", "KILLSTREAK MARKERS", 1, st_killstreak),
    ("stock", "airdrop", "AIRDROP CRATE", 1, st_airdrop),
    ("stock", "callouts", "CALLOUT / BATTLE CHATTER", 1, st_callouts),
    ("stock", "bounds", "BOUNDARY TRIGGERS", 1, st_bounds),
    ("modes", "dom", "DOMINATION FLAGS A B C", 4, st_dom),
    ("modes", "sd", "SEARCH AND DESTROY", 3, st_sd),
    ("modes", "ctf", "CAPTURE THE FLAG", 2, st_ctf),
    ("modes", "other_spawns", "OTHER MODE SPAWNS", 1, st_other_spawns),
    ("modes", "hq", "HEADQUARTERS RADIO", 1, st_hq),
    ("modes", "grind", "GRIND BANK", 1, st_grind),
    ("modes", "hardpoint", "HARDPOINT ZONES 1-5", 5, st_hardpoint),
    ("modes", "cyber", "CYBER ATTACK", 2, st_cyber),
    ("modes", "dd", "DEMOLITION SITES", 2, st_dd),
    ("modes", "goals", "FLAG GOALS + GROUND ZONE", 2, st_goals),
]

NOT_PLACED = [
    ("Stock clip brush models (clip*, player*, mantle*, mount*, ladder*, nosight*, stairs*, tactical_*_col, smoke_grenade_sight_clip_*, care_package_col)",
     "stock maps keep these as collision templates about 9,000 units outside the playable area; a brush that far away would stretch the map bounds, "
     "the light grid and the minimap"),
    ("Helicopter height mesh (script_brushmodel heli_height_mesh / heli_pilot_mesh)", "a brush 2,400 units up would stretch the map bounds the same way"),
    ("Tactical ladder marker (script_struct, model misc_vm_tactical_ladder)", "it marks one of the far-away clip templates above"),
    ("Search and Destroy / Demolition / Cyber site body brushes and dd_bombzone_clip_*", "their role is unknown and the editor's own templates leave them out"),
    ("Rugby end zones and juggernaut crate, infil structs and doors, export_volume, collmap_scriptable, node_3d_pathnode",
     "the build does not accept these classes (buildAccepted is false)"),
    ("Weapon pickup and drop-zone flare structs (script_struct weapon_pickup / dz_flare with a weapon model)",
     "the build check stops on a model name that is not in the build, and weapon models are not built as copies"),
    ("triggermodeldummydefault", "the build accepts the class, but in stock data it is only a physics asset name, never an entity"),
    ("pf*_auto* chain structs and delete_on_load structs", "generated link names and stock clean-up markers; not something a mapper places"),
    ("func_group", "editor-only grouping; nothing reaches the game"),
    ("Mantle key or brush", "the editor has none: any ledge can be mantled (station MANTLE)"),
    ("Killstreak templates", "the editor has no templates for them; the stock markers are on the KILLSTREAK MARKERS station instead"),
]

GSC_HEAD = """// IW8 Radiant dev showcase 2: map script (worldspawn iw8r_script). Written by tools/make_dev_showcase2.py.
// Station messages use the Script dock's "Trigger message" code with iw8r_trigger() in place of getent(): the build's
// own trigger test notifies that object (the game's trigger entities did not fire in built maps, fix queue F.1).
user_main()
{
    level endon("game_ended");
    wait 1;
"""

GSC_TAIL = """}

// Prints `text` when a player walks into the station trigger `name`, once per visit.
show2_station(name, text)
{
    level endon("game_ended");
    trig = iw8r_trigger(name);
    if (!isdefined(trig))
        return;
    for (;;)
    {
        trig waittill("trigger", player);
        if (!isdefined(player) || !isplayer(player))
            continue;
        now = gettime();
        if (!isdefined(player.show2_name) || player.show2_name != name || now - player.show2_time > 1500)
            player iprintlnbold(text);
        player.show2_name = name;
        player.show2_time = now;
    }
}

// Prints `text` each time the trigger `name` fires (at most every 2 seconds).
show2_message(name, text)
{
    level endon("game_ended");
    trig = iw8r_trigger(name);
    if (!isdefined(trig))
        return;
    for (;;)
    {
        trig waittill("trigger", player);
        if (isdefined(player) && isplayer(player))
            player iprintlnbold(text);
        wait 2;
    }
}

// The Script dock's "HUD message" code, started by a trigger.
show2_hud(name, text, seconds)
{
    level endon("game_ended");
    trig = iw8r_trigger(name);
    if (!isdefined(trig))
        return;
    for (;;)
    {
        trig waittill("trigger", player);
        msg = newhudelem();
        msg.x = 0;
        msg.y = -60;
        msg.alignx = "center";
        msg.aligny = "middle";
        msg.horzalign = "center";
        msg.vertalign = "middle";
        msg.fontscale = 1.5;
        msg.alpha = 1;
        msg settext(text);
        wait seconds;
        msg destroy();
        wait 1;
    }
}

// Hides the model for 1 second every 3 seconds.
show2_blink(name)
{
    level endon("game_ended");
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


def build():
    g = Gen()
    row = col = 0
    for i, (group, slug, title, w, fn) in enumerate(STATIONS):
        if col + w > NCOL:
            row, col = row + 1, 0
        S = St(g, i + 1, group, slug, title, w, row, col)
        col += w
        g.stations.append(S)
    nrows = row + 1
    ytop = STRIP + nrows * ROWP - WALK + 32
    x_lo, x_hi = -MARGIN, NCOL * CELL + MARGIN

    # Shell: a sealing slab under everything, walls, sky. The south and west walls are low (the sun stands south-west), with
    # sky brushes above them, so their shadow stays off the pads.
    g.world.append(box(x_lo - 16, -16, -32, x_hi + 16, ytop + 16, -16, FLOOR))
    for (x0, y0, x1, y1, h) in ((x_lo - 16, -16, x_hi + 16, 0, 128), (x_lo - 16, 0, x_lo, ytop, 128),
                                (x_lo - 16, ytop, x_hi + 16, ytop + 16, 256), (x_hi, 0, x_hi + 16, ytop, 256)):
        g.world.append(box(x0, y0, -16, x1, y1, h, WALL))
        g.world.append(box(x0, y0, h, x1, y1, 448, "sky"))
    g.world.append(box(x_lo - 16, -16, 448, x_hi + 16, ytop + 16, 464, "sky"))
    # Floor: the south strip, the side margins, a walkway north of every row, and one coloured pad per station.
    g.world.append(box(x_lo, 0, -16, x_hi, STRIP, 0, FLOOR, flags=("detail",)))
    g.world.append(box(x_lo, STRIP, -16, 0, ytop, 0, FLOOR, flags=("detail",)))
    g.world.append(box(NCOL * CELL, STRIP, -16, x_hi, ytop, 0, FLOOR, flags=("detail",)))
    used = {}
    for S in g.stations:
        used[S.row] = max(used.get(S.row, 0), S.col + S.w)
    for r in range(nrows):
        y0 = STRIP + r * ROWP
        g.world.append(box(0, y0 + CELL, -16, NCOL * CELL, min(y0 + ROWP, ytop), 0, FLOOR, flags=("detail",)))
        if used[r] < NCOL:
            g.world.append(box(used[r] * CELL, y0, -16, NCOL * CELL, y0 + CELL, 0, FLOOR, flags=("detail",)))
        g.world.append(box(used[r] * CELL - 4, y0, 0, used[r] * CELL, y0 + 4, SIGN_Z0, BOARD, flags=("detail",)))  # the row's last sign post
    # A roof over the lighting row (row 0), so the sun does not wash the lights out.
    g.world.append(box(224, STRIP - 176, 224, x_hi, STRIP + CELL, 240, WALL, flags=("detail",)))

    # Team Deathmatch set on the south strip, as the editor's spawn set template writes it (all facing the stations).
    cx = NCOL * CELL // 2
    g.ent("mp_tdm_spawn", [("origin", "%d 96 8" % (cx - 288)), ("angles", "0 90 0"), FMG])
    g.ent("mp_tdm_spawn", [("origin", "%d 96 8" % (cx + 288)), ("angles", "0 90 0"), FMG])
    g.ent("mp_tdm_spawn_allies_start", [("origin", "%d 96 8" % (cx - 96)), ("angles", "0 90 0"), FMG])
    g.ent("mp_tdm_spawn_axis_start", [("origin", "%d 96 8" % (cx + 96)), ("angles", "0 90 0"), FMG])
    for dx in (-672, -480, 480, 672):
        g.ent("mp_tdm_spawn", [("origin", "%d 96 8" % (cx + dx)), ("angles", "0 90 0"), FMG])
    g.ent("script_origin", [("targetname", "minimap_corner"), ("origin", "%d 0 0" % x_lo)])
    g.ent("script_origin", [("targetname", "minimap_corner"), ("origin", "%d %d 0" % (x_hi, ytop))])

    for S in g.stations:
        pad = "show2/pad_" + S.group
        g.world.append(box(S.x0, S.y0, -16, S.x0 + S.w * CELL, S.y0 + CELL, 0, FLOOR, flags=("detail",),
                           top=(pad, (CELL, CELL, 0, S.y0 % CELL, 0))))
        # Sign: a post, a board over the south edge of the first cell, the number in brushes, the name on the board's texture.
        g.world.append(box(S.x0, S.y0, 0, S.x0 + 4, S.y0 + 4, SIGN_Z0, BOARD, flags=("detail",)))
        g.world.append(box(S.x0, S.y0, SIGN_Z0, S.x0 + CELL, S.y0 + 4, SIGN_Z1, BOARD, flags=("detail",),
                           south=("show2/sign_%02d" % S.num, (CELL, 64, 0, 0, 0))))
        for k, ch in enumerate("%02d" % S.num):
            g.world.extend(digit_brushes(ch, S.x0 + 8 + k * 28, SIGN_Z0 + 10, S.y0))
        # The message trigger: one cylinder per cell, all with the station's name (the map script merges them).
        for c in range(S.w):
            g.ent("trigger_radius", [("targetname", S.name(S.slug, True)), ("origin", "%d %d 0" % (S.x0 + c * CELL + 96, S.y0 + 96)), ("radius", "88"),
                                     ("height", "100"), FMG])
        g.script.append('    level thread show2_station("%s", "^3%02d^7  %s");' % (S.name(S.slug, True), S.num, S.title))
        STATIONS[S.num - 1][4](S)
    return g, nrows, (x_hi - x_lo, ytop)


def map_text(g):
    L = ["iwmap 4", '"000_Global" flags active', '"The Map" flags ']
    count = [0]

    def emit(kvs, prims):
        L.append("// entity %d" % count[0])
        L.append("{")
        for k, v in kvs:
            L.append('"%s" "%s"' % (k, v))
        for i, p in enumerate(prims):
            L.append("// brush %d" % i)
            L.append("{")
            L.extend(p)
            L.append("}")
        L.append("}")
        count[0] += 1

    emit([("classname", "worldspawn"), ("iw8r_title", "IW8 Radiant dev showcase 2"), ("sundirection", "-40 235 0"), ("suncolor", "1 0.95 0.85"),
          ("skyname", "sky_mp_shipment"), ("iw8r_fog", "512 6000 0.6 0.65 0.72"), ("iw8r_film", "1.05 0 0.1 0 1.02 1 0.98 0.98 1 1.02"),
          ("iw8r_sun_sprite", "16 0.5 0.45 0.25"), ("iw8r_reverb", "iw8_ext_open_space_med_1"), ("iw8r_script", MAP_ID + ".gsc")], g.world)
    for kvs, prims in g.ents:
        emit(kvs, prims)
    return "\n".join(L) + "\n", count[0]


# ---------------------------------------------------------------- assets

def material_files():
    """name under materials/show2 -> bytes (PNG) or dict (definition)."""
    out = {}

    def mat(name, png, **fields):
        out[name + "_c.png"] = png
        d = {"name": "show2/" + name, "color": name + "_c.png"}
        d.update(fields)
        out[name + ".iw8mat.json"] = d

    for gid, _, rgb in GROUPS:
        img = Img(64, 64, tuple(int(c * 0.55) for c in rgb) + (255,))
        for a, b in ((0, 2), (62, 64)):
            img.rect(0, a, 64, b, rgb + (255,))
            img.rect(a, 0, b, 64, rgb + (255,))
        mat("pad_" + gid, img.png(), surface="concrete", scale=[CELL, CELL])
    mat("digit", Img(16, 16, (255, 240, 150, 255)).png(), surface="metal", scale=[64, 64])
    mat("board", Img(16, 16, (18, 20, 24, 255)).png(), surface="metal", scale=[64, 64])
    orange, dark = (230, 160, 40, 255), (40, 40, 48, 255)
    mat("solid", checker(orange, dark), surface="metal", scale=[64, 64])
    grate = Img(64, 64, (0, 0, 0, 0))
    for k in range(0, 64, 16):
        grate.rect(k, 0, k + 4, 64, (150, 150, 160, 255))
        grate.rect(0, k, 64, k + 4, (150, 150, 160, 255))
    mat("cutout", grate.png(), surface="metal", alphaTest=True, twoSided=True, scale=[64, 64])
    mat("blend", checker((60, 120, 255, 150), (60, 120, 255, 70)), blend=True, twoSided=True, scale=[64, 64])
    glow = Img(64, 64, (0, 0, 0, 255))
    for r in range(30, 0, -2):
        v = int(255 * (1 - r / 30.0))
        glow.rect(32 - r, 32 - r, 32 + r, 32 + r, (v, v, v, 255))
    mat("add", glow.png(), blendMode="add", unlit=True, twoSided=True, tint=[1, 0.6, 0.2], scale=[64, 64])
    mat("multiply", checker((120, 90, 70, 255), (200, 190, 180, 255)), blendMode="multiply", twoSided=True, scale=[64, 64])
    mat("unlit", checker((240, 240, 240, 255), (60, 60, 200, 255)), unlit=True, scale=[64, 64])
    mat("emissive", checker((60, 255, 90, 255), (20, 120, 40, 255)), emission=8, scale=[64, 64])
    mat("tint", checker(orange, dark), surface="metal", tint=[1, 0.35, 0.35], scale=[64, 64])
    disc = Img(64, 64, (0, 0, 0, 0))
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5)
            if d < 30:
                disc.rows[y][x] = (235, 120, 30, 255 if d < 22 or int(d) % 4 < 2 else 0)
    mat("decal", disc.png(), blend=True, sortKey=12, polygonOffset=1, scale=[64, 64])
    for i, (group, _, title, _, _) in enumerate(STATIONS):
        mat("sign_%02d" % (i + 1), sign_image(group, title), surface="metal", scale=[CELL, 64])
    return out


SPARKLE_FX = {
    "name": "show2/sparkle",
    "elements": [
        {"name": "sparks", "image": "", "additive": True, "looping": True, "interval_ms": 80, "count": 2, "life_ms": 900, "life_var_ms": 300,
         "radius": 4, "velocity_min": [-30, -30, 80], "velocity_max": [30, 30, 160], "gravity": 1, "size_start": 4, "size_end": 1,
         "color_start": [1, 0.8, 0.3, 1], "color_end": [1, 0.3, 0.05, 0], "spin": 0},
        {"name": "smoke", "image": "", "additive": False, "looping": True, "interval_ms": 300, "count": 1, "life_ms": 2000, "life_var_ms": 400,
         "radius": 6, "velocity_min": [-5, -5, 30], "velocity_max": [5, 5, 50], "gravity": -0.02, "size_start": 10, "size_end": 36,
         "color_start": [0.5, 0.5, 0.5, 0.5], "color_end": [0.4, 0.4, 0.4, 0], "spin": 0.3},
    ],
}


def sheet_text(g, nrows, size):
    by_group = {}
    for S in g.stations:
        by_group[S.group] = by_group.get(S.group, 0) + 1
    L = ["# Dev showcase 2: test sheet", "",
         "Map: `samples/dev/%s.map` (map id `%s`). Written by `py -3 -I tools/make_dev_showcase2.py`; run it again to get the same files back."
         % (MAP_ID, MAP_ID), "",
         "## Build and play", "",
         "1. Open `samples\\dev\\%s.map` in the editor." % MAP_ID,
         "2. Press **F5**. The editor builds the map, deploys it and starts a Team Deathmatch on it. (F7 builds only; Ctrl+F7 builds and deploys.)",
         "3. You spawn on the south strip and face north. The stations are in %d rows in front of you; number 01 is the first pad on your left."
         % nrows,
         "4. Headless build, no window: `bin\\release\\IW8Radiant.exe --compile samples\\dev\\%s.map --out out\\showcase2-build`." % MAP_ID, "",
         "The file is not called `iw8r_dev_showcase2.map` because the build takes the map id from the file name and cuts it to 15 characters: "
         "that name gives `mp_iw8r_dev_sho`, the id of the first showcase, and a build would overwrite it.", "",
         "## How to tell where you are", "",
         "- **Sign**: over the south edge of every pad. The number is made of bright brushes; the group and the name are on the board beside it. "
         "Signs face south, so read them walking north.",
         "- **Message**: walk onto a pad and the number and name show in the middle of the screen, once per visit.",
         "- **Editor**: every station has a `trigger_radius` named `s<NN>_<name>` and its other entities start with `s<NN>_` too, so the Outliner "
         "reads in station order. Game-mode objects keep the fixed names the game looks for.",
         "- **Pad colour**: one colour per group: " + ", ".join("%s (%d)" % (GROUP_TEXT[gid].lower(), by_group[gid]) for gid, _, _ in GROUPS
                                                              if gid in by_group) + ".",
         "- The arena is %d x %d units; a pad is %d units; the lighting row is under a roof so the sun does not hide the lights." % (size[0], size[1], CELL), "",
         "Legend: **seen working** = docs/ROADMAP.md or docs/TEST_CHECKLIST.md records that you confirmed it in game. **not seen** = built, never "
         "confirmed. **waiting** = needs something that is not built yet.", "",
         "Write OK or what is wrong next to each number and send the list back.", ""]
    group = None
    for S in g.stations:
        if S.group != group:
            group = S.group
            L += ["", "## " + GROUP_TEXT[group].capitalize(), "", "| No | What it is | Class and keys | In game | State |", "|---|---|---|---|---|"]
        esc = lambda s: s.replace("|", "/")
        L.append("| %02d | %s | %s | %s | %s |" % (S.num, esc(S.what), esc(S.keys), esc(S.see), esc(S.state)))
    L += ["", "## Not on the map", "", "| Kind | Why |", "|---|---|"]
    L += ["| %s | %s |" % (a, b) for a, b in NOT_PLACED]
    L += ["", "## Notes", "",
          "- Near-identical classes share a station: all spawn classes of the other modes are on OTHER MODE SPAWNS; the 21 "
          "`trigger_multiple_bcs_*` classes are shown by one of them on CALLOUT / BATTLE CHATTER; `light` and `light_spot` take the same keys.",
          "- Game-mode stations (teal) show nothing in Team Deathmatch: the game removes objects of other modes. To see them, start that mode "
          "with Live Link > Quick match. Picking other modes in the lobby needs the client with the game-mode list (fix queue F.11).",
          "- The message uses the Script dock's \"Trigger message\" code. The editor has no key for it, so the map script "
          "(`%s.gsc`) starts one thread per station; it asks `iw8r_trigger()` for the trigger, as the user guide says." % MAP_ID,
          "- Expected build warnings: " + EXPECTED_WARNINGS % {S.slug: S.num for S in g.stations}, ""]
    return "\n".join(L)


EXPECTED_WARNINGS = (
    "the build ends with `preflight: 0 error(s)` and every `Replay:` line says `0 failed`. These lines are expected: the ladder model "
    "'is built as a static prop' (the Make Ladder layout always gets this note; station %(ladder)02d); three screen materials of the bomb and EMP "
    "models 'build grey' (the game files have no colour image for them); the 160 degree spot 'built as an omni light' (station %(spot_wide)02d "
    "is there to show it); one misc_model traffic cone 'stands still' (on purpose, station %(dynamic)02d); the decal, the volumetric box, the "
    "dynamic props, the authored effect, the sound zones and the map's own sounds are reported as not verified in game; `show2_hum` 'may be "
    "silent' on the fire effect (it is the map's own sound; station %(fx_sound)02d tests it); 30 sky faces 'not rendered' (the sky comes from the "
    "sky asset); the converter says it drops `trigger_damage` and the editor's `iw8r_` keys (the map script handles both) and that the four "
    "stock effects have 'no extracted IW3 FX graph' (they are stock effects and play by name).")


def write_all(out_dir, sheet_path, root):
    g, nrows, size = build()
    files = {}  # path -> bytes
    text, ents = map_text(g)
    files[os.path.join(out_dir, MAP_ID + ".map")] = text.encode("utf-8")
    files[os.path.join(out_dir, MAP_ID + ".gsc")] = (GSC_HEAD + "\n".join(g.script) + "\n" + GSC_TAIL).encode("utf-8")
    for name, data in material_files().items():
        if isinstance(data, dict):
            data = (json.dumps(data, indent=1) + "\n").encode("utf-8")
        files[os.path.join(out_dir, "materials", "show2", name)] = data
    files[os.path.join(out_dir, "models", "show2_crate.obj")] = cube_obj(24, 48, "show2/solid", "IW8 Radiant dev showcase 2 crate (Y-up, map units)").encode()
    files[os.path.join(out_dir, "models", "iw8r_shape_cube_show2.obj")] = cube_obj(
        32, 64, WALL, "IW8 Radiant shape model (64 units, origin at the base centre)").encode()
    files[os.path.join(out_dir, "sounds", "show2_hum.wav")] = sound_hum()
    files[os.path.join(out_dir, "sounds", "show2_hum.iw8snd.json")] = b'{\n  "volume": 1,\n  "dist_min": 100,\n  "dist_max": 700,\n  "loop": true\n}\n'
    files[os.path.join(out_dir, "sounds", "show2_bed.wav")] = sound_bed()
    files[os.path.join(out_dir, "sounds", "show2_bed.iw8snd.json")] = b'{\n  "volume": 0.5\n}\n'
    files[os.path.join(out_dir, "sounds", "show2_chirp.wav")] = sound_chirp()
    files[os.path.join(out_dir, "fx", "show2", "sparkle.iw8fx.json")] = (json.dumps(SPARKLE_FX, indent=2) + "\n").encode("utf-8")
    with open(os.path.join(root, "data", "ies", "iw8r_spot_narrow.ies"), "rb") as f:
        files[os.path.join(out_dir, "lights", "show2_spot_narrow.ies")] = f.read()
    if sheet_path:
        files[sheet_path] = sheet_text(g, nrows, size).encode("utf-8")
    written = 0
    for path, data in sorted(files.items()):
        base = os.path.basename(path)
        own = (base.startswith(("show2_", MAP_ID + ".", "iw8r_shape_cube_show2.")) or os.path.basename(os.path.dirname(path)) == "show2" or
               (sheet_path and path == sheet_path))
        if not own:
            raise SystemExit("refusing to write a file this script does not own: " + path)
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        if os.path.exists(path):
            with open(path, "rb") as f:
                if f.read() == data:
                    continue
        with open(path, "wb") as f:
            f.write(data)
        written += 1
    # Sign plates of stations that no longer exist: materials/show2 holds nothing but this script's files.
    mdir = os.path.join(out_dir, "materials", "show2")
    for base in sorted(os.listdir(mdir)):
        if base.startswith("sign_") and os.path.join(mdir, base) not in files:
            os.remove(os.path.join(mdir, base))
    brushes = len(g.world) + sum(len(p) for _, p in g.ents)
    print("%s: %d stations in %d rows, arena %d x %d, %d entities, %d brushes and patches, %d files (%d written)"
          % (MAP_ID, len(g.stations), nrows, size[0], size[1], ents, brushes, len(files), written))
    counts = {}
    for S in g.stations:
        counts[S.group] = counts.get(S.group, 0) + 1
    print("by group: " + ", ".join("%s %d" % (k, v) for k, v in counts.items()))


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=os.path.join(root, "samples", "dev"))
    ap.add_argument("--sheet", default=os.path.join(root, "docs", "DEV_SHOWCASE.md"), help="test sheet path; '' writes none")
    a = ap.parse_args()
    write_all(a.out_dir, a.sheet, root)


if __name__ == "__main__":
    main()
