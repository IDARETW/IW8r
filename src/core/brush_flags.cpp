// IW8 Radiant Suite - brush body flags (see brush_flags.h).
#include "brush_flags.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <vector>

namespace iw8r {
namespace {

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Same rule as iw8_contents.cpp contentsWord(): "contents detail;" -> "detail", anything else -> "".
std::string directiveWord(const std::string& text) {
    const std::string t = trimmed(text);
    const std::string kw = "contents";
    if (t.size() <= kw.size() || lowerCase(t.substr(0, kw.size())) != kw) return {};
    if (!std::isspace(static_cast<unsigned char>(t[kw.size()]))) return {};
    std::string rest = trimmed(t.substr(kw.size()));
    if (!rest.empty() && rest.back() == ';') rest.pop_back();
    return lowerCase(trimmed(rest));
}

// Flag slots in a fixed order: detail, nonColliding, weaponClip.
constexpr size_t kFlagCount = 3;
constexpr std::array<const char*, kFlagCount> kLowerWords = {"detail", "noncolliding", "weaponclip"};
constexpr std::array<const char*, kFlagCount> kWrittenWords = {"detail", "nonColliding", "weaponClip"};

// Slot of a directive line, or -1 when the line is not one of the modelled flags.
int flagSlot(const std::string& lineText) {
    const std::string w = directiveWord(lineText);
    for (size_t k = 0; k < kFlagCount; ++k)
        if (w == kLowerWords[k]) return int(k);
    return -1;
}

std::array<bool, kFlagCount> wantedSlots(const BrushFlags& f) {
    return {f.detail, f.nonColliding, f.weaponClip};
}

}  // namespace

BrushFlags readBrushFlags(const Primitive& brush) {
    BrushFlags out;
    for (const TextLine& l : brush.lines) {
        switch (flagSlot(l.text)) {
        case 0: out.detail = true; break;
        case 1: out.nonColliding = true; break;
        case 2: out.weaponClip = true; break;
        default: break;
        }
    }
    return out;
}

std::optional<Primitive> withBrushFlags(const Primitive& brush, const BrushFlags& flags, const std::string& eol) {
    if (brush.kind != PrimKind::Brush) return std::nullopt;
    const std::array<bool, kFlagCount> want = wantedSlots(flags);

    // Keep the first line of each wanted flag; drop unwanted and duplicate flag lines.
    std::vector<TextLine> kept;
    kept.reserve(brush.lines.size() + kFlagCount);
    std::array<bool, kFlagCount> seen{};
    for (const TextLine& l : brush.lines) {
        const int slot = flagSlot(l.text);
        if (slot < 0) {
            kept.push_back(l);
            continue;
        }
        const auto s = size_t(slot);
        if (want[s] && !seen[s]) {
            seen[s] = true;
            kept.push_back(l);
        }
    }

    // Insert the missing wanted flags right after the opening brace.
    std::vector<TextLine> added;
    for (size_t k = 0; k < kFlagCount; ++k)
        if (want[k] && !seen[k]) added.push_back(TextLine{std::string(" contents ") + kWrittenWords[k] + ";", eol});
    if (!added.empty()) {
        size_t open = kept.size();
        for (size_t i = 0; i < kept.size(); ++i)
            if (trimmed(kept[i].text) == "{") {
                open = i;
                break;
            }
        if (open == kept.size()) return std::nullopt;  // no body: not a brush text
        kept.insert(kept.begin() + std::ptrdiff_t(open + 1), added.begin(), added.end());
    }

    bool same = kept.size() == brush.lines.size();
    for (size_t i = 0; same && i < kept.size(); ++i)
        same = kept[i].text == brush.lines[i].text && kept[i].eol == brush.lines[i].eol;
    if (same) return brush;

    Primitive out = brush;
    out.lines = std::move(kept);
    out.dirty = true;
    reparsePrimitive(out);
    return out;
}

}  // namespace iw8r
