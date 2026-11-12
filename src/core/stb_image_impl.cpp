// The one translation unit that includes stb_image.h (third_party/stb, public domain / MIT; the licence text is
// kept in that header). Exposes a single decode function to custom_materials.cpp.
#include <string>
#include <vector>

#include "custom_materials.h"

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma warning(pop)

namespace iw8r::detail {

// Decodes any file stb_image understands to RGBA8. Returns false with a reason in err.
bool stbDecodeRgba(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba, std::string* err) {
    int comp = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels) {
        if (err) *err = "cannot decode image '" + path + "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return false;
    }
    rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}

}  // namespace iw8r::detail
