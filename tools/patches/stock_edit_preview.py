import io

# Stock map edits shown in the viewport as they are made, without File > Stock Map > Rebuild Stock Zone (owner
# 2026-11-15: "the stock map edits should all be viewable in the viewport after making the changes, i shouldnt need to
# rebuild the zone first"). The entity edits (spawns, loot, vehicles, placed objects, added models and brushes, hidden
# props, the map shader) were drawn live already; the worldspawn edits of the stock world were only in the rebuilt zone:
#   iw8r_stock_retexture  the world surfaces' materials swapped (the pairs the rebuild takes, same catalogue check)
#   iw8r_remove_world     world triangles with all three vertices inside a box left out (decals on them too)
#   iw8r_sky_from         the other map's sky in the camera
#   iw8r_sun_color / iw8r_sun_intensity / iw8r_sun_direction   the Game Lighting sun
# The loaded world batches are kept (up to 4 M vertices) and the edited copy is made on a worker thread; a bigger world
# is read again with the edits.

ROOT = 'E:/IW8-Radiant/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


S = 'src/qtapp/main_window_stock_edit.cpp'
rw(S, [
# The material catalogue as a shared lookup (the rebuild and the preview).
('''QString qs(const std::string& s) { return QString::fromStdString(s); }
''', '''QString qs(const std::string& s) { return QString::fromStdString(s); }
// The stock zone the material catalogue lists for a material ("" unknown): the retexture's carry check.
std::string catalogueZoneOf(const std::string& material) {
    static const std::map<std::string, std::string> catalogue = [] {
        std::map<std::string, std::string> c;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_materials.tsv")); ++up) d.cdUp();
        QFile f(d.filePath("data/asset_catalog/iw8_materials.tsv"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine();
                const int tab = line.indexOf('\\t');
                if (tab <= 0 || line.startsWith('#')) continue;
                c.emplace(line.left(tab).toStdString(), line.mid(tab + 1).section(',', 0, 0).trimmed().toStdString());
            }
        }
        return c;
    }();
    const auto it = catalogue.find(material);
    return it == catalogue.end() ? std::string() : it->second;
}
'''),
('''        // Retexturing (worldspawn iw8r_stock_retexture): the catalogue says which stock zone holds each new material.
        static std::map<std::string, std::string> catalogue;
        if (catalogue.empty()) {
            QDir d(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_materials.tsv")); ++up) d.cdUp();
            QFile f(d.filePath("data/asset_catalog/iw8_materials.tsv"));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QString line = in.readLine();
                    const int tab = line.indexOf('\\t');
                    if (tab <= 0 || line.startsWith('#')) continue;
                    catalogue.emplace(line.left(tab).toStdString(), line.mid(tab + 1).section(',', 0, 0).trimmed().toStdString());
                }
            }
        }
        std::vector<std::string> entries;
        stockRetextureEdits(
            ctx_.doc.map(),
            [](const std::string& m) {
                const auto it = catalogue.find(m);
                return it == catalogue.end() ? std::string() : it->second;
            },
            carry, entries, &skyNotes);''', '''        // Retexturing (worldspawn iw8r_stock_retexture): the catalogue says which stock zone holds each new material.
        std::vector<std::string> entries;
        stockRetextureEdits(ctx_.doc.map(), catalogueZoneOf, carry, entries, &skyNotes);'''),
])

