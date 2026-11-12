#include "stock_prefabs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "map_text.h"

namespace iw8r {

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPosTol = 1.0;      // inches
constexpr double kAngTol = 1.0;      // degrees
constexpr double kScaleTol = 0.01;
constexpr double kGridCell = 128.0;  // >= the largest rule radius, so a 3x3 cell neighbourhood covers a radius

const std::vector<StockPrefabRule>& ruleTable() {
    static const std::vector<StockPrefabRule> t = {
        {"vehicle", "car cars vehicle vehicles truck trucks van bus motorcycle bike", 0, ""},
        {"door", "door doors", 96, ""},
        {"window", "window windows", 96, "shutter"},
        {"shutter", "shutter shutters blind blinds", 96, "window"},
        {"frame", "frame frames casing", 96, "window door shutter"},
        {"trim", "trim trims molding moulding", 0, ""},
        {"handle", "handle handles knob knobs hinge hinges latch", 0, ""},
        {"glass", "glass glasses pane panes glazing", 0, ""},
        {"stair", "stair stairs step steps staircase", 128, "railing ladder"},
        {"ladder", "ladder ladders rung rungs", 48, "railing"},
        {"railing", "railing railings rail rails banister handrail balustrade", 64, "fence"},
        {"fence", "fence fences gate gates", 64, "railing"},
        {"lamp", "lamp lamps light lights lantern sconce spotlight floodlight", 32, "pipe"},
        {"pipe", "pipe pipes duct ducts valve valves", 48, "lamp barrier"},
        {"barrel", "barrel barrels drum drums keg", 48, "barrel crate"},
        {"crate", "crate crates box boxes pallet pallets", 48, "crate barrel"},
        {"sandbag", "sandbag sandbags sack sacks", 48, "sandbag barrier"},
        {"barrier", "barrier barriers barricade barricades jersey", 48, "barrier sandbag"},
        {"cover", "cover covers bunker bunkers hesco", 0, ""},
        {"furniture",
         "chair chairs table tables desk desks shelf shelves shelving bed beds sofa couch cabinet cabinets bench benches "
         "locker lockers bookcase dresser stool counter",
         48, "furniture"},
        {"sign", "sign signs poster posters banner banners billboard plaque", 0, ""},
        {"debris", "debris trash rubble garbage litter junk scrap bottle bottles can cans", 0, ""},
        {"foliage", "foliage tree trees bush bushes shrub shrubs plant plants grass hedge flower flowers palm", 0, ""},
    };
    return t;
}

std::vector<std::string> words(const char* s) {
    std::vector<std::string> w;
    std::istringstream in(s ? s : "");
    std::string t;
    while (in >> t) w.push_back(t);
    return w;
}

bool hasWord(const char* list, const std::string& w) {
    for (const auto& t : words(list))
        if (t == w) return true;
    return false;
}

bool tokenMatches(const std::string& tok, const std::string& kw) {
    if (tok == kw || tok == kw + "s") return true;
    return kw.size() >= 5 && tok.compare(0, kw.size(), kw) == 0;
}

const StockPrefabRule* findRule(const std::string& category) {
    for (const auto& r : ruleTable())
        if (category == r.category) return &r;
    return nullptr;
}

std::string lastSegment(const std::string& model) {
    const auto p = model.find_last_of("/\\");
    return p == std::string::npos ? model : model.substr(p + 1);
}

// Per-model facts used by the harvest; computed once per distinct model.
struct ModelInfo {
    bool ok = false;
    std::string primary;                // "" when uncategorised
    std::vector<std::string> cats;      // every matching category, table order
    std::string family;
    const StockPrefabRule* rule = nullptr;
};

ModelInfo makeInfo(const std::string& model, const std::set<std::string>& okModels) {
    ModelInfo mi;
    mi.ok = okModels.empty() || okModels.count(model) > 0;
    mi.cats = stockPrefabCategories(model);
    mi.primary = mi.cats.empty() ? std::string() : mi.cats.front();
    mi.family = stockModelFamily(model);
    if (!mi.primary.empty()) mi.rule = findRule(mi.primary);
    return mi;
}

// An instance joins the cluster of an anchor when it shares the anchor's family or is attached-looking.
bool attachedTo(const ModelInfo& anchor, const ModelInfo& child) {
    if (!anchor.family.empty() && child.family == anchor.family) return true;
    for (const auto& c : child.cats) {
        if (c == "frame" || c == "trim" || c == "handle" || c == "glass") return true;
        if (anchor.rule && hasWord(anchor.rule->extra, c)) return true;
    }
    return false;
}

std::pair<long long, long long> cellOf(const Vec3& p) {
    return {static_cast<long long>(std::floor(p.x / kGridCell)), static_cast<long long>(std::floor(p.y / kGridCell))};
}

std::string multisetKey(const std::vector<StockModelInstance>& members) {
    std::vector<std::string> names;
    names.reserve(members.size());
    for (const auto& m : members) names.push_back(m.model);
    std::sort(names.begin(), names.end());
    std::string key;
    for (const auto& n : names) key += n + "|";
    return key;
}

bool closeInstance(const StockModelInstance& a, const StockModelInstance& b) {
    if (a.model != b.model) return false;
    if (std::fabs(a.origin.x - b.origin.x) > kPosTol || std::fabs(a.origin.y - b.origin.y) > kPosTol ||
        std::fabs(a.origin.z - b.origin.z) > kPosTol)
        return false;
    if (std::fabs(a.scale - b.scale) > kScaleTol) return false;
    const Vec3 aa = axisToAngles(a.axis), ba = axisToAngles(b.axis);
    auto angleDiff = [](double x, double y) {
        double d = std::fmod(std::fabs(x - y), 360.0);
        return std::min(d, 360.0 - d);
    };
    return angleDiff(aa.x, ba.x) <= kAngTol && angleDiff(aa.y, ba.y) <= kAngTol && angleDiff(aa.z, ba.z) <= kAngTol;
}

// One deduplicated cluster while the zones are read.
struct Group {
    std::string category;
    std::string anchorModel;
    std::vector<StockModelInstance> members;
    size_t count = 0;
    std::map<std::string, size_t> zones;
};

std::string joinNames(const std::vector<StockModelInstance>& members, const char* sep) {
    std::string s;
    for (size_t i = 0; i < members.size(); ++i) {
        if (i) s += sep;
        s += members[i].model;
    }
    return s;
}

std::string tsvField(std::string s) {
    for (auto& c : s)
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return s;
}

}  // namespace

