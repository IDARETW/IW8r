import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/prefab_bundle.h', [
 ('''bool writePrefabBundleFolder(const std::string& folder, const PrefabSaveResult& result, std::string* err, bool overwrite = false);
''', '''bool writePrefabBundleFolder(const std::string& folder, const PrefabSaveResult& result, std::string* err, bool overwrite = false);

// ------------------------------------------------------------------ sharing and converting

// A plain .map prefab as a bundle: the file is read into a document, everything is selected and saved as with Save
// Selection As Prefab, with the pivot at the file's own origin (placements of the old file and the bundle line up).
// opt.name / opt.id default to the file name. ok false (and error) when the file cannot be read or holds nothing.
PrefabSaveResult prefabBundleFromPlainMap(const std::string& mapPath, PrefabSaveOptions opt);

// Zip of one bundle folder (stored entries): every file under it as "<folder name>/<path>". Fails when the folder is
// not a bundle (no prefab.json and no prefab.map).
bool exportPrefabBundleZip(const std::string& folder, const std::string& zipPath, std::string* err = nullptr);

// Extracts the bundles of a zip into destRoot. The zip holds one or more "<name>.iw8prefab/" folders at its top, or the
// files of one bundle at its top (the folder is then named after the zip file). A bundle folder that already exists in
// destRoot is not replaced (the import fails and writes nothing). Each bundle is loaded once extracted; one that does
// not load is removed again and fails the import. `bundles` receives the folders written.
bool importPrefabBundleZip(const std::string& zipPath, const std::string& destRoot, std::vector<std::string>* bundles = nullptr,
                           std::string* err = nullptr);

// Placement hints of a manifest (placement.surface, placement.yaw_step, placement.snap) for a placement at `hit` on a
// surface with outward `normal` (when known):
//   wall    the prefab's +X points out of the wall: yaw from the wall normal, rounded to yaw_step (0: exact);
//   ceiling / floor / free   the origin is the hit point, yaw 0;
//   snap    x and y (and z on a wall) on that grid.
// `note` says when the surface does not fit the hint (a wall prefab on a floor).
struct PrefabPlacement {
    Vec3 origin;
    double yaw = 0;
    std::string note;
};
PrefabPlacement prefabPlacementFor(const PrefabManifest& m, const Vec3& hit, const std::optional<Vec3>& normal);
'''),
])

