// The project's IW8 GSC compiler (src/tools/gsc, gsc-tool 1.6.2) against what gsc-tool 1.6.2 wrote for the same
// scripts (tests/data/gsc, see README.txt there): same bytes, same error message.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/tools/gsc/api/iw8r_gsc.h"
#include "test_harness.h"

namespace fs = std::filesystem;

namespace {

fs::path gscFixtures() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "gsc";
        if (fs::exists(cand / "mp_jump.gsc")) return cand;
    }
    return {};
}

std::vector<std::uint8_t> readBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(gsc_compiler_matches_gsc_tool) {
    const fs::path dir = gscFixtures();
    CHECK(!dir.empty());
    if (dir.empty()) return;
    for (const char* name : {"mp_jump", "teamkills", "mp_scrbr"}) {
        const auto src = readBytes(dir / (std::string(name) + ".gsc"));
        const auto want = readBytes(dir / (std::string(name) + ".gscbin"));
        CHECK(!src.empty() && !want.empty());
        const auto out = iw8r::gsc::compileIw8Script(std::string(name) + ".gsc", src, dir);
        CHECK(out.ok);
        CHECK(out.error.empty());
        CHECK(out.gscbin == want);
    }
}

TEST(gsc_compiler_far_calls_name_the_script_file) {
    // gsc-tool 1.6.2 names a far call's script with its extension (scripts/mp/gametypes/br_gametypes.gsc); 1.4 did not.
    const fs::path dir = gscFixtures();
    if (dir.empty()) return;
    const auto out = iw8r::gsc::compileIw8Script("mp_scrbr.gsc", readBytes(dir / "mp_scrbr.gsc"), dir, false, true);
    CHECK(out.ok);
    const std::string stack(out.stack.begin(), out.stack.end());
    CHECK(stack.find("scripts/mp/gametypes/br_gametypes.gsc") != std::string::npos);
    CHECK(!out.bytecode.empty());
    CHECK(out.gscbin.empty());
}

TEST(gsc_compiler_reports_errors_like_gsc_tool) {
    const fs::path dir = gscFixtures();
    if (dir.empty()) return;
    const auto out = iw8r::gsc::compileIw8Script("mp_bad_syntax.gsc", readBytes(dir / "mp_bad_syntax.gsc"), dir);
    CHECK(!out.ok);
    CHECK(out.error == "[ERROR]:compiler:mp_bad_syntax.gsc:14:5: expected identifier, got 'level'");
    CHECK(out.gscbin.empty());
    // A script that is not there: gsc-tool's "couldn't open file" comes from the include reader, not a crash.
    const auto inc = iw8r::gsc::compileIw8Script("x.gsc", {'#', 'i', 'n', 'c', 'l', 'u', 'd', 'e', ' ', 'n', 'o', '_',
                                                           's', 'u', 'c', 'h', ';', '\n'}, dir);
    CHECK(!inc.ok);
    CHECK(!inc.error.empty());
}