const std::vector<StockPrefabRule>& stockPrefabRules() { return ruleTable(); }

std::vector<std::string> stockModelTokens(const std::string& model) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : lastSegment(model)) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<std::string> stockPrefabCategories(const std::string& model) {
    const auto tokens = stockModelTokens(model);
    std::vector<std::string> cats;
    for (const auto& r : ruleTable()) {
        bool hit = false;
        for (const auto& kw : words(r.keywords)) {
            for (const auto& t : tokens)
                if (tokenMatches(t, kw)) {
                    hit = true;
                    break;
                }
            if (hit) break;
        }
        if (hit) cats.push_back(r.category);
    }
    return cats;
}

std::string stockPrefabCategory(const std::string& model) {
    const auto cats = stockPrefabCategories(model);
    return cats.empty() ? std::string() : cats.front();
}

std::string stockModelFamily(const std::string& model) {
    const auto tokens = stockModelTokens(model);
    if (tokens.empty()) return {};
    if (tokens.size() == 1) return tokens[0];
    return tokens[0] + "_" + tokens[1];
}

std::string stockPrefabSafeName(const std::string& model) {
    std::string s;
    for (char ch : model) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u)) s += static_cast<char>(std::tolower(u));
        else s += '_';
    }
    while (!s.empty() && s.front() == '_') s.erase(s.begin());
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? std::string("model") : s;
}

