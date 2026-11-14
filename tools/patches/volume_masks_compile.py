import io

# Volumetric box masks in the build (research 127 part A): iw8r_volume keys mask .. mask4 become the box's "masks" in
# world.json, and each picture goes into the extraction as images/iw8r_vmask_<name>.dds. The packaging script takes the
# shipped pictures (data/volume_masks).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/core/compile_iw8.cpp'
patch(C, '''#include "mount_edges.h"''', '''#include "mount_edges.h"
#include "volume_mask.h"''')
patch(C, '''    std::string volumeJson;
    size_t volumeCount = 0;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "iw8r_volume") continue;''', '''    std::string volumeJson;
    size_t volumeCount = 0;
    // Mask pictures (keys mask .. mask4, core volume_mask): looked for in <map folder>/volume_masks, then beside the
    // editor's shader folders (data/volume_masks). Name in the build -> file.
    std::map<std::string, std::string> volumeMaskFiles;
    std::vector<std::string> volumeMaskDirs;
    volumeMaskDirs.push_back((fs::path(opt.mapDir.empty() ? "." : opt.mapDir) / kVolumeMaskFolder).string());
    for (const std::string& d : opt.shaderDirs) volumeMaskDirs.push_back((fs::path(d).parent_path() / kVolumeMaskFolder).string());
    size_t volumeMaskCount = 0;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "iw8r_volume") continue;''')
patch(C, '''                      ",\\"falloff\\":" + num(falloff) + ",\\"height_fade\\":" + num(heightFade) + ",\\"anisotropy\\":" + num(anisotropy) + "}";
        ++volumeCount;
    }
    if (volumeCount) warn.push_back(std::to_string(volumeCount) + " volumetric box(es) written (iw8r_volume; in-game look not verified)");''',
      '''                      ",\\"falloff\\":" + num(falloff) + ",\\"height_fade\\":" + num(heightFade) + ",\\"anisotropy\\":" + num(anisotropy);
        // Masks: each multiplies the box's fog by its picture's red channel (type 2, the only kind the game draws).
        std::vector<std::string> maskWarnings;
        std::string masksJson;
        for (const VolumeMask& m : volumeMasksOf(*pe, &maskWarnings)) {
            const std::string file = findVolumeMaskFile(m.image, volumeMaskDirs);
            if (file.empty()) {
                warn.push_back(where + ": mask picture '" + m.image + "' not found in the map's volume_masks folder or the editor's; mask left out");
                continue;
            }
            const std::string name = volumeMaskImageName(m.image);
            volumeMaskFiles[name] = file;
            masksJson += std::string(masksJson.empty() ? "" : ",") + "{\\"type\\":2,\\"axis\\":" + std::to_string(m.axis) + ",\\"image\\":" + quote(name) +
                         ",\\"scale\\":[" + num(m.size[0]) + "," + num(m.size[1]) + "],\\"offset\\":[" + num(m.offset[0]) + "," + num(m.offset[1]) +
                         "],\\"scroll\\":[" + num(m.scroll[0]) + "," + num(m.scroll[1]) + "]}";
            ++volumeMaskCount;
        }
        for (const std::string& w : maskWarnings) warn.push_back(where + ": " + w);
        if (!masksJson.empty()) volumeJson += ",\\"masks\\":[" + masksJson + "]";
        volumeJson += "}";
        ++volumeCount;
    }
    if (volumeCount)
        warn.push_back(std::to_string(volumeCount) + " volumetric box(es) written (iw8r_volume; in-game look not verified)" +
                       (volumeMaskCount ? ", with " + std::to_string(volumeMaskCount) + " mask(s) of " + std::to_string(volumeMaskFiles.size()) + " picture(s)" : std::string()));''')
patch(C, '''    // 4.27: the map's own HUD images (alpha kept: icons are cut out).
    {''', '''    // The pictures of the volumetric boxes' masks (above): uncompressed, with mips, as the converter reads them.
    for (const auto& [name, file] : volumeMaskFiles) {
        std::vector<uint8_t> dds;
        std::string imageErr;
        if (encodeDdsFromImage(file, false, false, dds, &imageErr))
            customFiles.push_back({root / "extracted" / "images" / (name + ".dds"), std::string(dds.begin(), dds.end())});
        else
            return fail("volumetric mask picture " + file + " cannot be read: " + imageErr);
    }
    // 4.27: the map's own HUD images (alpha kept: icons are cut out).
    {''')

patch('tools/package_release.ps1', ''''ies', 'stock_assets', 'fx', 'shaders') {''', ''''ies', 'stock_assets', 'fx', 'shaders', 'volume_masks') {''')
patch('tools/package_release.ps1', ''''data\\shaders\\stock.iw8shader.hlsl', ''', ''''data\\shaders\\stock.iw8shader.hlsl', 'data\\volume_masks\\clouds_soft.png', ''')
print('ok')
