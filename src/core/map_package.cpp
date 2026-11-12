#include "map_package.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

#include "map_text.h"

namespace fs = std::filesystem;

namespace iw8r {
namespace detail {
bool inflateRaw(const char* data, size_t compressedSize, size_t size, std::string& out);  // stb_image_impl.cpp
}  // namespace detail

namespace {

constexpr int kMaxPrefabDepth = 8;
constexpr uint16_t kUtf8NameFlag = 0x0800;

fs::path fromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

std::string toUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool endsWithCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    }
    return true;
}

// Splits a path written with either slash style into components. Empty and "." parts are dropped.
// Returns false when a part is ".." or the path contains ':' (drive letters, alternate streams).
bool splitPath(const std::string& raw, std::vector<std::string>& parts) {
    parts.clear();
    std::string cur;
    auto flush = [&] {
        if (!cur.empty() && cur != ".") parts.push_back(cur);
        cur.clear();
    };
    for (char c : raw) {
        if (c == ':') return false;
        if (c == '/' || c == '\\') {
            flush();
            continue;
        }
        cur += c;
    }
    flush();
    for (const std::string& p : parts)
        if (p == "..") return false;
    return true;
}

std::string joinParts(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& p : parts) {
        if (!out.empty()) out += '/';
        out += p;
    }
    return out;
}

std::string canonicalKey(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    if (ec) c = p.lexically_normal();
    return toUtf8(c);
}

fs::path absoluteOr(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) a = p;
    return a.lexically_normal();
}

// ---- CRC-32 / little-endian helpers ---------------------------------------------------------

uint32_t crc32Of(const char* data, size_t n) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ static_cast<uint8_t>(data[i])) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(std::string& b, uint32_t v) {
    b.push_back(static_cast<char>(v & 0xFFu));
    b.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void put32(std::string& b, uint32_t v) {
    put16(b, v & 0xFFFFu);
    put16(b, v >> 16);
}

uint16_t get16(const std::string& b, size_t at) {
    return static_cast<uint16_t>(static_cast<uint8_t>(b[at]) | (static_cast<uint8_t>(b[at + 1]) << 8));
}

uint32_t get32(const std::string& b, size_t at) {
    return static_cast<uint32_t>(get16(b, at)) | (static_cast<uint32_t>(get16(b, at + 2)) << 16);
}

void dosDateTime(uint16_t& date, uint16_t& time) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    int year = std::max(1980, tm.tm_year + 1900);
    time = static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    date = static_cast<uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
}

// ---- collection -----------------------------------------------------------------------------

struct Collector {
    fs::path mapDir;
    std::vector<fs::path> roots;
    PackageResult result;
    std::set<std::string> archives;  // archive names already in result.files
    std::set<std::string> visited;   // canonical source paths already scanned

    void addFile(const std::string& source, const std::string& archive) {
        if (!archives.insert(archive).second) return;
        result.files.push_back({source, archive});
    }

    void addMissing(const std::string& what) {
        if (std::find(result.missing.begin(), result.missing.end(), what) == result.missing.end())
            result.missing.push_back(what);
    }

    // Map folder first, then each prefab root. `archive` gets the package path for the hit.
    bool resolve(const std::vector<std::string>& parts, fs::path& src, std::string& archive) {
        const std::string rel = joinParts(parts);
        std::error_code ec;
        fs::path local = mapDir / fromUtf8(rel);
        if (fs::is_regular_file(local, ec)) {
            src = local;
            archive = rel;
            return true;
        }
        for (const fs::path& root : roots) {
            fs::path p = root / fromUtf8(rel);
            if (fs::is_regular_file(p, ec)) {
                src = p;
                archive = "prefabs/" + rel;
                return true;
            }
        }
        return false;
    }

