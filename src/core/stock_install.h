// IW8 Radiant Suite - installing an edited stock map in the game, and taking it out again (roadmap 11.2,
// docs/research/88_stock_map_editing.md, docs/research/116_stock_zone_install.md).
//
// The game's stock zones are signed, and the game reads a patch (<zone>.fp) of a signed zone only when the patch
// carries the game's own signature (research 116: an unsigned patch ends in "Disc read error [3.1]"). So an edited
// stock zone goes in as a whole unsigned zone file, which the owner's client loads under any name: the stock <zone>.ff
// (and its stock patch, which belongs to the stock file only) is moved to a folder beside the zone folder, and the
// edited zone takes its place. Restore moves the stock files back.
//
// These functions only move and copy files between the folders they are given. The editor calls them when the owner
// uses File > Install Edited Stock Map / Restore Stock Map; nothing else writes to the game folder.
// Pure core code.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct StockInstallReport {
    std::vector<std::string> installed;  // file names copied into the zone folder
    std::vector<std::string> backedUp;   // stock files of the same name saved first
    std::vector<std::string> restored;   // stock files put back
    std::vector<std::string> removed;    // our files taken out
};

// --- Whole zones (what the game loads) ------------------------------------------------------------------------------

// Is this file an unsigned zone the owner's client loads (its DB_ResetFileReader: magic "IWffc100", header version
// 11, zone version 0xFF7, no compression or encryption flag, transient type 0 or 1, the two decoded sizes equal and
// not 0)? `why` says what is wrong.
bool unsignedReplayZone(const std::string& file, std::string* why = nullptr);
// The same on the first 0x88 bytes of a zone file.
bool unsignedReplayZoneHeader(const std::string& header, std::string* why = nullptr);

// Is this a patch file (<zone>.fp) with unsigned data for a signed zone? The game stops with a disc read error on
// such a file; the game's own patches of signed zones carry signed data ("IWffs100" after the 0x148-byte header).
// Earlier builds of this editor wrote such patches.
bool unsignedPatchOfSignedZone(const std::string& file);

// The *.ff files of a rebuild's output folder, sorted (what an install would copy).
std::vector<std::string> stockZoneFiles(const std::string& outDir);

// Installs every *.ff of outDir into zoneDir:
//  - a source that is not an unsigned zone the client loads is refused before anything is changed;
//  - patch files of an earlier install by the old method (markers in backupDir) are taken out first;
//  - the stock <zone>.ff, and the stock <zone>.fp when the game has one, are moved to stockDir (kept under their own
//    names; a file already kept there is never overwritten); a <zone>.fp that can never load
//    (unsignedPatchOfSignedZone) is removed instead;
//  - the edited zone is copied to zoneDir/<zone>.ff, and "<zone>.ff.ours" in backupDir marks it as ours.
// stockDir should be on the drive of zoneDir (the move is then a rename and needs no space). Stops at the first error
// (false, *err), with the zone it was working on put back; what was done is in `report`.
bool installStockZones(const std::string& outDir, const std::string& zoneDir, const std::string& stockDir,
                       const std::string& backupDir, StockInstallReport& report, std::string* err = nullptr);

// Undoes every install recorded in backupDir: our zone is deleted from zoneDir and the stock zone and stock patch are
// moved back from stockDir. Patch files of the old method are taken out too. A zone whose stock file is missing from
// stockDir is left as it is (false, *err).
bool restoreStockZones(const std::string& backupDir, const std::string& zoneDir, const std::string& stockDir,
                       StockInstallReport& report, std::string* err = nullptr);

// Zone file names (<zone>.ff) that an install has put into the zone folder and that are still marked.
std::vector<std::string> installedStockZones(const std::string& backupDir);

// For every edit installed by this editor: the file name in the game's zone folder and the stock file to read in its
// place. Whoever reads the "stock" map from the game folder would otherwise read the edit; the converter takes these
// as editzone "stock_zones" (the stock patch lies beside the kept zone and is found there).
struct StockPatchOverride {
    std::string name;       // <zone>.ff or <zone>.fp, as in the game's zone folder
    std::string stockFile;  // the kept stock file ("" for a patch: the stock game has no patch of that name)
    bool operator==(const StockPatchOverride& o) const { return name == o.name && stockFile == o.stockFile; }
};
// `backupRoot` holds one backup folder per map (<backupRoot>/<zone>/). Sorted by name; a zone whose kept stock file
// is missing is left out.
std::vector<StockPatchOverride> installedStockZoneOverrides(const std::string& backupRoot, const std::string& stockDir);

// --- Patch files (the old method; kept to take such installs out and to read around them) ----------------------------

// The *.fp files of a patch folder, sorted.
std::vector<std::string> stockPatchFiles(const std::string& patchDir);

// Copies every *.fp of patchDir into zoneDir. A file of the same name already there that is not from an earlier
// install is first copied to backupDir as "<name>.stock" (an existing backup is never overwritten). "<name>.ours" in
// backupDir marks the installed file as ours. Stops at the first error (false, *err); what was done is in `report`.
// The game does not load such a patch on a signed zone (research 116): the editor no longer installs this way.
bool installStockPatches(const std::string& patchDir, const std::string& zoneDir, const std::string& backupDir,
                         StockInstallReport& report, std::string* err = nullptr);

// Undoes every patch install recorded in backupDir: our file is deleted from zoneDir, and the stock file of that name
// is copied back when a backup exists. The markers are removed; the stock backups are kept.
bool restoreStockPatches(const std::string& backupDir, const std::string& zoneDir, StockInstallReport& report,
                         std::string* err = nullptr);

// Patch names (<zone>.fp) that an install has put into the zone folder and that are still marked.
std::vector<std::string> installedStockPatches(const std::string& backupDir);

// The game ships patch files with real map data (mp_br_quarry.fp, mp_donetsk.fp, ...; a map zone without its patch is
// not even readable as the map). A patch install replaced the patch of its zone. This lists, for every such install,
// the patch file name and the stock patch saved at install ("" when the stock game has no patch of that name): the
// converter reads the zone with that file (editzone "stock_patches").
// `backupRoot` holds one backup folder per map (<backupRoot>/<zone>/). Sorted by name.
std::vector<StockPatchOverride> installedStockPatchOverrides(const std::string& backupRoot);

}  // namespace iw8r
