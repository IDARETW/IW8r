import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('tests/map_gametypes_tests.cpp', [(
 'checks.size() == 5 && checks[0].gametype == "war" && checks[1].gametype == "dm" && checks[4].gametype == "ctf"',
 'checks.size() == 6 && checks[0].gametype == "war" && checks[1].gametype == "dm" && checks[4].gametype == "ctf" &&\n'
 '          checks[5].gametype == "br" && !checks[5].supported')])

patch('tests/devmap_plan_tests.cpp', [(
 '    CHECK(backToLobbyCommand() == "xpartybackout");\n',
 '    // A game mode preset\'s dvars are set before the map and the start (research 87: a private match applies none).\n'
 '    const auto withMode = startMatchCommands("mp_iw8r_test", "br", lobbyHost(),\n'
 '                                            {{"scr_br_teamsize", "3"}, {"loot_table_name", "mp/loot_item_defs_s3.csv"}, {"bad name", "1"}, {"x", "a;b"}});\n'
 '    CHECK(withMode.size() == 5 && withMode[0] == "set scr_br_teamsize 3" && withMode[1] == "set loot_table_name mp/loot_item_defs_s3.csv" &&\n'
 '          withMode[2] == "set NSQLTTMRMP mp_iw8r_test" && withMode[3] == "set MOLPOSLOMO br" && withMode[4] == "xpartygo 1");\n'
 '    std::string modeText;\n'
 '    CHECK(joinCommands(withMode, modeText));\n'
 '    CHECK(backToLobbyCommand() == "xpartybackout");\n')])

# compile: baked dvars and br switches in the generated script; br detection
p = 'tests/compile_iw8_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '// The shipped catalogue: hardpoint_chevron is placeable; a stock door is listed but lives in map zones.'
assert s.count(a) == 1
t = r'''// Research 87: a map can carry game mode dvars (worldspawn iw8r_mode_dvars) and the Warzone switches of a small map
// (iw8r_br); the generated map script sets them right after the load script.
TEST(compile_iw8_game_mode_settings_in_map_script) {
    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string marker = "\"classname\" \"worldspawn\"" + eol;
    const size_t at = text.find(marker);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    text.insert(at + marker.size(), "\"iw8r_mode_dvars\" \"scr_br_teamsize=1; scr_br_gulag=0; bad name=3\"" + eol + "\"iw8r_br\" \"nocircle nogulag skipdrop\"" + eol);
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("mode_settings");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    const size_t load = gsc.find("maps\\mp\\_load::main();");
    const size_t d1 = gsc.find("setdvar(\"scr_br_teamsize\", \"1\");"), d2 = gsc.find("setdvar(\"scr_br_gulag\", \"0\");");
    CHECK(load != std::string::npos && d1 != std::string::npos && d2 != std::string::npos && d1 > load && d2 > d1);
    CHECK(gsc.find("bad name") == std::string::npos);
    CHECK(gsc.find("level.br_circle_disabled = 1;") != std::string::npos &&
          gsc.find("scripts\\mp\\gametypes\\br_gametypes::disablefeature(\"circle\");") != std::string::npos);
    CHECK(gsc.find("level.usegulag = 0;") != std::string::npos && gsc.find("level.skipprematchdropspawn = 1;") != std::string::npos);
    bool dvars = false, br = false, bad = false;
    for (const auto& w : rep.warnings) {
        dvars |= w.find("2 game mode dvar(s) are set by the map script") != std::string::npos;
        br |= w.find("Warzone (br) switches") != std::string::npos && w.find("no circle no gulag no drop from the plane") != std::string::npos;
        bad |= w.find("iw8r_mode_dvars") != std::string::npos && w.find("bad name=3") != std::string::npos;
    }
    CHECK(dvars && br && bad);

    // A map without the keys gets none of it.
    Id next2 = 1;
    std::string plainText;
    CHECK(readFile(samplePath(), plainText));
    ParseResult plain = parseMap(plainText, next2);
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    CompileReport rep2;
    const std::string out2 = scratch("mode_settings_none");
    CHECK(compileIw8Extraction(*plain.map, out2, opt, rep2, &err));
    const std::string gsc2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc2.find("setdvar(") == std::string::npos && gsc2.find("br_") == std::string::npos && gsc2.find("usegulag") == std::string::npos);
}

'''.replace('\n', nl)
s = s.replace(a, t + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
