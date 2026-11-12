// IW8 Radiant Suite - brush body flags (`contents X;` directive lines, doc 02 §3).
//
// Only the words iw8_contents.cpp recognises are modelled: detail, nonColliding and weaponClip. Other
// `contents` words and every other line of the brush stay byte-identical when the flags change.
#pragma once

#include <optional>
#include <string>

#include "map_text.h"

namespace iw8r {

struct BrushFlags {
    bool detail = false;        // `contents detail;`: collides and is drawn, does not seal a leak
    bool nonColliding = false;  // `contents nonColliding;`: no world collision, contents 0
    bool weaponClip = false;    // `contents weaponClip;`: item clip bit 0x400 added to the contents
    bool operator==(const BrushFlags& o) const = default;
};

// Flags written in the brush body (case-insensitive word, as the compiler reads them).
BrushFlags readBrushFlags(const Primitive& brush);

// The brush with exactly the `contents` lines `flags` asks for: missing flag lines are inserted after the
// opening brace (" contents X;" with `eol`), unwanted or duplicate flag lines are removed. All other lines
// are kept as they are. Returns the primitive unchanged when it already has these flags, and nullopt when
// `brush` is not a brush (patch or unknown).
std::optional<Primitive> withBrushFlags(const Primitive& brush, const BrushFlags& flags, const std::string& eol);

}  // namespace iw8r