std::vector<StockModelInstance> stockClusterRelative(const std::vector<StockModelInstance>& instances, size_t anchor,
                                                     const std::vector<size_t>& members) {
    const Vec3 anchorYpr = axisToAngles(instances[anchor].axis);
    const double th = anchorYpr.y * kPi / 180.0;
    const double c = std::cos(th), s = std::sin(th);
    // Rotation by minus the anchor yaw about Z: x' = x c + y s, y' = -x s + y c.
    auto rot = [&](const Vec3& v) { return Vec3{v.x * c + v.y * s, -v.x * s + v.y * c, v.z}; };
    const Vec3 o = instances[anchor].origin;
    std::vector<StockModelInstance> out;
    out.reserve(members.size());
    for (size_t m : members) {
        const StockModelInstance& in = instances[m];
        StockModelInstance r;
        r.model = in.model;
        r.origin = rot(in.origin - o);
        for (int k = 0; k < 3; ++k) r.axis[k] = rot(in.axis[k]);
        r.scale = in.scale;
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [](const StockModelInstance& a, const StockModelInstance& b) {
        if (a.model != b.model) return a.model < b.model;
        if (a.origin.x != b.origin.x) return a.origin.x < b.origin.x;
        if (a.origin.y != b.origin.y) return a.origin.y < b.origin.y;
        return a.origin.z < b.origin.z;
    });
    return out;
}

bool sameStockPrefab(const std::vector<StockModelInstance>& a, const std::vector<StockModelInstance>& b) {
    if (a.size() != b.size()) return false;
    std::vector<char> used(b.size(), 0);
    for (const auto& ma : a) {
        bool hit = false;
        for (size_t j = 0; j < b.size(); ++j) {
            if (used[j] || !closeInstance(ma, b[j])) continue;
            used[j] = 1;
            hit = true;
            break;
        }
        if (!hit) return false;
    }
    return true;
}

std::string stockPrefabMapText(const std::vector<StockModelInstance>& members) {
    std::string t;
    t += "iwmap 4\r\n";
    t += "\"000_Global\" flags active\r\n";
    t += "\"The Map\" flags \r\n";
    t += "// entity 0\r\n{\r\n\"classname\" \"worldspawn\"\r\n}\r\n";
    int n = 1;
    for (const auto& m : members) {
        t += "// entity " + std::to_string(n++) + "\r\n{\r\n";
        for (const auto& kv : stockModelInstancesAsEntities(m)) t += "\"" + kv.first + "\" \"" + kv.second + "\"\r\n";
        t += "}\r\n";
    }
    return t;
}

