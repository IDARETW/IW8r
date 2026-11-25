#include "shader_graph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace iw8r {
namespace {
using J = TechsetJson;
using P = ShaderPort;
bool identifier(const std::string &s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    for (unsigned char c : s)
        if (!std::isalnum(c) && c != '_')
            return false;
    return true;
}
int width(const std::string &type) {
    if (type == "float")
        return 1;
    if (type == "float2")
        return 2;
    if (type == "float3")
        return 3;
    if (type == "float4")
        return 4;
    return 0;
}
std::string number(double x) {
    if (!std::isfinite(x))
        throw std::runtime_error("a constant is not finite");
    std::ostringstream s;
    s << std::setprecision(9) << x;
    std::string v = s.str();
    if (v.find_first_of(".eE") == std::string::npos)
        v += ".0";
    return v;
}
struct Expr {
    std::string code, type;
};
std::string convert(const Expr &e, const std::string &type) {
    if (e.type == type || type == "any")
        return e.code;
    if (e.type == "float" && width(type) > 1)
        return "(" + e.code + ")." + std::string(width(type), 'x');
    throw std::runtime_error("cannot connect " + e.type + " to " + type + "; use Split or Combine explicitly");
}
std::string inputField(const std::string &k, ShaderStage stage) {
    const std::string s = stage == ShaderStage::Vertex ? "v." : "s.";
    if (k == "uv")
        return s + "uv";
    if (k == "position" || k == "normal" || k == "tangent")
        return s + k;
    if (k == "color")
        return s + "color";
    if (k == "view")
        return "s.view";
    if (k == "albedo")
        return "s.albedo";
    if (k == "alpha")
        return "s.alpha";
    if (k == "sun_direction")
        return "s.sunDir";
    if (k == "sun_color")
        return "s.sunColor";
    if (k == "ambient")
        return "s.ambient";
    if (k == "screen_uv")
        return "s.pixel / max(iw8rViewport.xy, 1.0)";
    if (k == "time")
        return "iw8rTime";
    return {};
}
const char *helpers = R"(
float iw8rHash(float3 p) { p = frac(p * 0.1031); p += dot(p, p.yzx + 33.33); return frac((p.x + p.y) * p.z); }
float iw8rNoise(float3 p) {
    float3 i=floor(p), f=frac(p); f=f*f*(3-2*f);
    return lerp(lerp(lerp(iw8rHash(i),iw8rHash(i+float3(1,0,0)),f.x),lerp(iw8rHash(i+float3(0,1,0)),iw8rHash(i+float3(1,1,0)),f.x),f.y),
                lerp(lerp(iw8rHash(i+float3(0,0,1)),iw8rHash(i+float3(1,0,1)),f.x),lerp(iw8rHash(i+float3(0,1,1)),iw8rHash(i+1),f.x),f.y),f.z);
}
float iw8rFbm(float3 p) { float n=0,a=0.5; [unroll] for(int i=0;i<5;++i) { n+=a*iw8rNoise(p); p=p*2.03+17.1; a*=0.5; } return n; }
float iw8rVoronoi(float3 p) {
    float3 i=floor(p), f=frac(p); float d=10;
    [unroll] for(int z=-1;z<=1;++z) [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        float3 o=float3(x,y,z), q=i+o; float3 v=o+float3(iw8rHash(q),iw8rHash(q+19),iw8rHash(q+71))-f; d=min(d,dot(v,v));
    } return sqrt(d);
}
float2 iw8rRotateUV(float2 uv,float angle) { float s=sin(angle), c=cos(angle); uv-=0.5; return float2(c*uv.x-s*uv.y,s*uv.x+c*uv.y)+0.5; }
float3 iw8rNormalBlend(float3 a,float3 b) { return normalize(float3(a.xy+b.xy,a.z*b.z)); }
float3 iw8rLit(float3 albedo,float3 n,float3 view,float3 sunDir,float3 sunColor,float3 ambient,float shadow,float roughness,float metallic) {
    n=normalize(n); float nl=saturate(dot(n,sunDir)); float3 h=normalize(view+sunDir);
    float power=max(2.0/pow(max(roughness,0.04),4.0)-2.0,1.0);
    float3 f0=lerp(0.04.xxx,albedo,saturate(metallic));
    return albedo*(1-saturate(metallic))*(ambient+sunColor*nl*shadow)+f0*pow(saturate(dot(n,h)),power)*nl*sunColor*shadow;
}
)";

