// Stock map vision / fog values (roadmap 11.6, research 48): a stock zone's vision is a plain-text rawfile of
// `KEY "value"` lines with hashed key names (vision/<map>.vision in a zonetool dump). Edits change values of keys that
// are already there and never add or remove one (the vision has a fixed field set).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace iw8r {

// Known hashed keys (x64-zt iw3_vision.cpp field table, research 48 section 11.6).
inline constexpr const char* kVisionFogStart = "RPNOKKL";        // fogStartDist, units
inline constexpr const char* kVisionFogDensity = "MSTOLQOTKN";   // fogDensity (distance scale)
inline constexpr const char* kVisionFogColor = "NMLMMNQLSQ";     // fogColor "r g b" 0..1
inline constexpr const char* kVisionFogMaxOpacity = "TRPROKTLS"; // fogMaxOpacity 0..1

// Values by key, in file order.
std::map<std::string, std::string> readVisionValues(const std::string& text);

// Applies `edits` (key -> value) to the text; keys not in the text are reported in *missing and not added. Returns the
// new text; *changed counts the values that differ from before. Line endings and other lines are kept.
std::string applyVisionEdits(const std::string& text, const std::map<std::string, std::string>& edits, int* changed = nullptr,
                             std::vector<std::string>* missing = nullptr);

}  // namespace iw8r
