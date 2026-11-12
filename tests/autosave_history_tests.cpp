#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "autosave_history.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Unique scratch folder under the system temp dir, removed on destruction.
struct ScratchDir {
    fs::path root;
    ScratchDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("iw8r_autosave_tests_" + std::to_string(ticks));
        std::error_code ec;
        fs::create_directories(root, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void writeFile(const fs::path& p, size_t bytes) {
    std::ofstream f(p, std::ios::binary);
    f << std::string(bytes, 'x');
}

}  // namespace

TEST(autosave_name_round_trip) {
    const std::string name = autosaveFileName("mp_test", "20261110-213005", "map");
    CHECK(name == "mp_test.20261110-213005.autosave.map");

    AutosaveEntry e;
    CHECK(parseAutosaveFileName(name, e));
    CHECK(e.base == "mp_test");
    CHECK(e.stamp == "20261110-213005");
    CHECK(e.ext == "map");

    const std::string ents = autosaveFileName("mp_test", "20261110-213005", "ents");
    CHECK(ents == "mp_test.20261110-213005.autosave.ents");
    CHECK(parseAutosaveFileName(ents, e));
    CHECK(e.ext == "ents");
}

TEST(autosave_dotted_bases) {
    const std::string name = autosaveFileName("my.map.v2", "20260101-000000", "map");
    CHECK(name == "my.map.v2.20260101-000000.autosave.map");

    AutosaveEntry e;
    CHECK(parseAutosaveFileName(name, e));
    CHECK(e.base == "my.map.v2");
    CHECK(e.stamp == "20260101-000000");

    // A dotted legacy base also keeps its dots.
    CHECK(parseAutosaveFileName("my.map.v2.autosave.map", e));
    CHECK(e.base == "my.map.v2");
    CHECK(e.stamp.empty());

    // A base that itself contains the tag word still round-trips.
    CHECK(parseAutosaveFileName(autosaveFileName("x.autosave", "20260101-000000", "map"), e));
    CHECK(e.base == "x.autosave");
}

TEST(autosave_rejects_bad_stamps_and_extensions) {
    // Stamp validation when building names.
    CHECK(autosaveFileName("mp_test", "2026111-213005", "map").empty());     // 14 chars
    CHECK(autosaveFileName("mp_test", "20261110_213005", "map").empty());    // wrong separator
    CHECK(autosaveFileName("mp_test", "20261110-21300a", "map").empty());    // non-digit
    CHECK(autosaveFileName("mp_test", "20261110-2130055", "map").empty());   // 16 chars
    CHECK(autosaveFileName("mp_test", "2026111-0213005", "map").empty());    // '-' at wrong index

    // Extension and base validation when building names.
    CHECK(autosaveFileName("mp_test", "20261110-213005", "bsp").empty());
    CHECK(autosaveFileName("mp_test", "20261110-213005", "").empty());
    CHECK(autosaveFileName("", "20261110-213005", "map").empty());
    CHECK(autosaveFileName("a/b", "20261110-213005", "map").empty());

    // Parsing: wrong extension, missing tag, empty base, directory part.
    AutosaveEntry e;
    CHECK(!parseAutosaveFileName("mp_test.20261110-213005.autosave.bsp", e));
    CHECK(!parseAutosaveFileName("mp_test.20261110-213005.autosave.map.bak", e));
    CHECK(!parseAutosaveFileName("mp_test.20261110-213005.map", e));          // no ".autosave" tag
    CHECK(!parseAutosaveFileName("mp_test.autosave", e));                     // no extension
    CHECK(!parseAutosaveFileName(".autosave.map", e));                        // empty base
    CHECK(!parseAutosaveFileName(".20261110-213005.autosave.map", e));        // stamp but empty base
    CHECK(!parseAutosaveFileName("", e));
    CHECK(!parseAutosaveFileName("maps/mp_test.autosave.map", e));
    CHECK(!parseAutosaveFileName("notes.txt", e));

    // A malformed stamp is never read as a stamped entry (it falls through to a legacy base).
    CHECK(parseAutosaveFileName("mp_test.2026111-213005.autosave.map", e));
    CHECK(e.stamp.empty());
    CHECK(e.base == "mp_test.2026111-213005");
}

TEST(autosave_legacy_names) {
    CHECK(autosaveFileName("mp_test", "", "map") == "mp_test.autosave.map");
    CHECK(autosaveFileName("mp_test", "", "ents") == "mp_test.autosave.ents");

    AutosaveEntry e;
    e.stamp = "stale";
    e.path = "keep";
    CHECK(parseAutosaveFileName("mp_test.autosave.ents", e));
    CHECK(e.base == "mp_test");
    CHECK(e.stamp.empty());
    CHECK(e.ext == "ents");
    CHECK(e.path == "keep");  // parse does not touch the path
}

TEST(autosave_list_ordering_in_folder) {
    ScratchDir scratch;
    const fs::path dir = scratch.root;

    writeFile(dir / "mp_a.20261109-010000.autosave.map", 5);
    writeFile(dir / "mp_a.20261110-213005.autosave.map", 3);
    writeFile(dir / "mp_b.20261110-090000.autosave.ents", 2);
    writeFile(dir / "mp_a.autosave.map", 1);          // legacy: listed, sorted last
    writeFile(dir / "notes.txt", 9);                  // not an autosave
    fs::create_directories(dir / "mp_c.20261110-000000.autosave.map");  // directory: skipped

    const std::vector<AutosaveEntry> all = listAutosaves(dir.string());
    CHECK(all.size() == 4);
    if (all.size() == 4) {
        CHECK(all[0].base == "mp_a" && all[0].stamp == "20261110-213005" && all[0].bytes == 3);
        CHECK(all[1].base == "mp_b" && all[1].stamp == "20261110-090000" && all[1].ext == "ents");
        CHECK(all[2].base == "mp_a" && all[2].stamp == "20261109-010000" && all[2].bytes == 5);
        CHECK(all[3].stamp.empty() && all[3].base == "mp_a" && all[3].bytes == 1);
        CHECK(all[0].path == (dir / "mp_a.20261110-213005.autosave.map").string());
    }

    const std::vector<AutosaveEntry> onlyA = listAutosaves(dir.string(), "mp_a");
    CHECK(onlyA.size() == 3);
    if (onlyA.size() == 3) {
        CHECK(onlyA[0].stamp == "20261110-213005");
        CHECK(onlyA[1].stamp == "20261109-010000");
        CHECK(onlyA[2].stamp.empty());
    }

    CHECK(listAutosaves(dir.string(), "mp_missing").empty());
    CHECK(listAutosaves((dir / "does_not_exist").string()).empty());
}

TEST(autosave_prune_keeps_newest_per_map) {
    // Built directly from entries; paths are labels, not files.
    auto mk = [](const char* base, const char* stamp, const char* ext, const char* path) {
        AutosaveEntry e;
        e.base = base;
        e.stamp = stamp;
        e.ext = ext;
        e.path = path;
        return e;
    };
    // Deliberately unsorted input.
    std::vector<AutosaveEntry> entries = {
        mk("mp_a", "20261110-090000", "map", "a2"),
        mk("mp_b", "20261108-000000", "map", "b3"),
        mk("mp_a", "20261111-120000", "map", "a0"),
        mk("mp_b", "20261110-120000", "map", "b1"),
        mk("mp_a", "20261110-000000", "map", "a3"),
        mk("mp_b", "20261109-120000", "map", "b2"),
        mk("mp_a", "", "map", "legacy"),
        mk("mp_c", "20261110-000000", "map", "c_only"),
    };

    const std::vector<std::string> pruned = autosavesToPrune(entries, 2);
    // mp_a has 4 stamped copies: keep a0 and a2, prune a3 (oldest) only; legacy never listed.
    // mp_b has 3 stamped copies: keep b1 and b2, prune b3. mp_c has one copy: nothing to prune.
    CHECK(pruned.size() == 2);
    auto has = [&](const char* p) {
        for (const std::string& s : pruned)
            if (s == p) return true;
        return false;
    };
    CHECK(has("a3"));
    CHECK(has("b3"));
    CHECK(!has("a0") && !has("a2") && !has("b1") && !has("b2"));
    CHECK(!has("legacy") && !has("c_only"));

    CHECK(autosavesToPrune(entries, 10).empty());
    CHECK(autosavesToPrune({}, 2).empty());
}

TEST(autosave_has_free_space) {
    ScratchDir scratch;
    const std::string dir = scratch.root.string();

    uintmax_t freeBytes = 12345;
    CHECK(hasFreeSpace(dir, 0, &freeBytes));
    CHECK(freeBytes > 0);  // a real volume reports some available space

    CHECK(hasFreeSpace(dir, 0));  // null out-param is allowed

    const uintmax_t huge = UINTMAX_MAX;
    CHECK(!hasFreeSpace(dir, huge, &freeBytes));
    CHECK(freeBytes < huge);

    // Unknown volume (space() fails): report true and zero bytes.
    uintmax_t unknownFree = 99;
    CHECK(hasFreeSpace((scratch.root / "no_such_dir_xyz").string(), huge, &unknownFree));
    CHECK(unknownFree == 0);
}
