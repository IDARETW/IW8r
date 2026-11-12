#include "cache_trim.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace iw8r {

bool cacheFileProtected(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.empty() || name[0] == '.') return true;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    if (lower == "index.json") return true;
    return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".ini") == 0;
}

uint64_t cacheTotalBytes(const std::vector<CacheFile>& files) {
    uint64_t t = 0;
    for (const CacheFile& f : files) t += f.bytes;
    return t;
}

std::vector<size_t> cacheFilesToTrim(const std::vector<CacheFile>& files, uint64_t capBytes) {
    std::vector<size_t> out;
    if (capBytes == 0) return out;
    uint64_t total = cacheTotalBytes(files);
    if (total <= capBytes) return out;
    std::vector<size_t> order(files.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const CacheFile& x = files[a];
        const CacheFile& y = files[b];
        if (x.mtime != y.mtime) return x.mtime < y.mtime;
        if (x.bytes != y.bytes) return x.bytes > y.bytes;
        return x.path < y.path;
    });
    for (size_t i : order) {
        if (total <= capBytes) break;
        if (cacheFileProtected(files[i].path)) continue;
        out.push_back(i);
        total -= files[i].bytes;
    }
    return out;
}

std::string formatCacheBytes(uint64_t bytes) {
    char buf[64];
    const double b = double(bytes);
    if (bytes >= 1000ull * 1000 * 1000) std::snprintf(buf, sizeof buf, "%.1f GB", b / 1e9);
    else if (bytes >= 1000ull * 1000) std::snprintf(buf, sizeof buf, "%.0f MB", b / 1e6);
    else if (bytes >= 1000) std::snprintf(buf, sizeof buf, "%.0f KB", b / 1e3);
    else std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(bytes));
    return buf;
}

}  // namespace iw8r
