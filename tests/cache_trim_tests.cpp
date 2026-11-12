#include <vector>

#include "cache_trim.h"
#include "test_harness.h"

using namespace iw8r;

TEST(cache_trim_protects_indexes) {
    CHECK(cacheFileProtected("index.json"));
    CHECK(cacheFileProtected("C:/x/material_cache/INDEX.JSON"));
    CHECK(cacheFileProtected("a\\b\\IW8Radiant.ini"));
    CHECK(cacheFileProtected("glb/.lock"));
    CHECK(!cacheFileProtected("images/tm_brick_c.dds"));
    CHECK(!cacheFileProtected("glb/box.lod1.glb"));
}

TEST(cache_trim_oldest_first_until_under_cap) {
    const std::vector<CacheFile> files = {
        {"index.json", 100, 1},       // oldest but protected
        {"images/a.dds", 400, 10},
        {"images/b.dds", 300, 5},
        {"images/c.dds", 200, 20},
        {"images/d.dds", 100, 30},
    };
    CHECK(cacheTotalBytes(files) == 1100);
    CHECK(cacheFilesToTrim(files, 0).empty());     // no cap
    CHECK(cacheFilesToTrim(files, 1100).empty());  // already under
    const auto t = cacheFilesToTrim(files, 600);
    // b (mtime 5) then a (10): 1100 - 300 - 400 = 400 <= 600.
    CHECK(t.size() == 2 && t[0] == 2 && t[1] == 1);
    // A cap below the protected bytes deletes every unprotected file and stops.
    const auto all = cacheFilesToTrim(files, 50);
    CHECK(all.size() == 4);
    for (size_t i : all) CHECK(i != 0);
}

TEST(cache_trim_ties_and_format) {
    const std::vector<CacheFile> files = {{"x/small.dds", 10, 7}, {"x/big.dds", 90, 7}};
    const auto t = cacheFilesToTrim(files, 50);
    CHECK(t.size() == 1 && t[0] == 1);  // same age: the larger file goes first
    CHECK(formatCacheBytes(2'800'000'000ull) == "2.8 GB");
    CHECK(formatCacheBytes(183'000'000ull) == "183 MB");
    CHECK(formatCacheBytes(12'000) == "12 KB");
    CHECK(formatCacheBytes(512) == "512 bytes");
}