# The preview functions, at the end of the file (before the namespace closes).
s = io.open(ROOT + S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'MainWindow::stockWorldEdits()' not in s:
    s = s.replace('\r\n', '\n')
    k = s.rindex('}  // namespace iw8r::qt')
    s = s[:k] + '''// ------------------------------------------------------------------ stock edits in the viewport

MainWindow::StockWorldEdits MainWindow::stockWorldEdits() const {
    StockWorldEdits e;
    // The retexture pairs the rebuild takes (same parse and catalogue check: a pair it leaves out is not shown).
    std::vector<StockEditCarry> carry;
    std::vector<std::string> entries;
    stockRetextureEdits(ctx_.doc.map(), catalogueZoneOf, carry, entries, nullptr);
    for (const std::string& j : entries) {
        const size_t f = j.find("\\"from\\":\\""), t = j.find("\\",\\"to\\":\\"");
        if (f == std::string::npos || t == std::string::npos) continue;
        const std::string from = j.substr(f + 8, t - (f + 8));
        const size_t te = j.find('"', t + 8);
        if (te == std::string::npos) continue;
        e.remap[from] = j.substr(t + 8, te - (t + 8));
    }
    for (const WorldRemovalBox& b : documentWorldRemovals(ctx_.doc.map()))
        if (b.render) e.boxes.push_back({b.mins.x, b.mins.y, b.mins.z, b.maxs.x, b.maxs.y, b.maxs.z});
    for (const auto& [from, to] : e.remap) e.signature += qs(from) + "=" + qs(to) + ";";
    for (const auto& b : e.boxes)
        for (const double v : b) e.signature += QString::number(v, 'f', 2) + " ";
    return e;
}

void MainWindow::applyStockWorldEdits(std::vector<app::ReferenceTexBatch>& batches, const StockWorldEdits& e, size_t* removed,
                                      size_t* swapped) {
    size_t gone = 0, renamed = 0;
    const auto inside = [&](const app::GpuTexVertex& v, const std::array<double, 6>& b) {
        constexpr double eps = 1e-3;
        return v.x >= b[0] - eps && v.y >= b[1] - eps && v.z >= b[2] - eps && v.x <= b[3] + eps && v.y <= b[4] + eps && v.z <= b[5] + eps;
    };
    for (app::ReferenceTexBatch& b : batches) {
        // A world surface's material (its layers carry the same name); decal volumes and terrain layers are not
        // world surfaces.
        if (!b.decalVolume && !b.terrainLayer)
            if (const auto it = e.remap.find(b.material); it != e.remap.end()) b.material = it->second, ++renamed;
        if (e.boxes.empty() || b.tris.size() < 3) continue;
        // World triangles with all three vertices inside a box (as the rebuild's remove_world_surfaces), and the decals
        // projected on them.
        size_t w = 0;
        for (size_t t = 0; t + 2 < b.tris.size(); t += 3) {
            bool out = false;
            for (const auto& box : e.boxes)
                if (inside(b.tris[t], box) && inside(b.tris[t + 1], box) && inside(b.tris[t + 2], box)) {
                    out = true;
                    break;
                }
            if (out) {
                ++gone;
                continue;
            }
            if (w != t) b.tris[w] = b.tris[t], b.tris[w + 1] = b.tris[t + 1], b.tris[w + 2] = b.tris[t + 2];
            w += 3;
        }
        b.tris.resize(w);
    }
    if (removed) *removed = gone;
    if (swapped) *swapped = renamed;
}

void MainWindow::refreshStockWorldPreview() {
    stockPreviewQueued_ = false;
    applyStockSunPreview();
    if (camera_) camera_->update();  // the sky source (iw8r_sky_from) is asked every frame
    if (referenceZone_.isEmpty() || !stockWorld_) return;
    StockWorldEdits edits = stockWorldEdits();
    if (edits.signature == stockWorldEditSig_) return;
    stockWorldEditSig_ = edits.signature;
    if (!stockRefBatches_) {
        // The world was too big to keep a copy of: read again, with the edits.
        ctx_.setStatus("Stock map edits: reading " + referenceZone_ + " again to show them...");
        loadStockReference(referenceZone_);
        return;
    }
    const int gen = ++stockPreviewGen_;
    QPointer<MainWindow> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, src = stockRefBatches_, edits = std::move(edits), gen] {
        auto out = std::make_shared<std::vector<app::ReferenceTexBatch>>(*src);
        size_t removed = 0, swapped = 0;
        applyStockWorldEdits(*out, edits, &removed, &swapped);
        QMetaObject::invokeMethod(qApp, [self, out, gen, removed, swapped] {
            if (!self || gen != self->stockPreviewGen_) return;  // a newer edit replaces this one
            self->camera_->setReferenceTextured(std::move(*out));
            QStringList parts;
            if (swapped) parts << QString("%1 surface batches retextured").arg(swapped);
            if (removed) parts << QString("%1 world triangles removed").arg(removed);
            self->ctx_.setTransientStatus(parts.isEmpty() ? QString("Stock map edits: the world is shown as stock")
                                                          : "Stock map edits shown in the viewport (" + parts.join(", ") + "; the rebuild writes them)");
        }, Qt::QueuedConnection);
    })).detach();
}

void MainWindow::applyStockSunPreview() {
    if (!stockSunBaseKnown_ || !camera_) return;
    double intensity = stockSunBaseIntensity_;
    Vec3 color = stockSunBaseColor_, toward = stockSunBaseToward_;
    QString sig;
    for (const Entity& e : ctx_.doc.map().entities) {
        if (!e.isWorldspawn()) continue;
        // The values the rebuild writes (core stockSunJson: the same ranges; a bad value is left out there too).
        if (const std::string* c = e.value("iw8r_sun_color"); c && !c->empty()) {
            double v[3] = {0, 0, 0};
            char tail = 0;
            if (std::sscanf(c->c_str(), "%lf %lf %lf %c", &v[0], &v[1], &v[2], &tail) == 3 && v[0] >= 0 && v[1] >= 0 && v[2] >= 0 &&
                v[0] <= 16 && v[1] <= 16 && v[2] <= 16)
                color = Vec3{v[0], v[1], v[2]}, sig += "c" + qs(*c);
        }
        if (const std::string* i = e.value("iw8r_sun_intensity"); i && !i->empty()) {
            char* endp = nullptr;
            const double v = std::strtod(i->c_str(), &endp);
            if (endp && *endp == 0 && v >= 0 && v <= 1000000) intensity = v, sig += "i" + qs(*i);
        }
        if (const std::string* d = e.value("iw8r_sun_direction"); d && !d->empty()) {
            double pitch = 0, yaw = 0;
            if (parseStockSunDirection(*d, pitch, yaw)) {
                const double p = pitch * kPi / 180.0, y = yaw * kPi / 180.0;
                toward = normalize(Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)});
                sig += "d" + qs(*d);
            }
        }
        break;
    }
    if (sig == stockSunSig_ && stockSunApplied_) return;
    stockSunSig_ = sig;
    stockSunApplied_ = true;
    if (intensity > 0) camera_->setGameStockSun(intensity, color, toward);
}

''' + s[k:]
    io.open(ROOT + S, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))

