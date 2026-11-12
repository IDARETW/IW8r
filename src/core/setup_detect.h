// IW8 Radiant Suite - first-run setup helpers (roadmap 9.1): find the Replay game folder and its exes, the Oodle DLL,
// and classify a game exe (stock, recompiled with the RPLYCMP2 marker, not a PE file, missing).
//
// Pure file-system helpers, no Qt: the first-run wizard (src/qtapp/setup_wizard.*) calls them and tests drive them
// with fake folders. Paths are returned with forward slashes, like the Tool Paths defaults.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace iw8r {

enum class ExeKind {
    Missing,     // no file (or unreadable)
    NotPe,       // present, but does not start with "MZ"
    Stock,       // PE without the recompiled marker (the shipped game_dx12_ship_replay.exe)
    Recompiled,  // PE with "RPLYCMP2" at file offset 0x40 (the owner's recompiled builds)
};

inline constexpr const char* kStockReplayMd5 = "1c238fe327f2ecc3b0db924c5b425439";  // game_dx12_ship_replay.exe 1.20
inline constexpr const char* kReplayExeStem = "game_dx12_ship_replay";                // game_dx12_ship_replay*.exe
inline constexpr const char* kRecompiledMarker = "RPLYCMP2";
inline constexpr std::size_t kRecompiledMarkerOffset = 0x40;

const char* exeKindName(ExeKind kind);  // "missing", "not a PE file", "stock", "recompiled"

// Classifies a game exe by its first bytes (MZ and the RPLYCMP2 marker); the file is not hashed.
ExeKind classifyExe(const std::string& path);

// Lower-case hex MD5 of a file (streamed), empty when it cannot be read. md5Hex hashes a byte string.
std::string md5File(const std::string& path);
std::string md5Hex(const std::string& bytes);

bool isExistingFile(const std::string& path);
bool isExistingDir(const std::string& path);

// "C:/" .. "Z:/" that exist (Windows). Empty elsewhere.
std::vector<std::string> windowsDriveRoots();

// Game folders under the given roots: <root>/IW8/Builds/<name>/ and <root>/<any>/IW8/Builds/<name>/, each holding at
// least one game_dx12_ship_replay*.exe. Sorted, no duplicates. Pass a temp folder as a root in tests.
std::vector<std::string> candidateGameDirs(const std::vector<std::string>& driveRoots);

// Every game_dx12_ship_replay*.exe directly in `dir` (case-insensitive extension), sorted by name.
std::vector<std::string> gameExesIn(const std::string& dir);

// Every oo2core_*_win64.dll directly in `dir`, sorted by name (the Replay build ships oo2core_7_win64.dll).
std::vector<std::string> oodleIn(const std::string& dir);

}  // namespace iw8r
