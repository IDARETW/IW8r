#include "techset_document.h"

#include <cmath>
#include <set>
#include <stdexcept>

namespace iw8r {
namespace {
using J = TechsetJson;
uint64_t integer(const J& value) {
    if (value.is_number_unsigned()) return value.get<uint64_t>();
    if (value.is_number_integer() && value.get<int64_t>() >= 0) return static_cast<uint64_t>(value.get<int64_t>());
    throw std::runtime_error("expected an exact non-negative integer");
}
uint32_t small(const J& j, const char* key, uint32_t fallback = 0) {
    if (!j.contains(key)) return fallback;
    const auto n = integer(j.at(key));
    if (n > UINT32_MAX) throw std::runtime_error(std::string(key) + " exceeds 32 bits");
    return static_cast<uint32_t>(n);
}
J object(const J& j, const char* key) {
    if (!j.contains(key)) return J::object();
    if (!j.at(key).is_object()) throw std::runtime_error(std::string(key) + " must be an object");
    return j.at(key);
}
J graphJson(const ShaderGraph& g) {
    J j = g.extra;
    j["nodes"] = J::array(); j["links"] = J::array(); j["subgraphs"] = J::object();
    for (const auto& n : g.nodes) {
        J v = n.extra;
        v["id"] = n.id; v["kind"] = n.kind; v["label"] = n.label;
        v["position"] = {n.x, n.y}; v["properties"] = n.properties;
        j["nodes"].push_back(std::move(v));
    }
    for (const auto& e : g.links) {
        J v = e.extra; v["from"] = e.from; v["output"] = e.output; v["to"] = e.to; v["input"] = e.input;
        j["links"].push_back(std::move(v));
    }
    for (const auto& [name, sub] : g.subgraphs) j["subgraphs"][name] = graphJson(sub);
    return j;
}
ShaderGraph graphRead(const J& j, unsigned depth = 0) {
    if (!j.is_object() || depth > 32) throw std::runtime_error("invalid graph or subgraph nesting exceeds 32");
    ShaderGraph g; g.extra = j;
    for (const auto& v : j.value("nodes", J::array())) {
        ShaderNode n; n.extra = v;
        n.id = v.at("id").get<std::string>(); n.kind = v.at("kind").get<std::string>();
        n.label = v.value("label", ""); n.properties = object(v, "properties");
        if (v.contains("position")) { n.x = v.at("position").at(0).get<double>(); n.y = v.at("position").at(1).get<double>(); }
        if (!std::isfinite(n.x) || !std::isfinite(n.y)) throw std::runtime_error("invalid node position");
        g.nodes.push_back(std::move(n));
    }
    for (const auto& v : j.value("links", J::array())) {
        ShaderLink e; e.extra = v; e.from = v.at("from"); e.to = v.at("to"); e.input = v.at("input");
        e.output = v.value("output", "out"); g.links.push_back(std::move(e));
    }
    const J subs = object(j, "subgraphs");
    for (auto it = subs.begin(); it != subs.end(); ++it) g.subgraphs.emplace(it.key(), graphRead(it.value(), depth + 1));
    return g;
}
J stageJson(const ShaderStageDocument& s) {
    J j = s.extra;
    j["stage"] = shaderStageName(s.stage); j["mode"] = s.mode; j["source"] = s.source; j["entry"] = s.entry;
    j["reference"] = s.reference; j["bytecodePath"] = s.bytecodePath; j["graph"] = graphJson(s.graph);
    return j;
}
std::string wrappedString(const J& j, const char* field, const char* value = "string") {
    if (!j.contains(field) || j.at(field).is_null()) return {};
    const auto& v = j.at(field);
    return v.is_string() ? v.get<std::string>() : v.value(value, "");
}
}

const char* shaderStageName(ShaderStage stage) {
    switch (stage) { case ShaderStage::Vertex: return "vertex"; case ShaderStage::Pixel: return "pixel";
        case ShaderStage::Hull: return "hull"; case ShaderStage::Domain: return "domain"; }
    return "pixel";
}
bool parseShaderStage(const std::string& name, ShaderStage& stage) {
    for (auto s : {ShaderStage::Vertex, ShaderStage::Pixel, ShaderStage::Hull, ShaderStage::Domain})
        if (name == shaderStageName(s)) { stage = s; return true; }
    return false;
}
ShaderStageDocument* TechniqueDefinition::findStage(ShaderStage stage) {
    for (auto& s : stages) if (s.stage == stage) return &s;
    return nullptr;
}
const ShaderStageDocument* TechniqueDefinition::findStage(ShaderStage stage) const {
    for (const auto& s : stages) if (s.stage == stage) return &s;
    return nullptr;
}
std::string TechniqueDefinition::previewLimitation() const {
    if (findStage(ShaderStage::Hull) || findStage(ShaderStage::Domain)) return "Hull/domain tessellation is preserved; live preview supports vertex and pixel stages.";
    if (slot != 0 && slot != 27 && slot != 30 && slot != 34 && slot != 38)
        return "This technique variant has no preview adapter (VRS, velocity, subdivision and special passes are preserved).";
    return {};
}
bool validateTechsetDocument(const TechsetDocument& d, std::string& error) {
    error.clear();
    const auto fail = [&](const std::string& e) { error = e; return false; };
    if (d.name.empty() || !d.layout.is_object() || !d.provenance.is_object() || !d.extra.is_object()) return fail("invalid techset name or layout");
    if (d.techniques.size() > 256) return fail("a technique set has at most 256 slots");
    std::set<std::string> ids; std::set<uint32_t> slots;
    for (const auto& t : d.techniques) {
        if (t.id.empty() || !ids.insert(t.id).second || t.slot > 255 || !slots.insert(t.slot).second) return fail("duplicate technique ID/slot, or slot outside 0..255");
        if (!t.layout.is_object() || !t.extra.is_object()) return fail("technique layout must be an object");
        std::set<ShaderStage> stages;
        for (const auto& s : t.stages) {
            if (!stages.insert(s.stage).second) return fail("duplicate shader stage");
            if (s.mode != "hlsl" && s.mode != "graph" && s.mode != "imported") return fail("unknown authoring mode");
            if (s.mode != "imported" && s.entry.empty()) return fail("shader entry point is empty");
        }
    }
    ids.clear(); slots.clear();
    for (const auto& p : d.parameters) {
        if (p.name.empty() || !ids.insert(p.name).second || p.slot >= 64 || !slots.insert(p.slot).second) return fail("duplicate parameter or slot outside 0..63");
        for (float f : p.value) if (!std::isfinite(f)) return fail("parameter is not finite");
    }
    ids.clear(); slots.clear();
    for (const auto& r : d.resources) {
        if (r.name.empty() || !ids.insert(r.name).second || r.slot >= 8 || !slots.insert(r.slot).second) return fail("duplicate resource or preview texture slot outside 0..7");
        if (r.kind != "texture2d" && r.kind != "texturecube") return fail("unsupported preview resource kind");
        if (r.filter != "point" && r.filter != "linear") return fail("sampler filter must be point or linear");
        if (r.address != "wrap" && r.address != "clamp" && r.address != "mirror") return fail("invalid sampler address mode");
    }
    return true;
}
void rebuildTechsetMask(TechsetDocument& d) {
    std::array<uint64_t, 4> mask{};
    for (const auto& t : d.techniques) if (t.slot < 256) mask[t.slot / 64] |= uint64_t(1) << (t.slot % 64);
    d.layout["techniqueMask"] = mask;
}
bool readTechsetDocument(const std::string& text, TechsetDocument& out, std::string& error) {
    try {
        const J j = J::parse(text);
        if (j.at("format") != "iw8r-techset" || integer(j.at("version")) != TechsetDocument::kVersion)
            throw std::runtime_error("unsupported techset format/version");
        TechsetDocument d; d.extra = j;
        d.name = j.at("name"); d.profile = j.value("profile", "replay-1.20"); d.sourceZone = j.value("sourceZone", "");
        d.layout = object(j, "layout"); d.provenance = object(j, "provenance");
        for (const auto& v : j.at("techniques")) {
            TechniqueDefinition t; t.extra = v; t.id = v.at("id"); t.name = v.at("name"); t.slot = small(v, "slot", 34);
            t.layout = object(v, "layout");
            for (const auto& sj : v.at("stages")) {
                ShaderStageDocument s; s.extra = sj;
                if (!parseShaderStage(sj.at("stage"), s.stage)) throw std::runtime_error("unknown shader stage");
                s.mode = sj.value("mode", "hlsl"); s.source = sj.value("source", ""); s.entry = sj.value("entry", "main");
                s.reference = sj.value("reference", ""); s.bytecodePath = sj.value("bytecodePath", "");
                s.graph = graphRead(object(sj, "graph")); t.stages.push_back(std::move(s));
            }
            d.techniques.push_back(std::move(t));
        }
        for (const auto& v : j.value("parameters", J::array())) {
            ShaderParameter p; p.extra = v; p.name = v.at("name"); p.slot = small(v, "slot");
            p.value = v.at("value").get<std::array<float, 4>>(); d.parameters.push_back(std::move(p));
        }
        for (const auto& v : j.value("resources", J::array())) {
            ResourceBinding r; r.extra = v; r.name = v.at("name"); r.slot = small(v, "slot");
            r.kind = v.value("kind", "texture2d"); r.path = v.value("path", ""); r.srgb = v.value("srgb", true);
            r.filter = v.value("filter", "linear"); r.address = v.value("address", "wrap"); d.resources.push_back(std::move(r));
        }
        if (!validateTechsetDocument(d, error)) return false;
        out = std::move(d); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool writeTechsetDocument(const TechsetDocument& d, std::string& text, std::string& error) {
    if (!validateTechsetDocument(d, error)) return false;
    try {
        J j = d.extra;
        j["format"] = "iw8r-techset"; j["version"] = TechsetDocument::kVersion; j["name"] = d.name;
        j["profile"] = d.profile; j["sourceZone"] = d.sourceZone; j["layout"] = d.layout; j["provenance"] = d.provenance;
        j["techniques"] = J::array(); j["parameters"] = J::array(); j["resources"] = J::array();
        for (const auto& t : d.techniques) {
            J v = t.extra; v["id"] = t.id; v["name"] = t.name; v["slot"] = t.slot; v["layout"] = t.layout; v["stages"] = J::array();
            for (const auto& s : t.stages) v["stages"].push_back(stageJson(s));
            j["techniques"].push_back(std::move(v));
        }
        for (const auto& p : d.parameters) {
            J v = p.extra; v["name"] = p.name; v["slot"] = p.slot; v["value"] = p.value; j["parameters"].push_back(std::move(v));
        }
        for (const auto& r : d.resources) {
            J v = r.extra; v["name"] = r.name; v["kind"] = r.kind; v["path"] = r.path; v["slot"] = r.slot;
            v["srgb"] = r.srgb; v["filter"] = r.filter; v["address"] = r.address; j["resources"].push_back(std::move(v));
        }
        text = j.dump(2) + '\n'; return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool importTechsetDocument(const std::string& text, TechsetDocument& out, std::string& error) {
    try {
        const J j = J::parse(text);
        const auto& asset = j.at("asset");
        if (asset.at("type") != "MaterialTechniqueSet") throw std::runtime_error("expected MaterialTechniqueSet");
        TechsetDocument d; d.provenance = j; d.layout = asset.at("fields");
        d.name = wrappedString(d.layout, "name");
        for (const auto& slot : d.layout.at("maskedTechniques").at("values")) {
            for (const auto& body : slot.at("values")) {
                TechniqueDefinition t; t.layout = body; t.name = wrappedString(body, "name");
                const auto& ty = body.at("type");
                const auto n = ty.is_object() ? integer(ty.at("value")) : integer(ty);
                if (n > 255) throw std::runtime_error("technique slot exceeds 255");
                t.slot = static_cast<uint32_t>(n); t.id = "technique_" + std::to_string(n);
                for (auto s : {ShaderStage::Vertex, ShaderStage::Pixel, ShaderStage::Hull, ShaderStage::Domain}) {
                    const std::string field = std::string(shaderStageName(s)) + "Shader";
                    if (!body.contains(field) || body.at(field).is_null()) continue;
                    ShaderStageDocument stage; stage.stage = s; stage.mode = "imported"; stage.entry.clear();
                    stage.reference = body.at(field).value("name", ""); t.stages.push_back(std::move(stage));
                }
                d.techniques.push_back(std::move(t));
            }
        }
        if (!validateTechsetDocument(d, error)) return false;
        out = std::move(d); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
std::string defaultPreviewSource(ShaderStage stage) {
    if (stage == ShaderStage::Vertex) return "// Position, normal and tangent are in world space.\nvoid iw8rPreviewVertex(inout IW8RPreviewVertex v)\n{\n    // v.position.z += sin(v.position.x * 0.05 + iw8rTime) * 4.0;\n}\n";
    if (stage == ShaderStage::Pixel) return "// Linear colour and coverage; resources and parameters are declared by the workspace.\nfloat4 iw8rPreviewPixel(IW8RPreviewSurface s)\n{\n    return float4(s.albedo * (s.ambient + s.sunColor * saturate(dot(s.normal, s.sunDir)) * s.sunShadow), s.alpha);\n}\n";
    return {};
}
TechsetDocument newTechsetDocument() {
    TechsetDocument d; TechniqueDefinition t; t.id = "technique_34"; t.name = "Lit surface";
    t.layout = {{"type", {{"value", 34}, {"name", "TECHNIQUE_LIT_FORWARDPLUS_BITMASK"}}},
                {"previewState", {{"blend", "opaque"}, {"cull", "back"}, {"depthTest", true}, {"depthWrite", true}}}};
    for (auto stage : {ShaderStage::Vertex, ShaderStage::Pixel}) {
        ShaderStageDocument s; s.stage = stage; s.entry = stage == ShaderStage::Vertex ? "iw8rPreviewVertex" : "iw8rPreviewPixel";
        s.source = defaultPreviewSource(stage); t.stages.push_back(std::move(s));
    }
    d.techniques.push_back(std::move(t)); rebuildTechsetMask(d); return d;
}
TechsetJson shaderGraphJson(const ShaderGraph& graph) { return graphJson(graph); }
bool readShaderGraph(const TechsetJson& j, ShaderGraph& graph, std::string& error) {
    try { graph = graphRead(j); error.clear(); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
}
} // namespace iw8r