# Header.
rw('src/qtapp/main_window.h', [
('''    std::shared_ptr<const StockWorld> stockWorld_;  // the loaded stock map reference (11.8 region export)''',
 '''    std::shared_ptr<const StockWorld> stockWorld_;  // the loaded stock map reference (11.8 region export)
    // Stock map edits shown in the viewport as they are made (main_window_stock_edit.cpp): the world batches as loaded
    // (null when too big to keep: read again for an edit), the edits the camera shows, the stock sun as loaded.
    struct StockWorldEdits {
        std::map<std::string, std::string> remap;  // iw8r_stock_retexture pairs the rebuild takes
        std::vector<std::array<double, 6>> boxes;  // iw8r_remove_world boxes that take triangles out of the drawing
        QString signature;                         // "" = none
    };
    StockWorldEdits stockWorldEdits() const;
    static void applyStockWorldEdits(std::vector<app::ReferenceTexBatch>& batches, const StockWorldEdits& e, size_t* removed,
                                     size_t* swapped);
    void refreshStockWorldPreview();
    void applyStockSunPreview();
    std::shared_ptr<const std::vector<app::ReferenceTexBatch>> stockRefBatches_;
    QString stockWorldEditSig_;
    int stockPreviewGen_ = 0;
    bool stockPreviewQueued_ = false;
    bool stockSunBaseKnown_ = false, stockSunApplied_ = false;
    double stockSunBaseIntensity_ = 0;
    Vec3 stockSunBaseColor_, stockSunBaseToward_;
    QString stockSunSig_;'''),
])

