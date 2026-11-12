import io, json, copy
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/core/compile_iw8.cpp', [
(r'''        if (lcls == "iw8r_volume") continue;    // written as a world.json volumetric box (5.7)''',
 r'''        if (lcls == "iw8r_volume") continue;    // written as a world.json volumetric box (5.7)
        if (lcls == "iw8r_decal") continue;     // written as a world.json decal volume (5.8)'''),
(r'''    const std::string volumesKey = volumeCount ? ",\"volumetrics\":[" + volumeJson + "]" : std::string();''',
 r'''    const std::string volumesKey = volumeCount ? ",\"volumetrics\":[" + volumeJson + "]" : std::string();
    // Decal volumes (5.8, research 80 section 6): `iw8r_decal` point entities (origin, angles: the forward axis is the
    // projection direction and points into the surface; size "depth width height"; material; optional opacity,
    // feather, draw_order, uv_rotate, uv_scale, uv_shift) become world.json `decal_volumes`. A bad entity is reported
    // and left out, so it cannot stop the build. A converter without the decal producer ignores the key.
    std::string decalJson;
    size_t decalCount = 0;
    std::set<std::string> decalMaterials;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "iw8r_decal") continue;
        const std::string where = "iw8r_decal entity " + std::to_string(pe - map.entities.data());
        const std::string* mv = pe->value("material");
        if (!mv || mv->empty()) {
            warn.push_back(where + ": no material; left out");
            continue;
        }
        double size[3] = {8, 64, 64};
        if (const std::string* sv = pe->value("size"); sv && !parseVec3Text(*sv, size)) {
            warn.push_back(where + ": size '" + *sv + "' not parsed (depth width height); left out");
            continue;
        }
        if (!(size[0] > 0 && size[1] > 0 && size[2] > 0) || !std::isfinite(size[0] + size[1] + size[2])) {
            warn.push_back(where + ": size must be three numbers above 0; left out");
            continue;
        }
        const auto ranged = [&](const char* key, double def, double lo, double hi) {
            const std::string* v = pe->value(key);
            double d = 0;
            if (!v || v->empty()) return def;
            if (!parseNumber(*v, d) || !std::isfinite(d) || d < lo || d > hi) {
                warn.push_back(where + ": " + key + " '" + *v + "' out of range (" + num(lo) + " to " + num(hi) + "); using " + num(def));
                return def;
            }
            return d;
        };
        const double opacity = ranged("opacity", 1, 0, 1), feather = ranged("feather", 0, 0, 1);
        const int drawOrder = int(std::lround(ranged("draw_order", 0, -4, 3)));
        const double uvRotate = ranged("uv_rotate", 0, -360, 360);
        const auto pair = [&](const char* key, double a, double b, bool positive) {
            std::array<double, 2> out{a, b};
            const std::string* v = pe->value(key);
            if (!v || v->empty()) return out;
            double x = 0, y = 0;
            if (std::sscanf(v->c_str(), "%lf %lf", &x, &y) != 2 || !std::isfinite(x + y) || (positive && !(x > 0 && y > 0))) {
                warn.push_back(where + ": " + key + " '" + *v + "' not understood (two numbers); using " + num(a) + " " + num(b));
                return out;
            }
            return std::array<double, 2>{x, y};
        };
        const auto uvScale = pair("uv_scale", 1, 1, true), uvShift = pair("uv_shift", 0, 0, false);
        // A material of the custom library is built with the map (its picture's alpha is the decal's shape); any other
        // name is passed through as a stock material the game already has loaded.
        std::string material = *mv;
        if (opt.library && opt.library->find(material)) material = materialFor(material);
        if (!decalMaterials.count(material) && decalMaterials.size() >= 2047) {
            warn.push_back(where + ": more than 2047 different decal materials; left out");
            continue;
        }
        decalMaterials.insert(material);
        double ang[3] = {0, 0, 0};
        if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, ang)) ang[0] = ang[1] = ang[2] = 0;
        const auto ax = anglesToAxis(ang[0], ang[1], ang[2]);
        const Vec3 o = pe->origin().value_or(Vec3{});
        const double rad = uvRotate * 3.14159265358979323846 / 180.0, c = std::cos(rad), s = std::sin(rad);
        decalJson += std::string(decalCount ? "," : "") + "{\"center\":" + vec3(o) + ",\"axes\":[" + vec3(ax[0]) + "," + vec3(ax[1]) + "," +
                     vec3(ax[2]) + "],\"half_size\":" + vec3raw(size[0] / 2, size[1] / 2, size[2] / 2) + ",\"material\":" + quote(material) +
                     ",\"opacity\":" + num(opacity) + ",\"feather\":" + num(feather) + ",\"draw_order\":" + std::to_string(drawOrder) +
                     ",\"uv_matrix\":[" + num(c * uvScale[0]) + "," + num(-s * uvScale[1]) + "," + num(s * uvScale[0]) + "," +
                     num(c * uvScale[1]) + "],\"uv_offset\":[" + num(uvShift[0]) + "," + num(uvShift[1]) + "]}";
        ++decalCount;
    }
    if (decalCount)
        warn.push_back(std::to_string(decalCount) + " decal volume(s) written (iw8r_decal; needs a converter with the decal producer, " +
                       "research 80 and 82; in-game look not verified)");
    const std::string decalsKey = decalCount ? ",\"decal_volumes\":[" + decalJson + "]" : std::string();'''),
