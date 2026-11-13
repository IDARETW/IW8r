import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/stock_probes.h'
C = 'src/core/stock_probes.cpp'

patch(H, '''StockProbeSet buildStockProbeSubset(const StockProbeSet& full, const ProbeVec3& camera, int maxBoxes = 127);''',
      '''StockProbeSet buildStockProbeSubset(const StockProbeSet& full, const ProbeVec3& camera, int maxBoxes = 127);

// Verdansk's images are 160+ MB: the editor keeps the records and this index of where each probe's image lies in the
// cache files, and reads only the subset's images.
struct StockProbeImageIndex {
    int size = 0, mipCount = 0;
    std::vector<std::string> files;
    std::vector<int> fileOf;         // per probe: index into files, -1 = no image anywhere
    std::vector<uint64_t> offsetOf;  // per probe: byte offset of its image in that file
};
// Adds the images of one cache file (v1, v2 main or v2 tile) to `idx`, sized to `probeCount` probes on first use.
// A later file wins for a probe two files hold. Fails on a file whose image size differs from the index's.
bool indexStockProbeCacheImages(const std::string& path, size_t probeCount, StockProbeImageIndex& idx, std::string* err = nullptr);
// buildStockProbeSubset for records whose images stay on disk: the same boxes and order, the chosen probes' images read
// from their files. Fails (empty set) when a chosen image cannot be read.
StockProbeSet buildStockProbeSubset(const StockProbeSet& records, const StockProbeImageIndex& idx, const ProbeVec3& camera,
                                    int maxBoxes = 127, std::string* err = nullptr);''')

# the subset choice, shared by both forms
patch(C, '''StockProbeSet buildStockProbeSubset(const StockProbeSet& full, const ProbeVec3& camera, int maxBoxes) {
    StockProbeSet sub;
    sub.size = full.size;
    sub.mipCount = full.mipCount;
    sub.arrayImageName = full.arrayImageName;
    sub.streamed = full.streamed;
    if (full.size <= 0 || full.mipCount <= 0) return sub;
    const size_t per = bc6hBytesPerProbe(full.size, full.mipCount);
    const auto hasImage = [&](const StockProbeBox& b) {
        return b.imageIndex >= 0 && size_t(b.imageIndex) < full.probes.size() && full.probes[size_t(b.imageIndex)].bc6h.size() == per;
    };''', '''namespace {
// The subset's boxes and probes (research 108 section 4.4); `imageOf(probe)` says whether a probe has an image, `fetch`
// fills a chosen probe's bc6h (false: the subset fails).
template <class HasImage, class Fetch>
StockProbeSet chooseProbeSubset(const StockProbeSet& full, int size, int mipCount, const ProbeVec3& camera, int maxBoxes,
                                HasImage imageOf, Fetch fetch) {
    StockProbeSet sub;
    sub.size = size;
    sub.mipCount = mipCount;
    sub.arrayImageName = full.arrayImageName;
    sub.streamed = full.streamed;
    if (size <= 0 || mipCount <= 0) return sub;
    const auto hasImage = [&](const StockProbeBox& b) {
        return b.imageIndex >= 0 && size_t(b.imageIndex) < full.probes.size() && imageOf(b.imageIndex);
    };''')
