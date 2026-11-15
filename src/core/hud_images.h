// A map's own HUD images (ROADMAP 4.27, research 36): picture files in the folder "<map name>.hud" beside the map.
// The build writes each one as extracted/images/hud_<name>.dds; the converter carries every hud_* image into the map
// zone with a 2D material of the same name, so the map script can draw it:
//     icon = newhudelem(); icon setshader("hud_<name>", 64, 64);
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct HudImage {
    std::string name;  // the image and material name: "hud_" + the file's name in [a-z0-9_]
    std::string path;  // the picture file
};

inline constexpr size_t kMaxHudImages = 64;

// The HUD name of a picture file name ("Flag Icon.png" -> "hud_flag_icon"; a name that starts with hud_ keeps it).
// Empty when nothing usable is left.
std::string hudImageName(const std::string& fileName);

// The pictures of a folder (PNG, TGA, JPG, BMP, DDS; not in sub-folders), sorted by name. A second file that gives
// the same name, a file whose name gives none, and files past kMaxHudImages are left out, each with a line in
// *warnings. A folder that does not exist gives none and no warning.
std::vector<HudImage> listHudImages(const std::string& folder, std::vector<std::string>* warnings = nullptr);

// The picture as the DDS the converter takes for a map's own images: DXT5 with mips, square, a power of two (the
// form of the minimap image). The picture is scaled to a square of the next power of two of its longer side, 4 to
// 1024; drawn with its own width and height by setshader it has its shape again.
bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err);
// The same for a picture in memory (RGBA, w x h): the map's font pictures (hud_fonts.h).
bool encodeHudRgbaDds(int w, int h, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& dds, std::string* err);

// The folder for a map file: "<dir>/<map base name>.hud". Empty for an empty path.
std::string hudImageFolderFor(const std::string& mapPath);

// The hud_* names a script draws (setshader / precacheshader with a literal name), lower case, sorted, each once.
std::vector<std::string> hudNamesInScript(const std::string& scriptText);

}  // namespace iw8r
