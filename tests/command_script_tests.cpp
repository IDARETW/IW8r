// Command scripts (roadmap 9.10): every command, quoting, error lines, one-step rollback and the macro recorder's
// output replaying to the same map.
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "command_script.h"
#include "document.h"
#include "geometry.h"
#include "map_queries.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// A box brush in iwmap face-line text, every face using `mat`.
std::string box(double x0, double y0, double z0, double x1, double y1, double z1, const std::string& mat) {
    auto p = [](double x, double y, double z) { return formatNumber(x) + " " + formatNumber(y) + " " + formatNumber(z); };
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) " + mat + " 64 64 0 0 0 0\n";
    };
    std::string t = "{\n";
    t += f(p(x1, y1, z0), p(x0, y1, z0), p(x0, y0, z0));
    t += f(p(x0, y0, z1), p(x0, y1, z1), p(x1, y1, z1));
    t += f(p(x0, y0, z0), p(x0, y0, z1), p(x1, y0, z1));
    t += f(p(x1, y0, z0), p(x1, y0, z1), p(x1, y1, z1));
    t += f(p(x1, y1, z0), p(x1, y1, z1), p(x0, y1, z1));
    t += f(p(x0, y1, z0), p(x0, y1, z1), p(x0, y0, z1));
    return t + "}\n";
}

// A point entity with an optional targetname and model.
std::string point(const std::string& cls, const std::string& target, const std::string& origin, const std::string& model = "") {
    std::string t = "{\n\"classname\" \"" + cls + "\"\n";
    if (!target.empty()) t += "\"targetname\" \"" + target + "\"\n";
    if (!model.empty()) t += "\"model\" \"" + model + "\"\n";
    return t + "\"origin\" \"" + origin + "\"\n}\n";
}

// Two caulk/metal brushes in worldspawn, two misc_models (a, b) and one light (lamp).
std::string fixture() {
    return "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box(0, 0, 0, 64, 64, 64, "caulk") + box(128, 0, 0, 192, 64, 64, "metal") +
           "}\n" + point("misc_model", "a", "0 0 0", "tm/a") + point("misc_model", "b", "256 0 0", "tm/a") +
           point("light", "lamp", "8 8 8");
}

void load(Document& doc) {
    std::string err;
    CHECK(doc.loadFromText(fixture(), "", &err));
}

const Entity* byTarget(const Document& d, const std::string& target) {
    for (const Entity& e : d.map().entities) {
        const std::string* v = e.value("targetname");
        if (v && *v == target) return &e;
    }
    return nullptr;
}

std::optional<Vec3> originOf(const Document& d, const std::string& target) {
    const Entity* e = byTarget(d, target);
    return e ? e->origin() : std::nullopt;
}

size_t countClass(const Document& d, const std::string& cls) { return findByClass(d.map(), cls).size(); }

size_t facesOf(const Document& d, const std::string& mat) {
    for (const MaterialUse& u : materialUsage(d.map()))
        if (u.material == mat) return u.faces;
    return 0;
}

}  // namespace

TEST(script_tokenizer_comments_and_quotes) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc,
                                      "# header\n"
                                      "   \n"
                                      "select class misc_model targetname \"b\" # trailing comment\n"
                                      "set note \"two words\" # more\n",
                                      false);
    CHECK(r.ok);
    CHECK(r.commands == 2);
    const Entity* b = byTarget(doc, "b");
    CHECK(b && b->value("note") && *b->value("note") == "two words");

    // An escaped quote is a valid token, but a map value cannot hold a quote, so the set is refused.
    r = runCommandScript(doc, "set k \"a\\\"b\"\n", false);
    CHECK(!r.ok && r.line == 1);
    // Quote in the middle of a bare word, unterminated quote, unknown escape.
    r = runCommandScript(doc, "set a\"b c\n", false);
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "set \"unterminated value\n", false);
    CHECK(!r.ok && r.line == 1 && r.message.find("unterminated") != std::string::npos);
    r = runCommandScript(doc, "set k \"bad \\q escape\"\n", false);
    CHECK(!r.ok && r.line == 1);
}

