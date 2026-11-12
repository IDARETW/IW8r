import io
p = 'tests/compile_iw8_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)

rep('''        notKnown |= w.find("'scriptable_auto_ascender' is always loaded, but") != std::string::npos;
''', '''        notKnown |= w.find("'scriptable_auto_ascender': no stock map shows all the numbers") != std::string::npos;
''')
rep('''    // The class is not an entity of the built map.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("scriptable") == std::string::npos);
''', '''    // The class is not an entity of the built map.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("scriptable") == std::string::npos);
    // No door was built, so the map script does not start the stock door script.
    CHECK(readAll((mp / (std::string(kSampleId) + ".gsc")).string()).find("scriptable_door") == std::string::npos);
    CHECK(door->carryZone() == "mp_backlot2" && door->carryable() && door->door() && chevron->carryZone().empty() && !chevron->door());

    // With carrying on, the door is written with the stock zone to take its definition from, and the map script
    // starts the stock door script right after the load script.
    {
        CompileOptions carry = opt;
        carry.carryScriptables = true;
        CompileReport rep4;
        const std::string out4 = scratch("scriptables_carry");
        CHECK(compileIw8Extraction(*pr.map, out4, carry, rep4, &err));
        const fs::path mp4 = fs::path(out4) / "extracted" / "maps" / "mp";
        const std::string col4 = readAll((mp4 / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
        size_t entries4 = 0;
        for (size_t p2 = 0; (p2 = col4.find("\\"definition\\":", p2)) != std::string::npos; ++p2) ++entries4;
        CHECK(entries4 == 3);
        CHECK(col4.find("{\\"definition\\":\\"scriptable_door_x\\",\\"origin\\":[0,0,0],\\"angles\\":[0,0,0],\\"carry_zone\\":\\"mp_backlot2\\","
                        "\\"server_instanced_parts\\":1,\\"usable_parts\\":1,\\"physics_limit\\":true,\\"max_dynent_parts\\":2,"
                        "\\"instance_dynent\\":true,\\"sparse_occluder\\":true,\\"collision_flags\\":1,\\"physics_dynents\\":0}") != std::string::npos);
        CHECK(col4.find("\\"definition\\":\\"hardpoint_chevron\\",\\"origin\\":[64,32,16],\\"angles\\":[0,90,0],\\"targetname\\":\\"hp_a\\","
                        "\\"script_noteworthy\\":\\"zone_1\\",\\"server_instanced_parts\\"") != std::string::npos);  // no carry_zone
        bool said = false;
        for (const auto& w : rep4.warnings) said |= w.find("1 scriptable instance(s) carry their definition from the stock zone mp_backlot2") != std::string::npos;
        CHECK(said);
        const std::string gsc = readAll((mp4 / (std::string(kSampleId) + ".gsc")).string());
        const size_t load = gsc.find("maps\\\\mp\\\\_load::main();"), doors = gsc.find("scripts\\\\engine\\\\scriptable_door::system_init();");
        CHECK(load != std::string::npos && doors != std::string::npos && doors > load);
    }
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
