// IW8 Radiant - stock material colour tints (owner playtest 2026-11-11: traffic cones came out grey).
//
// A stock material can tint its colour image: material constant 9 holds R, G, B bytes that multiply the colour, FF FF FF
// being neutral (mo/me_traffic_cone_01_top: ED 37 0F, orange, over the grey image it shares with the cone's base, whose
// 80 80 80 makes the dark rubber foot). The catalogue data/asset_catalog/iw8_material_tints.tsv
// (tools/build_material_tints.py) lists the non-neutral ones. The multiply is inferred from those values (white on
// most world materials, half grey on the cone's base); it has not been compared with the game's picture.
// Pure core code.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace iw8r {

class MaterialTints {
public:
    // "<material>\t<RRGGBB>[\t...]" lines; '#' comments. False (and *err) when the file cannot be read.
    bool load(const std::string& tsvPath, std::string* err = nullptr);
    bool parse(const std::string& text);
    std::optional<std::array<uint8_t, 3>> find(const std::string& material) const;
    size_t size() const { return tints_.size(); }
    // The catalogue beside the editor (data/asset_catalog/iw8_material_tints.tsv found from `startDir` upward); empty
    // when the file is not there.
    static const MaterialTints& shared(const std::string& startDir);

private:
    std::map<std::string, std::array<uint8_t, 3>> tints_;
};

// RGBA8 pixels (alpha untouched): each colour channel times tint / 255.
void applyMaterialTint(uint8_t* rgba, size_t texels, const std::array<uint8_t, 3>& tint);

}  // namespace iw8r
