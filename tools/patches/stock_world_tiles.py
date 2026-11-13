import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and s.count(b) >= 1:
        return  # already applied
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/stock_world.h'
C = 'src/core/stock_world.cpp'

patch(H, '''#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>''', '''#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>''')
patch(H, '''constexpr int kStockMaxLayers = 8;  // GfxWorldSurfData.layerCount on mp_br_tut goes up to 8 (twc4/14n_17n_17n_..._24n)
''', '''constexpr int kStockMaxLayers = 8;  // GfxWorldSurfData.layerCount on mp_br_tut goes up to 8 (twc4/14n_17n_17n_..._24n)

// The main zone's surface table (gfx_map surfaces and surfData): every surface, also those whose buffers are in a
// streamed tile. Kept with the world so a big map's tiles can be built one at a time later (buildStockWorldTile)
// without parsing the main gfx_map again (hundreds of MB on mp_donetsk).
struct StockWorldSurfaceTable {
    struct Surface {
        std::string material;  // without the leading comma ACTS prints
        int64_t posOffset = -1, vertexCount = -1, triCount = -1, baseIndex = -1;
        int64_t zone = 0;            // transientZone
        int64_t surfDataIndex = -1;
        int64_t lightmapIndex = 0;
        bool hasTris = false;
    };
    struct SurfData {
        int64_t layerCount = 0;
        int64_t texCoordOffset = 0;  // aux stream byte offset of vertex 0, layer 0 (0 when absent)
        int64_t xyzOffset = -1;      // posData byte offset of the block's vertex 0 (-1 when absent)
        int64_t lmapCoordOffset = 0; // aux stream byte offset of the block's vertex 0 lightmap UV (0 when absent)
        int64_t colorOffset = 0;     // aux stream byte offset of the block's vertex 0 colour record (0 when absent)
        std::array<int64_t, 7> normalTransformOffset{};  // per layer above 0 (0 when absent)
    };
    std::vector<Surface> surfaces;
    std::vector<SurfData> surfData;
    std::map<std::string, int> materialLayers;  // the largest layerCount of each material's surfaces
    double bakedLightScale = 1.0;
};
''')
patch(H, '''    // maps, 1/64 on mp_m_king (an indoor map whose lightmaps were stored 6 EV brighter).
    double bakedLightScale = 1.0;''', '''    // maps, 1/64 on mp_m_king (an indoor map whose lightmaps were stored 6 EV brighter).
    double bakedLightScale = 1.0;
    // The surface table the world was built from (readStockWorld / parseStockWorld): the tiles of a big map are built
    // from it (buildStockWorldTile).
    std::shared_ptr<const StockWorldSurfaceTable> surfaceTable;''')
patch(H, '''bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err = nullptr);''',
      '''// includeStreamed false: only the main zone's own buffers (the streamed tiles are added one by one later).
bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err = nullptr, bool includeStreamed = true);

// One streamed tile of a big map: the surfaces of `table` whose transient zone has its buffers in the tile export
// `tileDir` (<zoneDir>/streamed/<tile>, its gfx_map_trzone JSON), as a world of their own (same conventions as
// readStockWorld). Fails when the tile holds no buffers or none of the table's surfaces use them.
bool buildStockWorldTile(const StockWorldSurfaceTable& table, const std::string& tileDir, StockWorld& out,
                         std::string* err = nullptr);''')

# ---- cpp: the record types come from the header
patch(C, '''// The per-surface fields of GfxWorldSurfData (gfx_map surfaces.surfData) that the reader needs.
struct SurfData {
    int64_t layerCount = 0;
    int64_t texCoordOffset = 0;  // aux stream byte offset of vertex 0, layer 0 (0 when absent)
    int64_t xyzOffset = -1;      // posData byte offset of the block's vertex 0 (-1 when absent)
    int64_t lmapCoordOffset = 0; // aux stream byte offset of the block's vertex 0 lightmap UV (0 when absent)
    int64_t colorOffset = 0;     // aux stream byte offset of the block's vertex 0 colour record (0 when absent)
    std::array<int64_t, 7> normalTransformOffset{};  // per layer above 0 (0 when absent)
};''', '''// The per-surface fields of GfxWorldSurfData (gfx_map surfaces.surfData) that the reader needs.
using SurfData = StockWorldSurfaceTable::SurfData;''')

