#include "custom_shader.h"

#include "face_edges.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace iw8r {

namespace {

// Comments out of the way before the checks (a resource named in a comment is fine).
std::string withoutComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            if (i < s.size()) out += '\n';
        } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

}  // namespace

bool checkCustomShaderCode(const std::string& code, std::string* err) {
    const std::string c = withoutComments(code);
    if (!std::regex_search(c, std::regex(R"(float3\s+iw8rSurface\s*\(\s*IW8RSurface\s+\w+\s*\))"))) {
        if (err) *err = "the shader must define float3 iw8rSurface(IW8RSurface s)";
        return false;
    }
    static const std::regex resources(R"(\b(Texture\w*|RWTexture\w*|SamplerState|SamplerComparisonState|cbuffer|tbuffer|StructuredBuffer|RWStructuredBuffer|ByteAddressBuffer|RWByteAddressBuffer)\b|\bregister\s*\()");
    std::smatch m;
    if (std::regex_search(c, m, resources)) {
        if (err) *err = "the shader may not declare resources or registers ('" + m.str() + "'): the game and the viewport bind their own";
        return false;
    }
    if (c.find("#include") != std::string::npos) {
        if (err) *err = "the shader may not #include files (put the helpers in the same file)";
        return false;
    }
    return true;
}

bool readCustomShader(const std::string& path, CustomShader& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string e;
    if (!checkCustomShaderCode(ss.str(), &e)) {
        if (err) *err = path + ": " + e;
        return false;
    }
    out.path = path;
    out.code = ss.str();
    std::string file = path;
    if (const size_t slash = file.find_last_of("/\\"); slash != std::string::npos) file = file.substr(slash + 1);
    const std::string suffix = ".iw8shader.hlsl";
    out.name = file.size() > suffix.size() && file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0
                   ? file.substr(0, file.size() - suffix.size())
                   : file;
    return true;
}

std::string customShaderViewportSource(const std::string& baseSource, const std::string& insertBefore, const CustomShader& shader) {
    const size_t at = baseSource.find(insertBefore);
    if (at == std::string::npos) return {};
    std::string out = "#define IW8R_CUSTOM 1\n";
    out += baseSource.substr(0, at);
    out += customShaderPreamble();
    out += "#line 1 \"" + shader.name + ".iw8shader.hlsl\"\n";
    out += shader.code;
    out += "\n#line 1 \"viewport\"\n";
    out += baseSource.substr(at);
    return out;
}

bool findCustomShader(const std::string& name, const std::vector<std::string>& folders, CustomShader& out, std::string* err) {
    if (name.empty()) {
        if (err) *err = "no shader name";
        return false;
    }
    const bool hasSuffix = name.size() > 5 && name.compare(name.size() - 5, 5, ".hlsl") == 0;
    const std::string file = hasSuffix ? name : name + ".iw8shader.hlsl";
    for (const std::string& f : folders) {
        if (f.empty()) continue;
        const std::string p = f + "/" + file;
        std::ifstream probe(p, std::ios::binary);
        if (!probe) continue;
        probe.close();
        return readCustomShader(p, out, err);
    }
    if (err) {
        *err = "shader '" + file + "' not found in";
        for (const std::string& f : folders)
            if (!f.empty()) *err += " " + f;
    }
    return false;
}

bool customShaderUsesEdges(const std::string& code) {
    static const std::regex re(R"(\b(edgePixels|edgeUnits|edgeNormal)\b)");
    return std::regex_search(withoutComments(code), re);
}

std::string customShaderPreamble() {
    std::ostringstream o;
    o << kIw8rSurfaceStruct << "#define IW8R_EDGE_RANGE " << kEdgeRange << "\n" << kIw8rEdgeHelper;
    return o.str();
}

std::string customShaderBuildSource(const CustomShader& shader, bool edges) {
    return std::string(edges ? "#define IW8R_EDGES 1\n" : "") + customShaderPreamble() + "#line 1 \"" + shader.name +
           ".iw8shader.hlsl\"\n" + shader.code;
}

}  // namespace iw8r
