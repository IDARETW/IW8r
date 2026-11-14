import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# core: find a named shader in search folders
H = 'src/core/custom_shader.h'
patch(H, '''std::string customShaderViewportSource(const std::string& baseSource, const std::string& insertBefore, const CustomShader& shader);''',
      '''std::string customShaderViewportSource(const std::string& baseSource, const std::string& insertBefore, const CustomShader& shader);

// A map's surface shader by name (worldspawn "iw8r_surface_shader"): "<name>" or "<name>.iw8shader.hlsl" (or a path
// ending in .hlsl) looked for in each folder in order (the map's shaders folder, then the editor's data/shaders).
// The first file found is read; false with *err when none is found or it does not check.
bool findCustomShader(const std::string& name, const std::vector<std::string>& folders, CustomShader& out, std::string* err = nullptr);

// What the build hands the converter (world JSON "iw8r_surface_shader"): the IW8RSurface struct and the shader's code.
std::string customShaderBuildSource(const CustomShader& shader);''')
s = io.open(H, encoding='utf-8', newline='').read()
if '#include <vector>' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#include <string>', '#include <string>' + nl + '#include <vector>', 1)
    io.open(H, 'w', encoding='utf-8', newline='').write(s)
C = 'src/core/custom_shader.cpp'
patch(C, '''}  // namespace iw8r''', '''bool findCustomShader(const std::string& name, const std::vector<std::string>& folders, CustomShader& out, std::string* err) {
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

std::string customShaderBuildSource(const CustomShader& shader) {
    return std::string(kIw8rSurfaceStruct) + "#line 1 \\"" + shader.name + ".iw8shader.hlsl\\"\\n" + shader.code + "\\n";
}

}  // namespace iw8r''')

CH = 'src/core/compile_iw8.h'
patch(CH, '''    std::string mapDir;''', '''    std::string mapDir;
    // Where a worldspawn "iw8r_surface_shader" is looked for after <mapDir>/shaders (core custom_shader): the
    // editor's data/shaders. The shader goes to the converter in the world JSON, which lights the map with it.
    std::vector<std::string> shaderDirs;''')
CC = 'src/core/compile_iw8.cpp'
patch(CC, '''    const std::string decalsKey = decalCount ? ",\\"decal_volumes\\":[" + decalJson + "]" : std::string();''', '''    const std::string decalsKey = decalCount ? ",\\"decal_volumes\\":[" + decalJson + "]" : std::string();
    // The map's surface shader (worldspawn iw8r_surface_shader, core custom_shader): the converter compiles it into the
    // map's own surface shaders (needs the x64-zt converter with "iw8r_surface_shader"; plan M6).
    std::string surfaceShaderKey;
    if (const std::string* sname = worldspawn ? worldspawn->value("iw8r_surface_shader") : nullptr; sname && !sname->empty()) {
        std::vector<std::string> dirs;
        if (!opt.mapDir.empty()) dirs.push_back(opt.mapDir + "/shaders");
        dirs.insert(dirs.end(), opt.shaderDirs.begin(), opt.shaderDirs.end());
        CustomShader shader;
        std::string serr;
        if (findCustomShader(*sname, dirs, shader, &serr)) {
            surfaceShaderKey = ",\\"iw8r_surface_shader\\":" + quote(customShaderBuildSource(shader));
            warn.push_back("surface shader '" + shader.name + "' lights the map's own surfaces (custom shader; in-game look not verified)");
        } else {
            warn.push_back("surface shader not used: " + serr);
        }
    }''')
patch(CC, '''        ",\\"reflection_probes\\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey + decalsKey + lightmapKeys +''',
      '''        ",\\"reflection_probes\\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey + decalsKey + surfaceShaderKey + lightmapKeys +''')
s = io.open(CC, encoding='utf-8', newline='').read()
if '#include "custom_shader.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    i = s.index('#include "')
    s = s[:i] + '#include "custom_shader.h"' + nl + s[i:]
    io.open(CC, 'w', encoding='utf-8', newline='').write(s)

P = 'src/qtapp/compile_panel.cpp'
patch(P, '''    if (!ctx_->doc.path().empty()) opt.mapDir = QFileInfo(qs(ctx_->doc.path())).absolutePath().toStdString();''',
      '''    if (!ctx_->doc.path().empty()) opt.mapDir = QFileInfo(qs(ctx_->doc.path())).absolutePath().toStdString();
    {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("data/shaders")); ++up) d.cdUp();
        opt.shaderDirs.push_back(d.filePath("data/shaders").toStdString());
    }''')
print('ok')
