#include "stock_install.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace iw8r {

namespace fs = std::filesystem;

namespace {

bool isPatchName(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".fp";
}

bool fail(std::string* err, const std::string& what) {
    if (err) *err = what;
    return false;
}

const char* const kOurs = ".ours";
const char* const kStock = ".stock";

}  // namespace

std::vector<std::string> stockPatchFiles(const std::string& patchDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(patchDir), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && isPatchName(it->path())) out.push_back(it->path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<StockPatchOverride> installedStockPatchOverrides(const std::string& backupRoot) {
    std::vector<StockPatchOverride> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(backupRoot), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        for (const std::string& name : installedStockPatches(it->path().string())) {
            const fs::path saved = it->path() / (name + kStock);
            std::error_code sec;
            out.push_back({name, fs::is_regular_file(saved, sec) ? saved.string() : std::string()});
        }
    }
    std::sort(out.begin(), out.end(), [](const StockPatchOverride& a, const StockPatchOverride& b) { return a.name < b.name; });
    return out;
}

std::vector<std::string> installedStockPatches(const std::string& backupDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(backupDir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suffix = kOurs;
        if (it->is_regular_file(ec) && name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            out.push_back(name.substr(0, name.size() - suffix.size()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool installStockPatches(const std::string& patchDir, const std::string& zoneDir, const std::string& backupDir,
                         StockInstallReport& report, std::string* err) {
    const std::vector<std::string> files = stockPatchFiles(patchDir);
    if (files.empty()) return fail(err, "no .fp patch file in " + patchDir);
    std::error_code ec;
    if (!fs::is_directory(fs::path(zoneDir), ec)) return fail(err, "the zone folder " + zoneDir + " does not exist");
    fs::create_directories(fs::path(backupDir), ec);
    if (ec) return fail(err, "cannot create the backup folder " + backupDir + ": " + ec.message());
    for (const std::string& name : files) {
        const fs::path source = fs::path(patchDir) / fs::path(name);
        const fs::path target = fs::path(zoneDir) / fs::path(name);
        const fs::path marker = fs::path(backupDir) / fs::path(name + kOurs);
        const fs::path backup = fs::path(backupDir) / fs::path(name + kStock);
        // The stock zone the patch belongs to must be there: a patch without its zone does nothing.
        fs::path zone = target;
        zone.replace_extension(".ff");
        if (!fs::is_regular_file(zone, ec)) return fail(err, "the stock zone " + zone.string() + " is not in the zone folder");
        if (fs::exists(target, ec) && !fs::exists(marker, ec) && !fs::exists(backup, ec)) {
            fs::copy_file(target, backup, fs::copy_options::none, ec);
            if (ec) return fail(err, "cannot back up " + target.string() + ": " + ec.message());
            report.backedUp.push_back(name);
        }
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail(err, "cannot copy " + name + " into the zone folder (is the game running?): " + ec.message());
        std::ofstream(marker, std::ios::binary | std::ios::trunc) << "installed by IW8 Radiant\n";
        report.installed.push_back(name);
    }
    return true;
}

bool restoreStockPatches(const std::string& backupDir, const std::string& zoneDir, StockInstallReport& report, std::string* err) {
    std::error_code ec;
    for (const std::string& name : installedStockPatches(backupDir)) {
        const fs::path target = fs::path(zoneDir) / fs::path(name);
        const fs::path marker = fs::path(backupDir) / fs::path(name + kOurs);
        const fs::path backup = fs::path(backupDir) / fs::path(name + kStock);
        if (fs::exists(target, ec)) {
            fs::remove(target, ec);
            if (ec) return fail(err, "cannot remove " + target.string() + " (is the game running?): " + ec.message());
            report.removed.push_back(name);
        }
        if (fs::exists(backup, ec)) {
            fs::copy_file(backup, target, fs::copy_options::overwrite_existing, ec);
            if (ec) return fail(err, "cannot put back the stock " + name + ": " + ec.message());
            report.restored.push_back(name);
        }
        fs::remove(marker, ec);
    }
    return true;
}

}  // namespace iw8r
