import io

# Fixes for the defects a second independent review found (2026-11-14):
#  1. HUD images and the text file of an earlier build stayed in the extraction folder and were built in again after
#     the picture or the text was removed from the map: they are deleted before the build writes its own.
#  2. Opening a stock map a second time while the first read still ran deleted the folder that read was writing into:
#     the "already being read" check comes first.
#  3 / 4. The shadow passes left out cells of props by the camera's distance. Shadow maps are kept over several frames
#     (the lamp maps until a caster changes), so a prop could be drawn without its shadow after the camera came
#     closer. The skip is taken out again: a prop left out by distance still casts its shadow, as before today.
#  5. The list of HUD images is sorted by name before it is searched (the script check gave false warnings).
#  6. A picture whose file name is outside the system's character set is left out with a warning instead of ending
#     the program; an unreadable text file is said.
#  7. A text file saved with a byte-order mark lost its first entry.
#  8. The names a script uses (&"...") are read with comments and string literals skipped.
#  +  A tool face is hidden under paint meshes only when their triangles cover its whole area (a face with a gap
#     between two patches was hidden).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


def cut(p, start, end, replacement):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    start = start.replace('\n', nl); end = end.replace('\n', nl)
    assert s.count(start) == 1, (p, s.count(start), start[:60])
    i = s.index(start)
    j = s.index(end, i)
    write(p, s[:i] + replacement.replace('\n', nl) + s[j:])


# ---------------- 1, 5, 6: the build
K = 'src/core/compile_iw8.cpp'
patch(K, '''        for (const std::string& w : hudWarnings) rep.warnings.push_back(w);
        if (!rep.hudImages.empty()) {''', '''        for (const std::string& w : hudWarnings) rep.warnings.push_back(w);
        std::sort(rep.hudImages.begin(), rep.hudImages.end());  // searched below (the script check)
        if (!rep.hudImages.empty()) {''')
patch(K, '''        if (!opt.stringsFile.empty() && readFile(opt.stringsFile, strText)) {
            std::vector<std::string> strWarnings;
            strings = parseMapStrings(strText, id, &strWarnings);
            for (const std::string& w : strWarnings) rep.warnings.push_back(w);
        }''', '''        if (!opt.stringsFile.empty() && readFile(opt.stringsFile, strText)) {
            std::vector<std::string> strWarnings;
            strings = parseMapStrings(strText, id, &strWarnings);
            for (const std::string& w : strWarnings) rep.warnings.push_back(w);
        } else if (!opt.stringsFile.empty()) {
            rep.warnings.push_back("the map's text file " + opt.stringsFile + " cannot be read; its entries are not built");
        }''')
patch(K, '''    for (size_t i = 0; ok && i < customFiles.size(); ++i) ok = writeFile(customFiles[i].first, customFiles[i].second, werr);''',
      '''    {
        // The folder may hold an earlier build of this map. The converter takes every hud_* image and the text file it
        // finds there: what this build does not write must go, or a picture or text taken out of the map stays in it.
        std::error_code ec;
        const fs::path images = root / "extracted" / "images";
        std::vector<fs::path> stale;
        for (fs::directory_iterator it(images, ec), end; !ec && it != end; it.increment(ec)) {
            std::string stem, ext;
            try {
                stem = it->path().stem().string();
                ext = it->path().extension().string();
            } catch (...) {
                continue;  // a name outside the system's character set is not one of ours
            }
            for (char& c : stem) c = char(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (stem.rfind("hud_", 0) == 0 && (ext == ".dds" || ext == ".iwi") && !std::binary_search(rep.hudImages.begin(), rep.hudImages.end(), stem))
                stale.push_back(it->path());
        }
        for (const fs::path& p : stale) fs::remove(p, ec);
        if (rep.mapStrings.empty()) fs::remove(root / "extracted" / "localize" / "strings.tsv", ec);
    }
    for (size_t i = 0; ok && i < customFiles.size(); ++i) ok = writeFile(customFiles[i].first, customFiles[i].second, werr);''')

