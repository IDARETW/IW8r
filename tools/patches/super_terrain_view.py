import io


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


p = 'src/qtapp/main_window.cpp'
patch(p, '''        if (drawWorld) app::buildReferenceTexBatches(*drawWorld, *texBatches);  // textured camera reference (3.19)''',
      '''        if (drawWorld) app::buildReferenceTexBatches(*drawWorld, *texBatches);  // textured camera reference (3.19)
        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches);  // Warzone ground (research 102)''')
patch(p, '''// A copy of `world` with its displaced surfaces subdivided and moved (null when the map has none or they cannot be read).''',
      '''// The super terrain of a Warzone map (Verdansk, Quarry; research 102): its height maps as a mesh within a triangle
// budget, textured with each surface's baked albedo. Nothing for maps without one (remembered in <zoneDir>/terrain).
void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out);
// A copy of `world` with its displaced surfaces subdivided and moved (null when the map has none or they cannot be read).''')
patch(p, '''std::shared_ptr<const StockWorld> displaceStockWorld(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                     const StockDecalTools& tools) {''', '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out) {
    if (qEnvironmentVariableIsSet("IW8R_NO_SUPER_TERRAIN")) return;
    const QString dir = zoneDir + "/terrain";
    if (QFileInfo::exists(dir + "/none")) return;  // looked once: no super terrain in this map
    const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
    const QString ff = zoneFolder + zone + ".ff";
    const auto run = [&](const QStringList& extra, const QString& types, const QString& names) {
        if (!QFileInfo::exists(tools.acts) || !QFileInfo::exists(ff)) return;
        QStringList args{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", tools.replay, "--oodle", tools.oodle};
        args << extra << "-a" << types;
        if (!names.isEmpty()) args << "-n" << names;
        args << "-l" << "-o" << dir << ff;
        QProcess p;
        p.start(tools.acts, args);
        p.waitForFinished(600000);
    };
    const auto findJson = [&] {
        for (QDirIterator it(dir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QString f = it.next();
            if (f.contains("/assets/stterrain/")) return f;
        }
        return QString();
    };
    QString json = findJson();
    if (json.isEmpty()) {
        QDir().mkpath(dir);
        run({}, "stterrain", QString());
        json = findJson();
        if (json.isEmpty()) {
            QFile none(dir + "/none");
            if (none.open(QIODevice::WriteOnly)) none.write("no stterrain asset in this zone\\n");
            return;
        }
    }
    StockTerrain terrain;
    std::string err;
    if (!readStockTerrain(json.toStdString(), terrain, &err) || terrain.surfaces.empty()) return;
    const QString imageDir = dir + "/mw19replay/" + zone + "/assets/image";
    const std::vector<std::string> names = stockTerrainImageNames(terrain, kTerrainHeight | kTerrainCutout | kTerrainAlbedo);
    QStringList missing;
    for (const std::string& n : names)
        if (findStockTerrainImage(imageDir.toStdString(), n).empty()) missing << QString::fromStdString(n);
    if (!missing.isEmpty()) {
        QStringList xp;
        for (const QFileInfo& f : QDir(zoneFolder).entryInfoList({"*.xpak"}, QDir::Files))
            if (f.size() >= 64 * 1024) xp << "--xpak" << f.absoluteFilePath();
        run(xp, "image", missing.join(','));
    }
    std::vector<StockTerrainHeightGrid> heights(terrain.surfaces.size());
    std::vector<StockTerrainCutout> cutouts(terrain.surfaces.size());
    for (size_t s = 0; s < terrain.surfaces.size(); ++s) {
        const auto& sf = terrain.surfaces[s];
        const std::string hp = findStockTerrainImage(imageDir.toStdString(), sf.heightImage);
        if (!hp.empty()) readStockTerrainHeightDds(hp, heights[s]);
        const std::string cp = sf.cutoutImage.empty() ? std::string() : findStockTerrainImage(imageDir.toStdString(), sf.cutoutImage);
        if (!cp.empty()) readStockTerrainCutoutDds(cp, cutouts[s]);
    }
    // About a million triangles: the reference stores triangle lists (56 bytes a corner).
    StockTerrainBuildOptions opt;
    opt.stride = stockTerrainStrideForBudget(terrain, 1'000'000, &cutouts);
    StockTerrainMeshes meshes;
    if (!buildStockTerrainMeshes(terrain, heights, cutouts, opt, meshes, &err)) return;
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    for (const StockTerrainMesh& tm : meshes.meshes) {
        const ImportedMesh& m = tm.mesh;
        if (m.idx.empty()) continue;
        app::ReferenceTexBatch rb;
        // The surface's baked albedo, by its file ("\\x06<path>"); a plain colour when it was not exported.
        const std::string ap = tm.albedoImage.empty() ? std::string() : findStockTerrainImage(imageDir.toStdString(), tm.albedoImage);
        rb.material = ap.empty() ? std::string("\\x06") : "\\x06" + ap;
        rb.tris.reserve(m.idx.size());
        for (uint32_t k : m.idx) {
            const Vec3& p = m.pos[k];
            const Vec3 n = k < m.normal.size() ? m.normal[k] : Vec3{0, 0, 1};
            const double shade = 0.55 + 0.45 * std::fabs(dot(n, light));
            const uint32_t g = uint32_t(std::clamp(255.0 * shade, 0.0, 255.0));
            app::GpuTexVertex v{float(p.x), float(p.y), float(p.z), 0, 0, g | (g << 8) | (g << 16) | (0xffu << 24)};
            if (k < tm.albedoUv.size()) v.u = float(tm.albedoUv[k][0]), v.v = float(tm.albedoUv[k][1]);
            v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
            rb.tris.push_back(v);
        }
        out.push_back(std::move(rb));
    }
}

std::shared_ptr<const StockWorld> displaceStockWorld(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                     const StockDecalTools& tools) {''')
# the image lookup: "\x06<path>" is a file
patch(p, '''            // A stock decal volume image ("\\x05<image>": colour or mask, research 99): any catalogued image.''',
      '''            // A file by its path ("\\x06<path>": the super terrain's baked albedo); "" = none.
            if (!m.empty() && m[0] == '\\x06') {
                const QString f = qs(m.substr(1));
                return !f.isEmpty() && QFileInfo::exists(f) ? f : QString();
            }
            // A stock decal volume image ("\\x05<image>": colour or mask, research 99): any catalogued image.''')
patch(p, '''            if (!m.empty() && m[0] == '\\x05') return;  // decal volume images: requested by the lookup''',
      '''            if (!m.empty() && (m[0] == '\\x05' || m[0] == '\\x06')) return;  // requested by the lookup / a file''')
s = io.open(p, encoding='utf-8', newline='').read()
if '#include "stock_super_terrain.h"' not in s:
    i = s.index('#include ')
    s = s[:i] + '#include "stock_super_terrain.h"\n' + s[i:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
