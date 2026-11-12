// Tests for the in-game verification matrix (roadmap 1.13): checklist parsing, result store, marks, roadmap marks.
#include "test_harness.h"
#include "verification.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using iw8r::CheckItem;
using iw8r::CheckResult;

std::string readAll(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Walks up from the working directory (the suite runs from E:\IW8-Radiant or bin\<mode>) to find a repo file.
std::string findRepoFile(const std::string& rel) {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 6 && !p.empty(); ++i) {
        if (fs::exists(p / rel, ec)) return (p / rel).string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

std::string J(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string crlf(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

std::string scratchDir() {
    fs::path base = ::iw8r::test::tmpDir().empty() ? fs::temp_directory_path() / "iw8r_verification_tests"
                                                   : fs::path(::iw8r::test::tmpDir()) / "verification";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base.string();
}

const CheckItem* findItem(const std::vector<CheckItem>& items, const std::string& needle) {
    for (const CheckItem& it : items)
        if (it.text.find(needle) != std::string::npos) return &it;
    return nullptr;
}

bool hasRoadmap(const CheckItem* item, const std::string& n) {
    if (!item) return false;
    for (const std::string& r : item->roadmapItems)
        if (r == n) return true;
    return false;
}

CheckResult res(const std::string& id, const std::string& result, const std::string& date, const std::string& note = "",
                const std::string& build = "b1") {
    CheckResult r;
    r.id = id;
    r.result = result;
    r.build = build;
    r.date = date;
    r.note = note;
    return r;
}

}  // namespace

TEST(verification_real_checklist_parses) {
    const std::string path = findRepoFile("docs/TEST_CHECKLIST.md");
    CHECK(!path.empty());
    if (path.empty()) return;
    bool ok = false;
    const std::string md = readAll(path, ok);
    CHECK(ok);

    const auto items = iw8r::parseChecklist(md);
    std::printf("  verification: %zu checklist items parsed from docs/TEST_CHECKLIST.md\n", items.size());
    CHECK(items.size() > 40);

    std::set<std::string> ids;
    size_t editor = 0, game = 0;
    for (const CheckItem& it : items) {
        CHECK(!it.id.empty());
        CHECK(ids.insert(it.id).second);  // unique
        bool slug = true;
        for (char c : it.id)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) slug = false;
        CHECK(slug);
        if (it.section == "In the editor") ++editor;
        else if (it.section.rfind("In game", 0) == 0) ++game;
        CHECK(!it.text.empty());
    }
    CHECK(editor > 0);
    CHECK(game > 0);
    CHECK(editor + game == items.size());

    // Stable: a second parse gives the same ids in the same order.
    const auto again = iw8r::parseChecklist(md);
    CHECK(again.size() == items.size());
    for (size_t i = 0; i < items.size() && i < again.size(); ++i) CHECK(again[i].id == items[i].id);

    bool haveNamed = false, haveFirstWords = false;
    for (const CheckItem& it : items) {
        if (it.id == "stock_materials") haveNamed = true;
        if (it.id == "the_map_opens_the_camera_shows_textured_rows") haveFirstWords = true;
    }
    CHECK(haveNamed);        // **stock_materials**
    CHECK(haveFirstWords);   // unnamed: first 8 words
}

TEST(verification_real_checklist_roadmap_links) {
    const std::string path = findRepoFile("docs/TEST_CHECKLIST.md");
    CHECK(!path.empty());
    if (path.empty()) return;
    bool ok = false;
    const auto items = iw8r::parseChecklist(readAll(path, ok));
    CHECK(ok);

    const CheckItem* normal = findItem(items, "Normal and gloss maps on stock textures");
    CHECK(normal != nullptr);
    CHECK(hasRoadmap(normal, "3.2"));
    CHECK(hasRoadmap(normal, "4.14"));
    CHECK(hasRoadmap(findItem(items, "Time of Day presets build"), "5.19"));
    CHECK(hasRoadmap(findItem(items, "command palette"), "9.13"));
    CHECK(hasRoadmap(findItem(items, "Insert Behaviour"), "6.12"));
    CHECK(hasRoadmap(findItem(items, "Outliner dock"), "2.29"));
}

TEST(verification_real_checklist_seen_in_game) {
    const std::string path = findRepoFile("docs/TEST_CHECKLIST.md");
    CHECK(!path.empty());
    if (path.empty()) return;
    bool ok = false;
    const auto items = iw8r::parseChecklist(readAll(path, ok));
    CHECK(ok);

    // Every in-game item that names 4.14 gets an ok result: the roadmap item is then seen in game.
    std::vector<CheckResult> results;
    size_t named = 0;
    for (const CheckItem& it : items) {
        if (it.section.rfind("In game", 0) == 0 && hasRoadmap(&it, "4.14")) {
            results.push_back(res(it.id, "ok", "2026-11-10"));
            ++named;
        }
    }
    CHECK(named >= 1);
    auto seen = iw8r::roadmapSeenInGame(items, results);
    CHECK(seen.count("4.14") == 1);
    if (seen.count("4.14")) CHECK(seen["4.14"] == "seen in game 2026-11-10");

    // One of them failing takes the mark away again.
    if (!results.empty()) results.front().result = "wrong";
    seen = iw8r::roadmapSeenInGame(items, results);
    CHECK(seen.count("4.14") == 0);
}

TEST(verification_parse_synthetic) {
    const std::string md = J({
        "# Head",
        "",
        "## In the editor",
        "",
        "- [ ] **Alpha_Item**: first (3.2) and the 5.13 IES preview.",
        "- [X] Plain item with no name, see 1.20.4.7623265 only.",
        "- [ ] **dup**: one",
        "      two lines here.",
        "- [x] **dup**: again",
        "",
        "## In game (after deploy)",
        "- [ ] **alpha_item**: second",
        "  - [ ] nested is not an item",
    });
    const auto items = iw8r::parseChecklist(md);
    CHECK(items.size() == 5);
    if (items.size() != 5) return;

    CHECK(items[0].section == "In the editor");
    CHECK(items[0].id == "alpha_item");
    CHECK(items[0].text == "first (3.2) and the 5.13 IES preview.");
    CHECK(!items[0].checked);
    CHECK(hasRoadmap(&items[0], "3.2"));
    CHECK(hasRoadmap(&items[0], "5.13"));

    CHECK(items[1].checked);
    CHECK(items[1].id.rfind("plain_item", 0) == 0);
    CHECK(items[1].roadmapItems.empty());  // 1.20.4.7623265 is a version, not a roadmap item

    CHECK(items[2].id == "dup");
    CHECK(items[2].text == "one two lines here.");  // continuation line folded in
    CHECK(!items[2].checked);
    CHECK(items[3].id == "dup_2");
    CHECK(items[3].checked);

    CHECK(items[4].section == "In game (after deploy)");
    CHECK(items[4].id == "alpha_item_2");  // repeated slug gets a suffix
    CHECK(items[4].text == "second");
}

TEST(verification_result_store_round_trip) {
    const std::string path = scratchDir() + "/results_rt.json";
    std::error_code ec;
    fs::remove(path, ec);

    std::vector<CheckResult> none;
    std::string err;
    CHECK(iw8r::loadResults(path, none, err));  // missing file: empty history
    CHECK(none.empty());

    const std::vector<CheckResult> in = {
        res("stock_materials", "ok", "2026-11-10", "", "b-2026-11-10"),
        res("stock_materials", "wrong", "2026-11-09", "quote \" backslash \\ newline\nand tab\t end", "map-7"),
        res("x", "not-tested", "", "", ""),
    };
    CHECK(iw8r::saveResults(path, in, err));
    std::vector<CheckResult> out;
    CHECK(iw8r::loadResults(path, out, err));
    CHECK(out.size() == in.size());
    for (size_t i = 0; i < in.size() && i < out.size(); ++i) {
        CHECK(out[i].id == in[i].id);
        CHECK(out[i].result == in[i].result);
        CHECK(out[i].build == in[i].build);
        CHECK(out[i].date == in[i].date);
        CHECK(out[i].note == in[i].note);
    }

    // A bad result word is refused on save and on load.
    std::vector<CheckResult> bad = {res("a", "maybe", "2026-11-10")};
    CHECK(!iw8r::saveResults(path, bad, err));
    bool ok = false;
    const std::string badFile = scratchDir() + "/results_bad.json";
    {
        std::ofstream f(badFile, std::ios::binary);
        f << "{\"results\": [{\"id\": \"a\", \"result\": \"fine\"}]}";
        ok = static_cast<bool>(f);
    }
    CHECK(ok);
    CHECK(!iw8r::loadResults(badFile, out, err));
}

TEST(verification_marks_idempotent_minimal) {
    const std::string md = J({
        "# Title",
        "",
        "## Sec one",
        "",
        "- [ ] **alpha**: first thing.",
        "- [ ] **beta**: second thing",
        "      continues here.",
        "",
        "Prose line stays.",
        "",
        "- [x] gamma: already done.",
        "- [ ] **delta**: untouched.",
    });
    const std::vector<CheckResult> results = {
        res("alpha", "ok", "2026-11-10"),
        res("beta", "wrong", "2026-11-10", "say \"no\""),
    };
    const std::string expected = J({
        "# Title",
        "",
        "## Sec one",
        "",
        "- [x] **alpha**: first thing.",
        "- [ ] **beta**: second thing",
        "      continues here. (wrong 2026-11-10: say \"no\")",
        "",
        "Prose line stays.",
        "",
        "- [x] gamma: already done.",
        "- [ ] **delta**: untouched.",
    });
    const std::string out = iw8r::updateChecklistMarks(md, results);
    CHECK(out == expected);
    CHECK(iw8r::updateChecklistMarks(out, results) == out);  // idempotent
    CHECK(iw8r::updateChecklistMarks(md, results) == out);

    // A later ok clears the wrong suffix and checks the box again; the text goes back to the original shape.
    std::vector<CheckResult> later = results;
    later.push_back(res("beta", "ok", "2026-11-11"));
    const std::string expected2 = J({
        "# Title",
        "",
        "## Sec one",
        "",
        "- [x] **alpha**: first thing.",
        "- [x] **beta**: second thing",
        "      continues here.",
        "",
        "Prose line stays.",
        "",
        "- [x] gamma: already done.",
        "- [ ] **delta**: untouched.",
    });
    CHECK(iw8r::updateChecklistMarks(out, later) == expected2);
    CHECK(iw8r::updateChecklistMarks(md, later) == expected2);

    // A later wrong on an item that was checked unchecks it and adds the suffix (no note: no colon).
    const std::string outWrong = iw8r::updateChecklistMarks(out, {res("alpha", "wrong", "2026-11-12")});
    CHECK(outWrong.find("- [ ] **alpha**: first thing. (wrong 2026-11-12)\n") != std::string::npos);

    // CRLF files keep their line endings.
    const std::string mdCr = crlf(md);
    CHECK(iw8r::updateChecklistMarks(mdCr, results) == crlf(expected));
    CHECK(iw8r::parseChecklist(mdCr).size() == 4);
    CHECK(iw8r::parseChecklist(mdCr)[1].text == "second thing continues here.");
}

TEST(verification_latest_result_decides_mark) {
    const std::string md = J({"- [ ] **stock_materials**: text"});
    // The newer ok wins even though the wrong was listed later.
    const std::vector<CheckResult> results = {
        res("stock_materials", "ok", "2026-11-10"),
        res("stock_materials", "wrong", "2026-11-09", "old"),
    };
    CHECK(iw8r::updateChecklistMarks(md, results) == "- [x] **stock_materials**: text");
    // Same date: the later entry wins.
    const std::vector<CheckResult> same = {
        res("stock_materials", "ok", "2026-11-10"),
        res("stock_materials", "wrong", "2026-11-10", "second"),
    };
    CHECK(iw8r::updateChecklistMarks(md, same) == "- [ ] **stock_materials**: text (wrong 2026-11-10: second)");
    // not-tested leaves the line alone.
    CHECK(iw8r::updateChecklistMarks(md, {res("stock_materials", "not-tested", "2026-11-10")}) == md);
}

TEST(verification_roadmap_aggregation) {
    const std::string md = J({
        "## In the editor",
        "- [ ] **ed_a**: editor check for (3.2).",
        "## In game (after deploy)",
        "- [ ] **g_a**: in game (3.2) and 5.13 ok.",
        "- [ ] **g_b**: in game (3.2) also 4.14.",
        "- [ ] **g_c**: only 5.13 here.",
    });
    const auto items = iw8r::parseChecklist(md);
    CHECK(items.size() == 4);

    std::vector<CheckResult> results = {
        res("g_a", "ok", "2026-11-10"),
        res("g_b", "ok", "2026-11-12"),
        res("ed_a", "not-tested", "2026-11-12"),  // editor items do not count as seen in game
    };
    auto seen = iw8r::roadmapSeenInGame(items, results);
    CHECK(seen.size() == 2);  // 5.13 has an unchecked in-game item; 3.2 and 4.14 are seen
    CHECK(seen["3.2"] == "seen in game 2026-11-12");
    CHECK(seen["4.14"] == "seen in game 2026-11-12");
    CHECK(seen.count("5.13") == 0);

    results.push_back(res("g_c", "ok", "2026-11-10"));
    seen = iw8r::roadmapSeenInGame(items, results);
    CHECK(seen["5.13"] == "seen in game 2026-11-10");

    // A newer wrong on one of the items removes its roadmap marks.
    results.push_back(res("g_b", "wrong", "2026-11-13"));
    seen = iw8r::roadmapSeenInGame(items, results);
    CHECK(seen.count("3.2") == 0);
    CHECK(seen.count("4.14") == 0);
    CHECK(seen.count("5.13") == 1);

    // Nothing tested: no marks at all.
    CHECK(iw8r::roadmapSeenInGame(items, {}).empty());
}
