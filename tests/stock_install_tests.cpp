#include "stock_install.h"
#include "test_harness.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace iw8r;
namespace fs = std::filesystem;

namespace {
void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string get(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

// 11.2 (research 88): an edited stock map installs as .fp patch files beside the stock zones; a stock patch of the
// same name is backed up once and put back on restore. Only temporary folders are used here.
// The game's own patch files carry map data, and an installed edit replaces them: every installed edit is listed with
// the stock patch saved at install, so a later rebuild reads the stock map again.
TEST(stock_install_patch_overrides) {
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_overrides";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path zone = root / "game" / "zone", backups = root / "stock_backup";
    CHECK(installedStockPatchOverrides(backups.string()).empty());  // nothing there yet
    put(zone / "mp_b.ff", "stock zone b");
    put(zone / "mp_b.fp", "stock patch of b");  // the game ships this one
    put(zone / "techsets_mp_b.ff", "stock techsets b");
    put(zone / "mp_a.ff", "stock zone a");
    put(root / "out_b" / "patch" / "mp_b.fp", "our patch of b");
    put(root / "out_b" / "patch" / "techsets_mp_b.fp", "our techsets patch of b");
    put(root / "out_a" / "patch" / "mp_a.fp", "our patch of a");
    StockInstallReport r;
    std::string err;
    CHECK(installStockPatches((root / "out_b" / "patch").string(), zone.string(), (backups / "mp_b").string(), r, &err));
    CHECK(installStockPatches((root / "out_a" / "patch").string(), zone.string(), (backups / "mp_a").string(), r, &err));
    put(backups / "notes.txt", "not a backup folder");
    const std::vector<StockPatchOverride> o = installedStockPatchOverrides(backups.string());
    CHECK(o.size() == 3);
    if (o.size() == 3) {
        CHECK(o[0].name == "mp_a.fp" && o[0].stockFile.empty());  // the stock game has no patch of that name
        CHECK(o[1].name == "mp_b.fp" && fs::path(o[1].stockFile) == backups / "mp_b" / "mp_b.fp.stock" && get(o[1].stockFile) == "stock patch of b");
        CHECK(o[2].name == "techsets_mp_b.fp" && o[2].stockFile.empty());
    }
    // After a restore of b only a's edit is listed.
    StockInstallReport r2;
    CHECK(restoreStockPatches((backups / "mp_b").string(), zone.string(), r2, &err));
    CHECK((installedStockPatchOverrides(backups.string()) == std::vector<StockPatchOverride>{{"mp_a.fp", ""}}));
    fs::remove_all(root, ec);
}

TEST(stock_install_patches_and_restore) {
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path patch = root / "out" / "patch", zone = root / "game" / "zone", backup = root / "backup";
    put(zone / "mp_a.ff", "stock zone a");
    put(zone / "techsets_mp_a.ff", "stock techsets a");
    put(zone / "techsets_mp_a.fp", "stock patch of techsets a");  // a stock patch of the same name
    put(patch / "mp_a.fp", "our patch 1");
    put(patch / "techsets_mp_a.fp", "our techsets patch 1");
    put(patch / "notes.txt", "not a patch");

    CHECK((stockPatchFiles(patch.string()) == std::vector<std::string>{"mp_a.fp", "techsets_mp_a.fp"}));
    CHECK(installedStockPatches(backup.string()).empty());

    StockInstallReport r;
    std::string err;
    CHECK(installStockPatches(patch.string(), zone.string(), backup.string(), r, &err));
    CHECK((r.installed == std::vector<std::string>{"mp_a.fp", "techsets_mp_a.fp"}));
    CHECK((r.backedUp == std::vector<std::string>{"techsets_mp_a.fp"}));
    CHECK(get(zone / "mp_a.fp") == "our patch 1" && get(zone / "techsets_mp_a.fp") == "our techsets patch 1");
    CHECK(get(backup / "techsets_mp_a.fp.stock") == "stock patch of techsets a");
    CHECK(get(zone / "mp_a.ff") == "stock zone a");  // the zones are never touched
    CHECK((installedStockPatches(backup.string()) == std::vector<std::string>{"mp_a.fp", "techsets_mp_a.fp"}));

    // A second install (a newer edit) overwrites ours and keeps the first backup.
    put(patch / "mp_a.fp", "our patch 2");
    put(patch / "techsets_mp_a.fp", "our techsets patch 2");
    StockInstallReport r2;
    CHECK(installStockPatches(patch.string(), zone.string(), backup.string(), r2, &err));
    CHECK(r2.backedUp.empty() && r2.installed.size() == 2);
    CHECK(get(zone / "mp_a.fp") == "our patch 2" && get(backup / "techsets_mp_a.fp.stock") == "stock patch of techsets a");

    // Restore: our files go, the stock patch comes back, nothing stays marked.
    StockInstallReport r3;
    CHECK(restoreStockPatches(backup.string(), zone.string(), r3, &err));
    CHECK((r3.removed == std::vector<std::string>{"mp_a.fp", "techsets_mp_a.fp"}));
    CHECK((r3.restored == std::vector<std::string>{"techsets_mp_a.fp"}));
    CHECK(!fs::exists(zone / "mp_a.fp") && get(zone / "techsets_mp_a.fp") == "stock patch of techsets a");
    CHECK(installedStockPatches(backup.string()).empty());
    StockInstallReport r4;
    CHECK(restoreStockPatches(backup.string(), zone.string(), r4, &err) && r4.removed.empty() && r4.restored.empty());

    // Refusals: no patch files, a missing zone folder, a patch whose stock zone is not there.
    StockInstallReport bad;
    CHECK(!installStockPatches((root / "empty").string(), zone.string(), backup.string(), bad, &err) && err.find("no .fp") != std::string::npos);
    CHECK(!installStockPatches(patch.string(), (root / "nozone").string(), backup.string(), bad, &err));
    put(patch / "mp_other.fp", "patch without its zone");
    StockInstallReport orphan;
    CHECK(!installStockPatches(patch.string(), zone.string(), backup.string(), orphan, &err) && err.find("mp_other.ff") != std::string::npos);
    fs::remove_all(root, ec);
}

namespace {
// A zone header as the converter writes it for an unsigned zone (0x88 bytes), then some payload.
std::string unsignedZone(const std::string& payload, uint32_t xfile = 0xFF7, const char* magic = "IWffc100") {
    std::string h(0x88, '\0');
    std::memcpy(h.data(), magic, 8);
    const uint32_t version = 11, resident = uint32_t(payload.size()), decodedLow = 0x1234;
    const uint64_t decoded = 0x1234;
    std::memcpy(h.data() + 8, &version, 4);
    std::memcpy(h.data() + 12, &xfile, 4);
    std::memcpy(h.data() + 20, &resident, 4);
    std::memcpy(h.data() + 28, &decodedLow, 4);
    std::memcpy(h.data() + 32, &decoded, 8);
    return h + payload;
}
}  // namespace

// Research 116: what the owner's client takes as an unsigned zone (its DB_ResetFileReader).
TEST(stock_install_unsigned_zone_header) {
    std::string why;
    CHECK(unsignedReplayZoneHeader(unsignedZone("x"), &why));
    CHECK(!unsignedReplayZoneHeader(unsignedZone("x", 0xFF7, "IWffa100"), &why) && why.find("signed") != std::string::npos);
    CHECK(!unsignedReplayZoneHeader(unsignedZone("x", 0xFF5), &why) && why.find("zone version") != std::string::npos);
    CHECK(!unsignedReplayZoneHeader("IWffc100", &why) && why.find("shorter") != std::string::npos);
    std::string h = unsignedZone("x");
    h[16] = 1;  // the compression flag of a signed stock zone
    CHECK(!unsignedReplayZoneHeader(h, &why));
    h = unsignedZone("x");
    h[18] = 3;  // a streamed zone kind
    CHECK(!unsignedReplayZoneHeader(h, &why));
    h = unsignedZone("x");
    h[28] = 0x35;  // the two decoded sizes differ
    CHECK(!unsignedReplayZoneHeader(h, &why) && why.find("decoded sizes") != std::string::npos);
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_header";
    std::error_code ec;
    fs::remove_all(root, ec);
    put(root / "a.ff", unsignedZone("payload"));
    put(root / "short.ff", "IWffc100");
    CHECK(unsignedReplayZone((root / "a.ff").string(), &why));
    CHECK(!unsignedReplayZone((root / "short.ff").string(), &why));
    CHECK(!unsignedReplayZone((root / "missing.ff").string(), &why));
    fs::remove_all(root, ec);
}

// Research 116: an edited stock map installs as whole unsigned zones; the stock zone and its stock patch are moved to
// a keep folder and moved back on restore. Only temporary folders are used here.
TEST(stock_install_zones_and_restore) {
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_zones";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path out = root / "work" / "out", zone = root / "game" / "zone", keep = root / "game" / "iw8r_stock";
    const fs::path backups = root / "stock_backup", backup = backups / "mp_a";
    const std::string stockA = unsignedZone("stock a", 0xFF7, "IWffa100"), stockT = unsignedZone("stock techsets", 0xFF7, "IWffa100");
    put(zone / "mp_a.ff", stockA);
    put(zone / "mp_a.fp", "stock patch of a");  // the game ships a patch for this zone
    put(zone / "techsets_mp_a.ff", stockT);
    put(zone / "mp_b.ff", "another map");
    const std::string editA = unsignedZone("edited a"), editT = unsignedZone("edited techsets");
    put(out / "mp_a.ff", editA);
    put(out / "techsets_mp_a.ff", editT);
    put(out / "mp_a.edit-report.json", "{}");
    put(out / "patch" / "mp_a.fp", "not installed any more");

    CHECK((stockZoneFiles(out.string()) == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK(installedStockZones(backup.string()).empty());
    CHECK(installedStockZoneOverrides(backups.string(), keep.string()).empty());

    StockInstallReport r;
    std::string err;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r, &err));
    CHECK((r.installed == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK((r.backedUp == std::vector<std::string>{"mp_a.ff", "mp_a.fp", "techsets_mp_a.ff"}));
    CHECK(get(zone / "mp_a.ff") == editA && get(zone / "techsets_mp_a.ff") == editT);
    CHECK(!fs::exists(zone / "mp_a.fp"));  // the stock patch would stop the load of the edited zone
    CHECK(get(keep / "mp_a.ff") == stockA && get(keep / "mp_a.fp") == "stock patch of a" && get(keep / "techsets_mp_a.ff") == stockT);
    CHECK(get(zone / "mp_b.ff") == "another map");
    CHECK((installedStockZones(backup.string()) == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK(installedStockPatches(backup.string()).empty());
    const std::vector<StockPatchOverride> o = installedStockZoneOverrides(backups.string(), keep.string());
    CHECK(o.size() == 2);
    if (o.size() == 2) CHECK(o[0].name == "mp_a.ff" && fs::path(o[0].stockFile) == keep / "mp_a.ff" && o[1].name == "techsets_mp_a.ff");

    // A newer edit replaces ours; the kept stock files are not touched.
    const std::string editA2 = unsignedZone("edited a, second time");
    put(out / "mp_a.ff", editA2);
    StockInstallReport r2;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r2, &err));
    CHECK(r2.backedUp.empty() && r2.installed.size() == 2);
    CHECK(get(zone / "mp_a.ff") == editA2 && get(keep / "mp_a.ff") == stockA);

    // Restore: our zones go, the stock zone and its patch come back, nothing stays marked or kept.
    StockInstallReport r3;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r3, &err));
    CHECK((r3.removed == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK((r3.restored == std::vector<std::string>{"mp_a.ff", "mp_a.fp", "techsets_mp_a.ff"}));
    CHECK(get(zone / "mp_a.ff") == stockA && get(zone / "mp_a.fp") == "stock patch of a" && get(zone / "techsets_mp_a.ff") == stockT);
    CHECK(!fs::exists(keep / "mp_a.ff") && !fs::exists(keep / "mp_a.fp") && installedStockZones(backup.string()).empty());
    StockInstallReport r4;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r4, &err) && r4.removed.empty() && r4.restored.empty());

    // Refusals, each before anything is changed: a signed or foreign file, a zone the game does not have, no files.
    put(out / "mp_a.ff", unsignedZone("signed", 0xFF7, "IWffa100"));
    StockInstallReport bad;
    CHECK(!installStockZones(out.string(), zone.string(), keep.string(), backup.string(), bad, &err) && err.find("signed") != std::string::npos);
    CHECK(bad.installed.empty() && bad.backedUp.empty() && get(zone / "techsets_mp_a.ff") == stockT && !fs::exists(keep / "techsets_mp_a.ff"));
    put(out / "mp_a.ff", editA);
    put(out / "mp_new.ff", unsignedZone("a zone the game does not have"));
    CHECK(!installStockZones(out.string(), zone.string(), keep.string(), backup.string(), bad, &err) && err.find("mp_new.ff") != std::string::npos);
    CHECK(get(zone / "mp_a.ff") == stockA);
    fs::remove(out / "mp_new.ff", ec);
    CHECK(!installStockZones((root / "empty").string(), zone.string(), keep.string(), backup.string(), bad, &err) && err.find("no .ff") != std::string::npos);

    // A patch of the old method is taken out by the new install, and its stock patch is kept with the stock zone.
    put(root / "old" / "patch" / "mp_a.fp", "our old patch");
    StockInstallReport oldR;
    CHECK(installStockPatches((root / "old" / "patch").string(), zone.string(), backup.string(), oldR, &err));
    CHECK(get(zone / "mp_a.fp") == "our old patch" && (installedStockPatches(backup.string()) == std::vector<std::string>{"mp_a.fp"}));
    StockInstallReport r5;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r5, &err));
    CHECK(installedStockPatches(backup.string()).empty() && !fs::exists(zone / "mp_a.fp"));
    CHECK(get(keep / "mp_a.fp") == "stock patch of a" && get(zone / "mp_a.ff") == editA);

