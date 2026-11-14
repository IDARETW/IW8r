import io

# stock_install: a patch file made by an earlier build of this editor (unsigned data on a signed zone) can never load
# (research 116). The new install removes it instead of keeping it as "the stock patch".

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/stock_install.h', '''// The *.ff files of a rebuild's output folder, sorted (what an install would copy).
''', '''// Is this a patch file (<zone>.fp) with unsigned data for a signed zone? The game stops with a disc read error on
// such a file; the game's own patches of signed zones carry signed data ("IWffs100" after the 0x148-byte header).
// Earlier builds of this editor wrote such patches.
bool unsignedPatchOfSignedZone(const std::string& file);

// The *.ff files of a rebuild's output folder, sorted (what an install would copy).
''')
patch('src/core/stock_install.h', '''//  - the stock <zone>.ff, and the stock <zone>.fp when the game has one, are moved to stockDir (kept under their own
//    names; a file already kept there is never overwritten);
''', '''//  - the stock <zone>.ff, and the stock <zone>.fp when the game has one, are moved to stockDir (kept under their own
//    names; a file already kept there is never overwritten); a <zone>.fp that can never load
//    (unsignedPatchOfSignedZone) is removed instead;
''')
C = 'src/core/stock_install.cpp'
patch(C, '''std::vector<std::string> stockZoneFiles(const std::string& outDir) { return filesWith(outDir, isZoneName); }
''', '''bool unsignedPatchOfSignedZone(const std::string& file) {
    std::ifstream f(fs::path(file), std::ios::binary);
    std::string h(0x148 + 8, '\\0');
    if (!f || !f.read(h.data(), std::streamsize(h.size()))) return false;  // no data after the headers: nothing to refuse
    return h.compare(0, 8, "IWffd100") == 0 && h.compare(0x38, 8, "IWffa100") == 0 && h.compare(0x148, 8, "IWffs100") != 0;
}

std::vector<std::string> stockZoneFiles(const std::string& outDir) { return filesWith(outDir, isZoneName); }
''')
patch(C, '''        if (fs::exists(patch, ec)) {
            if (!fs::exists(keptPatch, ec)) {''', '''        if (fs::exists(patch, ec)) {
            if (unsignedPatchOfSignedZone(patch.string())) {  // of an earlier build of this editor: it can never load
                fs::remove(patch, ec);
                if (ec) return fail(err, "cannot remove " + patch.string() + " (is the game running?): " + ec.message());
                report.removed.push_back(patchName);
            } else if (!fs::exists(keptPatch, ec)) {''')
T = 'tests/stock_install_tests.cpp'
s = io.open(ROOT + T, encoding='utf-8', newline='').read()
s = s.rstrip() + '''

// Research 116: a patch file of an earlier build (unsigned data on a signed zone) stops the game with a disc read
// error. It is recognised and removed by an install; the game's own patches (signed data) are kept.
TEST(stock_install_removes_unloadable_patch) {
    const auto patchFile = [](const char* baseMagic, const std::string& data) {
        std::string p(0x148, '\\0');
        std::memcpy(p.data(), "IWffd100", 8);
        std::memcpy(p.data() + 0x38, baseMagic, 8);
        std::memcpy(p.data() + 0xC0, "IWffc100", 8);
        return p + data;
    };
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_stray";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path out = root / "out", zone = root / "game" / "zone", keep = root / "game" / "iw8r_stock", backup = root / "backup" / "mp_a";
    put(root / "ours.fp", patchFile("IWffa100", std::string("\\x02IWC", 4) + "delta data"));
    put(root / "stock.fp", patchFile("IWffa100", "IWffs100 signed data"));
    put(root / "unsigned_base.fp", patchFile("IWffc100", std::string("\\x02IWC", 4) + "delta data"));
    put(root / "header_only.fp", patchFile("IWffa100", ""));
    CHECK(unsignedPatchOfSignedZone((root / "ours.fp").string()));
    CHECK(!unsignedPatchOfSignedZone((root / "stock.fp").string()));
    CHECK(!unsignedPatchOfSignedZone((root / "unsigned_base.fp").string()));  // the client loads these
    CHECK(!unsignedPatchOfSignedZone((root / "header_only.fp").string()));
    CHECK(!unsignedPatchOfSignedZone((root / "missing.fp").string()));

    put(zone / "techsets_mp_a.ff", unsignedZone("stock techsets", 0xFF7, "IWffa100"));
    fs::copy_file(root / "ours.fp", zone / "techsets_mp_a.fp", ec);  // copied in by hand from an earlier build
    put(out / "techsets_mp_a.ff", unsignedZone("edited techsets"));
    StockInstallReport r;
    std::string err;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r, &err));
    CHECK((r.removed == std::vector<std::string>{"techsets_mp_a.fp"}) && (r.backedUp == std::vector<std::string>{"techsets_mp_a.ff"}));
    CHECK(!fs::exists(zone / "techsets_mp_a.fp") && !fs::exists(keep / "techsets_mp_a.fp"));
    StockInstallReport r2;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r2, &err));
    CHECK(!fs::exists(zone / "techsets_mp_a.fp") && get(zone / "techsets_mp_a.ff") == unsignedZone("stock techsets", 0xFF7, "IWffa100"));
    fs::remove_all(root, ec);
}
'''
io.open(ROOT + T, 'w', encoding='utf-8', newline='').write(s)
print('ok')
