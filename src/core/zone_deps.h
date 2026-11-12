#pragma once
// Zone dependency view (roadmap 1.15, base for 4.15 "reference instead of carry"). For a compiled extraction it lists what
// the map carries, which carried copies are copies of stock assets, the stock zones that already hold each of those
// copies (the "could be referenced instead" candidates), the names the map uses without carrying a file (effects, sounds,
// engine $ names), and a per-zone summary of what the map takes from each stock zone.
//
// Zone classes: a zone is always loaded when its name starts with common_, techsets_common, code_ or mp_patch. Evidence in
// data\asset_catalog: iw8_common_vfx.tsv and iw8_common_sounds.tsv say their rows are the always-loaded MP zones (common_mp,
// common_core_mp, common_base_mp, and the always-loaded MP soundbanks); code_post_gfx and techsets_common hold 688 and 870
// catalog rows (engine $ images and shared techsets). No catalog row names an mp_patch zone yet; the prefix is kept so a
// patch zone is classed as always loaded when one appears. global_* and techsets_global* are not in the rule: they are
// reported as map-specific zones, and the summary says so.
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

enum class DepKind { Material, Image, Model, Effect, Sound };

const char* depKindName(DepKind k);  // "material", "image", "model", "effect", "sound"

// A carried copy of a stock asset: materials\iw8r_stock\<stock>.json, an image whose material links it to a stock material
// (iw8rm_iw8r_stock_<...>_<hash>_<c|n|s>), or xmodel\iw8r_stock_<stock>.json.
struct StockCopy {
    DepKind kind = DepKind::Material;
    std::string copyName;                 // name in the extraction: material path without .json, image stem, model stem
    std::string stockName;                // stock name in the catalogs ("" when the copy has no stock name)
    uint64_t bytes = 0;                   // file bytes the copy takes: material json; image dds; model json + replay json
    std::vector<std::string> stockZones;  // zones whose catalog rows hold the stock asset (sorted; empty when none)
};

// A name the map uses without carrying a file: an effect (loadfx in the script, fx key of an entity), a sound alias (playsound
// family, soundalias key, sound\<id>.stock_sounds.txt) or an engine $ name (material or image).
struct ByNameRef {
    DepKind kind = DepKind::Effect;
    std::string name;                     // lower case for effects and sounds
    std::string where;                    // first place the map uses it
    std::vector<std::string> zones;       // catalog zones (sorted; empty when the catalogs do not list it)
};

struct KindTotals {
    DepKind kind = DepKind::Material;
    uint64_t carried = 0;                 // files of this kind in the extraction
    uint64_t carriedBytes = 0;
    uint64_t stockCopies = 0;             // carried copies of stock assets
    uint64_t stockCopyBytes = 0;
    uint64_t referenceable = 0;           // copies whose stock asset is listed in a catalog zone: could be referenced instead
    uint64_t referenceableBytes = 0;      // bytes those copies take (what referencing would save)
    uint64_t byName = 0;                  // names used without a carried file
    uint64_t byNameInZones = 0;           // of those, the ones the catalogs list in a zone
};

struct ZoneUsage {
    std::string zone;
    bool alwaysLoaded = false;
    uint64_t assets = 0;                  // stock candidates and by-name refs whose catalog zones include this zone
    uint64_t soleSource = 0;              // of those, the ones listed in this zone alone
};

struct ZoneDepReport {
    std::string mapId;
    std::vector<KindTotals> kinds;        // one per DepKind, in enum order
    std::vector<StockCopy> stockCopies;   // every carried copy of a stock asset (zoned or not)
    std::vector<ByNameRef> byName;        // every name used without a carried file
    std::vector<ZoneUsage> zones;         // most assets first, then by name
    std::vector<std::string> notes;       // catalogs or folders that could not be read
};

// True for the zones the rule above treats as always loaded.
bool zoneIsAlwaysLoaded(const std::string& zone);

// Reads the extraction folder (`extractedDir`, the `extracted` folder) for map `mapId` and the stock catalogs in `catalogDir`
// (data\asset_catalog). A missing file or folder counts as nothing and is listed in `notes`.
ZoneDepReport makeZoneDepReport(const std::string& extractedDir, const std::string& mapId, const std::string& catalogDir);

// Text: the per-kind totals, the per-zone table, then the top 20 reference-instead candidates by bytes.
std::string formatZoneDepReport(const ZoneDepReport& r);

}  // namespace iw8r
