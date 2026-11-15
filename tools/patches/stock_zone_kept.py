import io

# Owner report 2026-11-14: Shipment's tm/cw_foliage_ctl_tree_beech_vista_card drew its leaf cards as flat planes.
# Shipment's techsets zone in the game folder is the owner's installed toon edit (Install Edited Stock Map: a whole
# unsigned zone whose technique sets the converter renamed iw8rf_<hash>; the stock file waits in <game>/iw8r_stock).
# The material cache read Shipment's material bodies from the edited zone, and the cut-out mask (texture slot 27) is
# taken only when the technique set's name says alpha test or blend: 33 Shipment materials lost their masks.
#   1. Every ACTS read of a stock zone uses the kept stock file when an installed edit replaced the zone
#      (stockZoneFile, acts_runner; the other zone paths of main_window too).
#   2. Material cache entries resolved from an edited zone (an iw8rf_ technique set) resolve again.
#   3. A technique set whose name gives no blend token (renamed, hashed) still takes slot 27 as the cut-out mask (no
#      stock opaque set binds slot 27: 0 of 3,616 cached rpl materials do).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# 1. The zone file ACTS reads.
patch('src/qtapp/acts_runner.cpp', '''QString zoneFileOf(const QString& replay, const QString& zone) {
    return QFileInfo(replay).absolutePath() + "/zone/" + zone + ".ff";
}''', '''QString zoneFileOf(const QString& replay, const QString& zone) { return stockZoneFile(replay, zone); }''')
patch('src/qtapp/acts_runner.cpp', '''}  // namespace iw8r::qt''', '''QString stockZoneFile(const QString& replay, const QString& zone) {
    const QString game = QFileInfo(replay).absolutePath();
    const QString kept = game + "/iw8r_stock/" + zone + ".ff";
    return QFileInfo::exists(kept) ? kept : game + "/zone/" + zone + ".ff";
}

}  // namespace iw8r::qt''')
patch('src/qtapp/acts_runner.h', '''namespace iw8r::qt {''', '''namespace iw8r::qt {

// The stock file of a zone: <game>/zone/<zone>.ff, or, where an installed edit (Install Edited Stock Map) took that
// file's place, the stock file kept in <game>/iw8r_stock. The editor reads stock data from stock zones: an edited
// zone's technique sets are renamed (iw8rf_<hash>) and its materials lost their cut-out masks (owner report
// 2026-11-14, Shipment's beech vista cards drawn as flat planes).
QString stockZoneFile(const QString& replay, const QString& zone);''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
        for (const QString& ff : {zoneFolder + "techsets_" + zone + ".ff", zoneFolder + zone + ".ff"}) {''',
      '''        for (const QString& ff : {stockZoneFile(tools.replay, "techsets_" + zone), stockZoneFile(tools.replay, zone)}) {''')
patch(M, '''    const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
    const QString ff = zoneFolder + zone + ".ff";''', '''    const QString ff = stockZoneFile(tools.replay, zone);''')
patch(M, '''        for (const QString& z : {zoneFolder + "techsets_" + zone + ".ff", zoneFolder + zone + ".ff"})''',
      '''        for (const QString& z : {stockZoneFile(tools.replay, "techsets_" + zone), stockZoneFile(tools.replay, zone)})''')
patch(M, '''        acts(xp, "image", missing, zoneFolder + zone + ".ff");''', '''        acts(xp, "image", missing, stockZoneFile(tools.replay, zone));''')
patch(M, '''        if (!missing.isEmpty()) acts(xp, "image", missing, zoneFolder + "techsets_" + zone + ".ff");''',
      '''        if (!missing.isEmpty()) acts(xp, "image", missing, stockZoneFile(tools.replay, "techsets_" + zone));''')
patch(M, '''    const QString ff = QFileInfo(replay).absolutePath() + "/zone/" + zone + ".ff";
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache/" + zone + "/com_map";''',
      '''    const QString ff = stockZoneFile(replay, zone);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache/" + zone + "/com_map";''')

# 2 + 3. The material cache.
C = 'src/qtapp/material_cache.cpp'
patch(C, '''        if (!e.failed && o.value("constRules").toInt() < 3 && techsetAnimatesUv(e.techset)) continue;''',
      '''        if (!e.failed && o.value("constRules").toInt() < 3 && techsetAnimatesUv(e.techset)) continue;
        // Read from a zone that an installed edit replaced (its technique sets renamed iw8rf_<hash>; the cut-out masks
        // were lost): resolve again from the stock zone (stockZoneFile).
        if (!e.failed && e.techset.section('/', -1).startsWith("iw8rf_")) continue;''')
patch(C, '''    const MaterialBlend blend = materialBlendFromTechset(work.techset.toStdString());
    const bool opacitySlot = blend != MaterialBlend::Opaque;''', '''    const MaterialBlend blend = materialBlendFromTechset(work.techset.toStdString());
    // A name without a blend token (a renamed or hashed set) still has its opacity in slot 27: no stock opaque set
    // binds that slot.
    const bool opacitySlot = blend != MaterialBlend::Opaque || !materialBlendKnown(work.techset.toStdString());''')

# The core helper and its test.
patch('src/core/material_blend.h', '''const char* materialBlendName(MaterialBlend b);  // "opaque", "alpha test", "blend", "additive"''',
      '''const char* materialBlendName(MaterialBlend b);  // "opaque", "alpha test", "blend", "additive"
// True when the name carries one of the blend tokens (rpl, alt, bad, bar before a t.. table token).
bool materialBlendKnown(const std::string& techset);''')
patch('src/core/material_blend.cpp', '''MaterialBlend materialBlendFromTechset(const std::string& techset) {''', '''namespace {
// The blend token of a technique set name ("" when it has none).
std::string blendToken(const std::string& techset) {
    const size_t slash = techset.rfind('/');
    const std::string leaf = slash == std::string::npos ? techset : techset.substr(slash + 1);
    std::vector<std::string> tok;
    size_t start = 0;
    while (start <= leaf.size()) {
        const size_t us = leaf.find('_', start);
        tok.push_back(leaf.substr(start, us == std::string::npos ? std::string::npos : us - start));
        if (us == std::string::npos) break;
        start = us + 1;
    }
    for (size_t i = 0; i + 1 < tok.size(); ++i) {
        const std::string& t = tok[i];
        if (tok[i + 1].size() >= 2 && tok[i + 1][0] == 't' && (t == "rpl" || t == "alt" || t == "bad" || t == "bar")) return t;
    }
    return {};
}
}  // namespace

bool materialBlendKnown(const std::string& techset) { return !blendToken(techset).empty(); }

MaterialBlend materialBlendFromTechset(const std::string& techset) {''')
patch('tests/material_blend_tests.cpp', '''    CHECK(std::string(materialBlendName(MaterialBlend::Additive)) == "additive");''',
      '''    CHECK(std::string(materialBlendName(MaterialBlend::Additive)) == "additive");
    // A renamed set (an installed stock edit's iw8rf_<hash>) gives no blend: the cache then takes slot 27 as the mask.
    CHECK(!materialBlendKnown("tmo/iw8rf_e4dd71cccd61a1abc624d4ba"));
    CHECK(materialBlendKnown("#tmco/lit_3_lit_alt_ta1_808814_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0"));
    CHECK(materialBlendKnown("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0"));''')
print('ok')