# loadStockReference: the edits applied in the loader's worker; the unedited batches kept when small.
M = 'src/qtapp/main_window.cpp'
rw(M, [
('''    // Parsing takes a few seconds (hundreds of MB of JSON): off the UI thread, then hand the triangles to the camera.
    QPointer<MainWindow> self(this);
    WorldCache* cache = worldCache_;''', '''    // Parsing takes a few seconds (hundreds of MB of JSON): off the UI thread, then hand the triangles to the camera.
    // The document's stock world edits (retexture, removed geometry) are applied there too (refreshStockWorldPreview).
    QPointer<MainWindow> self(this);
    WorldCache* cache = worldCache_;
    StockWorldEdits worldEdits = stockWorldEdits();
    stockWorldEditSig_ = worldEdits.signature;
    ++stockPreviewGen_;  // an edit preview under way is for the old batches'''),
('''    std::thread(guardedWorker(__FILE__, __LINE__, [self, cache, zone, zoneDir, tools] {
        QString err;
        const bool autoStream = Settings::instance().value("stock/autoStream", true).toBool();''', '''    std::thread(guardedWorker(__FILE__, __LINE__, [self, cache, zone, zoneDir, tools, worldEdits = std::move(worldEdits)] {
        QString err;
        const bool autoStream = Settings::instance().value("stock/autoStream", true).toBool();'''),
('''        // Props (11.3, core stock_smodels): the static model placements of the same export.
        auto props = std::make_shared<std::vector<StockModelInstance>>();''', '''        // Kept as loaded for the stock edits' preview (up to 4 M vertices, about 240 MB), then the edits applied.
        std::shared_ptr<const std::vector<app::ReferenceTexBatch>> keptBatches;
        {
            size_t verts = 0;
            for (const auto& b : *texBatches) verts += b.tris.size();
            if (verts <= 4'000'000) keptBatches = std::make_shared<const std::vector<app::ReferenceTexBatch>>(*texBatches);
            if (!worldEdits.signature.isEmpty()) MainWindow::applyStockWorldEdits(*texBatches, worldEdits, nullptr, nullptr);
        }
        // Props (11.3, core stock_smodels): the static model placements of the same export.
        auto props = std::make_shared<std::vector<StockModelInstance>>();'''),
('''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter, terrainLayers, streamZones] {
            if (!self || self->referenceZone_ != zone) return;
            self->stockWorld_ = world;''', '''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter, terrainLayers, streamZones,
                                         keptBatches] {
            if (!self || self->referenceZone_ != zone) return;
            self->stockWorld_ = world;
            self->stockRefBatches_ = keptBatches;'''),
('''            if (intensity > 0) self->camera_->setGameStockSun(intensity, color, toward);''',
 '''            if (intensity > 0) {
                // The map's own sun, kept for the iw8r_sun_* edits shown over it (applyStockSunPreview).
                self->stockSunBaseKnown_ = true, self->stockSunApplied_ = false;
                self->stockSunBaseIntensity_ = intensity, self->stockSunBaseColor_ = color, self->stockSunBaseToward_ = toward;
                self->applyStockSunPreview();
            }'''),
# Edits: re-checked shortly after every document change while a stock map is open.
('''    referenceZone_ = zone;
    // Props the model catalogue does not list are looked for in the map's own zone.''', '''    if (!stockPreviewHooked_) {
        stockPreviewHooked_ = true;
        connect(&ctx_, &EditorContext::documentChanged, this, [this] {
            if (referenceZone_.isEmpty() || stockPreviewQueued_) return;
            stockPreviewQueued_ = true;
            QTimer::singleShot(250, this, [this] { refreshStockWorldPreview(); });
        });
    }
    if (zone != referenceZone_) stockSunBaseKnown_ = false, stockRefBatches_.reset();
    referenceZone_ = zone;
    // Props the model catalogue does not list are looked for in the map's own zone.'''),
# The other map's sky (iw8r_sky_from) in the camera.
('''                for (const char* key : {"skyname", "sky"})
                    if (const std::string* v = e.value(key); v && !v->empty()) {
                        const QString f = skyCache->cachedDds(qs(*v));
                        if (f.isEmpty()) skyCache->request(qs(*v));
                        return f;
                    }
                break;
            }''', '''                for (const char* key : {"skyname", "sky"})
                    if (const std::string* v = e.value(key); v && !v->empty()) {
                        const QString f = skyCache->cachedDds(qs(*v));
                        if (f.isEmpty()) skyCache->request(qs(*v));
                        return f;
                    }
                // A stock map with another map's sky (File > Stock Map > Relight Stock Map Sky, iw8r_sky_from): shown
                // before the rebuild carries it in.
                if (const std::string* v = e.value("iw8r_sky_from"); v && !v->empty() && stockWorld_) {
                    const QString id = qs(*v).trimmed().toLower();
                    static const QRegularExpression valid("^mp_[a-z0-9_]{1,37}$");
                    if (valid.match(id).hasMatch() && id != referenceZone_) {
                        const QString mat = "tw/sky_" + id;
                        const QString f = skyCache->cachedDds(mat);
                        if (f.isEmpty()) skyCache->request(mat);
                        return f;
                    }
                }
                break;
            }'''),
])
rw('src/qtapp/main_window.h', [
('''    bool stockPreviewQueued_ = false;''', '''    bool stockPreviewQueued_ = false, stockPreviewHooked_ = false;'''),
])
print('ok')
