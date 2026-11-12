// IW8 Radiant Suite - map source package: collects a map with every file it depends on
// (prefabs, sidecars, caller-resolved extras) and stores them in one shareable .zip, and
// extracts such a zip again. No Qt; std::filesystem and the map_text parser only.
//
// Archive layout written by collectMapPackage:
//   "<map file name>"                     the map itself, at the package root
//   "<base>.loadscreen.png" etc.          sidecars that exist beside the map
//   "<path as written>"                   a prefab found relative to the map folder (keeps its path)
//   "prefabs/<path as written>"           a prefab found under a prefab root
//   "<...>/<name>.iw8prefab/<every file>" a prefab bundle (folder) a misc_prefab names, whole: its contents file,
//                                         script, thumbnail, materials, models, sounds and effects
//
// Importing side: extract the zip into <destDir>. Map keys then resolve as Radiant does:
// relative to the map folder first (so "prefabs/crate.map" works directly), and keys that
// pointed into a prefab root ("iw8r/doors/door.map") resolve only if the importer adds
// "<destDir>/prefabs" as a prefab root. Nothing else needs rewriting.
//
// Known limit: if a map-relative file and a root-found file would get the same archive path,
// the first one collected wins (archive paths are unique in the package).
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct PackageFile {
    std::string source;   // absolute path on disk
    std::string archive;  // path inside the package, forward slashes, e.g. "mp_x.map", "prefabs/iw8r/doors/door.map"
};

struct PackageResult {
    std::vector<PackageFile> files;
    std::vector<std::string> missing;  // referenced but not found (e.g. a prefab path that resolves nowhere)
};

// Collects the map file itself (archive name = its file name), sidecar files that exist, every prefab it uses
// (recursively, at most 8 levels; cycles are broken). A prefab reference is resolved against the map folder
// first, then each prefab root in order. Nested references are resolved the same way. `extraFiles` (custom
// materials / models the caller resolves) are added as given; a source that is not a regular file goes to
// `missing`. Duplicate archive paths appear once (first one wins). A map that cannot be read is reported in
// `missing` and yields no files. A prefab that does not parse is packaged but its own references are not
// followed; it is reported in `missing` with a note.
PackageResult collectMapPackage(const std::string& mapPath, const std::vector<std::string>& prefabRoots,
                                const std::vector<PackageFile>& extraFiles = {});

// Writes a zip (stored, no compression, CRC-32 per entry, UTF-8 names flag) holding the files. Duplicate
// archive names are written once. Fails (false + *err) when a source cannot be read or the output cannot be
// written; a partially written output is removed. Limited to non-zip64 sizes (under 4 GiB, under 65535 entries).
bool writeZip(const std::string& zipPath, const std::vector<PackageFile>& files, std::string* err = nullptr);

// Extracts a zip: stored and deflated entries (zips from writeZip, Windows "Send to > Compressed folder" and most
// tools). Other compression methods and encrypted entries are rejected with a clear message, as are entries whose name is absolute, contains "..", or contains a drive/stream colon. Every
// entry is validated (structure, CRC, destination) before anything is written, so a rejected zip leaves nothing
// behind. `extracted` receives the paths of the files written (directories are created, not listed).
bool extractZip(const std::string& zipPath, const std::string& destDir, std::vector<std::string>* extracted = nullptr,
                std::string* err = nullptr);

}  // namespace iw8r