# ---- split buildWorld into the table parse and the mesh build
patch(C, '''// Builds the meshes from the gfx_map surfaces and the transient zone buffers.
bool buildWorld(const std::string& gfxText, std::map<int64_t, TrzData>& trz, StockWorld& out, std::string* err) {
    std::string jerr;
    const auto root = json::parse(gfxText, &jerr);
    if (!root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*root);''', '''// The surface table of a gfx_map JSON text.
bool parseSurfaceTable(const std::string& gfxText, StockWorldSurfaceTable& table, std::string* err) {
    std::string jerr;
    const auto root = json::parse(gfxText, &jerr);
    if (!root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    StockWorldSurfaceTable& out = table;
    const Value* f = fieldsOf(*root);''')
patch(C, '''        if (stride >= 0x1C && unhex(*sd->get("bytes")->string(), raw)) {
            const auto u32 = [&](size_t at) {
                return int64_t(uint32_t(raw[at]) | uint32_t(raw[at + 1]) << 8 | uint32_t(raw[at + 2]) << 16 | uint32_t(raw[at + 3]) << 24);
            };
            for (size_t at = 0; at + size_t(stride) <= raw.size(); at += size_t(stride)) {
                SurfData d;
                d.layerCount = u32(at + 0x04);
                d.xyzOffset = u32(at + 0x08);
                d.lmapCoordOffset = u32(at + 0x10);
                d.colorOffset = u32(at + 0x14);
                d.texCoordOffset = u32(at + 0x18);
                if (stride >= 0x38)  // normalTransformOffset[7] at 0x1C .. 0x37
                    for (size_t i = 0; i < d.normalTransformOffset.size(); ++i) d.normalTransformOffset[i] = u32(at + 0x1C + 4 * i);
                surfData.push_back(d);
            }
        }
    }
''', '''        if (stride >= 0x1C && unhex(*sd->get("bytes")->string(), raw)) {
            const auto u32 = [&](size_t at) {
                return int64_t(uint32_t(raw[at]) | uint32_t(raw[at + 1]) << 8 | uint32_t(raw[at + 2]) << 16 | uint32_t(raw[at + 3]) << 24);
            };
            for (size_t at = 0; at + size_t(stride) <= raw.size(); at += size_t(stride)) {
                SurfData d;
                d.layerCount = u32(at + 0x04);
                d.xyzOffset = u32(at + 0x08);
                d.lmapCoordOffset = u32(at + 0x10);
                d.colorOffset = u32(at + 0x14);
                d.texCoordOffset = u32(at + 0x18);
                if (stride >= 0x38)  // normalTransformOffset[7] at 0x1C .. 0x37
                    for (size_t i = 0; i < d.normalTransformOffset.size(); ++i) d.normalTransformOffset[i] = u32(at + 0x1C + 4 * i);
                surfData.push_back(d);
            }
        }
    }
    out.surfData = surfData;
    out.surfaces.clear();
    out.surfaces.reserve(surfs->arr.size());
    for (const Value& s : surfs->arr) {
        StockWorldSurfaceTable::Surface r;
        const Value* mat = s.get("material");
        r.material = mat && mat->get("name") && mat->get("name")->string() ? *mat->get("name")->string() : "";
        if (!r.material.empty() && r.material[0] == ',') r.material.erase(0, 1);
        if (const Value* tris = s.get("tris")) {
            r.hasTris = true;
            r.posOffset = int64_t(num(tris->get("posOffset"), -1));
            r.vertexCount = int64_t(num(tris->get("vertexCount"), -1));
            r.triCount = int64_t(num(tris->get("triCount"), -1));
            r.baseIndex = int64_t(num(tris->get("baseIndex"), -1));
        }
        r.zone = int64_t(num(s.get("transientZone"), 0));
        r.surfDataIndex = int64_t(num(s.get("surfDataIndex"), -1));
        r.lightmapIndex = int64_t(num(s.get("lightmapIndex"), 0));
        out.surfaces.push_back(std::move(r));
    }
    // The layer count of each material: the largest layerCount of its surfaces (research 97), so every vertex of a
    // mesh has the same number of layer records.
    out.materialLayers.clear();
    for (const auto& r : out.surfaces) {
        int n = 1;
        if (r.surfDataIndex >= 0 && r.surfDataIndex < int64_t(surfData.size()))
            n = int(std::clamp<int64_t>(surfData[size_t(r.surfDataIndex)].layerCount, 1, kStockMaxLayers));
        int& m = out.materialLayers[r.material];
        m = std::max(m, n);
    }
    return true;
}

// Builds the meshes from the surface table and the transient zone buffers. quietMissing: surfaces of zones not in
// `trz` are expected (one streamed tile at a time) and not reported.
bool buildFromTable(const StockWorldSurfaceTable& table, std::map<int64_t, TrzData>& trz, StockWorld& out, std::string* err,
                    bool quietMissing) {
    const std::vector<SurfData>& surfData = table.surfData;
    out.bakedLightScale = table.bakedLightScale;
''')
patch(C, '''    // The layer count of each material: the largest layerCount of its surfaces (research 97), so every vertex of a
    // mesh has the same number of layer records.
    const auto surfaceLayers = [&](const Value& s) -> int {
        const int64_t k = int64_t(num(s.get("surfDataIndex"), -1));
        if (k < 0 || k >= int64_t(surfData.size())) return 1;
        return int(std::clamp<int64_t>(surfData[size_t(k)].layerCount, 1, kStockMaxLayers));
    };
    const auto materialOf = [](const Value& s) {
        const Value* mat = s.get("material");
        std::string name = mat && mat->get("name") && mat->get("name")->string() ? *mat->get("name")->string() : "";
        if (!name.empty() && name[0] == ',') name.erase(0, 1);
        return name;
    };
    std::map<std::string, int> materialLayers;
    for (const Value& s : surfs->arr) {
        int& n = materialLayers[materialOf(s)];
        n = std::max(n, surfaceLayers(s));
    }

    for (const Value& s : surfs->arr) {
        const Value* tris = s.get("tris");
        if (!tris) {
            ++surfacesBad;
            continue;
        }
        const int64_t posOffset = int64_t(num(tris->get("posOffset"), -1));
        const int64_t vertexCount = int64_t(num(tris->get("vertexCount"), -1));
        const int64_t triCount = int64_t(num(tris->get("triCount"), -1));
        const int64_t baseIndex = int64_t(num(tris->get("baseIndex"), -1));
        const int64_t zoneIndex = int64_t(num(s.get("transientZone"), 0));
        if (posOffset < 0 || vertexCount <= 0 || triCount <= 0 || baseIndex < 0) {
            ++surfacesBad;
            continue;
        }
        const auto zit = trz.find(zoneIndex);
        if (zit == trz.end()) {
            ++missingZone[zoneIndex];
            continue;
        }''', '''    const auto surfaceLayers = [&](const StockWorldSurfaceTable::Surface& s) -> int {
        const int64_t k = s.surfDataIndex;
        if (k < 0 || k >= int64_t(surfData.size())) return 1;
        return int(std::clamp<int64_t>(surfData[size_t(k)].layerCount, 1, kStockMaxLayers));
    };
    const auto materialOf = [](const StockWorldSurfaceTable::Surface& s) -> const std::string& { return s.material; };
    std::map<std::string, int> materialLayers = table.materialLayers;

    for (const StockWorldSurfaceTable::Surface& s : table.surfaces) {
        if (!s.hasTris) {
            if (!quietMissing || trz.count(s.zone)) ++surfacesBad;
            continue;
        }
        const int64_t posOffset = s.posOffset;
        const int64_t vertexCount = s.vertexCount;
        const int64_t triCount = s.triCount;
        const int64_t baseIndex = s.baseIndex;
        const int64_t zoneIndex = s.zone;
        const auto zit = trz.find(zoneIndex);
        if (zit == trz.end()) {
            if (!quietMissing) ++missingZone[zoneIndex];
            continue;
        }
        if (posOffset < 0 || vertexCount <= 0 || triCount <= 0 || baseIndex < 0) {
            ++surfacesBad;
            continue;
        }''')