TEST(script_select_commands) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc, "select class misc_model\n", false);
    CHECK(r.ok && doc.selection().size() == 2);
    r = runCommandScript(doc, "select class misc_model targetname b\n", false);
    const Entity* b = byTarget(doc, "b");
    CHECK(r.ok && doc.selection().size() == 1 && b && doc.selection().begin()->entity == b->id);
    r = runCommandScript(doc, "select key targetname lamp\n", false);
    CHECK(r.ok && doc.selection().size() == 1);
    r = runCommandScript(doc, "select all\n", false);
    CHECK(r.ok && doc.selection().size() == 5);  // two brushes + three point entities
    r = runCommandScript(doc, "select none\n", false);
    CHECK(r.ok && doc.selection().empty());
    r = runCommandScript(doc, "select class misc_model\nselect add key targetname lamp\n", false);
    CHECK(r.ok && doc.selection().size() == 3);
    r = runCommandScript(doc, "select bogus x\n", false);
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "select class\n", false);
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "select none extra\n", false);
    CHECK(!r.ok && r.line == 1);
}

TEST(script_set_remove_and_guards) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc, "select class misc_model\nset team \"red team\"\n", false);
    CHECK(r.ok);
    const Entity* a = byTarget(doc, "a");
    const Entity* b = byTarget(doc, "b");
    CHECK(a && a->value("team") && *a->value("team") == "red team");
    CHECK(b && b->value("team") && *b->value("team") == "red team");

    r = runCommandScript(doc, "select class misc_model targetname a\nremove team\n", false);
    CHECK(r.ok);
    a = byTarget(doc, "a");
    b = byTarget(doc, "b");
    CHECK(a && a->value("team") == nullptr);
    CHECK(b && b->value("team") != nullptr);

    r = runCommandScript(doc, "select none\nset x y\n", false);
    CHECK(!r.ok && r.line == 2 && r.message.find("nothing selected") != std::string::npos);
    CHECK(r.commands == 1);
    r = runCommandScript(doc, "select none\nremove team\n", false);
    CHECK(!r.ok && r.line == 2);
    r = runCommandScript(doc, "select none\nset x\n", false);  // missing value
    CHECK(!r.ok && r.line == 2);
}

TEST(script_move_clone_delete) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc, "select class misc_model targetname a\nmove 8 0 -16\n", false);
    CHECK(r.ok);
    auto o = originOf(doc, "a");
    CHECK(o && *o == Vec3(8, 0, -16));

    r = runCommandScript(doc, "clone 0 100 0\n", false);
    CHECK(r.ok && countClass(doc, "misc_model") == 3 && doc.selection().size() == 1);
    r = runCommandScript(doc, "delete\n", false);
    CHECK(r.ok && countClass(doc, "misc_model") == 2);
    r = runCommandScript(doc, "delete\n", false);
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "move 1 2\n", false);  // wrong argument count
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "select all\nmove 1 two 3\n", false);  // not a number
    CHECK(!r.ok && r.line == 2);
}

TEST(script_add_command) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc, "add light 10 20 30 _color \"1 0.5 0\" style 3\n", false);
    CHECK(r.ok);
    CHECK(countClass(doc, "light") == 2);
    CHECK(doc.selection().size() == 1);
    const Entity* e = doc.selection().empty() ? nullptr : doc.findEntity(doc.selection().begin()->entity);
    CHECK(e && e->classname() == "light");
    CHECK(e && e->origin() && *e->origin() == Vec3(10, 20, 30));
    CHECK(e && e->value("_color") && *e->value("_color") == "1 0.5 0");
    CHECK(e && e->value("style") && *e->value("style") == "3");

    r = runCommandScript(doc, "add light 1 2\n", false);  // no coordinate triple
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "add light 0 0 0 key\n", false);  // odd key/value count
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "add worldspawn 0 0 0\n", false);
    CHECK(!r.ok && r.line == 1);
}

