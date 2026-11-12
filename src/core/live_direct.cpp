#include "live_direct.h"

#include <cmath>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace iw8r::live {

namespace {

template <class T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

void setErr(std::string* err, const std::string& why) {
    if (err) *err = why;
}

bool finiteVec(const float v[3], double limit) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(v[i]) || std::fabs(v[i]) > limit) return false;
    return true;
}

}  // namespace

bool parseRplMap(const uint8_t* data, size_t size, std::vector<RvaRange>& out, std::string* err) {
    out.clear();
    if (size < 24 || std::memcmp(data, "RPLYMAP2", 8) != 0) return setErr(err, "no RPLYMAP2 header"), false;
    const uint32_t version = rd<uint32_t>(data + 8), count = rd<uint32_t>(data + 12);
    if (version != 2 || count == 0 || count > 65536) return setErr(err, "bad map version or range count"), false;
    if (24 + uint64_t(count) * 12 > size) return setErr(err, "map ranges run past the section"), false;
    uint32_t end = 0;
    std::vector<RvaRange> ranges;
    ranges.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = data + 24 + size_t(i) * 12;
        RvaRange r{rd<uint32_t>(p), rd<uint32_t>(p + 4), rd<uint32_t>(p + 8)};
        if (r.begin != end || r.end <= r.begin || r.end > kNativeImageSize) return setErr(err, "map ranges are not contiguous"), false;
        end = r.end;
        ranges.push_back(r);
    }
    if (end != kNativeImageSize) return setErr(err, "map does not cover the native image"), false;
    out = std::move(ranges);
    return true;
}

uint64_t resolveRva(const std::vector<RvaRange>& ranges, uint64_t rva) {
    if (ranges.empty()) return rva;
    if (rva == kNativeImageSize) return ranges.back().mappedBegin + rva - ranges.back().begin;
    size_t first = 0, last = ranges.size();
    while (first < last) {
        const size_t i = first + (last - first) / 2;
        if (rva < ranges[i].begin) last = i;
        else if (rva >= ranges[i].end) first = i + 1;
        else return ranges[i].mappedBegin + rva - ranges[i].begin;
    }
    return rva;
}

DirectLink::~DirectLink() { detach(); }

void DirectLink::detach() {
#ifdef _WIN32
    if (process_) CloseHandle(static_cast<HANDLE>(process_));
#endif
    process_ = nullptr;
    pid_ = 0;
    base_ = 0;
    ranges_.clear();
}

bool DirectLink::read(uint64_t a, void* out, size_t n) const {
#ifdef _WIN32
    SIZE_T got = 0;
    return process_ && ReadProcessMemory(static_cast<HANDLE>(process_), reinterpret_cast<LPCVOID>(a), out, n, &got) && got == n;
#else
    (void)a, (void)out, (void)n;
    return false;
#endif
}

bool DirectLink::write(uint64_t a, const void* in, size_t n) {
#ifdef _WIN32
    SIZE_T put = 0;
    return process_ && WriteProcessMemory(static_cast<HANDLE>(process_), reinterpret_cast<LPVOID>(a), in, n, &put) && put == n;
#else
    (void)a, (void)in, (void)n;
    return false;
#endif
}

bool DirectLink::attach(uint32_t pid, std::string* err) {
    detach();
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return setErr(err, "cannot open process " + std::to_string(pid) + " (error " + std::to_string(GetLastError()) + ")"), false;
    HMODULE mods[1];
    DWORD needed = 0;
    if (!K32EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_64BIT) || needed < sizeof(HMODULE)) {
        CloseHandle(h);
        return setErr(err, "cannot list the modules of process " + std::to_string(pid)), false;
    }
    process_ = h;
    pid_ = pid;
    base_ = reinterpret_cast<uint64_t>(mods[0]);
    // Headers: identity at +0x40 ("RPLYCMP2" = recompiled with an .rplmap), then the section table.
    uint8_t hdr[0x1000];
    if (!read(base_, hdr, sizeof(hdr))) {
        detach();
        return setErr(err, "cannot read the exe headers"), false;
    }
    if (std::memcmp(hdr + 0x40, "RPLYCMP2", 8) == 0) {
        const uint32_t e = rd<uint32_t>(hdr + 0x3C);
        if (e > sizeof(hdr) - 0x200) {
            detach();
            return setErr(err, "bad PE header offset"), false;
        }
        const uint16_t sections = rd<uint16_t>(hdr + e + 6), optSize = rd<uint16_t>(hdr + e + 20);
        bool found = false;
        for (uint16_t i = 0; i < sections; ++i) {
            const size_t s = e + 24 + optSize + size_t(i) * 40;
            if (s + 40 > sizeof(hdr)) break;
            if (std::memcmp(hdr + s, ".rplmap\0", 8) != 0) continue;
            const uint32_t vsize = rd<uint32_t>(hdr + s + 8), va = rd<uint32_t>(hdr + s + 12);
            if (vsize < 24 || vsize > (64u << 20)) break;
            std::vector<uint8_t> map(vsize);
            std::string perr;
            if (!read(base_ + va, map.data(), map.size()) || !parseRplMap(map.data(), map.size(), ranges_, &perr)) {
                detach();
                return setErr(err, "the exe is recompiled (RPLYCMP2) but its .rplmap could not be read: " + perr), false;
            }
            found = true;
            break;
        }
        if (!found) {
            detach();
            return setErr(err, "the exe is recompiled (RPLYCMP2) but has no .rplmap section"), false;
        }
    }
    return true;
