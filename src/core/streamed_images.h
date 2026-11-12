// Streamed stock images: which Replay 1.20 images have their full mip chain only in .xpak payloads, and the
// resident size the editor's catalogue reports. Data comes from data/asset_catalog/iw8_streamed_images.tsv, built by
// tools/build_streamed_images.py (docs/research/52_streamed_images.md). Read-only; no ACTS run.
#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace iw8r {

// One catalogue row. Sizes are in texels. resident* is the top mip of the smallest XPak stream part when streamed
// (UNVERIFIED as the game-resident level), otherwise the full size.
struct StreamedImage {
    std::string image;
    int fullW = 0;
    int fullH = 0;
    int residentW = 0;
    int residentH = 0;
    bool streamed = false;
};

class StreamedImageCatalog {
public:
    // Reads a TSV written by tools/build_streamed_images.py. Lines starting with '#', the column header row, and
    // blank lines are ignored. A row is skipped (and counted in skippedRows()) when it has the wrong column count,
    // a non-integer or non-positive size, a resident size larger than the full size, a streamed value other than 0
    // or 1, or an image name already seen. Clears any previous contents. Returns false only when the file cannot be
    // read; err then says why.
    bool load(const std::string& tsvPath, std::string* err = nullptr);

    // Null when the image is not in the catalogue. The pointer stays valid until the next load().
    const StreamedImage* find(const std::string& image) const;
    // 4.25: by the editor's material cache file stem (the image name with '&' and '~' as '_', e.g.
    // "a_c_defaultspec_918..." for "a_c&defaultspec~918..."). Null when not in the catalogue.
    const StreamedImage* findByCacheStem(const std::string& stem) const;

    size_t size() const { return images_.size(); }
    size_t skippedRows() const { return skipped_; }

private:
    std::map<std::string, StreamedImage> images_;
    std::map<std::string, std::string> byStem_;  // cache stem -> image name
    size_t skipped_ = 0;
};

// Human-readable quality line for one image, for the material and build panels:
//   streamed: "streamed: editor gets 512x512 of 2048x2048"  (the resident size, see StreamedImage)
//   not streamed: "not streamed: full 512x512"
std::string streamedQualityText(const StreamedImage& image);

// The width and height of a DDS file (header bytes 12 and 16). False when the file is not a DDS.
bool readDdsSize(const std::string& path, int& width, int& height);

// The cache stem of a catalogue image name ('&' and '~' become '_').
std::string streamedCacheStem(const std::string& image);

}  // namespace iw8r
