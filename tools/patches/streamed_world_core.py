import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/stock_world.h', [
 ('''// Decodes one PackedTexCoords word:''', '''// The streamed world tiles of a big map (mp_br_quarry, mp_donetsk; research 87 section 1.2): the main zone holds
// only an empty stub per transient zone, the geometry is in <map>_<NNNNN>_tr.ff. For each stub: its transient zone
// index, its name (the name of the zone file that holds it, without .ff) and the bounds of the surfaces that use it
// (gfx_map surfaceBounds, one 56-byte record per surface: Bounds midPoint, halfSize first). Sorted by index.
struct StreamedWorldZone {
    int64_t index = 0;
    std::string name;
    AABB bounds;
    size_t surfaces = 0;
};
bool readStreamedWorldZones(const std::string& zoneDir, std::vector<StreamedWorldZone>& out, std::string* err = nullptr);

// The streamed zones whose surfaces come within `radius` of `centre` on X and Y, nearest first, at most `limit`.
std::vector<StreamedWorldZone> streamedZonesNear(const std::vector<StreamedWorldZone>& zones, double x, double y, double radius,
                                                 size_t limit);

// Decodes one PackedTexCoords word:'''),
 ('''// Reads an ACTS export folder: <zoneDir>/assets/gfx_map/**/*.json (exactly one) and
// <zoneDir>/assets/gfx_map_trzone/**/*.json (one or more). zoneDir is <out>/mw19replay/<zone>.
''', '''// Reads an ACTS export folder: <zoneDir>/assets/gfx_map/**/*.json (exactly one) and
// <zoneDir>/assets/gfx_map_trzone/**/*.json (one or more). zoneDir is <out>/mw19replay/<zone>. The streamed tiles a
// big map was given (exports of its _tr zones under <zoneDir>/streamed/**/gfx_map_trzone) are read too.
'''),
])
patch('src/core/stock_world.cpp', [
 ('''    // One transient zone text at a time: the largest parse holds a hex copy of its buffers, so peak memory stays low.
    std::map<int64_t, TrzData> trz;
    for (const fs::path& p : trzFiles) {
''', '''    // One transient zone text at a time: the largest parse holds a hex copy of its buffers, so peak memory stays low.
    std::map<int64_t, TrzData> trz;
    std::vector<fs::path> allTrz = trzFiles;
    {
        // Streamed tiles exported later (WorldCache::requestStreamed): <zoneDir>/streamed/<tile>/.../gfx_map_trzone.
        std::error_code ec;
        const fs::path streamed = fs::path(zoneDir) / "streamed";
        if (fs::is_directory(streamed, ec))
            for (fs::recursive_directory_iterator it(streamed, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".json" &&
                    it->path().string().find("gfx_map_trzone") != std::string::npos)
                    allTrz.push_back(it->path());
    }
    for (const fs::path& p : allTrz) {
'''),
 ('''void unpackStockTexCoords(uint32_t packed, double& u, double& v) {''', '''bool readStreamedWorldZones(const std::string& zoneDir, std::vector<StreamedWorldZone>& out, std::string* err) {
    out.clear();
    const fs::path base = fs::path(zoneDir) / "assets";
    const auto gfx = jsonFilesUnder(base / "gfx_map");
    if (gfx.size() != 1) {
        if (err) *err = "expected one gfx_map JSON under " + (base / "gfx_map").string();
        return false;
    }
    // The stubs: names and indices of the transient zones whose buffers are elsewhere.
    std::map<int64_t, StreamedWorldZone> byIndex;
    for (const fs::path& p : jsonFilesUnder(base / "gfx_map_trzone")) {
        std::string text;
        if (!readFile(p, text, err)) return false;
        std::string jerr;
        const auto root = json::parse(text, &jerr);
        if (!root) continue;
        const Value* f = fieldsOf(*root);
        const Value* dv = f ? f->get("drawVerts") : nullptr;
        if (!dv || num(dv->get("posDataSize"), -1) != 0) continue;  // a zone with its buffers here is not streamed
        StreamedWorldZone z;
        z.index = int64_t(num(f->get("transientZoneIndex"), 0));
        if (const Value* n = f->get("name")) {
            if (n->string()) z.name = *n->string();
            else if (const Value* s = n->get("string"); s && s->string()) z.name = *s->string();
        }
        if (z.name.empty()) continue;
        byIndex[z.index] = z;
    }
    std::string gfxText;
    if (!readFile(gfx.front(), gfxText, err)) return false;
    std::string jerr;
    const auto root = json::parse(gfxText, &jerr);
    if (!root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*root);
    const Value* surfaces = f && f->get("surfaces") ? f->get("surfaces") : nullptr;
    const Value* surfs = surfaces && surfaces->get("surfaces") ? surfaces->get("surfaces")->get("values") : nullptr;
    const Value* bounds = surfaces && surfaces->get("surfaceBounds") ? surfaces->get("surfaceBounds")->get("bytes") : nullptr;
    if (!bounds) bounds = f && f->get("surfaceBounds") ? f->get("surfaceBounds")->get("bytes") : nullptr;
    if (!surfs || surfs->type != Value::Type::Array || !bounds || !bounds->string()) {
        if (err) *err = "gfx_map has no surfaces or surfaceBounds";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!unhex(*bounds->string(), raw) || raw.size() / 56 < surfs->arr.size()) {
        if (err) *err = "gfx_map surfaceBounds do not cover the surfaces";
        return false;
    }
    for (size_t i = 0; i < surfs->arr.size(); ++i) {
        const auto it = byIndex.find(int64_t(num(surfs->arr[i].get("transientZone"), -1)));
        if (it == byIndex.end()) continue;
        const Vec3 mid{readF32(raw, i * 56), readF32(raw, i * 56 + 4), readF32(raw, i * 56 + 8)};
        const Vec3 half{readF32(raw, i * 56 + 12), readF32(raw, i * 56 + 16), readF32(raw, i * 56 + 20)};
        if (!std::isfinite(mid.x) || !std::isfinite(half.x)) continue;
        it->second.bounds.add(mid - half);
        it->second.bounds.add(mid + half);
        ++it->second.surfaces;
    }
    for (auto& [index, z] : byIndex)
        if (z.surfaces) out.push_back(z);
    return true;
}

std::vector<StreamedWorldZone> streamedZonesNear(const std::vector<StreamedWorldZone>& zones, double x, double y, double radius,
                                                 size_t limit) {
    std::vector<std::pair<double, const StreamedWorldZone*>> near;
    for (const StreamedWorldZone& z : zones) {
        if (!z.bounds.valid()) continue;
        const double dx = std::max({z.bounds.mins.x - x, 0.0, x - z.bounds.maxs.x});
        const double dy = std::max({z.bounds.mins.y - y, 0.0, y - z.bounds.maxs.y});
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= radius) near.push_back({d, &z});
    }
    std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<StreamedWorldZone> out;
    for (const auto& [d, z] : near) {
        if (out.size() >= limit) break;
        out.push_back(*z);
    }
    return out;
}

void unpackStockTexCoords(uint32_t packed, double& u, double& v) {'''),
])
print('ok')