#else
    (void)pid;
    return setErr(err, "direct live link is Windows only"), false;
#endif
}

bool DirectLink::client(uint64_t& clientOut, std::string* err) const {
    uint64_t entities = 0, cl = 0;
    if (!read(addr(kRvaGEntities), &entities, 8)) return setErr(err, "cannot read the entity table pointer"), false;
    if (!entities || !read(entities + 0x150, &cl, 8) || !cl) return setErr(err, "not in a match"), false;
    clientOut = cl;
    return true;
}

std::optional<PlayerView> DirectLink::readPlayer(std::string* err) const {
    uint64_t cl = 0;
    if (!client(cl, err)) return std::nullopt;
    float o[3], a[3];
    if (!read(cl + 0x30, o, sizeof(o)) || !read(cl + 0x1CC, a, sizeof(a))) return setErr(err, "cannot read the player state"), std::nullopt;
    if (!finiteVec(o, 1e6) || !finiteVec(a, 1e4)) return setErr(err, "player state does not look like a position (wrong build?)"), std::nullopt;
    return PlayerView{Vec3{o[0], o[1], o[2]}, Vec3{a[0], a[1], a[2]}};
}

bool DirectLink::sendCommand(const std::string& textIn, std::string* err) {
    struct Buffer {
        uint64_t data;
        int32_t capacity, size;
    } b{};
    const std::string text = textIn + "\n";
    if (!read(addr(kRvaCbufMain), &b, sizeof(b))) return setErr(err, "cannot read the command buffer"), false;
    if (!b.data || b.capacity != 0x10000 || b.size < 0 || b.size >= b.capacity)
        return setErr(err, "the command buffer does not look valid (wrong build?)"), false;
    if (b.size != 0) return setErr(err, "the game is busy with other commands; try again"), false;
    if (text.size() + 1 >= size_t(b.capacity)) return setErr(err, "command too long"), false;
    if (!write(b.data, text.c_str(), text.size() + 1)) return setErr(err, "cannot write the command text"), false;
    const int32_t next = int32_t(text.size());
    if (!write(addr(kRvaCbufMain) + 12, &next, sizeof(next))) return setErr(err, "cannot queue the command"), false;
    return true;
}

bool DirectLink::commandBufferIdle() const {
    struct Buffer {
        uint64_t data;
        int32_t capacity, size;
    } b{};
    return read(addr(kRvaCbufMain), &b, sizeof(b)) && b.data && b.capacity == 0x10000 && b.size == 0;
}

std::optional<int32_t> DirectLink::readI32(uint32_t nativeRva) const {
    int32_t v = 0;
    if (!read(addr(nativeRva), &v, sizeof(v))) return std::nullopt;
    return v;
}

std::optional<uint8_t> DirectLink::readU8(uint32_t nativeRva) const {
    uint8_t v = 0;
    if (!read(addr(nativeRva), &v, sizeof(v))) return std::nullopt;
    return v;
}

std::optional<bool> DirectLink::readDvarBool(uint32_t nativeRvaOfDvarPtr, uint32_t valueOffset) const {
    uint64_t dvar = 0;
    uint8_t v = 0;
    if (!read(addr(nativeRvaOfDvarPtr), &dvar, 8) || !dvar || !read(dvar + valueOffset, &v, 1)) return std::nullopt;
    return v != 0;
}

std::optional<std::string> DirectLink::readDvarString(uint32_t nativeRvaOfDvarPtr, uint32_t valueOffset, size_t maxLen) const {
    uint64_t dvar = 0, text = 0;
    if (!read(addr(nativeRvaOfDvarPtr), &dvar, 8) || !dvar || !read(dvar + valueOffset, &text, 8)) return std::nullopt;
    if (!text) return std::string();
    std::string out;
    char chunk[16];
    while (out.size() < maxLen) {
        if (!read(text + out.size(), chunk, sizeof(chunk))) {
            // The string may end close to a page boundary: fall back to byte reads.
            char c = 0;
            if (!read(text + out.size(), &c, 1)) return std::nullopt;
            if (!c) return out;
            out += c;
            continue;
        }
        for (char c : chunk) {
            if (!c) return out;
            if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return std::nullopt;
            out += c;
        }
    }
    return out.substr(0, maxLen);
}

bool DirectLink::inMatch() const {
    uint64_t cl = 0;
    return client(cl, nullptr);
}

bool DirectLink::setPlayerOrigin(const Vec3& o, std::string* err) {
    uint64_t cl = 0;
    if (!client(cl, err)) return false;
    const float v[3] = {float(o.x), float(o.y), float(o.z)};
    if (!finiteVec(v, 1e6)) return setErr(err, "bad position"), false;
    if (!write(cl + 0x30, v, sizeof(v))) return setErr(err, "cannot write the player origin"), false;
    return true;
}

}  // namespace iw8r::live
