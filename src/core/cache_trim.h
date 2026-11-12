// Cache size cap (roadmap 4.13): which files to delete so a cache folder fits under a byte cap. Pure logic; the
// editor's Cache Manager (src/qtapp/cache_manager.*) lists the files and deletes the chosen ones at startup, before any
// cache index is loaded (the indexes drop entries whose files are gone and fetch them again on demand).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct CacheFile {
    std::string path;    // relative or absolute; only the file name is looked at for protection
    uint64_t bytes = 0;
    int64_t mtime = 0;   // seconds; older files go first
};

// True for files a trim never deletes: index.json, *.ini, and anything whose name starts with '.'.
bool cacheFileProtected(const std::string& path);

uint64_t cacheTotalBytes(const std::vector<CacheFile>& files);

// Indexes into `files` to delete, oldest first (ties: larger first, then path), until the total is at most capBytes.
// Protected files count toward the total but are never chosen. capBytes 0 means no cap (nothing is chosen).
std::vector<size_t> cacheFilesToTrim(const std::vector<CacheFile>& files, uint64_t capBytes);

// "2.8 GB", "183 MB", "12 KB", "512 bytes".
std::string formatCacheBytes(uint64_t bytes);

}  // namespace iw8r