# ---------------- 6: HUD image listing
cut('src/core/hud_images.cpp', '''    std::vector<fs::path> files;
    for (fs::directory_iterator it(fs::path(folder), ec), end; !ec && it != end; it.increment(ec)) {''',
    '''bool encodeHudImageDds(''', '''    const auto warn = [&](const std::string& line) {
        if (warnings) warnings->push_back(line);
    };
    // File name and full path, as text. A name the system's character set cannot hold makes the conversion throw: such a
    // file is left out (the image decoder could not open it either).
    std::vector<std::pair<std::string, std::string>> files;
    size_t unusable = 0;
    for (fs::directory_iterator it(fs::path(folder), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        try {
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".dds")
                files.emplace_back(it->path().filename().string(), it->path().string());
        } catch (...) {
            ++unusable;
        }
    }
    if (unusable)
        warn(std::to_string(unusable) + " file(s) in the HUD images folder left out: their names use characters outside the system's character set");
    std::sort(files.begin(), files.end());
    std::set<std::string> seen;
    for (const auto& [file, path] : files) {
        const std::string name = hudImageName(file);
        if (name.empty()) {
            warn("HUD image " + file + " left out: its name has no letter or digit");
            continue;
        }
        if (!seen.insert(name).second) {
            warn("HUD image " + file + " left out: another file is already " + name);
            continue;
        }
        if (out.size() >= kMaxHudImages) {
            warn("HUD image " + file + " left out: more than " + std::to_string(kMaxHudImages) + " HUD images");
            continue;
        }
        out.push_back({name, path});
    }
    return out;
}

''')
patch('src/core/hud_images.cpp', '''#include <system_error>
''', '''#include <system_error>
#include <utility>
''')

# ---------------- 7, 8: the text file
S = 'src/core/map_strings.cpp'
patch(S, '''    size_t lineNo = 0, pos = 0;
    const auto closeRef = [&] {''', '''    size_t lineNo = 0, pos = 0;
    // A byte-order mark at the start (editors that save "UTF-8 with BOM") is not part of the first word.
    if (strText.size() >= 3 && static_cast<unsigned char>(strText[0]) == 0xEF && static_cast<unsigned char>(strText[1]) == 0xBB &&
        static_cast<unsigned char>(strText[2]) == 0xBF)
        pos = 3;
    const auto closeRef = [&] {''')
cut(S, '''std::vector<std::string> stringRefsInScript(const std::string& scriptText) {''', '''std::string mapStringsTemplate(const std::string& prefix) {''',
    '''std::vector<std::string> stringRefsInScript(const std::string& scriptText) {
    // Comments and ordinary string literals are skipped, so a quote inside them cannot pair up with a later one.
    std::set<std::string> names;
    const size_t n = scriptText.size();
    size_t i = 0;
    while (i < n) {
        const char c = scriptText[i];
        if (c == '/' && i + 1 < n && scriptText[i + 1] == '/') {
            while (i < n && scriptText[i] != '\\n') ++i;
        } else if (c == '/' && i + 1 < n && scriptText[i + 1] == '*') {
            const size_t end = scriptText.find("*/", i + 2);
            i = end == std::string::npos ? n : end + 2;
        } else if (c == '"' || (c == '&' && i + 1 < n && scriptText[i + 1] == '"')) {
            const bool localized = c == '&';
            size_t j = i + (localized ? 2 : 1);
            std::string text;
            while (j < n && scriptText[j] != '"' && scriptText[j] != '\\n') {
                if (scriptText[j] == '\\\\' && j + 1 < n) text.push_back(scriptText[j]), ++j;
                text.push_back(scriptText[j]);
                ++j;
            }
            if (localized && !text.empty() && text.find_first_of(" \\t\\r") == std::string::npos) names.insert(upper(text));
            i = j < n ? j + 1 : n;
        } else {
            ++i;
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

''')

# ---------------- 2: the stock map open
M = 'src/qtapp/main_window.cpp'
patch(M, '''    // An export that was cancelled or failed left its marker: what is in the folder may be half written.
    if (QFileInfo::exists(cache + "/export.incomplete")) QDir(cache + "/mw19replay").removeRecursively();''',
      '''    // A read of a stock map is still running (its wait runs the event loop): its folder must not be touched.
    if (exportingMapEnts_) {
        if (err) *err = "A stock map is being read already; wait for it to finish.";
        return false;
    }
    // An export that was cancelled or failed left its marker: what is in the folder may be half written.
    if (QFileInfo::exists(cache + "/export.incomplete")) QDir(cache + "/mw19replay").removeRecursively();''')

