import io

# The first-person look (research 128) as a product feature, core side: the edits file of a "shared look": only the
# technique sets of a shared zone's materials that match a pattern get the map shader's filter, and every name stays
# (other zones use the sets and shaders by name).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/stock_zone_edit.h', '''std::string stockLitFilterJson(const std::string& programPath, const std::string& screenProgramPath = {});
''', '''std::string stockLitFilterJson(const std::string& programPath, const std::string& screenProgramPath = {});

// The first-person look (research 128): the weapon and hands a player holds keep their materials and technique sets in
// a zone every map loads. The filter goes only into the sets of that zone's materials whose name matches the pattern
// (a regular expression, searched without case), and no set or shader changes its name, because other zones use them
// by name. While the result is installed, the look is on every map.
inline constexpr char kViewmodelLookZone[] = "global_stream_mp";
inline constexpr char kViewmodelLookMaterials[] = "(_vm_|/vm_|_vm$|_arm_[lr]_)";
// The edits file of such a look for the converter's editzone (only the techset zone is written, no patch file).
// `stockZones` as for stockEditJson: the kept stock files of the edits that are installed in the game.
std::string stockSharedLookEditJson(const std::string& programPath, const std::string& screenProgramPath,
                                    const std::string& materials = kViewmodelLookMaterials,
                                    const std::vector<StockPatchOverride>* stockZones = nullptr);
''')

patch('src/core/stock_zone_edit.cpp', '''std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints, const std::string* placedObjects,''',
      '''std::string stockSharedLookEditJson(const std::string& programPath, const std::string& screenProgramPath, const std::string& materials,
                                    const std::vector<StockPatchOverride>* stockZones) {
    std::string filter = stockLitFilterJson(programPath, screenProgramPath);
    filter.pop_back();  // the closing brace
    filter += ",\\"keep_names\\":true,\\"materials\\":" + jsonText(materials) + "}";
    std::string json = "{\\"techsets_only\\":true,\\"patch\\":false,\\"lit_filter\\":" + filter;
    if (stockZones && !stockZones->empty()) {
        json += ",\\"stock_zones\\":{";
        for (size_t i = 0; i < stockZones->size(); ++i)
            json += std::string(i ? "," : "") + jsonText((*stockZones)[i].name) + ":" + jsonText((*stockZones)[i].stockFile);
        json += "}";
    }
    return json + "}";
}

std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints, const std::string* placedObjects,''')

patch('tests/stock_zone_edit_tests.cpp', '''TEST(stock_zone_edit_world_removal) {''',
      '''TEST(stock_zone_edit_shared_look) {
    // The first-person look: only the techset zone, no patch file, the names kept, only the matching materials.
    const std::string json = stockSharedLookEditJson("C:\\\\w\\\\f.cso", "C:\\\\w\\\\s.cso");
    CHECK(json.rfind("{\\"techsets_only\\":true,\\"patch\\":false,\\"lit_filter\\":{\\"program\\":\\"C:\\\\\\\\w\\\\\\\\f.cso\\",\\"screen_program\\":\\"C:\\\\\\\\w\\\\\\\\s.cso\\",", 0) == 0);
    CHECK(json.find("\\"keep_names\\":true,\\"materials\\":\\"(_vm_|/vm_|_vm$|_arm_[lr]_)\\"}}") != std::string::npos);
    CHECK(json.find("\\"shared\\":[\\"techsets_common_mp\\"") != std::string::npos);
    CHECK(json.find("stock_zones") == std::string::npos);
    CHECK(json.back() == '}');
    // Without a screen program; an installed edit's kept stock file is named.
    const std::vector<StockPatchOverride> kept{{"techsets_global_stream_mp.ff", "D:\\\\g\\\\iw8r_stock\\\\techsets_global_stream_mp.ff"}};
    const std::string two = stockSharedLookEditJson("f.cso", "", "_vm_", &kept);
    CHECK(two.find("screen_program") == std::string::npos);
    CHECK(two.find("\\"materials\\":\\"_vm_\\"}") != std::string::npos);
    CHECK(two.find(",\\"stock_zones\\":{\\"techsets_global_stream_mp.ff\\":\\"D:\\\\\\\\g\\\\\\\\iw8r_stock\\\\\\\\techsets_global_stream_mp.ff\\"}}") != std::string::npos);
    CHECK(std::string(kViewmodelLookZone) == "global_stream_mp");
}

TEST(stock_zone_edit_world_removal) {''')
print('ok')
