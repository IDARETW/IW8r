#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "map_package.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Unique scratch folder, removed when the test leaves scope.
struct TempTree {
    fs::path root;
    TempTree() {
        std::random_device rd;
        root = fs::temp_directory_path() / ("iw8r_mappkg_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        fs::create_directories(root);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void writeBytes(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string readAll(const fs::path& p) {
    std::string s;
    readFile(p.string(), s);
    return s;
}

const PackageFile* findArchive(const PackageResult& r, const std::string& archive) {
    for (const PackageFile& f : r.files)
        if (f.archive == archive) return &f;
    return nullptr;
}

uint32_t u32at(const std::string& b, size_t at) {
    return static_cast<uint32_t>(static_cast<uint8_t>(b[at])) | (static_cast<uint32_t>(static_cast<uint8_t>(b[at + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[at + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[at + 3])) << 24);
}

}  // namespace

TEST(map_package_collects_map_sidecars_and_prefabs) {
    TempTree t;
    const fs::path mapDir = t.root / "maps";
    const fs::path prefabRoot = t.root / "data" / "prefabs";

    // One prefab relative to the map folder, one under the fake prefab root, a missing one,
    // and a nested prefab (door -> hinge) whose hinge points back at door (cycle).
    const std::string mapText =
        "iwmap 4\n"
        "// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n"
        "// entity 1\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/crate.map\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 2\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"iw8r/doors/door.map\"\n\"origin\" \"128 0 0\"\n}\n"
        "// entity 3\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/gone.map\"\n\"origin\" \"256 0 0\"\n}\n";
    writeBytes(mapDir / "mp_test.map", mapText);

    std::string blob;
    for (int i = 0; i < 1000; ++i) blob.push_back(static_cast<char>((i * 7) % 256));
    writeBytes(mapDir / "mp_test.loadscreen.png", blob);

    writeBytes(mapDir / "prefabs" / "crate.map", "iwmap 4\n// entity 0\n{\n\"classname\" \"light\"\n}\n");
    writeBytes(prefabRoot / "iw8r" / "doors" / "door.map",
               "iwmap 4\n// entity 0\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"iw8r/doors/hinge.map\"\n}\n");
    writeBytes(prefabRoot / "iw8r" / "doors" / "hinge.map",
               "iwmap 4\n// entity 0\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"iw8r/doors/door.map\"\n}\n");

    const fs::path matFile = t.root / "extra" / "mat.txt";
    writeBytes(matFile, "custom material");
    const std::vector<PackageFile> extras = {{matFile.string(), "materials/mat.txt"}};

    const std::string mapPath = (mapDir / "mp_test.map").string();
    const std::vector<std::string> roots = {prefabRoot.string()};
    PackageResult r = collectMapPackage(mapPath, roots, extras);

    CHECK(r.files.size() == 6);
    std::set<std::string> archives;
    for (const PackageFile& f : r.files) archives.insert(f.archive);
    CHECK(archives.size() == r.files.size());  // no duplicates

    CHECK(findArchive(r, "mp_test.map") != nullptr);
    CHECK(findArchive(r, "mp_test.loadscreen.png") != nullptr);
    CHECK(findArchive(r, "prefabs/crate.map") != nullptr);
    CHECK(findArchive(r, "prefabs/iw8r/doors/door.map") != nullptr);
    CHECK(findArchive(r, "prefabs/iw8r/doors/hinge.map") != nullptr);
    CHECK(findArchive(r, "materials/mat.txt") != nullptr);

    if (const PackageFile* f = findArchive(r, "prefabs/crate.map")) CHECK(fs::exists(fs::path(f->source)));
    if (const PackageFile* f = findArchive(r, "prefabs/iw8r/doors/hinge.map")) {
        CHECK(readAll(fs::path(f->source)) == readAll(prefabRoot / "iw8r" / "doors" / "hinge.map"));
    }

    CHECK(r.missing.size() == 1);
    if (!r.missing.empty()) CHECK(r.missing[0] == "prefabs/gone.map");

    // A map that does not exist yields nothing but a missing entry.
    PackageResult none = collectMapPackage((t.root / "nope.map").string(), roots);
    CHECK(none.files.empty());
    CHECK(none.missing.size() == 1);
}

// A placed prefab bundle (misc_prefab whose model names a .iw8prefab folder or its contents file) is packaged whole,
// and the prefabs its contents file names are followed.
TEST(map_package_collects_prefab_bundles_whole) {
    TempTree t;
    const fs::path mapDir = t.root / "maps";
    const fs::path prefabRoot = t.root / "data" / "prefabs";
    const std::string mapText =
        "iwmap 4\n"
        "// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n"
        "// entity 1\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"bundles/doors/door.iw8prefab\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 2\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"bundles/doors/door.iw8prefab/prefab.map\"\n\"origin\" \"64 0 0\"\n}\n"
        "// entity 3\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"mine/pad.iw8prefab\"\n\"origin\" \"128 0 0\"\n}\n"
        "// entity 4\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"bundles/gone.iw8prefab\"\n\"origin\" \"256 0 0\"\n}\n";
    writeBytes(mapDir / "mp_b.map", mapText);
    const fs::path door = prefabRoot / "bundles" / "doors" / "door.iw8prefab";
    writeBytes(door / "prefab.json", "{}");
    writeBytes(door / "prefab.map", "iwmap 4\n// entity 0\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"iw8r/hinge.map\"\n}\n");
    writeBytes(door / "script.gsc", "main() {}");
    writeBytes(door / "materials" / "wood" / "wood.png", "png");
    writeBytes(door / "sounds" / "creak.wav", "wav");
    writeBytes(prefabRoot / "iw8r" / "hinge.map", "iwmap 4\n// entity 0\n{\n\"classname\" \"light\"\n}\n");
    const fs::path pad = mapDir / "mine" / "pad.iw8prefab";  // a bundle beside the map
    writeBytes(pad / "prefab.json", "{}");
    writeBytes(pad / "prefab.map", "iwmap 4\n// entity 0\n{\n\"classname\" \"light\"\n}\n");

    PackageResult r = collectMapPackage((mapDir / "mp_b.map").string(), {prefabRoot.string()});
    std::set<std::string> archives;
    for (const PackageFile& f : r.files) archives.insert(f.archive);
    CHECK(archives.size() == r.files.size());
    CHECK(r.files.size() == 9);
    for (const char* name : {"mp_b.map", "prefabs/bundles/doors/door.iw8prefab/prefab.json", "prefabs/bundles/doors/door.iw8prefab/prefab.map",
                             "prefabs/bundles/doors/door.iw8prefab/script.gsc", "prefabs/bundles/doors/door.iw8prefab/materials/wood/wood.png",
                             "prefabs/bundles/doors/door.iw8prefab/sounds/creak.wav", "prefabs/iw8r/hinge.map",
                             "mine/pad.iw8prefab/prefab.json", "mine/pad.iw8prefab/prefab.map"})
        CHECK(findArchive(r, name) != nullptr);
    CHECK(r.missing.size() == 1);
    if (!r.missing.empty()) CHECK(r.missing[0] == "bundles/gone.iw8prefab");
}

TEST(map_package_zip_round_trip_is_byte_exact) {
    TempTree t;
    std::string blob;
    for (int i = 0; i < 1000; ++i) blob.push_back(static_cast<char>((i * 7) % 256));  // covers every byte value
    const fs::path a = t.root / "src" / "a.bin";
    const fs::path empty = t.root / "src" / "empty.txt";
    const fs::path b = t.root / "src" / "sub" / "b.map";
    writeBytes(a, blob);
    writeBytes(empty, "");
    writeBytes(b, "iwmap 4\r\n// entity 0\r\n{\r\n\"classname\" \"worldspawn\"\r\n}\r\n");

    const std::vector<PackageFile> files = {
        {a.string(), "mp/a.bin"}, {empty.string(), "empty.txt"}, {b.string(), "prefabs/sub/b.map"}};
    const fs::path zip = t.root / "pkg.zip";
    const fs::path dst = t.root / "out";

    std::string err;
    const bool wrote = writeZip(zip.string(), files, &err);
    CHECK(wrote);
    CHECK(err.empty());

    std::vector<std::string> extracted;
    const bool read = extractZip(zip.string(), dst.string(), &extracted, &err);
    CHECK(read);
    CHECK(extracted.size() == 3);
    for (const PackageFile& f : files) CHECK(readAll(dst / f.archive) == readAll(fs::path(f.source)));
    CHECK(readAll(dst / "mp" / "a.bin") == blob);
}

TEST(map_package_extract_refuses_escaping_entry) {
    TempTree t;
    const fs::path src = t.root / "src.txt";
    writeBytes(src, "x");
    const std::vector<PackageFile> files = {{src.string(), "../evil.txt"}};
    const fs::path zip = t.root / "evil.zip";
    const fs::path dst = t.root / "dst";

    std::string err;
    CHECK(writeZip(zip.string(), files, &err));

    std::vector<std::string> extracted;
    CHECK(!extractZip(zip.string(), dst.string(), &extracted, &err));
    CHECK(err.find("..") != std::string::npos);
    CHECK(extracted.empty());
    CHECK(!fs::exists(t.root / "evil.txt"));
    CHECK(!fs::exists(dst));  // rejected before anything is written
}

TEST(map_package_extract_rejects_compressed_entry) {
    TempTree t;
    const fs::path src = t.root / "stored.txt";
    writeBytes(src, "hello hello hello");
    const fs::path zip = t.root / "pkg.zip";
    std::string err;
    CHECK(writeZip(zip.string(), {{src.string(), "stored.txt"}}, &err));

    // Flip the method of the single entry to 12 (bzip2, not supported) in both the local and central headers.
    const std::string stored = readAll(zip);
    CHECK(stored.size() > 22);
    const size_t cdOff = u32at(stored, stored.size() - 22 + 16);
    std::string bytes = stored;
    bytes[8] = 12;
    bytes[cdOff + 10] = 12;
    writeBytes(zip, bytes);

    std::vector<std::string> extracted;
    CHECK(!extractZip(zip.string(), (t.root / "dst").string(), &extracted, &err));
    CHECK(err.find("compressed with method 12") != std::string::npos);
    // Method 8 (deflate) is read, but these stored bytes are not deflate data: refused, nothing written.
    bytes = stored;
    bytes[8] = 8;
    bytes[cdOff + 10] = 8;
    writeBytes(zip, bytes);
    CHECK(!extractZip(zip.string(), (t.root / "dst8").string(), &extracted, &err));
    CHECK(err.find("cannot inflate") != std::string::npos || err.find("CRC") != std::string::npos);
    CHECK(!fs::exists(t.root / "dst8" / "stored.txt"));
}

TEST(map_package_write_fails_on_unreadable_source) {
    TempTree t;
    const fs::path zip = t.root / "bad.zip";
    std::string err;
    CHECK(!writeZip(zip.string(), {{(t.root / "missing.bin").string(), "missing.bin"}}, &err));
    CHECK(!err.empty());
    CHECK(!fs::exists(zip));
}
