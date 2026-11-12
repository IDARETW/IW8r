#include "stock_install.h"
#include "test_harness.h"

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