patch('src/core/prefab_bundle.cpp', [
 ('''#include "asset_usage.h"
''', '''#include "asset_usage.h"
#include "map_package.h"
'''),
])
s = io.open('src/core/prefab_bundle.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
end = s.rstrip().rfind('}')
add = r'''
PrefabSaveResult prefabBundleFromPlainMap(const std::string& mapPath, PrefabSaveOptions opt) {
    PrefabSaveResult out;
    Document doc;
    std::string err;
    if (!doc.load(mapPath, &err)) {
        out.error = "cannot read '" + mapPath + "': " + err;
        return out;
    }
    for (const Entity& e : doc.map().entities) {
        if (!e.isWorldspawn()) doc.select({e.id, kNoId}, true);
        for (const EntityItem& it : e.items)
            if (it.prim()) doc.select({e.id, it.id}, true);
    }
    if (doc.selection().empty()) {
        out.error = "'" + mapPath + "' holds no brush, patch or entity";
        return out;
    }
    const std::string stem = fs::path(mapPath).stem().string();
    if (opt.name.empty()) opt.name = stem;
    if (opt.id.empty()) opt.id = prefabIdFromName(stem);
    opt.pivot = Vec3(0, 0, 0);  // the plain prefab's origin: placements of the old file and the bundle line up
    return prefabBundleFromSelection(doc, opt);
}

bool exportPrefabBundleZip(const std::string& folder, const std::string& zipPath, std::string* err) {
    const fs::path dir(folder);
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || (!isFile(dir / kPrefabManifestFile) && !isFile(dir / "prefab.map"))) {
        if (err) *err = "'" + folder + "' is not a prefab bundle folder";
        return false;
    }
    const std::string top = dir.filename().string();
    std::vector<PackageFile> files;
    for (fs::recursive_directory_iterator it(dir, ec), endIt; !ec && it != endIt; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string rel = fs::relative(it->path(), dir, ec).generic_string();
        files.push_back({it->path().string(), top + "/" + rel});
    }
    std::sort(files.begin(), files.end(), [](const PackageFile& a, const PackageFile& b) { return a.archive < b.archive; });
    return writeZip(zipPath, files, err);
}

bool importPrefabBundleZip(const std::string& zipPath, const std::string& destRoot, std::vector<std::string>* bundles, std::string* err) {
    const auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    if (bundles) bundles->clear();
    const fs::path root(destRoot);
    std::error_code ec;
    fs::create_directories(root, ec);
    // Extract into a scratch folder inside destRoot (same drive: the bundles are moved, not copied).
    fs::path scratch;
    for (int n = 0; n < 1000; ++n) {
        scratch = root / (".import-" + std::to_string(n));
        if (!fs::exists(scratch, ec)) break;
    }
    const auto cleanup = [&] {
        std::error_code rec;
        fs::remove_all(scratch, rec);
    };
    std::string zerr;
    if (!extractZip(zipPath, scratch.string(), nullptr, &zerr)) {
        cleanup();
        return fail("cannot read '" + zipPath + "': " + zerr);
    }
    // The bundles: "<name>.iw8prefab" folders at the top, or one bundle's files at the top.
    std::vector<std::pair<fs::path, std::string>> found;  // source folder, bundle folder name
    for (fs::directory_iterator it(scratch, ec), endIt; !ec && it != endIt; it.increment(ec))
        if (it->is_directory(ec) && hasBundleSuffix(it->path())) found.push_back({it->path(), it->path().filename().string()});
    if (found.empty() && (isFile(scratch / kPrefabManifestFile) || isFile(scratch / "prefab.map"))) {
        std::string name = fs::path(zipPath).stem().string();
        if (!hasBundleSuffix(fs::path(name))) name += kPrefabBundleSuffix;
        found.push_back({scratch, name});
    }
    if (found.empty()) {
        cleanup();
        return fail("'" + zipPath + "' holds no prefab bundle (a folder ending in " + std::string(kPrefabBundleSuffix) +
                    ", or prefab.json / prefab.map at its top)");
    }
    for (const auto& [src, name] : found)
        if (fs::exists(root / name, ec)) {
            cleanup();
            return fail("a prefab '" + name + "' is already in '" + destRoot + "'; it is not replaced (remove or rename it first)");
        }
    for (const auto& [src, name] : found) {
        PrefabBundle check;
        std::string lerr;
        if (!loadPrefabBundle(src.string(), check, &lerr)) {
            cleanup();
            return fail("'" + name + "' in the zip is not a working prefab bundle: " + lerr);
        }
    }
    for (const auto& [src, name] : found) {
        const fs::path dest = root / name;
        std::error_code mec;
        if (src == scratch) {
            fs::rename(scratch, dest, mec);
        } else {
            fs::rename(src, dest, mec);
        }
        if (mec) {
            cleanup();
            return fail("cannot move '" + name + "' into '" + destRoot + "': " + mec.message());
        }
        if (bundles) bundles->push_back(dest.string());
    }
    cleanup();
    return true;
}

PrefabPlacement prefabPlacementFor(const PrefabManifest& m, const Vec3& hit, const std::optional<Vec3>& normal) {
    PrefabPlacement p;
    p.origin = hit;
    const std::string surface = m.surface.empty() ? std::string("floor") : m.surface;
    const bool wallHit = normal && std::fabs(normal->z) < 0.7;
    const bool ceilingHit = normal && normal->z <= -0.7;
    if (surface == "wall") {
        if (wallHit) {
            double yaw = std::atan2(normal->y, normal->x) * 180.0 / 3.14159265358979323846;
            if (m.yawStep > 0) yaw = std::round(yaw / m.yawStep) * m.yawStep;
            yaw = std::fmod(yaw + 360.0, 360.0);
            if (std::fabs(yaw) < 1e-9 || std::fabs(yaw - 360.0) < 1e-9) yaw = 0;
            p.yaw = yaw;
        } else {
            p.note = "this prefab is made for walls; it was placed on a surface that is not a wall";
        }
    } else if (surface == "ceiling" && normal && !ceilingHit) {
        p.note = "this prefab is made for ceilings; it was placed on a surface that is not a ceiling";
    } else if (surface == "floor" && normal && normal->z < 0.7) {
        p.note = "this prefab is made for floors; it was placed on a surface that is not a floor";
    }
    if (m.snap > 0) {
        const auto snap = [&](double v) { return std::round(v / m.snap) * m.snap; };
        p.origin.x = snap(p.origin.x);
        p.origin.y = snap(p.origin.y);
        if (surface == "wall" && wallHit) {
            // On a wall the height snaps too; the depth stays on the wall surface.
            p.origin.z = snap(p.origin.z);
            if (std::fabs(normal->x) > std::fabs(normal->y)) p.origin.x = hit.x;
            else p.origin.y = hit.y;
        }
    }
    return p;
}

'''.replace('\n', nl)
s = s[:end] + add.lstrip(nl) + s[end:]
io.open('src/core/prefab_bundle.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