    // The kept stock zone went missing: restore changes nothing for it and says so.
    fs::rename(keep / "mp_a.ff", root / "lost.ff", ec);
    StockInstallReport r6;
    CHECK(!restoreStockZones(backup.string(), zone.string(), keep.string(), r6, &err) && err.find("mp_a.ff") != std::string::npos);
    CHECK(get(zone / "mp_a.ff") == editA);
    CHECK(installedStockZoneOverrides(backups.string(), keep.string()).size() == 1);  // only the zone whose stock file is there
    fs::rename(root / "lost.ff", keep / "mp_a.ff", ec);
    StockInstallReport r7;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r7, &err));
    CHECK(get(zone / "mp_a.ff") == stockA && get(zone / "mp_a.fp") == "stock patch of a" && get(zone / "techsets_mp_a.ff") == stockT);
    fs::remove_all(root, ec);
}

// Research 116: a patch file of an earlier build (unsigned data on a signed zone) stops the game with a disc read
// error. It is recognised and removed by an install; the game's own patches (signed data) are kept.
TEST(stock_install_removes_unloadable_patch) {
    const auto patchFile = [](const char* baseMagic, const std::string& data) {
        std::string p(0x148, '\0');
        std::memcpy(p.data(), "IWffd100", 8);
        std::memcpy(p.data() + 0x38, baseMagic, 8);
        std::memcpy(p.data() + 0xC0, "IWffc100", 8);
        return p + data;
    };
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_stray";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path out = root / "out", zone = root / "game" / "zone", keep = root / "game" / "iw8r_stock", backup = root / "backup" / "mp_a";
    put(root / "ours.fp", patchFile("IWffa100", std::string("\x02IWC", 4) + "delta data"));
    put(root / "stock.fp", patchFile("IWffa100", "IWffs100 signed data"));
    put(root / "unsigned_base.fp", patchFile("IWffc100", std::string("\x02IWC", 4) + "delta data"));
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
