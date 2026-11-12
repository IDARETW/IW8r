#include "techset_detail.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "json_lite.h"
#include "material_blend.h"
#include "techset_info.h"

namespace iw8r {

namespace {

using json::Value;

// Object member as a string, or "" when missing or not a string.
std::string strOf(const Value* v, const char* key) {
    if (!v) return {};
    const Value* m = v->get(key);
    if (!m) return {};
    const std::string* s = m->string();
    return s ? *s : std::string();
}

// Number member, or `fallback` when missing or not a number.
double numOf(const Value* v, const char* key, double fallback = 0) {
    if (!v) return fallback;
    const Value* m = v->get(key);
    if (!m) return fallback;
    const auto n = m->number();
    return n ? *n : fallback;
}

// Integer text for a JSON number (values such as 1.8e19 are printed as exact unsigned integers when they fit).
std::string intText(double v) {
    if (!std::isfinite(v)) return "?";
    if (v >= 0 && v < 1.8446744073709552e19 && std::floor(v) == v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(v));
        return buf;
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::string hexByte(double v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02x", static_cast<unsigned>(static_cast<int>(v) & 0xff));
    return buf;
}

// stateBits.values[i].bits: 5 bytes each, printed as hex groups.
std::string stateBitsText(const Value* tech) {
    const Value* sb = tech->get("stateBits");
    if (!sb) return {};
    const Value* vals = sb->get("values");
    if (!vals || vals->type != Value::Type::Array) return {};
    std::string out;
    for (const Value& rec : vals->arr) {
        const Value* bits = rec.get("bits");
        if (!bits || bits->type != Value::Type::Array) continue;
        std::string one;
        for (const Value& b : bits->arr) one += (one.empty() ? "" : " ") + hexByte(b.num);
        out += (out.empty() ? "" : "; ") + std::string("[") + one + "]";
    }
    return out;
}

std::string stateSummaryOf(const Value* tech) {
    std::string s = "PSOs " + intText(numOf(tech, "pipelineStateCount")) + ", RTs " + intText(numOf(tech, "colorRtCount"));
    const std::string bits = stateBitsText(tech);
    if (!bits.empty()) s += ", state bits " + bits;
    if (const Value* d = tech->get("defaultStateBits")) {
        if (const Value* blend = d->get("blendBits")) {
            s += ", blend " + intText(numOf(blend, "value"));
            const std::string bn = strOf(blend, "name");
            if (!bn.empty()) s += " (" + bn + ")";
        }
        if (const Value* other = d->get("otherBits")) s += ", other " + intText(numOf(other, "value"));
    }
    const std::string layout = strOf(tech->get("precompiledIndex"), "name");
    if (!layout.empty()) s += ", layout " + layout;
    return s;
}

// Stage shader names that are set. A null stage or a missing name is skipped.
std::vector<std::string> shadersOf(const Value* tech) {
    std::vector<std::string> out;
    for (const char* stage : {"vertexShader", "hullShader", "domainShader", "pixelShader"}) {
        const std::string n = strOf(tech->get(stage), "name");
        if (!n.empty()) out.push_back(n);
    }
    return out;
}

const char* familyText(TechsetFamily f) {
    switch (f) {
        case TechsetFamily::Lit: return "world and model surfaces, lit";
        case TechsetFamily::UnlitEffect: return "effect sprites and sheets, unlit";
        case TechsetFamily::Distortion: return "heat haze and water ripple, no colour of its own";
        case TechsetFamily::LightProxyCard: return "lighting proxy card, never drawn";
        case TechsetFamily::Sky: return "sky";
        case TechsetFamily::Ui2d: return "2D / UI";
        case TechsetFamily::Null: return "info carrier, never drawn";
        default: return "not a recognised family";
    }
}

}  // namespace

bool parseTechsetExport(const std::string& jsonText, TechsetDetail& out, std::string* err) {
    out = TechsetDetail{};
    std::string perr;
    const auto root = json::parse(jsonText, &perr);
    if (!root) {
        if (err) *err = perr.empty() ? "invalid JSON" : perr;
        return false;
    }
    if (root->type != Value::Type::Object) {
        if (err) *err = "root is not a JSON object";
        return false;
    }

    const Value* asset = root->get("asset");
    if (!asset || asset->type != Value::Type::Object) {
        out.notes.push_back("no asset object: this is not an ACTS asset export");
        return true;
    }
    const std::string assetType = strOf(asset, "type");
    if (!assetType.empty() && assetType != "MaterialTechniqueSet")
        out.notes.push_back("asset type is " + assetType + ", expected MaterialTechniqueSet");

    const Value* fields = asset->get("fields");
    if (!fields || fields->type != Value::Type::Object) {
        out.notes.push_back("no fields object: the technique set body is empty");
        return true;
    }
    out.name = strOf(fields->get("name"), "string");
    out.materialType = strOf(fields->get("materialType"), "name");
    if (out.name.empty()) out.notes.push_back("no technique set name in the export");

    const Value* masked = fields->get("maskedTechniques");
    const Value* slots = masked ? masked->get("values") : nullptr;
    if (!slots || slots->type != Value::Type::Array) {
        out.notes.push_back("no maskedTechniques: the technique set has no techniques");
        return true;
    }
    for (const Value& slot : slots->arr) {
        const Value* recs = slot.get("values");
        if (!recs || recs->type != Value::Type::Array) continue;
        for (const Value& rec : recs->arr) {
            if (rec.type != Value::Type::Object) continue;
            TechniqueDetail t;
            t.name = strOf(rec.get("name"), "string");
            t.typeName = strOf(rec.get("type"), "name");
            t.shaders = shadersOf(&rec);
            if (const Value* args = rec.get("args")) {
                if (const Value* av = args->get("values"); av && av->type == Value::Type::Array) t.argumentCount = av->arr.size();
            }
            t.stateSummary = stateSummaryOf(&rec);
            if (t.name.empty()) out.notes.push_back("a technique has no name");
            out.techniques.push_back(std::move(t));
        }
    }
    if (out.techniques.empty()) out.notes.push_back("maskedTechniques has no technique records");
    return true;
}

std::string describeTechsetName(const std::string& techsetName) {
    std::vector<std::string> lines;
    if (techsetName.empty()) return "No technique set name.";

    lines.push_back("Name: " + techsetName);
    const TechsetInfo info = parseTechset(techsetName);
    lines.push_back(std::string("Family: ") + techsetFamilyName(info.family) + " (" + familyText(info.family) + ")");
    lines.push_back(std::string("Blend: ") + materialBlendName(info.blend));
    if (info.glass) lines.push_back("Glass: yes (alpha blend with reveal, vehicle glass and visors)");
    if (!info.tableToken.empty()) lines.push_back("Table: " + info.tableToken + " (texture table token)");

    // State token: the one after the table token (research 66). Its meaning is not decoded.
    const std::string leaf = [&] {
        const size_t slash = techsetName.rfind('/');
        std::string l = slash == std::string::npos ? techsetName : techsetName.substr(slash + 1);
        if (!l.empty() && (l[0] == '#' || l[0] == ',')) l.erase(0, 1);
        return l;
    }();
    std::vector<std::string> tok;
    size_t start = 0;
    for (;;) {
        const size_t us = leaf.find('_', start);
        tok.push_back(leaf.substr(start, us == std::string::npos ? std::string::npos : us - start));
        if (us == std::string::npos) break;
        start = us + 1;
    }
    for (size_t i = 0; !info.blendToken.empty() && i + 2 < tok.size(); ++i) {
        if (tok[i] == info.blendToken && tok[i + 1] == info.tableToken) {
            lines.push_back("State word: " + tok[i + 2] + " (not decoded)");
            break;
        }
    }

    lines.push_back(std::string("Editor draws it: ") + (materialDrawFor("", techsetName, -1) == MaterialDraw::Draw ? "yes" : "no"));
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return out;
}

}  // namespace iw8r