patch(C, '''        int& s = slice[size_t(b.imageIndex)];
        if (s < 0) {
            s = int(sub.probes.size());
            StockProbe p = full.probes[size_t(b.imageIndex)];
            p.boxes.clear();
            sub.probes.push_back(std::move(p));
        }
        b.imageIndex = s;
        sub.probes[size_t(s)].boxes.push_back(int(sub.boxes.size()));
        sub.boxes.push_back(b);
    }
    return sub;
}''', '''        int& s = slice[size_t(b.imageIndex)];
        if (s < 0) {
            s = int(sub.probes.size());
            StockProbe p;
            const StockProbe& src = full.probes[size_t(b.imageIndex)];
            p.origin = src.origin, p.angles = src.angles, p.mins = src.mins, p.maxs = src.maxs;
            p.relightingIndex = src.relightingIndex, p.imageIndirection = src.imageIndirection, p.sh = src.sh;
            if (!fetch(b.imageIndex, p.bc6h)) return StockProbeSet{};
            sub.probes.push_back(std::move(p));
        }
        b.imageIndex = s;
        sub.probes[size_t(s)].boxes.push_back(int(sub.boxes.size()));
        sub.boxes.push_back(b);
    }
    return sub;
}
}  // namespace

StockProbeSet buildStockProbeSubset(const StockProbeSet& full, const ProbeVec3& camera, int maxBoxes) {
    if (full.size <= 0 || full.mipCount <= 0) {
        StockProbeSet sub;
        sub.size = full.size, sub.mipCount = full.mipCount, sub.arrayImageName = full.arrayImageName, sub.streamed = full.streamed;
        return sub;
    }
    const size_t per = bc6hBytesPerProbe(full.size, full.mipCount);
    return chooseProbeSubset(
        full, full.size, full.mipCount, camera, maxBoxes, [&](int i) { return full.probes[size_t(i)].bc6h.size() == per; },
        [&](int i, std::vector<uint8_t>& out) {
            out = full.probes[size_t(i)].bc6h;
            return true;
        });
}

StockProbeSet buildStockProbeSubset(const StockProbeSet& records, const StockProbeImageIndex& idx, const ProbeVec3& camera,
                                    int maxBoxes, std::string* err) {
    if (idx.fileOf.size() != records.probes.size() || idx.size <= 0) {
        if (err) *err = "the image index does not match the probe records";
        return StockProbeSet{};
    }
    const size_t per = bc6hBytesPerProbe(idx.size, idx.mipCount);
    std::vector<std::unique_ptr<std::ifstream>> open(idx.files.size());
    return chooseProbeSubset(
        records, idx.size, idx.mipCount, camera, maxBoxes, [&](int i) { return idx.fileOf[size_t(i)] >= 0; },
        [&](int i, std::vector<uint8_t>& out) {
            const size_t f = size_t(idx.fileOf[size_t(i)]);
            if (!open[f]) open[f] = std::make_unique<std::ifstream>(fs::path(idx.files[f]), std::ios::binary);
            std::ifstream& in = *open[f];
            out.resize(per);
            in.clear();
            in.seekg(std::streamoff(idx.offsetOf[size_t(i)]), std::ios::beg);
            if (!in || !in.read(reinterpret_cast<char*>(out.data()), std::streamsize(per))) {
                if (err) *err = "cannot read a probe image from " + idx.files[f];
                return false;
            }
            return true;
        });
}''')
# readCacheFile reports where the images start and whose they are
patch(C, '''bool readCacheFile(const std::string& path, StockProbeSet* set, StockProbeImages* tile, bool withImages, uint32_t* versionOut,
                   std::string* err) {''', '''bool readCacheFile(const std::string& path, StockProbeSet* set, StockProbeImages* tile, bool withImages, uint32_t* versionOut,
                   std::string* err, std::vector<int>* ownersOut = nullptr, uint64_t* imagesAtOut = nullptr) {''')
patch(C, '''    if (!ok || fileBytes - pos != uint64_t(images) * per) return fail(": probe cache size does not match its header");
''', '''    if (!ok || fileBytes - pos != uint64_t(images) * per) return fail(": probe cache size does not match its header");
    if (ownersOut) *ownersOut = owners;
    if (imagesAtOut) *imagesAtOut = pos;
''')
patch(C, '''bool readStockProbeTileCache(const std::string& path, StockProbeImages& out, std::string* err) {
    return readCacheFile(path, nullptr, &out, true, nullptr, err);
}''', '''bool readStockProbeTileCache(const std::string& path, StockProbeImages& out, std::string* err) {
    return readCacheFile(path, nullptr, &out, true, nullptr, err);
}

bool indexStockProbeCacheImages(const std::string& path, size_t probeCount, StockProbeImageIndex& idx, std::string* err) {
    StockProbeSet s;
    StockProbeImages t;
    std::vector<int> owners;
    uint64_t at = 0;
    // A main file needs a set to read into, a tile file an image record: try the main form first.
    std::string e1;
    bool ok = readCacheFile(path, &s, nullptr, false, nullptr, &e1, &owners, &at);
    int size = s.size, mips = s.mipCount;
    if (!ok) {
        ok = readCacheFile(path, nullptr, &t, false, nullptr, err, &owners, &at);
        size = t.size, mips = t.mipCount;
        if (!ok) return false;
    }
    if (idx.fileOf.empty()) idx.fileOf.assign(probeCount, -1), idx.offsetOf.assign(probeCount, 0);
    if (idx.size == 0) idx.size = size, idx.mipCount = mips;
    if (size != idx.size || mips != idx.mipCount) {
        if (err) *err = path + ": its probe images are not the index's size";
        return false;
    }
    const size_t per = bc6hBytesPerProbe(size, mips);
    const int fileNo = int(idx.files.size());
    idx.files.push_back(path);
    for (size_t k = 0; k < owners.size(); ++k) {
        const int p = owners[k];
        if (p < 0 || size_t(p) >= idx.fileOf.size()) continue;
        idx.fileOf[size_t(p)] = fileNo;
        idx.offsetOf[size_t(p)] = at + uint64_t(k) * per;
    }
    return true;
}''')
print('ok')