StockPrefabLibrary harvestStockPrefabs(const std::vector<StockZone>& zones, const StockPrefabOptions& opt) {
    StockPrefabLibrary lib;
    lib.options = opt;
    std::unordered_map<std::string, ModelInfo> infos;
    auto infoOf = [&](const std::string& model) -> const ModelInfo& {
        auto it = infos.find(model);
        if (it == infos.end()) it = infos.emplace(model, makeInfo(model, opt.okModels)).first;
        return it->second;
    };

    std::map<std::string, size_t> singleCount;
    std::map<std::string, std::map<std::string, size_t>> singleZones;
    std::map<std::string, std::vector<Group>> buckets;

    for (const auto& zone : zones) {
        const auto& ins = zone.instances;
        lib.zoneInstances[zone.name] = ins.size();
        std::vector<const ModelInfo*> mi(ins.size());
        std::map<std::pair<long long, long long>, std::vector<size_t>> grid;
        for (size_t i = 0; i < ins.size(); ++i) {
            mi[i] = &infoOf(ins[i].model);
            if (mi[i]->ok) grid[cellOf(ins[i].origin)].push_back(i);
        }
        for (size_t i = 0; i < ins.size(); ++i) {
            ++lib.instancesSeen;
            const ModelInfo& a = *mi[i];
            if (!a.ok) {
                ++lib.instancesSkipped;
                continue;
            }
            ++singleCount[ins[i].model];
            ++singleZones[ins[i].model][zone.name];
            if (a.primary.empty()) {
                ++lib.instancesUncategorised;
                continue;
            }
            if (!a.rule || a.rule->radius <= 0) continue;

            std::vector<size_t> members{i};
            const auto c = cellOf(ins[i].origin);
            for (long long dx = -1; dx <= 1; ++dx) {
                for (long long dy = -1; dy <= 1; ++dy) {
                    auto it = grid.find({c.first + dx, c.second + dy});
                    if (it == grid.end()) continue;
                    for (size_t j : it->second) {
                        if (j == i) continue;
                        const Vec3 d = ins[j].origin - ins[i].origin;
                        if (std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) > a.rule->radius) continue;
                        if (attachedTo(a, *mi[j])) members.push_back(j);
                    }
                }
            }
            if (members.size() < 2) continue;

            auto rel = stockClusterRelative(ins, i, members);
            auto& bucket = buckets[multisetKey(rel)];
            Group* hit = nullptr;
            for (auto& g : bucket) {
                if (sameStockPrefab(g.members, rel)) {
                    hit = &g;
                    break;
                }
            }
            if (hit) {
                ++hit->count;
                ++hit->zones[zone.name];
            } else {
                Group g;
                g.category = a.primary;
                g.anchorModel = ins[i].model;
                g.members = std::move(rel);
                g.count = 1;
                g.zones[zone.name] = 1;
                bucket.push_back(std::move(g));
            }
        }
    }

    // Candidates per category: singles first (one per ok model), then the deduplicated clusters.
    std::map<std::string, std::vector<StockPrefab>> perCat;
    for (const auto& [model, n] : singleCount) {
        const ModelInfo mi = infoOf(model);
        if (!mi.ok || mi.primary.empty() || n < opt.minCount) continue;
        StockPrefab p;
        p.kind = "single";
        p.category = mi.primary;
        p.anchorModel = model;
        p.name = stockPrefabSafeName(model) + "_single";
        StockModelInstance one;
        one.model = model;
        p.members = {one};
        p.count = n;
        p.zones = singleZones[model];
        perCat[p.category].push_back(std::move(p));
    }
    for (const auto& kv : buckets) {
        for (const auto& g : kv.second) {
            if (g.count < opt.minCount) continue;
            StockPrefab p;
            p.kind = "cluster";
            p.category = g.category;
            p.anchorModel = g.anchorModel;
            p.members = g.members;
            p.count = g.count;
            p.zones = g.zones;
            perCat[p.category].push_back(std::move(p));
        }
    }

    for (auto& [cat, list] : perCat) {
        lib.candidatesPerCategory[cat] = list.size();
        std::stable_sort(list.begin(), list.end(), [](const StockPrefab& a, const StockPrefab& b) {
            if (a.count != b.count) return a.count > b.count;
            if (a.anchorModel != b.anchorModel) return a.anchorModel < b.anchorModel;
            return a.kind < b.kind;
        });
        const size_t keep = std::min(list.size(), opt.maxPerCategory);
        lib.droppedPerCategory[cat] = list.size() - keep;
        std::map<std::string, int> variant;
        for (size_t k = 0; k < keep; ++k) {
            StockPrefab& p = list[k];
            if (p.kind == "cluster") {
                const int v = ++variant[p.anchorModel];
                char buf[32];
                std::snprintf(buf, sizeof buf, "_v%02d", v);
                p.name = stockPrefabSafeName(p.anchorModel) + buf;
            }
            p.relPath = cat + "/" + p.name + ".map";
            lib.prefabs.push_back(std::move(p));
        }
    }
    std::sort(lib.prefabs.begin(), lib.prefabs.end(), [](const StockPrefab& a, const StockPrefab& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
    return lib;
}

bool readStockWorldCache(const std::string& cacheDir, std::vector<StockZone>& out, std::vector<std::string>* warnings,
                         std::string* err) {
    std::error_code ec;
    if (!fs::is_directory(cacheDir, ec)) {
        if (err) *err = "not a folder: " + cacheDir;
        return false;
    }
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(cacheDir, ec)) {
        std::error_code e2;
        if (e.is_directory(e2)) dirs.push_back(e.path());
    }
    if (ec) {
        if (err) *err = "cannot list " + cacheDir;
        return false;
    }
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const auto& d : dirs) {
        const std::string name = d.filename().string();
        std::error_code e3;
        if (!fs::is_directory(d / "assets" / "gfx_map", e3)) {
            if (warnings) warnings->push_back(name + ": no assets/gfx_map export, skipped");
            continue;
        }
        StockZone z;
        z.name = name;
        std::string e;
        if (!readStockModelInstances(d.string(), z.instances, &e)) {
            if (warnings) warnings->push_back(name + ": " + e + ", skipped");
            continue;
        }
        out.push_back(std::move(z));
    }
    return true;
}