TEST(script_material_and_guard) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc, "select class misc_model\nmaterial wall\n", false);
    CHECK(!r.ok && r.line == 2 && r.message.find("needs selected brushes") != std::string::npos);
    r = runCommandScript(doc, "select all\nmaterial wall_01\n", false);
    CHECK(r.ok);
    CHECK(facesOf(doc, "wall_01") == 12);  // two box brushes, six faces each
    CHECK(facesOf(doc, "metal") == 0 && facesOf(doc, "caulk") == 0);
}

TEST(script_array_and_align) {
    Document doc;
    load(doc);
    ScriptResult r = runCommandScript(doc, "select class misc_model targetname a\narray 32 0 0 3\n", false);
    CHECK(r.ok);
    CHECK(countClass(doc, "misc_model") == 5);  // a, b and three copies of a
    CHECK(doc.selection().size() == 3);
    r = runCommandScript(doc, "select all\narray 1 0 0 0\n", false);  // count below 1
    CHECK(!r.ok && r.line == 2);

    // Align the two misc_models (same bounds) to their minimum x: both origins end at the same x.
    r = runCommandScript(doc, "select class misc_model targetname a\nselect add class misc_model targetname b\nalign x min\n", false);
    CHECK(r.ok);
    auto oa = originOf(doc, "a");
    auto ob = originOf(doc, "b");
    CHECK(oa && ob && oa->x == ob->x);
    r = runCommandScript(doc, "select all\nalign q min\n", false);
    CHECK(!r.ok && r.line == 2);
    r = runCommandScript(doc, "select none\nalign x min\n", false);
    CHECK(!r.ok && r.message.find("nothing selected") != std::string::npos);
}

TEST(script_replace_key) {
    Document doc;
    load(doc);
    std::vector<std::string> log;
    ScriptResult r = runCommandScript(doc, "replace-key model tm/a tm/z\n", false, &log);
    CHECK(r.ok && r.message == "ran 1 command");
    CHECK(log.size() == 1 && log[0].find("2 entities changed") != std::string::npos);
    CHECK(byTarget(doc, "a")->value("model") && *byTarget(doc, "a")->value("model") == "tm/z");

    r = runCommandScript(doc, "select class misc_model targetname a\nreplace-key model tm/z tm/q selected\n", false);
    CHECK(r.ok);
    CHECK(*byTarget(doc, "a")->value("model") == "tm/q");
    CHECK(*byTarget(doc, "b")->value("model") == "tm/z");

    r = runCommandScript(doc, "replace-key model tm sub substring\n", false);
    CHECK(r.ok);
    CHECK(*byTarget(doc, "a")->value("model") == "sub/q");
    CHECK(*byTarget(doc, "b")->value("model") == "sub/z");

    r = runCommandScript(doc, "replace-key model x y bogus\n", false);  // unknown option
    CHECK(!r.ok && r.line == 1);
    r = runCommandScript(doc, "select none\nreplace-key model sub x selected\n", false);
    CHECK(!r.ok && r.line == 2);
}

TEST(script_undo_redo_commands) {
    Document doc;
    load(doc);
    const std::string before = doc.text();
    ScriptResult r = runCommandScript(doc, "select all\nmove 0 0 8\n", false);
    CHECK(r.ok && doc.text() != before);

    r = runCommandScript(doc, "undo\n", false);
    CHECK(r.ok && doc.text() == before);
    r = runCommandScript(doc, "redo\n", false);
    CHECK(r.ok && doc.text() != before);
    const std::string moved = doc.text();
    r = runCommandScript(doc, "undo\nundo\n", false);  // the second undo has nothing to undo
    CHECK(!r.ok && r.line == 2 && doc.text() == before);
    CHECK(runCommandScript(doc, "redo\n", false).ok && doc.text() == moved);

    // undo/redo cannot run inside a one-step script; the move before it is rolled back.
    r = runCommandScript(doc, "select all\nmove 0 0 8\nundo\n", true);
    CHECK(!r.ok && r.line == 3 && doc.text() == moved);
}

