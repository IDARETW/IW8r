// First-run setup helpers (roadmap 9.1): MD5, exe classification, game folder and Oodle discovery on fake folders.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "setup_detect.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "setup_detect" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

// "MZ", zero padding up to `size` bytes, and `marker` written at file offset `markerAt` (none when empty).
void writeFakePe(const fs::path& p, std::size_t size, const std::string& marker = "", std::size_t markerAt = 0x40) {
    fs::create_directories(p.parent_path());
    std::string bytes(size, '\0');
    bytes[0] = 'M';
    bytes[1] = 'Z';
    if (!marker.empty()) bytes.replace(markerAt, marker.size(), marker);
    std::ofstream(p, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
}

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string name(const std::string& path) { return fs::path(path).filename().string(); }

}  // namespace

TEST(setup_md5_rfc1321_vectors) {
    CHECK(md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5Hex("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    // Crosses the 56-byte padding boundary and several 64-byte blocks.
    CHECK(md5Hex(std::string(1000, 'a')) == "cabe45dcc9ae5b66ba86600cca6b8ba8");
}

TEST(setup_md5_file_matches_string_hash) {
    const fs::path dir = freshDir("md5");
    std::string data;
    for (int i = 0; i < 3 * (1 << 20) + 17; ++i) data.push_back(char(i * 31));  // larger than the streaming chunk
    writeText(dir / "blob.bin", data);
    CHECK(md5File((dir / "blob.bin").string()) == md5Hex(data));
    CHECK(md5File((dir / "missing.bin").string()).empty());
}

TEST(setup_classify_exe_kinds) {
    const fs::path dir = freshDir("classify");
    CHECK(classifyExe((dir / "nope.exe").string()) == ExeKind::Missing);
    writeText(dir / "text.exe", "hello, not a program");
    CHECK(classifyExe((dir / "text.exe").string()) == ExeKind::NotPe);
    writeText(dir / "empty.exe", "");
    CHECK(classifyExe((dir / "empty.exe").string()) == ExeKind::NotPe);
    writeFakePe(dir / "stock.exe", 0x200);
    CHECK(classifyExe((dir / "stock.exe").string()) == ExeKind::Stock);
    writeFakePe(dir / "local_dw.exe", 0x200, kRecompiledMarker);
    CHECK(classifyExe((dir / "local_dw.exe").string()) == ExeKind::Recompiled);
    // The marker only counts at file offset 0x40; the same bytes elsewhere are a stock exe.
    writeFakePe(dir / "marker_elsewhere.exe", 0x200, kRecompiledMarker, 0x80);
    CHECK(classifyExe((dir / "marker_elsewhere.exe").string()) == ExeKind::Stock);
    // A marker without MZ is not a PE file.
    writeText(dir / "marker_only.exe", std::string(0x40, 'x') + kRecompiledMarker);
    CHECK(classifyExe((dir / "marker_only.exe").string()) == ExeKind::NotPe);
    // A file cut short right after the header: no marker, so stock.
    writeFakePe(dir / "short.exe", 0x42);
    CHECK(classifyExe((dir / "short.exe").string()) == ExeKind::Stock);
    CHECK(std::string(exeKindName(ExeKind::Recompiled)) == "recompiled");
}

TEST(setup_game_exes_in_folder) {
    const fs::path dir = freshDir("exes") / "1.20-replay";
    writeFakePe(dir / "game_dx12_ship_replay.exe", 0x100);
    writeFakePe(dir / "game_dx12_ship_replay_local_dw.exe", 0x100, kRecompiledMarker);
    writeText(dir / "game_dx12_ship_replay_notes.txt", "not an exe");
    writeText(dir / "other_game.exe", "x");
    fs::create_directories(dir / "game_dx12_ship_replay_folder.exe");  // a folder, not a file
    const auto exes = gameExesIn(dir.string());
    CHECK(exes.size() == 2);
    if (exes.size() == 2) {
        CHECK(name(exes[0]) == "game_dx12_ship_replay.exe");
        CHECK(name(exes[1]) == "game_dx12_ship_replay_local_dw.exe");
    }
    CHECK(gameExesIn((dir / "nothing_here").string()).empty());
    CHECK(gameExesIn("").empty());
}

TEST(setup_oodle_in_folder) {
    const fs::path dir = freshDir("oodle");
    writeText(dir / "oo2core_7_win64.dll", "dll");
    writeText(dir / "OO2CORE_6_WIN64.DLL", "dll");  // case-insensitive
    writeText(dir / "oo2core_7_win32.dll", "wrong arch");
    writeText(dir / "game_dx12_ship_replay.exe", "x");
    const auto dlls = oodleIn(dir.string());
    CHECK(dlls.size() == 2);
    CHECK(oodleIn((dir / "absent").string()).empty());
}

TEST(setup_candidate_game_dirs_on_fake_drives) {
    const fs::path root = freshDir("drives");
    const fs::path drive = root / "D";     // IW8/Builds directly under a drive root
    const fs::path other = root / "E";     // a folder one level down: <root>/Games/IW8/Builds
    const fs::path empty = root / "F";
    writeFakePe(drive / "IW8" / "Builds" / "1.20-replay" / "game_dx12_ship_replay.exe", 0x100);
    writeFakePe(other / "Games" / "IW8" / "Builds" / "local" / "game_dx12_ship_replay_local_dw.exe", 0x100, kRecompiledMarker);
    fs::create_directories(empty / "IW8" / "Builds" / "no-exe-here");  // builds folder without a game exe: skipped
    writeText(drive / "IW8" / "Builds" / "1.20-replay" / "oo2core_7_win64.dll", "dll");
    writeText(drive / "IW8" / "Builds" / "loose.exe", "a file in Builds, not a folder");

    const auto dirs = candidateGameDirs({drive.string(), other.string(), empty.string(), (root / "missing").string()});
    CHECK(dirs.size() == 2);
    if (dirs.size() == 2) {
        CHECK(dirs[0] == (drive / "IW8" / "Builds" / "1.20-replay").generic_string());
        CHECK(dirs[1] == (other / "Games" / "IW8" / "Builds" / "local").generic_string());
    }
    // Each drive root is searched; a root listed twice is reported once.
    CHECK(candidateGameDirs({drive.string(), drive.string()}).size() == 1);
    CHECK(candidateGameDirs({}).empty());
    CHECK(candidateGameDirs({(root / "missing").string()}).empty());
}

TEST(setup_existing_file_and_dir_checks) {
    const fs::path dir = freshDir("exists");
    writeText(dir / "acts.exe", "x");
    CHECK(isExistingFile((dir / "acts.exe").string()));
    CHECK(!isExistingFile((dir / "zonetool.exe").string()));
    CHECK(!isExistingFile(""));
    CHECK(!isExistingFile(dir.string()));  // a folder is not a file
    CHECK(isExistingDir(dir.string()));
    CHECK(!isExistingDir((dir / "acts.exe").string()));
    CHECK(!isExistingDir(""));
}