patch(C, '''        const int64_t surfDataIndex = int64_t(num(s.get("surfDataIndex"), -1));''',
      '''        const int64_t surfDataIndex = s.surfDataIndex;''')
patch(C, '''        const int64_t surfLightmap = int64_t(num(s.get("lightmapIndex"), 0));''',
      '''        const int64_t surfLightmap = s.lightmapIndex;''')
# the old entry point
patch(C, '''bool parseStockWorld(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson, StockWorld& out,
                     std::string* err) {''', '''namespace {
// The table parse, the mesh build, and the table kept with the world.
bool buildWorld(const std::string& gfxText, std::map<int64_t, TrzData>& trz, StockWorld& out, std::string* err) {
    auto table = std::make_shared<StockWorldSurfaceTable>();
    if (!parseSurfaceTable(gfxText, *table, err)) return false;
    if (!buildFromTable(*table, trz, out, err, false)) return false;
    out.surfaceTable = std::move(table);
    return true;
}
}  // namespace

bool parseStockWorld(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson, StockWorld& out,
                     std::string* err) {''')
patch(C, '''bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err) {''',
      '''bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err, bool includeStreamed) {''')
patch(C, '''        const fs::path streamed = fs::path(zoneDir) / "streamed";
        if (fs::is_directory(streamed, ec))
            for (fs::recursive_directory_iterator it(streamed, ec), end; !ec && it != end; it.increment(ec))''',
      '''        const fs::path streamed = fs::path(zoneDir) / "streamed";
        if (includeStreamed && fs::is_directory(streamed, ec))
            for (fs::recursive_directory_iterator it(streamed, ec), end; !ec && it != end; it.increment(ec))''')
