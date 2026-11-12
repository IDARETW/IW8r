#include "log_links.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>

namespace iw8r {

namespace {

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/' || c == '.';
}

bool hasAlpha(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; });
}

// A bare name after a keyword links only when it looks like an asset name: "model cache" and "material is" do not.
bool assetShaped(const std::string& s) {
    if (!hasAlpha(s)) return false;
    return std::any_of(s.begin(), s.end(), [](char c) { return c == '_' || c == '/' || c == '.'; });
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool equalFold(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

// "entity 12", "(entity 12)", "entity #12". Entity numbers are map.entities indices (see the header).
const std::regex& entityRe() {
    static const std::regex re(R"re(\bentity\s+#?([0-9]{1,9})(?![0-9A-Za-z_]))re");
    return re;
}

// "maps/mp/mp_snip.gsc:193" (a script position) or "mp_snip.gsc" (the file). Optional drive letter for Windows paths.
const std::regex& scriptRe() {
    static const std::regex re(R"re(((?:[A-Za-z]:[\\/])?[A-Za-z0-9_\-./\\]+\.gsc)(?![A-Za-z0-9_])(?::([0-9]{1,9})(?![0-9A-Za-z_]))?)re");
    return re;
}

// Other files the UI opens: map sources, .ents text, JSON and logs.
const std::regex& fileRe() {
    static const std::regex re(R"re(((?:[A-Za-z]:[\\/])?[A-Za-z0-9_\-./\\]+\.(?:map|ents|json|log))(?![A-Za-z0-9_]))re");
    return re;
}

struct KeywordRule {
    std::regex re;
    LogRef::Kind kind;
};

// `keyword 'name'`, `keyword "name"` or `keyword name` (bare name only when assetShaped).
const std::vector<KeywordRule>& keywordRules() {
    static const std::vector<KeywordRule> rules = [] {
        const std::vector<std::pair<std::string, LogRef::Kind>> keys = {
            {"model|script_model|misc_model", LogRef::Model}, {"targetname", LogRef::Targetname},
            {"material", LogRef::Material},                   {"image", LogRef::Image},
            {"effect", LogRef::Effect},                       {"soundalias", LogRef::Sound},
        };
        std::vector<KeywordRule> out;
        for (const auto& [kw, kind] : keys) {
            const std::string pattern = R"re(\b(?:)re" + kw + R"re()\s+(?:'([^']+)'|"([^"]+)"|([A-Za-z0-9_\-./]+)))re";
            out.push_back({std::regex(pattern), kind});
        }
        return out;
    }();
    return rules;
}

// Stock material path shapes the editor and zonetool print: "tm/..", "mo/..", and the "tmo/", "tm2co/", "custom/" and
// "iw8r_dev/" forms seen in the editor's own warnings.
void addTokenRefs(const std::string& line, std::vector<LogRef>& out) {
    static const std::vector<std::string> kMaterialPrefixes = {"tm/", "mo/", "tmo/", "tm2co/", "custom/", "iw8r_dev/"};
    size_t i = 0;
    while (i < line.size()) {
        if (!isNameChar(line[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < line.size() && isNameChar(line[j])) ++j;
        size_t len = j - i;
        while (len > 0 && line[i + len - 1] == '.') --len;  // a sentence full stop is not part of the name
        // "@mw120r_iw3fx@vfx/..." is the converter's translated target, not a name the log asks about.
        const bool afterAt = i > 0 && line[i - 1] == '@';
        const std::string tok = line.substr(i, len);
        if (!afterAt && len > 0) {
            if (startsWith(tok, "vfx/") && len > 4) {
                out.push_back({LogRef::Effect, tok, 0, i, i + len, false});
            } else {
                for (const std::string& p : kMaterialPrefixes) {
                    if (startsWith(tok, p) && len > p.size()) {
                        out.push_back({LogRef::Material, tok, 0, i, i + len, false});
                        break;
                    }
                }
            }
        }
        i = j;
    }
}

}  // namespace

std::vector<LogRef> findLogRefs(const std::string& logLine) {
    std::vector<LogRef> out;

    for (auto it = std::sregex_iterator(logLine.begin(), logLine.end(), entityRe()); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        const size_t b = size_t(m.position(0));
        out.push_back({LogRef::Entity, m[1].str(), 0, b, b + size_t(m.length(0)), false});
    }

    for (auto it = std::sregex_iterator(logLine.begin(), logLine.end(), scriptRe()); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        const size_t b = size_t(m.position(0));
        const size_t e = b + size_t(m.length(0));
        const long line = m[2].matched ? std::strtol(m[2].str().c_str(), nullptr, 10) : 0;
        if (line > 0)
            out.push_back({LogRef::ScriptLine, m[1].str(), int(line), b, e, false});
        else
            out.push_back({LogRef::File, m[1].str(), 0, b, e, false});
    }

    for (auto it = std::sregex_iterator(logLine.begin(), logLine.end(), fileRe()); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        const size_t b = size_t(m.position(0));
        out.push_back({LogRef::File, m[1].str(), 0, b, b + size_t(m.length(0)), false});
    }

    for (const KeywordRule& rule : keywordRules()) {
        for (auto it = std::sregex_iterator(logLine.begin(), logLine.end(), rule.re); it != std::sregex_iterator(); ++it) {
            const std::smatch& m = *it;
            const size_t g = m[1].matched ? 1 : m[2].matched ? 2 : 3;
            if (!m[g].matched) continue;
            std::string name = m[g].str();
            if (g == 3) {
                while (!name.empty() && name.back() == '.') name.pop_back();
                if (!assetShaped(name)) continue;
            }
            if (name.empty()) continue;
            const size_t b = size_t(m.position(g));
            out.push_back({rule.kind, name, 0, b, b + name.size(), false});
        }
    }

    addTokenRefs(logLine, out);

    // Keep one reference per stretch of text: the list is sorted by span, and when two overlap the first one found wins
    // (a keyword match before the generic path match that repeats it).
    std::stable_sort(out.begin(), out.end(), [](const LogRef& a, const LogRef& b) { return a.begin < b.begin; });
    std::vector<LogRef> kept;
    for (LogRef& r : out) {
        if (!kept.empty() && r.begin < kept.back().end) continue;
        kept.push_back(std::move(r));
    }
    return kept;
}

std::vector<Id> resolveLogRef(const MapFile& map, const LogRef& ref) {
    std::vector<Id> out;
    if (ref.value.empty()) return out;
    auto add = [&out](Id id) {
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };

    switch (ref.kind) {
        case LogRef::Entity: {
            if (ref.value.size() > 9 || ref.value.find_first_not_of("0123456789") != std::string::npos) break;
            const Id n = Id(std::strtoul(ref.value.c_str(), nullptr, 10));
            if (ref.entityIsId) {
                for (const Entity& e : map.entities)
                    if (e.id == n) add(e.id);
            } else if (n < map.entities.size()) {
                add(map.entities[n].id);
            }
            break;
        }
        case LogRef::Targetname:
            for (const Entity& e : map.entities) {
                const std::string* v = e.value("targetname");
                if (v && *v == ref.value) add(e.id);
            }
            break;
        case LogRef::Model:
            for (const Entity& e : map.entities) {
                const std::string* v = e.value("model");
                if (v && *v == ref.value) add(e.id);
            }
            break;
        case LogRef::Effect:
            for (const Entity& e : map.entities) {
                const std::string* v = e.value("fx");
                if (v && lower(e.classname()) == "fx_origin" && equalFold(*v, ref.value)) add(e.id);
            }
            break;
        case LogRef::Sound:
            for (const Entity& e : map.entities) {
                const std::string* v = e.value("soundalias");
                if (v && lower(e.classname()) == "sound_emitter" && equalFold(*v, ref.value)) add(e.id);
            }
            break;
        case LogRef::Material:
            // An entity matches when one of its primitives (brush faces or a patch) uses the material.
            for (const Entity& e : map.entities) {
                bool hit = false;
                for (const EntityItem& it : e.items) {
                    const Primitive* p = it.prim();
                    if (!p) continue;
                    if (p->kind == PrimKind::Brush) {
                        for (const BrushFace& f : p->faces)
                            if (equalFold(f.material(), ref.value)) hit = true;
                    } else if (p->kind == PrimKind::Patch && equalFold(p->patchMaterial, ref.value)) {
                        hit = true;
                    }
                    if (hit) break;
                }
                if (hit) add(e.id);
            }
            break;
        case LogRef::Image:
        case LogRef::ScriptLine:
        case LogRef::File:
            break;  // the UI opens the file instead
    }
    return out;
}

}  // namespace iw8r