    // A reference into a prefab bundle (a folder named <name>.iw8prefab, or a file inside one): the whole folder is
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
    void scan(const fs::path& file, int depth) {
        std::string text;
        if (!readFile(toUtf8(file), text)) {
            addMissing(toUtf8(file));
            return;
        }
        Id nextId = 1;
        ParseResult pr = parseMap(text, nextId);
        if (!pr.map) {
            addMissing(toUtf8(file) + " (could not parse; its prefabs were not scanned)");
            return;
        }
        for (const Entity& e : pr.map->entities) {
            if (e.classname() != "misc_prefab") continue;
            const std::string* model = e.value("model");
            if (!model || model->empty()) continue;

            std::vector<std::string> parts;
            if (!splitPath(*model, parts) || parts.empty()) {
                addMissing(*model);
                continue;
            }
            fs::path src;
            std::string archive;
            if (resolveBundle(parts, src, archive)) {
                addBundle(src, archive, depth);
                continue;
            }
            if (!resolve(parts, src, archive)) {
                addMissing(*model);
                continue;
            }
            const bool first = visited.insert(canonicalKey(src)).second;
            addFile(toUtf8(src), archive);
            if (!first) continue;  // already scanned (cycle or shared prefab)
            if (depth + 1 > kMaxPrefabDepth) {
                addMissing(*model + " (nested deeper than " + std::to_string(kMaxPrefabDepth) + " levels; not scanned)");
                continue;
            }
            scan(src, depth + 1);
        }
    }
};

// ---- zip writing / reading ------------------------------------------------------------------

struct CentralRecord {
    std::string name;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
};

struct ZipEntry {
    std::string name;  // cleaned, forward slashes, no trailing slash
    bool isDir = false;
    size_t dataAt = 0;
    uint32_t size = 0;
    std::string inflated;  // a deflated entry, decompressed (its bytes; dataAt is not used)
    bool deflated = false;
};

}  // namespace

PackageResult collectMapPackage(const std::string& mapPath, const std::vector<std::string>& prefabRoots,
                                const std::vector<PackageFile>& extraFiles) {
    Collector c;
    const fs::path map = absoluteOr(fromUtf8(mapPath));
    std::error_code ec;
    if (!fs::is_regular_file(map, ec)) {
        c.addMissing(mapPath);
        return c.result;
    }
    c.mapDir = map.parent_path();
    for (const std::string& r : prefabRoots) c.roots.push_back(absoluteOr(fromUtf8(r)));

    const std::string mapName = toUtf8(map.filename());
    c.visited.insert(canonicalKey(map));
    c.addFile(toUtf8(map), mapName);

    std::string base = mapName;
    if (endsWithCI(base, ".map"))
        base.resize(base.size() - 4);
    else
        base = toUtf8(map.stem());
    for (const char* suffix : {".loadscreen.png", ".campaths.json", ".ents"}) {
        fs::path side = c.mapDir / fromUtf8(base + suffix);
        if (fs::is_regular_file(side, ec)) c.addFile(toUtf8(side), base + suffix);
    }

    c.scan(map, 0);

    for (const PackageFile& x : extraFiles) {
        if (!fs::is_regular_file(fromUtf8(x.source), ec)) {
            c.addMissing(x.source);
            continue;
        }
        c.addFile(x.source, x.archive);
    }
    return c.result;
}