patch(C, '''    std::string gfxText;
    if (!readFile(gfx.front(), gfxText, err)) return false;
    return buildWorld(gfxText, trz, out, err);
}

}  // namespace iw8r''', '''    std::string gfxText;
    if (!readFile(gfx.front(), gfxText, err)) return false;
    return buildWorld(gfxText, trz, out, err);
}

bool buildStockWorldTile(const StockWorldSurfaceTable& table, const std::string& tileDir, StockWorld& out, std::string* err) {
    std::map<int64_t, TrzData> trz;
    std::error_code ec;
    if (!fs::is_directory(tileDir, ec)) {
        if (err) *err = "no tile export at " + tileDir;
        return false;
    }
    for (fs::recursive_directory_iterator it(tileDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".json" ||
            it->path().string().find("gfx_map_trzone") == std::string::npos)
            continue;
        std::string text;
        if (!readFile(it->path(), text, err)) return false;
        int64_t zone = 0;
        TrzData d;
        bool empty = false;
        if (!extractTrz(text, zone, d, err, &empty)) return false;
        if (!empty) trz[zone] = std::move(d);
    }
    if (trz.empty()) {
        if (err) *err = "the tile export at " + tileDir + " holds no transient zone buffers";
        return false;
    }
    return buildFromTable(table, trz, out, err, true);
}

}  // namespace iw8r''')
print('ok')
