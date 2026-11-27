#include "spawnlist.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "document.h"
#include "map_text.h"

namespace iw8r {

namespace {

constexpr size_t kSpawnSize = 40;
constexpr size_t kHeaderSize = 1032;
constexpr size_t kCountOffset = 0x128;  // MapEnts.spawnList.spawnsCount (u16; the next 16 bits are zero)
constexpr size_t kPaddingOffset = 2;
constexpr size_t kNameOffset = 4, kTargetOffset = 8, kNoteworthyOffset = 12, kOriginOffset = 16, kAnglesOffset = 28;
constexpr uint32_t kKeyClassname = 212, kKeyTarget = 1070, kKeyNoteworthy = 845, kKeyOrigin = 709, kKeyAngles = 80,
                   kKeyTargetname = 1072;

template <class T>
T load(const std::string& s, size_t at) {
    T v{};
    std::memcpy(&v, s.data() + at, sizeof(T));
    return v;
}
template <class T>
void store(std::string& s, size_t at, const T& v) {
    std::memcpy(&s[at], &v, sizeof(T));
}

Vec3 loadVec(const std::string& s, size_t at) {
    return {double(load<float>(s, at)), double(load<float>(s, at + 4)), double(load<float>(s, at + 8))};
}
void storeVec(std::string& s, size_t at, const Vec3& v) {
    store<float>(s, at, float(v.x));
    store<float>(s, at + 4, float(v.y));
    store<float>(s, at + 8, float(v.z));
}

bool headerOk(const Iw8Dump& d, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (d.type != kIw8MapEntsType) return fail("asset type " + std::to_string(d.type) + " is not map_ents (29)");
    if (d.records.empty() || d.records[0].kind != Iw8DumpRecord::Data || d.records[0].bytes.size() != kHeaderSize)
        return fail("record 0 is not the 1032-byte MapEnts header");
    return true;
}

uint32_t headerCount(const Iw8Dump& d) { return load<uint32_t>(d.records[0].bytes, kCountOffset); }

bool isScriptRecord(const Iw8DumpRecord& r) {
    return r.kind == Iw8DumpRecord::ScriptString || r.kind == Iw8DumpRecord::Null;
}

std::string scriptText(const Iw8DumpRecord& r) { return r.kind == Iw8DumpRecord::ScriptString ? r.bytes : std::string(); }

// The spawn array is the first data record of 40 * n bytes whose entries carry their own position, followed by
// n name / target / noteworthy script records each. Returns false when no such block exists.
bool findBlock(const Iw8Dump& d, uint32_t n, size_t& first) {
    const auto& recs = d.records;
    for (size_t s = 1; s < recs.size(); ++s) {
        if (recs[s].kind != Iw8DumpRecord::Data || recs[s].bytes.size() != kSpawnSize * n) continue;
        if (s + 3 * size_t(n) >= recs.size()) continue;
        bool ok = true;
        for (size_t k = s + 1; k <= s + 3 * size_t(n) && ok; ++k) ok = isScriptRecord(recs[k]);
        for (uint32_t i = 0; i < n && ok; ++i) ok = load<uint16_t>(recs[s].bytes, kSpawnSize * i) == i;
        if (ok) {
            first = s;
            return true;
        }
    }
    return false;
}

// Walks the MapEnts IW8D record stream up to MapEnts::spawnList.spawns (asset_streams.cpp stream_mapents).
// When spawnsCount == 0, stream_spawnpointrecordlist emits a single Null (or 0-byte Data) record at that slot.
bool findEmptySpawnSlot(const Iw8Dump& d, size_t& slot) {
    const auto& recs = d.records;
    const std::string& hdr = recs[0].bytes;
    const uint32_t triggerCount = load<uint32_t>(hdr, 0x18);
    const uint32_t clientTriggerCount = load<uint32_t>(hdr, 0x68);
    size_t i = 1;
    auto take = [&](size_t count) -> bool {
        if (i + count > recs.size()) return false;
        i += count;
        return true;
    };
    auto isPresent = [&](size_t at) -> bool {
        return recs[at].kind == Iw8DumpRecord::Data || recs[at].kind == Iw8DumpRecord::Runtime;
    };
    // 1. name, 2. entityString
    if (!take(2)) return false;
    // 3. stream_maptriggers: models (+ triggerCount physicsAsset refs if present), hulls, slabs, windings, windingPoints
    if (i >= recs.size()) return false;
    const bool hasTrigModels = isPresent(i);
    if (!take(1)) return false;
    if (hasTrigModels && triggerCount != 0 && !take(triggerCount)) return false;
    if (!take(4)) return false;
    // 4. stream_clienttriggers: ClientMapTriggers (3), triggerString (1), 9 per-trigger inline arrays
    if (!take(3 + 1 + 9)) return false;
    //    detailSoundBank (+ clientTriggerCount transient bank refs if present)
    if (i >= recs.size()) return false;
    const bool hasDetailBanks = isPresent(i);
    if (!take(1)) return false;
    if (hasDetailBanks && clientTriggerCount != 0 && !take(clientTriggerCount)) return false;
    //    audioRvbPanInfo, parent
    if (!take(2)) return false;
    //    linkTo (+ clientTriggerCount ClientEntityLinkToDef* records if present)
    if (i >= recs.size()) return false;
    const bool hasLinkTo = isPresent(i);
    if (!take(1)) return false;
    if (hasLinkTo && clientTriggerCount != 0 && !take(clientTriggerCount)) return false;
    //    spatialTree (+ tree & alwaysList if present)
    if (i >= recs.size()) return false;
    const bool hasSpatial = isPresent(i);
    if (!take(1)) return false;
    if (hasSpatial && !take(2)) return false;
    // 5. stream_clienttriggerblend: blendNodes
    if (!take(1)) return false;
    // 6. stream_spawnpointrecordlist: spawns pointer record (Null or 0-byte Data)
    if (i >= recs.size()) return false;
    if (recs[i].kind != Iw8DumpRecord::Null && !(recs[i].kind == Iw8DumpRecord::Data && recs[i].bytes.empty())) return false;
    slot = i;
    return true;
}

bool sameFields(const SpawnRecord& a, const SpawnRecord& b) {
    return a.classname == b.classname && a.target == b.target && a.script_noteworthy == b.script_noteworthy &&
           a.origin == b.origin && a.angles == b.angles && a.padding == b.padding;
}

bool finiteVec(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

bool readSpawnList(const Iw8Dump& d, std::vector<SpawnRecord>& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    if (!headerOk(d, err)) return false;
    const uint32_t n = headerCount(d);
    if (n == 0) return true;
    if (n > 0xFFFF) return fail("spawnsCount " + std::to_string(n) + " exceeds 65535");
    size_t first = 0;
    if (!findBlock(d, n, first)) return fail("spawnList block not found (count " + std::to_string(n) + ")");
    const std::string& data = d.records[first].bytes;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        const size_t at = kSpawnSize * i;
        const size_t rec = first + 1 + 3 * size_t(i);
        SpawnRecord s;
        s.index = load<uint16_t>(data, at);
        s.padding = load<uint16_t>(data, at + kPaddingOffset);
        s.origin = loadVec(data, at + kOriginOffset);
        s.angles = loadVec(data, at + kAnglesOffset);
        s.classname = scriptText(d.records[rec]);
        s.target = scriptText(d.records[rec + 1]);
        s.script_noteworthy = scriptText(d.records[rec + 2]);
        out.push_back(std::move(s));
    }
    return true;
}

bool writeSpawnList(Iw8Dump& d, const std::vector<SpawnRecord>& spawns, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (!headerOk(d, err)) return false;
    const uint32_t n = headerCount(d);
    if (n > 0xFFFF) return fail("spawnsCount " + std::to_string(n) + " exceeds 65535");
    const size_t m = spawns.size();
    if (m > 0xFFFF) return fail("too many spawns (" + std::to_string(m) + ", the limit is 65535)");
    for (size_t i = 0; i < m; ++i) {
        const SpawnRecord& s = spawns[i];
        if (s.classname.empty()) return fail("spawn " + std::to_string(i) + " has no classname");
        for (const std::string* t : {&s.classname, &s.target, &s.script_noteworthy})
            if (t->find('\0') != std::string::npos) return fail("spawn " + std::to_string(i) + " has a NUL in a string");
        if (!finiteVec(s.origin) || !finiteVec(s.angles)) return fail("spawn " + std::to_string(i) + " has a non-finite vector");
    }
    if (n == 0 && m == 0) return true;

    std::vector<SpawnRecord> old;
    size_t first = 0, oldEnd = 0;
    if (n == 0) {
        if (!findEmptySpawnSlot(d, first))
            return fail("the dump's spawnList is empty and its record slot could not be located");
        oldEnd = first + 1;
    } else {
        if (!readSpawnList(d, old, err)) return false;
        if (!findBlock(d, n, first)) return fail("spawnList block not found (count " + std::to_string(n) + ")");
        oldEnd = first + 1 + 3 * size_t(n);  // first record after the block
    }

    // Offsets only point backwards, and nothing points into the spawn array itself.
    for (const auto& r : d.records)
        if (r.kind == Iw8DumpRecord::Offset && r.value >= first && r.value < oldEnd)
            return fail("an offset record points into the spawnList block");

    // The new block: one Null record when m == 0, else one data record + three script records per spawn.
    std::vector<Iw8DumpRecord> block;
    if (m == 0) {
        Iw8DumpRecord nullRec;
        nullRec.kind = Iw8DumpRecord::Null;
        block.push_back(std::move(nullRec));
    } else {
        std::string data(kSpawnSize * m, '\0');
        block.reserve(1 + 3 * m);
        block.emplace_back();
        block.back().kind = Iw8DumpRecord::Data;
        for (size_t i = 0; i < m; ++i) {
            const SpawnRecord& s = spawns[i];
            const size_t at = kSpawnSize * i;
            const bool untouched = i < n && old[i].index == i && sameFields(s, old[i]);
            if (untouched) {
                std::memcpy(&data[at], d.records[first].bytes.data() + at, kSpawnSize);
                for (size_t k = 0; k < 3; ++k) block.push_back(d.records[first + 1 + 3 * i + k]);
                continue;
            }
            store<uint16_t>(data, at, uint16_t(i));
            store<uint16_t>(data, at + kPaddingOffset, s.padding);
            store<uint32_t>(data, at + kNameOffset, 0);  // script ids are zero in the dump; the text is in the script records
            store<uint32_t>(data, at + kTargetOffset, 0);
            store<uint32_t>(data, at + kNoteworthyOffset, 0);
            storeVec(data, at + kOriginOffset, s.origin);
            storeVec(data, at + kAnglesOffset, s.angles);
            for (const std::string* t : {&s.classname, &s.target, &s.script_noteworthy}) {
                Iw8DumpRecord r;
                r.kind = Iw8DumpRecord::ScriptString;
                r.bytes = *t;
                block.push_back(std::move(r));
            }
        }
        block[0].bytes = std::move(data);
    }

    // Assemble the new record list. Offsets behind the block move by the change in block size.
    const long long shift = (long long)block.size() - (long long)(oldEnd - first);
    std::vector<Iw8DumpRecord> recs;
    recs.reserve(size_t((long long)d.records.size() + shift));
    for (size_t k = 0; k < first; ++k) recs.push_back(d.records[k]);
    for (auto& b : block) recs.push_back(std::move(b));
    for (size_t k = oldEnd; k < d.records.size(); ++k) {
        Iw8DumpRecord r = d.records[k];
        if (r.kind == Iw8DumpRecord::Offset && r.value >= oldEnd) r.value = uint64_t((long long)r.value + shift);
        recs.push_back(std::move(r));
    }
    // The header carries the count (u16 at +0x128; the high bits stay zero).
    std::string header = recs[0].bytes;
    store<uint32_t>(header, kCountOffset, uint32_t(m));
    recs[0].bytes = std::move(header);

    d.records = std::move(recs);
    return true;
}

std::string spawnsToEntityText(const std::vector<SpawnRecord>& spawns) {
    auto num = [](double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.9g", v);  // 9 significant digits round-trip a float exactly
        return std::string(buf);
    };
    auto vec = [&](const Vec3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); };
    std::string out;
    for (const auto& s : spawns) {
        out += "{\n";
        out += std::to_string(kKeyClassname) + " \"" + s.classname + "\"\n";
        if (!s.targetname.empty()) out += std::to_string(kKeyTargetname) + " \"" + s.targetname + "\"\n";
        if (!s.target.empty()) out += std::to_string(kKeyTarget) + " \"" + s.target + "\"\n";
        if (!s.script_noteworthy.empty()) out += std::to_string(kKeyNoteworthy) + " \"" + s.script_noteworthy + "\"\n";
        out += std::to_string(kKeyOrigin) + " \"" + vec(s.origin) + "\"\n";
        out += std::to_string(kKeyAngles) + " \"" + vec(s.angles) + "\"\n";
        out += "}\n";
    }
    return out;
}

bool spawnsFromEntityText(const std::string& text, std::vector<SpawnRecord>& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    Document doc;
    std::string perr;
    if (!doc.loadFromText(text, "spawns.ents", &perr)) return fail("entity text does not parse: " + perr);
    size_t idx = 0;
    for (const auto& e : doc.map().entities) {
        const std::string where = "entity " + std::to_string(idx++);
        SpawnRecord s;
        s.classname = e.classname();
        if (s.classname.empty()) return fail(where + " has no classname");
        auto str = [&](const char* key) {
            const std::string* v = e.value(key);
            return v ? *v : std::string();
        };
        s.targetname = str("targetname");
        s.target = str("target");
        s.script_noteworthy = str("script_noteworthy");
        const auto origin = e.origin();
        if (!origin) return fail(where + " (" + s.classname + ") has no origin");
        s.origin = *origin;
        if (const std::string* a = e.value("angles")) {
            std::istringstream in(*a);
            double v[3] = {0, 0, 0};
            for (double& x : v)
                if (!(in >> x)) return fail(where + " (" + s.classname + ") has a bad angles value");
            s.angles = {v[0], v[1], v[2]};
        }
        out.push_back(std::move(s));
    }
    return true;
}

}  // namespace iw8r
