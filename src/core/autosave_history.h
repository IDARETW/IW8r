// Autosave history (timestamped copies per map). Pure logic on file names plus std::filesystem; no Qt.
//
// File names: "<base>.<stamp>.autosave.<ext>" where stamp is "yyyyMMdd-HHmmss" (15 chars, '-' at index 8) and ext is
// "map" or "ents". The legacy single-file form "<base>.autosave.<ext>" (no stamp) is still recognised, so existing
// autosaves are listed but never pruned.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct AutosaveEntry {
    std::string path;      // full path (set by listAutosaves; left empty by parseAutosaveFileName)
    std::string base;      // map base name, e.g. "mp_test" (may contain dots, e.g. "my.map.v2")
    std::string stamp;     // "20261110-213005" (yyyyMMdd-HHmmss); empty for legacy un-stamped files
    std::string ext;       // "map" or "ents"
    uintmax_t bytes = 0;   // file size in bytes (set by listAutosaves)
};

// Builds "<base>.<stamp>.autosave.<ext>". An empty stamp gives the legacy "<base>.autosave.<ext>" form.
// Returns an empty string when base is empty or contains a path separator, ext is not "map"/"ents", or stamp is
// non-empty and not a 15-char yyyyMMdd-HHmmss stamp (digits with '-' at index 8).
std::string autosaveFileName(const std::string& base, const std::string& stamp, const std::string& ext);

// Parses a bare file name (no directory) of the form above. Accepts the legacy "<base>.autosave.<ext>" with stamp "".
// Fills base, stamp and ext only; path and bytes are not touched. Returns false for anything else (wrong tag or
// extension, empty base, a path separator in the name).
// Note: a dotted base that ends in something stamp-shaped but invalid (e.g. "x.2026111-213005") is read as a legacy
// file whose base includes that text, because the spec allows dots in bases.
bool parseAutosaveFileName(const std::string& fileName, AutosaveEntry& out);

// Every autosave file directly inside dir (not recursive; directories and non-matching names are skipped), with sizes.
// Order: stamped entries newest first (by stamp), then legacy entries; ties broken by base, then ext.
// baseFilter empty means all maps, otherwise only entries whose base equals baseFilter. A missing dir gives an empty list.
std::vector<AutosaveEntry> listAutosaves(const std::string& dir, const std::string& baseFilter = "");

// Paths to delete so that each base keeps at most keepPerMap stamped copies (the newest by stamp). Legacy entries
// (empty stamp) are never returned. Entries are grouped by base, so two maps are pruned independently.
// keepPerMap == 0 returns every stamped copy. The input order does not matter.
std::vector<std::string> autosavesToPrune(const std::vector<AutosaveEntry>& entries, size_t keepPerMap);

// Low-disk guard: true when the volume holding path has at least minFreeBytes available (std::filesystem::space).
// If freeBytes is non-null it receives the available byte count, or 0 when space() fails. When space() fails the
// function returns true, so an unknown volume never blocks saving.
bool hasFreeSpace(const std::string& path, uintmax_t minFreeBytes, uintmax_t* freeBytes = nullptr);

}  // namespace iw8r