bool loadStockModelStatus(const std::string& tsvPath, std::set<std::string>& ok, std::string* err) {
    std::ifstream in(tsvPath);
    if (!in) {
        if (err) *err = "cannot read " + tsvPath;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const auto t2 = line.find('\t', t1 + 1);
        const std::string status = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
        if (status == "ok") ok.insert(line.substr(0, t1));
    }
    return true;
}

bool writeStockPrefabLibrary(const std::string& outDir, const StockPrefabLibrary& lib, std::string* err,
                             StockWriteStats* stats) {
    std::error_code ec;
    const fs::path root(outDir);
    fs::create_directories(root, ec);
    if (ec) {
        if (err) *err = "cannot create " + outDir;
        return false;
    }
    if (fs::exists(root / "index.tsv", ec)) {
        // An earlier harvest owns these folders: drop its .map files so no stale prefab survives a smaller run.
        for (const auto& d : fs::directory_iterator(root, ec)) {
            std::error_code e2;
            if (!d.is_directory(e2)) continue;
            for (const auto& f : fs::directory_iterator(d.path(), e2)) {
                std::error_code e3;
                if (f.is_regular_file(e3) && f.path().extension() == ".map") fs::remove(f.path(), e3);
            }
        }
    }

    StockWriteStats st;
    for (const auto& p : lib.prefabs) {
        fs::create_directories(root / p.category, ec);
        const std::string text = stockPrefabMapText(p.members);
        if (!writeFileAtomic((root / p.relPath).string(), text, err)) return false;
        ++st.files;
        st.bytes += text.size();
        st.bytesPerCategory[p.category] += text.size();
    }

    std::string index = "path\tcategory\tanchor_model\tmodels\tcount\tsource_zones\n";
    for (const auto& p : lib.prefabs) {
        std::string zones;
        for (const auto& [z, n] : p.zones) {
            if (!zones.empty()) zones += ';';
            zones += tsvField(z) + ":" + std::to_string(n);
        }
        index += tsvField(p.relPath) + "\t" + tsvField(p.category) + "\t" + tsvField(p.anchorModel) + "\t" +
                 tsvField(joinNames(p.members, ";")) + "\t" + std::to_string(p.count) + "\t" + zones + "\n";
    }
    if (!writeFileAtomic((root / "index.tsv").string(), index, err)) return false;

    // Per-category counts.
    struct CatRow {
        size_t singles = 0, clusters = 0;
    };
    std::map<std::string, CatRow> rows;
    for (const auto& p : lib.prefabs) {
        if (p.kind == "single") ++rows[p.category].singles;
        else ++rows[p.category].clusters;
    }
    std::vector<const StockPrefab*> top;
    for (const auto& p : lib.prefabs)
        if (p.kind == "cluster") top.push_back(&p);
    std::stable_sort(top.begin(), top.end(), [](const StockPrefab* a, const StockPrefab* b) { return a->count > b->count; });

    std::ostringstream md;
    md << "# Stock prefab library\n\n";
    md << "Harvested from the shipped IW8 maps (world_cache exports, IW8 only) by `iw8r stock-prefabs`.\n\n";
    md << "## How it was built\n\n";
    md << "- Placements come from the gfx_map smodels of each zone (src/core/stock_smodels.h); only models marked `ok` in\n";
    md << "  data/asset_catalog/iw8_models_status.tsv are used.\n";
    md << "- Anchor: an instance whose model matches a category keyword (src/core/stock_prefabs.cpp, category table).\n";
    md << "- Cluster: the anchor plus the ok instances within the category radius that are attached-looking (same family\n";
    md << "  prefix as the anchor, a frame / trim / handle / glass model, or an extra category of the table). Radius 0 =\n";
    md << "  no clusters for that category.\n";
    md << "- Relative placement: anchor origin at (0,0,0), anchor yaw removed (anchor faces +X), member pitch and roll kept.\n";
    md << "- Dedup: the same multiset of models with placements within 1 inch and 1 degree is one prefab; `count` is the\n";
    md << "  number of anchors that produced it and `source_zones` the per-zone counts.\n";
    md << "- Singles: one misc_model at the origin per ok model used in a category (`*_single.map`).\n";
    md << "- Format: iwmap 4 with an empty worldspawn and misc_model entities (layout of tests/data/iw8_sample_room.map).\n\n";
    md << "Parameters: min-count " << lib.options.minCount << ", max-per-category " << lib.options.maxPerCategory << ".\n\n";
    md << "## Zones\n\n";
    for (const auto& [z, n] : lib.zoneInstances) md << "- " << z << ": " << n << " instances\n";
    md << "\nInstances seen: " << lib.instancesSeen << " (not ok: " << lib.instancesSkipped
       << ", no category: " << lib.instancesUncategorised << ").\n\n";
    md << "## Counts per category\n\n";
    md << "| category | singles | clusters | candidates | dropped by cap | bytes |\n|---|---:|---:|---:|---:|---:|\n";
    std::map<std::string, bool> cats;
    for (const auto& [c, _] : lib.candidatesPerCategory) cats[c] = true;
    for (const auto& [c, _] : rows) cats[c] = true;
    size_t totSingle = 0, totCluster = 0, totCand = 0, totDrop = 0;
    for (const auto& [c, _] : cats) {
        const CatRow r = rows[c];
        const size_t cand = lib.candidatesPerCategory.count(c) ? lib.candidatesPerCategory.at(c) : 0;
        const size_t drop = lib.droppedPerCategory.count(c) ? lib.droppedPerCategory.at(c) : 0;
        const size_t bytes = st.bytesPerCategory.count(c) ? st.bytesPerCategory.at(c) : 0;
        md << "| " << c << " | " << r.singles << " | " << r.clusters << " | " << cand << " | " << drop << " | " << bytes
           << " |\n";
        totSingle += r.singles;
        totCluster += r.clusters;
        totCand += cand;
        totDrop += drop;
    }
    md << "| **total** | " << totSingle << " | " << totCluster << " | " << totCand << " | " << totDrop << " | " << st.bytes
       << " |\n\n";
    md << "## Examples (largest clusters)\n\n";
    for (size_t k = 0; k < top.size() && k < 8; ++k) {
        md << "- `" << top[k]->relPath << "`: anchor `" << top[k]->anchorModel << "`, " << top[k]->members.size()
           << " pieces, seen " << top[k]->count << "x\n";
    }
    md << "\nFiles: " << st.files << " .map prefabs, index in index.tsv.\n";
    if (!writeFileAtomic((root / "README.md").string(), md.str(), err)) return false;

    if (stats) *stats = st;
    return true;
}

}  // namespace iw8r