(r'''        ",\"reflection_probes\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey +''',
 r'''        ",\"reflection_probes\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey + decalsKey +'''),
])

p = 'data/iw8_entity_defs_overrides.json'
raw = io.open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
j = json.loads(raw)
gen = json.load(io.open('data/iw8_entity_defs.json', encoding='utf-8'))
src = None
classes = gen['classes']
if isinstance(classes, dict):
    src = classes.get('iw8r_volume')
else:
    src = next((c for c in classes if c.get('name') == 'iw8r_volume'), None)
assert src, 'iw8r_volume not found in the generated defs'
cls = copy.deepcopy(src)
def key(t, default, desc, required=False, examples=None):
    d = {"type": t, "default": default, "description": desc, "required": required}
    if examples: d["examples"] = examples
    return d
cls.pop('name', None)
cls['description'] = ('A decal: a picture projected onto the surfaces inside a box (stains, paint, markings). The box is placed at the '
                      'origin; its forward (X) axis is the direction the picture is projected and must point into the surface. '
                      'Needs a converter with the decal producer (research 80 and 82). Not seen in game.')
cls['keys'] = {
    'origin': key('vec3', '', 'The centre of the box.', True, ['0 0 4']),
    'angles': key('vec3', '0 0 0', 'Pitch, yaw, roll. Forward is the projection direction: for a floor decal use 90 0 0 (pointing down).', False, ['90 0 0', '0 90 0']),
    'size': key('vec3', '8 64 64', 'Depth, width and height of the box in map units. Depth is along the projection direction.', False, ['8 64 64', '16 128 128']),
    'material': key('string', '', 'The decal picture: a material of your custom library (its alpha is the decal\'s shape), or the name of a stock decal material the game always has loaded.', True, ['my/arrow_decal', 'i/vfx_decal_surface_scorch_1']),
    'opacity': key('float', '1', 'How strong the decal is, 0 to 1.', False, ['1', '0.6']),
    'feather': key('float', '0', 'Soft fade toward the front and back of the box, 0 to 1.', False, ['0', '0.25']),
    'draw_order': key('int', '0', 'Which decal draws on top where two overlap: -4 to 3, higher on top.', False, ['0', '2']),
    'uv_rotate': key('float', '0', 'Turns the picture, in degrees.', False, ['0', '90']),
    'uv_scale': key('string', '1 1', 'Tiling of the picture across the box: two numbers above 0.', False, ['1 1', '2 2']),
    'uv_shift': key('string', '0 0', 'Shifts the picture: two numbers.', False, ['0 0', '0.5 0']),
}
j['classes']['iw8r_decal'] = cls
out = json.dumps(j, indent=2, ensure_ascii=False) + '\n'
io.open(p, 'w', encoding='utf-8', newline='').write(out.replace('\n', '\r\n') if crlf else out)
print('ok', sorted(k for k in cls.keys()))