TEST(script_unknown_and_error_lines) {
    Document doc;
    load(doc);
    const std::string before = doc.text();
    ScriptResult r = runCommandScript(doc, "select all\n# comment\n\nfrobnicate 1 2\nmove 1 1 1\n", false);
    CHECK(!r.ok);
    CHECK(r.line == 4);
    CHECK(r.message.find("unknown command 'frobnicate'") != std::string::npos);
    CHECK(r.commands == 1);
    CHECK(doc.text() == before);  // the move after the failing line never ran
    CHECK(runCommandScript(doc, "\n# only comments\n", false).ok);
}

TEST(script_one_step_success_is_single_undo_step) {
    Document doc;
    load(doc);
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    ScriptResult r = runCommandScript(doc, "select class misc_model\nset team red\nmove 0 0 8\nset note hi\n", true);
    CHECK(r.ok && r.commands == 4);
    CHECK(doc.undoPosition() == pos + 1);
    CHECK(doc.undo());
    CHECK(doc.text() == before);
}

TEST(script_one_step_rollback_restores_map) {
    Document doc;
    load(doc);
    const Entity* lamp = byTarget(doc, "lamp");
    CHECK(lamp != nullptr);
    doc.select({lamp->id, kNoId}, false);
    const std::set<SelRef> selBefore = doc.selection();
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();

    const std::string script =
        "select class misc_model targetname a\n"
        "move 8 0 -16\n"
        "set team red\n"
        "add light 1 2 3\n"
        "remove team\n"
        "move nope 1 1\n";
    ScriptResult r = runCommandScript(doc, script, true);
    CHECK(!r.ok);
    CHECK(r.line == 6);
    CHECK(r.commands == 5);
    CHECK(r.message.find("rolled back") != std::string::npos);
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
    CHECK(doc.selection() == selBefore);
}

TEST(script_non_one_step_keeps_completed_lines) {
    Document doc;
    load(doc);
    const size_t pos = doc.undoPosition();
    ScriptResult r = runCommandScript(doc, "select class misc_model targetname a\nmove 8 0 0\nbogus\n", false);
    CHECK(!r.ok && r.line == 3);
    auto o = originOf(doc, "a");
    CHECK(o && *o == Vec3(8, 0, 0));
    CHECK(doc.undoPosition() == pos + 1);  // one undo step for the move, the select makes none
}

TEST(script_refuses_inside_open_group) {
    Document doc;
    load(doc);
    const std::string before = doc.text();
    doc.beginGroup("outer");
    ScriptResult r = runCommandScript(doc, "select all\nmove 0 0 8\n", true);
    doc.endGroup();
    CHECK(!r.ok);
    CHECK(doc.text() == before);
}

TEST(script_log_and_empty_scripts) {
    Document doc;
    load(doc);
    std::vector<std::string> log;
    ScriptResult r = runCommandScript(doc, "select all\nmove 1 0 0\n", false, &log);
    CHECK(r.ok && log.size() == 2);
    CHECK(log.size() == 2 && log[1].rfind("line 2: ", 0) == 0);
    r = runCommandScript(doc, "# nothing to do\n\n", true);
    CHECK(r.ok && r.commands == 0);
}