# ---------------- 3 / 4: no eye-dependent skip in the shadow passes
R = 'src/viewport/render.cpp'
patch(R, '''        // Runs of neighbouring cells in the view, one draw each. A cell whose placements are all past their cull
        // distance in the camera's view (research 119) casts nothing: the game leaves them out of its shadow passes too.
        const bool distCull = cellCullOn_ && g.lodOut > 0;
        uint32_t runFirst = 0, runCount = 0;
        for (const auto& c : g.cells) {
            if (distCull && c.maxScale > 0) {
                double d2 = 0;
                const double e[3] = {fog_[0], fog_[1], fog_[2]}, lo[3] = {c.box.mins.x, c.box.mins.y, c.box.mins.z},
                             hi[3] = {c.box.maxs.x, c.box.maxs.y, c.box.maxs.z};
                for (int k = 0; k < 3; ++k) {
                    const double d = std::max(std::max(lo[k] - e[k], e[k] - hi[k]), 0.0);
                    d2 += d * d;
                }
                if (iw8r::placementLodCulled(float(std::sqrt(d2)), cellCullFov_, c.maxScale, g.lodOut)) continue;
            }
            if (iw8r::frustumIntersectsAabb(*view, c.box)) {''', '''        // Runs of neighbouring cells in the view, one draw each. Every placement casts, also one the camera's view
        // leaves out by distance: a shadow map is kept over several frames (a lamp's until a caster changes), so
        // leaving casters out by where the camera stood when it was drawn would lose shadows after the camera moved.
        uint32_t runFirst = 0, runCount = 0;
        for (const auto& c : g.cells) {
            if (iw8r::frustumIntersectsAabb(*view, c.box)) {''')
patch(R, '''    cellCullOn_ = !noLodOut;
    cellCullFov_ = std::min(lodFov, 1.0f);
''', '')
patch(R, '''        cellCullOn_ = false;  // set by cullTexInstances when it runs
''', '')
patch(R, '''                        if (!g.cullScale.empty()) {
                            const float cs = g.cullScale[i];
                            if (c.count == 1) c.maxScale = cs;
                            else if (c.maxScale > 0) c.maxScale = cs > 0 ? std::max(c.maxScale, cs) : 0.0f;
                        }
''', '')
RH = 'src/viewport/render.h'
patch(RH, '''            float maxScale = 1;  // the largest cullScale of its placements; 0 = one of them is never left out
''', '')
patch(RH, '''    // The view's distance cull as the shadow passes need it (drawReferenceTexInstancesShadow): on for this frame, the
    // capped field-of-view factor.
    bool cellCullOn_ = false;
    float cellCullFov_ = 1.0f;
''', '')

# ---------------- covers: the whole area
P = 'src/viewport/patch_covers.cpp'
patch(P, '''}  // namespace

uint64_t PatchCovers::find(''', '''// Area of triangle (a, b, c) inside the convex polygon `poly` (all in the plane with normal n; poly wound either way).
double clippedArea(const Vec3& a, const Vec3& b, const Vec3& c, const Winding& poly, const Vec3& n) {
    // The polygon's own turning sense about n.
    double turn = 0;
    for (size_t k = 0; k < poly.size(); ++k)
        turn += dot(cross(poly[(k + 1) % poly.size()] - poly[k], poly[(k + 2) % poly.size()] - poly[(k + 1) % poly.size()]), n);
    const double sign = turn >= 0 ? 1.0 : -1.0;
    std::vector<Vec3> in{a, b, c}, out;
    for (size_t k = 0; k < poly.size() && in.size() >= 3; ++k) {
        const Vec3& p0 = poly[k];
        const Vec3 edge = poly[(k + 1) % poly.size()] - p0;
        const auto side = [&](const Vec3& p) { return sign * dot(cross(edge, p - p0), n); };  // >= 0: inside this edge
        out.clear();
        for (size_t i = 0; i < in.size(); ++i) {
            const Vec3& u = in[i];
            const Vec3& v = in[(i + 1) % in.size()];
            const double su = side(u), sv = side(v);
            if (su >= 0) out.push_back(u);
            if ((su >= 0) != (sv >= 0)) out.push_back(u + (v - u) * (su / (su - sv)));
        }
        in.swap(out);
    }
    double area = 0;
    for (size_t k = 1; k + 1 < in.size(); ++k) area += 0.5 * std::fabs(dot(cross(in[k] - in[0], in[k + 1] - in[0]), n));
    return area;
}

}  // namespace

uint64_t PatchCovers::find(''')
patch(P, '''        if (!in) return false;
    }
    return true;
}''', '''        if (!in) return false;
    }
    // The samples can all be covered while a strip between two patches is not: the patch triangles must also hold the
    // face's whole area. (Patches that lie over each other count twice; a paint mesh does not do that.)
    double faceArea = 0, held = 0;
    for (size_t k = 1; k + 1 < face.size(); ++k) faceArea += 0.5 * std::fabs(dot(cross(face[k] - face[0], face[k + 1] - face[0]), normal));
    if (!(faceArea > 0)) return false;
    for (const Cover* c : plane) {
        for (const Winding& t : *c->tris)
            for (size_t k = 1; k + 1 < t.size(); ++k) held += clippedArea(t[0], t[k], t[k + 1], face, normal);
        if (held >= faceArea * 0.999) return true;
    }
    return false;
}''')
print('ok')
