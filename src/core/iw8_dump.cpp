#include "iw8_dump.h"

#include <cstring>

namespace iw8r {

namespace {
constexpr uint32_t kMagic = 0x44385749;  // "IW8D"
constexpr uint32_t kVersion = 1;
constexpr size_t kMapEntsHeaderSize = 1032;
constexpr size_t kNumEntityCharsOffset = 16;

template <class T>
bool take(const std::string& s, size_t& i, T& v) {
    if (i + sizeof(T) > s.size()) return false;
    std::memcpy(&v, s.data() + i, sizeof(T));
    i += sizeof(T);
    return true;
}
template <class T>
void put(std::string& s, const T& v) {
    s.append(reinterpret_cast<const char*>(&v), sizeof(T));
}
}  // namespace

bool parseIw8Dump(const std::string& f, Iw8Dump& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    size_t i = 0;
    uint32_t magic = 0, version = 0, count = 0;
    int32_t type = 0;
    if (!take(f, i, magic) || !take(f, i, version) || !take(f, i, type) || !take(f, i, count)) return fail("truncated header");
    if (magic != kMagic) return fail("not an IW8D dump");
    if (version != kVersion) return fail("unsupported IW8D version " + std::to_string(version));
    out = {};
    out.type = type;
    out.records.reserve(count);
    for (uint32_t n = 0; n < count; ++n) {
        Iw8DumpRecord r;
        uint8_t kind = 0;
        if (!take(f, i, kind) || kind > Iw8DumpRecord::Temp) return fail("bad record kind at record " + std::to_string(n));
        r.kind = Iw8DumpRecord::Kind(kind);
        switch (r.kind) {
            case Iw8DumpRecord::Null:
                break;
            case Iw8DumpRecord::Data:
            case Iw8DumpRecord::String:
            case Iw8DumpRecord::Asset:
            case Iw8DumpRecord::ScriptString: {
                uint64_t len = 0;
                if (!take(f, i, r.value) || !take(f, i, len) || len > f.size() - i) return fail("truncated record " + std::to_string(n));
                r.bytes.assign(f.data() + i, size_t(len));
                i += size_t(len);
                break;
            }
            case Iw8DumpRecord::Runtime:
            case Iw8DumpRecord::Temp:
                if (!take(f, i, r.value)) return fail("truncated record " + std::to_string(n));
                break;
            case Iw8DumpRecord::Offset:
                if (!take(f, i, r.value) || !take(f, i, r.delta)) return fail("truncated record " + std::to_string(n));
                break;
        }
        out.records.push_back(std::move(r));
    }
    if (i != f.size()) return fail("trailing bytes after the last record");
    return true;
}

std::string serializeIw8Dump(const Iw8Dump& d) {
    std::string s;
    put(s, kMagic);
    put(s, kVersion);
    put(s, d.type);
    put(s, uint32_t(d.records.size()));
    for (const auto& r : d.records) {
        put(s, uint8_t(r.kind));
        switch (r.kind) {
            case Iw8DumpRecord::Null:
                break;
            case Iw8DumpRecord::Data:
            case Iw8DumpRecord::String:
            case Iw8DumpRecord::Asset:
            case Iw8DumpRecord::ScriptString:
                put(s, r.value);
                put(s, uint64_t(r.bytes.size()));
                s += r.bytes;
                break;
            case Iw8DumpRecord::Runtime:
            case Iw8DumpRecord::Temp:
                put(s, r.value);
                break;
            case Iw8DumpRecord::Offset:
                put(s, r.value);
                put(s, r.delta);
                break;
        }
    }
    return s;
}

namespace {
bool checkMapEnts(const Iw8Dump& d, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (d.type != kIw8MapEntsType) return fail("asset type " + std::to_string(d.type) + " is not map_ents (29)");
    if (d.records.size() < 3 || d.records[0].kind != Iw8DumpRecord::Data || d.records[0].bytes.size() != kMapEntsHeaderSize)
        return fail("record 0 is not the 1032-byte MapEnts header");
    if (d.records[2].kind != Iw8DumpRecord::Data) return fail("record 2 is not the entity string data record");
    uint32_t chars = 0;
    std::memcpy(&chars, d.records[0].bytes.data() + kNumEntityCharsOffset, sizeof(chars));
    if (chars != d.records[2].bytes.size())
        return fail("numEntityChars (" + std::to_string(chars) + ") does not match the entity string record (" +
                    std::to_string(d.records[2].bytes.size()) + ")");
    return true;
}
}  // namespace

bool mapEntsEntityString(const Iw8Dump& d, std::string& out, std::string* err) {
    if (!checkMapEnts(d, err)) return false;
    out = d.records[2].bytes;
    return true;
}

bool setMapEntsEntityString(Iw8Dump& d, const std::string& entityString, std::string* err) {
    if (!checkMapEnts(d, err)) return false;
    std::string s = entityString;
    if (s.empty() || s.back() != '\0') s.push_back('\0');  // the game reads a C string; numEntityChars counts the NUL
    if (s.size() > 0xFFFFFFFFull) {
        if (err) *err = "entity string too large";
        return false;
    }
    const uint32_t chars = uint32_t(s.size());
    d.records[2].bytes = std::move(s);
    std::memcpy(d.records[0].bytes.data() + kNumEntityCharsOffset, &chars, sizeof(chars));
    return true;
}

}  // namespace iw8r