bool writeZip(const std::string& zipPath, const std::vector<PackageFile>& files, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    const fs::path out = fromUtf8(zipPath);
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);

    uint16_t dosDate = 0, dosTime = 0;
    dosDateTime(dosDate, dosTime);

    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return fail("cannot create " + zipPath);

    uint64_t pos = 0;
    auto emit = [&](const std::string& bytes) {
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        pos += bytes.size();
    };
    auto bail = [&](const std::string& msg) {
        f.close();
        fs::remove(out, ec);
        return fail(msg);
    };

    std::vector<CentralRecord> central;
    std::set<std::string> seen;
    for (const PackageFile& pf : files) {
        if (pf.archive.empty() || pf.archive.size() > 0xFFFF) return bail("bad archive name for " + pf.source);
        if (!seen.insert(pf.archive).second) continue;
        if (central.size() >= 0xFFFF) return bail("too many entries for zip (no zip64 support)");

        std::error_code e2;
        std::string data;
        if (!fs::is_regular_file(fromUtf8(pf.source), e2) || !readFile(pf.source, data))
            return bail("cannot read " + pf.source);
        if (data.size() > 0xFFFFFFFFull || pos > 0xFFFFFFFFull) return bail("zip exceeds 4 GiB (no zip64 support)");

        CentralRecord rec{pf.archive, crc32Of(data.data(), data.size()), static_cast<uint32_t>(data.size()),
                          static_cast<uint32_t>(pos)};
        std::string h;
        put32(h, 0x04034B50u);  // local file header
        put16(h, 20);           // version needed
        put16(h, kUtf8NameFlag);
        put16(h, 0);            // method: stored
        put16(h, dosTime);
        put16(h, dosDate);
        put32(h, rec.crc);
        put32(h, rec.size);     // compressed size
        put32(h, rec.size);     // uncompressed size
        put16(h, static_cast<uint32_t>(pf.archive.size()));
        put16(h, 0);            // extra length
        h += pf.archive;
        emit(h);
        emit(data);
        central.push_back(rec);
    }

    const uint64_t cdStart = pos;
    for (const CentralRecord& rec : central) {
        std::string h;
        put32(h, 0x02014B50u);  // central directory header
        put16(h, 20);           // version made by
        put16(h, 20);           // version needed
        put16(h, kUtf8NameFlag);
        put16(h, 0);
        put16(h, dosTime);
        put16(h, dosDate);
        put32(h, rec.crc);
        put32(h, rec.size);
        put32(h, rec.size);
        put16(h, static_cast<uint32_t>(rec.name.size()));
        put16(h, 0);            // extra length
        put16(h, 0);            // comment length
        put16(h, 0);            // disk number start
        put16(h, 0);            // internal attributes
        put32(h, 0);            // external attributes
        put32(h, rec.offset);
        h += rec.name;
        emit(h);
    }
    const uint64_t cdSize = pos - cdStart;
    if (cdStart > 0xFFFFFFFFull || cdSize > 0xFFFFFFFFull) return bail("central directory too large");

    std::string e;
    put32(e, 0x06054B50u);      // end of central directory
    put16(e, 0);
    put16(e, 0);
    put16(e, static_cast<uint32_t>(central.size()));
    put16(e, static_cast<uint32_t>(central.size()));
    put32(e, static_cast<uint32_t>(cdSize));
    put32(e, static_cast<uint32_t>(cdStart));
    put16(e, 0);                // comment length
    emit(e);

    f.close();
    if (f.fail()) {
        fs::remove(out, ec);
        return fail("write failed: " + zipPath);
    }
    return true;
}