struct Generator {
    ShaderStage stage;
    const std::vector<ShaderParameter> &parameters;
    const std::vector<ResourceBinding> &resources;
    ShaderGraphResult result;
    std::string lines;
    size_t serial = 0;
    std::vector<std::string> calls;
    const std::map<std::string, ShaderGraph> *definitions = nullptr;
    struct Scope {
        const ShaderGraph &graph;
        std::map<std::string, const ShaderNode *> nodes;
        std::map<std::pair<std::string, std::string>, const ShaderLink *> links;
        std::map<std::string, Expr> values, arguments;
        std::set<std::string> active;
    };
    Scope scope(const ShaderGraph &g) {
        Scope s{g};
        for (const auto &n : g.nodes) {
            if (!identifier(n.id) || !s.nodes.emplace(n.id, &n).second)
                throw std::runtime_error("invalid/duplicate node ID: " + n.id);
            if (!shaderNodeSpec(n.kind))
                throw std::runtime_error("unknown node kind: " + n.kind);
        }
        for (const auto &e : g.links) {
            if (!s.nodes.count(e.from) || !s.nodes.count(e.to))
                throw std::runtime_error("link references a missing node");
            if (e.output != "out")
                throw std::runtime_error("unknown output port: " + e.output);
            const auto *dest = s.nodes.at(e.to);
            const auto *spec = shaderNodeSpec(dest->kind);
            bool known = dest->kind == "custom" || dest->kind == "subgraph";
            for (const auto &p : spec->inputs)
                if (p.name == e.input)
                    known = true;
            if (!known)
                throw std::runtime_error("unknown input port: " + e.input);
            if (!s.links.emplace(std::make_pair(e.to, e.input), &e).second)
                throw std::runtime_error("multiple links to one input");
        }
        // Validate disconnected cycles as well as reachable ones.
        std::set<std::string> visiting, done;
        std::function<void(const std::string &)> visit = [&](const std::string &id) {
            if (done.count(id))
                return;
            if (!visiting.insert(id).second)
                throw std::runtime_error("graph cycle at " + id);
            for (const auto &[port, e] : s.links)
                if (port.first == id)
                    visit(e->from);
            visiting.erase(id);
            done.insert(id);
        };
        for (const auto &[id, n] : s.nodes)
            visit(id);
        return s;
    }
    Expr input(Scope &s, const ShaderNode &n, const std::string &name, const std::string &type,
               const std::string &fallback) {
        auto it = s.links.find({n.id, name});
        if (it != s.links.end()) {
            Expr v = emit(s, it->second->from);
            return {convert(v, type), type == "any" ? v.type : type};
        }
        if (n.properties.contains(name)) {
            const auto &v = n.properties.at(name);
            if (v.is_number())
                return {convert({number(v.get<double>()), "float"}, type), type == "any" ? "float" : type};
            if (v.is_array() && v.size() >= 2 && v.size() <= 4) {
                std::string ty = "float" + std::to_string(v.size()), code = ty + "(";
                for (size_t i = 0; i < v.size(); ++i)
                    code += (i ? "," : "") + number(v[i].get<double>());
                code += ")";
                return {convert({code, ty}, type), type == "any" ? ty : type};
            }
            if (v.is_boolean() && type == "bool")
                return {v.get<bool>() ? "true" : "false", "bool"};
            throw std::runtime_error("invalid input value: " + name);
        }
        return {fallback, type == "any" ? "float" : type};
    }
    const ResourceBinding &texture(const ShaderNode &n) {
        const auto name = n.properties.value("resource", "");
        for (const auto &r : resources)
            if (r.name == name)
                return r;
        throw std::runtime_error("unknown texture resource: " + name);
    }
    Expr emit(Scope &s, const std::string &id) {
        if (s.values.count(id))
            return s.values.at(id);
        const auto &n = *s.nodes.at(id);
        try {
            if (!s.active.insert(id).second)
                throw std::runtime_error("graph cycle");
            const auto &spec = *shaderNodeSpec(n.kind);
            if (spec.pixelOnly && stage != ShaderStage::Pixel)
                throw std::runtime_error("node is only available in pixel shaders");
            const auto in = [&](const char *key, const char *ty = "float", const char *fallback = "0.0") {
                return input(s, n, key, ty, fallback);
            };
            Expr e;
            e.type = spec.output;
            const auto field = inputField(n.kind, stage);
            if (!field.empty())
                e.code = field;
            else if (n.kind == "constant") {
                e = in("value", "any", "0.0");
            } else if (n.kind == "parameter") {
                const auto name = n.properties.value("parameter", "");
                const ShaderParameter *p = nullptr;
                for (const auto &v : parameters)
                    if (v.name == name)
                        p = &v;
                if (!p)
                    throw std::runtime_error("unknown parameter: " + name);
                e.type = n.properties.value("type", "float4");
                if (!width(e.type))
                    throw std::runtime_error("invalid parameter type");
                e.code = "iw8rParameters[" + std::to_string(p->slot) + "]";
                if (width(e.type) < 4)
                    e.code += "." + std::string("xyzw").substr(0, width(e.type));
            } else if (n.kind == "add" || n.kind == "subtract" || n.kind == "multiply" || n.kind == "divide" ||
                       n.kind == "min" || n.kind == "max" || n.kind == "pow") {
                auto a = in("a", "any"), b = in("b", "any", "1.0");
                e.type = width(a.type) >= width(b.type) ? a.type : b.type;
                const auto x = convert(a, e.type), y = convert(b, e.type);
                if (n.kind == "min" || n.kind == "max" || n.kind == "pow")
                    e.code = n.kind + "(" + x + "," + y + ")";
                else {
                    const auto op = n.kind == "add"        ? "+"
                                    : n.kind == "subtract" ? "-"
                                    : n.kind == "multiply" ? "*"
                                                           : "/";
                    e.code = "(" + x + op + y + ")";
                }
            } else if (n.kind == "saturate" || n.kind == "abs" || n.kind == "sin" || n.kind == "cos" ||
                       n.kind == "floor" || n.kind == "frac" || n.kind == "normalize" || n.kind == "length" ||
                       n.kind == "ddx" || n.kind == "ddy") {
                auto a = in("value", "any");
                e.type = n.kind == "length" ? "float" : a.type;
                e.code = n.kind + "(" + a.code + ")";
            } else if (n.kind == "lerp" || n.kind == "clamp" || n.kind == "smoothstep" || n.kind == "remap") {
                auto a = in("a", "any"), b = in("b", "any", "1.0"), v = in("value", "any", "0.5");
                e.type = width(a.type) >= width(b.type) ? a.type : b.type;
                if (width(v.type) > width(e.type))
                    e.type = v.type;
                const auto x = convert(a, e.type), y = convert(b, e.type), z = convert(v, e.type);
                e.code = n.kind == "remap"   ? "((" + z + "-" + x + ")/(" + y + "-" + x + "))"
                         : n.kind == "clamp" ? "clamp(" + z + "," + x + "," + y + ")"
                                             : n.kind + "(" + x + "," + y + "," + z + ")";
            } else if (n.kind == "dot" || n.kind == "cross") {
                e.code = n.kind + "(" + in("a", "float3").code + "," + in("b", "float3", "float3(0,0,1)").code + ")";
            } else if (n.kind == "compare") {
                const auto op = n.properties.value("operator", ">");
                if (op != ">" && op != "<" && op != ">=" && op != "<=" && op != "==" && op != "!=")
                    throw std::runtime_error("invalid comparison");
                e.code = "(" + in("a").code + op + in("b").code + ")";
            } else if (n.kind == "switch") {
                auto a = in("a", "any"), b = in("b", "any", "1.0");
                e.type = width(a.type) >= width(b.type) ? a.type : b.type;
                e.code = "(" + in("condition", "bool", "false").code + "?" + convert(a, e.type) + ":" +
                         convert(b, e.type) + ")";
            } else if (n.kind == "split") {
                auto a = in("value", "any");
                const auto component = n.properties.value("component", "x");
                if (component.size() != 1 || std::string("xyzw").find(component) >= static_cast<size_t>(width(a.type)))
                    throw std::runtime_error("component is outside the input vector");
                e.code = "(" + a.code + ")." + component;
            } else if (n.kind == "swizzle") {
                const auto a = in("value", "any");
                const auto channels = n.properties.value("channels", "x");
                if (channels.empty() || channels.size() > 4)
                    throw std::runtime_error("Swizzle needs one to four channels");
                for (char c : channels) {
                    const auto index = std::string("xyzw").find(c);
                    if (index >= size_t(width(a.type)))
                        throw std::runtime_error("swizzle channel is outside the input vector");
                }
                e.type = channels.size() == 1 ? "float" : "float" + std::to_string(channels.size());
                e.code = "(" + a.code + ")." + channels;
            } else if (n.kind == "combine") {
                e.type = n.properties.value("type", "float3");
                if (width(e.type) < 2)
                    throw std::runtime_error("Combine needs float2, float3 or float4");
                e.code = e.type + "(";
                for (int i = 0; i < width(e.type); ++i) {
                    const std::string key(1, "xyzw"[i]);
                    e.code += (i ? "," : "") + in(key.c_str()).code;
                }
                e.code += ")";
            } else if (n.kind == "uv_transform") {
                e.code = "(" + in("uv", "float2", stage == ShaderStage::Pixel ? "s.uv" : "v.uv").code + "*" +
                         in("scale", "float2", "float2(1,1)").code + "+" + in("offset", "float2", "float2(0,0)").code +
                         ")";
            } else if (n.kind == "panner") {
                e.code = "(" + in("uv", "float2", stage == ShaderStage::Pixel ? "s.uv" : "v.uv").code + "+" +
                         in("speed", "float2", "float2(0.1,0)").code + "*iw8rTime)";
            } else if (n.kind == "rotate_uv") {
                e.code = "iw8rRotateUV(" + in("uv", "float2", stage == ShaderStage::Pixel ? "s.uv" : "v.uv").code +
                         "," + in("angle").code + ")";
            } else if (n.kind == "texture" || n.kind == "texture_cube" || n.kind == "triplanar") {
                const auto &r = texture(n);
                const std::string tex = "iw8rTexture" + std::to_string(r.slot),
                                  sam = "iw8rSampler" + std::to_string(r.slot);
                const bool cube = n.kind == "texture_cube";
                if ((r.kind == "texturecube") != cube)
                    throw std::runtime_error("texture dimensionality does not match the node");
                if (n.kind == "triplanar") {
                    const auto pos =
                        in("position", "float3", stage == ShaderStage::Pixel ? "s.position" : "v.position").code;
                    const auto norm =
                        in("normal", "float3", stage == ShaderStage::Pixel ? "s.normal" : "v.normal").code;
                    const auto sc = in("scale", "float", "0.01").code;
                    const std::string w = "abs(normalize(" + norm + "))";
                    e.code = "(" + tex + ".SampleLevel(" + sam + ",(" + pos + ").yz*" + sc + ",0)*(" + w + ").x+" +
                             tex + ".SampleLevel(" + sam + ",(" + pos + ").xz*" + sc + ",0)*(" + w + ").y+" + tex +
                             ".SampleLevel(" + sam + ",(" + pos + ").xy*" + sc + ",0)*(" + w + ").z)/max(dot(" + w +
                             ",float3(1,1,1)),1e-5)";
                } else {
                    const auto uv = cube ? in("direction", "float3", "float3(0,0,1)").code
                                         : in("uv", "float2", stage == ShaderStage::Pixel ? "s.uv" : "v.uv").code;
                    e.code = tex + ".SampleLevel(" + sam + "," + uv + "," + in("lod").code + ")";
                }
            } else if (n.kind == "normal_unpack")
                e.code = "normalize(" + in("value", "float3", "float3(0.5,0.5,1)").code + "*2-1)";
            else if (n.kind == "normal_blend")
                e.code = "iw8rNormalBlend(" + in("a", "float3", "float3(0,0,1)").code + "," +
                         in("b", "float3", "float3(0,0,1)").code + ")";
            else if (n.kind == "normal_to_world") {
                const auto n = in("value", "float3", "float3(0,0,1)").code;
                e.code =
                    "normalize(s.tangent*(" + n + ").x+cross(s.normal,s.tangent)*(" + n + ").y+s.normal*(" + n + ").z)";
            } else if (n.kind == "fresnel")
                e.code = "pow(1-saturate(dot(normalize(" + in("normal", "float3", "s.normal").code +
                         "),normalize(s.view)))," + in("power", "float", "5.0").code + ")";
            else if (n.kind == "noise" || n.kind == "fbm" || n.kind == "voronoi") {
                e.code = std::string(n.kind == "noise" ? "iw8rNoise"
                                     : n.kind == "fbm" ? "iw8rFbm"
                                                       : "iw8rVoronoi") +
                         "(" +
                         in("position", "float3", stage == ShaderStage::Pixel ? "s.position" : "v.position").code +
                         "*" + in("scale", "float", "0.01").code + ")";
            } else if (n.kind == "checker")
                e.code = "fmod(abs(floor(" + in("uv", "float2", stage == ShaderStage::Pixel ? "s.uv" : "v.uv").code +
                         ".x*" + in("scale", "float", "8.0").code + ")+floor(" +
                         in("uv", "float2", stage == ShaderStage::Pixel ? "s.uv" : "v.uv").code + ".y*" +
                         in("scale", "float", "8.0").code + ")),2)";
            else if (n.kind == "scene_depth")
                e.code = "iw8rPreviewSceneDepth(" + in("pixel", "float2", "s.pixel").code + ")";
            else if (n.kind == "reroute")
                e = in("value", "any");
            else if (n.kind == "input") {
                const auto name = n.properties.value("name", n.id);
                auto it = s.arguments.find(name);
                if (it == s.arguments.end())
                    throw std::runtime_error("subgraph input is not supplied: " + name);
                e = it->second;
            } else if (n.kind == "subgraph") {
                const auto name = n.properties.value("name", "");
                if (!definitions || !definitions->count(name))
                    throw std::runtime_error("unknown subgraph: " + name);
                if (std::find(calls.begin(), calls.end(), name) != calls.end() || calls.size() >= 32)
                    throw std::runtime_error("recursive subgraph: " + name);
                calls.push_back(name);
                Scope child = scope(definitions->at(name));
                for (const auto &cn : child.graph.nodes)
                    if (cn.kind == "input") {
                        const auto key = cn.properties.value("name", cn.id), ty = cn.properties.value("type", "float");
                        child.arguments[key] = input(s, n, key, ty, "0.0");
                    }
                const ShaderNode *output = nullptr;
                for (const auto &cn : child.graph.nodes)
                    if (cn.kind == "output") {
                        if (output)
                            throw std::runtime_error("subgraph has multiple outputs");
                        output = &cn;
                    }
                if (!output)
                    throw std::runtime_error("subgraph has no output");
                e = input(child, *output, "value", "any", "0.0");
                calls.pop_back();
            } else if (n.kind == "custom") {
                e.type = n.properties.value("type", "float3");
                if (!width(e.type) && e.type != "bool")
                    throw std::runtime_error("invalid custom output type");
                e.code = n.properties.value("expression", "float3(1,0,1)");
                const auto inputs = n.properties.value("inputs", J::object());
                // Expression nodes may use intrinsics and declared inputs. Full helper functions belong in HLSL mode.
                if (e.code.find_first_of(";{}#") != std::string::npos)
                    throw std::runtime_error("custom node expects one HLSL expression");
                for (auto it = inputs.begin(); it != inputs.end(); ++it) {
                    if (!identifier(it.key()))
                        throw std::runtime_error("invalid custom input name");
                    const auto value = input(s, n, it.key(), it.value().get<std::string>(), "0.0");
                    const std::string token = "$" + it.key();
                    size_t p = 0;
                    while ((p = e.code.find(token, p)) != std::string::npos) {
                        e.code.replace(p, token.size(), "(" + value.code + ")");
                        p += value.code.size() + 2;
                    }
                }
            } else
                throw std::runtime_error("this node is an output or annotation, not a value");
            if (e.type == "any")
                throw std::runtime_error("unresolved port type");
            const auto variable = "iw8rn_" + std::to_string(serial++);
            const auto file = "node_" + n.id + ".hlsl";
            result.nodeFiles[file] = n.id;
            lines += "#line 1 \"" + file + "\"\n    " + e.type + " " + variable + " = " + e.code + ";\n";
            s.active.erase(id);
            return s.values[id] = {variable, e.type};
        } catch (const std::exception &e) {
            if (result.diagnostics.empty())
                result.diagnostics.push_back({e.what(), n.id, "", 0, 0, false});
            throw;
        }
    }
};
} // namespace

