// Direct live link (no proxy): .rplmap parsing and RVA resolution (src/core/live_direct.*).
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "devmap_plan.h"
#include "live_direct.h"
#include "test_harness.h"

using namespace iw8r;
using namespace iw8r::live;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
std::vector<uint8_t> makeMap(const std::vector<RvaRange>& rs) {
    std::vector<uint8_t> b(8);
    std::memcpy(b.data(), "RPLYMAP2", 8);
    put32(b, 2);
    put32(b, uint32_t(rs.size()));
    put32(b, 0);
    put32(b, uint32_t(24 + rs.size() * 12));
    for (const auto& r : rs) {
        put32(b, r.begin);
        put32(b, r.end);
        put32(b, r.mappedBegin);
    }
    return b;
}
}  // namespace

TEST(live_direct_rplmap_parse_and_resolve) {
    const std::vector<RvaRange> rs{{0, 0x1000, 0x1000}, {0x1000, 0xD000000, 0x2000}, {0xD000000, kNativeImageSize, 0xE000000}};
    const auto bytes = makeMap(rs);
    std::vector<RvaRange> got;
    CHECK(parseRplMap(bytes.data(), bytes.size(), got) && got.size() == 3);
    CHECK(resolveRva(got, 0x10) == 0x1010);
    CHECK(resolveRva(got, 0x1000) == 0x2000);
    CHECK(resolveRva(got, kRvaCbufMain) == 0xE000000 + (kRvaCbufMain - 0xD000000));
    CHECK(resolveRva({}, kRvaGEntities) == kRvaGEntities);  // stock exe: native RVAs
    // Gaps, a short cover and a bad magic are refused.
    const auto gap = makeMap({{0, 0x1000, 0}, {0x2000, kNativeImageSize, 0x2000}});
    CHECK(!parseRplMap(gap.data(), gap.size(), got) && got.empty());
    const auto shortMap = makeMap({{0, 0x1000, 0}});
    CHECK(!parseRplMap(shortMap.data(), shortMap.size(), got));
    auto bad = bytes;
    bad[0] = 'X';
    CHECK(!parseRplMap(bad.data(), bad.size(), got));
}

// The owner's recompiled exe (read-only, skipped when absent): its .rplmap parses and both globals land in a writable
// initialized-data section of the file.
TEST(live_direct_real_recompiled_exe_map) {
    const std::filesystem::path exe = "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay_dw.exe";
    if (!std::filesystem::exists(exe)) return;
    std::ifstream f(exe, std::ios::binary);
    std::vector<uint8_t> hdr(0x1000);
    f.read(reinterpret_cast<char*>(hdr.data()), std::streamsize(hdr.size()));
    CHECK(std::memcmp(hdr.data() + 0x40, "RPLYCMP2", 8) == 0);
    uint32_t e, chars = 0;
    std::memcpy(&e, hdr.data() + 0x3C, 4);
    uint16_t ns, opt;
    std::memcpy(&ns, hdr.data() + e + 6, 2);
    std::memcpy(&opt, hdr.data() + e + 20, 2);
    struct Sec { char name[9]; uint32_t vsize, va, rawSize, rawPtr, chars; };
    std::vector<Sec> secs;
    for (uint16_t i = 0; i < ns; ++i) {
        const uint8_t* s = hdr.data() + e + 24 + opt + i * 40;
        Sec x{};
        std::memcpy(x.name, s, 8);
        std::memcpy(&x.vsize, s + 8, 4);
        std::memcpy(&x.va, s + 12, 4);
        std::memcpy(&x.rawSize, s + 16, 4);
        std::memcpy(&x.rawPtr, s + 20, 4);
        std::memcpy(&x.chars, s + 36, 4);
        secs.push_back(x);
    }
    std::vector<RvaRange> ranges;
    for (const Sec& s : secs) {
        if (std::strcmp(s.name, ".rplmap") != 0) continue;
        std::vector<uint8_t> map(s.rawSize);
        f.seekg(s.rawPtr);
        f.read(reinterpret_cast<char*>(map.data()), std::streamsize(map.size()));
        std::string err;
        CHECK(parseRplMap(map.data(), std::min<size_t>(map.size(), s.vsize), ranges, &err));
    }
    CHECK(!ranges.empty());
    // The F5 devmap state reads (docs/research/65) are globals too: dvar pointers, mode / front-end / party ints.
    namespace dm = iw8r::devmap;
    for (uint32_t rva : {kRvaCbufMain, kRvaGEntities, dm::kRvaDvarUiMapname, dm::kRvaDvarUiGametype, dm::kRvaDvarSystemlink,
                         dm::kRvaDvarSystemlinkHost, dm::kRvaDvarPrivateMatch, dm::kRvaDvarServerRunning, dm::kRvaActiveGameMode,
                         dm::kRvaFrontEndState, dm::kRvaPartyInParty, dm::kRvaPartyAreWeHost}) {
        const uint64_t at = resolveRva(ranges, rva);
        bool writable = false;
        for (const Sec& s : secs)
            if (at >= s.va && at < uint64_t(s.va) + s.vsize) writable = (s.chars & 0x80000000u) != 0, chars = s.chars;
        CHECK(writable);
    }
    (void)chars;
}
