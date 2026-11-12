// DDS texture decoding for material-browser thumbnails.
// Decodes mip 0 / array slice 0 of an in-memory DDS file to an 8-bit RGBA image.
// Block compression is handled by the vendored bcdec single-header decoder (third_party/bcdec).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r::app {

struct Rgba8Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, row-major, top-down; size == width * height * 4
};

// Decodes mip 0 of slice 0. Accepts legacy FourCC (DXT1/3/5, ATI1/BC4U, ATI2/BC5U) and DX10 headers with
// BC1-BC7, R8G8B8A8, R8 and R16G16B16A16_FLOAT. BC6H and float data are tone-mapped to LDR (x/(1+x), then
// gamma 1/2.2). BC4 becomes grey, BC5 becomes RG with B = reconstructed normal Z. Returns false on any
// malformed, truncated or unsupported input and sets *err when given.
bool decodeDds(const std::string& fileBytes, Rgba8Image& out, std::string* err = nullptr);

// Box-filter downscale so that max(width, height) <= maxSide, keeping aspect. Returns a copy of the input
// when it is already small enough (or when maxSide <= 0).
Rgba8Image downscale(const Rgba8Image& in, int maxSide);

// Previews (viewport textures, browser thumbnails) never show a stock image at full size, and decoding the top mip
// of a 2048x2048 block-compressed image costs 100 ms and more. These decode the smallest mip level of slice 0 that
// is still at least maxSide on its larger side (the top level when the file stores no smaller one, or is cut short),
// then box-filter that level down to maxSide. fullWidth / fullHeight receive the size of mip 0. maxSide <= 0 decodes
// mip 0 unscaled. Formats, tone mapping and errors are those of decodeDds.
bool decodeDdsPreview(const std::string& fileBytes, int maxSide, Rgba8Image& out, int* fullWidth = nullptr,
                      int* fullHeight = nullptr, std::string* err = nullptr);

// decodeDdsPreview for a file on disk (UTF-8 path): reads the header and the bytes of the chosen level only.
bool loadDdsPreview(const std::string& path, int maxSide, Rgba8Image& out, int* fullWidth = nullptr,
                    int* fullHeight = nullptr, std::string* err = nullptr);

}  // namespace iw8r::app