const std::vector<ShaderNodeSpec> &shaderNodeLibrary() {
    static const std::vector<ShaderNodeSpec> specs = [] {
        std::vector<ShaderNodeSpec> v;
        const auto add = [&](const char *k, const char *title, const char *cat, std::vector<P> in = {},
                             const char *out = "float",
                             bool pixel = false) { v.push_back({k, title, cat, std::move(in), out, pixel}); };
        add("constant", "Constant / Color", "Inputs", {{"value", "any", "0"}}, "any");
        add("parameter", "Exposed parameter", "Inputs", {}, "any");
        for (auto k : {"uv", "position", "normal", "tangent", "color", "time"})
            add(k, k, "Inputs", {},
                std::string(k) == "uv"      ? "float2"
                : std::string(k) == "color" ? "float4"
                : std::string(k) == "time"  ? "float"
                                            : "float3");
        for (auto k : {"view", "albedo", "alpha", "sun_direction", "sun_color", "ambient", "screen_uv"})
            add(k, k, "Surface inputs", {},
                std::string(k) == "alpha"       ? "float"
                : std::string(k) == "screen_uv" ? "float2"
                                                : "float3",
                true);
        for (auto k : {"add", "subtract", "multiply", "divide", "min", "max", "pow"})
            add(k, k, "Math", {{"a", "any", "0"}, {"b", "any", "1"}}, "any");
        for (auto k : {"saturate", "abs", "sin", "cos", "floor", "frac", "normalize", "length"})
            add(k, k, "Math", {{"value", "any", "0"}}, "any");
        for (auto k : {"ddx", "ddy"})
            add(k, k, "Math", {{"value", "any", "0"}}, "any", true);
        for (auto k : {"lerp", "clamp", "smoothstep", "remap"})
            add(k, k, "Math", {{"a", "any", "0"}, {"b", "any", "1"}, {"value", "any", "0.5"}}, "any");
        add("dot", "Dot", "Vectors", {{"a", "float3", "0"}, {"b", "float3", "0"}});
        add("cross", "Cross", "Vectors", {{"a", "float3", "0"}, {"b", "float3", "0"}}, "float3");
        add("split", "Split component", "Vectors", {{"value", "any", "0"}});
        add("swizzle", "Swizzle / channel mask", "Vectors", {{"value", "any", "0"}}, "any");
        add("combine", "Combine components", "Vectors", {{"x"}, {"y"}, {"z"}, {"w"}}, "any");
        add("compare", "Compare", "Logic", {{"a"}, {"b"}}, "bool");
        add("switch", "Switch", "Logic", {{"condition", "bool", "false"}, {"a", "any", "0"}, {"b", "any", "1"}}, "any");
        add("uv_transform", "UV scale / offset", "UV", {{"uv", "float2"}, {"scale", "float2"}, {"offset", "float2"}},
            "float2");
        add("panner", "UV panner", "UV", {{"uv", "float2"}, {"speed", "float2"}}, "float2");
        add("rotate_uv", "UV rotate", "UV", {{"uv", "float2"}, {"angle"}}, "float2");
        add("texture", "Texture 2D", "Textures", {{"uv", "float2"}, {"lod"}}, "float4");
        add("texture_cube", "Texture cube", "Textures", {{"direction", "float3"}, {"lod"}}, "float4");
        add("triplanar", "Triplanar texture", "Textures", {{"position", "float3"}, {"normal", "float3"}, {"scale"}},
            "float4");
        add("normal_unpack", "Unpack normal", "Normals", {{"value", "float3"}}, "float3");
        add("normal_blend", "Blend normals", "Normals", {{"a", "float3"}, {"b", "float3"}}, "float3");
        add("normal_to_world", "Tangent normal to world", "Normals", {{"value", "float3"}}, "float3", true);
        add("fresnel", "Fresnel", "Lighting", {{"normal", "float3"}, {"power"}}, "float", true);
        for (auto k : {"noise", "fbm", "voronoi"})
            add(k, k, "Procedural", {{"position", "float3"}, {"scale"}});
        add("checker", "Checker", "Procedural", {{"uv", "float2"}, {"scale"}});
        add("scene_depth", "Scene depth", "Surface inputs", {{"pixel", "float2"}}, "float", true);
        add("reroute", "Reroute", "Utility", {{"value", "any"}}, "any");
        add("custom", "Custom HLSL expression", "Utility", {}, "any");
        add("subgraph", "Reusable subgraph", "Utility", {}, "any");
        add("input", "Subgraph input", "Utility", {}, "any");
        add("output", "Subgraph output", "Utility", {{"value", "any"}}, "any");
        add("comment", "Comment", "Utility", {}, "none");
        add("surface", "Lit / unlit surface", "Output",
            {{"color", "float3"},
             {"normal", "float3"},
             {"alpha"},
             {"cutoff"},
             {"emission", "float3"},
             {"roughness"},
             {"metallic"}},
            "float4", true);
        add("vertex_output", "Vertex displacement", "Output", {{"offset", "float3"}, {"normal", "float3"}}, "none");
        return v;
    }();
    return specs;
}
const ShaderNodeSpec *shaderNodeSpec(const std::string &kind) {
    for (const auto &s : shaderNodeLibrary())
        if (s.kind == kind)
            return &s;
    return nullptr;
}
ShaderGraph newShaderGraph(ShaderStage stage) {
    ShaderGraph g;
    ShaderNode out;
    out.id = "output_1";
    out.kind = stage == ShaderStage::Vertex ? "vertex_output" : "surface";
    out.label = stage == ShaderStage::Vertex ? "Vertex output" : "Surface output";
    out.x = 360;
    out.y = 100;
    g.nodes.push_back(out);
    return g;
}
ShaderGraphResult compileShaderGraph(const ShaderGraph &sourceGraph, ShaderStage stage,
                                     const std::vector<ShaderParameter> &parameters,
                                     const std::vector<ResourceBinding> &resources, const std::string &previewNode) {
    Generator g{stage, parameters, resources};
    try {
        // Resolve nested definitions lexically, then compile one deterministic definition table.
        // A subgraph edited in its own canvas may contain another reusable subgraph with the same local name.
        ShaderGraph graph;
        std::map<std::string, ShaderGraph> definitions;
        std::function<ShaderGraph(const ShaderGraph &, std::map<std::string, std::string>, const std::string &,
                                  unsigned)>
            flatten;
        flatten = [&](const ShaderGraph &input, std::map<std::string, std::string> names, const std::string &prefix,
                      unsigned depth) {
            if (depth > 32)
                throw std::runtime_error("subgraph nesting exceeds 32 levels");
            for (const auto &[name, sub] : input.subgraphs)
                names[name] = prefix + std::to_string(name.size()) + ":" + name;
            ShaderGraph output = input;
            output.subgraphs.clear();
            for (auto &node : output.nodes)
                if (node.kind == "subgraph") {
                    const auto name = node.properties.value("name", "");
                    if (names.count(name))
                        node.properties["name"] = names.at(name);
                }
            for (const auto &[name, sub] : input.subgraphs)
                definitions.emplace(names.at(name), flatten(sub, names, names.at(name) + "::", depth + 1));
            return output;
        };
        graph = flatten(sourceGraph, {}, "", 0);
        graph.subgraphs = std::move(definitions);
        g.definitions = &graph.subgraphs;
        if (stage != ShaderStage::Pixel && stage != ShaderStage::Vertex)
            throw std::runtime_error("only vertex/pixel graphs can be previewed");
        auto s = g.scope(graph);
        std::set<std::string> visiting, checked;
        std::function<void(const std::string &)> checkSub = [&](const std::string &name) {
            if (checked.count(name))
                return;
            if (!graph.subgraphs.count(name))
                throw std::runtime_error("unknown subgraph: " + name);
            if (!visiting.insert(name).second)
                throw std::runtime_error("recursive subgraph: " + name);
            g.scope(graph.subgraphs.at(name));
            for (const auto &node : graph.subgraphs.at(name).nodes)
                if (node.kind == "subgraph")
                    checkSub(node.properties.value("name", ""));
            visiting.erase(name);
            checked.insert(name);
        };
        for (const auto &[name, sub] : graph.subgraphs)
            checkSub(name);
        std::string tail;
        if (!previewNode.empty()) {
            if (!s.nodes.count(previewNode))
                throw std::runtime_error("preview node was removed");
            const auto e = g.emit(s, previewNode);
            const auto code = e.type == "bool" ? "(" + e.code + "?1.0:0.0)" : e.code;
            if (stage == ShaderStage::Vertex)
                tail = "    v.position += " +
                       (width(e.type) == 3   ? code
                        : width(e.type) == 4 ? code + ".xyz"
                                             : "float3(0,0," + code + (width(e.type) == 2 ? ".x" : "") + ")") +
                       ";\n";
            else
                tail = "    return " +
                       (width(e.type) == 4   ? code
                        : width(e.type) == 3 ? "float4(" + code + ",1)"
                        : width(e.type) == 2 ? "float4(" + code + ",0,1)"
                                             : "float4((" + code + ").xxx,1)") +
                       ";\n";
        } else {
            const ShaderNode *out = nullptr;
            for (const auto &n : graph.nodes)
                if (n.kind == (stage == ShaderStage::Vertex ? "vertex_output" : "surface")) {
                    if (out)
                        throw std::runtime_error("multiple stage output nodes");
                    out = &n;
                }
            if (!out)
                throw std::runtime_error("graph has no stage output");
            const auto in = [&](const char *key, const char *type, const char *def) {
                return g.input(s, *out, key, type, def).code;
            };
            if (stage == ShaderStage::Vertex)
                tail = "    v.position += " + in("offset", "float3", "float3(0,0,0)") + ";\n    v.normal = normalize(" +
                       in("normal", "float3", "v.normal") + ");\n";
            else {
                const auto color = in("color", "float3", "s.albedo"), normal = in("normal", "float3", "s.normal"),
                           alpha = in("alpha", "float", "s.alpha");
                const auto emission = in("emission", "float3", "float3(0,0,0)"), cutoff = in("cutoff", "float", "0.0");
                const auto roughness = in("roughness", "float", "0.6"), metal = in("metallic", "float", "0.0");
                const auto unlit = out->properties.value("unlit", false);
                tail = "    s.albedo=" + color + ";s.normal=" + normal + ";s.alpha=" + alpha +
                       ";s.emission=" + emission + ";s.cutoff=" + cutoff + ";s.roughness=" + roughness +
                       ";s.metallic=" + metal + ";s.unlit=" + (unlit ? "1.0" : "0.0") +
                       ";\n    clip(s.alpha-s.cutoff);\n";
            }
        }
        g.result.source = std::string(helpers) +
                          (stage == ShaderStage::Vertex ? "void iw8rPreviewVertex(inout IW8RPreviewVertex v) {\n"
                                                        : "float4 iw8rPreviewPixel(IW8RPreviewSurface s) {\n") +
                          g.lines + tail + "}\n";
        if (stage == ShaderStage::Pixel && previewNode.empty())
            g.result.source =
                std::string(helpers) +
                "\n#define IW8R_PREVIEW_MATERIAL 1\nvoid iw8rPreviewMaterial(inout IW8RPreviewSurface s) {\n" +
                g.lines + tail +
                "}\nfloat4 iw8rPreviewPixel(IW8RPreviewSurface s) { clip(s.alpha-s.cutoff); return "
                "float4((s.unlit>0.5?s.albedo:s.litColor)+s.emission,s.alpha); }\n";
        g.result.ok = true;
    } catch (const std::exception &e) {
        if (g.result.diagnostics.empty())
            g.result.diagnostics.push_back({e.what()});
    }
    return g.result;
}
std::string previewShaderInterface(const std::vector<ResourceBinding> &resources) {
    std::string s = R"(
cbuffer IW8RPreviewParameters : register(b13) { float4 iw8rParameters[64]; float4 iw8rClock; float4 iw8rViewport; };
#define iw8rTime iw8rClock.x
struct IW8RPreviewVertex { float3 position; float3 normal; float3 tangent; float2 uv; float4 color; };
struct IW8RPreviewSurface {
    float3 albedo; float alpha; float3 normal; float3 tangent; float3 position; float2 uv; float4 color;
    float3 view; float3 sunDir; float3 sunColor; float sunShadow; float3 ambient; float2 pixel; float viewDepth; float3 litColor; float roughness; float metallic; float cutoff; float unlit; float3 emission;
};
float iw8rPreviewSceneDepth(float2 pixel);

)";
    for (const auto &r : resources)
        s += (r.kind == "texturecube" ? "TextureCube " : "Texture2D ") + std::string("iw8rTexture") +
             std::to_string(r.slot) + " : register(t" + std::to_string(96 + r.slot) + ");\nSamplerState iw8rSampler" +
             std::to_string(r.slot) + " : register(s" + std::to_string(8 + r.slot) + ");\n";
    return s;
}
} // namespace iw8r
