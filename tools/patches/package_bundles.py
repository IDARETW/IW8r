import io

def load(p):
    return io.open(p, encoding='utf-8', newline='').read()
def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
def rep(s, a, b, count=1):
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    return s.replace(a, b)

p = 'src/core/map_package.cpp'
s = load(p)
s = rep(s, '''    // Scans the misc_prefab references of one map or prefab file. `depth` is 0 for the map itself.
''', '''    // A reference into a prefab bundle (a folder named <name>.iw8prefab, or a file inside one): the whole folder is
    // packaged, because a bundle carries its script, materials, models, sounds and effects beside its contents file.
    // `folder` gets the folder on disk and `archive` its package path. False when the reference names no bundle folder.
    bool resolveBundle(const std::vector<std::string>& parts, fs::path& folder, std::string& archive) {
        size_t upTo = 0;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string lower = parts[i];
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            const std::string suffix = ".iw8prefab";
            if (lower.size() > suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
                upTo = i + 1;
                break;
            }
        }
        if (!upTo) return false;
        const std::string rel = joinParts(std::vector<std::string>(parts.begin(), parts.begin() + static_cast<std::ptrdiff_t>(upTo)));
        std::error_code ec;
        if (fs::path local = mapDir / fromUtf8(rel); fs::is_directory(local, ec)) {
            folder = local;
            archive = rel;
            return true;
        }
        for (const fs::path& root : roots)
            if (fs::path p = root / fromUtf8(rel); fs::is_directory(p, ec)) {
                folder = p;
                archive = "prefabs/" + rel;
                return true;
            }
        return false;
    }

    // Every file of a bundle folder, then the references of its .map files. Returns the folder's files it added.
    void addBundle(const fs::path& folder, const std::string& archive, int depth) {
        if (!visited.insert(canonicalKey(folder)).second) return;
        std::error_code ec;
        std::vector<fs::path> maps;
        std::vector<std::pair<std::string, fs::path>> found;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path relative = it->path().lexically_relative(folder);
            found.push_back({archive + "/" + toUtf8(relative.generic_string()), it->path()});
        }
        std::sort(found.begin(), found.end());
        for (const auto& [name, path] : found) {
            addFile(toUtf8(path), name);
            std::string ext = toUtf8(path.extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext == ".map") maps.push_back(path);
        }
        if (depth + 1 > kMaxPrefabDepth) {
            addMissing(archive + " (nested deeper than " + std::to_string(kMaxPrefabDepth) + " levels; not scanned)");
            return;
        }
        for (const fs::path& m : maps)
            if (visited.insert(canonicalKey(m)).second) scan(m, depth + 1);
    }

    // Scans the misc_prefab references of one map or prefab file. `depth` is 0 for the map itself.
''')
s = rep(s, '''            fs::path src;
            std::string archive;
            if (!resolve(parts, src, archive)) {
''', '''            fs::path src;
            std::string archive;
            if (resolveBundle(parts, src, archive)) {
                addBundle(src, archive, depth);
                continue;
            }
            if (!resolve(parts, src, archive)) {
''')
for inc in ('<algorithm>', '<cctype>'):
    if '#include ' + inc not in s:
        first = s.index('#include <')
        s = s[:first] + '#include ' + inc + ('\r\n' if '\r\n' in s else '\n') + s[first:]
save(p, s)

p = 'src/core/map_package.h'
s = load(p)
s = rep(s, '''//   "prefabs/<path as written>"           a prefab found under a prefab root
''', '''//   "prefabs/<path as written>"           a prefab found under a prefab root
//   "<...>/<name>.iw8prefab/<every file>" a prefab bundle (folder) a misc_prefab names, whole: its contents file,
//                                         script, thumbnail, materials, models, sounds and effects
''')
save(p, s)

p = 'tests/map_package_tests.cpp'
s = load(p)
a = 'TEST(map_package_zip_round_trip_is_byte_exact) {'
assert s.count(a) == 1
nl = '\r\n' if '\r\n' in s else '\n'
t = r'''// A placed prefab bundle (misc_prefab whose model names a .iw8prefab folder or its contents file) is packaged whole,
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

'''.replace('\n', nl)
s = s.replace(a, t + a)
save(p, s)
print('ok')
