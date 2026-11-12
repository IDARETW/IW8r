#include "material_slots.h"

namespace iw8r {

// Slot roles from the x64-zt converter (iw3_render_assets.cpp) and a survey of 6,135 stock material bodies
// (research 66: image name suffixes per slot and blend mode). Layered world materials use one slot per layer in
// each group (colour 0-6, normal 9-15, opacity / reveal 27-33, tint 59-65).
std::string iw8TextureSlotName(int index) {
    auto layer = [](int base, int i) { return i == base ? std::string() : ", layer " + std::to_string(i - base + 1); };
    if (index == 0x00) return "colour (non-metal mask in alpha)";
    if (index >= 0x01 && index <= 0x06) return "colour" + layer(0, index);
    if (index >= 0x09 && index <= 0x0F) return (index == 0x09 ? "normal + gloss + occlusion (packed)" : "normal + gloss") + layer(9, index);
    if (index >= 18 && index <= 20) return "emissive" + layer(18, index);
    if (index == 0x1B) return "alpha / opacity (cut-out mask of alpha-tested sets)";
    if (index >= 0x1C && index <= 0x21) return "reveal / opacity" + layer(27, index);
    if (index == 36) return "colour, detail";
    if (index == 45) return "thermal";
    if (index >= 54 && index <= 55) return index == 54 ? "transition" : "colour lookup (LUT)";
    if (index >= 59 && index <= 65) return "tint" + layer(59, index);
    if (index >= 68 && index <= 69) return "translucency / absorption" + layer(68, index);
    if (index >= 77 && index <= 78) return "thickness" + layer(77, index);
    if (index >= 95 && index <= 99) return "height / displacement" + layer(95, index);
    if (index == 104) return "infrared";
    if (index >= 123 && index <= 125) return "cavity / curvature" + layer(123, index);
    if (index == 126) return "roughness / dirt reveal";
    if (index == 128) return "sheen";
    if (index == 144) return "tint (constant name)";
    if (index >= 159 && index <= 164) {
        static const char* kImposter[] = {"impostor albedo", "impostor normal", "impostor translucence", "impostor specular",
                                          "impostor depth", "impostor geometry"};
        return kImposter[index - 159];
    }
    return "slot " + std::to_string(index);
}

bool iw8TextureSlotIsBuilt(int index) { return index == 0x00 || index == 0x09 || index == 0x1B; }

}  // namespace iw8r
