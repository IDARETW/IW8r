#include "stock_install.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace iw8r {

namespace fs = std::filesystem;

namespace {

std::string lowerExt(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}
bool isPatchName(const fs::path& p) { return lowerExt(p) == ".fp"; }
bool isZoneName(const fs::path& p) { return lowerExt(p) == ".ff"; }

bool fail(std::string* err, const std::string& what) {
    if (err) *err = what;
    return false;
}

const char* const kOurs = ".ours";
const char* const kStock = ".stock";

std::vector<std::string> filesWith(const std::string& dir, bool (*wanted)(const fs::path&)) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && wanted(it->path())) out.push_back(it->path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

// The names marked "<name>.ours" in a backup folder whose own extension is `ext`.
std::vector<std::string> marked(const std::string& backupDir, const char* ext) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::string suffix = kOurs;
    for (fs::directory_iterator it(fs::path(backupDir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!it->is_regular_file(ec) || name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        const std::string file = name.substr(0, name.size() - suffix.size());
        if (lowerExt(fs::path(file)) == ext) out.push_back(file);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A move that also works across drives (rename, else copy and delete). Never overwrites.
bool moveFile(const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (fs::exists(to, ec)) {
        ec = std::make_error_code(std::errc::file_exists);
        return false;
    }
    fs::rename(from, to, ec);
    if (!ec) return true;
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::none, ec);
    if (ec) return false;
    fs::remove(from, ec);
    if (ec) {  // the source stays: take the copy away again, so nothing is doubled
        std::error_code ignore;
        fs::remove(to, ignore);
        return false;
    }
    return true;
}

uint32_t u32At(const std::string& s, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, s.data() + at, 4);
    return v;
}

}  // namespace

// --- Whole zones ------------------------------------------------------------------------------------------------------

bool unsignedReplayZoneHeader(const std::string& h, std::string* why) {
    // The owner's client, DB_ResetFileReader (DB_IsReplayIwcHeader): the fields of the 0x88-byte zone header.
    if (h.size() < 0x88) return fail(why, "the file is shorter than a zone header");
    if (h.compare(0, 8, "IWffa100") == 0) return fail(why, "it is a signed zone (IWffa100): only the game's own files carry a valid signature");
    if (h.compare(0, 8, "IWffc100") != 0) return fail(why, "it is not a zone file (no IWffc100 header)");
    if (u32At(h, 8) != 11) return fail(why, "its header version is " + std::to_string(u32At(h, 8)) + ", not 11");
    if (u32At(h, 12) != 0xFF7) return fail(why, "its zone version is " + std::to_string(u32At(h, 12)) + ", not 4087 (0xFF7)");
    if (h[16] != 0 || h[17] != 0) return fail(why, "its header has a compression or encryption flag");
    if (static_cast<unsigned char>(h[18]) > 1 || h[19] != 0) return fail(why, "it is a streamed (transient) zone of a kind the client does not load unsigned");
    if (u32At(h, 24) != 0) return fail(why, "its header has a value where the client wants 0");
    uint64_t decoded = 0;
    std::memcpy(&decoded, h.data() + 32, 8);
    if (decoded == 0 || decoded != u32At(h, 28))
        return fail(why, decoded > 0xFFFFFFFFull ? "its data is larger than 4 GB, which the client does not load unsigned"
                                                 : "the two decoded sizes of its header differ");
    return true;
}

bool unsignedReplayZone(const std::string& file, std::string* why) {
    std::ifstream f(fs::path(file), std::ios::binary);
    std::string header(0x88, '\0');
    if (!f || !f.read(header.data(), std::streamsize(header.size()))) return fail(why, "the file cannot be read or is shorter than a zone header");
    return unsignedReplayZoneHeader(header, why);
}

bool unsignedPatchOfSignedZone(const std::string& file) {
    std::ifstream f(fs::path(file), std::ios::binary);
    std::string h(0x148 + 8, '\0');
    if (!f || !f.read(h.data(), std::streamsize(h.size()))) return false;  // no data after the headers: nothing to refuse
    return h.compare(0, 8, "IWffd100") == 0 && h.compare(0x38, 8, "IWffa100") == 0 && h.compare(0x148, 8, "IWffs100") != 0;
}

std::vector<std::string> stockZoneFiles(const std::string& outDir) { return filesWith(outDir, isZoneName); }

std::vector<std::string> installedStockZones(const std::string& backupDir) { return marked(backupDir, ".ff"); }

std::vector<StockPatchOverride> installedStockZoneOverrides(const std::string& backupRoot, const std::string& stockDir) {
    std::vector<StockPatchOverride> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(backupRoot), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        for (const std::string& name : installedStockZones(it->path().string())) {
            const fs::path kept = fs::path(stockDir) / fs::path(name);
            std::error_code sec;
            if (fs::is_regular_file(kept, sec)) out.push_back({name, kept.string()});
        }
    }
    std::sort(out.begin(), out.end(), [](const StockPatchOverride& a, const StockPatchOverride& b) { return a.name < b.name; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool installStockZones(const std::string& outDir, const std::string& zoneDir, const std::string& stockDir,
                       const std::string& backupDir, StockInstallReport& report, std::string* err) {
    const std::vector<std::string> files = stockZoneFiles(outDir);
    if (files.empty()) return fail(err, "no .ff zone file in " + outDir);
    std::error_code ec;
    if (!fs::is_directory(fs::path(zoneDir), ec)) return fail(err, "the zone folder " + zoneDir + " does not exist");
    // Everything is checked before anything is changed.
    for (const std::string& name : files) {
        std::string why;
        if (!unsignedReplayZone((fs::path(outDir) / fs::path(name)).string(), &why))
            return fail(err, name + " cannot be installed: " + why);
        if (!fs::is_regular_file(fs::path(zoneDir) / fs::path(name), ec) && !fs::is_regular_file(fs::path(stockDir) / fs::path(name), ec))
            return fail(err, "the stock zone " + name + " is not in the zone folder " + zoneDir);
    }
    fs::create_directories(fs::path(backupDir), ec);
    if (ec) return fail(err, "cannot create the backup folder " + backupDir + ": " + ec.message());
    fs::create_directories(fs::path(stockDir), ec);
    if (ec) return fail(err, "cannot create the folder for the stock files " + stockDir + ": " + ec.message());
    // Patch files of the old method would be read with the new zone and stop the load: out first.
    if (!restoreStockPatches(backupDir, zoneDir, report, err)) return false;

    for (const std::string& name : files) {
        const fs::path source = fs::path(outDir) / fs::path(name);
        const fs::path target = fs::path(zoneDir) / fs::path(name);
        const fs::path kept = fs::path(stockDir) / fs::path(name);
        const fs::path marker = fs::path(backupDir) / fs::path(name + kOurs);
        fs::path patch = target, keptPatch = kept;
        patch.replace_extension(".fp");
        keptPatch.replace_extension(".fp");
        const std::string patchName = patch.filename().string();

        bool movedZone = false, movedPatch = false;
        // No kept file and no marker: the file in the zone folder is the stock one, keep it. (With a marker and no
        // kept file the stock zone was lost outside the editor; our older edit is then just replaced.)
        if (!fs::exists(kept, ec) && !fs::exists(marker, ec)) {
            if (!moveFile(target, kept, ec))
                return fail(err, "cannot move the stock " + name + " to " + stockDir + " (is the game running?): " + ec.message());
            movedZone = true;
            report.backedUp.push_back(name);
        }
        // The stock patch belongs to the stock zone only (the game refuses it on any other file).
        if (fs::exists(patch, ec)) {
            if (unsignedPatchOfSignedZone(patch.string())) {  // of an earlier build of this editor: it can never load
                fs::remove(patch, ec);
                if (ec) return fail(err, "cannot remove " + patch.string() + " (is the game running?): " + ec.message());
                report.removed.push_back(patchName);
            } else if (!fs::exists(keptPatch, ec)) {
                if (!moveFile(patch, keptPatch, ec)) {
                    const std::string why = ec.message();
                    if (movedZone) moveFile(kept, target, ec);
                    return fail(err, "cannot move the stock " + patchName + " to " + stockDir + " (is the game running?): " + why);
                }
                movedPatch = true;
                report.backedUp.push_back(patchName);
            } else {
                fs::remove(patch, ec);  // the stock patch is kept already: this one is a leftover
                if (ec) return fail(err, "cannot remove " + patch.string() + " (is the game running?): " + ec.message());
                report.removed.push_back(patchName);
            }
        }
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            const std::string why = ec.message();
            std::error_code ignore;
            if (movedZone) {
                fs::remove(target, ignore);
                moveFile(kept, target, ignore);
            }
            if (movedPatch) moveFile(keptPatch, patch, ignore);
            return fail(err, "cannot copy " + name + " into the zone folder (disk space? is the game running?): " + why);
        }
        std::ofstream(marker, std::ios::binary | std::ios::trunc) << "installed by IW8 Radiant\n";
        report.installed.push_back(name);
    }
    return true;
}

bool restoreStockZones(const std::string& backupDir, const std::string& zoneDir, const std::string& stockDir,
                       StockInstallReport& report, std::string* err) {
    std::error_code ec;
    if (!restoreStockPatches(backupDir, zoneDir, report, err)) return false;
    for (const std::string& name : installedStockZones(backupDir)) {
        const fs::path target = fs::path(zoneDir) / fs::path(name);
        const fs::path kept = fs::path(stockDir) / fs::path(name);
        const fs::path marker = fs::path(backupDir) / fs::path(name + kOurs);
        fs::path patch = target, keptPatch = kept;
        patch.replace_extension(".fp");
        keptPatch.replace_extension(".fp");
        // Without the stock file our zone stays: a map that loads edited is better than one that is gone.
        if (!fs::is_regular_file(kept, ec))
            return fail(err, "the stock " + name + " is not in " + stockDir + ": nothing was changed for it. Put the stock file back there, "
                             "or verify the game files, then restore again");
        if (fs::exists(target, ec)) {
            fs::remove(target, ec);
            if (ec) return fail(err, "cannot remove " + target.string() + " (is the game running?): " + ec.message());
            report.removed.push_back(name);
        }
        if (!moveFile(kept, target, ec)) return fail(err, "cannot put back the stock " + name + ": " + ec.message());
        report.restored.push_back(name);
        if (fs::is_regular_file(keptPatch, ec)) {
            if (fs::exists(patch, ec)) fs::remove(patch, ec);
            if (!moveFile(keptPatch, patch, ec)) return fail(err, "cannot put back the stock " + patch.filename().string() + ": " + ec.message());
            report.restored.push_back(patch.filename().string());
        }
        fs::remove(marker, ec);
    }
    return true;
}

// --- Patch files (the old method) --------------------------------------------------------------------------------------

std::vector<std::string> stockPatchFiles(const std::string& patchDir) { return filesWith(patchDir, isPatchName); }

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

std::vector<std::string> installedStockPatches(const std::string& backupDir) { return marked(backupDir, ".fp"); }

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