bool extractZip(const std::string& zipPath, const std::string& destDir, std::vector<std::string>* extracted,
                std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    if (extracted) extracted->clear();

    std::string buf;
    if (!readFile(zipPath, buf)) return fail("cannot read " + zipPath);
    const size_t n = buf.size();
    if (n < 22) return fail("not a zip file (too small): " + zipPath);

    // End-of-central-directory record: near the end, within 22 + 65535 bytes (comment allowance).
    size_t eocd = std::string::npos;
    const size_t lowest = n > 22 + 0xFFFF ? n - 22 - 0xFFFF : 0;
    for (size_t i = n - 22;; --i) {
        if (get32(buf, i) == 0x06054B50u) {
            eocd = i;
            break;
        }
        if (i == lowest) break;
    }
    if (eocd == std::string::npos) return fail("not a zip file (no end record): " + zipPath);
    if (get16(buf, eocd + 4) != 0 || get16(buf, eocd + 6) != 0) return fail("multi-disk zips are not supported");

    const uint32_t count = get16(buf, eocd + 10);
    const uint64_t cdSize = get32(buf, eocd + 12);
    const uint64_t cdOff = get32(buf, eocd + 16);
    if (cdOff + cdSize > eocd) return fail("corrupt zip: central directory out of range");

    // Pass 1: parse and validate every entry. Nothing is written until all of them pass.
    std::vector<ZipEntry> entries;
    size_t pos = static_cast<size_t>(cdOff);
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 46 > n || get32(buf, pos) != 0x02014B50u) return fail("corrupt zip: bad central directory entry");
        const uint16_t flags = get16(buf, pos + 8);
        const uint16_t method = get16(buf, pos + 10);
        const uint32_t crc = get32(buf, pos + 16);
        const uint32_t csize = get32(buf, pos + 20);
        const uint32_t usize = get32(buf, pos + 24);
        const uint16_t nlen = get16(buf, pos + 28);
        const uint16_t xlen = get16(buf, pos + 30);
        const uint16_t clen = get16(buf, pos + 32);
        const uint32_t localOff = get32(buf, pos + 42);
        const size_t nameAt = pos + 46;
        if (nameAt + nlen > n) return fail("corrupt zip: entry name out of range");
        const std::string rawName = buf.substr(nameAt, nlen);
        pos = nameAt + nlen + xlen + clen;

        if (flags & 1u) return fail("encrypted entry is not supported: " + rawName);
        if (method != 0 && method != 8)
            return fail("entry '" + rawName + "' is compressed with method " + std::to_string(method) +
                        "; only stored and deflated entries are supported");
        if (method == 0 && csize != usize) return fail("corrupt zip: size mismatch for " + rawName);
        // A zip bomb guard: one entry at most 512 MB unpacked.
        if (usize > 512u * 1024u * 1024u) return fail("entry '" + rawName + "' is larger than 512 MB unpacked");

        if (!rawName.empty() && (rawName[0] == '/' || rawName[0] == '\\'))
            return fail("unsafe entry name (absolute path): " + rawName);
        const bool isDir = !rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\');
        std::vector<std::string> parts;
        if (!splitPath(rawName, parts)) return fail("unsafe entry name (contains '..' or ':'): " + rawName);
        if (parts.empty()) {
            if (isDir) continue;  // "./" style root entry
            return fail("unsafe entry name: " + rawName);
        }

        if (static_cast<uint64_t>(localOff) + 30 > n || get32(buf, localOff) != 0x04034B50u)
            return fail("corrupt zip: bad local header for " + rawName);
        const size_t dataAt = static_cast<size_t>(localOff) + 30 + get16(buf, localOff + 26) + get16(buf, localOff + 28);
        if (dataAt + csize > cdOff) return fail("corrupt zip: data out of range for " + rawName);
        ZipEntry entry{joinParts(parts), isDir, dataAt, usize};
        if (method == 8) {
            if (!detail::inflateRaw(buf.data() + dataAt, csize, usize, entry.inflated))
                return fail("corrupt zip: cannot inflate " + rawName);
            entry.deflated = true;
            if (crc32Of(entry.inflated.data(), usize) != crc) return fail("CRC mismatch for " + rawName);
        } else if (crc32Of(buf.data() + dataAt, usize) != crc) {
            return fail("CRC mismatch for " + rawName);
        }

        entries.push_back(std::move(entry));
    }

    // Destination check: every target must stay inside destDir.
    std::error_code ec;
    const fs::path dest = absoluteOr(fromUtf8(destDir));
    std::vector<fs::path> targets;
    targets.reserve(entries.size());
    for (const ZipEntry& e : entries) {
        fs::path target = (dest / fromUtf8(e.name)).lexically_normal();
        const fs::path rel = target.lexically_relative(dest);
        if (rel.empty() || *rel.begin() == fs::path("..")) return fail("entry escapes destination: " + e.name);
        targets.push_back(target);
    }

    // Pass 2: write.
    fs::create_directories(dest, ec);
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        const fs::path& target = targets[i];
        if (e.isDir) {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream f(target, std::ios::binary | std::ios::trunc);
        if (!f) return fail("cannot create " + toUtf8(target));
        f.write(e.deflated ? e.inflated.data() : buf.data() + e.dataAt, static_cast<std::streamsize>(e.size));
        f.close();
        if (f.fail()) return fail("write failed: " + toUtf8(target));
        if (extracted) extracted->push_back(toUtf8(target));
    }
    return true;
}

}  // namespace iw8r
