import io


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


p = 'src/qtapp/main_window.cpp'
patch(p, '''        if (world) app::buildReferenceTexBatches(*world, *texBatches);  // textured camera reference (3.19)''',
      '''        // Displaced stock surfaces (research 98 / 101): a copy of the world with those meshes subdivided and moved by
        // their height maps, for the textured reference only (picking and the flat reference keep the flat world).
        std::shared_ptr<const StockWorld> drawWorld = world;
        if (world)
            if (auto displaced = displaceStockWorld(*world, zone, zoneDir, tools)) drawWorld = displaced;
        if (drawWorld) app::buildReferenceTexBatches(*drawWorld, *texBatches);  // textured camera reference (3.19)''')
patch(p, '''std::shared_ptr<StockDecalData> appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                        const StockDecalTools& tools, std::vector<app::ReferenceTexBatch>& out);''',
      '''std::shared_ptr<StockDecalData> appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                        const StockDecalTools& tools, std::vector<app::ReferenceTexBatch>& out);
// A copy of `world` with its displaced surfaces subdivided and moved (null when the map has none or they cannot be read).
std::shared_ptr<const StockWorld> displaceStockWorld(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                     const StockDecalTools& tools);''')
patch(p, '''void MainWindow::loadStockSun(const QString& zone, const QString& skyMaterialIn) {''', '''std::shared_ptr<const StockWorld> displaceStockWorld(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                     const StockDecalTools& tools) {
    if (qEnvironmentVariableIsSet("IW8R_NO_DISPLACEMENT")) return nullptr;
    StockDisplacementData data;
    std::string err;
    if (!readStockDisplacement(zoneDir.toStdString(), world, data, &err) || data.meshes.empty()) return nullptr;
    // The displaced materials' bodies (constant 45 + L: scale, bias, w; texture 95 + L: the height map), exported once.
    const QString dir = zoneDir + "/disp";
    const auto findFiles = [&](const QString& sub, const QStringList& filters) {
        QStringList out;
        for (QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QString f = it.next();
            if (f.contains(sub)) out << f;
        }
        return out;
    };
    const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
    const auto acts = [&](const QStringList& extra, const QString& types, const QStringList& names, const QString& ff) {
        if (!QFileInfo::exists(tools.acts) || !QFileInfo::exists(ff) || names.isEmpty()) return;
        QStringList args{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", tools.replay, "--oodle", tools.oodle};
        args << extra << "-a" << types << "-n" << names.join(',') << "-l" << "-o" << dir << ff;
        QProcess p;
        p.start(tools.acts, args);
        p.waitForFinished(300000);
    };
    std::set<QString> wanted;
    for (const auto& m : data.meshes) wanted.insert(QString::fromStdString(m.material).remove(QChar(',')));
    if (findFiles("/assets/material/", {"*.json"}).isEmpty())
        for (const QString& z : {zoneFolder + "techsets_" + zone + ".ff", zoneFolder + zone + ".ff"})
            acts({}, "material", QStringList(wanted.begin(), wanted.end()), z);
    struct MatDisp {
        std::map<int, std::array<float, 3>> constants;  // L -> scale, bias, w
        std::map<int, QString> height;                   // L -> image
    };
    std::map<QString, MatDisp> mats;
    std::set<QString> images;
    for (const QString& f : findFiles("/assets/material/", {"*.json"})) {
        QFile mf(f);
        if (!mf.open(QIODevice::ReadOnly)) continue;
        const QJsonObject fields = QJsonDocument::fromJson(mf.readAll()).object()["asset"].toObject()["fields"].toObject();
        const QString name = fields["name"].toString().remove(QChar(','));
        if (!wanted.count(name)) continue;
        MatDisp md;
        for (const QJsonValue& cv : fields["constantTable"].toObject()["values"].toArray()) {
            const QJsonObject c = cv.toObject();
            const int idx = c["index"].toInt(-1);
            if (idx < 45 || idx >= 53) continue;
            const QJsonArray v = c["literal"].toObject()["v"].toArray();
            if (v.size() >= 4) md.constants[idx - 45] = {float(v[0].toDouble()), float(v[1].toDouble()), float(v[3].toDouble())};
        }
        for (const QJsonValue& tv : fields["textureTable"].toObject()["values"].toArray()) {
            const QJsonObject t = tv.toObject();
            const int slot = t["index"].toInt(-1);
            const QString img = t["image"].toObject()["name"].toString().remove(QChar(','));
            if (slot >= 95 && slot < 103 && !img.isEmpty() && !img.startsWith('$')) md.height[slot - 95] = img, images.insert(img);
        }
        mats[name] = md;
    }
    if (mats.empty()) return nullptr;
    // The height images (streamed: the populated XPaks are passed).
    const auto imageFile = [&](const QString& img) {
        for (const QString& f : findFiles("/assets/image/", {"*.dds"}))
            if (QFileInfo(f).fileName().startsWith(img + ".")) return f;
        return QString();
    };
    QStringList missing;
    for (const QString& img : images)
        if (imageFile(img).isEmpty()) missing << img;
    if (!missing.isEmpty()) {
        QStringList xp;
        for (const QFileInfo& f : QDir(zoneFolder).entryInfoList({"*.xpak"}, QDir::Files))
            if (f.size() >= 64 * 1024) xp << "--xpak" << f.absoluteFilePath();
        acts(xp, "image", missing, zoneFolder + zone + ".ff");
        missing.clear();
        for (const QString& img : images)
            if (imageFile(img).isEmpty()) missing << img;
        if (!missing.isEmpty()) acts(xp, "image", missing, zoneFolder + "techsets_" + zone + ".ff");
    }
    std::map<QString, std::shared_ptr<StockHeightMap>> heights;
    for (const QString& img : images) {
        const QString f = imageFile(img);
        app::Rgba8Image rgba;
        if (f.isEmpty() || !app::loadDdsPreview(f.toStdString(), 1024, rgba) || rgba.width <= 0) continue;
        heights[img] = std::make_shared<StockHeightMap>(stockHeightMapFromR8(rgba.width, rgba.height, rgba.pixels.data(), 4));
    }
    auto out = std::make_shared<StockWorld>(world);
    StockDisplacementOptions opt;
    opt.targetEdge = stockDisplacementEdgeForBudget(world, data, 1'500'000);
    size_t done = 0;
    for (const StockMeshDisplacement& md : data.meshes) {
        const auto mi = mats.find(QString::fromStdString(md.material).remove(QChar(',')));
        if (mi == mats.end()) continue;
        std::vector<StockDisplacementLayer> layers;
        for (int L = 0; L < md.layerCount; ++L) {
            if (!(md.displacedLayers & (1u << L))) continue;
            const auto c = mi->second.constants.find(L);
            const auto h = mi->second.height.find(L);
            if (c == mi->second.constants.end() || h == mi->second.height.end() || !heights.count(h->second)) continue;
            layers.push_back({L, c->second[0], c->second[1], c->second[2], heights[h->second].get()});
        }
        if (layers.empty()) continue;
        StockDisplacedMesh dm;
        if (displaceStockMesh(*out, md, layers, opt, dm)) replaceStockWorldMesh(*out, md.mesh, std::move(dm)), ++done;
    }
    return done ? out : nullptr;
}

void MainWindow::loadStockSun(const QString& zone, const QString& skyMaterialIn) {''')
s = io.open(p, encoding='utf-8', newline='').read()
for inc in ('#include "stock_displacement.h"', '#include "texture_decode.h"', '#include <set>'):
    if inc not in s:
        i = s.index('#include ')
        s = s[:i] + inc + '\n' + s[i:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
