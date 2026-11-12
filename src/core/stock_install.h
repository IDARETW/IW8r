// IW8 Radiant Suite - installing an edited stock map as patch files, and taking it out again (roadmap 11.2,
// docs/research/88_stock_map_editing.md).
//
// The converter's editzone writes <zone>.fp patches: the game applies a patch beside the stock zone of the same name,
// so an edited stock map installs as one or two small files and the stock zones stay as shipped. Some stock zones
// already have a patch of that name; it is kept in a backup folder and put back on restore.
//
// These functions only copy files between the folders they are given. The editor calls them when the owner uses
// File > Install Edited Stock Map / Restore Stock Map; nothing else writes to the game folder.
// Pure core code.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct StockInstallReport {
    std::vector<std::string> installed;  // file names copied into the zone folder
    std::vector<std::string> backedUp;   // stock files of the same name saved to the backup folder first
    std::vector<std::string> restored;   // stock files put back
    std::vector<std::string> removed;    // our files taken out
};

// The *.fp files of a patch folder, sorted (what an install would copy).
std::vector<std::string> stockPatchFiles(const std::string& patchDir);

// Copies every *.fp of patchDir into zoneDir. A file of the same name already there that is not from an earlier
// install is first copied to backupDir as "<name>.stock" (an existing backup is never overwritten). "<name>.ours" in
// backupDir marks the installed file as ours. Stops at the first error (false, *err); what was done is in `report`.
bool installStockPatches(const std::string& patchDir, const std::string& zoneDir, const std::string& backupDir,
                         StockInstallReport& report, std::string* err = nullptr);

// Undoes every install recorded in backupDir: our file is deleted from zoneDir, and the stock file of that name is
// copied back when a backup exists. The markers are removed; the stock backups are kept.
bool restoreStockPatches(const std::string& backupDir, const std::string& zoneDir, StockInstallReport& report,
                         std::string* err = nullptr);

// Names that an install has put into the zone folder and that are still marked (empty: nothing is installed).
std::vector<std::string> installedStockPatches(const std::string& backupDir);

// The game ships patch files with real map data (mp_br_quarry.fp, mp_donetsk.fp, ...; a map zone without its patch is
// not even readable as the map). An installed edit replaces the patch of its zone, so whoever reads the "stock" map
// from the game folder afterwards would read the edit. This lists, for every edit installed by this editor, the patch
// file name and the stock patch saved at install ("" when the stock game has no patch of that name): the converter
// reads the zone with that file (editzone "stock_patches").
struct StockPatchOverride {
    std::string name;       // <zone>.fp, as in the game's zone folder
    std::string stockFile;  // the saved stock patch, or ""
    bool operator==(const StockPatchOverride& o) const { return name == o.name && stockFile == o.stockFile; }
};
// `backupRoot` holds one backup folder per zone (<backupRoot>/<zone>/). Sorted by name.
std::vector<StockPatchOverride> installedStockPatchOverrides(const std::string& backupRoot);

}  // namespace iw8r