TEST(script_quoting_round_trip) {
    CHECK(quoteArg("plain") == "plain");
    CHECK(quoteArg("a b") == "\"a b\"");
    CHECK(quoteArg("") == "\"\"");
    CHECK(quoteArg("#x") == "\"#x\"");
    CHECK(quoteArg("say \"hi\"") == "\"say \\\"hi\\\"\"");
    CHECK(quoteArg("back\\slash") == "\"back\\\\slash\"");

    // Each quoted value reaches the map unchanged.
    Document doc;
    load(doc);
    CHECK(runCommandScript(doc, "select class misc_model targetname a\n", false).ok);
    for (const std::string& v : {std::string("a b"), std::string(""), std::string("#hash"), std::string("back\\slash")}) {
        ScriptResult r = runCommandScript(doc, cmdSet("note", v) + "\n", false);
        const Entity* a = byTarget(doc, "a");
        CHECK(r.ok && a && a->value("note") && *a->value("note") == v);
    }
}

TEST(script_builder_lines) {
    CHECK(cmdMove(Vec3(8, 0, -16)) == "move 8 0 -16");
    CHECK(cmdClone(Vec3(0, 64, 0)) == "clone 0 64 0");
    CHECK(cmdDelete() == "delete");
    CHECK(cmdRemove("note") == "remove note");
    CHECK(cmdSet("a key", "two words") == "set \"a key\" \"two words\"");
    CHECK(cmdSelectEntity("misc_model", "") == "select class misc_model");
    CHECK(cmdSelectEntity("misc_model", "a") == "select class misc_model targetname a");
    CHECK(cmdSelectEntity("light", "two words") == "select class light targetname \"two words\"");
}

TEST(recorder_replays_to_same_result) {
    Document live, replay;
    load(live);
    load(replay);
    MacroRecorder rec;
    const std::vector<std::string> steps = {
        cmdSelectEntity("misc_model", "a"),
        cmdMove(Vec3(8, 0, -16)),
        cmdSet("note", "hello world"),
        cmdSelectEntity("misc_model", "b"),
        cmdClone(Vec3(0, 64, 0)),
        cmdDelete(),
        cmdSelectEntity("misc_model", "a"),
        cmdRemove("note"),
    };
    for (const std::string& s : steps) {
        ScriptResult r = runCommandScript(live, s + "\n", true);
        CHECK(r.ok);
        rec.record(s);
    }
    CHECK(rec.size() == steps.size());
    rec.record("   ");  // blank lines are ignored
    CHECK(rec.size() == steps.size());

    ScriptResult rr = runCommandScript(replay, rec.text(), true);
    CHECK(rr.ok && rr.commands == steps.size());
    CHECK(replay.text() == live.text());
    CHECK(countClass(live, "misc_model") == 2);
    auto o = originOf(live, "a");
    CHECK(o && *o == Vec3(8, 0, -16));
    const Entity* a = byTarget(live, "a");
    CHECK(a && a->value("note") == nullptr);
}

TEST(document_macro_sink_records_ui_edits) {
    Document live, replay;
    load(live);
    load(replay);
    MacroRecorder rec;
    CHECK(runCommandScript(live, "select class misc_model targetname a\n", false).ok);
    CHECK(runCommandScript(replay, "select class misc_model targetname a\n", false).ok);
    live.macroSink = [&](const std::string& l) { rec.record(l); };
    // Edits as the UI makes them: a drag of three small moves, a key on the selected entity, a clone.
    CHECK(live.translateSelection(Vec3(8, 0, 0)));
    CHECK(live.translateSelection(Vec3(8, 0, 0)));
    CHECK(live.translateSelection(Vec3(0, 0, -4)));
    const Entity* a = byTarget(live, "a");
    CHECK(a && live.setKeyValue(a->id, "note", "from ui"));
    CHECK(live.cloneSelection(Vec3(0, 32, 0)));
    live.macroSink = nullptr;
    CHECK(rec.size() == 3);  // merged move, set, clone (the clone's inner moves are not reported)
    CHECK(rec.text() == "move 16 0 -4\nset note \"from ui\"\nclone 0 32 0\n");
    ScriptResult rr = runCommandScript(replay, rec.text(), true);
    CHECK(rr.ok);
    CHECK(replay.text() == live.text());
}
