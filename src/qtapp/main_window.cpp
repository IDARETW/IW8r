#include "stock_decal_volumes.h"
#include "game_lightmap.h"
#include "vision_editor_dialog.h"
#include "material_replace.h"
#include "techset_export.h"
#include "techset_dialog.h"
#include "image_adjust_dialog.h"
#include "sound_settings_dialog.h"
#include "asset_usage_dialog.h"
#include "material_clone.h"
#include "uv_editor.h"
#include <cstring>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include "cache_manager.h"
#include "fx_editor.h"
#include "stock_world_brushes.h"
#include "stock_vision.h"
#include <QPainterPath>
#include <QPainter>
#include "foliage_sway.h"
#include "sky_image.h"
#include "sky_sun.h"
#include "stock_world_export.h"
#include "material_blend.h"
#include "techset_info.h"
#include "vfx_preview.h"
#include "hot_channels.h"
#include "port_wizard_dialog.h"
#include "vfx_info.h"
#include "hotkey_presets.h"
#include "decal_tools.h"
#include "stock_smodels.h"
#include "nav_debug.h"
#include "lightmap_fields.h"
#include "verification.h"
#include "command_script.h"
#include "road_tools.h"
#include "tool_flags.h"
#include "zone_deps.h"
#include "overview_image.h"
#include "convert_ops.h"
#include "sky_panorama.h"
#include "bevel_ops.h"
#include "scatter_rules.h"
#include "map_diff.h"
#include "spline_tools.h"
#include "trigger_templates.h"
#include "link_graph.h"
#include "layout_tools.h"
#include "command_palette.h"
#include "tod_presets.h"
#include "iw8_contents.h"
#include "asset_info.h"
#include "compile_iw8.h"
#include "main_window.h"

#include <QDate>
#include <QComboBox>
#include <QFontDatabase>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QTextStream>
#include <QAction>
#include <QCheckBox>
#include <QActionGroup>
#include <filesystem>
#include <QInputDialog>
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QSaveFile>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QUrl>
#include <QDirIterator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QRegularExpression>
#include <QToolBar>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "DockAreaWidget.h"
#include "DockManager.h"
#include "asset_browser.h"
#include "brush_flags.h"
#include "brush_tools.h"
#include "camera_view.h"
#include "find_dialog.h"
#include "prefab_browser.h"
#include "model_preview.h"
#include "gamemode_templates.h"
#include "world_cache.h"
#include <QInputDialog>
#include <QPointer>
#include <thread>
#include "layers_panel.h"
#include "entity_defs.h"
#include "glb_mesh.h"
#include "material_cache.h"
#include "stock_sky.h"
#include "texture_decode.h"
#include "model_cache.h"
#include "tool_commands.h"
#include "traversal.h"
#include <QDateTime>
#include "atmosphere_dialog.h"
#include "sun_sky_dialog.h"
#include "live_link_panel.h"
#include "devmap_launch.h"
#include "replay_log_panel.h"
#include "log_panel.h"
#include "script_panel.h"
#include "outliner_panel.h"
#include "toast_host.h"
#include "leak_check.h"
#include "map_queries.h"
#include "patch_tools.h"
#include "vfx_sim.h"
#include "acts_runner.h"
#include "clipboard_ops.h"
#include "material_editor.h"
#include "ies_browser.h"
#include "brush_ops.h"
#include "structure_generators.h"
#include "budget_panel.h"
#include "help_panel.h"
#include "key_replace.h"
#include "collision_world.h"
#include "spawn_generator.h"
#include "map_package.h"
#include "link_graph_view.h"
#include "autosave_history.h"
#include "prefab.h"
#include "prefab_bundle.h"
#include "region.h"
#include "compile_panel.h"
#include "grid_view.h"
#include "iw8_mapents.h"
#include "stock_spawns.h"
#include "stock_zone_edit.h"
#include "thumb_store.h"
#include "scripted_objects.h"
#include "shape_models.h"
#include "theme.h"
#include <QColorDialog>
#include <QListWidget>
#include "auto_caulk.h"
#include "upnp.h"
#include "web_server.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include "panels.h"
#include "settings.h"
#include "setup_wizard.h"

namespace iw8r::qt {

// Tool paths a worker thread needs to export stock decal volume assets (appendStockDecalVolumes).
struct StockDecalTools {
    QString acts, replay, oodle;
};
void appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out);


namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
constexpr int kDockLayoutVersion = 12;  // 12: Script, Outliner  // 11: Sounds  // 10: Images  // 9: Effects  // 8: Prefabs  // 7: Console  // 6: Live Link  // 3: Surface and Compile docks; 4: Replay Log; 5: Layers

// ACTS layout: <out>/mw19replay/<zone>/assets/map_ents/maps/mp/<map>.d3dbsp.0.asset.json
QString findMapEntsJson(const QString& dir) {
    QDirIterator it(dir, {"*.asset.json"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (f.contains("/map_ents/")) return f;
    }
    return {};
}
}  // namespace

MainWindow::MainWindow(ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent) : QMainWindow(parent), dev_(dev), dc_(dc) {
    setAcceptDrops(true);
    ctx_.doc.loadFromText("iwmap 4\r\n\"000_Global\" flags active\r\n\"The Map\" flags \r\n// entity 0\r\n{\r\n\"classname\" \"worldspawn\"\r\n}\r\n", "", nullptr);
    ctx_.cam.speed = Settings::instance().value("camera/speed", 600.0).toDouble();
    ctx_.cam.fov = Settings::instance().value("camera/fov", 90.0).toDouble();
    buildDocks();
    toasts_ = new ToastHost(this);
    connect(compile_, &CompilePanel::finished, toasts_, [this](bool ok) {
        toasts_->showToast(ok ? LogLevel::Info : LogLevel::Error, ok ? "Compile finished" : "Compile failed", "compile");
    });
    buildMenus();
    // Textured camera: custom materials, then exported stock images, then the ACTS material cache (fetched on demand).
    customMaterials_ = std::make_unique<MaterialLibrary>();
    matCache_ = new MaterialCache([this](const QString& k) { return toolPath(k); }, this);
    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) { camera_->materialImagesChanged(); });
    camera_->setToolPathSource([this](const QString& k) { return toolPath(k); });
    connect(matCache_, &MaterialCache::progress, this, [this](const QString& m) { ctx_.setStatus(m); });
    camera_->setCustomTintSource([this](const std::string& m) -> std::optional<std::array<double, 3>> {
        const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m) : nullptr;
        if (!cm || !cm->hasTint()) return std::nullopt;
        return std::array<double, 3>{cm->tint[0], cm->tint[1], cm->tint[2]};
    });
    camera_->setMaterialImages(
        [this](const std::string& m) -> std::optional<QString> {
            // A stock decal volume image ("\x05<image>": colour or mask, research 99): any catalogued image.
            if (!m.empty() && m[0] == '\x05') {
                const QString image = qs(m.substr(1));
                const QString f = imageCache_->cachedDds(image);
                if (!f.isEmpty()) return f;
                if (!imageCache_->lastError(image).isEmpty()) return QString();  // could not be exported
                imageCache_->request(image);
                return std::nullopt;
            }
            // A layer image of a layered stock material: "<material>\x01<L>" colour, "<material>\x02<L>" alpha + reveal.
            if (const size_t sep = m.find_first_of("\x01\x02"); sep != std::string::npos && sep > 0) {
                const QString base = qs(m.substr(0, sep));
                const auto e = matCache_->lookup(base);
                if (!e) return std::nullopt;
                if (e->failed) return QString();
                const auto imgs = matCache_->layerImages(base);
                const int L = std::atoi(m.c_str() + sep + 1);
                if (L == 0 && m[sep] == '\x02') return e->maskDds;  // a decal's layer 0 alpha + reveal: slot 27
                if (L < 0 || size_t(L) >= imgs.size()) return QString();
                return m[sep] == '\x01' ? imgs[size_t(L)].colorDds : imgs[size_t(L)].revealDds;
            }
            if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m) : nullptr) return qs(cm->colorImage);
            // The built-in glass of Make Glass Pane (owner report 2026-11-11: it showed the missing-material checker in
            // the camera): its image exists only inside a build folder, so the editor keeps its own copy.
            if (qs(m).compare("iw8r_glass", Qt::CaseInsensitive) == 0) {
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/builtin_materials";
                const QString png = dir + "/iw8r_builtin_glass_c.png";
                if (!QFileInfo(png).isFile()) {
                    QDir().mkpath(dir);
                    QImage img(128, 128, QImage::Format_RGBA8888);
                    for (int y = 0; y < 128; ++y)
                        for (int x = 0; x < 128; ++x) {
                            const double streak = 0.5 + 0.5 * std::sin((x + y * 0.35) * 0.049);  // the same sheen as the build's image
                            img.setPixel(x, y, qRgba(int(185 + 30 * streak), int(205 + 25 * streak), int(220 + 20 * streak), int(48 + 30 * streak)));
                        }
                    img.save(png);
                }
                return png;
            }
            const QString f = materials_->colorImageFile(qs(m));
            if (!f.isEmpty()) return f;
            if (auto e = matCache_->lookup(qs(m))) return e->failed ? QString() : e->colorDds;
            return std::nullopt;
        },
        [this](const std::string& m) {
            if (!m.empty() && m[0] == '\x05') return;  // decal volume images: requested by the lookup
            matCache_->request({qs(m.substr(0, m.find_first_of("\x01\x02")))});
        });
    camera_->setMaterialFacts([this](const std::string& m) -> std::optional<CameraView::StockMaterialFacts> {
        const auto e = matCache_->lookup(qs(m));
        if (!e || e->failed) return std::nullopt;
        CameraView::StockMaterialFacts f;
        // Decals: the blended world techsets (`_bad_` blend token; research 98), sortKey 10-17 on Speedball.
        const QString ts = e->techset.section('/', -1);
        if ((ts.contains("_bad_") || (e->sortKey >= 10 && e->sortKey <= 17 && (ts.startsWith("lit") || e->techset.startsWith("tw")))) &&
            (e->techset.startsWith("tw/") || e->techset.startsWith("twc/") || e->techset.startsWith("tw4/") || e->techset.startsWith("twc4/")))
            f.decalSortKey = std::max(1, e->sortKey);
        f.tint = e->tint;
        f.tintMasked = e->tintMasked;
        f.tintMaskFile = e->tintMaskDds.toStdString();
        if (auto c = matCache_->materialConstants(qs(m))) f.constants = *c;
        return f;
    });
    camera_->setMaterialLayers([this](const std::string& m) -> std::optional<StockLayeredMaterial> {
        const auto e = matCache_->lookup(qs(m));
        if (!e) return std::nullopt;
        if (auto c = matCache_->layerConstants(qs(m))) return *c;
        return StockLayeredMaterial{};
    });
    camera_->setMaterialMasks([this](const std::string& m) -> QString {
        if (auto e = matCache_->lookup(qs(m)); e && !e->failed) return e->maskDds;
        return {};
    });
    // 3.18: tags of selected stock models from the model cache's detail files (read once per model name).
    ctx_.scene.tagsFor = [this](const std::string& m) -> std::vector<Vec3> {
        auto it = tagCache_.find(m);
        if (it == tagCache_.end()) {
            std::vector<Vec3> tags;
            if (const auto d = modelCacheStock_->loadDetail(qs(m)))
                for (const XModelBone& b : d->bones)
                    if (isTag(b)) tags.push_back(Vec3{b.pos[0], b.pos[1], b.pos[2]});
            if (tags.empty() && !modelCacheStock_->lookup(qs(m))) return tags;  // not cached yet: ask again later
            it = tagCache_.emplace(m, std::move(tags)).first;
        }
        return it->second;
    };
    // 5.23: foliage (alpha-tested stock materials whose names read as plants) sways in the wind preview.
    ctx_.scene.foliageFor = [this](const std::string& m) -> bool {
        const auto e = matCache_->lookup(qs(m));
        return isFoliageMaterial(m, e && !e->failed ? e->techset.toStdString() : std::string());
    };
    // Research 66: stock nodraw materials (foliage *_shadowcaster meshes) only cast shadows; info carriers, lighting
    // proxy cards and distortion draw nothing.
    ctx_.scene.drawClassFor = [this](const std::string& m) -> int {
        const auto e = matCache_->lookup(qs(m));
        const MaterialDraw d = materialDrawFor(m, e && !e->failed ? e->techset.toStdString() : std::string(),
                                               e && !e->failed ? e->surfaceFlags : -1);
        return d == MaterialDraw::ShadowOnly ? 1 : d == MaterialDraw::Hidden ? 2 : 0;
    };
    // 3.17: stock blended and additive materials draw see-through in the textured view (blend mode from the techset).
    ctx_.scene.blendFor = [this](const std::string& m) -> int {
        // Custom materials (doc 21): their own "blend" key (decals, glass) draws see-through too.
        // 12.6: additive custom materials draw additive, multiply ones see-through.
        if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m) : nullptr)
            return cm->blendMode == 1 ? 2 : (cm->blend || cm->blendMode == 2) ? 1 : 0;
        if (qs(m).compare("iw8r_glass", Qt::CaseInsensitive) == 0) return 1;  // the built-in glass pane material
        const auto e = matCache_->lookup(qs(m));
        if (!e || e->failed) return 0;
        const MaterialBlend b = materialBlendFromTechset(e->techset.toStdString());
        return b == MaterialBlend::Blend ? 1 : b == MaterialBlend::Additive ? 2 : 0;
    };
    camera_->setEffectSource([this](const std::string& fx) -> std::shared_ptr<const VfxSimDef> {
        auto it = fxSims_.find(fx);
        if (it != fxSims_.end()) return it->second;
        std::shared_ptr<const VfxSimDef> out;
        const QString f = findVfxExport(qs(fx));
        std::string text;
        if (!f.isEmpty() && readFile(f.toStdString(), text)) {
            auto def = std::make_shared<VfxSimDef>();
            if (buildVfxSim(text, *def) && !def->emitters.empty()) out = def;
        }
        fxSims_[fx] = out;  // also remembers "no export" (cleared when Effect Info exports one)
        return out;
    });
    camera_->setProbeFolder([this] {
        const QString d = compile_ ? compile_->buildRoot() + "/probe_capture" : QString();
        return QFileInfo(d).isDir() ? d : QString();
    });
    camera_->setMaterialEmissive([this](const std::string& m) -> QString {
        if (auto e = matCache_->lookup(qs(m)); e && !e->failed) return e->emissiveDds;
        return {};
    });
    camera_->setMaterialNormals([this](const std::string& m) -> QString {
        // A layer of a layered stock material ("<material>\x01<L>"): that layer's normal map.
        if (const size_t sep = m.find('\x01'); sep != std::string::npos && sep > 0) {
            const auto imgs = matCache_->layerImages(qs(m.substr(0, sep)));
            const int L = std::atoi(m.c_str() + sep + 1);
            return L >= 0 && size_t(L) < imgs.size() ? imgs[size_t(L)].normalDds : QString();
        }
        if (auto e = matCache_->lookup(qs(m)); e && !e->failed) return e->normalDds;
        return {};
    });
    ctx_.scene.modelSource = [this](const Entity& e) -> std::shared_ptr<const ImportedModel> {
        const std::string* m = e.value("model");
        if (!m || m->empty()) return nullptr;
        // 3.18: LOD preview / collision LOD. Past the last LOD the placement is not drawn in game: shown as its
        // entity box. A LOD not cached yet is fetched; LOD0 draws meanwhile.
        if (const int want = modelCacheStock_ ? stockLodFor(e, *m) : 0; want != 0) {
            if (want < 0) return nullptr;
            const QString qn = qs(*m);
            if (modelCacheStock_->hasLod(qn, want)) {
                const std::string key = *m + "#lod" + std::to_string(want);
                if (auto it = modelCache_.find(key); it != modelCache_.end() && it->second) return it->second;
                if (auto loaded = modelCacheStock_->loadModelLod(qn, want)) {
                    auto im = std::make_shared<ImportedModel>(std::move(*loaded));
                    const double s = ctx_.scene.modelScale > 0 ? 1.0 / ctx_.scene.modelScale : 1.0;
                    for (auto& mesh : im->meshes)
                        for (auto& p : mesh.pos) p = p * s;
                    im->name = key;
                    modelCache_[key] = im;
                    return im;
                }
            } else {
                modelCacheStock_->requestLod(qn, want);
            }
        }
        if (auto it = modelCache_.find(*m); it != modelCache_.end()) return it->second;
        std::vector<std::string> roots;
        for (const QString& r : lookupModelRoots()) roots.push_back(r.toStdString());  // the placed bundles' models too
        std::shared_ptr<const ImportedModel> out;
        // Same lookup as the compiler: <root>/**/<name>.obj, .glb or .gltf.
        if (const std::string f = findCustomModelFile(roots, *m); !f.empty()) {
            auto im = std::make_shared<ImportedModel>();
            std::string err;
            if (importModelFile(f, *im, &err)) {
                // An imported model has OBJ texture coordinates (v = 0 at the bottom); the camera draws model meshes with
                // v = 0 at the top, as stock models have. Flip here, or the picture is upside down in the editor while
                // the built map is right (owner report 2026-11-11: grass cubes upside down in the viewport only).
                for (auto& mesh : im->meshes)
                    for (auto& t : mesh.uv) t[1] = 1.0 - t[1];
                out = im;
            }
        }
        if (!out && modelCacheStock_) {
            // Stock xmodel: the ACTS model cache (docs/research/23). Y-up inches from the GLB; pre-divided by the
            // custom-model scale so the scene's shared scale leaves stock models at 1.
            if (auto ent = modelCacheStock_->lookup(qs(*m))) {
                if (!ent->failed && !ent->glbPath.isEmpty()) {
                    // The cache's loader: meshes named after their xmodel materials (textured view), the winding the
                    // editor uses, shadow-only surfaces dropped (the same mesh the build gets).
                    if (auto loaded = modelCacheStock_->loadModel(qs(*m))) {
                        auto im = std::make_shared<ImportedModel>(std::move(*loaded));
                        const double s = ctx_.scene.modelScale > 0 ? 1.0 / ctx_.scene.modelScale : 1.0;
                        for (auto& mesh : im->meshes)
                            for (auto& p : mesh.pos) p = p * s;
                        im->name = *m;
                        out = im;
                    }
                }
            } else {
                modelCacheStock_->request({qs(*m)});
                return nullptr;  // drawn as a box until the cache answers (not remembered)
            }
        }
        modelCache_[*m] = out;
        return out;
    };
    ctx_.scene.modelScale = Settings::instance().value("assets/modelScale", 1.0).toDouble();
    modelCacheStock_ = new ModelCache([this](const QString& k) { return toolPath(k); }, this);
    {
        // Browser thumbnails from the ACTS caches (visible rows only; anything missing is requested and the row is
        // refreshed when its cache answers).
        const auto decodeFile = [](const QString& file) -> QImage {
            // Only the mip level a 96-texel thumbnail needs is read and decoded (texture_decode.h): decoding the top
            // level of every visible image froze the browser for seconds.
            app::Rgba8Image small;
            if (file.isEmpty() || !app::loadDdsPreview(file.toStdString(), 96, small)) return {};
            QImage q = QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).copy();
            // Thumbnails show colour only: the alpha of stock colour images is often specular, not coverage.
            return q.convertToFormat(QImage::Format_RGB32);
        };
        // Persistent thumbnails (thumb_store.h): one compact JPEG pack per browser, keyed by the source file's
        // signature, so a browser opens with its thumbnails instead of decoding every DDS / GLB again.
        const QString thumbDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/thumbs/";
        auto matThumbs = std::make_shared<ThumbStore>(thumbDir + "materials.pack");
        auto modelThumbs = std::make_shared<ThumbStore>(thumbDir + "models.pack");
        auto imageThumbs = std::make_shared<ThumbStore>(thumbDir + "images.pack");
        // 4.13: full packs (`--thumb-pack materials|models`, every buildable stock asset) are read only: a thumbnail
        // without any cached export and without an ACTS run.
        auto matFull = std::make_shared<ThumbStore>(thumbDir + "materials.full.pack");
        auto modelFull = std::make_shared<ThumbStore>(thumbDir + "models.full.pack");
        const auto cached = [](ThumbStore& store, const QString& name, const QString& source, const auto& make) -> QImage {
            const quint64 sig = ThumbStore::signatureOf(source);
            if (QImage hit = store.get(name, sig); !hit.isNull()) return hit;
            QImage img = make();
            if (!img.isNull()) store.put(name, sig, img);
            return img;
        };
        materials_->setThumbnailSource([this, decodeFile, matThumbs, matFull, cached](const QString& m, bool* pending) -> QImage {
            if (auto e = matCache_->lookup(m)) {
                if (e->failed) return {};
                if (!e->colorDds.isEmpty() && !QFileInfo::exists(e->colorDds))
                    if (QImage full = matFull->getAny(m); !full.isNull()) return full;
                return cached(*matThumbs, m, e->colorDds, [&] { return decodeFile(e->colorDds); });
            }
            if (QImage full = matFull->getAny(m); !full.isNull()) return full;
            matCache_->request({m});
            *pending = true;
            return {};
        });
        connect(matCache_, &MaterialCache::materialReady, materials_, [this] { materials_->refreshThumbnails(); });
        // Availability marks: what the caches tried and could not resolve is greyed with the reason.
        materials_->setStatusSource([this](const QString& m) -> QString {
            const auto e = matCache_->lookup(m);
            if (!e) return {};
            if (e->failed) return e->error.isEmpty() ? QString("the material was not found in any zone") : e->error;
            if (e->colorDds.isEmpty()) return "no colour image (" + e->error.left(200) + "); builds grey";
            return {};
        });
        models_->setStatusSource([this](const QString& name) -> QString {
            const auto e = modelCacheStock_->lookup(name);
            return e && e->failed ? (e->error.isEmpty() ? QString("ACTS could not export the model") : e->error) : QString();
        });
        models_->setThumbnailSource([this, modelThumbs, modelFull, cached](const QString& name, bool* pending) -> QImage {
            const auto e = modelCacheStock_->lookup(name);
            if (!e) {
                if (QImage full = modelFull->getAny(name); !full.isNull()) return full;
                modelCacheStock_->request({name});
                *pending = true;
                return {};
            }
            if (e->failed) return {};
            return cached(*modelThumbs, name, e->glbPath, [&] {
                const auto m = modelCacheStock_->loadModel(name);
                return m ? renderModelPreview(*m, 96) : QImage();
            });
        });
        connect(modelCacheStock_, &ModelCache::modelReady, models_, [this] { models_->refreshThumbnails(); });
        images_->setThumbnailSource([this, decodeFile, imageThumbs, cached](const QString& name, bool* pending) -> QImage {
            const QString dds = imageCache_->cachedDds(name);
            if (!dds.isEmpty()) return cached(*imageThumbs, name, dds, [&] { return decodeFile(dds); });
            if (!imageCache_->lastError(name).isEmpty()) return {};
            imageCache_->request(name);
            *pending = true;
            return {};
        });
        connect(imageCache_, &StockSkyCache::skyReady, images_, [this] { images_->refreshThumbnails(); });
    }
    connect(modelCacheStock_, &ModelCache::modelReady, this, [this](const QString& name) {
        modelCache_.erase(name.toStdString());
        tagCache_.erase(name.toStdString());
        // Model Info open on this model: show what the cache now has (LODs / bones after a refetch).
        if (auto* d = findChild<QDialog*>("modelInfoDialog"); d && d->isVisible() && d->windowTitle() == "Model Info: " + name)
            showModelInfo(name);
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
    });
    connect(modelCacheStock_, &ModelCache::progress, this, [this](const QString& m) { ctx_.setStatus(m); });
    connect(modelCacheStock_, &ModelCache::lodReady, this, [this](const QString&, int) {
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
    });
    connect(modelCacheStock_, &ModelCache::modelReady, this, [this](const QString& name) { lodDetail_.erase(name.toStdString()); });
    lodTimer_ = new QTimer(this);
    lodTimer_->setSingleShot(true);
    lodTimer_->setInterval(150);
    connect(lodTimer_, &QTimer::timeout, this, &MainWindow::refreshLodPreview);
    connect(&ctx_, &EditorContext::cameraChanged, this, [this] {
        const bool active = lodPreview_ || ctx_.scene.cameraMode == iw8r::app::CameraMode::Collision;
        if ((active || !lodSig_.empty()) && !lodTimer_->isActive()) lodTimer_->start();
    });
    {
        // Entity definitions (data/iw8_entity_defs.json) for Check Map.
        static EntityDefs defs;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("data/iw8_entity_defs.json")); ++up) d.cdUp();
        std::string err;
        if (defs.load(d.filePath("data/iw8_entity_defs.json").toStdString(), &err)) setCheckMapEntityDefs(&defs);
    }
    compile_->setStockStatusSource([this](const QString& m) -> QString {
        const auto e = matCache_->lookup(m);
        if (!e) return {};
        if (e->failed) return e->error.isEmpty() ? QString("not found in any zone") : e->error;
        return e->colorDds.isEmpty() ? "no colour image: " + e->error : QString();
    });
    compile_->setStockImageSource([this](const QString& m) -> QString {
        if (auto e = matCache_->lookup(m)) return e->failed ? QString() : e->colorDds;
        const QString f = materials_->colorImageFile(m);
        if (!f.isEmpty()) return f;
        matCache_->request({m});  // fetched in the background for the next build
        return {};
    });
    compile_->setStockNormalSource([this](const QString& m) -> QString {
        const auto e = matCache_->lookup(m);
        return e && !e->failed ? e->normalDds : QString();
    });
    // Reflection probes are captured from the camera renderer before every build (the default probe at the map centre
    // and each reflection_probe), so each probe has its own picture of its surroundings.
    compile_->setBeforeExtraction([this] {
        if (!camera_ || ctx_.doc.path().empty()) return;
        const int n = captureReflectionProbes();
        if (n > 0) ctx_.setStatus(QString("Captured %1 reflection probe(s) for the build").arg(n));
    });
    compile_->setStockSurfaceSource([this](const QString& m) -> qint64 {
        const auto e = matCache_->lookup(m);
        return e && !e->failed ? e->surfaceFlags : qint64(-1);
    });
    compile_->setStockBlendSource([this](const QString& m) {
        const auto e = matCache_->lookup(m);
        return e && !e->failed && materialBlendFromTechset(e->techset.toStdString()) == MaterialBlend::Blend;
    });
    compile_->setStockMaskSource([this](const QString& m) -> QString {
        const auto e = matCache_->lookup(m);
        return e && !e->failed ? e->maskDds : QString();
    });
    compile_->setStockModelSource([this](const QString& name) -> std::optional<ImportedModel> {
        // The model cache holds LOD0 as glTF Y-up inches, the same convention as OBJ files: the model writer rotates
        // it to Z-up itself (ModelExportOptions::zUp), so it is passed on unchanged.
        if (!modelCacheStock_) return std::nullopt;
        const auto e = modelCacheStock_->lookup(name);
        if (!e) {
            modelCacheStock_->request({name});
            return std::nullopt;
        }
        if (e->failed) return std::nullopt;
        return modelCacheStock_->loadModel(name);
    });
    {
        // Stock skies (doc 33): exported with ACTS into the material cache's images/ folder on first use.
        auto* skyCache = skyCache_ = new StockSkyCache([this](const QString& k) { return toolPath(k); }, matCache_->cacheRoot() + "/images", this);
        connect(skyCache, &StockSkyCache::skyReady, this, [this](const QString& image, const QString& file) {
            if (!pendingSunSky_.isEmpty() && !file.isEmpty() && QString::fromStdString(skyImageName(pendingSunSky_.toStdString())) ==
                                                                 QString::fromStdString(skyImageName(image.toStdString()))) {
                pendingSunSky_.clear();
                sunFromSky(true);
            }
        });
        // Sun from sky, automatically: when the worldspawn sky changes, the sun is aimed at the new sky's sun.
        connect(&ctx_, &EditorContext::documentChanged, this, [this] {
            QString sky;
            currentSkyFile(&sky);
            const Entity* ws = ctx_.doc.worldspawn();
            const std::string* sd = ws ? ws->value("sundirection") : nullptr;
            const QString sun = sd ? qs(*sd) : QString("");
            const bool sunChanged = !lastSunDirSeen_.isNull() && sun != lastSunDirSeen_;
            lastSunDirSeen_ = sun;
            if (sky == lastSky_) return;
            const bool first = lastSky_.isNull();
            lastSky_ = sky.isNull() ? QString("") : sky;
            // Only a fresh edit (it leaves nothing to redo): an undo or redo of a sky change must not add an edit. An
            // edit that set the sun itself (Time of Day presets) keeps its sun.
            if (first || sunChanged || sky.isEmpty() || ctx_.doc.canRedo() || !Settings::instance().value("sky/autoSun", true).toBool())
                return;
            sunFromSky(true);
        });
        connect(skyCache, &StockSkyCache::progress, this, [this](const QString& m) { ctx_.setStatus(m); });
        compile_->setSkySource([skyCache](const QString& sky) -> QString {
            const QString f = skyCache->cachedDds(sky);
            if (f.isEmpty()) skyCache->request(sky);
            return f;
        });        // Sky preview (3.8): the camera draws the worldspawn skyname from the same cache.
        camera_->setSkySource([this, skyCache]() -> QString {
            for (const auto& e : ctx_.doc.map().entities) {
                if (e.classname() != "worldspawn") continue;
                if (const std::string* f = e.value("iw8r_sky_file"); f && !f->empty()) {
                    const QString path = QFileInfo(qs(*f)).isAbsolute() ? qs(*f)
                                                                         : QFileInfo(qs(ctx_.doc.path())).absoluteDir().absoluteFilePath(qs(*f));
                    if (QFileInfo::exists(path)) return path;
                }
                for (const char* key : {"skyname", "sky"})
                    if (const std::string* v = e.value(key); v && !v->empty()) {
                        const QString f = skyCache->cachedDds(qs(*v));
                        if (f.isEmpty()) skyCache->request(qs(*v));
                        return f;
                    }
                break;
            }
            // A stock map's document names no sky: its world's own sky material (tw/sky_<map>) does.
            if (stockWorld_)
                for (const auto& m : stockWorld_->meshes) {
                    const QString mat = qs(m.material);
                    if (!mat.section(QChar('/'), -1).startsWith("sky_", Qt::CaseInsensitive)) continue;  // tw/sky_<map>, not skyline / skyscraper
                    const QString f = skyCache->cachedDds(mat);
                    if (f.isEmpty()) skyCache->request(mat);
                    return f;
                }
            // A stock world without sky surfaces (mp_shipment): the map's own catalogued sky image, sky_<map>.
            if (stockWorld_ && !referenceZone_.isEmpty() && [] { static const QStringList names = stockSkyNames(); return names; }().contains("sky_" + referenceZone_)) {
                const QString f = skyCache->cachedDds("sky_" + referenceZone_);
                if (f.isEmpty()) skyCache->request("sky_" + referenceZone_);
                return f;
            }
            return {};
        });
        connect(skyCache, &StockSkyCache::skyReady, camera_, [this] { camera_->update(); });
    }
    compile_->setAssetRoots([this] { return customMaterialRoots(); }, [this] { return customModelRoots(); },
                            [] { return Settings::instance().value("assets/modelScale", 1.0).toDouble(); });
    reloadCustomAssets();
    // misc_prefab entities draw their contents (files resolved next to the map or in Settings prefabRoots). A prefab
    // bundle resolves to the contents a build makes (its fallback when that is what is built), so it draws the same way.
    ctx_.scene.prefabSource = [this](const Entity& e) -> std::shared_ptr<const MapFile> {
        const std::string* m = e.value("model");
        return m ? loadPrefab(*m) : nullptr;
    };
    // Prefab bundles bring their own material and model folders: when a placement of a bundle comes or goes, the
    // material library and the model lookup of the open map are loaded again with those folders.
    connect(&ctx_, &EditorContext::documentChanged, this, [this] { bundleRootsMayHaveChanged(); });
    {
        // Toolbar: current IW8 material (used for new brushes and Shift+M).
        QToolBar* bar = addToolBar("Material");
        bar->setObjectName("materialToolbar");
        bar->addWidget(new QLabel(" Material ", bar));
        materialEdit_ = new QLineEdit(qs(ctx_.currentMaterial), bar);
        materialEdit_->setObjectName("materialEdit");
        materialEdit_->setMinimumWidth(320);
        materialEdit_->setToolTip("Current IW8 material for new brushes and Brush > Apply Current Material (Shift+M)");
        bar->addWidget(materialEdit_);
        connect(materialEdit_, &QLineEdit::editingFinished, this, [this] {
            const QString m = materialEdit_->text().trimmed();
            if (!m.isEmpty() && !m.contains(' ')) ctx_.currentMaterial = m.toStdString();
        });
        bar->addAction(actions_.value("ToggleClipper"));
        bar->addAction(actions_.value("CSGHollow"));
        bar->addAction(actions_.value("CSGSubtract"));
    }

    gridLabel_ = new QLabel(this);
    selLabel_ = new QLabel(this);
    entLabel_ = new QLabel(this);
    posLabel_ = new QLabel(this);
    for (QLabel* l : {gridLabel_, selLabel_, entLabel_, posLabel_}) statusBar()->addPermanentWidget(l);
    connect(&ctx_, &EditorContext::statusMessage, this, [this](const QString& s) { statusBar()->showMessage(s, 6000); });
    connect(&ctx_, &EditorContext::documentChanged, this, [this] {
        updateStatus();
        updateTitle();
    });
    connect(grid_, &GridView::mouseWorld, this, [this](double x, double y, double z) {
        posLabel_->setText(QString("%1 %2 %3").arg(qs(formatNumber(x)), qs(formatNumber(y)), qs(formatNumber(z))));
    });
    clock_.start();
    connect(&tick_, &QTimer::timeout, this, [this] {
        const double dt = std::clamp(clock_.restart() / 1000.0, 0.0, 0.1);
        if (ctx_.playing) camera_->tickPlay(dt);
        else ctx_.tickCamera(dt, QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier);
    });
    tick_.start(16);
    // Autosave a snapshot every 5 minutes while there are unsaved changes (never overwrites the document).
    connect(&autosave_, &QTimer::timeout, this, [this] {
        if (!ctx_.doc.modified()) return;
        QString msg;
        autosaveNow(&msg);
        ctx_.setStatus(msg);
    });
    if (const int m = Settings::instance().value("autosave/minutes", 5).toInt(); m > 0) autosave_.start(m * 60 * 1000);
    ctx_.documentEdited();
    resize(1600, 960);
    // Restore the mapper's window geometry and dock layout.
    const QByteArray geometry = Settings::instance().value("window/geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    // A layout saved by an older build (fewer docks) is not restored: ADS would leave the new docks
    // without a parent, so they could never be shown. Bump kDockLayoutVersion whenever a dock is added.
    const QByteArray state = Settings::instance().value("window/docks").toByteArray();
    if (!state.isEmpty() && !dock_->restoreState(state, kDockLayoutVersion)) ctx_.setStatus("Dock layout reset (new panels)");
    // ADS un-parents the widgets of non-current tabs by design; only a dock with no area at all is lost.
    for (ads::CDockWidget* d : dock_->dockWidgetsMap())
        if (!d->dockAreaWidget() && !d->isClosed()) dock_->addDockWidgetTabToArea(d, dock_->findDockWidget("Undo History")->dockAreaWidget());
}

void MainWindow::showDock(const QString& name) {
    if (ads::CDockWidget* d = dock_->findDockWidget(name)) {
        d->toggleView(true);
        d->setAsCurrentTab();
        d->raise();
    }
}

MainWindow::~MainWindow() = default;

// ------------------------------------------------------------------ layout

void MainWindow::buildDocks() {
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, false);
    dock_ = new ads::CDockManager(this);

    camera_ = new CameraView(&ctx_, dev_, dc_);
    connect(camera_, &CameraView::contextMenuAt, this, [this](const QPoint& global, const QPointF& local) {
        QMenu* m = buildCameraContextMenu(local);
        m->setAttribute(Qt::WA_DeleteOnClose);
        m->popup(global);
    });
    grid_ = new GridView(&ctx_);
    inspector_ = new EntityInspector(&ctx_);
    auto* history = new HistoryPanel(&ctx_);
    auto* spawns = new SpawnsPanel(&ctx_);
    materials_ = new AssetBrowser(&ctx_, AssetBrowser::Kind::Materials);
    models_ = new AssetBrowser(&ctx_, AssetBrowser::Kind::Models);
    effects_ = new AssetBrowser(&ctx_, AssetBrowser::Kind::Effects);
    images_ = new AssetBrowser(&ctx_, AssetBrowser::Kind::Images);
    sounds_ = new AssetBrowser(&ctx_, AssetBrowser::Kind::Sounds);
    imageCache_ = new StockSkyCache([this](const QString& k) { return toolPath(k); },
                                    QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/image_browser", this,
                                    true);
    connect(images_, &AssetBrowser::imageHighlighted, this, [this](const QString& n) { previewImage(n); });
    connect(imageCache_, &StockSkyCache::skyReady, this, [this](const QString& image, const QString&) {
        if (image == previewedImage_) previewImage(image);
    });
    connect(effects_, &AssetBrowser::effectChosen, this, [this](const QString& name) {
        // An fx_origin with a stock effect (6.6); the build plays it from the map script.
        const Vec3 at = placementPoint();
        ctx_.doc.addEntity({{"classname", "fx_origin"}, {"fx", name.toStdString()},
                            {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z + 8)}},
                           {}, "Place effect " + name.toStdString());
        ctx_.documentEdited("Placed fx_origin " + name + " (add a delay key to repeat it)");
    });
    connect(sounds_, &AssetBrowser::soundChosen, this, [this](const QString& name, bool loop) {
        // A sound_emitter with a stock alias (6.7); the build plays it from the map script (loops keep playing,
        // one-shots repeat every `delay` seconds).
        const Vec3 at = placementPoint();
        std::vector<std::pair<std::string, std::string>> kv = {
            {"classname", "sound_emitter"}, {"soundalias", name.toStdString()}, {"loop", loop ? "1" : "0"},
            {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z + 16)}};
        if (!loop) kv.push_back({"delay", "5"});
        ctx_.doc.addEntity(kv, {}, "Place sound " + name.toStdString());
        ctx_.documentEdited("Placed sound_emitter " + name + (loop ? " (loop)" : " (one-shot every 5 s; edit the delay key)"));
    });
    camera_->setAcceptDrops(true);
    grid_->setAcceptDrops(true);
    connect(camera_, &CameraView::assetHovered, this, [this](const QString& kind, const QString& name, const QPointF& pos) {
        if (kind == "prefabs") return prefabDragHover(name, pos);  // the prefab itself follows the cursor
        if (kind != "materials") return;
        SelRef hit{kNoId, kNoId};
        camera_->surfacePoint(pos, &hit);
        previewMaterialAt(name, hit);
    });
    connect(camera_, &CameraView::assetHoverEnded, this, [this] {
        endMaterialPreview(false);
        endPrefabPreview(false);
    });
    connect(camera_, &CameraView::assetDropped, this, [this](const QString& kind, const QString& name, bool loop, const QPointF& pos) {
        if (kind == "prefabs" && prefabDropKeepsPreview(name)) return;  // the previewed placement stays where it is
        SelRef hit{kNoId, kNoId};
        const std::optional<Vec3> at = camera_->surfacePoint(pos, &hit);
        if (!at && kind != "materials") return ctx_.setStatus("Drop onto a surface (nothing is under the cursor)");
        dropAsset(kind, name, loop, at.value_or(Vec3{}), hit);
    });
    connect(grid_, &GridView::assetDropped, this, [this](const QString& kind, const QString& name, bool loop, double h, double v) {
        if (kind == "materials") return ctx_.setStatus("Drop materials onto a surface in the camera view");
        Vec3 at;
        at[ctx_.axisH()] = h;
        at[ctx_.axisV()] = v;
        const AABB sel = ctx_.doc.selectionBounds();
        at[ctx_.axisD()] = sel.valid() ? sel.mins[ctx_.axisD()] : 0;
        dropAsset(kind, name, loop, at, {kNoId, kNoId});
    });
    connect(materials_, &AssetBrowser::materialChosen, this, [this](const QString& name) {
        ctx_.currentMaterial = name.toStdString();
        if (materialEdit_) materialEdit_->setText(name);
        // Radiant: picking a texture applies it to the selected brushes/patches.
        if (applyMaterialToSelection(ctx_, ctx_.currentMaterial)) ctx_.documentEdited("Applied " + name);
        else ctx_.setStatus("Current material " + name);
    });
    // Model preview (4.3): render the highlighted model from the ACTS model cache; request it when not exported yet.
    connect(models_, &AssetBrowser::modelHighlighted, this, [this](const QString& name) { previewModel(name); });
    connect(modelCacheStock_, &ModelCache::modelReady, this, [this](const QString& name) {
        if (name == previewedModel_) previewModel(name);
    });
    connect(models_, &AssetBrowser::modelChosen, this, [this](const QString& name) {
        const Vec3 at = placementPoint();  // the grid centre, or the drop point of a drag from the browser
        // Decor is a static model (misc_model), as in stock maps (doc 34); script_model is for scripted / gameplay objects.
        ctx_.doc.addEntity({{"classname", "misc_model"}, {"model", name.toStdString()},
                            {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z)}},
                           {}, "Place " + name.toStdString());
        ctx_.documentEdited("Placed misc_model (static) " + name);
    });

    auto makeDock = [this](const QString& title, QWidget* w, bool closable) {
        auto* d = new ads::CDockWidget(dock_, title);
        d->setObjectName(title);
        d->setWidget(w);
        d->setFeature(ads::CDockWidget::DockWidgetClosable, closable);
        return d;
    };
    auto* camDock = makeDock("Camera", camera_, false);
    auto* gridDock = makeDock("Grid", grid_, false);
    // The Entity dock: the prefab bundle panel above the entity inspector (main_window_prefabs.cpp).
    QWidget* entHost = makeEntityDockHost();
    auto* entDock = makeDock("Entity", entHost, true);
    auto* histDock = makeDock("Undo History", history, true);
    auto* spawnDock = makeDock("IW8 Spawns", spawns, true);
    auto* camArea = dock_->addDockWidget(ads::CenterDockWidgetArea, camDock);
    auto* gridArea = dock_->addDockWidget(ads::RightDockWidgetArea, gridDock, camArea);
    auto* entArea = dock_->addDockWidget(ads::BottomDockWidgetArea, entDock, gridArea);
    dock_->addDockWidgetTabToArea(makeDock("Surface", new SurfacePanel(&ctx_), true), entArea);
    layers_ = new LayersPanel(&ctx_);
    dock_->addDockWidgetTabToArea(makeDock("Layers", layers_, true), entArea);
    outliner_ = new OutlinerPanel(&ctx_);
    dock_->addDockWidgetTabToArea(makeDock("Outliner", outliner_, true), entArea);
    // 6.25: the link graph (target / targetname, script_linkto) as a node view.
    linkView_ = new LinkGraphView(&ctx_);
    dock_->addDockWidgetTabToArea(makeDock("Links", linkView_, true), entArea);
    entArea->setCurrentDockWidget(entDock);
    auto* bottomArea = dock_->addDockWidget(ads::BottomDockWidgetArea, histDock, camArea);
    dock_->addDockWidgetTabToArea(spawnDock, bottomArea);
    dock_->addDockWidgetTabToArea(makeDock("Materials", materials_, true), bottomArea);
    dock_->addDockWidgetTabToArea(makeDock("Models", models_, true), bottomArea);
    dock_->addDockWidgetTabToArea(makeDock("Effects", effects_, true), bottomArea);
    dock_->addDockWidgetTabToArea(makeDock("Images", images_, true), bottomArea);
    dock_->addDockWidgetTabToArea(makeDock("Sounds", sounds_, true), bottomArea);
    {
        // Prefabs: <map dir>/prefabs and the prefab folders (Preferences); double-click stamps one.
        prefabs_ = new PrefabBrowser(
            [this] {
                QStringList roots;
                if (!ctx_.doc.path().empty()) {
                    const QString dir = QFileInfo(qs(ctx_.doc.path())).absolutePath();
                    roots << dir + "/prefabs" << dir + "/map_source/prefabs";
                }
                roots << PrefabBrowser::searchRoots();
                return roots;
            },
            [this](const QString& f, QString* err) { return insertPrefab(f, err); });
        PrefabBrowser::setModelBounds([this](const std::string& m, double b[6]) {
            const auto e = modelCacheStock_ ? modelCacheStock_->lookup(qs(m)) : std::nullopt;
            if (!e || e->failed) return false;
            for (int k = 0; k < 6; ++k) b[k] = e->bounds[size_t(k)];
            return true;
        });
        // Prefab pictures draw the models' own meshes; a model that is not cached yet is fetched and its prefabs are
        // drawn again when it arrives (a few at a time, so opening the dock does not fetch thousands of models).
        PrefabBrowser::setModelMesh([this](const std::string& m) -> std::shared_ptr<const ImportedModel> {
            if (!modelCacheStock_) return nullptr;
            static std::map<std::string, std::shared_ptr<const ImportedModel>> meshes;
            if (const auto it = meshes.find(m); it != meshes.end()) return it->second;
            const auto e = modelCacheStock_->lookup(qs(m));
            if (!e) {
                modelCacheStock_->request({qs(m)});
                return nullptr;
            }
            std::shared_ptr<const ImportedModel> out;
            if (!e->failed)
                if (auto im = modelCacheStock_->loadModel(qs(m))) out = std::make_shared<const ImportedModel>(std::move(*im));
            if (meshes.size() > 400) meshes.clear();  // a bounded cache: thumbnails are drawn once per item
            meshes[m] = out;
            return out;
        });
        if (modelCacheStock_) {
            auto* again = new QTimer(this);
            again->setSingleShot(true);
            again->setInterval(3000);
            connect(again, &QTimer::timeout, this, [this] {
                if (prefabs_ && prefabs_->isVisible()) prefabs_->refreshIncomplete();
            });
            connect(modelCacheStock_, &ModelCache::modelReady, again, [again](const QString&) { again->start(); });
        }
        dock_->addDockWidgetTabToArea(makeDock("Prefabs", prefabs_, true), bottomArea);
    }
    compile_ = new CompilePanel(&ctx_, [this](const QString& k) { return toolPath(k); });
    compileDock_ = makeDock("Compile", compile_, true);
    dock_->addDockWidgetTabToArea(compileDock_, bottomArea);
    {
        // 1.16: the last build against its limits.
        auto* budget = budget_ = new BudgetPanel([this] { return compile_->buildRoot() + "/last_build_report.json"; });
        dock_->addDockWidgetTabToArea(makeDock("Build Budget", budget, true), bottomArea);
        connect(compile_, &CompilePanel::finished, budget, [budget](bool) { budget->refresh(); });
        connect(&ctx_, &EditorContext::documentChanged, budget, [budget, this] {
            static std::string lastPath;
            if (ctx_.doc.path() != lastPath) {  // another map: its own last build
                lastPath = ctx_.doc.path();
                budget->refresh();
            }
        });
    }
    replayLog_ = new ReplayLogPanel([this] { return QFileInfo(toolPath("replay")).absolutePath(); },
                                    [this] { return compile_ ? compile_->mapId() : QString(); });
    dock_->addDockWidgetTabToArea(makeDock("Replay Log", replayLog_, true), bottomArea);
    logPanel_ = new LogPanel();
    dock_->addDockWidgetTabToArea(makeDock("Console", logPanel_, true), bottomArea);
    scriptPanel_ = new ScriptPanel(&ctx_);
    // 7.13: double-clicked log lines (Compile and Replay Log) jump to the script line or select what they name.
    compile_->scriptLineJump = [this](const QString& line) { return jumpToScriptLine(line); };
    replayLog_->lineActivated = [this](const QString& line) {
        if (!jumpToScriptLine(line)) compile_->selectFromLogLine(line);
    };
    dock_->addDockWidgetTabToArea(makeDock("Script", scriptPanel_, true), bottomArea);
    {
        // Hot update (7.4): after an edit, send the selected script entities' placements to the running game.
        connect(&ctx_, &EditorContext::documentChanged, this, [this] {
            if (!liveLink_ || !liveLink_->hotUpdateEnabled()) return;
            std::set<Id> sent;
            for (const SelRef& r : ctx_.doc.selection()) {
                if (r.item != kNoId || !sent.insert(r.entity).second) continue;
                const Entity* e = ctx_.doc.findEntity(r.entity);
                if (!e) continue;
                const std::string cls = e->classname();
                const std::string* tn = e->value("targetname");
                const auto o = e->origin();
                if (o && cls == "fx_origin") {  // 7.7: replay the effect where it now stands (effects the map loaded)
                    const std::string* fx = e->value("fx");
                    HotFx h;
                    if (!fx || fx->empty()) continue;
                    h.effect = *fx;
                    h.origin[0] = o->x, h.origin[1] = o->y, h.origin[2] = o->z;
                    if (const std::string* a = e->value("angles"))
                        std::sscanf(a->c_str(), "%lf %lf %lf", &h.angles[0], &h.angles[1], &h.angles[2]);
                    h.play = true;
                    liveLink_->sendHotChannel(kHotFxDvar, qs(encodeHotFx({h}, liveLink_->nextHotSeq())), qs(*fx));
                    continue;
                }
                if (o && cls == "sound_emitter") {  // 7.7: the alias plays (or its loop moves) at the new spot
                    const std::string* al = e->value("soundalias");
                    if (!al || al->empty()) continue;
                    HotSound h;
                    h.alias = *al;
                    h.origin[0] = o->x, h.origin[1] = o->y, h.origin[2] = o->z;
                    const std::string* lp = e->value("loop");
                    h.loop = lp && *lp == "1";
                    liveLink_->sendHotChannel(kHotSndDvar, qs(encodeHotSound(h, liveLink_->nextHotSeq())), qs(*al));
                    continue;
                }
                if (!tn || tn->empty() || !o || (cls != "script_model" && cls != "script_origin")) continue;
                Vec3 ang;
                if (const std::string* a = e->value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
                liveLink_->sendHotUpdate(qs(*tn), *o, ang);
            }
        });
        // 7.9: worldspawn sundirection / suncolor edits are pushed to the running game (iw8r_sun) while hot update is on.
        connect(&ctx_, &EditorContext::documentChanged, this, [this] {
            if (!liveLink_ || !liveLink_->hotUpdateEnabled()) return;
            const Entity* ws = ctx_.doc.worldspawn();
            if (!ws) return;
            const std::string* d = ws->value("sundirection");
            const std::string* c = ws->value("suncolor");
            const std::string* l = ws->value("sunlight");
            const std::string key = (d ? *d : "") + "|" + (c ? *c : "") + "|" + (l ? *l : "");
            if (key == lastSunSent_ || !d) return;
            const bool first = lastSunSent_.empty();
            lastSunSent_ = key;
            if (first) return;  // the first look is the loaded map, not an edit
            // Not sent: the sun builtins crashed Replay MP when called from the map script (owner test 2026-11-10).
            ctx_.setStatus("Hot update: sun changes need a rebuild (changing the sun live crashed the game)");
        });
        auto* live = liveLink_ = new LiveLinkPanel([this] { return QFileInfo(toolPath("replay")).absolutePath(); },
                                       [this] { return compile_ ? compile_->mapId() : QString(); });
        live->setEditorCameraSource([this] { return ctx_.cam.pos; });
        live->setMapSource([this]() -> const MapFile* { return &ctx_.doc.map(); });
        live->setSelectionNameSource([this]() -> QString {
            for (const auto& sel : ctx_.doc.selection())
                if (const Entity* e = ctx_.doc.findEntity(sel.entity))
                    if (const std::string* tn = e->value("targetname"); tn && !tn->empty()) return QString::fromStdString(*tn);
            return {};
        });
        connect(live, &LiveLinkPanel::followCameraUpdate, this, [this](double x, double y, double z, double pitch, double yaw, double) {
            if (ctx_.playing) return;  // the play preview owns the camera
            ctx_.cam.pos = Vec3{x, y, z};
            ctx_.cam.pitch = std::clamp(-pitch, -89.0, 89.0);
            ctx_.cam.yaw = yaw;
            ctx_.cameraMoved();
        });
        connect(live, &LiveLinkPanel::moveEditorCameraRequested, this,
                [this](double x, double y, double z, double pitch, double yaw, double) {
                    ctx_.cam.pos = Vec3{x, y, z};
                    ctx_.cam.pitch = std::clamp(-pitch, -89.0, 89.0);  // game pitch is positive looking down
                    ctx_.cam.yaw = yaw;
                    ctx_.cameraMoved();
                    ctx_.setStatus("Editor camera moved to the in-game camera");
                });
        dock_->addDockWidgetTabToArea(makeDock("Live Link", live, true), bottomArea);
    }
    bottomArea->setCurrentDockWidget(histDock);
    dock_->setSplitterSizes(camArea, {960, 640});
    dock_->setSplitterSizes(bottomArea, {700, 220});
    dock_->setSplitterSizes(entArea, {560, 360});
    entDock->toggleViewAction()->setObjectName("toggleEntity");
    QTimer::singleShot(0, this, [this] { rebuildMainToolbar(); });  // after every menu command exists
    QTimer::singleShot(0, this, [this] {
        // 2.30 (core hotkey_presets, data/hotkey_presets.json): IW8 Radiant defaults, stock CoD Radiant, iw3xo.
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/hotkey_presets.json")); ++up) d.cdUp();
        QFile f(d.filePath("data/hotkey_presets.json"));
        std::vector<HotkeyPreset> presets;
        std::string err;
        if (!f.open(QIODevice::ReadOnly) || !loadHotkeyPresets(f.readAll().toStdString(), presets, &err)) return;
        QMenu* editMenu = nullptr;
        for (QAction* a : menuBar()->actions())
            if (a->menu() && a->text().remove('&') == "Edit") editMenu = a->menu();
        if (!editMenu) return;
        QMenu* hk = editMenu->addMenu("Hotkey Presets");
        hk->setObjectName("hotkeyPresetsMenu");
        for (const HotkeyPreset& p : presets) {
            hk->addAction(qs(p.name), this, [this, p] {
                // The preset's keys over the defaults; commands it does not list return to their default key.
                const HotkeyPreset* def = nullptr;
                (void)def;
                size_t n = 0;
                for (auto it = actions_.begin(); it != actions_.end(); ++it) {
                    const auto k = p.keys.find(it.key().toStdString());
                    if (k == p.keys.end()) continue;
                    const QKeySequence seq(qs(k->second));
                    Settings::instance().setShortcut(it.key(), seq);
                    it.value()->setShortcut(seq);
                    ++n;
                }
                ctx_.setStatus(QString("Hotkey preset %1: %2 command(s) rebound (Edit > Keyboard Shortcuts to adjust)").arg(qs(p.name)).arg(n));
            });
        }
    });
    defaultDockState_ = dock_->saveState(kDockLayoutVersion);
}

QAction* MainWindow::addCommand(QMenu* menu, const QString& command, const QString& text, const QKeySequence& def,
                                const std::function<void()>& fn) {
    auto* a = new QAction(text, this);
    a->setObjectName(command);
    a->setShortcut(Settings::instance().hasShortcut(command) ? Settings::instance().shortcut(command) : def);
    a->setShortcutContext(Qt::ApplicationShortcut);
    connect(a, &QAction::triggered, this, fn);
    if (menu) menu->addAction(a);
    addAction(a);
    actions_.insert(command, a);
    return a;
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    addCommand(file, "OpenInNewWindow", "Open in New Window...", QKeySequence(), [this] {
        // 2.28: another map side by side (its own editor process); Edit > Copy / Paste moves items between them.
        const QString f = QFileDialog::getOpenFileName(this, "Open in New Window", Settings::instance().value("lastDir").toString(),
                                                       "Maps (*.map *.ents *.iw8);;All files (*)");
        QStringList args;
        if (!f.isEmpty()) args << f;
        if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) ctx_.setStatus("Opened a new editor window" + (f.isEmpty() ? QString() : " with " + QFileInfo(f).fileName()));
    });
    addCommand(file, "NewMap", "&New Map", QKeySequence("Ctrl+N"), [this] {
        if (!confirmDiscard()) return;
        ctx_.scene.clear();
        prefabCache_.clear();
        std::string err;
        ctx_.doc.loadFromText("iwmap 4\r\n\"000_Global\" flags active\r\n\"The Map\" flags \r\n// entity 0\r\n{\r\n\"classname\" \"worldspawn\"\r\n}\r\n",
                              "", &err);
        displayName_ = "untitled.map";
        ctx_.documentEdited("New map");
        updateTitle();
    });
    addCommand(file, "Open", "&Open...", QKeySequence("Ctrl+O"), [this] { openDialog(); });
    addCommand(file, "OpenStockZone", "Open IW8 Stock &Map (ACTS)...", QKeySequence("Ctrl+Shift+O"), [this] { openStockZoneDialog(); });
    addCommand(file, "RelightStockMap", "Relight Stock Map (Fog / Vision)...", QKeySequence(), [this] { relightStockMapDialog(); });
    addCommand(file, "RelightStockSky", "Relight Stock Map Sky (another map's sky)...", QKeySequence(), [this] {
        // 11.6: worldspawn iw8r_sky_from; File > Rebuild Stock Zone carries that map's sky material and puts it on this
        // map's sky surfaces (converter replace_materials "@sky").
        const Entity* world = nullptr;
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.isWorldspawn()) {
                world = &e;
                break;
            }
        if (!world || stockZone_.isEmpty()) return ctx_.setStatus("Relight Sky: open a stock map first (File > Open IW8 Stock Map)");
        QStringList ids;
        {
            QDir d(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_stock_maps.tsv")); ++up) d.cdUp();
            QFile f(d.filePath("data/asset_catalog/iw8_stock_maps.tsv"));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QStringList c = in.readLine().split('\t');
                    if (c.size() < 7 || c[0].startsWith('#') || c[0] == "map_id" || c[6] != "yes" || c[0] == stockZone_) continue;
                    ids << c[0];
                }
            }
        }
        ids.prepend("(this map's own sky)");
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, "Relight Stock Map Sky", "Use the sky of:", ids, 0, false, &ok);
        if (!ok) return;
        if (pick.startsWith("(")) ctx_.doc.removeKey(world->id, "iw8r_sky_from");
        else ctx_.doc.setKeyValue(world->id, "iw8r_sky_from", pick.toStdString());
        ctx_.documentEdited(pick.startsWith("(") ? QString("The map keeps its own sky")
                                                 : "The sky of " + pick + " is used; File > Rebuild Stock Zone writes it (the sky's lighting stays)");
    });
    addCommand(file, "RelightStockSun", "Relight Stock Map Sun (Colour / Intensity / Direction)...", QKeySequence(), [this] {
        // 11.6: worldspawn iw8r_sun_color (linear), iw8r_sun_intensity and iw8r_sun_direction ("pitch yaw");
        // File > Rebuild Stock Zone writes them (a new direction bakes the sun shadow again).
        const Entity* world = nullptr;
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.isWorldspawn()) {
                world = &e;
                break;
            }
        if (!world || stockZone_.isEmpty()) return ctx_.setStatus("Relight Sun: open a stock map first (File > Open IW8 Stock Map)");
        double lin[3] = {1.0, 0.95, 0.9}, intensity = 8.6;  // about the stock suns (mp_shipment 1.05 0.99 0.92, 8.63)
        if (const std::string* c = world->value("iw8r_sun_color")) std::sscanf(c->c_str(), "%lf %lf %lf", &lin[0], &lin[1], &lin[2]);
        if (const std::string* i = world->value("iw8r_sun_intensity")) intensity = std::atof(i->c_str());
        const auto toS = [](double v) { return int(std::lround(std::pow(std::clamp(v, 0.0, 1.0), 1.0 / 2.2) * 255.0)); };
        const QColor picked = QColorDialog::getColor(QColor(toS(lin[0]), toS(lin[1]), toS(lin[2])), this, "Sun colour");
        if (!picked.isValid()) return;
        bool ok = false;
        const double in = QInputDialog::getDouble(this, "Relight Sun", "Sun intensity (stock: mp_shipment 8.6, mp_br_tut 30.7; the rebuild report shows the old value):", intensity, 0, 1000, 2, &ok);
        if (!ok) return;
        // The direction: "pitch yaw" in degrees as the worldspawn sundirection (negative pitch = above the horizon);
        // empty keeps the stock direction. The stock map's own sundirection is the starting text.
        QString dirText;
        if (const std::string* d = world->value("iw8r_sun_direction")) dirText = QString::fromStdString(*d);
        else if (const std::string* s = world->value("sundirection")) dirText = QString::fromStdString(*s).section(' ', 0, 1, QString::SectionSkipEmpty);
        QString direction;
        for (;;) {
            direction = QInputDialog::getText(this, "Relight Sun",
                                              "Sun direction \"pitch yaw\" in degrees (pitch -90 to -1, negative = above the horizon; "
                                              "stock mp_shipment -20.261 -15.4). Empty keeps the stock direction. A new direction "
                                              "bakes the sun shadow again when the zone is rebuilt:",
                                              QLineEdit::Normal, dirText, &ok).trimmed();
            if (!ok) return;
            double pitch = 0, yaw = 0;
            if (direction.isEmpty() || parseStockSunDirection(direction.toStdString(), pitch, yaw)) break;
            QMessageBox::warning(this, "Relight Sun", "\"" + direction + "\" is not \"pitch yaw\" with a pitch of -90 to -1.");
            dirText = direction;
        }
        const auto toL = [](int v) { return std::pow(v / 255.0, 2.2); };
        const std::string color = formatNumber(toL(picked.red())) + " " + formatNumber(toL(picked.green())) + " " + formatNumber(toL(picked.blue()));
        const Id worldId = world->id;  // the entity list may move while the keys change
        const bool hadDirection = world->value("iw8r_sun_direction") != nullptr;
        ctx_.doc.beginGroup("Relight Sun");
        ctx_.doc.setKeyValue(worldId, "iw8r_sun_color", color);
        ctx_.doc.setKeyValue(worldId, "iw8r_sun_intensity", formatNumber(in));
        if (!direction.isEmpty()) ctx_.doc.setKeyValue(worldId, "iw8r_sun_direction", direction.simplified().toStdString());
        else if (hadDirection) ctx_.doc.removeKey(worldId, "iw8r_sun_direction");
        ctx_.doc.endGroup();
        ctx_.documentEdited("Stock sun set (colour " + QString::fromStdString(color) + ", intensity " + QString::number(in) +
                            (direction.isEmpty() ? QString(", stock direction") : ", direction " + direction.simplified()) +
                            "); File > Rebuild Stock Zone writes it" +
                            (direction.isEmpty() ? QString() : QString(" and bakes the sun shadow again")));
    });
    addCommand(file, "EditStockSpawns", "Edit Compiled Spawns (stock map)", QKeySequence(), [this] { editStockSpawns(); });
    addCommand(file, "EditLootPoints", "Edit Loot Points (Warzone stock map)", QKeySequence(), [this] { editLootPoints(); });
    addCommand(file, "EditPlacedObjects", "Edit Placed Objects (Warzone buy stations)", QKeySequence(), [this] { editPlacedObjects(); });
    addCommand(file, "LoadStreamedWorld", "Load Streamed World Around Camera (Warzone maps)", QKeySequence(), [this] { loadStreamedWorldAroundCamera(); });
    addCommand(file, "RebuildStockEntities", "Rebuild Stock Map With These Entities...", QKeySequence(), [this] {
        // 6.10: the M2 path from the menu: the open stock entity text (iw8-ents / .ents) is rebuilt into the stock
        // zone (tools/m2_rebuild_mapents.ps1: ACTS export, entity swap, x64-zt rebuild, ACTS --test) under out/.
        QString zone = QFileInfo(qs(ctx_.doc.path())).completeBaseName();
        zone = zone.section('.', 0, 0);
        bool ok = false;
        zone = QInputDialog::getText(this, "Rebuild Stock Map", "Stock zone to rebuild with the open entities:", QLineEdit::Normal,
                                     zone.startsWith("mp_") ? zone : QString("mp_shipment"), &ok).trimmed();
        if (!ok || zone.isEmpty()) return;
        QString err;
        const QString work = rebuildStockEntities(zone, &err);
        ctx_.setStatus(work.isEmpty() ? "Rebuild Stock Map: " + err
                                      : "Rebuild Stock Map: rebuilding " + zone + " with the open entities (a few minutes; the status line reports the result)");
    });
    // Change report (11.7): entities added, removed, moved or edited between this map and another .map file;
    // the differences can be saved as a patch (JSON) and applied to another copy in one undo step.
    const auto compareWith = [this](const QString& other) {
        std::string text, err;
        Id next = 1;
        if (!readFile(other.toStdString(), text)) return ctx_.setStatus("Cannot read " + other);
        ParseResult pr = parseMap(text, next);
        if (!pr.map) return ctx_.setStatus("Cannot parse " + other);
        const auto changes = diffMapEntities(*pr.map, ctx_.doc.map());
        QString report = QString("From %1\nto the open map: %2 change(s)\n\n").arg(other).arg(changes.size()) + qs(formatMapDiff(changes));
        QDialog dlg(this);
        dlg.setObjectName("compareMapsDialog");
        dlg.setWindowTitle("Compare With Map");
        dlg.resize(720, 520);
        auto* lay = new QVBoxLayout(&dlg);
        auto* view = new QPlainTextEdit(report, &dlg);
        view->setReadOnly(true);
        lay->addWidget(view);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        QPushButton* save = box->addButton("Save Patch...", QDialogButtonBox::ActionRole);
        connect(save, &QPushButton::clicked, &dlg, [&] {
            const QString f = QFileDialog::getSaveFileName(&dlg, "Save Patch", QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/changes.iw8patch.json",
                                                           "Map patch (*.iw8patch.json *.json)");
            if (f.isEmpty()) return;
            QFile out(f);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(QByteArray::fromStdString(mapPatchJson(*pr.map, ctx_.doc.map())));
            ctx_.setStatus("Saved patch " + QFileInfo(f).fileName() + " (File > Apply Map Patch applies it to another copy)");
        });
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(box);
        dlg.exec();
    };
    addCommand(file, "CompareWithMap", "Compare With Map...", QKeySequence(), [this, compareWith] {
        const QString other = QFileDialog::getOpenFileName(this, "Compare With Map", QFileInfo(qs(ctx_.doc.path())).absolutePath(), "Maps (*.map *.bak)");
        if (!other.isEmpty()) compareWith(other);
    });
    // Owner playtest 2026-11-11: no .bak was at hand to compare with. The last save's backup sits next to the map.
    addCommand(file, "CompareWithBackup", "Compare With Last Saved Backup (.bak)", QKeySequence(), [this, compareWith] {
        if (ctx_.doc.path().empty()) return ctx_.setStatus("Compare With Backup: save the map first");
        const QString bak = qs(ctx_.doc.path()) + ".bak";
        if (!QFileInfo(bak).isFile())
            return ctx_.setStatus("Compare With Backup: no " + QFileInfo(bak).fileName() + " yet (it is written when a saved map is saved again)");
        compareWith(bak);
    });
    addCommand(file, "ExportOverview", "Export Overview Image (PNG)...", QKeySequence(), [this] {
        // 3.21 (core overview_image): top-down map image with spawns and objectives, e.g. for callout sheets.
        const QFileInfo mi(qs(ctx_.doc.path()));
        const QString f = QFileDialog::getSaveFileName(this, "Export Overview Image",
                                                       mi.absoluteDir().filePath((mi.completeBaseName().isEmpty() ? QString("map") : mi.completeBaseName()) + "_overview.png"),
                                                       "PNG images (*.png)");
        if (f.isEmpty()) return;
        bool ok = false;
        const int width = QInputDialog::getInt(this, "Export Overview Image", "Width in pixels:", 2048, 256, 8192, 256, &ok);
        if (!ok) return;
        OverviewOptions o;
        o.width = width;
        o.drawGrid = QMessageBox::question(this, "Export Overview Image", "Draw a 512-unit grid?") == QMessageBox::Yes;
        std::string err;
        const size_t bytes = exportOverviewPng(ctx_.doc.map(), o, f.toStdString(), &err);
        ctx_.setStatus(bytes ? QString("Exported %1 (%2 KB)").arg(QFileInfo(f).fileName()).arg(bytes / 1024) : "Overview: " + qs(err));
    });
    addCommand(file, "ExportCameraImage", "Export Camera Image (high resolution)...", QKeySequence(), [this] {
        const QFileInfo mi(qs(ctx_.doc.path()));
        const QString f = QFileDialog::getSaveFileName(this, "Export Camera Image",
                                                       mi.absoluteDir().filePath((mi.completeBaseName().isEmpty() ? QString("map") : mi.completeBaseName()) + "_shot.png"),
                                                       "PNG images (*.png);;JPEG images (*.jpg)");
        if (f.isEmpty()) return;
        const QStringList sizes = {"1920 x 1080", "2560 x 1440", "3840 x 2160", "7680 x 4320", "Viewport x 2", "Viewport x 4"};
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, "Export Camera Image", "Size:", sizes, 2, false, &ok);
        if (!ok) return;
        int w = 3840, h = 2160;
        if (pick.startsWith("Viewport")) {
            const int k = pick.endsWith('4') ? 4 : 2;
            w = camera_->width() * k, h = camera_->height() * k;
        } else {
            w = pick.section(" x ", 0, 0).toInt(), h = pick.section(" x ", 1, 1).toInt();
        }
        const bool clean = QMessageBox::question(this, "Export Camera Image",
                                                 "Game surfaces only (hide outlines, entity markers, tool faces and volumes)?") == QMessageBox::Yes;
        QString err;
        if (exportCameraImage(f, w, h, clean, &err)) ctx_.setStatus(QString("Exported %1 (%2 x %3)").arg(QFileInfo(f).fileName()).arg(w).arg(h));
        else ctx_.setStatus("Camera image: " + err);
    });
    addCommand(file, "ExportTurntable", "Export Turntable of Selection (PNG frames)...", QKeySequence(), [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Export Turntable", QFileInfo(qs(ctx_.doc.path())).absolutePath());
        if (dir.isEmpty()) return;
        bool ok = false;
        const int frames = QInputDialog::getInt(this, "Export Turntable", "Frames per turn:", 36, 4, 360, 1, &ok);
        if (!ok) return;
        QString err;
        const int n = exportTurntable(dir, frames, 1024, 1024, &err);
        ctx_.setStatus(n > 0 ? QString("Turntable: %1 frame(s) (1024 x 1024) in %2").arg(n).arg(dir) : "Turntable: " + err);
    });
    addCommand(file, "ExportMapPackage", "Export Map Package (.zip)...", QKeySequence(), [this] {
        // 4.26: the map with its prefabs, custom materials / models and sidecars (loadscreen, camera paths) in one zip.
        if (ctx_.doc.path().empty()) return ctx_.setStatus("Save the map first");
        const QFileInfo mi(qs(ctx_.doc.path()));
        const QString f = QFileDialog::getSaveFileName(this, "Export Map Package", mi.absoluteDir().filePath(mi.completeBaseName() + "_package.zip"),
                                                       "Zip packages (*.zip)");
        if (f.isEmpty()) return;
        QString err;
        const int n = exportMapPackage(f, &err);
        ctx_.setStatus(n > 0 ? QString("Exported %1 file(s) to %2%3").arg(n).arg(QFileInfo(f).fileName()).arg(err.isEmpty() ? QString() : " (" + err + ")")
                             : "Export package: " + err);
    });
    addCommand(file, "ImportMapPackage", "Import Map Package (.zip)...", QKeySequence(), [this] {
        const QString zip = QFileDialog::getOpenFileName(this, "Import Map Package", Settings::instance().value("lastDir").toString(), "Zip packages (*.zip)");
        if (zip.isEmpty()) return;
        const QString dest = QFileDialog::getExistingDirectory(this, "Extract the package into (an empty folder is best)", QFileInfo(zip).absolutePath());
        if (dest.isEmpty()) return;
        QString err;
        const QString map = importMapPackage(zip, dest, &err);
        if (map.isEmpty()) return ctx_.setStatus("Import package: " + err);
        openPath(map);
        ctx_.setStatus("Imported " + QFileInfo(zip).fileName() + " into " + dest);
    });
    addCommand(file, "ApplyMapPatch", "Apply Map Patch...", QKeySequence(), [this] {
        const QString f = QFileDialog::getOpenFileName(this, "Apply Map Patch", QFileInfo(qs(ctx_.doc.path())).absolutePath(), "Map patch (*.iw8patch.json *.json)");
        if (f.isEmpty()) return;
        std::string text, err;
        if (!readFile(f.toStdString(), text)) return ctx_.setStatus("Cannot read " + f);
        if (!applyMapPatch(ctx_.doc, text, &err)) return ctx_.setStatus("Patch not applied (nothing changed): " + qs(err));
        ctx_.scene.invalidate();
        ctx_.documentEdited("Applied " + QFileInfo(f).fileName() + " (one undo step)");
    });
    // The stock map with the document's entities, spawns, added models and hidden props, through the converter's
    // editzone (a few seconds; research 88). The Replay install is only read; the result is not installed.
    addCommand(file, "RebuildStockZoneFast", "Rebuild Stock Zone...", QKeySequence(), [this] { rebuildStockZoneFast(); });
    addCommand(file, "InstallStockMap", "Install Edited Stock Map Into Game...", QKeySequence(), [this] { installEditedStockMap(); });
    addCommand(file, "RestoreStockMap", "Restore Stock Map In Game...", QKeySequence(), [this] { restoreStockMap(); });
    addCommand(file, "HideStockProps", "Hide Stock Props In Selection", QKeySequence(), [this] { hideStockPropsInSelection(); });
    addCommand(file, "ShowStockProps", "Show All Stock Props", QKeySequence(), [this] { showAllStockProps(); });
    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (dump script, slow)...", QKeySequence(), [this] {
        // Runs in the background; the Console dock shows its log. The Replay install is only read.
        const QString work = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_rebuild/" + stockZone_ + "-" +
                             QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        QStringList args;
        QString err;
        if (!prepareStockRebuild(work, &args, &err)) {
            QMessageBox::warning(this, "Rebuild Stock Zone", err);
            return;
        }
        auto* proc = new QProcess(this);
        proc->setProgram("powershell");
        proc->setArguments(args);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::readyRead, this, [proc] {
            for (const QString& line : QString::fromLocal8Bit(proc->readAll()).split('\n', Qt::SkipEmptyParts))
                EditorLog::instance().add(LogLevel::Info, "stock", line.trimmed());
        });
        connect(proc, &QProcess::finished, this, [this, proc, work](int code) {
            ctx_.setStatus(code == 0 ? "Stock zone rebuilt and validated: " + work + "/out (not installed)"
                                     : QString("Stock zone rebuild failed (exit %1); see the Console").arg(code));
            proc->deleteLater();
        });
        proc->start();
        ctx_.setStatus("Rebuilding " + stockZone_ + " in " + work + " (Console shows progress)");
    });
    recentMenu_ = file->addMenu("Recent");
    rebuildRecentMenu();
    file->addSeparator();
    addCommand(file, "Save", "&Save", QKeySequence("Ctrl+S"), [this] { save(); });
    addCommand(file, "SaveAs", "Save &As...", QKeySequence("Ctrl+Shift+S"), [this] { saveAsDialog(false); });
    addCommand(file, "SaveCopy", "Save a &Copy...", QKeySequence(), [this] { saveAsDialog(true); });
    addCommand(file, "RecoverAutosave", "Autosave History / Recover...", QKeySequence(), [this] { showAutosaveHistory(); });
    file->addSeparator();
    // Prefabs: misc_prefab entities that reference another .map ("model" "prefabs/name.map").
    addCommand(file, "ImportTexture", "Import &Texture as Material...", QKeySequence(), [this] {
        const QString img = QFileDialog::getOpenFileName(this, "Import Texture", Settings::instance().value("lastDir").toString(),
                                                         "Images (*.png *.tga *.jpg *.jpeg *.bmp);;All files (*)");
        if (img.isEmpty()) return;
        QString stem = QFileInfo(img).completeBaseName().toLower();
        stem.replace(QRegularExpression("[^a-z0-9_]"), "_");
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Import Texture", "Material name:", QLineEdit::Normal, "custom/" + stem, &ok);
        if (!ok) return;
        QString err;
        if (!importTexture(img, name.trimmed(), &err)) QMessageBox::warning(this, "Import Texture", err);
    });
    addCommand(file, "ImportModel", "Import &Model (OBJ) and Place...", QKeySequence(), [this] {
        const QString obj = QFileDialog::getOpenFileName(this, "Import Model", Settings::instance().value("lastDir").toString(),
                                                         "Models (*.obj *.glb *.gltf);;Wavefront OBJ (*.obj);;glTF (*.glb *.gltf);;All files (*)");
        if (obj.isEmpty()) return;
        QString err;
        if (!importModel(obj, nullptr, &err)) QMessageBox::warning(this, "Import Model", err);
    });
    addCommand(file, "NewCustomMaterial", "New Custom Material...", QKeySequence(), [this] { editCustomMaterial(QString()); });
    addCommand(file, "EditCustomMaterial", "Edit Custom Material...", QKeySequence(), [this] {
        // The current material when it is a custom one, else a pick from the library.
        QString n = qs(ctx_.currentMaterial);
        if (!customMaterials_ || !customMaterials_->find(n.toStdString())) {
            QStringList names;
            if (customMaterials_)
                for (const std::string& m : customMaterials_->names()) names << qs(m);
            if (names.isEmpty()) return ctx_.setStatus("No custom materials yet: File > New Custom Material");
            bool ok = false;
            n = QInputDialog::getItem(this, "Edit Custom Material", "Material:", names, 0, false, &ok);
            if (!ok) return;
        }
        editCustomMaterial(n);
    });
    addCommand(file, "InsertPrefab", "Insert &Prefab...", QKeySequence(), [this] { insertPrefabDialog(); });
    addCommand(file, "ExplodePrefab", "E&xplode Selected Prefab", QKeySequence(), [this] { explodeSelectedPrefab(); });
    addCommand(file, "PrefabParameters", "Prefab Parameters (per-instance overrides)...", QKeySequence(), [this] { showPrefabParameters(); });
    // A prefab bundle: one folder with the contents, the behaviour and the mapper's own assets (docs/PREFAB_BUNDLES.md).
    addCommand(file, "SaveSelectionAsPrefab", "Save Selection As Prefab...", QKeySequence(), [this] { saveSelectionAsBundleDialog(); });
    addCommand(file, "SaveSelectionAsMapPrefab", "Save Selection As Plain .map Prefab...", QKeySequence(), [this] { saveSelectionAsMapPrefabDialog(); });
    addCommand(file, "OpenBundleFolder", "Open Bundle Folder of Selected Prefab", QKeySequence(), [this] { openSelectedBundleFolder(); });
    addCommand(file, "RotatePrefabStep", "Rotate Selected Prefab (its yaw step)", QKeySequence(), [this] {
        if (!rotateSelectedPrefabsByStep()) ctx_.setStatus("Rotate Prefab: select misc_prefab placements first");
    });
    addCommand(file, "ConvertPrefabMap", "Convert Plain .map Prefab To Bundle...", QKeySequence(), [this] {
        const QString f = QFileDialog::getOpenFileName(this, "Convert Prefab To Bundle", prefabSaveFolder(), "Prefab maps (*.map)");
        if (f.isEmpty()) return;
        QString err;
        const QString folder = convertPlainPrefabToBundle(f, &err);
        if (folder.isEmpty()) return void(QMessageBox::warning(this, "Convert Prefab To Bundle", err));
        ctx_.setStatus("Bundle written: " + QDir::toNativeSeparators(folder) + " (the .map file is kept)");
    });
    addCommand(file, "ExportPrefabZip", "Export Prefab Bundle As Zip...", QKeySequence(), [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, "Prefab bundle folder (.iw8prefab)", prefabSaveFolder());
        if (folder.isEmpty()) return;
        const QString zip = QFileDialog::getSaveFileName(this, "Export Prefab Bundle", folder + ".zip", "Zip (*.zip)");
        if (zip.isEmpty()) return;
        QString err;
        if (!exportPrefabBundleAsZip(folder, zip, &err)) return void(QMessageBox::warning(this, "Export Prefab Bundle", err));
        ctx_.setStatus("Prefab bundle exported: " + QDir::toNativeSeparators(zip));
    });
    addCommand(file, "ImportPrefabZip", "Import Prefab Bundles From Zip...", QKeySequence(), [this] {
        const QString zip = QFileDialog::getOpenFileName(this, "Import Prefab Bundles", QString(), "Zip (*.zip)");
        if (zip.isEmpty()) return;
        QString err;
        const QStringList bundles = importPrefabBundlesFromZip(zip, &err);
        if (bundles.isEmpty()) return void(QMessageBox::warning(this, "Import Prefab Bundles", err));
        if (prefabs_) prefabs_->refresh();
        ctx_.setStatus(QString("Imported %1 prefab bundle(s) into %2").arg(bundles.size()).arg(QDir::toNativeSeparators(prefabSaveFolder())));
    });
    addCommand(file, "MakePrefabUnique", "Make Selected Prefab Unique", QKeySequence(), [this] {
        QString err;
        if (!makeSelectedPrefabUnique(&err)) QMessageBox::warning(this, "Make Prefab Unique", err);
    });
    addCommand(file, "SaveSelected", "Save Selected...", QKeySequence(), [this] { saveSubsetDialog(false); });
    addCommand(file, "SaveRegion", "Save Region...", QKeySequence(), [this] { saveSubsetDialog(true); });
    file->addSeparator();
    addCommand(file, "Exit", "E&xit", QKeySequence(), [this] { close(); });

    QMenu* edit = menuBar()->addMenu("&Edit");
    addCommand(edit, "Preferences", "&Preferences...", QKeySequence(), [this] { showPreferences(); });
    QAction* undo = addCommand(edit, "Undo", "&Undo", QKeySequence("Ctrl+Z"), [this] {
        const QString n = ctx_.doc.canUndo() ? qs(ctx_.doc.history()[ctx_.doc.undoPosition() - 1].name) : QString();
        ctx_.documentEdited(ctx_.doc.undo() ? "Undo " + n : "Nothing to undo");
    });
    // 2.28: copy / cut / paste of map items through the system clipboard (iwmap text): between two editor windows
    // (File > Open in New Window), within one map, or to a text editor and back.
    addCommand(edit, "CopySelection", "&Copy", QKeySequence("Ctrl+C"), [this] { copySelection(false); });
    addCommand(edit, "CutSelection", "Cu&t", QKeySequence("Ctrl+X"), [this] { copySelection(true); });
    addCommand(edit, "PasteClipboard", "&Paste", QKeySequence("Ctrl+V"), [this] {
        QString err;
        const size_t n = pasteFromClipboard(&err);
        ctx_.setStatus(n ? QString("Pasted %1 item(s) in place (one undo step)").arg(n) : "Paste: " + err);
    });
    QAction* redo = addCommand(edit, "Redo", "&Redo", QKeySequence("Ctrl+Y"), [this] {
        const QString n = ctx_.doc.canRedo() ? qs(ctx_.doc.history()[ctx_.doc.undoPosition()].name) : QString();
        ctx_.documentEdited(ctx_.doc.redo() ? "Redo " + n : "Nothing to redo");
    });
    if (!Settings::instance().hasShortcut("Redo")) redo->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
    connect(edit, &QMenu::aboutToShow, this, [this, undo, redo] {
        undo->setEnabled(ctx_.doc.canUndo());
        redo->setEnabled(ctx_.doc.canRedo());
        undo->setText(ctx_.doc.canUndo() ? "&Undo " + qs(ctx_.doc.history()[ctx_.doc.undoPosition() - 1].name) : "&Undo");
        redo->setText(ctx_.doc.canRedo() ? "&Redo " + qs(ctx_.doc.history()[ctx_.doc.undoPosition()].name) : "&Redo");
    });
    connect(edit, &QMenu::aboutToHide, this, [undo, redo] {
        undo->setEnabled(true);
        redo->setEnabled(true);
    });
    edit->addSeparator();
    addCommand(edit, "CloneSelection", "&Clone", QKeySequence(Qt::Key_Space), [this] {
        Vec3 d;
        d[ctx_.axisH()] = ctx_.doc.grid();
        d[ctx_.axisV()] = -ctx_.doc.grid();
        ctx_.documentEdited(cloneSelection(d) ? "Cloned" : "Nothing selected");
    });
    addCommand(edit, "DeleteSelection", "&Delete", QKeySequence(Qt::Key_Backspace),
               [this] { ctx_.documentEdited(ctx_.doc.deleteSelection() ? "Deleted" : "Nothing selected"); });
    addCommand(edit, "UnSelectSelection", "D&eselect", QKeySequence(Qt::Key_Escape), [this] {
        ctx_.doc.deselectAll();
        ctx_.documentEdited();
    });
    addCommand(edit, "InvertSelection", "&Invert Selection", QKeySequence(Qt::Key_I), [this] {
        ctx_.doc.invertSelection();
        ctx_.documentEdited();
    });
    addCommand(edit, "SelectAllOfType", "Select All Of Type (class or material)", QKeySequence("Shift+A"), [this] {
        // Radiant: a selected point entity selects its class; otherwise the first selected material.
        std::vector<SelRef> refs;
        QString what;
        for (const auto& r : ctx_.doc.selection()) {
            const Entity* e = ctx_.doc.findEntity(r.entity);
            if (!e) continue;
            if (!r.item || !e->isWorldspawn()) {
                refs = findByClass(ctx_.doc.map(), e->classname());
                what = qs(e->classname());
            } else if (const std::string m = materialOfSelection(ctx_); !m.empty()) {
                refs = findByMaterial(ctx_.doc.map(), m, false);
                what = qs(m);
            }
            break;
        }
        if (refs.empty()) return ctx_.setStatus("Select an entity or a brush first");
        ctx_.doc.deselectAll();
        for (const auto& r : refs) ctx_.doc.select(r, true);
        ctx_.documentEdited(QString("Selected %1 x %2").arg(refs.size()).arg(what));
    });
    QMenu* selectBy = edit->addMenu("Select By Volume (selected brush = template)");
    const std::pair<const char*, SelectMode> modes[] = {{"Select Complete Tall", SelectMode::CompleteTall},
                                                         {"Select Partial Tall", SelectMode::PartialTall},
                                                         {"Select Touching", SelectMode::Touching},
                                                         {"Select Inside", SelectMode::Inside}};
    for (const auto& [label, mode] : modes) {
        QString cmd = QString(label).remove(' ');
        addCommand(selectBy, cmd, label, QKeySequence(), [this, mode = mode, label = QString(label)] {
            const size_t n = selectByTemplate(ctx_, mode);
            ctx_.documentEdited(QString("%1: %2 selected (the template brush was removed; Ctrl+Z restores it)").arg(label).arg(n));
        });
    }
    addCommand(edit, "SelectConnected", "Select Connected (target / targetname)", QKeySequence("Ctrl+E"), [this] {
        const size_t n = selectConnected(ctx_);
        ctx_.documentEdited(QString("%1 connected entities selected").arg(n));
    });
    addCommand(edit, "FindReplaceKeys", "Find / Replace &Key Values (models, keys)...", QKeySequence("Ctrl+Shift+F"), [this] {
        if (!keyDialog_) keyDialog_ = new KeyReplaceDialog(&ctx_, this);
        keyDialog_->refreshUsage();
        keyDialog_->show();
        keyDialog_->raise();
    });
    addCommand(edit, "SnapSelectionToGrid", "Snap Selection to Grid", QKeySequence("Ctrl+Shift+G"), [this] {
        ctx_.documentEdited(snapSelectionToGrid(ctx_) ? QStringLiteral("Selection snapped to the grid (one undo step)")
                                                     : QStringLiteral("Already on the grid"));
    });
    // Selection sets (8.7): name the current selection and bring it back later (kept per map for the session).
    addCommand(edit, "EnterPrefab", "Enter Prefab (edit selected misc_prefab)", QKeySequence("Ctrl+Shift+E"), [this] { enterPrefab(); });
    addCommand(edit, "LeavePrefab", "Leave Prefab", QKeySequence("Ctrl+Shift+Q"), [this] { leavePrefab(); });
    QMenu* selSets = edit->addMenu("Selection &Sets");
    addCommand(selSets, "SaveSelectionSet", "Save Selection As...", QKeySequence(), [this, selSets] {
        if (ctx_.doc.selection().empty()) return ctx_.setStatus("Nothing selected");
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Save Selection Set", "Name:", QLineEdit::Normal,
                                                   QString("Set %1").arg(selectionSets_.size() + 1), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        saveSelectionSet(name);
    });
    selectionSetsMenu_ = selSets->addMenu("Restore");
    addCommand(edit, "FindReplace", "&Find / Replace Materials...", QKeySequence("Ctrl+F"), [this] {
        if (!findDialog_) findDialog_ = new FindReplaceDialog(&ctx_, this);
        const std::string m = materialOfSelection(ctx_);
        if (!m.empty()) findDialog_->setFind(qs(m));
        findDialog_->setReplace(qs(ctx_.currentMaterial));
        findDialog_->refreshUsage();
        findDialog_->show();
        findDialog_->raise();
    });
    edit->addSeparator();
    QAction* vertex = addCommand(edit, "ToggleVertexMode", "&Vertex Mode (drag corners / control points)", QKeySequence(Qt::Key_V), [this] {
        ctx_.vertexMode = !ctx_.vertexMode;
        if (ctx_.vertexMode && ctx_.edgeMode) {  // vertex and edge modes are exclusive
            ctx_.edgeMode = false;
            if (QAction* e = action("ToggleEdgeMode")) e->setChecked(false);
        }
        if (QAction* a = action("ToggleVertexMode")) a->setChecked(ctx_.vertexMode);
        ctx_.documentEdited(ctx_.vertexMode ? QStringLiteral("Vertex mode: drag the handles of the selected brushes and patches in the 2D view")
                                            : QStringLiteral("Vertex mode off"));
    });
    vertex->setCheckable(true);
    // E is handled by GridView (grid focus only), which triggers this action.
    QAction* edgeToggle = addCommand(edit, "ToggleEdgeMode", "&Edge Mode (E: drag edge midpoints of selected brushes)", QKeySequence(), [this] {
        ctx_.edgeMode = !ctx_.edgeMode;
        if (ctx_.edgeMode && ctx_.vertexMode) {
            ctx_.vertexMode = false;
            if (QAction* v = action("ToggleVertexMode")) v->setChecked(false);
        }
        if (QAction* a = action("ToggleEdgeMode")) a->setChecked(ctx_.edgeMode);
        ctx_.documentEdited(ctx_.edgeMode ? QStringLiteral("Edge mode: drag an edge midpoint of the selected brushes in the 2D view")
                                          : QStringLiteral("Edge mode off"));
    });
    edgeToggle->setCheckable(true);
    QAction* terrainToggle = addCommand(edit, "ToggleTerrainBrush", "&Terrain Brush (LMB raises, Shift+LMB lowers the selected patch)", QKeySequence(), [this] {
        ctx_.terrainBrush = !ctx_.terrainBrush;
        if (QAction* a = action("ToggleTerrainBrush")) a->setChecked(ctx_.terrainBrush);
        ctx_.documentEdited(ctx_.terrainBrush ? QStringLiteral("Terrain brush: select a patch, then drag in the XY top view (Shift lowers)")
                                              : QStringLiteral("Terrain brush off"));
    });
    terrainToggle->setCheckable(true);
    // Terrain brush mode (exclusive) and radius (grid units, 1..32; no shortcut: [ and ] are Grid Smaller/Larger).
    auto* terrainModes = new QActionGroup(this);
    terrainModes->setExclusive(true);
    const struct TerrainModeCmd {
        const char* command;
        const char* text;
        EditorContext::TerrainBrushMode mode;
    } terrainModeCmds[] = {
        {"TerrainModeRaise", "Terrain Mode: &Raise / Lower (Shift lowers)", EditorContext::TerrainBrushMode::Raise},
        {"TerrainModeSmooth", "Terrain Mode: &Smooth", EditorContext::TerrainBrushMode::Smooth},
        {"TerrainModeFlatten", "Terrain Mode: &Flatten (to the point nearest the stroke start)", EditorContext::TerrainBrushMode::Flatten},
        {"TerrainModeNoise", "Terrain Mode: &Noise", EditorContext::TerrainBrushMode::Noise},
    };
    for (const TerrainModeCmd& m : terrainModeCmds) {
        QAction* a = addCommand(edit, m.command, m.text, QKeySequence(), [this, mode = m.mode] {
            ctx_.terrainBrushMode = mode;
            ctx_.documentEdited(QStringLiteral("Terrain brush mode: %1 (turn on Terrain Brush to paint)")
                                    .arg(mode == EditorContext::TerrainBrushMode::Raise     ? QStringLiteral("Raise")
                                         : mode == EditorContext::TerrainBrushMode::Smooth  ? QStringLiteral("Smooth")
                                         : mode == EditorContext::TerrainBrushMode::Flatten ? QStringLiteral("Flatten")
                                                                                            : QStringLiteral("Noise")));
        });
        a->setCheckable(true);
        a->setChecked(ctx_.terrainBrushMode == m.mode);
        terrainModes->addAction(a);
    }
    addCommand(edit, "TerrainRadiusUp", "Terrain Brush Radius &Larger", QKeySequence(), [this] {
        ctx_.terrainRadius = qMin(32.0, ctx_.terrainRadius + 1);
        ctx_.documentEdited(QStringLiteral("Terrain brush radius: %1 grid units").arg(ctx_.terrainRadius));
    });
    addCommand(edit, "TerrainRadiusDown", "Terrain Brush Radius &Smaller", QKeySequence(), [this] {
        ctx_.terrainRadius = qMax(1.0, ctx_.terrainRadius - 1);
        ctx_.documentEdited(QStringLiteral("Terrain brush radius: %1 grid units").arg(ctx_.terrainRadius));
    });

    QMenu* view = menuBar()->addMenu("&View");
    for (auto* d : dock_->dockWidgetsMap()) view->addAction(d->toggleViewAction());
    addCommand(view, "ResetLayout", "Reset Panel Layout", QKeySequence(), [this] {
        dock_->restoreState(defaultDockState_, kDockLayoutVersion);
        ctx_.setStatus("Panel layout reset");
    });
    {
        // Workspaces (9.13): named panel layouts (e.g. Layout, Lighting, Scripting), saved in the settings.
        QMenu* ws = view->addMenu("Workspaces");
        ws->setObjectName("workspacesMenu");
        auto refill = std::make_shared<std::function<void()>>();
        *refill = [this, ws, refill] {
            ws->clear();
            const QStringList names = Settings::instance().value("workspaces/names").toStringList();
            for (int i = 0; i < names.size(); ++i) {
                const QString n = names[i];
                QAction* a = ws->addAction(n, this, [this, n] {
                    const QByteArray st = Settings::instance().value("workspaces/state/" + n).toByteArray();
                    if (st.isEmpty() || !dock_->restoreState(st, kDockLayoutVersion))
                        return ctx_.setStatus("Workspace " + n + " was saved by an older version (new panels); save it again");
                    ctx_.setStatus("Workspace " + n);
                });
                if (i < 9) a->setShortcut(QKeySequence(QString("Alt+Shift+%1").arg(i + 1)));
            }
            if (!names.isEmpty()) ws->addSeparator();
            ws->addAction("Save Current Layout As...", this, [this, refill] {
                bool ok = false;
                const QString n = QInputDialog::getText(this, "Save Workspace", "Name (e.g. Layout, Lighting, Scripting):",
                                                        QLineEdit::Normal, QString(), &ok).trimmed();
                if (!ok || n.isEmpty() || n.contains('/')) return;
                QStringList names = Settings::instance().value("workspaces/names").toStringList();
                if (!names.contains(n)) names << n;
                Settings::instance().setValue("workspaces/names", names);
                Settings::instance().setValue("workspaces/state/" + n, dock_->saveState(kDockLayoutVersion));
                (*refill)();
                ctx_.setStatus("Saved workspace " + n + " (View > Workspaces; Alt+Shift+number)");
            });
            if (!names.isEmpty())
                ws->addAction("Delete Workspace...", this, [this, refill] {
                    QStringList names = Settings::instance().value("workspaces/names").toStringList();
                    bool ok = false;
                    const QString n = QInputDialog::getItem(this, "Delete Workspace", "Workspace:", names, 0, false, &ok);
                    if (!ok) return;
                    names.removeAll(n);
                    Settings::instance().setValue("workspaces/names", names);
                    Settings::instance().setValue("workspaces/state/" + n, QByteArray());
                    (*refill)();
                });
        };
        (*refill)();
    }
    addCommand(nullptr, "ViewEntityInfo", "Entity Inspector", QKeySequence(Qt::Key_N), [this] {
        if (auto* d = dock_->findDockWidget("Entity")) d->toggleView(d->isClosed());
    });
    view->addSeparator();
    addCommand(view, "FourViewLayout", "Four-View Layout (camera, top, front, side)", QKeySequence(), [this] { fourViewLayout(); });
    {
        // 5.15 / 6.18: heat maps in the top 2D view.
        QMenu* heat = view->addMenu("Heat Map (top view)");
        auto* group = new QActionGroup(this);
        const std::pair<const char*, const char*> modes[] = {{"HeatMapNone", "None"},
                                                             {"HeatMapLights", "Light Overlap (at the camera height; red = over 8 lights)"},
                                                             {"HeatMapSpawns", "Spawn Density (blue sparse, red crowded)"}};
        for (int i = 0; i < 3; ++i) {
            QAction* a = addCommand(heat, modes[i].first, modes[i].second, QKeySequence(), [this, i] {
                ctx_.heatMap = i;
                if (i != 0 && ctx_.gridAxis != 0) ctx_.gridAxis = 0;  // drawn in the XY top view
                ctx_.cameraMoved();
            });
            a->setCheckable(true);
            a->setChecked(i == 0);
            group->addAction(a);
        }
    }
    addCommand(view, "NextView", "Next 2D View (XY / XZ / YZ)", QKeySequence("Ctrl+Tab"), [this] {
        ctx_.gridAxis = (ctx_.gridAxis + 1) % 3;
        ctx_.cameraMoved();
    });
    addCommand(view, "CenterCameraOnSelection", "Center Camera On Selection", QKeySequence(Qt::Key_End), [this] { ctx_.frameSelection(); });
    addCommand(view, "HideSelected", "Hide Selected", QKeySequence(Qt::Key_H), [this] {
        for (const auto& r : ctx_.doc.selection()) ctx_.scene.hidden.insert(r.item != kNoId ? r.item : r.entity);
        ctx_.doc.deselectAll();
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("%1 hidden (Shift+H shows all)").arg(ctx_.scene.hidden.size()));
    });
    addCommand(view, "HideUnselected", "Hide Unselected", QKeySequence("Ctrl+H"), [this] {
        // Everything not selected (primitives and point entities) is hidden; worldspawn keys stay editable.
        size_t n = 0;
        for (const auto& e : ctx_.doc.map().entities) {
            bool hasPrim = false;
            for (const auto& it : e.items)
                if (it.isPrimitive()) {
                    hasPrim = true;
                    if (!ctx_.doc.isSelected({e.id, it.id})) n += ctx_.scene.hidden.insert(it.id).second;
                }
            if (!hasPrim && !e.isWorldspawn() && !ctx_.doc.isSelected({e.id, kNoId})) n += ctx_.scene.hidden.insert(e.id).second;
        }
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("%1 hidden (Shift+H shows all)").arg(n));
    });
    addCommand(view, "ShowHidden", "Show All Hidden", QKeySequence("Shift+H"), [this] {
        ctx_.scene.hidden.clear();
        ctx_.scene.invalidate();
        ctx_.documentEdited("All shown");
    });
    QMenu* filter = view->addMenu("Filter");
    const QList<QPair<QString, QString>> filters = {{"Pathnodes (node_*)", "node_"}, {"Triggers (trigger_*)", "trigger_"},
                                                     {"Script structs (script_struct*)", "script_struct"},
                                                     {"Script origins", "script_origin"}, {"Reflection probes", "reflection_probe"},
                                                     {"Spawn entities (mp_*spawn*)", "mp_"}};
    for (const auto& [label, prefix] : filters) {
        QAction* a = filter->addAction(label);
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this, prefix](bool hide) {
            auto& v = ctx_.scene.hiddenClassPrefixes;
            v.erase(std::remove(v.begin(), v.end(), prefix.toStdString()), v.end());
            if (hide) v.push_back(prefix.toStdString());
            ctx_.scene.invalidate();
            ctx_.documentEdited();
        });
    }
    QAction* spawnsToggle = addCommand(view, "ToggleCompiledSpawns", "Show IW8 Compiled Spawns", QKeySequence(), [this] {
        ctx_.showSpawns = !ctx_.showSpawns;
        ctx_.cameraMoved();
    });
    spawnsToggle->setCheckable(true);
    spawnsToggle->setChecked(true);
    QAction* gizmoToggle = addCommand(view, "ToggleGizmo", "Transform Gizmo (camera view)", QKeySequence(Qt::Key_G), [this] {
        ctx_.gizmo = !ctx_.gizmo;
        if (QAction* a = action("ToggleGizmo")) a->setChecked(ctx_.gizmo);
        Settings::instance().setValue("view/gizmo", ctx_.gizmo);
        ctx_.cameraMoved();
    });
    gizmoToggle->setCheckable(true);
    ctx_.gizmo = Settings::instance().value("view/gizmo", true).toBool();
    gizmoToggle->setChecked(ctx_.gizmo);
    addCommand(view, "PlayPreview", "&Play Preview (walk the map; Esc exits)", QKeySequence(Qt::Key_P), [this] {
        showDock("Camera");
        if (ctx_.playing) camera_->stopPlay();
        else camera_->startPlay();
    });
    // While playing, the camera owns the keyboard: editor shortcuts would otherwise eat W/A/S/D, C, N, Space.
    connect(&ctx_, &EditorContext::playModeChanged, this, [this](bool playing) {
        for (QAction* a : actions_)
            if (a->objectName() != "PlayPreview") a->setEnabled(!playing);
        if (QAction* p = action("PlayPreview")) p->setEnabled(!playing);  // Esc / P are handled by the camera itself
    });
    addCommand(view, "GizmoMode", "Gizmo: Move / Rotate", QKeySequence(Qt::Key_R), [this] {
        camera_->toggleGizmoMode();
        ctx_.setStatus(camera_->gizmoRotateMode() ? "Gizmo: rotate (drag a ring, 15 degree steps)" : "Gizmo: move");
    });
    {
        // Entity display (2.17), remembered between sessions.
        QMenu* entDisplay = view->addMenu("Entity &Display");
        auto& s = Settings::instance();
        ctx_.showEntityNames = s.value("view/entityNames", false).toBool();
        ctx_.showAngles = s.value("view/entityAngles", true).toBool();
        ctx_.showModelOrigins = s.value("view/modelOrigins", false).toBool();
        const struct {
            const char* id;
            const char* text;
            bool EditorContext::*flag;
            const char* key;
        } modes[] = {{"ShowEntityNames", "Names (targetname, model or class)", &EditorContext::showEntityNames, "view/entityNames"},
                     {"ShowEntityAngles", "Facing Arrows", &EditorContext::showAngles, "view/entityAngles"},
                     {"ShowModelOrigins", "Model Origins", &EditorContext::showModelOrigins, "view/modelOrigins"}};
        for (const auto& m : modes) {
            QAction* a = addCommand(entDisplay, m.id, m.text, QKeySequence(), [this, flag = m.flag, key = QString(m.key), id = QString(m.id)] {
                ctx_.*flag = !(ctx_.*flag);
                Settings::instance().setValue(key, ctx_.*flag);
                if (QAction* act = action(id)) act->setChecked(ctx_.*flag);
                if (grid_) grid_->update();
            });
            if (a) {
                a->setCheckable(true);
                a->setChecked(ctx_.*(m.flag));
            }
        }
    }
    {
        // Themes (9.2): applied live and remembered.
        QMenu* themes = view->addMenu("&Theme");
        auto* group = new QActionGroup(this);
        const QString current = Settings::instance().value("ui/theme", "Dark").toString();
        for (const QString& name : themeNames()) {
            QAction* a = themes->addAction(name, this, [this, name] {
                Settings::instance().setValue("ui/theme", name);
                applyTheme(name, QColor(Settings::instance().value("ui/accent").toString()));
                rebuildMainToolbar();  // the line icons follow the theme's text colour
                ctx_.setStatus("Theme: " + name);
            });
            a->setObjectName("Theme" + name);
            a->setCheckable(true);
            a->setChecked(name == current);
            group->addAction(a);
        }
        themes->addSeparator();
        themes->addAction("Accent Colour...", this, [this] {
            const QColor c = QColorDialog::getColor(QColor(Settings::instance().value("ui/accent", defaultAccent().name()).toString()), this,
                                                    "Accent Colour");
            if (!c.isValid()) return;
            Settings::instance().setValue("ui/accent", c.name());
            applyTheme(Settings::instance().value("ui/theme", "Dark").toString(), c);
        });
        addCommand(view, "CustomizeToolbar", "Customize Toolbar...", QKeySequence(), [this] {
            QDialog dlg(this);
            dlg.setWindowTitle("Customize Toolbar");
            auto* lay = new QVBoxLayout(&dlg);
            lay->addWidget(new QLabel("Commands on the main toolbar (drag to reorder):", &dlg));
            auto* list = new QListWidget(&dlg);
            list->setDragDropMode(QAbstractItemView::InternalMove);
            const QStringList chosen = Settings::instance().value("ui/toolbar", defaultToolbarCommands()).toStringList();
            QStringList order = chosen;
            QStringList all = actions_.keys();
            all.sort();
            for (const QString& id : all)
                if (!order.contains(id)) order << id;
            for (const QString& id : order) {
                QAction* a = actions_.value(id);
                if (!a || a->text().isEmpty()) continue;
                auto* item = new QListWidgetItem(a->text().remove('&'), list);
                item->setData(Qt::UserRole, id);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(chosen.contains(id) ? Qt::Checked : Qt::Unchecked);
            }
            lay->addWidget(list, 1);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dlg);
            connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dlg, [&dlg] { dlg.done(2); });
            lay->addWidget(buttons);
            dlg.resize(420, 520);
            const int r = dlg.exec();
            if (r == 2) Settings::instance().setValue("ui/toolbar", defaultToolbarCommands());
            else if (r == QDialog::Accepted) {
                QStringList ids;
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->checkState() == Qt::Checked) ids << list->item(i)->data(Qt::UserRole).toString();
                Settings::instance().setValue("ui/toolbar", ids);
            } else {
                return;
            }
            rebuildMainToolbar();
        });
    }
    {
        QAction* loc = addCommand(view, "GizmoLocal", "Gizmo: Local Space (moves follow the entity's angles)", QKeySequence("Shift+R"), [this] {
            ctx_.gizmoLocal = !ctx_.gizmoLocal;
            if (QAction* a = action("GizmoLocal")) a->setChecked(ctx_.gizmoLocal);
            ctx_.setStatus(ctx_.gizmoLocal ? "Gizmo: local space" : "Gizmo: world space");
            camera_->update();
        });
        if (loc) loc->setCheckable(true);
    }
    {
        QAction* nm = addCommand(view, "ToggleNormalMaps", "Normal Maps and Gloss (textured view)", QKeySequence(), [this] {
            ctx_.normalMaps = !ctx_.normalMaps;
            if (QAction* a = action("ToggleNormalMaps")) a->setChecked(ctx_.normalMaps);
            Settings::instance().setValue("view/normalMaps", ctx_.normalMaps);
            ctx_.setStatus(ctx_.normalMaps ? "Normal maps on" : "Normal maps off");
            camera_->materialImagesChanged();
        });
        ctx_.normalMaps = Settings::instance().value("view/normalMaps", true).toBool();
        if (nm) {
            nm->setCheckable(true);
            nm->setChecked(ctx_.normalMaps);
        }
    }
    addCommand(view, "GizmoScale", "Gizmo: Scale", QKeySequence(Qt::Key_T), [this] {
        camera_->toggleGizmoScale();
        ctx_.setStatus(camera_->gizmoScaleMode() ? "Gizmo: scale (drag an axis box; models scale uniformly)" : "Gizmo: move");
    });
    {
        // Saved lit preview brightness (calibrate against the game once; applies to every map).
        auto& st = Settings::instance();
        ctx_.scene.ambient = st.value("lit/ambient", ctx_.scene.ambient).toDouble();
        ctx_.scene.sunGain = st.value("lit/sunGain", ctx_.scene.sunGain).toDouble();
        ctx_.scene.lightGain = st.value("lit/lightGain", ctx_.scene.lightGain).toDouble();
        ctx_.scene.exposure = st.value("lit/exposure", ctx_.scene.exposure).toDouble();
    }
    {
        QAction* wp = addCommand(view, "WindPreview", "Wind Preview (foliage sways; worldspawn iw8r_wind)", QKeySequence(), [this] {
            ctx_.windPreview = !ctx_.windPreview;
            if (QAction* a = action("WindPreview")) a->setChecked(ctx_.windPreview);
            ctx_.setStatus(ctx_.windPreview ? "Wind preview on: foliage sways in the textured camera (preview only; research 56)"
                                            : "Wind preview off");
            ctx_.cameraMoved();
        });
        wp->setCheckable(true);
    }
    {
        QAction* bp = addCommand(view, "BehaviourPreview", "Behaviour Preview (spinning and bobbing models move)", QKeySequence(), [this] {
            ctx_.behaviourPreview = !ctx_.behaviourPreview;
            if (QAction* a = action("BehaviourPreview")) a->setChecked(ctx_.behaviourPreview);
            ctx_.scene.behaviourTime = ctx_.behaviourPreview ? 0.0 : -1.0;
            ctx_.scene.invalidate();
            ctx_.setStatus(ctx_.behaviourPreview ? "Behaviour preview on: models with iw8r_spin or iw8r_bob move in the camera as the map script moves them in game"
                                                 : "Behaviour preview off");
            ctx_.cameraMoved();
        });
        bp->setCheckable(true);
    }
    addCommand(view, "LitPreviewBrightness", "Lit Preview Brightness...", QKeySequence(), [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Lit Preview Brightness");
        auto* form = new QFormLayout(&dlg);
        auto* note = new QLabel("Match the camera to the game by eye: shadowed areas (ambient), sunlit faces (sun) and light "
                                "entities (lights). Saved for every map; the build is not affected.", &dlg);
        note->setWordWrap(true);
        form->addRow(note);
        auto spin = [&](double v, double max) {
            auto* s = new QDoubleSpinBox(&dlg);
            s->setRange(0, max);
            s->setSingleStep(0.05);
            s->setDecimals(2);
            s->setValue(v);
            return s;
        };
        QDoubleSpinBox* amb = spin(ctx_.scene.ambient, 2);
        QDoubleSpinBox* sun = spin(ctx_.scene.sunGain, 3);
        QDoubleSpinBox* lig = spin(ctx_.scene.lightGain, 4);
        QDoubleSpinBox* exp = spin(ctx_.scene.exposure, 4);
        form->addRow("Ambient (shadowed faces)", amb);
        form->addRow("Sun", sun);
        form->addRow("Light entities", lig);
        form->addRow("Exposure (overall brightness, like the game's)", exp);
        auto apply = [this, amb, sun, lig, exp] {
            ctx_.scene.ambient = amb->value();
            ctx_.scene.sunGain = sun->value();
            ctx_.scene.lightGain = lig->value();
            ctx_.scene.exposure = exp->value();
            ctx_.scene.invalidate();
            ctx_.cameraMoved();
        };
        for (QDoubleSpinBox* s : {amb, sun, lig, exp}) connect(s, &QDoubleSpinBox::valueChanged, &dlg, apply);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dlg);
        connect(box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dlg, [amb, sun, lig, exp] {
            amb->setValue(0.55), sun->setValue(0.75), lig->setValue(0.6), exp->setValue(1.5);
        });
        connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(box);
        const double a0 = ctx_.scene.ambient, s0 = ctx_.scene.sunGain, l0 = ctx_.scene.lightGain, e0 = ctx_.scene.exposure;
        if (dlg.exec() == QDialog::Accepted) {
            auto& st = Settings::instance();
            st.setValue("lit/ambient", ctx_.scene.ambient);
            st.setValue("lit/sunGain", ctx_.scene.sunGain);
            st.setValue("lit/lightGain", ctx_.scene.lightGain);
            st.setValue("lit/exposure", ctx_.scene.exposure);
            ctx_.setStatus("Lit preview brightness saved");
        } else {
            ctx_.scene.ambient = a0, ctx_.scene.sunGain = s0, ctx_.scene.lightGain = l0, ctx_.scene.exposure = e0;
            ctx_.scene.invalidate();
            ctx_.cameraMoved();
        }
    });
    QAction* litToggle = addCommand(view, "ToggleLitPreview", "Lit Preview (sun + light entities)", QKeySequence("Ctrl+L"), [this] {
        ctx_.scene.litPreview = !ctx_.scene.litPreview;
        if (QAction* a = action("ToggleLitPreview")) a->setChecked(ctx_.scene.litPreview);
        ctx_.cameraMoved();
    });
    litToggle->setCheckable(true);
    QAction* gameToggle = addCommand(view, "ToggleGameLighting", "Game Lighting (as the built map looks in game)", QKeySequence("Ctrl+Alt+L"), [this] {
        // research 95 "Editor maps in game": the converter's world shader, the editor's lightmap bake (background,
        // again after edits), the game's sun / lights / exposure / tone map.
        ctx_.scene.gameLook = !ctx_.scene.gameLook;
        if (QAction* a = action("ToggleGameLighting")) a->setChecked(ctx_.scene.gameLook);
        ctx_.cameraMoved();
    });
    gameToggle->setCheckable(true);
    addCommand(view, "StockReference", "Stock Map Reference...", QKeySequence(), [this] {
        const QDir zoneDir(QFileInfo(toolPath("replay")).absolutePath() + "/zone");
        QStringList zones;
        for (const QString& f : zoneDir.entryList({"mp_*.ff"}, QDir::Files, QDir::Name)) zones << QFileInfo(f).completeBaseName();
        if (zones.isEmpty()) {
            ctx_.setStatus("No mp_*.ff zones in " + zoneDir.absolutePath() + " (set the Replay exe in Compile settings)");
            return;
        }
        bool ok = false;
        const int cur = std::max(0, int(zones.indexOf(referenceZone_.isEmpty() ? QString("mp_shipment") : referenceZone_)));
        const QString zone = QInputDialog::getItem(this, "Stock Map Reference",
                                                   "Zone (first use exports gfx_map through ACTS, about 10 s and a few hundred MB on C:):",
                                                   zones, cur, false, &ok);
        if (ok && !zone.isEmpty()) loadStockReference(zone);
    });
    addCommand(view, "ClearStockReference", "Clear Stock Map Reference", QKeySequence(), [this] {
        referenceZone_.clear();
        camera_->setReference({});
        camera_->setReferenceInstances({});
        camera_->setReferenceTexInstances({}, {});
        camera_->setReferenceTextured({});
        camera_->setGameStockMap("");
        camera_->setStockLightmap(nullptr);
        camera_->setStockProbes(nullptr);
        ctx_.referencePlans.clear();
        stockWorld_.reset();
        referenceProps_.clear();
        refProps_.reset();
        refBaseTris_.reset();
        referencePropMeshes_ = 0;
        ctx_.cameraMoved();
        ctx_.setStatus("Stock map reference cleared");
    });
    addCommand(view, "ExportReferenceRegion", "Stock Reference to Model (selection bounds)", QKeySequence(), [this] {
        exportReferenceRegion();
    });
    addCommand(view, "ExportReferenceBrushes", "Stock Reference to Brushes (selection bounds)", QKeySequence(), [this] {
        exportReferenceBrushes();
    });
    QAction* texToggle = addCommand(view, "ToggleTexturedView", "Textured Camera View", QKeySequence(), [this] {
        ctx_.texturedView = !ctx_.texturedView;
        if (QAction* a = action("ToggleTexturedView")) a->setChecked(ctx_.texturedView);
        Settings::instance().setValue("view/textured", ctx_.texturedView);
        // Keeps View > Camera Mode in step: on = Textured, off = Flat.
        ctx_.scene.cameraMode = ctx_.texturedView ? iw8r::app::CameraMode::Textured : iw8r::app::CameraMode::Flat;
        if (QAction* m = action(ctx_.texturedView ? "CameraModeTextured" : "CameraModeFlat")) m->setChecked(true);
        ctx_.cameraMoved();
    });
    texToggle->setCheckable(true);
    // View > Camera Mode: exclusive shading modes. Lit Preview (Ctrl+L) stays a separate toggle.
    {
        QMenu* bm = view->addMenu("Camera Bookmarks");
        bm->setObjectName("cameraBookmarksMenu");
        for (int i = 1; i <= 9; ++i)
            addCommand(bm, QString("CameraBookmarkGo%1").arg(i), QString("Go to Bookmark %1").arg(i), QKeySequence(QString("Ctrl+%1").arg(i)),
                       [this, i] {
                           if (!goToCameraBookmark(i)) ctx_.setStatus(QString("Camera bookmark %1 is empty (Ctrl+Shift+%1 saves it)").arg(i));
                       });
        bm->addSeparator();
        for (int i = 1; i <= 9; ++i)
            addCommand(bm, QString("CameraBookmarkSet%1").arg(i), QString("Save Bookmark %1").arg(i),
                       QKeySequence(QString("Ctrl+Shift+%1").arg(i)), [this, i] { saveCameraBookmark(i); });
    }
    {
        // Camera fly-through (8.15): add keys from the current view, play at constant speed, save beside the map.
        QMenu* cp = view->addMenu("Camera Path");
        cp->setObjectName("cameraPathMenu");
        addCommand(cp, "CameraPathAddKey", "Add Key at Camera", QKeySequence("Ctrl+Alt+K"), [this] {
            CameraKey k;
            k.pos = ctx_.cam.pos;
            k.yaw = ctx_.cam.yaw;
            k.pitch = ctx_.cam.pitch;
            camPath_.keys.push_back(k);
            retimeByDistance(camPath_, Settings::instance().value("camera/pathSpeed", 300.0).toDouble());
            ctx_.setStatus(QString("Camera path: %1 key(s), %2 s").arg(camPath_.keys.size()).arg(cameraPathDuration(camPath_), 0, 'f', 1));
        });
        addCommand(cp, "CameraPathPlay", "Play / Stop", QKeySequence("Ctrl+Alt+P"), [this] {
            if (camPathTimer_.isActive()) {
                camPathTimer_.stop();
                return ctx_.setStatus("Camera path stopped");
            }
            if (camPath_.keys.size() < 2) return ctx_.setStatus("Camera path: add two or more keys first (Ctrl+Alt+K)");
            camPathClock_.start();
            camPathTimer_.start(16);
            ctx_.setStatus(QString("Playing camera path (%1 s)").arg(cameraPathDuration(camPath_), 0, 'f', 1));
        });
        QAction* loop = addCommand(cp, "CameraPathLoop", "Loop", QKeySequence(), [this] {
            camPath_.loop = !camPath_.loop;
            if (QAction* a = action("CameraPathLoop")) a->setChecked(camPath_.loop);
        });
        loop->setCheckable(true);
        addCommand(cp, "CameraPathSpeed", "Speed...", QKeySequence(), [this] {
            bool ok = false;
            const double v = QInputDialog::getDouble(this, "Camera Path", "Speed (units per second):",
                                                     Settings::instance().value("camera/pathSpeed", 300.0).toDouble(), 10, 5000, 0, &ok);
            if (!ok) return;
            Settings::instance().setValue("camera/pathSpeed", v);
            retimeByDistance(camPath_, v);
        });
        addCommand(cp, "CameraPathClear", "Clear", QKeySequence(), [this] {
            camPathTimer_.stop();
            camPath_ = CameraPath{};
            ctx_.setStatus("Camera path cleared");
        });
        cp->addSeparator();
        addCommand(cp, "CameraPathSave", "Save Beside Map", QKeySequence(), [this] {
            const QString f = camPathFile();
            if (f.isEmpty()) return ctx_.setStatus("Save the map first");
            QFile out(f);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return ctx_.setStatus("Cannot write " + f);
            out.write(QByteArray::fromStdString(cameraPathsToJson({camPath_})));
            ctx_.setStatus("Saved " + QFileInfo(f).fileName());
        });
        addCommand(cp, "CameraPathLoad", "Load From Map Folder", QKeySequence(), [this] {
            std::string text, err;
            std::vector<CameraPath> paths;
            if (camPathFile().isEmpty() || !readFile(camPathFile().toStdString(), text) || !cameraPathsFromJson(text, paths, &err) || paths.empty())
                return ctx_.setStatus("No camera path saved for this map" + (err.empty() ? QString() : ": " + qs(err)));
            camPath_ = paths.front();
            if (QAction* a = action("CameraPathLoop")) a->setChecked(camPath_.loop);
            ctx_.setStatus(QString("Camera path loaded: %1 key(s)").arg(camPath_.keys.size()));
        });
        addCommand(cp, "CameraPathExportFrames", "Export Frames (PNG sequence)...", QKeySequence(), [this] {
            if (camPath_.keys.size() < 2) return ctx_.setStatus("Camera path: add at least two keys first");
            const QString dir = QFileDialog::getExistingDirectory(this, "Export Camera Path Frames", QFileInfo(qs(ctx_.doc.path())).absolutePath());
            if (dir.isEmpty()) return;
            bool ok = false;
            const int fps = QInputDialog::getInt(this, "Export Frames", "Frames per second:", 30, 1, 120, 1, &ok);
            if (!ok) return;
            QString err;
            const int n = exportCameraPathFrames(dir, fps, 1920, 1080, &err);
            ctx_.setStatus(n > 0 ? QString("Exported %1 frame(s) (1920 x 1080) to %2").arg(n).arg(dir) : "Export frames: " + err);
        });
        connect(&camPathTimer_, &QTimer::timeout, this, [this] {
            const double t = camPathClock_.elapsed() / 1000.0, d = cameraPathDuration(camPath_);
            stepCameraPath(t);
            if (!camPath_.loop && t >= d) camPathTimer_.stop();
        });
    }
    QMenu* camMode = view->addMenu("Camera Mode");
    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    struct CamMode {
        const char* command;
        const char* text;
        iw8r::app::CameraMode mode;
    };
    const CamMode camModes[] = {{"CameraModeTextured", "Textured", iw8r::app::CameraMode::Textured},
                                {"CameraModeFlat", "Flat (shaded faces)", iw8r::app::CameraMode::Flat},
                                {"CameraModeWireframe", "Wireframe (edges only)", iw8r::app::CameraMode::Wireframe},
                                {"CameraModeFullbright", "Fullbright (no shading)", iw8r::app::CameraMode::Fullbright},
                                {"CameraModeNormals", "Normals (face normal as colour)", iw8r::app::CameraMode::Normals},
                                {"CameraModeContents", "Contents (structural, detail, non-colliding, clip, entity)",
                                 iw8r::app::CameraMode::Contents},
                                {"CameraModeDensity", "Texel Density (green on target, blue low, red high)",
                                 iw8r::app::CameraMode::Density},
                                {"CameraModeCollision", "Collision Only (what the player collides with in the build)",
                                 iw8r::app::CameraMode::Collision}};
    if (const QColor st(Settings::instance().value("view/selectionTint", "#ff3030").toString()); st.isValid()) {
        ctx_.scene.selectionTint = 0xff000000u | uint32_t(st.red()) | (uint32_t(st.green()) << 8) | (uint32_t(st.blue()) << 16);
        ctx_.scene.selectionTintAmount = std::clamp(Settings::instance().value("view/selectionTintAmount", 0.5).toDouble(), 0.05, 1.0);
    }
    for (const auto& cm : camModes) {
        const iw8r::app::CameraMode mode = cm.mode;
        QAction* a = addCommand(camMode, cm.command, cm.text, QKeySequence(), [this, mode] {
            ctx_.scene.cameraMode = mode;
            ctx_.texturedView = mode == iw8r::app::CameraMode::Textured || mode == iw8r::app::CameraMode::Fullbright;
            Settings::instance().setValue("view/cameraMode", int(mode));
            Settings::instance().setValue("view/textured", ctx_.texturedView);
            ctx_.cameraMoved();
        });
        a->setCheckable(true);
        a->setChecked(mode == ctx_.scene.cameraMode);
        modeGroup->addAction(a);
    }
    camera_->setTextureBudgetMB(Settings::instance().value("view/textureBudgetMB", 1024).toInt());
    addCommand(view, "TextureBudget", "Texture Memory Budget...", QKeySequence(), [this] {
        // 3.20: past the budget, new camera textures load at 128 px (the rest of the session).
        bool ok = false;
        const int mb = QInputDialog::getInt(this, "Texture Memory Budget",
                                            QString("Camera texture memory now: %1 MB. Budget (MB); past it new textures load at 128 px:")
                                                .arg(camera_->textureBytes() / (1024 * 1024)),
                                            int(camera_->textureBudgetBytes() / (1024 * 1024)), 64, 65536, 64, &ok);
        if (!ok) return;
        Settings::instance().setValue("view/textureBudgetMB", mb);
        camera_->setTextureBudgetMB(mb);
        ctx_.setStatus(QString("Texture memory budget %1 MB (%2 MB in use)").arg(mb).arg(camera_->textureBytes() / (1024 * 1024)));
    });
    addCommand(view, "SelectionHighlight", "Selection Highlight...", QKeySequence(), [this] {
        // 3.13: the tint selected faces take in the camera (colour and strength), kept in the settings.
        const uint32_t t = ctx_.scene.selectionTint;
        const QColor c = QColorDialog::getColor(QColor(int(t & 0xff), int((t >> 8) & 0xff), int((t >> 16) & 0xff)), this, "Selection Tint");
        if (!c.isValid()) return;
        bool ok = false;
        const int pct = QInputDialog::getInt(this, "Selection Highlight", "Tint strength (%):", int(ctx_.scene.selectionTintAmount * 100), 5, 100, 5, &ok);
        if (!ok) return;
        setSelectionHighlight(c, pct / 100.0);
    });
    QAction* fogToggle = addCommand(view, "ToggleFogPreview", "Fog Preview (Compile > Atmosphere fog)", QKeySequence(), [this] {
        ctx_.fogPreview = !ctx_.fogPreview;
        if (QAction* a = action("ToggleFogPreview")) a->setChecked(ctx_.fogPreview);
        ctx_.cameraMoved();
    });
    fogToggle->setCheckable(true);
    fogToggle->setChecked(true);
    QAction* fxToggle = addCommand(view, "ToggleEffectPreview", "Effect Preview in Camera (fx_origin particles)", QKeySequence(), [this] {
        ctx_.effectPreview = !ctx_.effectPreview;
        if (QAction* a = action("ToggleEffectPreview")) a->setChecked(ctx_.effectPreview);
        if (ctx_.effectPreview && !ctx_.texturedView) ctx_.setStatus("Effect preview draws in the textured camera (F3 / View > Camera Mode > Textured)");
        ctx_.cameraMoved();
    });
    fxToggle->setCheckable(true);
    QAction* gradeToggle = addCommand(view, "ToggleGradePreview", "Colour Grade Preview (Compile > Atmosphere film / LUT)", QKeySequence(), [this] {
        ctx_.gradePreview = !ctx_.gradePreview;
        if (QAction* a = action("ToggleGradePreview")) a->setChecked(ctx_.gradePreview);
        ctx_.cameraMoved();
    });
    gradeToggle->setCheckable(true);
    gradeToggle->setChecked(true);
    // Build preview: the last compile's collision hulls (solid cyan, player clip orange, ladder green).
    auto loadCollisionPreview = [this] {
        ctx_.scene.collisionPreview.clear();
        if (QAction* a = action("ToggleCompiledCollision"); !a || !a->isChecked()) return ctx_.cameraMoved();
        const QString f = compile_->extractionDir() + "/extracted/maps/mp/" + compile_->mapId() + ".d3dbsp.replay-collision.json";
        std::string text, err;
        if (!readFile(f.toStdString(), text) || !readCollisionHulls(text, ctx_.scene.collisionPreview, &err))
            ctx_.setStatus("No compiled collision for " + compile_->mapId() + " yet (compile the extraction first)");
        else
            ctx_.setStatus(QString("Compiled collision: %1 hulls").arg(ctx_.scene.collisionPreview.size()));
        ctx_.cameraMoved();
    };
    QAction* colToggle = addCommand(view, "ToggleCompiledCollision", "Show Compiled Collision", QKeySequence(), loadCollisionPreview);
    colToggle->setCheckable(true);
    connect(compile_, &CompilePanel::finished, this, [loadCollisionPreview](bool) { loadCollisionPreview(); });
    // Build preview (3.10): the last compile's render surfaces, as the build produced them (magenta wireframe).
    auto loadWorldPreview = [this] {
        ctx_.scene.compiledWorld.clear();
        if (QAction* a = action("ToggleCompiledWorld"); !a || !a->isChecked()) return ctx_.cameraMoved();
        const QString f = compile_->extractionDir() + "/extracted/maps/mp/" + compile_->mapId() + ".d3dbsp.replay-world.json";
        std::string text, err;
        size_t tris = 0;
        if (!readFile(f.toStdString(), text) || !readCompiledSurfaces(text, ctx_.scene.compiledWorld, &tris, &err))
            ctx_.setStatus("No compiled world for " + compile_->mapId() + " yet (compile the extraction first)");
        else
            ctx_.setStatus(QString("Compiled world: %1 triangles (magenta)").arg(tris));
        ctx_.cameraMoved();
    };
    auto loadNavPreview = [this] {
        // 6.23 (core nav_debug): the navmesh the converter built for the last compile, from its debug dump.
        ctx_.scene.navLines.clear();
        if (QAction* a = action("ToggleNavigation"); !a || !a->isChecked()) return ctx_.cameraMoved();
        std::string text, err;
        NavDebug nav;
        if (!readFile(compile_->navDebugFile().toStdString(), text) || !readNavDebug(text, nav, &err))
            ctx_.setStatus("No navigation dump yet: compile the map (F7); x64-zt writes it while building the zones" +
                           (err.empty() ? QString() : " (" + qs(err) + ")"));
        else {
            ctx_.scene.navLines = navDebugLines(nav, 2.0);
            ctx_.setStatus("Navigation (green): " + qs(navDebugSummary(nav)));
        }
        ctx_.cameraMoved();
    };
    QAction* lodToggle = addCommand(view, "ToggleLodPreview", "LOD Preview (stock models switch LODs by camera distance)", QKeySequence(),
                                    [this] { setLodPreview(!lodPreview_); });
    lodToggle->setCheckable(true);
    QAction* navToggle = addCommand(view, "ToggleNavigation", "Show Navigation (navmesh, nodes, links)", QKeySequence(), loadNavPreview);
    navToggle->setCheckable(true);
    connect(compile_, &CompilePanel::finished, this, [loadNavPreview](bool) { loadNavPreview(); });
    QAction* worldToggle = addCommand(view, "ToggleCompiledWorld", "Show Compiled World", QKeySequence(), loadWorldPreview);
    worldToggle->setCheckable(true);
    connect(compile_, &CompilePanel::finished, this, [loadWorldPreview](bool) { loadWorldPreview(); });
    QAction* toastToggle = addCommand(view, "ToggleToasts", "Toast Notifications (warnings, errors, compile result)", QKeySequence(), [this] {
        const bool on = !toasts_->toastsEnabled();
        toasts_->setToastsEnabled(on);
        Settings::instance().setValue("view/toasts", on);
        if (QAction* a = action("ToggleToasts")) a->setChecked(on);
    });
    toastToggle->setCheckable(true);
    toastToggle->setChecked(toasts_->toastsEnabled());
    // Camera mode: saved mode, else the old textured flag (Textured or Flat).
    {
        const bool texOn = Settings::instance().value("view/textured", true).toBool();
        const int savedMode = Settings::instance().value("view/cameraMode", -1).toInt();
        ctx_.scene.cameraMode = savedMode >= 0 && savedMode <= int(iw8r::app::CameraMode::Density)
                                    ? iw8r::app::CameraMode(savedMode)
                                    : (texOn ? iw8r::app::CameraMode::Textured : iw8r::app::CameraMode::Flat);
        ctx_.texturedView = ctx_.scene.cameraMode == iw8r::app::CameraMode::Textured ||
                            ctx_.scene.cameraMode == iw8r::app::CameraMode::Fullbright;
        if (QAction* m = action(ctx_.scene.cameraMode == iw8r::app::CameraMode::Textured      ? "CameraModeTextured"
                                : ctx_.scene.cameraMode == iw8r::app::CameraMode::Flat        ? "CameraModeFlat"
                                : ctx_.scene.cameraMode == iw8r::app::CameraMode::Wireframe   ? "CameraModeWireframe"
                                : ctx_.scene.cameraMode == iw8r::app::CameraMode::Fullbright  ? "CameraModeFullbright"
                                : ctx_.scene.cameraMode == iw8r::app::CameraMode::Contents    ? "CameraModeContents"
                                : ctx_.scene.cameraMode == iw8r::app::CameraMode::Density     ? "CameraModeDensity"
                                : ctx_.scene.cameraMode == iw8r::app::CameraMode::Collision   ? "CameraModeCollision"
                                                                                              : "CameraModeNormals"))
            m->setChecked(true);
    }
    texToggle->setChecked(ctx_.texturedView);

    QMenu* region = view->addMenu("Region");
    addCommand(region, "RegionFromSelection", "Set From Selection", QKeySequence(), [this] {
        QString err;
        if (!setRegionFromSelection(&err)) ctx_.setStatus(err);
    });
    addCommand(region, "RegionFromXY", "Set From XY View", QKeySequence(), [this] {
        QString err;
        if (!setRegionFromXYView(&err)) ctx_.setStatus(err);
    });
    addCommand(region, "RegionOff", "Off", QKeySequence(), [this] { clearRegion(); });

    QMenu* grid = menuBar()->addMenu("&Grid");
    struct G {
        const char* cmd;
        double size;
        const char* key;
    };
    // Stock CoD Radiant grid keys (docs/research/07 section 3a): ~ = 0.5, 1-7 = 1..64, 8 = 256, 9 = 512, 128 unbound.
    const G sizes[] = {{"SetGridPointFive", 0.5, "`"}, {"SetGrid1", 1, "1"},     {"SetGrid2", 2, "2"},     {"SetGrid4", 4, "3"},
                       {"SetGrid8", 8, "4"},          {"SetGrid16", 16, "5"},   {"SetGrid32", 32, "6"},   {"SetGrid64", 64, "7"},
                       {"SetGrid128", 128, ""},       {"SetGrid256", 256, "8"}, {"SetGrid512", 512, "9"}};
    for (const auto& g : sizes) {
        const double size = g.size;
        addCommand(grid, g.cmd, "Grid " + qs(formatNumber(size)), QKeySequence(g.key), [this, size] {
            ctx_.doc.setGrid(size);
            ctx_.documentEdited();
        });
    }
    grid->addSeparator();
    addCommand(grid, "GridDown", "Grid Smaller", QKeySequence(Qt::Key_BracketLeft), [this] {
        ctx_.doc.gridDown();
        ctx_.documentEdited();
    });
    addCommand(grid, "GridUp", "Grid Larger", QKeySequence(Qt::Key_BracketRight), [this] {
        ctx_.doc.gridUp();
        ctx_.documentEdited();
    });
    QAction* snap = addCommand(grid, "ToggleSnapToGrid", "Snap To Grid", QKeySequence("Ctrl+Alt+G"), [this] {
        ctx_.snap = !ctx_.snap;
        ctx_.documentEdited(ctx_.snap ? "Snap on" : "Snap off");
    });
    snap->setCheckable(true);
    snap->setChecked(true);
    grid->addSeparator();
    addCommand(grid, "SelectNudgeLeft", "Nudge Left", QKeySequence("Alt+Left"), [this] { runNudge(-1, 0); });
    addCommand(grid, "SelectNudgeRight", "Nudge Right", QKeySequence("Alt+Right"), [this] { runNudge(1, 0); });
    addCommand(grid, "SelectNudgeUp", "Nudge Up", QKeySequence("Alt+Up"), [this] { runNudge(0, 1); });
    addCommand(grid, "SelectNudgeDown", "Nudge Down", QKeySequence("Alt+Down"), [this] { runNudge(0, -1); });

    QMenu* brush = menuBar()->addMenu("&Brush");
    QAction* clipToggle = addCommand(brush, "ToggleClipper", "Clipper", QKeySequence(Qt::Key_X), [this] {
        ctx_.clipMode = !ctx_.clipMode;
        ctx_.clipPoints.clear();
        ctx_.clipFlip = false;
        for (const char* c : {"ClipSelected", "SplitSelected", "FlipClip"}) actions_.value(c)->setEnabled(ctx_.clipMode);
        ctx_.documentEdited(ctx_.clipMode ? "Clipper: click two points in the 2D view" : "Clipper off");
    });
    clipToggle->setCheckable(true);
    auto runClip = [this, clipToggle](bool split) {
        auto plane = ctx_.clipPlane();
        if (!plane) { ctx_.setStatus("Clipper: place two points first"); return; }
        if (clipSelection(ctx_, *plane, split)) {
            ctx_.clipPoints.clear();
            ctx_.clipMode = false;
            clipToggle->setChecked(false);
            for (const char* c : {"ClipSelected", "SplitSelected", "FlipClip"}) actions_.value(c)->setEnabled(false);
            ctx_.documentEdited(split ? "Split (undoable)" : "Clipped (undoable)");
        }
    };
    addCommand(brush, "ClipSelected", "Clip Selected", QKeySequence(Qt::Key_Return), [runClip] { runClip(false); })->setEnabled(false);
    addCommand(brush, "SplitSelected", "Split Selected", QKeySequence("Shift+Return"), [runClip] { runClip(true); })->setEnabled(false);
    addCommand(brush, "FlipClip", "Flip Clip Side", QKeySequence("Ctrl+Return"), [this] {
        ctx_.clipFlip = !ctx_.clipFlip;
        ctx_.documentEdited("Clipper side flipped");
    })->setEnabled(false);
    brush->addSeparator();
    addCommand(brush, "CSGHollow", "CSG Hollow", QKeySequence(), [this] { if (hollowSelection(ctx_)) ctx_.documentEdited("Hollowed (wall = grid size)"); });
    addCommand(brush, "CSGSubtract", "CSG Subtract", QKeySequence("Shift+U"), [this] { if (subtractSelection(ctx_)) ctx_.documentEdited("Subtracted"); });
    addCommand(brush, "MakeCylinder", "Cylinder (8 sides) From Selection", QKeySequence(), [this] { if (prismFromSelection(ctx_, 8)) ctx_.documentEdited("Cylinder"); });
    brush->addSeparator();
    const char* axes[3] = {"X", "Y", "Z"};
    for (int a = 0; a < 3; ++a) {
        addCommand(brush, QString("Rotate%1").arg(axes[a]), QString("Rotate 90 about %1").arg(axes[a]), QKeySequence(),
                   [this, a] { if (rotateSelection(ctx_, a, 90)) ctx_.documentEdited("Rotated"); });
        addCommand(brush, QString("Flip%1").arg(axes[a]), QString("Flip %1").arg(axes[a]), QKeySequence(),
                   [this, a] { if (flipSelection(ctx_, a)) ctx_.documentEdited("Flipped"); });
    }
    brush->addSeparator();
    // Brush body flags (contents detail / nonColliding) on every selected brush: one undo step. `change` gets
    // each brush's flags and whether every selected brush is already nonColliding.
    auto editBrushFlags = [this](const QString& name, const std::function<void(BrushFlags&, bool)>& change) {
        std::vector<Document::PrimEdit> edits;
        int brushes = 0;
        bool allNonColliding = true;
        for (const auto& r : ctx_.doc.selection()) {
            const EntityItem* it = r.item == kNoId ? nullptr : ctx_.doc.findItem(r.entity, r.item);
            if (!it || !it->prim() || it->prim()->kind != PrimKind::Brush) continue;
            ++brushes;
            allNonColliding = allNonColliding && readBrushFlags(*it->prim()).nonColliding;
        }
        if (brushes == 0) { ctx_.setStatus("Select one or more brushes first"); return; }
        for (const auto& r : ctx_.doc.selection()) {
            const EntityItem* it = r.item == kNoId ? nullptr : ctx_.doc.findItem(r.entity, r.item);
            if (!it || !it->prim() || it->prim()->kind != PrimKind::Brush) continue;
            BrushFlags f = readBrushFlags(*it->prim());
            change(f, allNonColliding);
            auto p = withBrushFlags(*it->prim(), f, ctx_.doc.eol());
            if (p && p->text() != it->prim()->text()) edits.push_back({r.entity, r.item, *p});
        }
        if (edits.empty()) { ctx_.setStatus(name + ": already set on the selection"); return; }
        if (ctx_.doc.editPrimitives(edits, name.toStdString(), false)) ctx_.documentEdited(name);
    };
    addCommand(brush, "BrushMakeDetail", "Make &Detail (contents detail)", QKeySequence("Ctrl+M"),
               [editBrushFlags] { editBrushFlags("Make Detail", [](BrushFlags& f, bool) { f.detail = true; }); });
    addCommand(brush, "BrushMakeStructural", "Make &Structural (clear detail and nonColliding)", QKeySequence(),
               [editBrushFlags] {
                   editBrushFlags("Make Structural", [](BrushFlags& f, bool) {
                       f.detail = false;
                       f.nonColliding = false;
                   });
               });
    addCommand(brush, "BrushToggleNonColliding", "Toggle &Non-Colliding (contents nonColliding)", QKeySequence(),
               [editBrushFlags] {
                   editBrushFlags("Toggle Non-Colliding", [](BrushFlags& f, bool allNc) { f.nonColliding = !allNc; });
               });
    brush->addSeparator();
    addCommand(brush, "AutoCaulk", "Auto Caulk (hidden faces of the selection, or the whole map)", QKeySequence(), [this] {
        const bool sel = !ctx_.doc.selection().empty();
        const size_t n = autoCaulk(ctx_.doc, sel);
        ctx_.documentEdited(n ? QString("%1 hidden face(s) set to caulk (one undo step)").arg(n) : QString("No hidden faces found"));
    });
    addCommand(brush, "ApplyMaterial", "Apply Current Material", QKeySequence("Shift+M"), [this] {
        if (applyMaterialToSelection(ctx_, ctx_.currentMaterial)) ctx_.documentEdited("Applied " + qs(ctx_.currentMaterial));
    });
    {
        // Tool materials (iw8_contents.h): picking one makes it current and applies it to the selection.
        QMenu* tools = brush->addMenu("Tool Material");
        const char* const kToolMats[][2] = {{"caulk", "Caulk (solid, not drawn)"},
                                            {"nodraw", "Nodraw"},
                                            {"clip", "Clip (blocks everything)"},
                                            {"clip_player", "Player Clip"},
                                            {"clip_weapon", "Weapon Clip"},
                                            {"clip_nosight", "AI No-Sight Clip"},
                                            {"ladder", "Ladder (climbable faces)"},
                                            {"water", "Water (non-solid volume)"},
                                            {"trigger", "Trigger"},
                                            {"hint", "Hint"},
                                            {"skip", "Skip"},
                                            {"sky", "Sky"}};
        for (const auto& tm : kToolMats) {
            const std::string mat = tm[0];
            addCommand(tools, QString("ToolMaterial_%1").arg(tm[0]), tm[1], QKeySequence(), [this, mat] {
                ctx_.currentMaterial = mat;
                if (materialEdit_) materialEdit_->setText(qs(mat));
                if (applyMaterialToSelection(ctx_, mat)) ctx_.documentEdited("Applied tool material " + qs(mat));
                else ctx_.setStatus("Current material: " + qs(mat) + " (new brushes)");
            });
        }
    }
    QAction* texLock = addCommand(brush, "ToggleTextureLock", "Texture &Lock (moves, rotations, flips)", QKeySequence("Shift+T"), [this] {
        ctx_.doc.textureLock = !ctx_.doc.textureLock;
        if (QAction* a = action("ToggleTextureLock")) a->setChecked(ctx_.doc.textureLock);
        Settings::instance().setValue("brush/textureLock", ctx_.doc.textureLock);
        ctx_.setStatus(ctx_.doc.textureLock ? "Texture lock on" : "Texture lock off");
    });
    texLock->setCheckable(true);
    ctx_.doc.textureLock = Settings::instance().value("brush/textureLock", false).toBool();
    texLock->setChecked(ctx_.doc.textureLock);
    {
        // Layout (2.25, core layout_tools): array / radial duplicates, mirrored copy, align and distribute. One undo step each.
        QMenu* lay = brush->addMenu("Arrange");
        lay->setObjectName("arrangeMenu");
        auto done = [this](bool ok, const QString& what) {
            if (!ok) return ctx_.setStatus(what + ": select something first (distribute needs three objects)");
            ctx_.scene.invalidate();
            ctx_.documentEdited(what);
        };
        addCommand(lay, "ArrayDuplicate", "Array Duplicate...", QKeySequence(), [this, done] {
            if (ctx_.doc.selection().empty()) return done(false, "Array duplicate");
            const AABB b = ctx_.doc.selectionBounds();
            const double w = b.valid() ? b.maxs.x - b.mins.x : ctx_.doc.grid();
            bool ok = false;
            const QString step = QInputDialog::getText(this, "Array Duplicate", "Offset between copies (x y z):", QLineEdit::Normal,
                                                       QString("%1 0 0").arg(w), &ok);
            if (!ok) return;
            const QStringList p = step.split(' ', Qt::SkipEmptyParts);
            if (p.size() != 3) return ctx_.setStatus("Array duplicate: the offset needs three numbers");
            const int count = QInputDialog::getInt(this, "Array Duplicate", "Number of copies:", 4, 1, 500, 1, &ok);
            if (!ok) return;
            size_t n = 0;
            duplicateWithNewIds("Array Duplicate", [&] {
                n = arrayDuplicate(ctx_.doc, Vec3{p[0].toDouble(), p[1].toDouble(), p[2].toDouble()}, count);
                return n > 0;
            });
            done(n > 0, QString("Array duplicate: %1 copies").arg(n));
        });
        addCommand(lay, "RadialDuplicate", "Radial Duplicate (around Z)...", QKeySequence(), [this, done] {
            if (ctx_.doc.selection().empty()) return done(false, "Radial duplicate");
            bool ok = false;
            const int count = QInputDialog::getInt(this, "Radial Duplicate", "Number of copies:", 5, 1, 359, 1, &ok);
            if (!ok) return;
            const double deg = QInputDialog::getDouble(this, "Radial Duplicate", "Total angle (360 = full circle):", 360, 1, 360, 1, &ok);
            if (!ok) return;
            // Around the grid view's centre, so the ring can be placed by panning the 2D view first.
            Vec3 c;
            c[ctx_.axisH()] = snapToGrid(ctx_.gridCenterH, ctx_.doc.grid());
            c[ctx_.axisV()] = snapToGrid(ctx_.gridCenterV, ctx_.doc.grid());
            size_t n = 0;
            duplicateWithNewIds("Radial Duplicate", [&] {
                n = radialDuplicate(ctx_.doc, c, 2, count, deg);
                return n > 0;
            });
            done(n > 0, QString("Radial duplicate: %1 copies around (%2, %3)").arg(n).arg(c.x).arg(c.y));
        });
        lay->addSeparator();
        const char* axes[] = {"X", "Y", "Z"};
        for (int a = 0; a < 3; ++a)
            addCommand(lay, QString("MirrorClone%1").arg(axes[a]), QString("Mirror Copy across +%1 side").arg(axes[a]), QKeySequence(),
                       [this, done, a] {
                           const AABB b = ctx_.doc.selectionBounds();
                           if (!b.valid()) return done(false, "Mirror copy");
                           const bool ok = duplicateWithNewIds("Mirror Clone", [&] { return mirrorClone(ctx_.doc, a, b.maxs[a]); });
                           done(ok, QString("Mirrored copy across %1 = %2").arg("XYZ"[a]).arg(b.maxs[a]));
                       });
        lay->addSeparator();
        const struct {
            AlignMode m;
            const char* name;
        } modes[] = {{AlignMode::Min, "Min"}, {AlignMode::Center, "Center"}, {AlignMode::Max, "Max"}};
        for (int a = 0; a < 3; ++a) {
            QMenu* al = lay->addMenu(QString("Align %1").arg(axes[a]));
            for (const auto& m : modes)
                addCommand(al, QString("Align%1%2").arg(axes[a], m.name), QString("%1 %2").arg(m.name, axes[a]), QKeySequence(),
                           [this, done, a, mode = m.m, name = QString(m.name)] {
                               done(alignSelection(ctx_.doc, a, mode), QString("Aligned to %1 %2").arg(name).arg("XYZ"[a]));
                           });
        }
        for (int a = 0; a < 3; ++a)
            addCommand(lay, QString("Distribute%1").arg(axes[a]), QString("Distribute along %1 (equal gaps)").arg(axes[a]), QKeySequence(),
                       [this, done, a] { done(distributeSelection(ctx_.doc, a), QString("Distributed along %1").arg("XYZ"[a])); });
    }
    {
        // Splines (2.26, core spline_tools): select point entities (script_origin, info_null...) in path order, then
        // build walls, posts or pipes through them with the current material. One undo step.
        QMenu* sp = brush->addMenu("Spline");
        sp->setObjectName("splineMenu");
        auto run = [this](const QString& kind, const QString& aLabel, double aDef, const QString& bLabel, double bDef) {
            bool ok = false;
            const double a = QInputDialog::getDouble(this, "Spline " + kind, aLabel, aDef, 1, 4096, 0, &ok);
            if (!ok) return;
            const double b = QInputDialog::getDouble(this, "Spline " + kind, bLabel, bDef, 1, 8192, 0, &ok);
            if (!ok) return;
            const bool closed = QMessageBox::question(this, "Spline " + kind, "Close the loop (last point back to the first)?") == QMessageBox::Yes;
            if (!splineFromSelection(kind, a, b, closed))
                ctx_.setStatus("Spline: select two or more point entities (three for a loop) in path order first");
        };
        addCommand(sp, "SplineWall", "Wall Through Selected Points...", QKeySequence(), [run] { run("wall", "Thickness:", 16, "Height:", 128); });
        addCommand(sp, "SplinePosts", "Posts / Fence Through Selected Points...", QKeySequence(), [run] { run("posts", "Spacing:", 64, "Height:", 48); });
        addCommand(sp, "SplinePipe", "Pipe Through Selected Points...", QKeySequence(), [run] { run("pipe", "Radius:", 8, "Sides:", 8); });
    }
    // Bevel and inset (2.23, core bevel_ops): one undo step each.
    addCommand(brush, "ChamferEdges", "Chamfer All Edges...", QKeySequence(), [this] {
        if (ctx_.doc.selection().empty()) return ctx_.setStatus("Select brushes first");
        bool ok = false;
        const double size = QInputDialog::getDouble(this, "Chamfer Edges", "Cut size (units along each face):",
                                                    std::max(2.0, ctx_.doc.grid()), 0.5, 1024, 1, &ok);
        if (!ok) return;
        const size_t n = chamferSelection(ctx_.doc, size);
        ctx_.scene.invalidate();
        ctx_.documentEdited(n ? QString("Chamfered %1 brush(es) (one undo step)").arg(n)
                              : QString("Chamfer: the cut is too large for the selected brushes"));
    });
    addCommand(brush, "UvEditor", "UV Editor...", QKeySequence(), [this] { openUvEditor(); });
    addCommand(brush, "PlaceDecal", "Place Decal on Selected Face...", QKeySequence(), [this] {
        // 8.10 phase A (core decal_tools, research 46): a thin non-colliding patch with the current material, centred on
        // the selected face, conformed onto neighbouring faces; alpha-tested materials cut out. One undo step.
        if (ctx_.doc.faceSelection().size() != 1) return ctx_.setStatus("Select one brush face first (Ctrl+Shift+click in the camera)");
        if (ctx_.currentMaterial.empty()) return ctx_.setStatus("Pick the decal material first (Materials browser)");
        const FaceRef f = *ctx_.doc.faceSelection().begin();
        const EntityItem* it = ctx_.doc.findItem(f.entity, f.item);
        const Primitive* p = it ? it->prim() : nullptr;
        const auto pl = p ? facePlane(*p, f.face) : std::nullopt;
        const auto wins = p ? faceWindingsOf(*p) : std::vector<Winding>{};
        if (!pl || f.face >= wins.size() || wins[f.face].empty()) return ctx_.setStatus("That face has no area");
        Vec3 c;
        for (const Vec3& v : wins[f.face]) c += v;
        c = c * (1.0 / double(wins[f.face].size()));
        bool ok = false;
        const double size = QInputDialog::getDouble(this, "Place Decal", "Size (units):", 64, 1, 4096, 0, &ok);
        if (!ok) return;
        const double rot = QInputDialog::getDouble(this, "Place Decal", "Rotation (degrees):", 0, -360, 360, 0, &ok);
        if (!ok) return;
        const auto patches = makeConformingDecal(ctx_.doc.map(), c, pl->n, size, size, rot, 0.5, ctx_.currentMaterial);
        if (patches.empty() || !placeDecal(ctx_.doc, patches, true)) return ctx_.setStatus("Decal: nothing to place there");
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("Decal %1 placed (%2 piece(s), non-colliding, one undo step)").arg(qs(ctx_.currentMaterial)).arg(patches.size()));
    });
    {
        QAction* dm = addCommand(brush, "DecalMode", "Decal Mode (click surfaces in the camera)...", QKeySequence(), [this] {
            // 8.10: click-to-place decals with the current material; size and rotation asked when it is turned on.
            if (!ctx_.decalMode) {
                bool ok = false;
                const double size = QInputDialog::getDouble(this, "Decal Mode", "Decal size (units):", ctx_.decalSize, 1, 4096, 0, &ok);
                if (!ok) {
                    if (QAction* a = action("DecalMode")) a->setChecked(false);
                    return;
                }
                const double rot = QInputDialog::getDouble(this, "Decal Mode", "Rotation (degrees):", ctx_.decalRotation, -360, 360, 0, &ok);
                if (!ok) {
                    if (QAction* a = action("DecalMode")) a->setChecked(false);
                    return;
                }
                ctx_.decalSize = size, ctx_.decalRotation = rot;
            }
            ctx_.decalMode = !ctx_.decalMode;
            if (ctx_.decalMode) ctx_.vertexPaint = false;
            if (QAction* a = action("DecalMode")) a->setChecked(ctx_.decalMode);
            ctx_.setStatus(ctx_.decalMode ? QString("Decal mode: click a surface in the camera to place %1 (%2 units); turn off to select again")
                                                .arg(qs(ctx_.currentMaterial.empty() ? std::string("(pick a material)") : ctx_.currentMaterial)).arg(ctx_.decalSize)
                                          : QString("Decal mode off"));
        });
        dm->setCheckable(true);
    }
    addCommand(brush, "BakeToModel", "Convert Selection to Static Model", QKeySequence(), [this] {
        QString err;
        if (!bakeSelectionToModel(&err)) ctx_.setStatus("Convert to Static Model: " + err);
    });
    addCommand(brush, "BridgeFaces", "Bridge Two Faces", QKeySequence(), [this] {
        if (ctx_.doc.faceSelection().size() != 2) return ctx_.setStatus("Bridge: select exactly two brush faces first (Ctrl+Shift+click in the camera)");
        if (!bridgeSelectedFaces(ctx_.doc)) return ctx_.setStatus("Bridge: the two faces lie in one plane (nothing to span)");
        ctx_.scene.invalidate();
        ctx_.documentEdited("Bridged two faces with a new brush (one undo step)");
    });
    addCommand(brush, "InsetFace", "Inset Selected Face (recess)...", QKeySequence(), [this] {
        if (ctx_.doc.faceSelection().size() != 1) return ctx_.setStatus("Select exactly one brush face first (Ctrl+Shift+click in the camera)");
        bool ok = false;
        const double inset = QInputDialog::getDouble(this, "Inset Face", "Border width:", std::max(4.0, ctx_.doc.grid()), 0.5, 4096, 1, &ok);
        if (!ok) return;
        const double depth = QInputDialog::getDouble(this, "Inset Face", "Recess depth:", std::max(4.0, ctx_.doc.grid()), 0.5, 4096, 1, &ok);
        if (!ok) return;
        const FaceRef f = *ctx_.doc.faceSelection().begin();
        if (!insetSelectedFace(ctx_.doc, f, inset, depth)) return ctx_.setStatus("Inset: the border or depth is too large for that face");
        ctx_.scene.invalidate();
        ctx_.documentEdited("Inset face (one undo step)");
    });
    {
        // 2.22 (core tool_flags): named toolFlags lines on the selected brushes; unknown names are kept as they are.
        QMenu* tf = brush->addMenu("Tool Flags");
        tf->setObjectName("toolFlagsMenu");
        for (const ToolFlagDef& f : knownToolFlags()) {
            const QString name = qs(f.name);
            QAction* on = addCommand(tf, "ToolFlagOn_" + name, "Set " + name, QKeySequence(), [this, name] {
                if (!setToolFlagOnSelection(ctx_.doc, name.toStdString(), true)) return ctx_.setStatus("No selected brush changed");
                ctx_.documentEdited("toolFlags " + name + " set (one undo step)");
            });
            on->setToolTip(qs(f.help));
            addCommand(tf, "ToolFlagOff_" + name, "Clear " + name, QKeySequence(), [this, name] {
                if (!setToolFlagOnSelection(ctx_.doc, name.toStdString(), false)) return ctx_.setStatus("No selected brush changed");
                ctx_.documentEdited("toolFlags " + name + " cleared (one undo step)");
            });
        }
        tf->addSeparator();
        addCommand(tf, "ToolFlagCustom", "Set Other Flag...", QKeySequence(), [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, "Tool Flags", "Flag name:", QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || name.isEmpty()) return;
            if (!setToolFlagOnSelection(ctx_.doc, name.toStdString(), true)) return ctx_.setStatus("No selected brush changed (or the name is not valid)");
            ctx_.documentEdited("toolFlags " + name + " set (one undo step)");
        });
    }
    addCommand(brush, "LightmapScale", "Lightmap Scale (selected faces / brushes)...", QKeySequence(), [this] {
        // 2.21 (core lightmap_fields): the lightmap scale tokens of the face lines; other tokens stay as written.
        // The converter bakes no lightmaps for editor maps today, so this matters for exported .map files and later bakes.
        bool ok = false;
        const double sc = QInputDialog::getDouble(this, "Lightmap Scale", "Lightmap scale (16384 = Radiant default):", 16384, 1, 1e6, 0, &ok);
        if (!ok) return;
        LightmapParams lp;
        lp.sx = lp.sy = sc;
        if (!setLightmapOnSelection(ctx_.doc, lp)) return ctx_.setStatus("Select brushes or faces first");
        ctx_.documentEdited(QString("Lightmap scale %1 set (one undo step)").arg(sc));
    });
    addCommand(brush, "BrushToPatches", "Convert Brush Faces to Patches", QKeySequence(), [this] {
        // 2.24 (core convert_ops): one mesh patch per visible face, textured like the brush; the brush is replaced.
        if (!convertSelectedBrushesToPatches(ctx_.doc, false, true)) return ctx_.setStatus("Select brushes with visible faces first");
        ctx_.scene.invalidate();
        ctx_.documentEdited("Converted the brush faces to patches (one undo step)");
    });
    addCommand(brush, "ArbitraryRotation", "Arbitrary Rotation...", QKeySequence(), [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Arbitrary Rotation (degrees)");
        auto* form = new QFormLayout(&dlg);
        QDoubleSpinBox* ax[3];
        for (int a = 0; a < 3; ++a) {
            ax[a] = new QDoubleSpinBox(&dlg);
            ax[a]->setRange(-360, 360);
            ax[a]->setDecimals(3);
            form->addRow(QString("XYZ").mid(a, 1), ax[a]);
        }
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        ctx_.doc.beginGroup("Arbitrary Rotation");
        bool any = false;
        for (int a = 0; a < 3; ++a)
            if (ax[a]->value() != 0) any |= rotateSelection(ctx_, a, ax[a]->value());
        ctx_.doc.endGroup();
        ctx_.documentEdited(any ? QStringLiteral("Rotated (one undo step)") : QStringLiteral("Nothing rotated"));
    });
    addCommand(brush, "ScaleSelection", "Scale...", QKeySequence(), [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Scale Selection");
        auto* form = new QFormLayout(&dlg);
        QDoubleSpinBox* ax[3];
        for (int a = 0; a < 3; ++a) {
            ax[a] = new QDoubleSpinBox(&dlg);
            ax[a]->setRange(0.01, 100);
            ax[a]->setDecimals(3);
            ax[a]->setValue(1);
            form->addRow(QString("XYZ").mid(a, 1), ax[a]);
        }
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        const bool ok = scaleSelection(ctx_, Vec3{ax[0]->value(), ax[1]->value(), ax[2]->value()});
        ctx_.documentEdited(ok ? QStringLiteral("Scaled (undoable)") : QStringLiteral("Nothing scaled"));
    });
    addCommand(brush, "PickMaterial", "Pick Material From Selection", QKeySequence("Ctrl+Shift+M"), [this] {
        const std::string m = materialOfSelection(ctx_);
        if (!m.empty()) {
            ctx_.currentMaterial = m;
            if (materialEdit_) materialEdit_->setText(qs(m));
            ctx_.setStatus("Current material " + qs(m));
        }
    });

    addCommand(brush, "CSGMerge", "CSG Merge", QKeySequence(), [this] {
        ctx_.documentEdited(mergeSelectedBrushes(ctx_) ? QStringLiteral("Merged (undoable)") : QStringLiteral("Select brushes whose union is convex"));
    });
    addCommand(brush, "ExtrudeFace", "Extrude Selected Face (grid)", QKeySequence(), [this] {
        ctx_.documentEdited(extrudeSelectedFace(ctx_, ctx_.doc.grid()) ? QStringLiteral("Extruded (undoable)")
                                                                        : QStringLiteral("Select exactly one brush face (Ctrl+Shift+click)"));
    });
    QMenu* prims = menuBar()->addMenu("P&rimitives");
    {
        // 8.12: building and stair generators fill the selection's bounds (the selected brushes are replaced).
        addCommand(prims, "GenerateStairs", "Generate Stairs in Selection...", QKeySequence(), [this] {
            const QStringList dirs = {"+X", "-X", "+Y", "-Y"};
            bool ok = false;
            const QString d = QInputDialog::getItem(this, "Generate Stairs", "Climb toward:", dirs, 0, false, &ok);
            if (!ok) return;
            QString err;
            const int n = generateStructure(false, dirs.indexOf(d), 1, &err);
            ctx_.setStatus(n > 0 ? QString("Stairs: %1 brush(es) (one undo step)").arg(n) : "Generate Stairs: " + err);
        });
        addCommand(prims, "GenerateBuilding", "Generate Building in Selection...", QKeySequence(), [this] {
            bool ok = false;
            const int floors = QInputDialog::getInt(this, "Generate Building", "Storeys:", 2, 1, 2, 1, &ok);
            if (!ok) return;
            QString err;
            const int n = generateStructure(true, 0, floors, &err);
            ctx_.setStatus(n > 0 ? QString("Building: %1 brush(es) with a door, windows%2 (one undo step)").arg(n).arg(floors > 1 ? ", stairs" : "")
                                 : "Generate Building: " + err);
        });
        prims->addSeparator();
    }
    const std::pair<const char*, PrimitiveKind> primList[] = {{"Cone", PrimitiveKind::Cone}, {"Sphere", PrimitiveKind::Sphere},
                                                              {"Stairs", PrimitiveKind::Stairs}, {"Arch", PrimitiveKind::Arch},
                                                              {"Ramp", PrimitiveKind::Ramp}};
    for (const auto& [label, kind] : primList) {
        addCommand(prims, QString("Create") + label, QString("Create ") + label, QKeySequence(), [this, kind = kind, label = QString(label)] {
            ctx_.documentEdited(createPrimitive(ctx_, kind) ? "Created " + label + " (from the selection or the view centre)" : "Could not create " + label);
        });
    }

    // Shape models: cube, sphere, torus, ... as custom models with the current material (resize with the scale gizmo).
    QMenu* shapesMenu = prims->addMenu("Shape &Models");
    for (const ShapeInfo& sh : shapeKinds())
        addCommand(shapesMenu, QString("ShapeModel_") + sh.id, sh.label, QKeySequence(), [this, k = int(sh.kind)] { addShapeModel(k); });

    // Scripted objects (roadmap 6.5, scripted_objects.h): act on the selected model entities, one undo step each.
    QMenu* scriptedMenu = prims->addMenu("&Scripted Objects");
    const auto selectedEntities = [this] {
        std::set<Id> ids;
        for (const SelRef& r : ctx_.doc.selection()) ids.insert(r.entity);
        return ids;
    };
    addCommand(scriptedMenu, "MakeDestructible", "Make Destructible (breaks when shot)", QKeySequence(), [this, selectedEntities] {
        const int n = makeDestructible(ctx_.doc, selectedEntities());
        if (n) ctx_.documentEdited(QString("%1 model(s) are destructible props (targetname destructable: break after 40 damage)").arg(n));
        else ctx_.setStatus("Select misc_model or script_model entities first");
    });
    addCommand(scriptedMenu, "MakeGlassPane", "Make Glass Pane (breakable glass from the selected brushes)", QKeySequence(), [this] {
        makeGlassPane();
    });
    addCommand(scriptedMenu, "MakeHingedDoor", "Make Hinged Door (swings open when used)", QKeySequence(), [this] {
        // Owner request 2026-11-11: the selected brushes become a door that hangs on a hinge at one end and swings away
        // from the player who presses Use near it; it stays open until it is used again. No trigger is needed.
        bool brushes = false;
        for (const SelRef& r : ctx_.doc.selection())
            if (const EntityItem* it = ctx_.doc.findItem(r.entity, r.item); it && it->prim() && it->prim()->kind == PrimKind::Brush) brushes = true;
        if (!brushes) return ctx_.setStatus("Make Hinged Door: select the door brush(es) first");
        int n = 1;
        std::set<std::string> names;
        for (const auto& e : ctx_.doc.map().entities)
            if (const std::string* t = e.value("targetname")) names.insert(*t);
        while (names.count("door_" + std::to_string(n))) ++n;
        const std::string name = "door_" + std::to_string(n);
        ctx_.doc.beginGroup("Make Hinged Door");
        const Id id = ctx_.doc.groupSelection("script_brushmodel");
        if (id != kNoId) {
            ctx_.doc.setKeyValue(id, "targetname", name);
            ctx_.doc.setKeyValue(id, "iw8r_door", "hinge 100 0.6");
        }
        ctx_.doc.endGroup();
        if (id == kNoId) return ctx_.setStatus("Make Hinged Door: only worldspawn brushes can become a door");
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("Hinged door %1: swings 100 degrees about the hinge at one end when used (key iw8r_door \"hinge 100 0.6\"; "
                                    "iw8r_hinge \"far\" moves the hinge to the other end; View > Behaviour Preview shows it; one undo step)").arg(qs(name)));
    });
    addCommand(scriptedMenu, "MakeLadder", "Make Ladder (climbable brush with a ladder model)", QKeySequence(), [this] { makeLadder(); });
    addCommand(scriptedMenu, "MakeScripted", "Make Scripted (live, for the map script)", QKeySequence(), [this, selectedEntities] {
        std::string names;
        const int n = makeScripted(ctx_.doc, selectedEntities(), &names);
        if (n)
            ctx_.documentEdited(QString("%1 live script_model(s): %2. Drive them from the map's GSC (worldspawn iw8r_script, user_main)")
                                    .arg(n).arg(qs(names)));
        else ctx_.setStatus("Select misc_model or script_model entities first");
    });

    // Game-mode object templates (roadmap 6.2): one undo step each, placed like prefabs (grid centre, floor level).
    QMenu* gameModesMenu = prims->addMenu("Game &Modes");
    for (const GameModeTemplate& t : gameModeTemplates()) {
        addCommand(gameModesMenu, "GameModeTemplate_" + qs(t.id), qs(t.name), QKeySequence(), [this, id = t.id] {
            const GameModeTemplate* tpl = findGameModeTemplate(id);
            if (!tpl) return;
            std::string err;
            if (insertGameModeTemplate(ctx_.doc, *tpl, placementPoint(), &err).empty())
                ctx_.setStatus("Game mode template failed: " + qs(err));
            else
                ctx_.documentEdited("Inserted " + qs(tpl->name) + " (one undo step)");
        });
    }

    {
        // Physics prop (6.15) and volumetric box (5.7): editor classes the build turns into a dynamic model and a
        // world volumetric. A selected misc_model becomes a physics prop; otherwise a new one with the last model.
        addCommand(prims, "MakePhysicsProp", "Physics Prop (dynamic_prop)", QKeySequence(), [this] {
            size_t changed = 0;
            ctx_.doc.beginGroup("Make Physics Prop");
            for (const SelRef& r : std::set<SelRef>(ctx_.doc.selection())) {
                const Entity* e = ctx_.doc.findEntity(r.entity);
                if (!e || r.item != kNoId) continue;
                const std::string c = e->classname();
                if (c != "misc_model" && c != "script_model") continue;
                ctx_.doc.setKeyValue(r.entity, "classname", "dynamic_prop");
                if (!e->value("mass")) ctx_.doc.setKeyValue(r.entity, "mass", "10");
                if (!e->value("material")) ctx_.doc.setKeyValue(r.entity, "material", "wood");
                ++changed;
            }
            ctx_.doc.endGroup();
            if (!changed) return ctx_.setStatus("Select misc_model entities to turn them into physics props");
            ctx_.scene.invalidate();
            ctx_.documentEdited(QString("%1 physics prop(s): set mass (kg) and material (wood, metal, plastic, glass, paper, rubber, brick)").arg(changed));
        });
        addCommand(prims, "MakeTraversal", "Traversal For Bots (jump / climb / ladder)...", QKeySequence(), [this] {
            // 6.23: a node_negotiation_begin / _end pair with one of the nine stock animations, at the 2D view centre.
            QStringList items;
            for (const TraversalAnim& a : stockTraversalAnims()) items << QString("%1  (%2)").arg(a.title, a.name);
            bool ok = false;
            const QString picked = QInputDialog::getItem(this, "Traversal For Bots", "Animation the bot plays:", items, 0, false, &ok);
            if (!ok) return;
            const TraversalAnim* anim = nullptr;
            for (const TraversalAnim& a : stockTraversalAnims())
                if (picked.contains(QString("(%1)").arg(a.name))) anim = &a;
            if (!anim) return;
            const double yaw = QInputDialog::getDouble(this, "Traversal For Bots", "Facing (yaw, degrees):", 0, -360, 360, 0, &ok);
            if (!ok) return;
            std::set<std::string> names;
            for (const Entity& e : ctx_.doc.map().entities)
                if (const std::string* n = e.value("targetname")) names.insert(*n);
            std::string name;
            for (int i = 1; name.empty() || names.count(name) || names.count(name + "_end"); ++i) name = "traverse_" + std::to_string(i);
            const auto [beginKeys, endKeys] = traversalPairKeys(*anim, placementPoint(), yaw, name);
            ctx_.doc.beginGroup("Traversal For Bots");
            const Id b = ctx_.doc.addEntity(beginKeys, {}, "Traversal begin");
            const Id e = ctx_.doc.addEntity(endKeys, {}, "Traversal end");
            ctx_.doc.endGroup();
            ctx_.doc.deselectAll();
            ctx_.doc.select({b, kNoId}, false);
            ctx_.doc.select({e, kNoId}, true);
            ctx_.scene.invalidate();
            ctx_.documentEdited(QString("Traversal %1 (%2): move the begin node to the edge and the end node to where the bot lands; "
                                        "the build writes it into the navigation mesh").arg(QString::fromStdString(name), anim->name));
        });
        addCommand(prims, "AddVolumetricBox", "Volumetric Box (iw8r_volume)", QKeySequence(), [this] {
            const Vec3 at = placementPoint();
            const Id id = ctx_.doc.addEntity({{"classname", "iw8r_volume"}, {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z + 64)},
                                              {"mins", "-128 -128 -64"}, {"maxs", "128 128 128"}, {"density", "0.2"}, {"falloff", "64"}, {"color", "1 1 1"}},
                                             {}, "Add Volumetric Box");
            ctx_.doc.select({id, kNoId}, false);
            ctx_.scene.invalidate();
            ctx_.documentEdited("Volumetric box (mins / maxs, density, falloff, color); in-game look not verified");
        });
    }
    // Triggers and callout zones (6.17 / 6.20, core trigger_templates): the volume is the selection's bounds, or a
    // 128 x 128 x 128 box at the grid centre when nothing is selected. One undo step each.
    {
        auto triggerBox = [this]() {
            AABB b = ctx_.doc.selectionBounds();
            if (b.valid() && b.maxs.x - b.mins.x > 1 && b.maxs.y - b.mins.y > 1 && b.maxs.z - b.mins.z > 1) return b;
            const Vec3 c = placementPoint();
            AABB box;
            box.add(c + Vec3{-64, -64, 0});
            box.add(c + Vec3{64, 64, 128});
            return box;
        };
        QMenu* trig = prims->addMenu("&Triggers");
        trig->setObjectName("triggersMenu");
        for (const TriggerKind& k : triggerKinds()) {
            QAction* a = addCommand(trig, "Trigger_" + qs(k.id), qs(k.title) + (k.buildSupported ? QString() : QString(" (not built)")),
                                    QKeySequence(), [this, triggerBox, id = k.id] {
                                        bool ok = false;
                                        const QString tn = QInputDialog::getText(this, "Trigger", "targetname (optional):",
                                                                                 QLineEdit::Normal, QString(), &ok);
                                        if (!ok) return;
                                        std::string err;
                                        if (insertTrigger(ctx_.doc, id, triggerBox(), tn.trimmed().toStdString(), &err) == kNoId)
                                            return ctx_.setStatus("Trigger: " + qs(err));
                                        ctx_.documentEdited("Inserted " + qs(findTriggerKind(id)->classname) + " (one undo step)");
                                    });
            a->setToolTip(qs(k.description + (k.buildNote.empty() ? "" : " (" + k.buildNote + ")")));
            a->setEnabled(k.buildSupported);
        }
        trig->addSeparator();
        addCommand(trig, "CalloutZone", "Callout Zone (battle chatter area)...", QKeySequence(), [this, triggerBox] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, "Callout Zone",
                                                       "Area name (lower case, e.g. containers, backalley):", QLineEdit::Normal,
                                                       QString(), &ok).trimmed();
            if (!ok) return;
            std::string err;
            if (insertCalloutZone(ctx_.doc, name.toStdString(), triggerBox(), &err) == kNoId)
                return ctx_.setStatus("Callout zone: " + qs(err));
            ctx_.documentEdited("Inserted trigger_multiple_bcs_" + name + " (one undo step)");
        });
    }

    // Radiant patch tools (one undo step each). Creation uses the selected brushes' bounds.
    QMenu* patch = menuBar()->addMenu("&Patch");
    {
        QAction* vp = addCommand(patch, "VertexPaintMode", "Vertex Paint Mode (click patches to paint)...", QKeySequence(), [this] {
            QAction* a = action("VertexPaintMode");
            if (ctx_.vertexPaint) {
                ctx_.vertexPaint = false;
                if (a) a->setChecked(false);
                return ctx_.setStatus("Vertex paint mode off");
            }
            PaintBrush& b = ctx_.vertexBrush;
            const QColor start = QColor::fromRgbF(b.rgba[0], b.rgba[1], b.rgba[2], b.rgba[3]);
            const QColor col = QColorDialog::getColor(start, this, "Vertex Paint Colour (alpha drives blend layers)",
                                                      QColorDialog::ShowAlphaChannel);
            if (!col.isValid()) {
                if (a) a->setChecked(false);
                return;
            }
            bool ok = false;
            const int radius = QInputDialog::getInt(this, "Vertex Paint", "Brush radius (units):", int(b.radius), 8, 4096, 8, &ok);
            if (!ok) return;
            const int strength = QInputDialog::getInt(this, "Vertex Paint", "Strength (percent per click):", int(b.strength * 100), 1, 100, 5, &ok);
            if (!ok) return;
            b.rgba[0] = float(col.redF()), b.rgba[1] = float(col.greenF()), b.rgba[2] = float(col.blueF()), b.rgba[3] = float(col.alphaF());
            b.radius = radius;
            b.strength = strength / 100.0;
            ctx_.vertexPaint = true;
            ctx_.paintModel.clear();
            if (a) a->setChecked(true);
            ctx_.setStatus(QString("Vertex paint: left-click near patches in the camera (radius %1, %2%). The build writes the colours "
                                   "per vertex (research 57); how layered materials use them is unverified in game.").arg(radius).arg(strength));
        });
        if (vp) vp->setCheckable(true);
    }
    auto patchCmd = [this](bool ok, const QString& what) {
        ctx_.documentEdited(ok ? what + " (undoable)" : QString("Select patches first (or brushes to build a patch from)"));
    };
    {
        // 2.21: control point locks (editor-side) and the Patch Inspector.
        auto lockCmd = [this](PatchLockMode m, const QString& what) {
            const size_t n = lockSelectedPatchVertices(ctx_, m);
            ctx_.setStatus(n ? QString("%1 on %2 patch(es)").arg(what).arg(n) : QString("Select one or more patches first"));
            ctx_.cameraMoved();
        };
        addCommand(patch, "PatchLockBorder", "Lock Border Vertices", QKeySequence(), [lockCmd] { lockCmd(PatchLockMode::Border, "Border vertices locked"); });
        addCommand(patch, "PatchLockCorners", "Lock Corner Vertices", QKeySequence(), [lockCmd] { lockCmd(PatchLockMode::Corners, "Corner vertices locked"); });
        addCommand(patch, "PatchUnlock", "Unlock Vertices", QKeySequence(), [lockCmd] { lockCmd(PatchLockMode::Unlock, "Vertices unlocked"); });
        addCommand(patch, "PatchInspector", "Patch Inspector (control points)...", QKeySequence(), [this] { showPatchInspector(); });
        patch->addSeparator();
    }
    addCommand(patch, "PatchSimpleMesh", "Create Simple Patch Mesh (3x3)", QKeySequence("Shift+P"),
               [this, patchCmd] { patchCmd(createPatch(ctx_, PatchShape::Flat3x3), "Created patch mesh"); });
    addCommand(patch, "PatchMesh5", "Create Patch Mesh (5x5)", QKeySequence(),
               [this, patchCmd] { patchCmd(createPatch(ctx_, PatchShape::Flat5x5), "Created patch mesh"); });
    addCommand(patch, "PatchCylinder", "Create Cylinder", QKeySequence(),
               [this, patchCmd] { patchCmd(createPatch(ctx_, PatchShape::Cylinder), "Created cylinder patch"); });
    addCommand(patch, "PatchBevel", "Create Bevel", QKeySequence(),
               [this, patchCmd] { patchCmd(createPatch(ctx_, PatchShape::Bevel), "Created bevel patch"); });
    addCommand(patch, "PatchEndcap", "Create Endcap", QKeySequence(),
               [this, patchCmd] { patchCmd(createPatch(ctx_, PatchShape::Endcap), "Created endcap patch"); });
    patch->addSeparator();
    addCommand(patch, "InvertCurve", "Invert (Flip Facing)", QKeySequence("Ctrl+I"),
               [this, patchCmd] { patchCmd(invertSelectedPatches(ctx_), "Inverted patch"); });
    addCommand(patch, "PatchTranspose", "Matrix Transpose", QKeySequence(),
               [this, patchCmd] { patchCmd(transposeSelectedPatches(ctx_), "Transposed patch"); });
    addCommand(patch, "PatchNatural", "Natural Texture", QKeySequence("Ctrl+Shift+N"),
               [this, patchCmd] { patchCmd(naturalTextureSelectedPatches(ctx_), "Natural texture"); });
    addCommand(patch, "PatchThicken", "Thicken (grid, with seams)", QKeySequence(),
               [this, patchCmd] { patchCmd(thickenSelectedPatches(ctx_, ctx_.doc.grid(), true), "Thickened patch"); });
    patch->addSeparator();
    addCommand(patch, "PatchCap", "Cap", QKeySequence(), [this, patchCmd] { patchCmd(capSelectedPatches(ctx_), "Capped patch"); });
    addCommand(patch, "RoadThroughPoints", "Road Through Selected Points...", QKeySequence(), [this] {
        // 8.13 (core road_tools): a road patch draped on the terrain mesh patches through the selected point entities.
        bool ok = false;
        const double width = QInputDialog::getDouble(this, "Road", "Width:", 192, 8, 4096, 0, &ok);
        if (!ok) return;
        RoadOptions o;
        o.material = ctx_.currentMaterial.empty() ? std::string("caulk") : ctx_.currentMaterial;
        o.carve = QMessageBox::question(this, "Road", "Also flatten the terrain under the road (same undo step)?") == QMessageBox::Yes;
        if (!addRoadFromSelection(ctx_.doc, width, o))
            return ctx_.setStatus("Road: select two or more point entities over a terrain mesh patch first");
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("Road %1 units wide%2 (one undo step)").arg(width).arg(o.carve ? ", terrain flattened" : ""));
    });
    addCommand(patch, "PatchToTerrain", "Convert to Terrain Mesh...", QKeySequence(), [this, patchCmd] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "Convert to Terrain", "Control points per side:", 17, 3, 65, 2, &ok);
        if (!ok) return;
        ctx_.scene.invalidate();
        patchCmd(convertSelectedPatchesToTerrain(ctx_.doc, n, n), "Converted to a terrain mesh");
    });
    {
        QMenu* hm = patch->addMenu("Height Map");
        hm->setObjectName("heightMapMenu");
        addCommand(hm, "HeightMapImport", "Import Onto Selected Terrain...", QKeySequence(), [this] {
            const QString f = QFileDialog::getOpenFileName(this, "Import Height Map", QFileInfo(qs(ctx_.doc.path())).absolutePath(),
                                                           "Height maps (*.png *.pgm *.tga *.jpg *.bmp)");
            if (f.isEmpty()) return;
            bool ok = false;
            const double lo = QInputDialog::getDouble(this, "Import Height Map", "Height of black:", 0, -65536, 65536, 0, &ok);
            if (!ok) return;
            const double hi = QInputDialog::getDouble(this, "Import Height Map", "Height of white:", 512, -65536, 65536, 0, &ok);
            if (!ok) return;
            QString err;
            if (!importHeightMapToSelectedPatch(ctx_, f, lo, hi, &err)) return ctx_.setStatus("Height map: " + err);
            ctx_.scene.invalidate();
            ctx_.documentEdited("Imported " + QFileInfo(f).fileName() + " onto the terrain (one undo step)");
        });
        addCommand(hm, "HeightMapExport", "Export Selected Terrain (16-bit PGM)...", QKeySequence(), [this] {
            const QString f = QFileDialog::getSaveFileName(this, "Export Height Map", QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/terrain.pgm",
                                                           "16-bit PGM (*.pgm)");
            if (f.isEmpty()) return;
            QString err;
            ctx_.setStatus(exportSelectedPatchHeightMap(ctx_, f, &err) ? "Exported " + QFileInfo(f).fileName() : "Height map: " + err);
        });
        addCommand(hm, "TerrainErodeThermal", "Thermal Erosion (slopes settle)", QKeySequence(), [this, patchCmd] {
            ctx_.scene.invalidate();
            patchCmd(erodeSelectedPatches(ctx_, false, 20), "Thermal erosion");
        });
        addCommand(hm, "TerrainErodeHydraulic", "Hydraulic Erosion (rain carves)", QKeySequence(), [this, patchCmd] {
            ctx_.scene.invalidate();
            patchCmd(erodeSelectedPatches(ctx_, true, 30), "Hydraulic erosion");
        });
        addCommand(hm, "TerrainSlopeBlend", "Blend By Slope (vertex alpha)...", QKeySequence(), [this] {
            bool ok = false;
            const double lo = QInputDialog::getDouble(this, "Blend By Slope", "Flat up to (degrees, alpha 0):", 25, 0, 89, 1, &ok);
            if (!ok) return;
            const double hi = QInputDialog::getDouble(this, "Blend By Slope", "Fully blended from (degrees, alpha 255):", 45, lo + 0.5, 90, 1, &ok);
            if (!ok) return;
            const int n = slopeBlendSelectedPatches(ctx_, lo, hi, false);
            if (!n) return ctx_.setStatus("Blend By Slope: select terrain patches first (nothing changed)");
            ctx_.scene.invalidate();
            ctx_.documentEdited(QString("Blend By Slope: %1 patch(es), vertex alpha from the slope (%2 to %3 degrees); the second layer of a "
                                        "layered material shows on the steep parts (one undo step)").arg(n).arg(lo).arg(hi));
        });
    }
    addCommand(patch, "PatchWeld", "Weld Two Patches", QKeySequence(), [this, patchCmd] { patchCmd(weldSelectedPatches(ctx_), "Welded patches"); });
    addCommand(patch, "PatchRedisperse", "Redisperse", QKeySequence(), [this, patchCmd] { patchCmd(redisperseSelectedPatches(ctx_), "Redispersed"); });
    addCommand(patch, "PatchSplit", "Split", QKeySequence(), [this, patchCmd] { patchCmd(splitSelectedPatches(ctx_), "Split patch"); });
    patch->addSeparator();
    addCommand(patch, "TerrainCreate", "Create Terrain (9x9)", QKeySequence(), [this, patchCmd] { patchCmd(createTerrain(ctx_), "Created terrain"); });
    addCommand(patch, "TerrainNoise", "Terrain Noise (grid amplitude)", QKeySequence(), [this, patchCmd] {
        patchCmd(noiseSelectedPatches(ctx_, ctx_.doc.grid(), uint32_t(QDateTime::currentMSecsSinceEpoch())), "Noise applied");
    });
    addCommand(patch, "TerrainSmooth", "Terrain Smooth", QKeySequence(), [this, patchCmd] { patchCmd(smoothSelectedPatches(ctx_, 2), "Smoothed"); });
    patch->addSeparator();
    addCommand(patch, "PatchInsertColumn", "Insert Column", QKeySequence("Ctrl+="),
               [this, patchCmd] { patchCmd(insertPatchRowCol(ctx_, false), "Inserted patch column"); });
    addCommand(patch, "PatchInsertRow", "Insert Row", QKeySequence("Ctrl+Shift+="),
               [this, patchCmd] { patchCmd(insertPatchRowCol(ctx_, true), "Inserted patch row"); });
    addCommand(patch, "PatchDeleteColumn", "Delete Column", QKeySequence("Ctrl+-"),
               [this, patchCmd] { patchCmd(removePatchRowCol(ctx_, false), "Deleted patch column"); });
    addCommand(patch, "PatchDeleteRow", "Delete Row", QKeySequence("Ctrl+Shift+-"),
               [this, patchCmd] { patchCmd(removePatchRowCol(ctx_, true), "Deleted patch row"); });

    QMenu* compile = menuBar()->addMenu("&Compile");
    auto showCompile = [this] { showDock("Compile"); };
    addCommand(compile, "CompileMap", "Compile Map (extraction, zones, validate)", QKeySequence("F7"), [this, showCompile] {
        showCompile();
        compile_->run({CompilePanel::Stage::Extraction, CompilePanel::Stage::Zones, CompilePanel::Stage::Validate});
    });
    addCommand(compile, "ForceFullRebuild", "Force Full Rebuild (rebuild the zones even if nothing changed)", QKeySequence(),
               [this, showCompile] {
                   showCompile();
                   compile_->forceFullRebuild();
               });
    addCommand(compile, "CompileExtraction", "Compile IW8 Extraction Only", QKeySequence("Shift+F7"), [this, showCompile] {
        showCompile();
        compile_->run({CompilePanel::Stage::Extraction});
    });
    addCommand(compile, "MapAtmosphere", "Atmosphere (Fog / Film Grade)...", QKeySequence(), [this] { editAtmosphere(ctx_, this); });
    addCommand(compile, "MapSunSky", "Sun && Sky...", QKeySequence(), [this] { editSunSky(ctx_, stockSkyList(), this); });
    addCommand(compile, "ZoneDependencies", "Zone Dependencies (last build)...", QKeySequence(), [this] {
        // 1.15 (core zone_deps): which stock zones the build relies on and which carried copies could be referenced.
        const QString extracted = compile_->extractionDir() + "/extracted";
        if (!QFileInfo::exists(extracted)) return ctx_.setStatus("Compile the map first (F7)");
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog")); ++up) d.cdUp();
        const ZoneDepReport rep = makeZoneDepReport(extracted.toStdString(), compile_->mapId().toStdString(),
                                                    d.filePath("data/asset_catalog").toStdString());
        QDialog dlg(this);
        dlg.setObjectName("zoneDepsDialog");
        dlg.setWindowTitle("Zone Dependencies: " + compile_->mapId());
        dlg.resize(900, 640);
        auto* lay = new QVBoxLayout(&dlg);
        auto* text = new QPlainTextEdit(qs(formatZoneDepReport(rep)), &dlg);
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        lay->addWidget(text);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(box);
        dlg.exec();
    });
    addCommand(compile, "VisionEditor", "Vision Editor (every field: fog, tonemap, bloom, grade)...", QKeySequence(), [this] {
        // 5.6: every vision field of the game by name, with its type and default; the values set here are the
        // worldspawn iw8r_vision overrides the build applies last. A selected trigger with iw8r_vision (a vision
        // volume) is edited instead of the map's own vision.
        Id target = kNoId;
        QString title = "Vision Editor: the map";
        for (const SelRef& r : ctx_.doc.selection()) {
            const Entity* e = ctx_.doc.findEntity(r.entity);
            if (e && !e->isWorldspawn() && e->classname().rfind("trigger_", 0) == 0) {
                target = e->id;
                const std::string* tn = e->value("targetname");
                title = "Vision Editor: volume " + (tn ? qs(*tn) : QString("(no targetname)"));
                break;
            }
        }
        if (target == kNoId) {
            const Entity* ws = ctx_.doc.worldspawn();
            if (!ws) return;
            target = ws->id;
        }
        const Entity* e = ctx_.doc.findEntity(target);
        const std::string* cur = e ? e->value(kVisionKey) : nullptr;
        QString text = cur ? qs(*cur) : QString();
        if (!showVisionEditorDialog(this, title, text)) return;
        if (text.trimmed().isEmpty()) {
            if (cur) ctx_.doc.removeKey(target, kVisionKey);
        } else {
            ctx_.doc.setKeyValue(target, kVisionKey, text.toStdString());
        }
        ctx_.documentEdited(text.trimmed().isEmpty() ? QString("Vision overrides cleared") : "Vision overrides set (" + QString::number(text.count('=')) + " field(s); used by the next build)");
    });
    addCommand(compile, "SunFromSky", "Sun From Sky (aim sundirection at the sky's sun)", QKeySequence(), [this] {
        const QString why = sunFromSky(false);
        if (!why.isEmpty()) ctx_.setStatus("Sun From Sky: " + why);
    });
    {
        QAction* a = addCommand(compile, "AutoSunFromSky", "Aim the Sun When the Sky Changes", QKeySequence(), [this] {
            const bool on = !Settings::instance().value("sky/autoSun", true).toBool();
            Settings::instance().setValue("sky/autoSun", on);
            if (QAction* x = action("AutoSunFromSky")) x->setChecked(on);
        });
        a->setCheckable(true);
        a->setChecked(Settings::instance().value("sky/autoSun", true).toBool());
    }
    addCommand(compile, "SkyFromPanorama", "Sky From Panorama (equirectangular PNG / JPG / HDR)...", QKeySequence(), [this] {
        // 5.17 (core sky_panorama): the panorama becomes a cube DDS next to the map (sky/<name>.dds), worldspawn
        // skyname + iw8r_sky_file name it, and the sun can be aimed at the panorama's brightest spot. One undo step.
        if (ctx_.doc.path().empty()) return ctx_.setStatus("Save the map first: the sky file is written next to it");
        const QString src = QFileDialog::getOpenFileName(this, "Sky From Panorama", QFileInfo(qs(ctx_.doc.path())).absolutePath(),
                                                         "Panoramas (*.hdr *.png *.jpg *.jpeg *.tga)");
        if (src.isEmpty()) return;
        bool ok = false;
        const double yaw = QInputDialog::getDouble(this, "Sky From Panorama", "Rotate the sky (degrees):", 0, -360, 360, 0, &ok);
        if (!ok) return;
        const double ev = QInputDialog::getDouble(this, "Sky From Panorama", "Exposure (EV stops):", 0, -8, 8, 1, &ok);
        if (!ok) return;
        FloatImage pano;
        std::string err;
        if (!readPanorama(src.toStdString(), pano, &err)) return ctx_.setStatus("Panorama: " + qs(err));
        const std::string cube = cubeDdsFromPanorama(pano, 1024, yaw, ev, &err);
        if (cube.empty()) return ctx_.setStatus("Panorama: " + qs(err));
        QString stem = QFileInfo(src).completeBaseName().toLower();
        stem.replace(QRegularExpression("[^a-z0-9_]"), "_");
        const QString name = "iw8r_sky_" + stem.left(40);
        const QDir mapDir = QFileInfo(qs(ctx_.doc.path())).absoluteDir();
        mapDir.mkpath("sky");
        const QString rel = "sky/" + name + ".dds";
        QFile out(mapDir.absoluteFilePath(rel));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return ctx_.setStatus("Cannot write " + out.fileName());
        out.write(cube.data(), qint64(cube.size()));
        out.close();
        Entity* ws = ctx_.doc.worldspawn();
        if (!ws) return;
        const Id wid = ws->id;
        const bool aimSun = QMessageBox::question(this, "Sky From Panorama", "Aim the sun at the brightest spot of the panorama?") == QMessageBox::Yes;
        ctx_.doc.beginGroup("Sky From Panorama");
        ctx_.doc.setKeyValue(wid, "skyname", name.toStdString());
        ctx_.doc.setKeyValue(wid, "iw8r_sky_file", rel.toStdString());
        if (aimSun) ctx_.doc.setKeyValue(wid, "sundirection", sunDirectionKeyFor(brightestDirection(pano, yaw)));
        ctx_.doc.endGroup();
        ctx_.scene.invalidate();
        ctx_.documentEdited("Sky " + name + " from " + QFileInfo(src).fileName() + (aimSun ? " (sun aimed at its brightest spot)" : QString()));
    });
    {
        // Time-of-day presets (5.19): sun, sky, fog, film and exposure in one undo step; Shift keeps the current sky.
        QMenu* tod = compile->addMenu("Time of Day");
        tod->setObjectName("timeOfDayMenu");
        for (const TodPreset& p : todPresets()) {
            const std::string id = p.id;
            addCommand(tod, QString("TimeOfDay_") + p.id, p.title, QKeySequence(), [this, id] {
                const bool keepSky = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
                // One undo step for the preset and the sun aim that follows it.
                struct Group {
                    Document& d;
                    ~Group() { d.endGroup(); }
                } group{ctx_.doc};
                ctx_.doc.beginGroup("Time of Day");
                if (!applyTodPreset(ctx_.doc, id, keepSky)) return ctx_.setStatus("Time of day: nothing to change");
                ctx_.scene.invalidate();  // the lit preview reads the sun keys
                ctx_.documentEdited(QString("Time of day: %1%2 (tune it in Sun & Sky and Atmosphere)")
                                        .arg(QString::fromUtf8(findTodPreset(id)->title), keepSky ? ", sky kept" : ""));
                // Owner playtest 2026-11-11: the preset's sun did not line up with the sun painted in its sky. With
                // "Aim the Sun When the Sky Changes" on, the sun is aimed at the new sky's sun (the same undo step; a
                // sky without a clear sun keeps the preset's direction).
                if (!keepSky && Settings::instance().value("sky/autoSun", true).toBool()) {
                    const QString why = sunFromSky(true);
                    if (!why.isEmpty()) ctx_.setStatus("Time of day: the preset's sun direction is kept (" + why + ")");
                    ctx_.scene.invalidate();
                }
            });
        }
    }
    addCommand(compile, "BuildZones", "Build Zones From Last Extraction (x64-zt)", QKeySequence(), [this, showCompile] {
        showCompile();
        compile_->run({CompilePanel::Stage::Zones, CompilePanel::Stage::Validate});
    });
    addCommand(compile, "ValidateZones", "Validate Built Zones (ACTS --test)", QKeySequence(), [this, showCompile] {
        showCompile();
        compile_->run({CompilePanel::Stage::Validate});
    });
    compile->addSeparator();
    // 7.8: game to editor (direct link): the player's view drives editor actions.
    addCommand(compile, "SelectPlayerTarget", "Select What the Player Looks At (Live Link)", QKeySequence(), [this] {
        QString err;
        const auto v = playerView(&err);
        if (!v) return ctx_.setStatus("Select what the player looks at: " + err);
        selectAlongView(v->first, v->second);
    });
    addCommand(compile, "PlaceAtPlayer", "Move Selection to the Player (Live Link)", QKeySequence(), [this] {
        QString err;
        const auto v = playerView(&err);
        if (!v) return ctx_.setStatus("Move selection to the player: " + err);
        moveSelectionTo(v->first);
    });
    addCommand(compile, "CameraToPlayer", "Editor Camera to the Player's View (Live Link)", QKeySequence(), [this] {
        QString err;
        const auto v = playerView(&err);
        if (!v) return ctx_.setStatus("Camera to the player's view: " + err);
        ctx_.cam.pos = v->first + Vec3{0, 0, 60};
        ctx_.cam.pitch = -v->second.x;  // the game's positive pitch looks down; the editor's looks up
        ctx_.cam.yaw = v->second.y;
        ctx_.cameraMoved();
        ctx_.setStatus("Editor camera at the player's view (Ctrl+Shift+1..9 saves it as a camera bookmark)");
    });
    addCommand(compile, "LoadMapInGame", "Load Map in Game (Live Link)", QKeySequence("Ctrl+F8"), [this] {
        // 7.2: the running Replay (direct link) loads the compiled map; deploy it first.
        if (liveLink_ && liveLink_->loadMapInGame()) ctx_.setStatus("Sent to the game: " + LiveLinkPanel::loadMapCommand(compile_->mapId()));
        else ctx_.setStatus(liveLink_ ? liveLink_->messageText() : QString("Live Link is not available"));
    });
    addCommand(compile, "DeployMap", "Deploy to Replay Custom Maps...", QKeySequence(), [this, showCompile] {
        showCompile();
        const QString gameRoot = QFileInfo(toolPath("replay")).absolutePath();
        const QString target = gameRoot + "/mods/mw120r/maps/" + compile_->mapId();
        if (compile_->packageFiles().isEmpty())
            return (void)QMessageBox::information(this, "Deploy", "No complete package for " + compile_->mapId() + " yet. Run Compile Map (F7) first.");
        if (QMessageBox::question(this, "Deploy to Replay",
                                  "Install the five zones and map.json into\n" + target +
                                      "\n\nAn existing package there is moved to .proxy\\backups and can be restored with Undo Last Deploy."
                                      "\nReplay may stay open (a map that is loaded right now cannot be replaced until another map is"
                                      " loaded; a map id that is new to the running game needs one game restart)."
                                      " In-game loading is not verified by the editor.") != QMessageBox::Yes)
            return;
        QString err;
        if (!compile_->deploy(gameRoot, &err)) QMessageBox::warning(this, "Deploy", err);
    });
    addCommand(compile, "CompileAndDeploy", "Compile Map and Deploy (Replay may stay open)", QKeySequence("Ctrl+F7"), [this, showCompile] {
        showCompile();
        if (compile_->busy()) return;
        pendingDeploy_ = true;
        compile_->run({CompilePanel::Stage::Extraction, CompilePanel::Stage::Zones, CompilePanel::Stage::Validate});
    });
    addCommand(compile, "SetLoadscreen", "Set Loadscreen From Camera", QKeySequence(), [this] {
        // 1.17: the current view (game surfaces only) becomes the loading screen / map-select preview of the next build.
        QString err;
        const QString f = setLoadscreenFromCamera(&err);
        ctx_.setStatus(f.isEmpty() ? "Loadscreen: " + err : "Loadscreen saved as " + QFileInfo(f).fileName() + "; the next build (F7) uses it");
    });
    addCommand(compile, "CaptureProbes", "Capture Reflection Probes (camera renderer)", QKeySequence(), [this] {
        // Uses the current camera mode: Textured (with Lit preview for sun and lights) gives the closest reflections.
        captureReflectionProbes();
    });
    addCommand(compile, "BuildDeployLoad", "Build, Deploy and Play in Game (devmap)", QKeySequence("F5"), [this, showCompile] {
        // F5 devmap (docs/research/65): F7, then DevmapRunner deploys and puts you in game on the map: it starts the
        // game when it is not running, hosts a private match from the MP menu, ends a running match, or reloads the
        // running map (DW map_reload). Replay MP has no map/devmap command; the lobby start is used instead.
        showCompile();
        if (compile_->busy()) return;
        if (auto* runner = findChild<DevmapRunner*>("devmapRunner")) runner->cancel("a new F5");
        pendingDeploy_ = true;
        pendingLoad_ = true;
        compile_->run({CompilePanel::Stage::Extraction, CompilePanel::Stage::Zones, CompilePanel::Stage::Validate});
    });
    connect(compile_, &CompilePanel::finished, this, [this](bool ok) {
        if (!pendingDeploy_) return;
        pendingDeploy_ = false;
        const bool load = pendingLoad_;
        pendingLoad_ = false;
        if (!ok) return;
        const QString gameRoot = QFileInfo(toolPath("replay")).absolutePath();
        if (load) {
            // F5: the runner deploys when no game holds the package and drives the game into the map.
            auto* runner = findChild<DevmapRunner*>("devmapRunner");
            if (!runner) {
                runner = new DevmapRunner(this);
                connect(runner, &DevmapRunner::status, this, [this](const QString& text, bool error) {
                    ctx_.setStatus(text);
                    compile_->appendLog(text, error);
                });
            }
            DevmapRunner::Config cfg;
            cfg.mapId = compile_->mapId();
            cfg.gameRoot = gameRoot;
            QString exeNote;
            cfg.launchExe = DevmapRunner::chooseLaunchExe(toolPath("launch"), toolPath("replay"), &exeNote);
            DevmapRunner::applySettings(cfg);
            if (!exeNote.isEmpty() && !CompilePanel::gameRunningFrom(gameRoot)) compile_->appendLog("F5: " + exeNote, exeNote.startsWith("warning"));
            runner->start(cfg, [this, gameRoot](QString* err) { return compile_->deploy(gameRoot, err); });
            return;
        }
        QString err;
        if (!compile_->deploy(gameRoot, &err)) return ctx_.setStatus("Deploy failed: " + err);
    });
    addCommand(compile, "LaunchReplay", "Launch Replay", QKeySequence(), [this] {
        // Starts the game exe in the game folder with no arguments (F5 is the one that also starts the map).
        const QString exe = DevmapRunner::chooseLaunchExe(toolPath("launch"), toolPath("replay"));
        const QString dir = QFileInfo(exe).absolutePath();
        if (!QFileInfo::exists(exe)) return ctx_.setStatus("No game exe at " + exe + " (Tools > Tool Paths)");
        if (DevmapRunner::hostCheckSetting() && !DevmapRunner::localHostOf(exe).isEmpty() &&
            !DevmapRunner::probeHost(DevmapRunner::hostHealthSetting()))
            return ctx_.setStatus("The local online-services host does not answer (" + DevmapRunner::hostHealthSetting() + "); " +
                                  QFileInfo(exe).fileName() + " needs it. Start Host-Local-Server.ps1 -NoLaunch, or press F5 (it starts the host)");
        if (CompilePanel::gameRunningFrom(dir)) return ctx_.setStatus("Replay is already running from " + dir);
        if (QProcess::startDetached(exe, {}, dir)) ctx_.setStatus("Started " + QFileInfo(exe).fileName() + "; F5 starts " + compile_->mapId() + " for you");
        else ctx_.setStatus("Could not start " + exe);
    });
    addCommand(compile, "ShowReplayLog", "Show Replay Log", QKeySequence(), [this] { showDock("Replay Log"); });
    addCommand(compile, "UndoDeploy", "Undo Last Deploy", QKeySequence(), [this] {
        QString err;
        if (!compile_->undoDeploy(&err)) QMessageBox::warning(this, "Undo Deploy", err);
    });
    compile->addSeparator();
    addCommand(compile, "CancelCompile", "Cancel Compile", QKeySequence(), [this] { compile_->cancel(); });
    addCommand(compile, "OpenBuildFolder", "Open Build Folder", QKeySequence(), [this] {
        QDir().mkpath(compile_->buildRoot());
        QDesktopServices::openUrl(QUrl::fromLocalFile(compile_->buildRoot()));
    });

    QMenu* tools = menuBar()->addMenu("&Tools");
    addCommand(tools, "MaterialInfo", "Material Info (current material)...", QKeySequence("Ctrl+Alt+M"), [this] {
        if (ctx_.currentMaterial.empty()) return ctx_.setStatus("No current material: pick one in the Materials browser");
        showMaterialInfo(qs(ctx_.currentMaterial));
    });
    // L12 asset editing (owner request 2026-11-11).
    addCommand(tools, "CloneStockMaterial", "Make Editable Copy of the Current Material...", QKeySequence(), [this] {
        // 12.5: the stock material's images and a definition are written into the map's materials folder; the copy is a
        // custom material the Material Editor can change (shader state, tint, images).
        if (ctx_.currentMaterial.empty()) return ctx_.setStatus("Editable copy: pick a stock material in the Materials browser first");
        if (ctx_.doc.path().empty()) return ctx_.setStatus("Editable copy: save the map first (the copy goes into its materials folder)");
        const QString stock = qs(ctx_.currentMaterial);
        const auto entry = matCache_->lookup(stock);
        if (!entry || entry->failed || entry->colorDds.isEmpty()) {
            matCache_->request({stock});
            return ctx_.setStatus("Editable copy: " + stock + " is not in the texture cache yet (it is being fetched; try again in a moment)");
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Make Editable Copy", "Name of the new material (lower case, a-z 0-9 _ /):",
                                                   QLineEdit::Normal, suggestCloneName(stock, {}), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        MaterialCloneResult res;
        QString err;
        if (!cloneStockMaterial(*entry, customMaterialRoots().front(), name, &res, &err))
            return ctx_.setStatus("Editable copy failed: " + err);
        reloadCustomAssets();
        ctx_.currentMaterial = name.toStdString();
        if (materialEdit_) materialEdit_->setText(name);
        ctx_.setStatus("Editable copy " + name + " of " + stock + " written to " + res.definitionFile +
                       (res.warnings.isEmpty() ? QString() : " (" + res.warnings.join("; ") + ")"));
        editCustomMaterial(name);
    });
    addCommand(tools, "AdjustImage", "Adjust Image (brightness, tint, resize, normal map from height)...", QKeySequence(), [this] {
        // 12.4: a custom material's image (or any PNG / TGA / JPG) adjusted and saved as a new PNG.
        QString saved;
        if (showImageAdjustDialog(this, QString(), &saved) && !saved.isEmpty()) {
            reloadCustomAssets();
            ctx_.setStatus("Adjusted image saved: " + saved);
        }
    });
    addCommand(tools, "TechsetInfo", "Techset Info (current material)...", QKeySequence(), [this] {
        // 12.1: what the current stock material's technique set does (blend, alpha test, lighting), from its name and,
        // when it has been exported, from the technique set itself.
        if (ctx_.currentMaterial.empty()) return ctx_.setStatus("Techset Info: pick a material in the Materials browser first");
        const QString name = qs(ctx_.currentMaterial);
        const auto entry = matCache_->lookup(name);
        if (!entry || entry->failed) {
            matCache_->request({name});
            return ctx_.setStatus("Techset Info: " + name + " is not in the material cache yet (it is being fetched; try again in a moment)");
        }
        // The technique set body is exported once (ACTS, zone techsets_common_mp) and kept; the dialog then lists its
        // techniques. Without it the dialog explains the set from its name.
        const QString techset = entry->techset;
        ctx_.setStatus("Techset Info: reading " + techset + "...");
        requestTechsetExport([this](const QString& k) { return toolPath(k); }, techset, this,
                             [this, name, techset](const QString& path, const QString& err) {
                                 if (!err.isEmpty()) ctx_.setStatus("Techset Info: the technique set body could not be exported (" + err + "); showing what its name says");
                                 else ctx_.setStatus("Techset Info: " + techset);
                                 showTechsetDialog(this, name, techset, path);
                             });
    });
    addCommand(tools, "ReplaceMaterialInMap", "Replace a Material in the Whole Map...", QKeySequence(), [this] {
        // 12.9: every brush face and patch using one material takes the current material (one undo step); model keys
        // (iw8r_material, iw8r_mat:...) holding it follow.
        if (ctx_.currentMaterial.empty()) return ctx_.setStatus("Replace material: pick the new material in the Materials browser first");
        QStringList used;
        for (const std::string& m : materialsInMap(ctx_.doc.map())) used << qs(m);
        if (used.isEmpty()) return ctx_.setStatus("Replace material: the map uses no materials yet");
        const QString sel = qs(materialOfSelection(ctx_)).toLower();
        bool ok = false;
        const QString from = QInputDialog::getItem(this, "Replace a Material in the Whole Map",
                                                   "Material to replace with " + qs(ctx_.currentMaterial) + ":", used,
                                                   std::max(0, int(used.indexOf(sel))), false, &ok);
        if (!ok || from.isEmpty()) return;
        const MaterialReplaceResult plan = planMaterialReplace(ctx_.doc.map(), from.toStdString(), ctx_.currentMaterial);
        if (plan.edits.empty() && plan.keys.empty()) return ctx_.setStatus("Replace material: nothing uses " + from);
        std::vector<Document::PrimEdit> edits;
        for (const MaterialReplaceEdit& e : plan.edits) edits.push_back({e.entity, e.item, e.prim});
        ctx_.doc.beginGroup("Replace material " + from.toStdString());
        if (!edits.empty()) ctx_.doc.editPrimitives(edits, "Replace material " + from.toStdString(), false);
        for (const auto& [entity, key] : plan.keys) ctx_.doc.setKeyValue(entity, key, ctx_.currentMaterial);
        ctx_.doc.endGroup();
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("Replaced %1 by %2: %3 face(s), %4 patch(es), %5 model key(s) (one undo step)")
                                .arg(from, qs(ctx_.currentMaterial)).arg(plan.faces).arg(plan.patches).arg(plan.keys.size()));
    });
    addCommand(tools, "ReplaceModelMaterial", "Replace One Material of the Selected Model...", QKeySequence(), [this] {
        // 12.7: one material slot of a stock model takes the current material (key iw8r_mat:<slot>). The camera shows
        // it and the build makes a copy of the model with that slot replaced.
        if (ctx_.currentMaterial.empty()) return ctx_.setStatus("Replace model material: pick the new material in the Materials browser first");
        for (const SelRef& r : ctx_.doc.selection()) {
            const Entity* e = ctx_.doc.findEntity(r.entity);
            const std::string* m = e ? e->value("model") : nullptr;
            if (!m || m->empty() || (*m)[0] == '*') continue;
            const auto entry = modelCacheStock_ ? modelCacheStock_->lookup(qs(*m)) : std::nullopt;
            if (!entry || entry->failed || entry->materials.isEmpty())
                return ctx_.setStatus("Replace model material: " + qs(*m) + " is not a cached stock model (only stock models have replaceable slots)");
            QStringList slotNames = entry->materials;
            slotNames.removeDuplicates();
            bool ok = false;
            const QString slot = QInputDialog::getItem(this, "Replace One Material", "Material of " + qs(*m) + " to replace with " +
                                                       qs(ctx_.currentMaterial) + ":", slotNames, 0, false, &ok);
            if (!ok || slot.isEmpty()) return;
            ctx_.doc.setKeyValue(r.entity, "iw8r_mat:" + slot.toStdString(), ctx_.currentMaterial);
            ctx_.scene.invalidate();
            ctx_.documentEdited("Model " + qs(*m) + ": " + slot + " replaced by " + qs(ctx_.currentMaterial) + " (key iw8r_mat:" + slot + "; undoable)");
            return;
        }
        ctx_.setStatus("Replace model material: select a stock model first");
    });
    addCommand(tools, "AssetUsage", "Asset Usage (what the map uses, unused own files)...", QKeySequence(), [this] {
        const QString base = ctx_.doc.path().empty() ? QString() : QFileInfo(customMaterialRoots().front()).absolutePath();
        showAssetUsageDialog(this, ctx_.doc.map(), customMaterialRoots(), customModelRoots(),
                             base.isEmpty() ? QStringList() : QStringList{base + "/sounds"},
                             base.isEmpty() ? QStringList() : QStringList{base + "/fx"});
    });
    addCommand(tools, "SoundSettings", "Sound Settings (one of the map's own sounds)...", QKeySequence(), [this] {
        if (ctx_.doc.path().empty()) return ctx_.setStatus("Sound settings: save the map first (its sounds are in the sounds folder next to it)");
        const QString wav = QFileDialog::getOpenFileName(this, "Sound Settings: pick a sound", QFileInfo(customMaterialRoots().front()).absolutePath() + "/sounds", "Sounds (*.wav)");
        if (wav.isEmpty()) return;
        if (showSoundSettingsDialog(this, wav)) ctx_.setStatus("Sound settings saved for " + QFileInfo(wav).fileName() + " (used by the next build)");
    });
    addCommand(tools, "ModelInfo", "Model Info (selected model)...", QKeySequence("Ctrl+Alt+I"), [this] {
        for (const SelRef& r : ctx_.doc.selection()) {
            const Entity* e = ctx_.doc.findEntity(r.entity);
            const std::string* m = e ? e->value("model") : nullptr;
            if (m && !m->empty() && (*m)[0] != '*') {
                showModelInfo(qs(*m));
                return;
            }
        }
        ctx_.setStatus("Select an entity with a model first (or right-click a model in the Models browser)");
    });
    connect(materials_, &AssetBrowser::infoRequested, this, [this](const QString& n) { showMaterialInfo(n); });
    // 4.22: Replace Everywhere With... from the browsers' context menu (one undo step).
    connect(materials_, &AssetBrowser::replaceRequested, this, [this](const QString& n) {
        bool ok = false;
        const QString to = QInputDialog::getText(this, "Replace Everywhere", "Replace material " + n + " on every face with:", QLineEdit::Normal,
                                                 qs(ctx_.currentMaterial), &ok).trimmed();
        if (!ok || to.isEmpty() || to == n) return;
        const size_t c = replaceEverywhere(true, n, to);
        ctx_.setStatus(QString("Replaced %1 face(s) of %2 with %3 (one undo step)").arg(c).arg(n, to));
    });
    connect(models_, &AssetBrowser::replaceRequested, this, [this](const QString& n) {
        bool ok = false;
        const QString to = QInputDialog::getText(this, "Replace Everywhere", "Replace model " + n + " on every entity with:", QLineEdit::Normal, n, &ok).trimmed();
        if (!ok || to.isEmpty() || to == n) return;
        const size_t c = replaceEverywhere(false, n, to);
        ctx_.setStatus(QString("Replaced the model on %1 entity(ies) (one undo step)").arg(c));
    });
    connect(models_, &AssetBrowser::infoRequested, this, [this](const QString& n) { showModelInfo(n); });
    connect(effects_, &AssetBrowser::infoRequested, this, [this](const QString& n) { showEffectInfo(n); });
    tools->addSeparator();
    addCommand(tools, "CreateRope", "Create Rope (between two selected)", QKeySequence(), [this] {
        ctx_.documentEdited(createRope(ctx_) ? QStringLiteral("Rope created (undoable)") : QStringLiteral("Select two entities or primitives"));
    });
    addCommand(tools, "ScatterByRules", "Scatter by Rules (slope, material, spacing)...", QKeySequence(), [this] {
        // 8.12 (core scatter_rules): misc_models over the selection's bounds (or the whole map) on surfaces that pass the
        // slope and material filters, with a minimum spacing. One undo step; the last settings are remembered.
        QDialog dlg(this);
        dlg.setObjectName("scatterRulesDialog");
        dlg.setWindowTitle("Scatter by Rules");
        auto* form = new QFormLayout(&dlg);
        auto& S = Settings::instance();
        auto* models = new QLineEdit(S.value("scatter/models", models_ ? models_->visibleName(0) : QString()).toString(), &dlg);
        models->setToolTip("Stock or custom model names, comma separated (weights: name*3)");
        auto* density = new QDoubleSpinBox(&dlg);
        density->setRange(0.1, 200);
        density->setValue(S.value("scatter/density", 6.0).toDouble());
        auto* spacing = new QDoubleSpinBox(&dlg);
        spacing->setRange(0, 4096);
        spacing->setValue(S.value("scatter/spacing", 48.0).toDouble());
        auto* slope = new QDoubleSpinBox(&dlg);
        slope->setRange(0, 90);
        slope->setValue(S.value("scatter/maxSlope", 30.0).toDouble());
        auto* allow = new QLineEdit(S.value("scatter/allow").toString(), &dlg);
        allow->setPlaceholderText("e.g. grass, dirt (empty = any material)");
        auto* deny = new QLineEdit(S.value("scatter/deny").toString(), &dlg);
        deny->setPlaceholderText("e.g. concrete, metal");
        auto* align = new QCheckBox("Tilt to the surface", &dlg);
        align->setChecked(S.value("scatter/align", false).toBool());
        auto* seed = new QSpinBox(&dlg);
        seed->setRange(1, 999999);
        seed->setValue(S.value("scatter/seed", 1).toInt());
        form->addRow("Models", models);
        form->addRow("Per 256x256 units", density);
        form->addRow("Min spacing", spacing);
        form->addRow("Max slope (degrees)", slope);
        form->addRow("Only on materials containing", allow);
        form->addRow("Never on materials containing", deny);
        form->addRow("", align);
        form->addRow("Seed", seed);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(box);
        if (dlg.exec() != QDialog::Accepted) return;
        S.setValue("scatter/models", models->text());
        S.setValue("scatter/density", density->value());
        S.setValue("scatter/spacing", spacing->value());
        S.setValue("scatter/maxSlope", slope->value());
        S.setValue("scatter/allow", allow->text());
        S.setValue("scatter/deny", deny->text());
        S.setValue("scatter/align", align->isChecked());
        S.setValue("scatter/seed", seed->value());
        ScatterRule rule;
        for (const QString& m : models->text().split(',', Qt::SkipEmptyParts)) {
            const QString name = m.section('*', 0, 0).trimmed();
            if (name.isEmpty()) continue;
            rule.models.push_back(name.toStdString());
            rule.weights.push_back(std::max(0.01, m.section('*', 1, 1).trimmed().isEmpty() ? 1.0 : m.section('*', 1, 1).toDouble()));
        }
        if (rule.models.empty()) return ctx_.setStatus("Scatter: name at least one model");
        auto words = [](const QString& t) {
            std::vector<std::string> out;
            for (const QString& w : t.split(',', Qt::SkipEmptyParts))
                if (!w.trimmed().isEmpty()) out.push_back(w.trimmed().toStdString());
            return out;
        };
        rule.density = density->value();
        rule.minSpacing = spacing->value();
        rule.maxSlopeDeg = slope->value();
        rule.materialsAllow = words(allow->text());
        rule.materialsDeny = words(deny->text());
        rule.alignToNormal = align->isChecked();
        rule.seed = unsigned(seed->value());
        const AABB sel = ctx_.doc.selectionBounds();
        const auto tris = collectSurfaces(ctx_.doc.map(), sel.valid() ? &sel : nullptr);
        const auto placed = scatter(tris, rule);
        if (placed.empty()) return ctx_.setStatus("Scatter: no surface passed the filters (check slope and materials)");
        const size_t n = placeScatter(ctx_.doc, placed);
        ctx_.scene.invalidate();
        ctx_.documentEdited(QString("Scattered %1 model(s) %2 (one undo step)").arg(n).arg(sel.valid() ? "in the selection's bounds" : "over the map"));
    });
    addCommand(tools, "ScatterModels", "Scatter Models (Mesh Painter)...", QKeySequence(), [this] {
        bool ok = false;
        const QString model = QInputDialog::getText(this, "Scatter Models", "Model (stock or custom name):", QLineEdit::Normal,
                                                    Settings::instance().value("scatter/model").toString(), &ok);
        if (!ok || model.trimmed().isEmpty()) return;
        const int count = QInputDialog::getInt(this, "Scatter Models", "Count:", 20, 1, 500, 1, &ok);
        if (!ok) return;
        Settings::instance().setValue("scatter/model", model.trimmed());
        const size_t n = scatterModels(ctx_, model.trimmed(), count, 256, uint32_t(QDateTime::currentMSecsSinceEpoch()));
        ctx_.documentEdited(QString("Scattered %1 x %2 around the 2D view centre (one undo step)").arg(n).arg(model.trimmed()));
    });
    {
        QAction* paint = addCommand(tools, "ModelPaintMode", "Model Paint Mode (click to paint)...", QKeySequence(), [this] {
            QAction* a = action("ModelPaintMode");
            if (!ctx_.paintModel.isEmpty()) {
                ctx_.paintModel.clear();
                if (a) a->setChecked(false);
                return ctx_.setStatus("Model paint mode off");
            }
            bool ok = false;
            const QString model = QInputDialog::getText(this, "Model Paint Mode", "Model to paint (stock or custom name):", QLineEdit::Normal,
                                                        Settings::instance().value("scatter/model").toString(), &ok).trimmed();
            if (!ok || model.isEmpty()) {
                if (a) a->setChecked(false);
                return;
            }
            const int count = QInputDialog::getInt(this, "Model Paint Mode", "Models per click:", ctx_.paintCount, 1, 50, 1, &ok);
            if (!ok) return;
            const int radius = QInputDialog::getInt(this, "Model Paint Mode", "Brush radius (units):", int(ctx_.paintRadius), 8, 2048, 8, &ok);
            if (!ok) return;
            Settings::instance().setValue("scatter/model", model);
            ctx_.paintModel = model;
            ctx_.paintCount = count;
            ctx_.paintRadius = radius;
            if (a) a->setChecked(true);
            ctx_.setStatus(QString("Model paint mode: left-click surfaces in the camera to place %1 x %2 within %3 units").arg(count).arg(model).arg(radius));
        });
        if (paint) paint->setCheckable(true);
    }
    {
        QAction* m = addCommand(tools, "MeasureTool", "Measure Tool (drag in a 2D view)", QKeySequence("Alt+M"), [this] {
            ctx_.measureMode = !ctx_.measureMode;
            if (!ctx_.measureMode) ctx_.measureValid = false;
            if (QAction* a = action("MeasureTool")) a->setChecked(ctx_.measureMode);
            ctx_.setStatus(ctx_.measureMode ? "Measure: left-drag in a 2D view (Alt+M to stop)" : "Measure tool off");
            if (grid_) grid_->update();
        });
        if (m) m->setCheckable(true);
    }
    {
        QAction* web = addCommand(tools, "WebServer", "Web Server (use the suite from a browser)", QKeySequence(), [this] {
            Settings::instance().setValue("web/enabled", !Settings::instance().value("web/enabled", false).toBool());
            QString msg;
            applyWebServerSettings(&msg);
            ctx_.setStatus(msg);
        });
        if (web) web->setCheckable(true);
        addCommand(tools, "WebServerSettings", "Web Server Settings...", QKeySequence(), [this] {
            auto& s = Settings::instance();
            QDialog dlg(this);
            dlg.setWindowTitle("Web Server");
            auto* form = new QFormLayout(&dlg);
            auto* enabled = new QCheckBox("Serve the editor to browsers", &dlg);
            enabled->setChecked(s.value("web/enabled", false).toBool());
            auto* port = new QSpinBox(&dlg);
            port->setRange(1024, 65535);
            port->setValue(s.value("web/port", 8780).toInt());
            auto* lan = new QCheckBox("Allow other machines (LAN; login required)", &dlg);
            lan->setChecked(s.value("web/lan", false).toBool());
            auto* upnp = new QCheckBox("Open the port on the router (UPnP) for access from the internet", &dlg);
            upnp->setChecked(s.value("web/upnp", false).toBool());
            auto* tls = new QCheckBox("HTTPS (certificate and key in web_tls beside the editor; tools/make_web_cert.ps1)", &dlg);
            tls->setChecked(s.value("web/tls", false).toBool());
            auto* user = new QLineEdit(WebServer::credentialUser(), &dlg);
            auto* pass = new QLineEdit(&dlg);
            pass->setEchoMode(QLineEdit::Password);
            pass->setPlaceholderText(WebServer::hasCredentials() ? "(unchanged; type to replace)" : "required for remote access");
            auto* note = new QLabel("Without HTTPS, logins travel unencrypted. With a self-signed certificate browsers warn once; "
                                    "accept it to continue.", &dlg);
            note->setWordWrap(true);
            note->setStyleSheet("color: gray");
            form->addRow(enabled);
            form->addRow("Port", port);
            form->addRow(lan);
            form->addRow(upnp);
            form->addRow(tls);
            form->addRow("Username", user);
            form->addRow("Password", pass);
            form->addRow(note);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            if (dlg.exec() != QDialog::Accepted) return;
            if ((lan->isChecked() || upnp->isChecked()) && user->text().trimmed().isEmpty())
                return (void)QMessageBox::warning(this, "Web Server", "Remote access needs a username and password.");
            if (user->text().trimmed().isEmpty()) WebServer::setCredentials({}, {});
            else if (!pass->text().isEmpty() || user->text().trimmed() != WebServer::credentialUser()) {
                if (pass->text().size() < 8) return (void)QMessageBox::warning(this, "Web Server", "Use a password of at least 8 characters.");
                WebServer::setCredentials(user->text(), pass->text());
            }
            s.setValue("web/enabled", enabled->isChecked());
            s.setValue("web/port", port->value());
            s.setValue("web/lan", lan->isChecked() || upnp->isChecked());
            s.setValue("web/upnp", upnp->isChecked());
            s.setValue("web/tls", tls->isChecked());
            QString msg;
            applyWebServerSettings(&msg);
            ctx_.setStatus(msg);
        });
    }
    addCommand(tools, "DropToFloor", "Drop Selection To Floor", QKeySequence(), [this] {
        const size_t n = dropSelectionToFloor(ctx_);
        ctx_.documentEdited(QString("%1 entit%2 dropped to the floor").arg(n).arg(n == 1 ? "y" : "ies"));
    });
    addCommand(tools, "LeakCheck", "&Leak Check (draw pointfile)", QKeySequence(), [this] {
        const LeakResult leak = checkLeak(ctx_.doc.map());
        ctx_.scene.pointfile = leak.sealed ? std::vector<Vec3>{} : leak.path;
        if (!leak.error.empty()) ctx_.documentEdited("Leak check failed: " + qs(leak.error));
        else if (leak.sealed) ctx_.documentEdited(QString("Map is sealed (%1 cells checked)").arg(leak.cells));
        else ctx_.documentEdited(QString("LEAK: %1 (entity %2) reaches the void; the yellow line shows the path").arg(qs(leak.classname)).arg(leak.leakingEntity));
    });
    addCommand(tools, "ClearPointfile", "Clear Pointfile", QKeySequence(), [this] {
        ctx_.scene.pointfile.clear();
        ctx_.documentEdited();
    });
    addCommand(tools, "RecordMacro", "Start / Stop Macro Recording", QKeySequence(), [this] {
        if (!toggleMacroRecording().isEmpty())
            if (QAction* a = actions_.value("RunCommandScript")) a->trigger();  // review, save or replay the macro
    });
    addCommand(tools, "RunCommandScript", "Run Command Script...", QKeySequence(), [this] {
        // 9.10 (core command_script): batch edits as text (select / set / move / clone / add / array / align ...).
        QDialog dlg(this);
        dlg.setObjectName("commandScriptDialog");
        dlg.setWindowTitle("Run Command Script");
        dlg.resize(720, 560);
        auto* lay = new QVBoxLayout(&dlg);
        auto* help = new QLabel("One command per line: select class|key|all|none ..., set k v, remove k, move dx dy dz, clone dx dy dz, "
                                "delete, add class x y z [k v]..., material name, array dx dy dz n, align x|y|z min|center|max, "
                                "replace-key k old new, undo, redo. # starts a comment.", &dlg);
        help->setWordWrap(true);
        lay->addWidget(help);
        auto* text = new QPlainTextEdit(Settings::instance().value("scripts/last").toString(), &dlg);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        lay->addWidget(text, 1);
        auto* one = new QCheckBox("One undo step (a failing line undoes the whole script)", &dlg);
        one->setChecked(true);
        lay->addWidget(one);
        auto* out = new QPlainTextEdit(&dlg);
        out->setReadOnly(true);
        out->setMaximumHeight(120);
        lay->addWidget(out);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        QPushButton* run = box->addButton("Run", QDialogButtonBox::ActionRole);
        QPushButton* load = box->addButton("Load...", QDialogButtonBox::ActionRole);
        QPushButton* save = box->addButton("Save...", QDialogButtonBox::ActionRole);
        connect(run, &QPushButton::clicked, &dlg, [this, text, one, out] {
            Settings::instance().setValue("scripts/last", text->toPlainText());
            QStringList log;
            const QString res = runScriptText(text->toPlainText(), one->isChecked(), &log);
            out->setPlainText(log.join('\n') + (log.isEmpty() ? "" : "\n") + res);
        });
        connect(load, &QPushButton::clicked, &dlg, [this, text, &dlg] {
            const QString f = QFileDialog::getOpenFileName(&dlg, "Load Command Script", QFileInfo(qs(ctx_.doc.path())).absolutePath(), "Command scripts (*.iw8cmd *.txt)");
            QFile in(f);
            if (!f.isEmpty() && in.open(QIODevice::ReadOnly)) text->setPlainText(QString::fromUtf8(in.readAll()));
        });
        connect(save, &QPushButton::clicked, &dlg, [this, text, &dlg] {
            const QString f = QFileDialog::getSaveFileName(&dlg, "Save Command Script", QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/edits.iw8cmd", "Command scripts (*.iw8cmd)");
            QFile o(f);
            if (!f.isEmpty() && o.open(QIODevice::WriteOnly | QIODevice::Truncate)) o.write(text->toPlainText().toUtf8());
        });
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(box);
        dlg.exec();
    });
    addCommand(tools, "IesBrowser", "IES Profile Browser...", QKeySequence(), [this] {
        auto* d = new IesBrowserDialog(&ctx_, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    addCommand(tools, "AssetAudit", "Asset Availability Audit...", QKeySequence(), [this] { showAssetAudit(); });
    addCommand(tools, "EffectEditor", "Effect Editor...", QKeySequence(), [this] { openEffectEditor(); });
    addCommand(tools, "CacheManager", "Cache Manager...", QKeySequence(), [this] {
        if (auto* open = findChild<CacheManagerDialog*>()) {
            open->refresh();
            open->raise();
            return;
        }
        auto* d = new CacheManagerDialog(this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    addCommand(tools, "ListUnusedCustomAssets", "List Unused Custom Assets...", QKeySequence(), [this] {
        // 4.22: the mapper's custom materials and models the open map does not use.
        const QStringList unused = unusedCustomAssets();
        QMessageBox box(this);
        box.setWindowTitle("Unused Custom Assets");
        box.setText(unused.isEmpty() ? QString("Every custom material and model is used by this map (or there are none).")
                                     : QString("%1 custom asset(s) this map does not use (nothing is deleted):").arg(unused.size()));
        box.setDetailedText(unused.join("\n"));
        box.exec();
    });
    addCommand(tools, "GenerateSpawns", "Generate Spawns...", QKeySequence(), [this] {
        // 6.18: a starting spawn set over the walkable floors (FFA / TDM / DOM spawns and team starts).
        bool ok = false;
        const int ffa = QInputDialog::getInt(this, "Generate Spawns", "Free-for-all spawns (also TDM and Domination):", 16, 2, 64, 1, &ok);
        if (!ok) return;
        const bool replace = QMessageBox::question(this, "Generate Spawns",
                                                   "Replace the map's existing spawn entities?\n(No adds the new set beside them.)") == QMessageBox::Yes;
        QString msg;
        const int n = generateSpawnSet(ffa, 4, replace, &msg);
        ctx_.setStatus(n > 0 ? QString("Generated %1 spawn entities (one undo step)%2").arg(n).arg(msg.isEmpty() ? QString() : "; " + msg)
                             : "Generate Spawns: " + msg);
    });
    addCommand(tools, "SelectLinked", "Select Linked (target / targetname chain)", QKeySequence("Ctrl+Shift+L"), [this] {
        // Everything connected to the selection through target, target2..8 and script_linkto (6.25).
        const LinkGraph g = buildLinkGraph(ctx_.doc.map());
        std::set<Id> ents;
        for (const SelRef& r : ctx_.doc.selection()) ents.insert(r.entity);
        size_t added = 0;
        for (const Id e : std::set<Id>(ents))
            for (const Id other : connectedTo(g, e))
                if (ents.insert(other).second) {
                    const Entity* oe = ctx_.doc.findEntity(other);
                    bool prim = false;
                    if (oe)
                        for (const auto& it : oe->items)
                            if (it.isPrimitive()) {
                                ctx_.doc.select({other, it.id}, true);
                                prim = true;
                            }
                    if (!prim) ctx_.doc.select({other, kNoId}, true);
                    ++added;
                }
        ctx_.documentEdited(added ? QString("Selected %1 linked entit%2").arg(added).arg(added == 1 ? "y" : "ies")
                                  : QString("Nothing is linked to the selection"));
    });
    addCommand(tools, "GameModes", "&Game Modes (Warzone and mode presets)...", QKeySequence(), [this] { showGameModes(); });
    addCommand(tools, "CheckMap", "&Check Map...", QKeySequence(), [this] {
        auto* d = new CheckMapDialog(&ctx_, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    QAction* paths = tools->addAction("Tool Paths (ACTS, Replay, Oodle, x64-zt)...");
    connect(paths, &QAction::triggered, this, [this] { toolPathsDialog(); });
    QAction* hk = tools->addAction("Hotkeys...");
    connect(hk, &QAction::triggered, this, [this] {
        auto* d = new ads::CDockWidget(dock_, "Hotkeys");
        d->setWidget(new HotkeysPanel(actions_));
        d->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
        dock_->addDockWidgetFloating(d);
    });

    QMenu* help = menuBar()->addMenu("&Help");
    addCommand(help, "CommandPalette", "Command Palette...", QKeySequence("Ctrl+Shift+P"), [this] {
        auto* p = new CommandPalette(menuBar(), this);
        p->setAttribute(Qt::WA_DeleteOnClose);
        const QRect g = geometry();
        p->move(g.center().x() - p->sizeHint().width() / 2, g.top() + 80);
        p->show();
    });
    addCommand(help, "RestartSafeMode", "Restart in Safe Mode", QKeySequence(), [this] {
        // Default settings for one session; the saved ones stay untouched (Shift at start does the same).
        if (!confirmDiscard()) return;
        QStringList args{"--safe-mode"};
        if (!ctx_.doc.path().empty()) args << qs(ctx_.doc.path());
        if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) close();
    });
    addCommand(help, "ResetSettings", "Reset All Settings...", QKeySequence(), [this] {
        const QString ini = QDir(QCoreApplication::applicationDirPath()).filePath("IW8Radiant.ini");
        if (QMessageBox::question(this, "Reset All Settings",
                                  "Restart with default settings? The current ones are kept as IW8Radiant.ini.bak (tool paths, "
                                  "layout, preferences and the web server login are reset).") != QMessageBox::Yes)
            return;
        if (!confirmDiscard()) return;
        QFile::remove(ini + ".bak");
        Settings::instance().setVolatile(true);  // nothing more is written on the way out
        QFile::rename(ini, ini + ".bak");
        QStringList args;
        if (!ctx_.doc.path().empty()) args << qs(ctx_.doc.path());
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        close();
    });
    help->addSeparator();
    addCommand(help, "TestChecklist", "Test Checklist (record in-game results)...", QKeySequence(), [this] {
        // 1.13 (core verification): docs/TEST_CHECKLIST.md items with OK / Wrong / Not tested; results are kept with the
        // date and build in docs/test_results.json and the checklist's boxes are updated to match.
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("docs/TEST_CHECKLIST.md")); ++up) d.cdUp();
        const QString mdPath = d.filePath("docs/TEST_CHECKLIST.md"), resPath = d.filePath("docs/test_results.json");
        QFile mf(mdPath);
        if (!mf.open(QIODevice::ReadOnly)) return ctx_.setStatus("docs/TEST_CHECKLIST.md not found next to the editor");
        const std::string md = mf.readAll().toStdString();
        mf.close();
        const auto items = parseChecklist(md);
        std::vector<CheckResult> results;
        std::string err;
        loadResults(resPath.toStdString(), results, err);
        QDialog dlg(this);
        dlg.setObjectName("testChecklistDialog");
        dlg.setWindowTitle("Test Checklist");
        dlg.resize(900, 640);
        auto* lay = new QVBoxLayout(&dlg);
        auto* table = new QTableWidget(int(items.size()), 3, &dlg);
        table->setHorizontalHeaderLabels({"Item", "Result", "Note"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setWordWrap(true);
        std::map<std::string, CheckResult> latest;
        for (const auto& r : results) latest[r.id] = r;  // file order: later entries win
        for (int i = 0; i < int(items.size()); ++i) {
            const auto& it = items[size_t(i)];
            auto* t = new QTableWidgetItem(QString::fromStdString((it.section.rfind("In game", 0) == 0 ? "[game] " : "[editor] ") + it.text));
            t->setFlags(t->flags() & ~Qt::ItemIsEditable);
            t->setToolTip(QString::fromStdString(it.text));
            table->setItem(i, 0, t);
            auto* combo = new QComboBox(table);
            combo->addItems({"not-tested", "ok", "wrong"});
            if (auto f = latest.find(it.id); f != latest.end()) combo->setCurrentText(QString::fromStdString(f->second.result));
            table->setCellWidget(i, 1, combo);
            table->setItem(i, 2, new QTableWidgetItem(latest.count(it.id) ? QString::fromStdString(latest[it.id].note) : QString()));
        }
        table->resizeRowsToContents();
        lay->addWidget(table, 1);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
            const std::string date = QDate::currentDate().toString(Qt::ISODate).toStdString();
            const std::string build = QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toString(Qt::ISODate).toStdString();
            size_t changed = 0;
            for (int i = 0; i < int(items.size()); ++i) {
                const std::string res = static_cast<QComboBox*>(table->cellWidget(i, 1))->currentText().toStdString();
                const std::string note = table->item(i, 2) ? table->item(i, 2)->text().toStdString() : std::string();
                const auto f = latest.find(items[size_t(i)].id);
                const bool same = f != latest.end() ? (f->second.result == res && f->second.note == note) : res == "not-tested";
                if (same) continue;
                results.push_back({items[size_t(i)].id, res, build, date, note});
                ++changed;
            }
            std::string serr;
            if (changed && !saveResults(resPath.toStdString(), results, serr)) return ctx_.setStatus("Cannot save results: " + qs(serr));
            const std::string updated = updateChecklistMarks(md, results);
            if (updated != md) {
                QFile out(mdPath);
                if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(QByteArray::fromStdString(updated));
            }
            ctx_.setStatus(QString("Recorded %1 result(s) in docs/test_results.json").arg(changed));
            dlg.accept();
        });
        lay->addWidget(box);
        dlg.exec();
    });
    addCommand(help, "EntityReference", "Entity Class Reference", QKeySequence(), [this] {
        // data/generated/entity_reference.html (tools/build_entity_reference.py, from data/iw8_entity_defs.json).
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/generated/entity_reference.html")); ++up) d.cdUp();
        QFile f(d.filePath("data/generated/entity_reference.html"));
        if (!f.open(QIODevice::ReadOnly)) return ctx_.setStatus("data/generated/entity_reference.html not found");
        auto* dlg = new QDialog(this);
        dlg->setObjectName("entityReferenceDialog");
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle("Entity Class Reference");
        dlg->resize(860, 760);
        auto* lay = new QVBoxLayout(dlg);
        auto* text = new QTextBrowser(dlg);
        text->setHtml(QString::fromUtf8(f.readAll()));
        lay->addWidget(text);
        dlg->show();
    });
    addCommand(help, "UserGuide", "Mapper's Guide", QKeySequence(), [this] {
        // docs/USER_GUIDE.md beside the exe (package) or in the repo above bin/release.
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("docs/USER_GUIDE.md")); ++up) d.cdUp();
        const QString f = d.filePath("docs/USER_GUIDE.md");
        if (QFileInfo::exists(f)) QDesktopServices::openUrl(QUrl::fromLocalFile(f));
        else ctx_.setStatus("docs/USER_GUIDE.md not found next to the editor");
    });
    addCommand(help, "SearchHelp", "Search Help...", QKeySequence("Shift+F1"), [this] {
        // 9.11: every section of the guides in docs/, searchable.
        auto* d = new HelpSearchDialog(this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    addCommand(help, "FirstMapGuide", "First Map Guide", QKeySequence(), [this] {
        if (ads::CDockWidget* d = dock_->findDockWidget("First Map Guide")) {
            d->toggleView(true);
            if (auto* g = qobject_cast<FirstMapGuide*>(d->widget())) g->refresh();
            return;
        }
        auto* g = new FirstMapGuide(
            &ctx_, [this](const QString& cmd) { if (QAction* a = action(cmd)) a->trigger(); },
            [this] {
                const QString game = QFileInfo(toolPath("replay")).absolutePath();
                const bool deployed = !game.isEmpty() && QFileInfo(game + "/mods/mw120r/maps/" + compile_->mapId()).isDir();
                return std::pair<bool, bool>{compile_->lastOk(), deployed};
            });
        auto* d = new ads::CDockWidget(dock_, "First Map Guide");
        d->setObjectName("First Map Guide");
        d->setWidget(g);
        d->setFeature(ads::CDockWidget::DockWidgetClosable, true);
        dock_->addDockWidget(ads::RightDockWidgetArea, d);
        connect(compile_, &CompilePanel::finished, g, [g](bool) { g->refresh(); });
    });
    addCommand(help, "ShortcutSheet", "Keyboard Shortcuts...", QKeySequence("F1"), [this] {
        // Every registered command with its current key (Edit > Keyboard Shortcuts rebinds them), plus the mouse and
        // play-mode keys that are not commands.
        QDialog dlg(this);
        dlg.setWindowTitle("Keyboard Shortcuts");
        dlg.resize(620, 640);
        auto* lay = new QVBoxLayout(&dlg);
        auto* filter = new QLineEdit(&dlg);
        filter->setPlaceholderText("Filter (command or key)");
        auto* table = new QTableWidget(&dlg);
        table->setObjectName("shortcutSheet");
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"Key", "Command", "Menu id"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        QList<QStringList> rows;
        for (auto it = actions_.cbegin(); it != actions_.cend(); ++it) {
            const QString key = it.value()->shortcut().toString(QKeySequence::NativeText);
            rows << QStringList{key, it.value()->text().remove('&'), it.key()};
        }
        const QStringList extra[] = {
            {"RMB drag", "Camera: look", "camera"},          {"W A S D / arrows", "Camera: fly", "camera"},
            {"Ctrl+Shift+LMB", "Select face", "camera"},     {"Alt+LMB drag", "Grid: move selection", "grid"},
            {"P", "Play preview: walk (Esc leaves)", "play"}, {"Space", "Play: jump / mantle / ladder push-off", "play"},
            {"Shift", "Play: sprint", "play"},               {"C / Ctrl", "Play: crouch (while sprinting: slide)", "play"},
            {"Z", "Play: prone", "play"},                    {"N", "Play: noclip", "play"},
            {"E (grid view)", "Edge drag mode", "grid"}};
        for (const QStringList& e : extra) rows << e;
        std::sort(rows.begin(), rows.end(), [](const QStringList& a, const QStringList& b) {
            if (a[0].isEmpty() != b[0].isEmpty()) return !a[0].isEmpty();  // bound keys first
            return a[1].compare(b[1], Qt::CaseInsensitive) < 0;
        });
        table->setRowCount(int(rows.size()));
        for (int r = 0; r < rows.size(); ++r)
            for (int c = 0; c < 3; ++c) table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
        table->resizeColumnsToContents();
        connect(filter, &QLineEdit::textChanged, table, [table](const QString& f) {
            for (int r = 0; r < table->rowCount(); ++r) {
                bool hit = f.isEmpty();
                for (int c = 0; c < 3 && !hit; ++c) hit = table->item(r, c)->text().contains(f, Qt::CaseInsensitive);
                table->setRowHidden(r, !hit);
            }
        });
        lay->addWidget(filter);
        lay->addWidget(table);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        connect(close, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(close);
        lastShortcutRows_ = table->rowCount();
        dlg.exec();
    });
    addCommand(help, "FirstRunSetup", "First-Run Setup...", QKeySequence(), [this] { showFirstRunSetup(false); });
    QAction* about = help->addAction("About / Target Build");
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, "IW8 Radiant Suite",
                           QString("<b>IW8 Radiant Suite 0.1.0</b> (milestone 1: edit &rarr; save &rarr; reopen)<br><br>"
                                   "Target build: Call of Duty: Modern Warfare (2019), Replay 1.20.4.7623265<br>"
                                   "game_dx12_ship_replay.exe MD5 1c238fe327f2ecc3b0db924c5b425439<br>"
                                   "IW8 key table: %1 canonical ids<br>Qt %2 &middot; Qt Advanced Docking System 4.4.1<br><br>"
                                   "<i>In-game loading of edited data is not verified by this build.</i>")
                               .arg(KeyTable::global().size())
                               .arg(qVersion()));
    });
}

void MainWindow::runNudge(int dh, int dv) {
    Vec3 d;
    d[ctx_.axisH()] = dh * ctx_.doc.grid();
    d[ctx_.axisV()] = dv * ctx_.doc.grid();
    if (ctx_.doc.translateSelection(d, "Nudge")) ctx_.documentEdited("Nudged " + qs(formatNumber(ctx_.doc.grid())));
}

// ------------------------------------------------------------------ file

bool MainWindow::confirmDiscard() {
    if (!ctx_.doc.modified()) return true;
    const auto r = QMessageBox::warning(this, "IW8 Radiant Suite", "Save changes to " + displayName_ + "?",
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save();
    return true;
}

void MainWindow::openPath(const QString& path) {
    if (!confirmDiscard()) return;
    loadDocument(path);
}

void MainWindow::loadDocument(const QString& path) {
    std::string err;
    const std::string p = path.toStdString();
    ctx_.scene.clear();
    stockZone_.clear();  // openStockZone sets it again after loading its dump
    stockSpawnsEditable_ = false;
    stockLoot_ = StockLoot{};
    stockLootEditable_ = false;
    stockModelNames_.clear();
    stockPlaced_.clear();
    selectionSets_.clear();  // ids belong to the previous document
    if (selectionSetsMenu_) selectionSetsMenu_->clear();
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        // IW8 stock MapEnts from an ACTS mw19-asset-json dump (Replay 1.20 profile only).
        std::string strings;
        const int cut = path.indexOf("assets");
        if (cut >= 0) {
            const QString guess = path.left(cut) + "script_strings.json";
            if (QFileInfo::exists(guess)) strings = guess.toStdString();
        }
        Iw8MapEntsDump dump;
        if (!loadActsMapEnts(p, strings, dump, &err) || !ctx_.doc.loadFromText(dump.entityString, "", &err)) {
            QMessageBox::critical(this, "Open IW8 MapEnts", qs(err));
            return;
        }
        ctx_.scene.spawns = std::move(dump.spawns);
        stockModelNames_ = entityModelNames(ctx_.doc.map());
        if (std::string perr; !readStockPlacedObjects(p, stockPlaced_, &perr)) {
            stockPlaced_.clear();
            EditorLog::instance().add(LogLevel::Warning, "stock", "the placed objects could not be read: " + qs(perr));
        }
        // 13.5: the loot points of a Warzone map (File > Edit Loot Points makes them entities).
        if (std::string lerr; !readStockLoot(p, strings, stockLoot_, &lerr)) {
            stockLoot_ = StockLoot{};
            EditorLog::instance().add(LogLevel::Warning, "stock", "the loot points could not be read: " + qs(lerr));
        }
        displayName_ = qs(dump.assetName) + " (IW8 entity string - Save As to write)";
        ctx_.setStatus(QString("Opened IW8 MapEnts: %1 entities, %2 compiled spawns (read-only overlay)%3")
                           .arg(ctx_.doc.map().entities.size())
                           .arg(ctx_.scene.spawns.size())
                           .arg(stockLoot_.points.empty() ? QString() : QString(", %1 loot points (File > Edit Loot Points)").arg(stockLoot_.points.size())));
    } else {
        if (!ctx_.doc.load(p, &err)) {
            QMessageBox::critical(this, "Open", qs(err));
            return;
        }
        displayName_ = QFileInfo(path).fileName();
        ctx_.setStatus("Opened " + path);
    }
    prefabCache_.clear();
    reloadCustomAssets();
    Settings::instance().addRecentFile(path);
    rebuildRecentMenu();
    ctx_.scene.sync(ctx_.doc);
    AABB b;
    for (const app::PrimGeom* g : ctx_.scene.items()) b.add(g->bounds);
    if (b.valid()) {
        ctx_.cam.yaw = 45;
        ctx_.cam.pitch = -30;
        app::frameBounds(ctx_.cam, b);
        ctx_.gridCenterH = b.center()[ctx_.axisH()];
        ctx_.gridCenterV = b.center()[ctx_.axisV()];
        const double extent = std::max({64.0, b.maxs[ctx_.axisH()] - b.mins[ctx_.axisH()], b.maxs[ctx_.axisV()] - b.mins[ctx_.axisV()]});
        ctx_.gridScale = std::clamp(0.85 * std::min(grid_->width(), grid_->height()) / extent, 0.002, 8.0);
    }
    ctx_.documentEdited();
    ctx_.cameraMoved();
}

void MainWindow::openDialog() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Open", Settings::instance().value("lastDir").toString(),
        "IW8 Radiant documents (*.map *.ents *.json);;Map source (*.map);;IW8 entity string (*.ents);;IW8 MapEnts ACTS dump (*.asset.json);;All files (*)");
    if (path.isEmpty()) return;
    Settings::instance().setValue("lastDir", QFileInfo(path).absolutePath());
    loadDocument(path);
}

QString MainWindow::toolPath(const QString& key) const {
    // Defaults are this workstation's locations (docs/research/04, 10); Tools > Tool Paths overrides them.
    static const QMap<QString, QString> defaults = {
        {"acts", "E:/mw124/atian-cod-tools/build/bin/Release/acts.exe"},
        {"replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"},
        {"oodle", "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"},
        {"zonetool", "E:/mw124/x64-zt/build/bin/x64/Release/zonetool.exe"},
        {"gsc_tool", "C:/Users/user/Downloads/iw8 level maker/gsc-tool.exe"},
    };
    return Settings::instance().value("tools/" + key, defaults.value(key)).toString();
}

void MainWindow::toolPathsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Tool Paths");
    auto* form = new QFormLayout(&dlg);
    QMap<QString, QLineEdit*> edits;
    const QList<QPair<QString, QString>> rows = {{"acts", "ACTS (acts.exe)"}, {"replay", "Replay 1.20 game_dx12_ship_replay.exe"}, {"oodle", "Oodle (oo2core_7_win64.dll)"}, {"zonetool", "x64-zt (zonetool.exe)"}, {"gsc_tool", "gsc-tool (compiles the map script; empty = none)"}, {"x64zt_output", "x64-zt output folder (empty = app data)"},
                                                 {"launch", "Game exe for Launch Replay (empty = the Replay exe)"}};
    for (const auto& [key, label] : rows) {
        auto* row = new QHBoxLayout();
        auto* edit = new QLineEdit(toolPath(key), &dlg);
        edit->setMinimumWidth(480);
        auto* browse = new QPushButton("...", &dlg);
        const bool folder = key.endsWith("_output");
        connect(browse, &QPushButton::clicked, &dlg, [edit, &dlg, folder] {
            const QString f = folder ? QFileDialog::getExistingDirectory(&dlg, "Locate", edit->text())
                                     : QFileDialog::getOpenFileName(&dlg, "Locate", edit->text());
            if (!f.isEmpty()) edit->setText(f);
        });
        row->addWidget(edit, 1);
        row->addWidget(browse);
        form->addRow(label, row);
        edits.insert(key, edit);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    for (auto it = edits.begin(); it != edits.end(); ++it) Settings::instance().setValue("tools/" + it.key(), it.value()->text());
}

void MainWindow::showFirstRunSetup(bool onlyIfReplayMissing) {
    if (onlyIfReplayMissing && QFileInfo::exists(toolPath("replay"))) return;
    if (runFirstRunSetup(this, [this](const QString& k) { return toolPath(k); }))
        ctx_.setStatus("Tool paths saved (First-Run Setup)");
}

// ------------------------------------------------------------------ custom assets (docs/research/21, 22)

namespace {
QString assetBase(const std::string& docPath) {
    return docPath.empty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/custom_assets"
                           : QFileInfo(QString::fromStdString(docPath)).absolutePath();
}
}  // namespace

QStringList MainWindow::customMaterialRoots() const {
    QStringList roots{assetBase(ctx_.doc.path()) + "/materials"};
    roots << Settings::instance().value("assets/materialRoots").toStringList();
    return roots;
}

QStringList MainWindow::customModelRoots() const {
    QStringList roots{assetBase(ctx_.doc.path()) + "/models"};
    roots << Settings::instance().value("assets/modelRoots").toStringList();
    return roots;
}

void MainWindow::reloadCustomAssets() {
    updateBundleRoots();
    std::vector<std::string> roots;
    for (const QString& r : lookupMaterialRoots())
        if (QFileInfo(r).isDir()) roots.push_back(r.toStdString());
    customMaterials_ = std::make_unique<MaterialLibrary>();
    std::string err;
    if (!roots.empty()) customMaterials_->load(roots, &err);
    QList<QPair<QString, QString>> list;
    for (const std::string& n : customMaterials_->names())
        if (const CustomMaterial* cm = customMaterials_->find(n)) list.append({qs(cm->name), qs(cm->colorImage)});
    materials_->setCustomMaterials(list);
    modelCache_.clear();
    ctx_.scene.invalidate();
    camera_->materialImagesChanged();
    if (!err.empty()) ctx_.setStatus("Custom materials: " + qs(err));
}

bool MainWindow::importTexture(const QString& image, const QString& name, QString* errOut) {
    const QString root = customMaterialRoots().front();
    if (!isValidCustomMaterialName(name.toStdString())) {
        if (errOut) *errOut = "invalid material name " + name + " (lower case, digits, _ and /)";
        return false;
    }
    // Copy the image next to the definition so the library is self-contained.
    const QString dir = QFileInfo(root + "/" + name).absolutePath();
    QDir().mkpath(dir);
    const QString copy = dir + "/" + QFileInfo(image).fileName();
    if (QFileInfo(copy).absoluteFilePath() != QFileInfo(image).absoluteFilePath()) {
        QFile::remove(copy);
        if (!QFile::copy(image, copy)) {
            if (errOut) *errOut = "cannot copy " + image + " to " + copy;
            return false;
        }
    }
    // Re-importing a name updates it: the previous definition is replaced.
    QFile::remove(root + "/" + name + ".iw8mat.json");
    std::string err;
    if (!MaterialLibrary::createFromImage(copy.toStdString(), root.toStdString(), name.toStdString(), &err)) {
        if (errOut) *errOut = qs(err);
        return false;
    }
    reloadCustomAssets();
    ctx_.currentMaterial = name.toStdString();
    if (materialEdit_) materialEdit_->setText(name);
    ctx_.setStatus("Imported " + name + " (Shift+M applies it; it builds into the map with its image)");
    return true;
}

bool MainWindow::importModel(const QString& obj, QString* placedName, QString* errOut) {
    QString stem = QFileInfo(obj).completeBaseName().toLower();
    stem.replace(QRegularExpression("[^a-z0-9_-]"), "_");
    if (stem.isEmpty()) stem = "model";
    ImportedModel im;
    std::string err;
    if (!importModelFile(obj.toStdString(), im, &err)) {
        if (errOut) *errOut = qs(err);
        return false;
    }
    const QString root = customModelRoots().front();
    QDir().mkpath(root);
    const QString ext = "." + QFileInfo(obj).suffix().toLower();
    const QString dest = root + "/" + stem + ext;
    // A .gltf with external buffers: copy the .bin files it names next to it (same names).
    if (ext == ".gltf") {
        QFile g(obj);
        if (g.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(g.readAll());
            QRegularExpressionMatchIterator mi = QRegularExpression("\"uri\"\\s*:\\s*\"([^\"]+\\.bin)\"").globalMatch(text);
            while (mi.hasNext()) {
                const QString bin = mi.next().captured(1);
                const QString src = QFileInfo(obj).absoluteDir().filePath(bin), to = QDir(root).filePath(bin);
                if (QFileInfo(src).absoluteFilePath() != QFileInfo(to).absoluteFilePath()) {
                    QFile::remove(to);
                    QFile::copy(src, to);
                }
            }
        }
    }
    if (QFileInfo(dest).absoluteFilePath() != QFileInfo(obj).absoluteFilePath()) {
        QFile::remove(dest);
        if (!QFile::copy(obj, dest)) {
            if (errOut) *errOut = "cannot copy " + obj + " to " + dest;
            return false;
        }
    }
    modelCache_.erase(stem.toStdString());
    const Vec3 at = placementPoint();
    ctx_.doc.addEntity({{"classname", "misc_model"}, {"model", stem.toStdString()},
                        {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z)}, {"angles", "0 0 0"}},
                       {}, "Place " + stem.toStdString());
    ctx_.scene.invalidate();
    ctx_.documentEdited(QString("Placed custom model %1 (%2 meshes); it builds into the map").arg(stem).arg(im.meshes.size()));
    if (placedName) *placedName = stem;
    return true;
}

// ------------------------------------------------------------------ prefabs

bool MainWindow::addShapeModel(int kind, QString* placedName) {
    if (ctx_.doc.path().empty()) {
        ctx_.setStatus("Save the map first: shape models are written into the models folder beside it");
        return false;
    }
    const auto k = ShapeKind(kind);
    const std::string material = ctx_.currentMaterial.empty() ? std::string("iw8r/grey") : ctx_.currentMaterial;
    const QString name = qs(shapeModelName(k, material));
    const QString dir = customModelRoots().front();
    QDir().mkpath(dir);
    const QString file = dir + "/" + name + ".obj";
    if (!QFileInfo::exists(file)) {
        QSaveFile f(file);
        const std::string obj = shapeObj(k, material);
        if (!f.open(QIODevice::WriteOnly) || f.write(obj.data(), qint64(obj.size())) != qint64(obj.size()) || !f.commit()) {
            ctx_.setStatus("Cannot write " + file);
            return false;
        }
    }
    modelCache_.erase(name.toStdString());
    const Vec3 at = placementPoint();
    ctx_.doc.addEntity({{"classname", "misc_model"}, {"model", name.toStdString()},
                        {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z)}},
                       {}, "Add shape " + name.toStdString());
    if (placedName) *placedName = name;
    ctx_.documentEdited(QString("Added %1 (%2; 64 units, resize with the scale gizmo)").arg(name, qs(material)));
    return true;
}

int MainWindow::captureReflectionProbes(QString* dirOut) {
    ctx_.scene.sync(ctx_.doc);
    AABB world;
    for (const auto* g : ctx_.scene.items())
        if (g->kind != iw8r::app::PrimGeom::PointEntity && !g->brushEntity && g->bounds.valid()) world.add(g->bounds);
    std::vector<Vec3> positions{world.valid() ? world.center() : Vec3{}};
    for (const auto& e : ctx_.doc.map().entities)
        if (e.classname() == "reflection_probe" && positions.size() < 257) positions.push_back(e.origin().value_or(world.center()));
    const QString dir = compile_->buildRoot() + "/probe_capture";
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    int done = 0;
    for (size_t i = 0; i < positions.size(); ++i) {
        const std::vector<uint8_t> faces = camera_->captureCube(positions[i], 64);
        if (faces.empty()) continue;
        QFile rf(dir + QString("/probe%1.rgba").arg(i)), pf(dir + QString("/probe%1.pos").arg(i));
        if (!rf.open(QIODevice::WriteOnly) || !pf.open(QIODevice::WriteOnly)) continue;
        rf.write(reinterpret_cast<const char*>(faces.data()), qint64(faces.size()));
        pf.write(QString("%1 %2 %3").arg(positions[i].x, 0, 'g', 10).arg(positions[i].y, 0, 'g', 10).arg(positions[i].z, 0, 'g', 10).toUtf8());
        ++done;
    }
    if (dirOut) *dirOut = dir;
    ctx_.setStatus(QString("Captured %1 of %2 reflection probe(s) from the camera renderer; the next compile uses them").arg(done).arg(positions.size()));
    return done;
}

bool MainWindow::applyWebServerSettings(QString* message) {
    auto& s = Settings::instance();
    if (!web_) {
        WebServer::Hooks hooks;
        hooks.save = [this] { return save(); };
        hooks.compile = [this] {
            if (QAction* a = action("CompileMap")) a->trigger();
        };
        hooks.mapName = [this] { return displayName_; };
        hooks.compileBusy = [this] { return compile_ && compile_->busy(); };
        hooks.deploy = [this] {
            if (QAction* a = action("CompileAndDeploy")) a->trigger();
        };
        hooks.buildDeployLoad = [this] {
            if (QAction* a = action("BuildDeployLoad")) a->trigger();
        };
        hooks.loadInGame = [this](QString* why) {
            const bool ok = liveLink_ && liveLink_->loadMapInGame();
            if (!ok && why) *why = liveLink_ ? liveLink_->messageText() : QString("Live Link is not available");
            return ok;
        };
        hooks.thumbnail = [this](const QString& kind, const QString& name) -> QByteArray {
            // Cached JPEG per asset; materials from the material cache colour image, models rendered from the model cache.
            static QHash<QString, QByteArray> cache;
            const QString key = kind + ":" + name;
            if (auto it = cache.find(key); it != cache.end()) return *it;
            QImage img;
            if (kind == "materials") {
                if (auto e = matCache_->lookup(name); e && !e->failed && !e->colorDds.isEmpty()) {
                    std::string bytes;
                    app::Rgba8Image raw;
                    if (readFile(e->colorDds.toStdString(), bytes) && app::decodeDds(bytes, raw)) {
                        const app::Rgba8Image small = app::downscale(raw, 96);
                        img = QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).copy();
                    }
                } else if (!e) {
                    matCache_->request({name});
                }
            } else if (kind == "models") {
                if (auto e = modelCacheStock_->lookup(name); e && !e->failed) {
                    if (const auto m = modelCacheStock_->loadModel(name)) img = renderModelPreview(*m, 96);
                } else if (!e) {
                    modelCacheStock_->request({name});
                }
            }
            if (img.isNull()) return {};
            QByteArray jpeg;
            QBuffer b(&jpeg);
            b.open(QIODevice::WriteOnly);
            img.convertToFormat(QImage::Format_RGB32).save(&b, "JPG", 85);
            if (cache.size() > 4000) cache.clear();
            cache.insert(key, jpeg);
            return jpeg;
        };
        web_ = new WebServer(&ctx_, hooks, this);
        upnp_ = new UpnpPortMapper(this);
        connect(upnp_, &UpnpPortMapper::changed, this, [this] {
            EditorLog::instance().add(LogLevel::Info, "web", upnp_->status());
            ctx_.setStatus(upnp_->status());
        });
    }
    const bool on = s.value("web/enabled", false).toBool();
    if (QAction* a = action("WebServer")) a->setChecked(on);
    upnp_->remove();
    web_->stop();
    if (!on) {
        if (message) *message = "Web server off";
        return true;
    }
    const bool lan = s.value("web/lan", false).toBool();
    if (lan && !WebServer::hasCredentials()) {
        if (message) *message = "Web server: set a username and password first (Tools > Web Server Settings)";
        return false;
    }
    const quint16 port = quint16(s.value("web/port", 8780).toInt());
    QString err;
    // HTTPS (web/tls): PEM certificate and key, by default web_tls/cert.pem and key.pem beside the editor.
    const QString tlsDir = QCoreApplication::applicationDirPath() + "/web_tls";
    const bool tls = s.value("web/tls", false).toBool();
    const QString cert = tls ? s.value("web/cert", tlsDir + "/cert.pem").toString() : QString();
    const QString key = tls ? s.value("web/key", tlsDir + "/key.pem").toString() : QString();
    if (!web_->start(port, lan, &err, cert, key)) {
        if (message) *message = QString("Web server: port %1 is not available (%2)").arg(port).arg(err);
        return false;
    }
    if (lan && s.value("web/upnp", false).toBool()) upnp_->map(port, "IW8 Radiant web editor");
    if (message) *message = "Web server running at " + web_->url() + (lan ? " (login required from other machines)" : "");
    EditorLog::instance().add(LogLevel::Info, "web", "Web server running at " + web_->url());
    return true;
}

QString MainWindow::showMaterialInfo(const QString& name) {
    MaterialInfoInput in;
    in.name = name;
    const std::string n = name.toStdString();
    in.toolMaterial = isIw8ToolMaterial(n);
    for (const MaterialUse& u : materialUsage(ctx_.doc.map()))
        if (u.material == n) in.faces = u.faces, in.primitives = u.primitives;
    if (const int st = iw3SurfaceTypeForMaterial(n); st > 0) in.surfaceType = iw3SurfaceTypeName(st);
    if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(n) : nullptr) in.customSource = qs(cm->defFile);
    if (!in.toolMaterial && in.customSource.isEmpty()) {
        in.entry = matCache_->lookup(name);
        if (!in.entry) matCache_->request({name});
    }
    in.buildsNormal = Settings::instance().value("compile/stockNormalMaps", true).toBool();
    const QString html = materialInfoHtml(in);
    showAssetInfo(this, "materialInfoDialog", "Material Info: " + name, html);
    return html;
}

QImage MainWindow::materialImage(const QString& m) {
    QString file;
    if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m.toStdString()) : nullptr) file = qs(cm->colorImage);
    if (file.isEmpty()) file = materials_->colorImageFile(m);
    if (file.isEmpty())
        if (auto e = matCache_->lookup(m); e && !e->failed) file = e->colorDds;
    if (file.isEmpty()) return {};
    if (!file.endsWith(".dds", Qt::CaseInsensitive)) return QImage(file);
    std::string bytes;
    app::Rgba8Image img;
    if (!readFile(file.toStdString(), bytes) || !app::decodeDds(bytes, img) || img.width <= 0) return {};
    const app::Rgba8Image small = app::downscale(img, 256);
    QImage q(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888);
    return q.convertToFormat(QImage::Format_RGB32);  // deep copy; the colour alpha is specular, not opacity
}

QString MainWindow::currentSkyFile(QString* skyName) {
    for (const auto& e : ctx_.doc.map().entities) {
        if (!e.isWorldspawn()) continue;
        if (const std::string* f = e.value("iw8r_sky_file"); f && !f->empty()) {
            if (skyName) *skyName = qs(*f);
            const QString path = QFileInfo(qs(*f)).isAbsolute() ? qs(*f) : QFileInfo(qs(ctx_.doc.path())).absoluteDir().absoluteFilePath(qs(*f));
            return QFileInfo::exists(path) ? path : QString();
        }
        for (const char* key : {"skyname", "sky"})
            if (const std::string* v = e.value(key); v && !v->empty()) {
                if (skyName) *skyName = qs(*v);
                return skyCache_ ? skyCache_->cachedDds(qs(*v)) : QString();
            }
        break;
    }
    if (skyName) *skyName = QString("");
    return {};
}

QString MainWindow::sunFromSky(bool automatic) {
    QString sky;
    const QString file = currentSkyFile(&sky);
    if (sky.isEmpty()) return "the map has no sky (worldspawn skyname)";
    if (file.isEmpty()) {
        if (skyCache_ && !sky.contains('.')) skyCache_->request(sky);
        pendingSunSky_ = sky;
        if (automatic) ctx_.setStatus("Sun From Sky: exporting " + sky + "; the sun is aimed when it arrives");
        return automatic ? QString() : "exporting " + sky + "; the sun is aimed when it arrives";
    }
    std::string bytes, err;
    if (!readFile(file.toStdString(), bytes)) return "cannot read " + file;
    const auto s = sunFromSkyDds(bytes, &err);
    if (!s) return "no sun found in " + sky + (err.empty() ? QString() : ": " + qs(err));
    Entity* ws = ctx_.doc.worldspawn();
    if (!ws) return "no worldspawn";
    const std::string key = sunDirectionKeyFor(s->dir);
    const std::string* cur = ws->value("sundirection");
    if (cur && *cur == key) return {};
    ctx_.doc.setKeyValue(ws->id, "sundirection", key);
    ctx_.documentEdited(QString("Sun aimed at the sun of %1: sundirection \"%2\"%3 (undo to keep the previous one)")
                            .arg(sky, qs(key), s->contrast < 1.6 ? QString(" (faint: the sky has no clear sun disc)") : QString()));
    return {};
}

QString MainWindow::writeVisionEdits(const QString& file, double fogStart, double fogDensity, const QColor& fogColor, double maxOpacity) {
    // KEY "value" lines for iw8r vision-set (core stock_vision: values of existing keys only).
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    const QString text = QString("%1 \"%2\"\n%3 \"%4\"\n%5 \"%6 %7 %8\"\n%9 \"%10\"\n")
                             .arg(kVisionFogStart).arg(fogStart).arg(kVisionFogDensity).arg(fogDensity).arg(kVisionFogColor)
                             .arg(fogColor.redF(), 0, 'f', 4).arg(fogColor.greenF(), 0, 'f', 4).arg(fogColor.blueF(), 0, 'f', 4)
                             .arg(kVisionFogMaxOpacity).arg(maxOpacity);
    f.write(text.toUtf8());
    return file;
}

void MainWindow::relightStockMapDialog() {
    // 11.6 (research 48): the stock zone's vision rawfile (fog values) edited and the zone rebuilt and validated by
    // tools/m2_rebuild_mapents.ps1 -Vision (dumpzone, edit, buildzone, verifyzone, ACTS --test). The result is
    // out/m2-<zone>-<time>/out/<zone>.ff; installing it into the game stays manual (back up the stock zone first).
    QDialog dlg(this);
    dlg.setWindowTitle("Relight Stock Map (Fog / Vision)");
    auto* form = new QFormLayout(&dlg);
    auto* zone = new QLineEdit(stockZone().isEmpty() ? QString("mp_shipment") : stockZone(), &dlg);
    auto* start = new QDoubleSpinBox(&dlg);
    start->setRange(0, 100000), start->setValue(600);
    auto* density = new QDoubleSpinBox(&dlg);
    density->setRange(1, 1000000), density->setValue(25000);
    auto* opacity = new QDoubleSpinBox(&dlg);
    opacity->setRange(0, 1), opacity->setSingleStep(0.05), opacity->setValue(0.75);
    QColor colour = QColor::fromRgbF(0.851f, 0.925f, 1.0f);
    auto* pick = new QPushButton("Fog Colour...", &dlg);
    connect(pick, &QPushButton::clicked, &dlg, [&] {
        const QColor c = QColorDialog::getColor(colour, &dlg, "Fog Colour");
        if (c.isValid()) colour = c;
    });
    auto* note = new QLabel("Starts from mp_shipment's stock values (fog start 600, density 25000, max opacity 0.75). The rebuilt "
                            "zone is validated, not installed: copy it into a backed-up game folder to test.", &dlg);
    note->setWordWrap(true);
    form->addRow(note);
    form->addRow("Stock zone", zone);
    form->addRow("Fog start (units)", start);
    form->addRow("Fog density (distance scale)", density);
    form->addRow("Fog max opacity", opacity);
    form->addRow(pick);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText("Rebuild Zone");
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString root = QCoreApplication::applicationDirPath() + "/../..";
    const QString work = QDir(root).absoluteFilePath("out/relight-" + zone->text() + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QDir().mkpath(work);
    const QString edits = writeVisionEdits(work + "/vision_edits.txt", start->value(), density->value(), colour, opacity->value());
    if (edits.isEmpty()) return ctx_.setStatus("Relight: cannot write " + work);
    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setWorkingDirectory(root);
    connect(proc, &QProcess::finished, this, [this, proc, work](int code, QProcess::ExitStatus) {
        const QString log = QString::fromLocal8Bit(proc->readAll());
        QFile lf(work + "/relight.log");
        if (lf.open(QIODevice::WriteOnly)) lf.write(log.toUtf8());
        proc->deleteLater();
        ctx_.setStatus(code == 0 ? "Relight: rebuilt and validated (ACTS --test); the zone is in " + work + "/out"
                                 : "Relight failed (exit " + QString::number(code) + "); see " + work + "/relight.log");
        EditorLog::instance().add(code == 0 ? LogLevel::Info : LogLevel::Error, "relight", log.right(2000));
    });
    proc->start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", QDir(root).absoluteFilePath("tools/m2_rebuild_mapents.ps1"),
                               "-Zone", zone->text(), "-Vision", edits, "-Work", work, "-ZoneTool", toolPath("zonetool"), "-Acts", toolPath("acts")});
    ctx_.setStatus("Relight: rebuilding " + zone->text() + " with the new fog values (a few minutes; status line reports the result)");
}

QString MainWindow::rebuildStockEntities(const QString& zone, QString* err, bool start) {
    if (ctx_.doc.map().entities.empty()) {
        if (err) *err = "open a stock map's entities first (File > Open a .ents, or an iw8-ents export)";
        return {};
    }
    const QString root = QCoreApplication::applicationDirPath() + "/../..";
    const QString work = QDir(root).absoluteFilePath("out/m2-" + zone + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QDir().mkpath(work);
    const QString ents = work + "/edited.ents";
    std::string serr;
    if (!ctx_.doc.saveCopy(ents.toStdString(), &serr)) {
        if (err) *err = "cannot write " + ents + ": " + qs(serr);
        return {};
    }
    const QStringList args = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", QDir(root).absoluteFilePath("tools/m2_rebuild_mapents.ps1"),
                              "-Zone", zone, "-Ents", ents, "-Work", work, "-Game", QFileInfo(toolPath("replay")).absolutePath(),
                              "-ZoneTool", toolPath("zonetool"), "-Acts", toolPath("acts")};
    QFile af(work + "/command.txt");
    if (af.open(QIODevice::WriteOnly)) af.write(("powershell " + args.join(' ')).toUtf8());
    if (!start) return work;  // tests: prepared only
    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setWorkingDirectory(root);
    connect(proc, &QProcess::finished, this, [this, proc, work, zone](int code, QProcess::ExitStatus) {
        const QString log = QString::fromLocal8Bit(proc->readAll());
        QFile lf(work + "/rebuild.log");
        if (lf.open(QIODevice::WriteOnly)) lf.write(log.toUtf8());
        proc->deleteLater();
        ctx_.setStatus(code == 0 ? "Rebuild Stock Map: " + zone + " rebuilt and validated (ACTS --test); the zone is in " + work + "/out (installing it is manual)"
                                 : "Rebuild Stock Map failed (exit " + QString::number(code) + "); see " + work + "/rebuild.log");
        EditorLog::instance().add(code == 0 ? LogLevel::Info : LogLevel::Error, "stock rebuild", log.right(2000));
    });
    proc->start("powershell", args);
    return work;
}

QString MainWindow::prefetchStockAssets(int timeoutMs) {
    MapFile flat = ctx_.doc.map();
    {
        std::vector<std::string> roots;
        for (const QString& r : PrefabBrowser::searchRoots()) roots.push_back(r.toStdString());
        std::vector<std::string> pw;
        std::string perr;
        MapFile out;
        if (flattenPrefabs(ctx_.doc.map(), ctx_.doc.path(), roots, out, &pw, &perr)) flat = std::move(out);
    }
    QStringList models, materials;
    std::vector<std::string> modelRoots;
    for (const QString& x : lookupModelRoots()) modelRoots.push_back(x.toStdString());  // a placed bundle's own models are not stock
    std::set<std::string> customSeen;
    for (const auto& e : flat.entities) {
        const std::string cls = e.classname();
        if (cls == "script_model" || cls == "misc_model" || cls == "dynamic_prop")
            if (const std::string* m = e.value("model"); m && !m->empty() && (*m)[0] != '*') {
                const std::string file = findCustomModelFile(modelRoots, *m);
                if (file.empty()) {
                    if (!models.contains(qs(*m))) models << qs(*m);
                } else if (customSeen.insert(*m).second) {
                    // 11.8: a custom model whose groups name stock materials (Stock Reference to Model copies).
                    ImportedModel im;
                    if (importModelFile(file, im, nullptr))
                        for (const auto& mesh : im.meshes)
                            if (!mesh.material.empty() && !isIw8ToolMaterial(mesh.material) &&
                                !(customMaterials_ && customMaterials_->find(mesh.material)) && !materials.contains(qs(mesh.material)))
                                materials << qs(mesh.material);
                }
            }
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                auto add = [&](const std::string& m) {
                    if (m.empty() || isIw8ToolMaterial(m) || (customMaterials_ && customMaterials_->find(m))) return;
                    if (!materials.contains(qs(m))) materials << qs(m);
                };
                if (p->kind == PrimKind::Patch) add(p->patchMaterial);
                for (const auto& f : p->faces) add(f.material());
            }
    }
    auto waitIdle = [&](const std::function<bool()>& busy) {
        QElapsedTimer t;
        t.start();
        while (busy() && t.elapsed() < timeoutMs) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    };
    if (!models.isEmpty() && modelCacheStock_) {
        modelCacheStock_->request(models);
        waitIdle([this] { return modelCacheStock_->busy(); });
        for (const QString& m : models)
            if (auto e = modelCacheStock_->lookup(m); e && !e->failed)
                for (const QString& mm : e->materials)
                    if (!mm.isEmpty() && !materials.contains(mm)) materials << mm;
    }
    int readyModels = 0, readyMaterials = 0;
    QStringList missing;
    for (const QString& m : models)
        if (auto e = modelCacheStock_ ? modelCacheStock_->lookup(m) : std::nullopt; e && !e->failed) ++readyModels;
        else missing << m;
    if (!materials.isEmpty() && matCache_) {
        matCache_->request(materials);
        waitIdle([this] { return matCache_->busy(); });
    }
    for (const QString& m : materials)
        if (auto e = matCache_ ? matCache_->lookup(m) : std::nullopt; e && !e->failed) ++readyMaterials;
    return QString("prefetch: %1 of %2 stock model(s) and %3 of %4 stock material(s) cached")
        .arg(readyModels).arg(models.size()).arg(readyMaterials).arg(materials.size()) +
           (missing.isEmpty() ? QString() : "; not available: " + missing.join(", "));
}

void MainWindow::setSelectionHighlight(const QColor& c, double amount) {
    ctx_.scene.selectionTint = 0xff000000u | uint32_t(c.red()) | (uint32_t(c.green()) << 8) | (uint32_t(c.blue()) << 16);
    ctx_.scene.selectionTintAmount = std::clamp(amount, 0.05, 1.0);
    Settings::instance().setValue("view/selectionTint", c.name());
    Settings::instance().setValue("view/selectionTintAmount", ctx_.scene.selectionTintAmount);
    ctx_.cameraMoved();
}

int MainWindow::exportMapPackage(const QString& zipPath, QString* err) {
    if (ctx_.doc.path().empty()) {
        if (err) *err = "save the map first";
        return 0;
    }
    // Custom assets keep their place under the map's materials / models folders (where the editor looks), so an
    // import into a fresh folder resolves them with no settings.
    std::vector<PackageFile> extra;
    auto addUnder = [&](const std::string& file, const QStringList& roots, const QString& prefix) {
        if (file.empty() || !QFileInfo::exists(qs(file))) return;
        QString rel = QFileInfo(qs(file)).fileName();
        for (const QString& r : roots) {
            const QString cr = QDir::cleanPath(QFileInfo(r).absoluteFilePath());
            const QString cf = QDir::cleanPath(QFileInfo(qs(file)).absoluteFilePath());
            if (cf.startsWith(cr + "/", Qt::CaseInsensitive)) {
                rel = cf.mid(cr.size() + 1);
                break;
            }
        }
        extra.push_back({QFileInfo(qs(file)).absoluteFilePath().toStdString(), (prefix + "/" + rel).toStdString()});
    };
    std::set<std::string> mats, models;
    for (const auto& e : ctx_.doc.map().entities) {
        if (const std::string* m = e.value("model"); m && !m->empty() && e.classname() != "misc_prefab") models.insert(*m);
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                if (p->kind == PrimKind::Patch) mats.insert(p->patchMaterial);
                for (const auto& f : p->faces) mats.insert(f.material());
            }
    }
    const QStringList matRoots = customMaterialRoots(), modelRoots = customModelRoots();
    if (customMaterials_)
        for (const std::string& m : mats)
            if (const CustomMaterial* cm = customMaterials_->find(m))
                for (const std::string* f : {&cm->defFile, &cm->colorImage, &cm->normalImage, &cm->specImage}) addUnder(*f, matRoots, "materials");
    std::vector<std::string> mroots;
    for (const QString& r : modelRoots) mroots.push_back(r.toStdString());
    for (const std::string& m : models)
        if (const std::string f = findCustomModelFile(mroots, m); !f.empty()) {
            addUnder(f, modelRoots, "models");
            const QFileInfo fi(qs(f));
            const QString mtl = fi.absolutePath() + "/" + fi.completeBaseName() + ".mtl";
            if (QFileInfo::exists(mtl)) addUnder(mtl.toStdString(), modelRoots, "models");
        }
    std::vector<std::string> proots;
    for (const QString& r : PrefabBrowser::searchRoots()) proots.push_back(r.toStdString());
    const PackageResult pkg = collectMapPackage(ctx_.doc.path(), proots, extra);
    std::string werr;
    if (!writeZip(zipPath.toStdString(), pkg.files, &werr)) {
        if (err) *err = qs(werr);
        return 0;
    }
    if (err && !pkg.missing.empty()) *err = QString("%1 reference(s) not found: %2").arg(pkg.missing.size()).arg(qs(pkg.missing.front()));
    return int(pkg.files.size());
}

QString MainWindow::importMapPackage(const QString& zipPath, const QString& destDir, QString* err) {
    std::vector<std::string> files;
    std::string xerr;
    if (!extractZip(zipPath.toStdString(), destDir.toStdString(), &files, &xerr)) {
        if (err) *err = qs(xerr);
        return {};
    }
    // Prefabs that came from a library root are under <dest>/prefabs: make it a prefab root so their keys resolve.
    const QString pre = QDir::cleanPath(destDir + "/prefabs");
    if (QFileInfo(pre).isDir()) {
        QStringList roots = Settings::instance().value("prefabRoots").toStringList();
        if (!roots.contains(pre, Qt::CaseInsensitive)) {
            roots << pre;
            Settings::instance().setValue("prefabRoots", roots);
        }
    }
    // The map is the .map at the package root (the first one when there are several).
    for (const std::string& f : files) {
        const QFileInfo fi(qs(f));
        if (fi.suffix().compare("map", Qt::CaseInsensitive) == 0 && QDir::cleanPath(fi.absolutePath()) == QDir::cleanPath(QFileInfo(destDir).absoluteFilePath()))
            return fi.absoluteFilePath();
    }
    if (err) *err = "the package has no .map at its top level";
    return {};
}

QString MainWindow::autosaveDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/autosave";
}

QString MainWindow::autosaveNow(QString* message) {
    // 9.14: a timestamped copy per autosave (<base>.<yyyyMMdd-HHmmss>.autosave.map), the newest
    // autosave/keep (20) per map kept, and nothing written when the volume has less than 200 MB free.
    const QString dir = autosaveDir();
    QDir().mkpath(dir);
    uintmax_t freeBytes = 0;
    if (!hasFreeSpace(dir.toStdString(), uintmax_t(200) << 20, &freeBytes)) {
        if (message) *message = QString("Autosave skipped: only %1 MB free on the autosave drive").arg(freeBytes >> 20);
        return {};
    }
    QString base = displayName_;
    base.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    if (base.isEmpty()) base = "untitled";
    const bool ents = !ctx_.doc.map().entities.empty() && ctx_.doc.map().entities.front().usesCanonicalKeys();
    const std::string stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss").toStdString();
    const QString path = dir + "/" + qs(autosaveFileName(base.toStdString(), stamp, ents ? "ents" : "map"));
    std::string err;
    if (!ctx_.doc.saveCopy(path.toStdString(), &err)) {
        if (message) *message = "Autosave failed: " + qs(err);
        return {};
    }
    const size_t keep = size_t(std::max(1, Settings::instance().value("autosave/keep", 20).toInt()));
    for (const std::string& old : autosavesToPrune(listAutosaves(dir.toStdString(), base.toStdString()), keep)) QFile::remove(qs(old));
    if (message) *message = "Autosaved " + QFileInfo(path).fileName();
    return path;
}

void MainWindow::showAutosaveHistory() {
    QDialog dlg(this);
    dlg.setObjectName("autosaveHistoryDialog");
    dlg.setWindowTitle("Autosave History");
    auto* lay = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"Map", "Saved", "Size", "File"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QString cur = displayName_;
    cur.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    auto entries = listAutosaves(autosaveDir().toStdString());
    // The open map's copies first, then the rest (each newest first, as listed).
    std::stable_partition(entries.begin(), entries.end(), [&](const AutosaveEntry& e) { return qs(e.base) == cur; });
    table->setRowCount(int(entries.size()));
    for (int i = 0; i < int(entries.size()); ++i) {
        const AutosaveEntry& e = entries[size_t(i)];
        const QDateTime t = QDateTime::fromString(qs(e.stamp), "yyyyMMdd-HHmmss");
        table->setItem(i, 0, new QTableWidgetItem(qs(e.base)));
        table->setItem(i, 1, new QTableWidgetItem(t.isValid() ? t.toString("yyyy-MM-dd HH:mm:ss") : QString("(older single copy)")));
        table->setItem(i, 2, new QTableWidgetItem(QString("%1 KB").arg((e.bytes + 1023) / 1024)));
        auto* f = new QTableWidgetItem(QFileInfo(qs(e.path)).fileName());
        f->setData(Qt::UserRole, qs(e.path));
        table->setItem(i, 3, f);
    }
    table->resizeColumnsToContents();
    lay->addWidget(new QLabel(QString("%1 autosave(s) in %2; double-click or Open loads a copy (the original map is not touched).")
                                  .arg(entries.size()).arg(autosaveDir()), &dlg));
    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Close, &dlg);
    auto* browse = box->addButton("Browse...", QDialogButtonBox::ActionRole);
    lay->addWidget(box);
    QString chosen;
    auto pick = [&] {
        const auto rows = table->selectionModel()->selectedRows();
        if (!rows.isEmpty()) chosen = table->item(rows.front().row(), 3)->data(Qt::UserRole).toString();
        if (!chosen.isEmpty()) dlg.accept();
    };
    connect(table, &QTableWidget::cellDoubleClicked, &dlg, [&](int, int) { pick(); });
    connect(box, &QDialogButtonBox::accepted, &dlg, pick);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(browse, &QPushButton::clicked, &dlg, [&] {
        chosen = QFileDialog::getOpenFileName(this, "Recover Autosave", autosaveDir(), "Autosaves (*.autosave.map *.autosave.ents);;All files (*)");
        if (!chosen.isEmpty()) dlg.accept();
    });
    dlg.resize(720, 420);
    if (dlg.exec() == QDialog::Accepted && !chosen.isEmpty()) openPath(chosen);
}

bool MainWindow::showPatchInspector() {
    // 2.21: the curve-edit dialog: every control point of the first selected patch as x y z s t, applied as one undo.
    const auto g = firstSelectedPatchGrid(ctx_);
    if (!g) {
        ctx_.setStatus("Patch Inspector: select a patch first");
        return false;
    }
    QDialog dlg(this);
    dlg.setObjectName("patchInspectorDialog");
    dlg.setWindowTitle(QString("Patch Inspector (%1 x %2, %3)").arg(g->rows).arg(g->cols).arg(qs(g->material)));
    auto* lay = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(int(g->pos.size()), 7, &dlg);
    table->setObjectName("patchInspectorTable");
    table->setHorizontalHeaderLabels({"Row", "Col", "X", "Y", "Z", "S", "T"});
    for (int i = 0; i < int(g->pos.size()); ++i) {
        auto ro = [&](int c, const QString& t) {
            auto* it = new QTableWidgetItem(t);
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, c, it);
        };
        ro(0, QString::number(i / std::max(1, g->cols)));
        ro(1, QString::number(i % std::max(1, g->cols)));
        const double v[5] = {g->pos[size_t(i)].x, g->pos[size_t(i)].y, g->pos[size_t(i)].z,
                             size_t(i) < g->st.size() ? g->st[size_t(i)][0] : 0.0, size_t(i) < g->st.size() ? g->st[size_t(i)][1] : 0.0};
        for (int c = 0; c < 5; ++c) table->setItem(i, 2 + c, new QTableWidgetItem(qs(formatNumber(v[c]))));
    }
    table->resizeColumnsToContents();
    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(560, 420);
    if (dlg.exec() != QDialog::Accepted) return false;
    std::vector<std::array<double, 5>> pts(g->pos.size());
    for (int i = 0; i < int(pts.size()); ++i)
        for (int c = 0; c < 5; ++c) {
            bool ok = false;
            const double x = table->item(i, 2 + c) ? table->item(i, 2 + c)->text().toDouble(&ok) : 0.0;
            if (!ok) {
                ctx_.setStatus(QString("Patch Inspector: row %1 has a value that is not a number").arg(i + 1));
                return false;
            }
            pts[size_t(i)][size_t(c)] = x;
        }
    if (!applyPatchPoints(ctx_, pts)) {
        ctx_.setStatus("Patch Inspector: nothing changed");
        return false;
    }
    ctx_.documentEdited("Patch control points edited (undoable)");
    return true;
}

size_t MainWindow::replaceEverywhere(bool material, const QString& from, const QString& to) {
    const size_t n = material ? replaceMaterial(ctx_.doc, from.toStdString(), to.toStdString(), false, false)
                              : replaceKeyValues(ctx_.doc, "model", from.toStdString(), to.toStdString(), false, false);
    if (n) ctx_.documentEdited();
    return n;
}

QStringList MainWindow::unusedCustomAssets() {
    std::set<std::string> mats, models;
    for (const auto& e : ctx_.doc.map().entities) {
        if (const std::string* m = e.value("model"); m && !m->empty()) models.insert(*m);
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                if (p->kind == PrimKind::Patch) mats.insert(p->patchMaterial);
                for (const auto& f : p->faces) mats.insert(f.material());
            }
    }
    QStringList out;
    if (customMaterials_)
        for (const std::string& n : customMaterials_->names())
            if (const CustomMaterial* cm = customMaterials_->find(n); cm && !mats.count(cm->name))
                out << "material  " + qs(cm->name) + "  (" + qs(cm->defFile) + ")";
    for (const QString& root : customModelRoots()) {
        QDirIterator it(root, {"*.obj", "*.glb", "*.gltf"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const std::string name = QFileInfo(f).completeBaseName().toStdString();
            if (!models.count(name)) out << "model  " + qs(name) + "  (" + f + ")";
        }
    }
    return out;
}

int MainWindow::generateSpawnSet(int ffaCount, int startsPerSide, bool replace, QString* message) {
    CollisionWorld world;
    world.build(ctx_.doc.map());
    AABB area;
    for (const Entity& e : ctx_.doc.map().entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) area.add(p->bounds());
    if (!area.valid()) {
        if (message) *message = "the map has no brushes or patches to stand on";
        return 0;
    }
    SpawnGenOptions opt;
    opt.ffaCount = ffaCount;
    opt.teamStartsPerSide = startsPerSide;
    const SpawnGenResult r = generateSpawns(world, area, opt);
    if (message) *message = r.warnings.empty() ? QString() : qs(r.warnings.front());
    if (r.spawns.empty()) {
        if (message && message->isEmpty()) *message = "no walkable floor found";
        return 0;
    }
    ctx_.doc.beginGroup("Generate Spawns");
    if (replace) {
        ctx_.doc.deselectAll();
        for (const Entity& e : ctx_.doc.map().entities) {
            const std::string c = e.classname();
            if (c == "info_player_start" || (c.rfind("mp_", 0) == 0 && c.find("spawn") != std::string::npos)) ctx_.doc.select(SelRef{e.id, kNoId}, true);
        }
        if (!ctx_.doc.selection().empty()) ctx_.doc.deleteSelection();
    }
    for (const GeneratedSpawn& s : r.spawns)
        ctx_.doc.addEntity({{"classname", s.classname},
                            {"origin", formatNumber(s.origin.x) + " " + formatNumber(s.origin.y) + " " + formatNumber(s.origin.z)},
                            {"angles", "0 " + formatNumber(s.yaw) + " 0"}},
                           {}, "Generate Spawns");
    ctx_.doc.endGroup();
    ctx_.doc.deselectAll();
    ctx_.documentEdited();
    return int(r.spawns.size());
}

int MainWindow::generateStructure(bool building, int direction, int floors, QString* err) {
    // The selection's bounds; the selected worldspawn brushes are replaced (one undo step).
    const AABB box = ctx_.doc.selectionBounds();
    if (!box.valid()) {
        if (err) *err = "select a brush (its bounds are filled)";
        return 0;
    }
    std::string e;
    std::vector<StructureBox> parts;
    if (building) {
        BuildingOptions o;
        o.floors = floors;
        o.storeyHeight = std::max(96.0, (box.maxs.z - box.mins.z) / floors);
        o.stairs = floors > 1;
        parts = generateBuilding(box, o, &e);
    } else {
        StairOptions o;
        o.axis = direction / 2;
        o.reverse = direction % 2 == 1;
        parts = generateStairs(box, o, &e);
    }
    if (parts.empty()) {
        if (err) *err = qs(e);
        return 0;
    }
    Entity* w = ctx_.doc.worldspawn();
    if (!w) return 0;
    std::vector<Document::PrimEdit> edits;
    for (const auto& r : ctx_.doc.selection())
        if (r.entity == w->id && r.item != kNoId)
            if (const EntityItem* it = ctx_.doc.findItem(r.entity, r.item); it && it->prim() && it->prim()->kind == PrimKind::Brush)
                edits.push_back({r.entity, r.item, std::nullopt});
    const std::string mat = ctx_.currentMaterial.empty() ? std::string("caulk") : ctx_.currentMaterial;
    const std::string suffix = " " + mat + " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    int n = 0;
    for (const StructureBox& p : parts)
        if (auto prim = makeBrushPrimitive(makeBox(p.box, suffix), ctx_.doc.eol(), std::string("// ") + structurePartName(p.part))) {
            edits.push_back({w->id, kNoId, *prim});
            ++n;
        }
    if (!ctx_.doc.editPrimitives(edits, building ? "Generate Building" : "Generate Stairs")) return 0;
    ctx_.documentEdited();
    return n;
}

bool MainWindow::jumpToScriptLine(const QString& line) {
    // "<path>.gsc" followed by a line number ("file.gsc:123", "file.gsc, line 123", "file.gsc(123)").
    static const QRegularExpression re(R"(([\w./\\-]+\.gsc)\D{0,12}?(\d+))", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch() || !scriptPanel_) return false;
    const QString file = QFileInfo(m.captured(1)).fileName();
    int n = m.captured(2).toInt();
    const QString own = QFileInfo(scriptPanel_->scriptPath()).fileName();
    // The generated map script (extracted/maps/mp/<id>.gsc, named like the map) holds the mapper's own script after a
    // marker line (map_script.cpp); a reference to it is mapped back. The mapper's own file is usually <map>.gsc too,
    // so the generated one wins only when the last build wrote it.
    const QString generated = compile_->extractionDir() + "/extracted/maps/mp/" + compile_->mapId() + ".gsc";
    if (file.compare(compile_->mapId() + ".gsc", Qt::CaseInsensitive) == 0 && QFileInfo::exists(generated)) {
        QFile g(generated);
        int marker = 0, k = 0;
        if (g.open(QIODevice::ReadOnly | QIODevice::Text))
            while (!g.atEnd()) {
                ++k;
                if (g.readLine().contains("---- map script (worldspawn iw8r_script) ----")) {
                    marker = k;
                    break;
                }
            }
        if (marker == 0 || n <= marker) {
            ctx_.setStatus(QString("Line %1 of %2 is in the generated part of the map script (spawn, fx, sound and door setup), not in your script")
                               .arg(n).arg(file));
            return true;
        }
        n -= marker;
    } else if (own.isEmpty() || file.compare(own, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (ads::CDockWidget* d = dock_->findDockWidget("Script")) d->toggleView(true), d->setAsCurrentTab();
    const bool ok = scriptPanel_->goToLine(n);
    ctx_.setStatus(ok ? QString("Script line %1").arg(n) : QString("Your script has no line %1").arg(n));
    return true;
}

std::optional<std::pair<Vec3, Vec3>> MainWindow::playerView(QString* err) {
    if (!liveLink_ || !liveLink_->directAttach()) {
        if (err) *err = liveLink_ ? liveLink_->messageText() : QString("Live Link is not available");
        return std::nullopt;
    }
    std::string e;
    const auto v = liveLink_->direct().readPlayer(&e);
    if (!v) {
        if (err) *err = "no player (in a menu or loading?): " + qs(e);
        return std::nullopt;
    }
    return std::pair<Vec3, Vec3>{v->origin, v->angles};
}

bool MainWindow::selectAlongView(const Vec3& feet, const Vec3& gameAngles) {
    // Eye 60 units above the feet; the game's angles (positive pitch looks down) to a direction.
    const double p = gameAngles.x * 3.14159265358979 / 180.0, y = gameAngles.y * 3.14159265358979 / 180.0;
    Ray ray;
    ray.origin = feet + Vec3{0, 0, 60};
    ray.dir = Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
    ctx_.scene.sync(ctx_.doc);
    double t = 0;
    const auto hit = ctx_.scene.pick(ray, &t);
    if (!hit) {
        ctx_.setStatus("Nothing in the map along the player's view");
        return false;
    }
    ctx_.doc.deselectAll();
    ctx_.doc.select(*hit, true);
    ctx_.documentEdited(QString("Selected what the player looks at, %1 units away").arg(t, 0, 'f', 0));
    return true;
}

bool MainWindow::moveSelectionTo(const Vec3& feet) {
    const AABB b = ctx_.doc.selectionBounds();
    if (!b.valid()) {
        ctx_.setStatus("Select something to move to the player first");
        return false;
    }
    // The selection's bottom centre lands on the player's feet.
    const Vec3 delta = feet - Vec3{(b.mins.x + b.maxs.x) / 2, (b.mins.y + b.maxs.y) / 2, b.mins.z};
    if (!ctx_.doc.translateSelection(delta, "Move to Player")) return false;
    ctx_.documentEdited("Moved the selection to the player (one undo step)");
    return true;
}

QString MainWindow::editCustomMaterial(const QString& name, std::function<void(MaterialEditorDialog&)> drive) {
    // 4.17: the editor writes <map>/materials/<name>.iw8mat.json (the first custom material root), then the library reloads.
    const CustomMaterial* existing = name.isEmpty() || !customMaterials_ ? nullptr : customMaterials_->find(name.toStdString());
    QString root = customMaterialRoots().front();
    if (existing && !existing->defFile.empty()) root = QFileInfo(qs(existing->defFile)).absolutePath();
    MaterialEditorDialog dlg(root, existing, this);
    QString path;
    if (drive) {  // tests: fill the fields and save without showing the dialog
        drive(dlg);
        QString err;
        path = dlg.save(&err);
        if (path.isEmpty()) ctx_.setStatus("Custom material: " + err);
    } else if (dlg.exec() == QDialog::Accepted) {
        path = dlg.savedPath();
    }
    if (path.isEmpty()) return {};
    reloadCustomAssets();
    ctx_.scene.invalidate();
    ctx_.cameraMoved();
    ctx_.setStatus("Custom material saved: " + QFileInfo(path).fileName() + " (Materials browser lists it first)");
    return path;
}

size_t MainWindow::copySelection(bool cut) {
    size_t n = 0;
    const std::string text = selectionToClipboardText(ctx_.doc, &n);
    if (text.empty()) {
        ctx_.setStatus("Copy: nothing selected");
        return 0;
    }
    QGuiApplication::clipboard()->setText(QString::fromStdString(text));
    if (cut) {
        ctx_.doc.deleteSelection();
        ctx_.documentEdited(QString("Cut %1 item(s) to the clipboard").arg(n));
    } else {
        ctx_.setStatus(QString("Copied %1 item(s) (map text on the clipboard)").arg(n));
    }
    return n;
}

size_t MainWindow::pasteFromClipboard(QString* err) {
    std::string e;
    const std::string text = QGuiApplication::clipboard()->text().toStdString();
    // A pasted placement of a prefab bundle that carries an id another placement already holds gets a new id, in the
    // same undo step (two placements with one id would share their entity names in the build).
    const bool ids = text.find(std::string("\"") + kPrefabPlacementIdKey + "\"") != std::string::npos;
    if (ids) ctx_.doc.beginGroup("Paste");
    const size_t n = pasteClipboardText(ctx_.doc, text, &e);
    if (ids) {
        if (n) renewPlacementIds();
        ctx_.doc.endGroup();
    }
    if (!n) {
        if (err) *err = qs(e);
        return 0;
    }
    ctx_.scene.invalidate();
    ctx_.documentEdited();
    return n;
}

void MainWindow::fourViewLayout() {
    // 3.22: camera and the main 2D view (set to XY top) side by side, with Front (XZ) and Side (YZ) views under them.
    // The extra views keep their own pan and zoom (GridView::setFixedAxis); closing them leaves the usual layout.
    ctx_.gridAxis = 0;
    ads::CDockWidget* camDock = dock_->findDockWidget("Camera");
    ads::CDockWidget* gridDock = dock_->findDockWidget("Grid");
    if (!camDock || !gridDock) return;
    auto addView = [&](const QString& title, int axis, ads::CDockWidget* under) {
        ads::CDockWidget* d = dock_->findDockWidget(title);
        if (!d) {
            auto* g = new GridView(&ctx_);
            g->setFixedAxis(axis);
            g->setAcceptDrops(true);
            connect(g, &GridView::mouseWorld, this, [this](double x, double y, double z) {
                posLabel_->setText(QString("%1 %2 %3").arg(qs(formatNumber(x)), qs(formatNumber(y)), qs(formatNumber(z))));
            });
            d = new ads::CDockWidget(dock_, title);
            d->setObjectName(title);
            d->setWidget(g);
            d->setFeature(ads::CDockWidget::DockWidgetClosable, true);
            dock_->addDockWidget(ads::BottomDockWidgetArea, d, under->dockAreaWidget());
        } else {
            d->toggleView(true);
        }
        return d;
    };
    addView("Front (XZ)", 1, camDock);
    addView("Side (YZ)", 2, gridDock);
    ctx_.cameraMoved();
    ctx_.setStatus("Four-view layout: camera, top (XY), front (XZ), side (YZ); each 2D view pans and zooms on its own");
}

bool MainWindow::exportCameraImage(const QString& path, int w, int h, bool clean, QString* err) {
    const QImage img = camera_->renderImage(w, h, clean, 0, 20000);
    if (img.isNull()) {
        if (err) *err = QString("cannot render %1 x %2 (16384 is the largest side)").arg(w).arg(h);
        return false;
    }
    if (!img.save(path)) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

QString MainWindow::setLoadscreenFromCamera(QString* err) {
    const QString path = compile_->loadscreenImageFile();
    if (path.isEmpty()) {
        if (err) *err = "save the map first (the shot is kept beside it)";
        return {};
    }
    const QImage img = camera_->renderImage(2048, 1024, true, 16.0 / 9.0, 20000);
    if (img.isNull() || !img.save(path)) {
        if (err) *err = "cannot render or write " + path;
        return {};
    }
    return path;
}

int MainWindow::exportTurntable(const QString& dir, int frames, int w, int h, QString* err) {
    // Bounds of what is drawn for the selection (model meshes included), else the document's selection bounds.
    AABB box;
    for (const auto* g : ctx_.scene.items())
        if (g->bounds.valid() && ctx_.doc.isSelected(g->ref)) box.add(g->bounds);
    if (!box.valid()) box = ctx_.doc.selectionBounds();
    if (!box.valid() || frames < 1) {
        if (err) *err = "select a model, prefab or brushes first";
        return 0;
    }
    QDir().mkpath(dir);
    const Vec3 c = box.center();
    const double radius = std::max(16.0, length(box.maxs - box.mins) * 0.5);
    const double halfFov = std::max(10.0, ctx_.cam.fov) * 0.5 * 3.14159265358979 / 180.0 * std::min(1.0, double(h) / w);
    const double dist = radius / std::sin(std::max(0.05, halfFov)) * 1.1;
    const double elev = 20.0 * 3.14159265358979 / 180.0;
    const app::Camera saved = ctx_.cam;
    const std::set<SelRef> savedSel = ctx_.doc.selection();
    ctx_.doc.deselectAll();  // no selection tint in the frames
    int n = 0;
    for (int i = 0; i < frames; ++i) {
        const double yaw = 2.0 * 3.14159265358979 * i / frames;
        ctx_.cam.pos = c + Vec3{std::cos(yaw) * std::cos(elev), std::sin(yaw) * std::cos(elev), std::sin(elev)} * dist;
        ctx_.cam.yaw = yaw * 180.0 / 3.14159265358979 + 180.0;  // looking back at the centre
        ctx_.cam.pitch = -20.0;
        const QImage img = camera_->renderImage(w, h, true, 0, i == 0 ? 20000 : 0);
        if (img.isNull() || !img.save(QDir(dir).filePath(QString("turntable_%1.png").arg(i, 5, 10, QChar('0'))))) {
            if (err) *err = QString("frame %1 could not be rendered or written").arg(i);
            break;
        }
        ++n;
    }
    ctx_.cam = saved;
    for (const SelRef& r : savedSel) ctx_.doc.select(r, true);
    ctx_.cameraMoved();
    return n;
}

int MainWindow::exportCameraPathFrames(const QString& dir, double fps, int w, int h, QString* err) {
    const std::vector<CameraKey> frames = cameraPathFrames(camPath_, fps);
    if (frames.empty()) {
        if (err) *err = "the camera path has no frames";
        return 0;
    }
    QDir().mkpath(dir);
    camPathTimer_.stop();
    const app::Camera saved = ctx_.cam;
    int n = 0;
    for (const CameraKey& k : frames) {
        ctx_.cam.pos = k.pos;
        ctx_.cam.yaw = k.yaw;
        ctx_.cam.pitch = k.pitch;
        const QImage img = camera_->renderImage(w, h, true, 0, n == 0 ? 20000 : 0);
        if (img.isNull() || !img.save(QDir(dir).filePath(QString("frame_%1.png").arg(n, 5, 10, QChar('0'))))) {
            if (err) *err = QString("frame %1 could not be rendered or written").arg(n);
            break;
        }
        ++n;
    }
    ctx_.cam = saved;
    ctx_.cameraMoved();
    return n;
}

QString MainWindow::writeYupObj(const std::string& zupObj, const QString& stem, QString* err) {
    // The export is Z-up; custom OBJ models are Y-up (x, z, -y), like the editor's shape models.
    std::string yup;
    yup.reserve(zupObj.size());
    size_t pos = 0;
    while (pos < zupObj.size()) {
        size_t end = zupObj.find('\n', pos);
        if (end == std::string::npos) end = zupObj.size();
        const std::string line = zupObj.substr(pos, end - pos);
        double x, y, z;
        if ((line.rfind("v ", 0) == 0 || line.rfind("vn ", 0) == 0) &&
            std::sscanf(line.c_str() + (line[1] == 'n' ? 3 : 2), "%lf %lf %lf", &x, &y, &z) == 3)
            yup += (line[1] == 'n' ? "vn " : "v ") + formatNumber(x) + " " + formatNumber(z) + " " + formatNumber(-y) + "\n";
        else
            yup += line + "\n";
        pos = end + 1;
    }
    const QString dir = customModelRoots().front();
    QDir().mkpath(dir);
    QString name;
    for (int i = 1; i < 10000; ++i) {
        name = QString("%1%2").arg(stem).arg(i);
        if (!QFileInfo::exists(dir + "/" + name + ".obj")) break;
    }
    QSaveFile f(dir + "/" + name + ".obj");
    if (!f.open(QIODevice::WriteOnly) || f.write(yup.data(), qint64(yup.size())) != qint64(yup.size()) || !f.commit()) {
        if (err) *err = "cannot write " + dir + "/" + name + ".obj";
        return {};
    }
    modelCache_.erase(name.toStdString());
    return name;
}

size_t MainWindow::bakeSelectionToModel(QString* err) {
    // 8.8 / 2.24: the selected worldspawn brushes become one custom OBJ model (their materials and texture
    // coordinates kept) placed as a misc_model where they were; the brushes are removed. One undo step.
    if (ctx_.doc.path().empty()) {
        if (err) *err = "save the map first (the model is written beside it, in models/)";
        return 0;
    }
    Entity* w = ctx_.doc.worldspawn();
    if (!w) return 0;
    StockWorld sw;
    std::map<std::string, size_t> meshOf;
    std::vector<SelRef> brushes;
    for (const auto& r : ctx_.doc.selection()) {
        if (r.entity != w->id || r.item == kNoId) continue;
        const EntityItem* it = ctx_.doc.findItem(r.entity, r.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush) continue;
        brushes.push_back(r);
        const auto wins = faceWindingsOf(*p);
        for (size_t f = 0; f < wins.size() && f < p->faces.size(); ++f) {
            const std::string mat = p->faces[f].material();
            if (wins[f].size() < 3 || isIw8ToolMaterial(mat)) continue;  // caulk, clip, ... are not drawn
            const auto pl = facePlane(*p, f);
            if (!pl) continue;
            const TexParams tp = texParamsOfFace(*p, f).value_or(TexParams{});
            auto [mi, fresh] = meshOf.emplace(mat, sw.meshes.size());
            if (fresh) {
                sw.meshes.emplace_back();
                sw.meshes.back().material = mat;
            }
            ImportedMesh& m = sw.meshes[mi->second];
            const uint32_t base = uint32_t(m.pos.size());
            for (const Vec3& v : wins[f]) {
                const auto uv = faceUV(pl->n, tp, v);
                m.pos.push_back(v);
                m.normal.push_back(pl->n);
                m.uv.push_back({uv[0], 1.0 - uv[1]});  // OBJ convention (v = 0 at the bottom), as the stock world stores
                sw.bounds.add(v);
            }
            // Fan; ImportedMesh is counter-clockwise seen from outside: flip when the winding faces into the brush.
            const Vec3& w0 = wins[f][0];
            const bool flip = dot(cross(wins[f][1] - w0, wins[f][2] - w0), pl->n) < 0;
            for (size_t k = 1; k + 1 < wins[f].size(); ++k) {
                m.idx.push_back(base);
                m.idx.push_back(base + uint32_t(flip ? k + 1 : k));
                m.idx.push_back(base + uint32_t(flip ? k : k + 1));
            }
        }
    }
    if (brushes.empty() || sw.meshes.empty()) {
        if (err) *err = "select worldspawn brushes with visible (non-tool) faces";
        return 0;
    }
    AABB region = sw.bounds;
    region.add(region.mins - Vec3{1, 1, 1});
    region.add(region.maxs + Vec3{1, 1, 1});
    size_t tris = 0;
    std::vector<std::string> mats;
    Vec3 offset;
    const std::string obj = stockWorldToObj(sw, region, false, &tris, &mats, &offset);
    QString werr;
    const QString name = writeYupObj(obj, "baked_part", &werr);
    if (name.isEmpty()) {
        if (err) *err = werr;
        return 0;
    }
    ctx_.doc.beginGroup("Convert Brushes to Model");
    std::vector<Document::PrimEdit> dels;
    for (const SelRef& r : brushes) dels.push_back({r.entity, r.item, std::nullopt});
    ctx_.doc.editPrimitives(dels, "Convert Brushes to Model", false);
    ctx_.doc.addEntity({{"classname", "misc_model"}, {"model", name.toStdString()},
                        {"origin", formatNumber(offset.x) + " " + formatNumber(offset.y) + " " + formatNumber(offset.z)}},
                       {}, "Convert Brushes to Model");
    ctx_.doc.endGroup();
    ctx_.documentEdited(QString("%1 brush(es) -> models/%2.obj (%3 triangles, %4 material(s)), placed as a misc_model (one undo step)")
                            .arg(brushes.size()).arg(name).arg(tris).arg(mats.size()));
    return tris;
}

Id MainWindow::makeGlassPane() {
    // 5.12 (research 46): a thin brush with a blended material in a script_brushmodel becomes Replay's native breakable
    // glass (the converter replaces it; checked in a real build). One undo step: material, then the entity.
    bool brushes = false;
    for (const SelRef& r : ctx_.doc.selection())
        if (const EntityItem* it = ctx_.doc.findItem(r.entity, r.item); it && it->prim() && it->prim()->kind == PrimKind::Brush) brushes = true;
    if (!brushes) {
        ctx_.setStatus("Make Glass Pane: select the pane brush(es) first (thin: one side much smaller than the others)");
        return kNoId;
    }
    int n = 1;
    std::set<std::string> names;
    for (const auto& e : ctx_.doc.map().entities)
        if (const std::string* t = e.value("targetname")) names.insert(*t);
    while (names.count("glass_" + std::to_string(n))) ++n;
    const std::string name = "glass_" + std::to_string(n);
    ctx_.doc.beginGroup("Make Glass Pane");
    applyMaterialToSelection(ctx_, "iw8r_glass");
    const Id id = ctx_.doc.groupSelection("script_brushmodel");
    if (id != kNoId) ctx_.doc.setKeyValue(id, "targetname", name);
    ctx_.doc.endGroup();
    if (id == kNoId) {
        ctx_.setStatus("Make Glass Pane: only worldspawn brushes can become a pane");
        return kNoId;
    }
    ctx_.documentEdited(QString("Glass pane %1: script_brushmodel with the built-in iw8r_glass (breakable native glass in the "
                                "build; one undo step)").arg(qs(name)));
    return id;
}

Id MainWindow::makeLadder() {
    // Owner playtest 2 (2026-11-11): the bottom could not be climbed and the hands sat off the rungs. The ladder brush
    // is now a thin slab through the rungs (the model's rungs are about 3 units in from its back), and the model is a
    // live script_model without player collision (iw8r_no_collision), so nothing stands between the player and the
    // climbable faces. The model is not scaled: the nearest stock height is used and the brush has that height.
    const AABB sel = ctx_.doc.selectionBounds();
    Vec3 base = placementPoint();
    double want = 160;
    if (sel.valid()) {
        base = Vec3{(sel.mins.x + sel.maxs.x) * 0.5, (sel.mins.y + sel.maxs.y) * 0.5, sel.mins.z};
        want = sel.maxs.z - sel.mins.z;
    }
    struct Stock {
        double height;
        const char* model;
    };
    static const Stock kLadders[] = {{72, "tm/ladder_old_wood_01_72"}, {144, "tm/ladder_old_wood_01_144"}, {160, "tm/ladder_old_wood_01_160"}};
    const Stock* pick = &kLadders[2];
    for (const Stock& s : kLadders)
        if (std::fabs(s.height - want) < std::fabs(pick->height - want)) pick = &s;
    const double height = pick->height;
    AABB box;
    box.add(Vec3{base.x + 2.2, base.y - 12, base.z});
    box.add(Vec3{base.x + 4.2, base.y + 12, base.z + height});
    int n = 1;
    std::set<std::string> names;
    for (const auto& e : ctx_.doc.map().entities)
        if (const std::string* t = e.value("targetname")) names.insert(*t);
    while (names.count("iw8r_ladder_" + std::to_string(n))) ++n;
    const std::string before = ctx_.currentMaterial;
    ctx_.doc.beginGroup("Make Ladder");
    ctx_.currentMaterial = "ladder";
    const bool brush = createBoxBrush(ctx_, box);
    ctx_.currentMaterial = before;
    Id id = kNoId;
    if (brush)
        id = ctx_.doc.addEntity({{"classname", "script_model"}, {"model", pick->model},
                                 {"targetname", "iw8r_ladder_" + std::to_string(n)},
                                 {"origin", formatNumber(base.x) + " " + formatNumber(base.y) + " " + formatNumber(base.z)},
                                 {"iw8r_no_collision", "1"}},
                                {}, "Make Ladder");
    ctx_.doc.endGroup();
    if (id == kNoId) {
        ctx_.setStatus("Make Ladder: could not create the ladder here");
        return kNoId;
    }
    if (modelCacheStock_) modelCacheStock_->request({pick->model});
    ctx_.scene.invalidate();
    ctx_.documentEdited(QString("Ladder %1 units tall: a thin climbable brush through the rungs and the model %2 without collision "
                                "(move or rotate both together; one undo step)").arg(height).arg(pick->model));
    return id;
}

size_t MainWindow::exportReferenceBrushes() {
    if (!stockWorld_) {
        ctx_.setStatus("Stock Reference to Brushes: load a stock map reference first (View > Stock Map Reference)");
        return 0;
    }
    const AABB region = ctx_.doc.selectionBounds();
    if (!region.valid()) {
        ctx_.setStatus("Stock Reference to Brushes: select a brush or entities whose bounds cover the part to copy");
        return 0;
    }
    constexpr size_t kMaxBrushes = 4000;
    StockBrushResult r = stockWorldToBrushes(*stockWorld_, region, 2.0, kMaxBrushes, ctx_.doc.eol());
    if (r.brushes.empty()) {
        ctx_.setStatus("Stock Reference to Brushes: no stock triangles inside the selection bounds");
        return 0;
    }
    const size_t n = r.brushes.size();
    ctx_.doc.addEntity({{"classname", "func_group"}, {"iw8r_note", "stock " + referenceZone_.toStdString() + " geometry"}},
                       std::move(r.brushes), "Stock reference to brushes");
    ctx_.documentEdited(QString("Stock Reference to Brushes: %1 triangle prism brush(es) in a func_group (%2 stock material(s)%3%4; "
                                "one undo step)")
                            .arg(n)
                            .arg(r.materials.size())
                            .arg(r.skipped ? QString(", %1 degenerate triangle(s) skipped").arg(r.skipped) : QString())
                            .arg(r.capped ? QString(", stopped at %1: select a smaller area").arg(kMaxBrushes) : QString()));
    return n;
}

size_t MainWindow::exportReferenceRegion() {
    if (!stockWorld_) {
        ctx_.setStatus("Stock Reference to Model: load a stock map reference first (View > Stock Map Reference)");
        return 0;
    }
    if (ctx_.doc.path().empty()) {
        ctx_.setStatus("Stock Reference to Model: save the map first (the model is written beside it)");
        return 0;
    }
    const AABB region = ctx_.doc.selectionBounds();
    if (!region.valid()) {
        ctx_.setStatus("Stock Reference to Model: select a brush or entities whose bounds cover the part to copy");
        return 0;
    }
    size_t tris = 0;
    std::vector<std::string> mats;
    Vec3 offset;
    std::string obj = stockWorldToObj(*stockWorld_, region, true, &tris, &mats, &offset);
    if (tris == 0) {
        ctx_.setStatus("Stock Reference to Model: no stock triangles inside the selection bounds");
        return 0;
    }
    QString werr;
    const QString name = writeYupObj(obj, QString("stock_%1_part").arg(referenceZone_), &werr);
    if (name.isEmpty()) {
        ctx_.setStatus("Stock Reference to Model: " + werr);
        return 0;
    }
    ctx_.doc.addEntity({{"classname", "misc_model"}, {"model", name.toStdString()},
                        {"origin", formatNumber(offset.x) + " " + formatNumber(offset.y) + " " + formatNumber(offset.z)}},
                       {}, "Stock reference to model " + name.toStdString());
    ctx_.documentEdited(QString("%1: %2 stock triangles, %3 material(s), placed where they were (models/%1.obj; one undo step)")
                            .arg(name).arg(tris).arg(mats.size()));
    return tris;
}

PortWizardDialog* MainWindow::openPortWizard() {
    auto* dlg = findChild<PortWizardDialog*>("portWizard");
    if (!dlg)
        dlg = new PortWizardDialog([this](const QString& k) { return toolPath(k); },
                                   [this](const QString& path) {
                                       if (confirmDiscard()) loadDocument(path);
                                   },
                                   this);
    dlg->show();
    dlg->raise();
    return dlg;
}

UvEditor* MainWindow::openUvEditor() {
    auto* dlg = findChild<UvEditor*>("uvEditor");
    if (!dlg) {
        dlg = new UvEditor(&ctx_, [this](const QString& m) { return materialImage(m); }, this);
    } else {
        dlg->loadSelection();
    }
    if (!dlg->faceCount()) ctx_.setStatus("UV Editor: select brush faces (Ctrl+Shift+click) or brushes first");
    dlg->show();
    dlg->raise();
    return dlg;
}

QString MainWindow::findVfxExport(const QString& name) const {
    // The converter's stock cache (<name>.vfx.<n>.asset.json) and the editor's on-demand exports (<name>.<n>.asset.json).
    QString stem = name;
    stem.replace('\\', '/');
    const QString leaf = stem.section('/', -1);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString found;
    for (const QString& r : {base + "/x64zt_output/_stock_cache", base + "/vfx_cache"}) {
        const bool own = r.endsWith("vfx_cache");
        QDirIterator it(r, {own ? leaf + ".*.asset.json" : leaf + ".vfx.*.asset.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (QDir::fromNativeSeparators(f).contains("/" + stem + (own ? "." : ".vfx."))) return f;
            if (found.isEmpty()) found = f;
        }
    }
    return found;
}

QString MainWindow::showEffectInfo(const QString& name) {
    // The stock vfx exports the converter keeps (x64-zt _stock_cache, `<name>.vfx.<n>.asset.json`), research 50.
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/x64zt_output/_stock_cache";
    QString stem = name;
    stem.replace('\\', '/');
    const QString leaf = stem.section('/', -1);
    QString found;
    if (name.endsWith(".asset.json") && QFileInfo::exists(name)) found = name;  // an export file named directly
    // The converter's stock cache, then the editor's own on-demand exports (8.9).
    const QString ownCache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/vfx_cache";
    for (const QString& r : {root, ownCache}) {
        // The converter names its exports <name>.vfx.<n>.asset.json; ACTS run directly writes <name>.<n>.asset.json.
        const bool own = r == ownCache;
        QDirIterator it(r, {own ? leaf + ".*.asset.json" : leaf + ".vfx.*.asset.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (found.isEmpty() && it.hasNext()) {
            const QString f = it.next();
            if (QDir::fromNativeSeparators(f).contains("/" + stem + (own ? "." : ".vfx."))) {
                found = f;
                break;
            }
            if (found.isEmpty()) found = f;
        }
    }
    if (found.isEmpty() && !name.endsWith(".asset.json") && !vfxExporting_.contains(name)) {
        // 8.9: export it now with ACTS from the zone that holds it (always-loaded MP zones list), then show it again.
        QString zone;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && zone.isEmpty(); ++up, d.cdUp())
            if (QFile f(d.filePath("data/asset_catalog/iw8_common_vfx.tsv")); f.open(QIODevice::ReadOnly | QIODevice::Text))
                while (!f.atEnd()) {
                    const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split('\t');
                    if (c.size() >= 2 && c[0] == stem) {
                        zone = c[1].section(',', 0, 0).trimmed();
                        break;
                    }
                }
        if (!zone.isEmpty()) {
            vfxExporting_.insert(name);
            ActsRequest req;
            req.zone = zone;
            req.types = "vfx";
            req.names = {stem};
            req.outDir = ownCache + "/" + zone;
            QDir().mkpath(req.outDir);
            ActsRunner::shared()->submit([this](const QString& k) { return toolPath(k); }, req, this, [this, name](const ActsResult& r) {
                vfxExporting_.remove(name);
                fxSims_.erase(name.toStdString());  // the camera preview picks the new export up
                if (findChild<QDialog*>("effectInfoDialog")) showEffectInfo(name);  // refresh the open window
                if (!r.started || (r.exitCode != 0 && r.exitCode != 127)) ctx_.setStatus("Effect export failed for " + name + " (ACTS)");
            });
            ctx_.setStatus("Exporting " + name + " from " + zone + " with ACTS (Effect Info refreshes when it is done)");
        }
    }
    QString html = "<h3>" + name.toHtmlEscaped() + "</h3>";
    VfxInfo v;
    std::string err;
    if (found.isEmpty()) {
        html += vfxExporting_.contains(name)
                    ? QString("<p>Exporting this effect with ACTS now; this window refreshes when it is done.</p>")
                    : QString("<p>No export of this effect yet, and it is not in an always-loaded zone the editor can export it from. "
                              "The converter exports the stock effects a build uses (place it as an fx_origin and compile once), research 50.</p>");
    } else if (!readVfxExport(found.toStdString(), v, &err)) {
        html += "<p>Cannot read " + found.toHtmlEscaped() + ": " + qs(err).toHtmlEscaped() + "</p>";
    } else {
        const auto list = [](const std::vector<std::string>& xs) {
            QStringList l;
            for (const auto& x : xs) l << qs(x).toHtmlEscaped();
            return l.isEmpty() ? QString("<i>none</i>") : l.join("<br>");
        };
        html += QString("<table><tr><td>Emitters</td><td>%1</td></tr><tr><td>Plays</td><td>%2</td></tr>"
                        "<tr><td>Materials</td><td>%3</td></tr><tr><td>Models</td><td>%4</td></tr><tr><td>Sounds</td><td>%5</td></tr>"
                        "<tr><td>Child effects</td><td>%6</td></tr></table><p style='color:#9aa'>%7</p>")
                    .arg(v.emitterCount)
                    .arg(v.looping ? QString("looping") : QString("one-shot, about %1 s").arg(v.lifeSeconds, 0, 'f', 1))
                    .arg(list(v.materials), list(v.models), list(v.sounds), list(v.childEffects), found.toHtmlEscaped());
    }
    showAssetInfo(this, "effectInfoDialog", "Effect Info: " + name, html);
    // 8.1: animated preview of the export (simplified particle sim) above the text.
    if (auto* dlg = findChild<QDialog*>("effectInfoDialog")) {
        auto* pv = dlg->findChild<VfxPreview*>();
        if (!pv) {
            pv = new VfxPreview(dlg);
            if (auto* lay = qobject_cast<QVBoxLayout*>(dlg->layout())) lay->insertWidget(0, pv, 1);
            dlg->resize(dlg->width(), std::max(dlg->height(), 760));
        }
        VfxSimDef sim;
        std::string text;
        const bool ok = !found.isEmpty() && readFile(found.toStdString(), text) && buildVfxSim(text, sim) && !sim.emitters.empty();
        pv->setVisible(ok);
        if (ok) pv->setEffect(sim);
        if (ok)
            if (const AABB b = vfxBounds(sim); b.valid())
                if (auto* tb = dlg->findChild<QTextBrowser*>()) {
                    // 8.9: the simulated extent (particle centres +- half their size over the run).
                    html += QString("<p>Bounds (simulated): %1 x %2 x %3 units, from (%4 %5 %6) around the origin.</p>")
                                .arg(b.maxs.x - b.mins.x, 0, 'f', 0).arg(b.maxs.y - b.mins.y, 0, 'f', 0).arg(b.maxs.z - b.mins.z, 0, 'f', 0)
                                .arg(b.mins.x, 0, 'f', 0).arg(b.mins.y, 0, 'f', 0).arg(b.mins.z, 0, 'f', 0);
                    tb->setHtml(html);
                }
    }
    return html;
}

QString MainWindow::showModelInfo(const QString& name) {
    ModelInfoInput in;
    in.name = name;
    const std::string n = name.toStdString();
    for (const auto& e : ctx_.doc.map().entities)
        if (const std::string* m = e.value("model"); m && *m == n) ++in.placed;
    in.entry = modelCacheStock_->lookup(name);
    if (!in.entry) modelCacheStock_->request({name});
    else if (!in.entry->failed) in.mesh = modelCacheStock_->loadModel(name);
    in.detail = modelCacheStock_->loadDetail(name);
    if (in.entry && !in.entry->failed && !in.detail) modelCacheStock_->refetch(name);  // older cache entry: get LODs / bones
    for (const auto& e : ctx_.doc.map().entities)
        if (const std::string* m = e.value("model"); m && *m == n)
            if (const auto o = e.origin()) {
                const double d = length(*o - ctx_.cam.pos);
                if (in.cameraDistance < 0 || d < in.cameraDistance) in.cameraDistance = d;
            }
    const QString html = modelInfoHtml(in);
    showAssetInfo(this, "modelInfoDialog", "Model Info: " + name, html);
    return html;
}

QMenu* MainWindow::buildCameraContextMenu(const QPointF& widgetPos) {
    auto* menu = new QMenu(this);
    menu->setObjectName("cameraContextMenu");
    SelRef hit;
    const std::optional<Vec3> at = camera_->surfacePoint(widgetPos, &hit);
    const auto fmt = [](const Vec3& p) {
        return formatNumber(std::round(p.x)) + " " + formatNumber(std::round(p.y)) + " " + formatNumber(std::round(p.z));
    };
    if (at) {
        const Entity* he = ctx_.doc.findEntity(hit.entity);
        menu->addAction(QString("Select %1").arg(he ? qs(he->classname()) : QString("item")), this, [this, hit] {
            ctx_.doc.select(hit, false);
            ctx_.documentEdited("Selected from the camera menu");
        });
        if (!ctx_.doc.selection().empty())
            menu->addAction("Move Selection Here", this, [this, p = *at] {
                const AABB b = ctx_.doc.selectionBounds();
                if (!b.valid()) return;
                Vec3 d = p - Vec3{(b.mins.x + b.maxs.x) / 2, (b.mins.y + b.maxs.y) / 2, b.mins.z};
                for (int a = 0; a < 3; ++a) d[a] = snapToGrid(d[a], ctx_.doc.grid());
                ctx_.doc.beginDrag();
                ctx_.doc.updateDrag(d);
                ctx_.documentEdited(ctx_.doc.endDrag(true) ? QString("Moved the selection to the clicked surface") : QString());
            });
        QMenu* shapes = menu->addMenu("Add Shape Model Here");
        for (const ShapeInfo& sh : shapeKinds())
            shapes->addAction(sh.label, this, [this, k = int(sh.kind), p = *at] {
                QString name;
                if (!addShapeModel(k, &name)) return;
                // addShapeModel places at the view centre: move the new entity to the clicked point.
                for (auto it = ctx_.doc.map().entities.rbegin(); it != ctx_.doc.map().entities.rend(); ++it)
                    if (it->value("model") && qs(*it->value("model")) == name) {
                        ctx_.doc.setKeyValue(it->id, "origin", formatNumber(snapToGrid(p.x, ctx_.doc.grid())) + " " +
                                                                   formatNumber(snapToGrid(p.y, ctx_.doc.grid())) + " " + formatNumber(p.z));
                        ctx_.documentEdited("Shape placed at the clicked surface");
                        break;
                    }
            });
        menu->addAction(QString("Copy Position (%1)").arg(qs(fmt(*at))), this, [p = *at, fmt] {
            QGuiApplication::clipboard()->setText(QString::fromStdString(fmt(p)));
        });
        menu->addSeparator();
    }
    if (!ctx_.doc.selection().empty()) {
        for (const char* id : {"DropToFloor", "MakeDestructible", "MakeScripted", "EnterPrefab"})
            if (QAction* a = action(id)) menu->addAction(a);
        menu->addAction("Deselect", this, [this] {
            ctx_.doc.deselectAll();
            ctx_.documentEdited();
        });
        menu->addSeparator();
    }
    QMenu* speed = menu->addMenu("Camera Speed");
    for (int v : {150, 300, 600, 1200, 2400})
        speed->addAction(QString::number(v) + (ctx_.cam.speed == v ? "  (current)" : ""), this, [this, v] {
            ctx_.cam.speed = v;
            Settings::instance().setValue("camera/speed", v);
            ctx_.setStatus(QString("Camera speed %1").arg(v));
        });
    QMenu* fov = menu->addMenu("Field of View");
    for (int v : {70, 80, 90, 100, 110})
        fov->addAction(QString::number(v) + (std::lround(ctx_.cam.fov) == v ? "  (current)" : ""), this, [this, v] {
            ctx_.cam.fov = v;
            Settings::instance().setValue("camera/fov", v);
            ctx_.cameraMoved();
        });
    for (const char* id : {"GizmoMode", "GizmoScale", "ToggleLitPreview", "ToggleTexturedView"})
        if (QAction* a = action(id)) menu->addAction(a);
    return menu;
}

QStringList MainWindow::defaultToolbarCommands() {
    return {"NewMap", "Open", "Save", "Undo", "Redo", "GizmoMode", "GizmoScale", "ToggleTexturedView", "ToggleLitPreview", "CompileMap",
            "BuildDeployLoad", "PlayPreview", "WebServer"};
}

namespace {
// Flat 16 px line icons in the UI's text colour, so the toolbar matches the dark theme (owner: the stock icons did not
// fit). One small drawing per command; unknown commands get a dot.
QIcon toolbarIcon(const QString& id, const QColor& ink) {
    QPixmap pm(32, 32);  // drawn at 2x for high-DPI, shown at 16
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ink, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    auto arrowHead = [&](QPointF tip, QPointF dir) {
        const QPointF n(-dir.y(), dir.x());
        p.drawLine(tip, tip - dir * 6 + n * 5);
        p.drawLine(tip, tip - dir * 6 - n * 5);
    };
    if (id == "NewMap") {
        p.drawPolyline(QPolygonF{{20, 4}, {8, 4}, {8, 28}, {24, 28}, {24, 8}, {20, 4}});
        p.drawLine(QPointF(16, 13), QPointF(16, 21)), p.drawLine(QPointF(12, 17), QPointF(20, 17));
    } else if (id == "Open") {
        p.drawPolyline(QPolygonF{{4, 26}, {4, 7}, {12, 7}, {15, 10}, {26, 10}, {26, 26}, {4, 26}});
    } else if (id == "Save") {
        p.drawRect(QRectF(5, 5, 22, 22));
        p.drawRect(QRectF(10, 5, 12, 8));
        p.drawLine(QPointF(10, 21), QPointF(22, 21));
    } else if (id == "Undo" || id == "Redo") {
        const bool undo = id == "Undo";
        QPainterPath path;
        path.moveTo(undo ? 26 : 6, 24);
        path.cubicTo(undo ? 26 : 6, 10, undo ? 12 : 20, 10, undo ? 7 : 25, 12);
        p.drawPath(path);
        arrowHead(QPointF(undo ? 6 : 26, 12), QPointF(undo ? -1 : 1, 0));
    } else if (id == "GizmoMode") {
        p.drawLine(QPointF(16, 5), QPointF(16, 27)), p.drawLine(QPointF(5, 16), QPointF(27, 16));
        arrowHead({16, 4}, {0, -1}), arrowHead({16, 28}, {0, 1}), arrowHead({4, 16}, {-1, 0}), arrowHead({28, 16}, {1, 0});
    } else if (id == "GizmoScale") {
        p.drawRect(QRectF(5, 13, 14, 14));
        p.drawLine(QPointF(14, 18), QPointF(26, 6));
        arrowHead({26, 6}, {0.707, -0.707});
    } else if (id == "ToggleTexturedView") {
        p.drawRect(QRectF(5, 5, 22, 22));
        p.fillRect(QRectF(5, 5, 11, 11), ink), p.fillRect(QRectF(16, 16, 11, 11), ink);
    } else if (id == "ToggleLitPreview") {
        p.drawEllipse(QPointF(16, 16), 6, 6);
        for (int k = 0; k < 8; ++k) {
            const double a = k * 3.14159265 / 4;
            p.drawLine(QPointF(16 + 9 * std::cos(a), 16 + 9 * std::sin(a)), QPointF(16 + 13 * std::cos(a), 16 + 13 * std::sin(a)));
        }
    } else if (id == "CompileMap") {
        p.drawPolygon(QPolygonF{{9, 6}, {25, 16}, {9, 26}});
    } else if (id == "BuildDeployLoad") {
        p.drawPolygon(QPolygonF{{5, 7}, {16, 16}, {5, 25}});
        p.drawPolygon(QPolygonF{{16, 7}, {27, 16}, {16, 25}});
    } else if (id == "PlayPreview") {
        p.drawRect(QRectF(4, 7, 24, 16));
        p.drawLine(QPointF(12, 27), QPointF(20, 27));
    } else if (id == "WebServer") {
        p.drawEllipse(QPointF(16, 16), 11, 11);
        p.drawEllipse(QPointF(16, 16), 5, 11);
        p.drawLine(QPointF(5, 16), QPointF(27, 16));
    } else {
        p.setBrush(ink);
        p.drawEllipse(QPointF(16, 16), 4, 4);
    }
    p.end();
    QIcon icon;
    icon.addPixmap(pm);
    return icon;
}

QString toolbarLabel(const QString& id, const QString& fallback) {
    static const QHash<QString, QString> labels = {
        {"NewMap", "New"},          {"Open", "Open"},       {"Save", "Save"},         {"Undo", "Undo"},
        {"Redo", "Redo"},           {"GizmoMode", "Move"},  {"GizmoScale", "Scale"},  {"ToggleTexturedView", "Textured"},
        {"ToggleLitPreview", "Lit"}, {"CompileMap", "Build"}, {"BuildDeployLoad", "Play"}, {"PlayPreview", "Preview"},
        {"WebServer", "Web"}};
    if (labels.contains(id)) return labels.value(id);
    QString t = fallback;
    t.remove('&');
    t = t.section(" (", 0, 0).section("...", 0, 0);
    return t.size() > 14 ? t.left(13) + "." : t;
}
}  // namespace

void MainWindow::rebuildMainToolbar() {
    if (!mainToolbar_) {
        mainToolbar_ = new QToolBar("Main", this);
        mainToolbar_->setObjectName("mainToolbar");
        mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        mainToolbar_->setIconSize(QSize(16, 16));
        // Compact: small padding and spacing (owner: the toolbar was too big for the rest of the UI).
        mainToolbar_->setStyleSheet("QToolBar#mainToolbar { spacing: 1px; padding: 1px; }"
                                    "QToolBar#mainToolbar QToolButton { padding: 2px 5px; margin: 0px; }");
        addToolBar(Qt::TopToolBarArea, mainToolbar_);
        insertToolBarBreak(mainToolbar_);
    }
    mainToolbar_->clear();
    const QStringList ids = Settings::instance().value("ui/toolbar", defaultToolbarCommands()).toStringList();
    const QColor ink = palette().color(QPalette::WindowText);
    for (const QString& id : ids) {
        QAction* a = actions_.value(id);
        if (!a) continue;
        // A toolbar twin of the command: short label and flat icon; the menu keeps the full text.
        auto* t = new QAction(toolbarIcon(id, ink), toolbarLabel(id, a->text()), mainToolbar_);
        t->setObjectName("tb_" + id);
        t->setToolTip(a->text().remove('&') + (a->shortcut().isEmpty() ? QString() : "  (" + a->shortcut().toString() + ")"));
        t->setCheckable(a->isCheckable());
        t->setChecked(a->isChecked());
        connect(t, &QAction::triggered, a, [a] { a->trigger(); });
        connect(a, &QAction::toggled, t, [t](bool on) { t->setChecked(on); });
        connect(a, &QAction::changed, t, [t, a] { t->setEnabled(a->isEnabled()), t->setChecked(a->isChecked()); });
        mainToolbar_->addAction(t);
    }
}

bool MainWindow::dropAsset(const QString& kind, const QString& name, bool loop, const Vec3& at, SelRef hit) {
    if (name.isEmpty()) return false;
    if (kind == "materials") {
        // Onto a brush, patch or model: that object gets the material. The hover preview already applied it when the
        // drop lands on the previewed object; it is then simply kept.
        if (hit.entity == kNoId) {
            endMaterialPreview(false);
            ctx_.setStatus("Drop a material onto a brush, patch or model");
            return false;
        }
        const bool previewed = previewActive_ && previewHit_ == hit && previewMaterial_ == name;
        if (previewed) endMaterialPreview(true);
        else {
            endMaterialPreview(false);
            if (!applyMaterialTo(hit, name)) {
                ctx_.setStatus("That object cannot take a material (brushes, patches and models can)");
                return false;
            }
        }
        ctx_.currentMaterial = name.toStdString();
        if (materialEdit_) materialEdit_->setText(name);
        ctx_.scene.invalidate();
        ctx_.documentEdited("Applied " + name + " (undoable)");
        return true;
    }
    Vec3 p = at;
    if (ctx_.snap) for (int a = 0; a < 3; ++a) p[a] = snapToGrid(p[a], ctx_.doc.grid());
    if (kind == "prefabs" && prefabPreview_.active) {
        // The drag preview is already a placement at this point: the drop keeps it (still one undo step).
        if (prefabPreview_.file == name && length(prefabPreview_.at - at) < 1e-6 && ctx_.doc.undoPosition() == prefabPreview_.undoPos + 1) {
            const Entity* e = ctx_.doc.findEntity(prefabPreview_.entity);
            const std::string* model = e ? e->value("model") : nullptr;
            endPrefabPreview(true);
            ctx_.documentEdited("Inserted prefab " + (model ? qs(*model) : name) + " (undoable)");
            return true;
        }
        endPrefabPreview(false);
    }
    dropPoint_ = p;
    if (kind == "models") emit models_->modelChosen(name);
    else if (kind == "prefabs") {
        QString err;
        if (!insertPrefab(name, &err)) ctx_.setStatus("Prefab not placed: " + err);
    } else if (kind == "effects") emit effects_->effectChosen(name);
    else if (kind == "sounds") emit sounds_->soundChosen(name, loop);
    else {
        dropPoint_.reset();
        return false;
    }
    dropPoint_.reset();
    return true;
}

bool MainWindow::applyMaterialTo(SelRef hit, const QString& material) {
    const std::string m = material.toStdString();
    if (hit.entity == kNoId || m.empty()) return false;
    if (hit.item != kNoId) {
        const EntityItem* it = ctx_.doc.findItem(hit.entity, hit.item);
        if (!it || !it->prim()) return false;
        Primitive p = *it->prim();
        if (!p.setMaterial(m)) return false;
        std::vector<Document::PrimEdit> edits;
        edits.push_back({hit.entity, hit.item, std::move(p)});
        return ctx_.doc.editPrimitives(edits, "Apply " + m);
    }
    const Entity* e = ctx_.doc.findEntity(hit.entity);
    if (!e) return false;
    const std::string cls = e->classname();
    if (cls != "misc_model" && cls != "script_model" && cls != "dynamic_prop") return false;
    const std::string* cur = e->value("iw8r_material");
    if (cur && *cur == m) return false;
    return ctx_.doc.setKeyValue(hit.entity, "iw8r_material", m);
}

void MainWindow::previewMaterialAt(const QString& material, SelRef hit) {
    if (previewActive_ && previewHit_ == hit && previewMaterial_ == material) return;
    endMaterialPreview(false);
    if (hit.entity == kNoId) return;
    const size_t pos = ctx_.doc.undoPosition();
    if (!applyMaterialTo(hit, material) || ctx_.doc.undoPosition() != pos + 1) return;
    previewActive_ = true;
    previewHit_ = hit;
    previewMaterial_ = material;
    previewUndoPos_ = pos;
    ctx_.scene.invalidate();
    camera_->update();
    ctx_.setStatus("Preview: " + material + " (drop to apply, move away to cancel)");
}

void MainWindow::endMaterialPreview(bool keep) {
    if (!previewActive_) return;
    previewActive_ = false;
    if (!keep) {
        // The preview is the newest undo step (nothing else edits during a drag): take exactly that step back.
        if (ctx_.doc.undoPosition() == previewUndoPos_ + 1) ctx_.doc.undo();
        ctx_.scene.invalidate();
        camera_->update();
    }
    previewHit_ = {kNoId, kNoId};
    previewMaterial_.clear();
}

size_t MainWindow::splineFromSelection(const QString& kind, double a, double b, bool closed) {
    std::vector<Vec3> pts;
    std::set<Id> seen;
    for (const SelRef& r : ctx_.doc.selection()) {
        if (r.item != kNoId || !seen.insert(r.entity).second) continue;
        if (const Entity* e = ctx_.doc.findEntity(r.entity))
            if (auto o = e->origin()) pts.push_back(*o);
    }
    if (pts.size() < (closed ? 3u : 2u)) return 0;
    const std::string mat = ctx_.currentMaterial.empty() ? std::string("caulk") : ctx_.currentMaterial;
    const std::vector<Vec3> path = sampleSpline(pts, std::max(16.0, ctx_.doc.grid() * 4), closed);
    std::vector<Primitive> prims;
    if (kind == "wall") prims = wallAlongSpline(path, a, b, mat, closed);
    else if (kind == "posts") prims = postsAlongSpline(path, a, 8, b, mat);
    else if (kind == "pipe") prims = pipeAlongSpline(path, a, std::clamp(int(b), 4, 16), mat);
    Entity* ws = ctx_.doc.worldspawn();
    if (prims.empty() || !ws) return 0;
    std::vector<Document::PrimEdit> edits;
    for (auto& p : prims) edits.push_back({ws->id, kNoId, std::move(p)});
    if (!ctx_.doc.editPrimitives(edits, "Spline " + kind.toStdString())) return 0;
    ctx_.scene.invalidate();
    ctx_.documentEdited(QString("Spline %1: %2 brush(es) through %3 point(s) (one undo step)").arg(kind).arg(edits.size()).arg(pts.size()));
    return edits.size();
}

QString MainWindow::toggleMacroRecording() {
    if (!macroRecording_) {
        macro_.clear();
        macroRecording_ = true;
        ctx_.doc.macroSink = [this](const std::string& l) {
            macro_.record(l);
            ctx_.setStatus(QString("Recording macro: %1 line(s), last: %2").arg(macro_.size()).arg(qs(l)));
        };
        if (QAction* a = actions_.value("RecordMacro")) a->setText("Stop Macro Recording");
        ctx_.setStatus("Recording macro: moves, clones, deletes and key edits of the selection are recorded");
        return {};
    }
    macroRecording_ = false;
    ctx_.doc.macroSink = nullptr;
    if (QAction* a = actions_.value("RecordMacro")) a->setText("Start / Stop Macro Recording");
    const QString text = qs(macro_.text());
    if (text.isEmpty()) {
        ctx_.setStatus("Macro recording stopped: nothing was recorded");
        return {};
    }
    Settings::instance().setValue("scripts/last", "# recorded macro: runs on the current selection\n" + text);
    ctx_.setStatus(QString("Macro recording stopped: %1 line(s)").arg(macro_.size()));
    return text;
}

QString MainWindow::runScriptText(const QString& text, bool oneUndoStep, QStringList* log) {
    std::vector<std::string> lines;
    auto sink = std::move(ctx_.doc.macroSink);  // a replay is not recorded again
    ctx_.doc.macroSink = nullptr;
    const ScriptResult r = runCommandScript(ctx_.doc, text.toStdString(), oneUndoStep, &lines);
    ctx_.doc.macroSink = std::move(sink);
    if (log)
        for (const auto& l : lines) *log << qs(l);
    ctx_.scene.invalidate();
    ctx_.documentEdited(qs(r.message));
    return qs(r.message);
}

QString MainWindow::camPathFile() const {
    if (ctx_.doc.path().empty()) return {};
    const QFileInfo fi(QString::fromStdString(ctx_.doc.path()));
    return fi.absoluteDir().filePath(fi.completeBaseName() + ".campaths.json");
}

void MainWindow::stepCameraPath(double t) {
    if (camPath_.keys.empty()) return;
    const CameraKey k = sampleCameraPath(camPath_, t);
    ctx_.cam.pos = k.pos;
    ctx_.cam.yaw = k.yaw;
    ctx_.cam.pitch = k.pitch;
    ctx_.cameraMoved();
}

QString MainWindow::bookmarkKey(int slot) const {
    // Per map file; an unsaved map shares one set.
    const QString path = ctx_.doc.path().empty() ? QString("untitled") : QFileInfo(QString::fromStdString(ctx_.doc.path())).absoluteFilePath();
    return QString("cameraBookmarks/%1/%2").arg(QString::number(qHash(path.toLower()), 16)).arg(slot);
}

bool MainWindow::saveCameraBookmark(int slot) {
    if (slot < 1 || slot > 9) return false;
    const auto& c = ctx_.cam;
    Settings::instance().setValue(bookmarkKey(slot), QString("%1 %2 %3 %4 %5").arg(c.pos.x).arg(c.pos.y).arg(c.pos.z).arg(c.yaw).arg(c.pitch));
    ctx_.setStatus(QString("Camera bookmark %1 saved (Ctrl+%1 returns here)").arg(slot));
    return true;
}

bool MainWindow::goToCameraBookmark(int slot) {
    if (slot < 1 || slot > 9) return false;
    const QStringList p = Settings::instance().value(bookmarkKey(slot)).toString().split(' ', Qt::SkipEmptyParts);
    if (p.size() != 5) return false;
    ctx_.cam.pos = {p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
    ctx_.cam.yaw = p[3].toDouble();
    ctx_.cam.pitch = p[4].toDouble();
    ctx_.cameraMoved();
    ctx_.setStatus(QString("Camera bookmark %1").arg(slot));
    return true;
}

Vec3 MainWindow::placementPoint() const {
    if (dropPoint_) return *dropPoint_;
    Vec3 at;
    at[ctx_.axisH()] = snapToGrid(ctx_.gridCenterH, ctx_.doc.grid());
    at[ctx_.axisV()] = snapToGrid(ctx_.gridCenterV, ctx_.doc.grid());
    const AABB sel = ctx_.doc.selectionBounds();
    at[ctx_.axisD()] = sel.valid() ? sel.mins[ctx_.axisD()] : 0;
    return at;
}

bool MainWindow::setRegionFromSelection(QString* err) {
    const AABB b = ctx_.doc.selectionBounds();
    if (ctx_.doc.selection().empty() || !b.valid()) {
        if (err) *err = "Select brushes or entities to set a region from them";
        return false;
    }
    ctx_.region = b;
    ctx_.documentEdited(QString("Region set from the selection (%1 hidden outside it)").arg(ctx_.scene.hiddenByRegion.size()));
    return true;
}

bool MainWindow::setRegionFromXYView(QString* err) {
    if (!grid_) {
        if (err) *err = "no grid view";
        return false;
    }
    // Visible area of the grid view in its own H/V axes; the depth axis and the grid view's hidden axis stay full.
    constexpr double kFull = 1e6;
    AABB r;
    r.mins = {-kFull, -kFull, -kFull};
    r.maxs = {kFull, kFull, kFull};
    r.mins[ctx_.axisH()] = std::min(grid_->widgetToWorldH(0), grid_->widgetToWorldH(grid_->width()));
    r.maxs[ctx_.axisH()] = std::max(grid_->widgetToWorldH(0), grid_->widgetToWorldH(grid_->width()));
    r.mins[ctx_.axisV()] = std::min(grid_->widgetToWorldV(grid_->height()), grid_->widgetToWorldV(0));
    r.maxs[ctx_.axisV()] = std::max(grid_->widgetToWorldV(grid_->height()), grid_->widgetToWorldV(0));
    ctx_.region = r;
    ctx_.documentEdited(QString("Region set from the grid view (%1 hidden outside it)").arg(ctx_.scene.hiddenByRegion.size()));
    return true;
}

void MainWindow::clearRegion() {
    ctx_.region.reset();
    ctx_.documentEdited("Region off");
}

bool MainWindow::saveSelectedAs(const QString& file, QString* err) {
    if (ctx_.doc.selection().empty()) {
        if (err) *err = "nothing selected";
        return false;
    }
    const Document& doc = ctx_.doc;
    // Selected primitives, selected brush entities (as a unit) and selected point entities; worldspawn keys always.
    const std::string text = mapTextSubset(doc.map(), [&doc](Id entity, Id item) {
        if (item == kNoId) return doc.isSelected({entity, kNoId});
        if (doc.isSelected({entity, item})) return true;
        const Entity* e = doc.findEntity(entity);
        return e && !e->isWorldspawn() && doc.isSelected({entity, kNoId});
    });
    std::string werr;
    if (!writeFileAtomic(file.toStdString(), text, &werr)) {
        if (err) *err = qs(werr);
        return false;
    }
    ctx_.setStatus("Saved the selection to " + file);
    return true;
}

bool MainWindow::saveRegionAs(const QString& file, QString* err) {
    if (!ctx_.region) {
        if (err) *err = "no region set (View > Region > Set From Selection or Set From XY View)";
        return false;
    }
    std::string werr;
    if (!writeFileAtomic(file.toStdString(), mapTextInRegion(ctx_.doc.map(), *ctx_.region), &werr)) {
        if (err) *err = qs(werr);
        return false;
    }
    ctx_.setStatus("Saved the region to " + file);
    return true;
}

void MainWindow::saveSubsetDialog(bool regionOnly) {
    if (regionOnly ? !ctx_.region : ctx_.doc.selection().empty()) {
        return ctx_.setStatus(regionOnly ? "Set a region first (View > Region)" : "Select brushes or entities to save");
    }
    const QString start = ctx_.doc.path().empty() ? Settings::instance().value("lastDir").toString()
                                                  : QFileInfo(qs(ctx_.doc.path())).absolutePath();
    const QString f = QFileDialog::getSaveFileName(this, regionOnly ? "Save Region" : "Save Selected", start, "Map files (*.map)");
    if (f.isEmpty()) return;
    QString err;
    const bool ok = regionOnly ? saveRegionAs(f, &err) : saveSelectedAs(f, &err);
    if (!ok) QMessageBox::warning(this, regionOnly ? "Save Region" : "Save Selected", err);
}

void MainWindow::openStockZoneDialog() {
    if (!confirmDiscard()) return;
    const QString install = QFileInfo(toolPath("replay")).absolutePath();
    const QString zoneDir = install + "/zone";
    // The stock map catalogue (11.1, tools/build_stock_map_catalog.py): openable maps first, Browse as a fallback.
    QStringList items;
    QStringList paths;
    {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_stock_maps.tsv")); ++up) d.cdUp();
        QFile f(d.filePath("data/asset_catalog/iw8_stock_maps.tsv"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QStringList c = in.readLine().split('\t');
                if (c.size() < 7 || c[0].startsWith('#') || c[0] == "map_id" || c[6] != "yes" || c[2].isEmpty()) continue;
                const QString path = QDir(install).filePath(c[2]);
                if (!QFileInfo::exists(path)) continue;
                items << QString("%1   (%2, %3 MB)").arg(c[0], c[2]).arg(c[5].toLongLong() / (1024 * 1024));
                paths << path;
            }
        }
    }
    items << "Browse for a .ff file...";
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, "Open IW8 Stock Map", "Stock map (Replay 1.20 install):", items, 0, false, &ok);
    if (!ok) return;
    QString zone;
    if (const int i = int(items.indexOf(pick)); i >= 0 && i < paths.size()) zone = paths[i];
    else zone = QFileDialog::getOpenFileName(this, "Open IW8 Stock Map (Replay 1.20 zone)", zoneDir, "Replay fastfiles (*.ff)");
    if (zone.isEmpty()) return;
    QString err;
    if (!openStockZone(zone, &err)) QMessageBox::critical(this, "Open IW8 Stock Map", err);
}

bool MainWindow::openStockZone(const QString& zone, QString* err) {
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    for (const QString& f : {acts, replay, oodle, zone})
        if (!QFileInfo::exists(f)) {
            if (err) *err = "Missing file: " + f + "\nSet tool locations in Tools > Tool Paths.";
            return false;
        }
    const QString name = QFileInfo(zone).completeBaseName();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_cache/" + name;
    QString json = findMapEntsJson(cache);
    if (json.isEmpty()) {
        QDir().mkpath(cache);
        // While an edit of this map is installed, the patch beside the zone is ours: the stock map is read from a
        // copy with the stock patch saved at install (main_window_stock_edit.cpp).
        QString viewDir;
        const QString readZone = stockZoneForReading(zone, &viewDir, err);
        if (readZone.isEmpty()) return false;
        QProcess proc;
        proc.setProgram(acts);
        proc.setArguments({"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "map_ents", "-o", cache, readZone});
        proc.setProcessChannelMode(QProcess::MergedChannels);
        QProgressDialog progress("Reading " + name + " with ACTS (map_ents only)...", "Cancel", 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(500);
        proc.start();
        while (!proc.waitForFinished(100)) {
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                proc.kill();
                proc.waitForFinished(3000);
                if (!viewDir.isEmpty()) QDir(viewDir).removeRecursively();
                if (err) *err = "ACTS cancelled";
                return false;
            }
            if (proc.state() == QProcess::NotRunning) break;
        }
        const QString log = QString::fromLocal8Bit(proc.readAll());
        if (!viewDir.isEmpty()) QDir(viewDir).removeRecursively();
        QFile logFile(cache + "/acts.log");
        if (logFile.open(QIODevice::WriteOnly)) logFile.write(log.toUtf8());
        json = findMapEntsJson(cache);
        if (proc.exitCode() != 0 || json.isEmpty()) {
            if (err) *err = "ACTS did not produce map_ents for " + name + " (exit " + QString::number(proc.exitCode()) + ").\n\n" + log.right(1500);
            return false;
        }
    }
    loadDocument(json);
    stockZone_ = name;
    stockSpawnsEditable_ = false;
    // The map's own world and props come with it (research 84: opening a stock map loaded the entities only, so the
    // scene looked empty; the world was drawn only after View > Stock Map Reference). Setting
    // stock/referenceOnOpen = false keeps the old behaviour.
    if (Settings::instance().value("stock/referenceOnOpen", true).toBool() && ctx_.doc.map().entities.size() > 0) loadStockReference(name);
    return ctx_.doc.map().entities.size() > 0;
}

void MainWindow::saveSelectionSet(const QString& name) {
    selectionSets_[name] = ctx_.doc.selection();
    if (selectionSetsMenu_) {
        selectionSetsMenu_->clear();
        for (const auto& [n, refs] : selectionSets_)
            selectionSetsMenu_->addAction(QString("%1 (%2)").arg(n).arg(refs.size()), this, [this, n = n] { restoreSelectionSet(n); });
    }
    ctx_.setStatus(QString("Selection set '%1': %2 item(s)").arg(name).arg(ctx_.doc.selection().size()));
}

bool MainWindow::restoreSelectionSet(const QString& name) {
    const auto it = selectionSets_.find(name);
    if (it == selectionSets_.end()) return false;
    ctx_.doc.deselectAll();
    size_t kept = 0;
    for (const SelRef& r : it->second)
        if (ctx_.doc.findEntity(r.entity)) {  // items deleted since are skipped
            ctx_.doc.select(r, true);
            ++kept;
        }
    ctx_.documentEdited(QString("Restored selection set '%1' (%2 item(s))").arg(name).arg(kept));
    return kept > 0;
}

bool MainWindow::editStockSpawns() {
    if (stockZone_.isEmpty() || ctx_.scene.spawns.empty()) {
        ctx_.setStatus(stockSpawnsEditable_ ? "The compiled spawns are already entities of the document"
                                            : "Open a stock map first (File > Open IW8 Stock Map)");
        return false;
    }
    ctx_.doc.beginGroup("Edit Compiled Spawns");
    for (const auto& s : ctx_.scene.spawns) ctx_.doc.addEntity(spawnEntityKeys(s), {}, "Spawn " + s.name);
    ctx_.doc.endGroup();
    const size_t n = ctx_.scene.spawns.size();
    ctx_.scene.spawns.clear();  // the overlay is now the entities themselves
    stockSpawnsEditable_ = true;
    ctx_.documentEdited(QString("%1 compiled spawns are now entities: move, add or delete them; Rebuild Stock Zone writes them "
                                "back as the spawnList").arg(n));
    return true;
}

bool MainWindow::prepareStockRebuild(const QString& work, QStringList* args, QString* err) {
    if (stockZone_.isEmpty()) {
        if (err) *err = "Open a stock map first (File > Open IW8 Stock Map).";
        return false;
    }
    QString script;
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4 && script.isEmpty(); ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("tools/m2_rebuild_mapents.ps1"))) script = d.filePath("tools/m2_rebuild_mapents.ps1");
    if (script.isEmpty()) {
        if (err) *err = "tools/m2_rebuild_mapents.ps1 not found beside the editor.";
        return false;
    }
    std::string ents, serr;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(ctx_.doc.map(), ents, spawns, &serr);
    if (!serr.empty()) EditorLog::instance().add(LogLevel::Warning, "stock", qs(serr).trimmed());
    QDir().mkpath(work);
    QFile ef(work + "/edit.ents");
    if (!ef.open(QIODevice::WriteOnly) || ef.write(ents.data(), qint64(ents.size())) != qint64(ents.size())) {
        if (err) *err = "Cannot write " + ef.fileName();
        return false;
    }
    ef.close();
    *args = {"-ExecutionPolicy", "Bypass", "-File", script, "-Zone", stockZone_, "-Ents", work + "/edit.ents", "-Work", work,
             "-Game", toolPath("replay").isEmpty() ? QString() : QFileInfo(toolPath("replay")).absolutePath(),
             "-ZoneTool", toolPath("zonetool"), "-Acts", toolPath("acts")};
    if (stockSpawnsEditable_) {
        if (spawns.empty()) {
            if (err) *err = "Every spawn was deleted: the spawnList cannot be written empty (docs/research/26).";
            return false;
        }
        const std::string text = spawnsToEntityText(spawns);
        QFile sf(work + "/edit.spawns.ents");
        if (!sf.open(QIODevice::WriteOnly) || sf.write(text.data(), qint64(text.size())) != qint64(text.size())) {
            if (err) *err = "Cannot write " + sf.fileName();
            return false;
        }
        *args << "-Spawns" << work + "/edit.spawns.ents";
    }
    return true;
}
bool MainWindow::save() {
    if (ctx_.doc.path().empty()) return saveAsDialog(false);
    std::string err;
    if (!ctx_.doc.save(&err)) {
        QMessageBox::critical(this, "Save", qs(err));
        return false;
    }
    ctx_.documentEdited("Saved " + qs(ctx_.doc.path()));
    return true;
}

bool MainWindow::saveTo(const QString& path, QString* err) {
    std::string e;
    if (!ctx_.doc.saveAs(path.toStdString(), &e)) {
        if (err) *err = qs(e);
        return false;
    }
    displayName_ = QFileInfo(path).fileName();
    ctx_.documentEdited("Saved " + path);
    return true;
}

bool MainWindow::saveAsDialog(bool copyOnly) {
    const QString start = ctx_.doc.path().empty() ? Settings::instance().value("lastDir").toString() : qs(ctx_.doc.path());
    const QString path = QFileDialog::getSaveFileName(this, copyOnly ? "Save a Copy" : "Save As", start,
                                                      "Map source (*.map);;IW8 entity string (*.ents);;All files (*)");
    if (path.isEmpty()) return false;
    std::string err;
    if (copyOnly) {
        if (!ctx_.doc.saveCopy(path.toStdString(), &err)) {
            QMessageBox::critical(this, "Save a Copy", qs(err));
            return false;
        }
        ctx_.setStatus("Saved copy " + path);
        return true;
    }
    QString e;
    if (!saveTo(path, &e)) {
        QMessageBox::critical(this, "Save As", e);
        return false;
    }
    Settings::instance().addRecentFile(path);
    rebuildRecentMenu();
    return true;
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList recent = Settings::instance().recentFiles();
    recentMenu_->setEnabled(!recent.isEmpty());
    for (const QString& r : recent) {
        QAction* a = recentMenu_->addAction(r);
        connect(a, &QAction::triggered, this, [this, r] { openPath(r); });
    }
}

void MainWindow::updateTitle() {
    setWindowTitle(QString("IW8 Radiant Suite - %1%2  [MW2019 Replay 1.20.4.7623265]%3")
                       .arg(displayName_, ctx_.doc.modified() ? " *" : "", qEnvironmentVariableIsSet("IW8R_SAFE_MODE") ? "  (Safe Mode)" : ""));
}

void MainWindow::updateStatus() {
    gridLabel_->setText(QString("Grid %1%2").arg(qs(formatNumber(ctx_.doc.grid())), ctx_.snap ? "" : " (snap off)"));
    selLabel_->setText(ctx_.doc.faceSelection().empty() ? QString("%1 selected").arg(ctx_.doc.selection().size())
                                                        : QString("%1 faces selected").arg(ctx_.doc.faceSelection().size()));
    entLabel_->setText(QString("%1 entities").arg(ctx_.doc.map().entities.size()));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }
    Settings::instance().setValue("window/geometry", saveGeometry());
    Settings::instance().setValue("window/docks", dock_->saveState(kDockLayoutVersion));
    Settings::instance().setValue("camera/speed", ctx_.cam.speed);
    Settings::instance().setValue("camera/fov", ctx_.cam.fov);
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.front().isLocalFile()) openPath(urls.front().toLocalFile());
}


void MainWindow::rebuildReferenceProps() {
    if (!refProps_ || !refBaseTris_ || referenceZone_.isEmpty()) return;
    // Boxes are instanced unit cubes (research 84): one mesh per kind, one transform row set per placement. They
    // used to be added to the flat reference triangle list, which the camera does not draw while the textured
    // reference is set, so no box was ever seen.
    //   box      a cached model whose mesh cannot be drawn (unreadable, or past the triangle budget): its bounds, tan
    //   waiting  a model that is not in the cache yet: a small amber marker at the placement
    //   failed   a model the cache could not get: the same marker in red
    size_t drawn = 0, waiting = 0, boxed = 0, failedPlacements = 0;
    std::map<std::string, size_t> uncached;
    const auto unitCube = [](const uint32_t shades[6]) {  // corners 0..1 on every axis
        std::vector<app::GpuVertex> out;
        static const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
        for (int f = 0; f < 6; ++f)
            for (const int t : {0, 1, 2, 0, 2, 3}) {
                const int k = faces[f][t];
                out.push_back({float(k & 1 ? 1 : 0), float(k & 2 ? 1 : 0), float(k & 4 ? 1 : 0), shades[f]});
            }
        return out;
    };
    // Rows that take a cube corner (x, y, z) to origin + axis0 * (x * sx) + axis1 * (y * sy) + axis2 * (z * sz).
    const auto cubeRows = [](std::vector<float>& rows, const StockModelInstance& in, const Vec3& origin, double sx, double sy, double sz) {
        for (int r = 0; r < 3; ++r) {
            const auto comp = [r](const Vec3& v) { return r == 0 ? v.x : r == 1 ? v.y : v.z; };
            rows.push_back(float(comp(in.axis[0]) * sx));
            rows.push_back(float(comp(in.axis[1]) * sy));
            rows.push_back(float(comp(in.axis[2]) * sz));
            rows.push_back(float(comp(origin)));
        }
    };
    const uint32_t boxShades[6] = {0xff5a6e82u, 0xff8fa6c2u, 0xff7890aau, 0xff7890aau, 0xff6c829cu, 0xff6c829cu};
    const uint32_t waitShades[6] = {0xff1c6ea8u, 0xff40c0ffu, 0xff30a0e6u, 0xff30a0e6u, 0xff2888c8u, 0xff2888c8u};
    const uint32_t failShades[6] = {0xff281c96u, 0xff5040f0u, 0xff4030d2u, 0xff4030d2u, 0xff3426b4u, 0xff3426b4u};
    app::InstancedMesh boxMesh, waitMesh, failMesh;
    const auto box = [&](const StockModelInstance& in, const std::array<double, 6>& b) {
        const double s = in.scale;
        cubeRows(boxMesh.rows, in, in.origin + in.axis[0] * (b[0] * s) + in.axis[1] * (b[1] * s) + in.axis[2] * (b[2] * s),
                 (b[3] - b[0]) * s, (b[4] - b[1]) * s, (b[5] - b[2]) * s);
        ++boxed;
    };
    // Markers are editor-only drawing. The camera draws every reference instance into the sun shadow map of the lit
    // preview, and this function may not change the renderer, so no marker is made while the lit preview is on (the
    // status-bar label still says what is missing); loadStockReference redraws the props when the preview is toggled.
    constexpr double kMarker = 24.0;  // marker edge in units, resting on the placement's origin
    const bool markers = !ctx_.scene.litPreview;
    const auto marker = [&](app::InstancedMesh& mesh, const StockModelInstance& in) {
        if (markers)
            cubeRows(mesh.rows, in, in.origin - in.axis[0] * (kMarker / 2) - in.axis[1] * (kMarker / 2), kMarker, kMarker, kMarker);
    };
    // 3.19 / 11.3 / 3.20: a cached model draws its real LOD0 mesh (shaded by facing in model space) as an
    // instanced mesh: each model uploaded once, drawn per placement on the GPU. A budget on the distinct models'
    // triangles; past it, and for models whose mesh cannot be read, the box.
    // Each material of a model is its own mesh: with a material and texture coordinates it is drawn textured (the
    // camera loads the stock texture); without them it joins the model's plain shaded mesh.
    struct ModelMeshes {
        int plain = -1;        // index into instanced (-1: none)
        std::vector<int> tex;  // indices into texInstanced
        bool any = false;
    };
    std::map<std::string, ModelMeshes> meshIndex;
    std::vector<app::InstancedMesh> instanced;
    std::vector<app::InstancedTexMesh> texInstanced;
    std::vector<std::string> texMaterials;
    size_t meshTris = 0, meshed = 0;
    constexpr size_t kPropTriangleBudget = 4'000'000;
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    const auto mesh = [&](const StockModelInstance& in) -> bool {
        auto it = meshIndex.find(in.model);
        if (it == meshIndex.end()) {
            ModelMeshes mm;
            // A model is read and converted once and kept while models are still arriving (refPropBuilt_, dropped
            // when the fetch has ended). A pass after every batch used to read every cached GLB again: about four
            // seconds on the UI thread for the 500 models of mp_rust, during which no ACTS answer was handled.
            std::shared_ptr<const RefPropMesh> built;
            if (const auto kept = refPropBuilt_.find(in.model); kept != refPropBuilt_.end()) {
                built = kept->second;
            } else if (auto im = modelCacheStock_->loadModel(qs(in.model))) {
                auto fresh = std::make_shared<RefPropMesh>();
                for (const auto& ms : im->meshes) fresh->tris += ms.idx.size() / 3;
                {
                    std::vector<app::GpuVertex>& plain = fresh->plain;
                    const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up
                    for (const auto& ms : im->meshes) {
                        const bool textured = !ms.material.empty() && ms.uv.size() == ms.pos.size();
                        std::vector<app::GpuTexVertex> texTris;
                        for (size_t i = 0; i + 2 < ms.idx.size(); i += 3) {
                            const Vec3 a = zup(ms.pos[ms.idx[i]]), b = zup(ms.pos[ms.idx[i + 1]]), c = zup(ms.pos[ms.idx[i + 2]]);
                            const double k = 0.55 + 0.45 * std::fabs(dot(normalize(cross(b - a, c - a)), light));
                            if (textured) {
                                const uint32_t g = uint32_t(std::clamp(k * 255.0, 0.0, 255.0));
                                const uint32_t col = 0xff000000u | (g << 16) | (g << 8) | g;
                                const Vec3* v[3] = {&a, &b, &c};
                                for (int n = 0; n < 3; ++n) {
                                    const auto& uv = ms.uv[ms.idx[i + size_t(n)]];
                                    app::GpuTexVertex tv;
                                    tv.x = float(v[n]->x);
                                    tv.y = float(v[n]->y);
                                    tv.z = float(v[n]->z);
                                    tv.u = float(uv[0]);
                                    tv.v = float(uv[1]);
                                    tv.rgba = col;
                                    // The model's own vertex normals (tree cards bend theirs outward from the crown): Game
                                    // Lighting shades with them (vs_tex_inst turns them by the instance).
                                    if (ms.normal.size() == ms.pos.size()) {
                                        const Vec3 nn = zup(ms.normal[ms.idx[i + size_t(n)]]);
                                        if (length(nn) > 1e-6) {
                                            const Vec3 un = normalize(nn);
                                            tv.nx = float(un.x), tv.ny = float(un.y), tv.nz = float(un.z);
                                        }
                                    }
                                    texTris.push_back(tv);
                                }
                            } else {
                                const uint32_t g = uint32_t(std::clamp(k * 175.0, 0.0, 255.0));
                                const uint32_t col = 0xff000000u | (uint32_t(g * 0.92) << 16) | (g << 8) | uint32_t(g * 0.85);
                                for (const Vec3* v : {&a, &b, &c}) plain.push_back({float(v->x), float(v->y), float(v->z), col});
                            }
                        }
                        if (!texTris.empty()) fresh->tex.push_back({ms.material, std::move(texTris)});
                    }
                }
                built = fresh;
                if (refPropBuiltTris_ + fresh->tris <= kPropTriangleBudget) {  // kept copies stay within the draw budget
                    refPropBuiltTris_ += fresh->tris;
                    refPropBuilt_[in.model] = fresh;
                }
            }
            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {
                meshTris += built->tris;
                for (const auto& [material, tris] : built->tex) {
                    app::InstancedTexMesh tm;
                    tm.tris = tris;
                    mm.tex.push_back(int(texInstanced.size()));
                    texInstanced.push_back(std::move(tm));
                    texMaterials.push_back(material);
                }
                if (!built->plain.empty()) {
                    app::InstancedMesh pm;
                    pm.tris = built->plain;
                    mm.plain = int(instanced.size());
                    instanced.push_back(std::move(pm));
                }
                mm.any = mm.plain >= 0 || !mm.tex.empty();
            }
            it = meshIndex.emplace(in.model, std::move(mm)).first;
        }
        if (!it->second.any) return false;
        if (meshed == 0) referencePropSample_ = in.origin;
        // world_i = axis0_i * s * x + axis1_i * s * y + axis2_i * s * z + origin_i
        const double sc = in.scale;
        float rows[12];
        for (int r = 0; r < 3; ++r) {
            const auto comp = [r](const Vec3& v) { return r == 0 ? v.x : r == 1 ? v.y : v.z; };
            rows[r * 4 + 0] = float(comp(in.axis[0]) * sc);
            rows[r * 4 + 1] = float(comp(in.axis[1]) * sc);
            rows[r * 4 + 2] = float(comp(in.axis[2]) * sc);
            rows[r * 4 + 3] = float(comp(in.origin));
        }
        if (it->second.plain >= 0) {
            auto& dst = instanced[size_t(it->second.plain)].rows;
            dst.insert(dst.end(), rows, rows + 12);
        }
        for (const int ti : it->second.tex) {
            auto& dst = texInstanced[size_t(ti)].rows;
            dst.insert(dst.end(), rows, rows + 12);
        }
        ++meshed;
        return true;
    };
    referenceProps_.clear();  // boxes are instances now (above); the flat list is the world alone (loadStockReference)
    const std::vector<uint32_t> hiddenProps = stockEditHiddenProps(ctx_.doc.map());  // 11.5: hidden in the rebuilt map
    for (const StockModelInstance& in : *refProps_) {
        if (std::binary_search(hiddenProps.begin(), hiddenProps.end(), in.index)) continue;
        const auto e = modelCacheStock_ ? modelCacheStock_->lookup(qs(in.model)) : std::nullopt;
        if (e && !e->failed) {
            if (!mesh(in)) box(in, e->bounds);
            ++drawn;
        } else if (!e) {
            ++uncached[in.model];
            ++waiting;
            marker(waitMesh, in);
        } else {
            ++failedPlacements;
            marker(failMesh, in);
        }
    }
    refPropPlaceholders_ = (waitMesh.rows.size() + failMesh.rows.size()) / 12;
    for (auto [m, shades] : {std::pair{&boxMesh, boxShades}, std::pair{&waitMesh, waitShades}, std::pair{&failMesh, failShades}})
        if (!m->rows.empty()) {
            m->tris = unitCube(shades);
            m->editorMarker = m != &boxMesh;  // amber / red markers: on screen only, never in captures or shadows
            instanced.push_back(std::move(*m));
        }
    // Every missing model is asked for at once, most used first. The model cache works the list off in batches on
    // its own and reports each batch (batchEnded: loadStockReference redraws the props then), so the chain does not
    // depend on this function being called again. The guard says whether to ask, to wait, or that the cache has
    // given up (a missing tool): then the label says so instead of promising models that will not come.
    // (Owner playtests 2026-11-11: only 40, later 150, were asked for per pass.)
    const PropFetchStep step = refPropChain_.step(uncached.size(), modelCacheStock_ && modelCacheStock_->busy());
    if (step == PropFetchStep::Request && modelCacheStock_) {
        if (!refPropClock_.isValid()) refPropClock_.start();
        refPropFetchMs_ = -1;
        QStringList want;
        for (const std::string& m : propFetchOrder(uncached)) want << qs(m);
        modelCacheStock_->request(want);
    } else if (step == PropFetchStep::Done || step == PropFetchStep::Stalled) {
        if (refPropClock_.isValid() && refPropFetchMs_ < 0) refPropFetchMs_ = refPropClock_.elapsed();
        // No more passes are expected: the kept copies of the converted models are given back. They stay only
        // while markers remain, because toggling the lit preview then redraws the props (loadStockReference).
        if (failedPlacements == 0 && waiting == 0) {
            refPropBuilt_.clear();
            refPropBuiltTris_ = 0;
        }
    }
    camera_->setReferenceInstances(std::move(instanced));
    camera_->setReferenceTexInstances(std::move(texInstanced), std::move(texMaterials));
    referencePropMeshes_ = meshed;
    updateStockPropLabel();
    ctx_.setStatus(QString("Stock map reference %1: %2 triangles, %3 of %4 props drawn (%5 as their meshes, %6 as boxes)%7 (read-only, camera view)")
                             .arg(referenceZone_).arg(refWorldTris_).arg(drawn).arg(refProps_->size()).arg(meshed).arg(boxed)
                             .arg(waiting || failedPlacements
                                      ? QString("; %1 placements wait for their model, %2 have a model that failed (%3)")
                                            .arg(waiting).arg(failedPlacements)
                                            .arg(markers ? "amber and red markers" : "markers hidden in the lit preview")
                                      : QString()));
}

// The stock reference's props by state, and the line that the status bar shows for them (research 84).
MainWindow::StockPropStatus MainWindow::stockPropStatus() const {
    StockPropStatus s;
    s.zone = referenceZone_;
    s.loaded = refProps_ && refBaseTris_ && !referenceZone_.isEmpty() && refPropZone_ == referenceZone_;
    if (!s.loaded) return s;
    s.worldTriangles = refWorldTris_;
    s.placeholders = refPropPlaceholders_;
    std::vector<std::string> errors;
    for (const auto& [model, uses] : refPropUse_) {
        ++s.counts.models;
        s.counts.placements += uses;
        const auto e = modelCacheStock_ ? modelCacheStock_->lookup(qs(model)) : std::nullopt;
        if (!e) {
            ++s.counts.missing;
            s.counts.placementsMissing += uses;
        } else if (e->failed) {
            ++s.counts.failed;
            s.counts.placementsFailed += uses;
            s.failures.push_back({qs(model), e->error});
            errors.push_back(e->error.toStdString());
        } else {
            ++s.counts.ready;
            s.counts.placementsReady += uses;
        }
    }
    s.stalled = s.counts.missing > 0 && refPropChain_.stalled();
    s.fetching = s.counts.missing > 0 && !s.stalled;
    if (s.stalled) s.stallReason = modelCacheStock_ ? modelCacheStock_->problem() : QString("no model cache");
    s.fetchMs = refPropFetchMs_ >= 0 ? refPropFetchMs_ : refPropClock_.isValid() ? refPropClock_.elapsed() : -1;
    s.line = qs(propStatusLine(s.counts, s.fetching, s.stalled, s.stallReason.toStdString(), propFailureReasons(errors)));
    return s;
}

// The fixed status-bar label for the stock reference's props. It stays while a reference is shown: amber while
// models are fetched, red when some failed or the fetch stopped, plain when all are there. The tooltip lists the
// failed models; they are also written to the Console once.
void MainWindow::updateStockPropLabel() {
    if (!refPropLabel_) return;
    const StockPropStatus s = stockPropStatus();
    if (!s.loaded) {
        refPropLabel_->hide();
        return;
    }
    QString text = s.line;
    if (s.fetching && s.fetchMs >= 0) text += QString(", %1 s").arg(s.fetchMs / 1000);
    else if (!s.fetching && !s.stalled && s.fetchMs >= 0) text += QString(" (fetched in %1 s)").arg(s.fetchMs / 1000);
    QString tip = "Props of the stock map " + s.zone + QString(": %1 placements of %2 models.").arg(s.counts.placements).arg(s.counts.models) +
                  "\nIn the camera, a small amber box marks a placement whose model is still being fetched and a red box one whose "
                  "model failed.\nThe boxes are not drawn while the lit preview is on.";
    if (!s.failures.empty()) {
        tip += QString("\n\nFailed models (%1):").arg(s.failures.size());
        for (size_t i = 0; i < s.failures.size() && i < 25; ++i) tip += "\n  " + s.failures[i].first + ": " + s.failures[i].second.left(140);
        if (s.failures.size() > 25) tip += QString("\n  and %1 more (see the Console)").arg(s.failures.size() - 25);
    }
    const QString style = s.fetching ? "color: #ffc040; padding: 0 6px;"
                          : s.stalled || s.counts.failed ? "color: #ff7060; padding: 0 6px;" : "padding: 0 6px;";
    if (refPropLabel_->text() != text) refPropLabel_->setText(text);
    if (refPropLabel_->toolTip() != tip) refPropLabel_->setToolTip(tip);
    if (refPropLabel_->styleSheet() != style) refPropLabel_->setStyleSheet(style);
    refPropLabel_->show();
    if (!s.fetching && !refPropFailuresLogged_ && (s.stalled || !s.failures.empty())) {
        refPropFailuresLogged_ = true;
        EditorLog::instance().add(LogLevel::Warning, "stock", s.line);
        for (const auto& [model, error] : s.failures) EditorLog::instance().add(LogLevel::Warning, "stock", "prop model " + model + ": " + error);
    }
}

void MainWindow::loadStockLightmap(const QString& zone) {
    if (!worldCache_) return;
    // MP maps: one lightmap (lightmap.iw8rlm); Warzone maps: lightmaps/<index>.iw8rlm, more as streamed tiles arrive.
    std::vector<std::string> paths;
    if (const std::optional<QString> file = worldCache_->lightmapFile(zone)) paths.push_back(file->toStdString());
    for (const int idx : worldCache_->lightmapIndices(zone))
        if (const std::optional<QString> f = worldCache_->lightmapFile(zone, idx)) paths.push_back(f->toStdString());
    if (paths.empty()) return;
    QPointer<MainWindow> self(this);
    std::thread([self, zone, paths] {
        std::vector<StockLightmap> lms;
        std::string err;
        for (const std::string& path : paths) {
            StockLightmap lm;
            std::string e;
            if (readStockLightmapCache(path, lm, &e)) lms.push_back(std::move(lm));
            else err = e;
        }
        std::shared_ptr<const app::StockLightmapLayers> layers;
        if (!lms.empty()) layers = app::stockLightmapAtlas(lms);
        QMetaObject::invokeMethod(qApp, [self, zone, layers, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (layers && layers->width > 0) {
                self->camera_->setStockLightmap(layers);
                self->ctx_.setStatus(QString("Stock lightmap of %1: %2 x %3 (View > Game Lighting uses it)").arg(zone).arg(layers->width).arg(layers->height));
            } else {
                self->ctx_.setStatus("Stock lightmap of " + zone + " not read: " + QString::fromStdString(err));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

// A stock map's decal volumes (research 99): read from its world export, their materials and masks exported once with
// ACTS (beside the world cache), projected on the world and appended as blended reference batches.
void appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out) {
    StockDecalVolumeSet set;
    std::string err;
    if (!readStockDecalVolumes(zoneDir.toStdString(), set, &err) || set.volumes.empty()) return;
    // Materials and masks (pools decalvolumematerial / decalvolumemask), cached in <zoneDir>/decalvol.
    const QString dvDir = zoneDir + "/decalvol";
    const auto readAssets = [&](const char* pool, std::map<std::string, std::string>& texts) {
        for (QDirIterator it(dvDir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QString f = it.next();
            if (!f.contains(QString("/assets/") + pool + "/")) continue;
            std::string t;
            if (readFile(f.toStdString(), t)) texts[f.toStdString()] = t;
        }
    };
    std::map<std::string, std::string> matTexts, maskTexts;
    readAssets("decalvolumematerial", matTexts);
    readAssets("decalvolumemask", maskTexts);
    if (matTexts.empty() && QFileInfo::exists(tools.acts)) {
        QStringList names;
        for (const auto& m : set.materials)
            if (!m.empty()) names << QString::fromStdString(m);
        for (const auto& m : set.masks)
            if (!m.empty()) names << QString::fromStdString(m);
        const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
        for (const QString& ff : {zoneFolder + "techsets_" + zone + ".ff", zoneFolder + zone + ".ff"}) {
            if (!QFileInfo::exists(ff)) continue;
            QProcess p;
            p.start(tools.acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", tools.replay, "--oodle", tools.oodle, "-a",
                                 "decalvolumematerial,decalvolumemask", "-n", names.join(','), "-l", "-o", dvDir, ff});
            p.waitForFinished(180000);
        }
        readAssets("decalvolumematerial", matTexts);
        readAssets("decalvolumemask", maskTexts);
    }
    std::map<std::string, StockDecalMaterial> mats;
    for (const auto& [f, t] : matTexts) {
        StockDecalMaterial m;
        if (parseStockDecalMaterial(t, m)) mats[m.name] = m;
    }
    std::map<std::string, std::string> maskImage;
    for (const auto& [f, t] : maskTexts) {
        std::string name, image;
        if (parseStockDecalMask(t, name, image)) maskImage[name] = image;
    }
    // Receivers: the world without its sky and shadow-only caulk.
    std::vector<bool> keep(world.meshes.size(), true);
    for (size_t m = 0; m < world.meshes.size(); ++m) {
        std::string lm = world.meshes[m].material;
        for (char& c : lm) c = char(std::tolower(static_cast<unsigned char>(c)));
        const std::string leaf = lm.substr(lm.rfind('/') == std::string::npos ? 0 : lm.rfind('/') + 1);
        if (leaf == "sky" || leaf.rfind("sky_", 0) == 0 || lm.find("caulk") != std::string::npos) keep[m] = false;
    }
    StockDecalReceivers receivers;
    receivers.addWorld(world, keep);
    receivers.build();
    const std::vector<StockDecalBatch> batches = projectDecalVolumes(receivers, set);
    int order = 0;
    for (const StockDecalBatch& b : batches) {
        if (b.volume >= set.volumes.size() || b.idx.empty()) continue;
        const StockDecalVolume& vol = set.volumes[b.volume];
        const auto mi = mats.find(vol.material);
        if (mi == mats.end() || mi->second.channels[0].empty()) continue;
        const StockDecalShading sh = stockDecalShading(vol, mi->second);
        app::ReferenceTexBatch rb;
        rb.material = "\x05" + mi->second.channels[0];
        rb.decalVolume = true;
        if (const auto mk = maskImage.find(vol.mask); mk != maskImage.end()) rb.decalMask = mk->second;
        rb.decalA = float(sh.opacityScale), rb.decalB = float(sh.opacityBias);
        rb.decalTint[0] = float(sh.tint.x), rb.decalTint[1] = float(sh.tint.y), rb.decalTint[2] = float(sh.tint.z);
        rb.decalOrder = order++;
        for (uint32_t i : b.idx) {
            const StockDecalVertex& v = b.verts[i];
            app::GpuTexVertex g{float(v.pos.x), float(v.pos.y), float(v.pos.z), v.mu, v.mv, 0xFFFFFFFFu};
            g.nx = float(v.normal.x), g.ny = float(v.normal.y), g.nz = float(v.normal.z);
            g.tx = v.u, g.ty = v.v, g.tz = v.fade;
            if (v.lightmap >= 0 && v.lmU >= 0) g.lu = float(v.lightmap) + std::min(v.lmU, 0.99994f), g.lv = v.lmV;
            rb.tris.push_back(g);
        }
        out.push_back(std::move(rb));
    }
}

void MainWindow::loadStockSun(const QString& zone, const QString& skyMaterialIn) {
    const QString skyMaterial = skyMaterialIn.isEmpty() ? "tw/sky_" + zone : skyMaterialIn;
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    const QString ff = QFileInfo(replay).absolutePath() + "/zone/" + zone + ".ff";
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache/" + zone + "/com_map";
    QPointer<MainWindow> self(this);
    std::thread([self, zone, acts, replay, oodle, ff, dir, skyMaterial] {
        const auto findJson = [&] {
            for (QDirIterator it(dir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString f = it.next();
                if (f.contains("/assets/com_map/")) return f;
            }
            return QString();
        };
        QString json = findJson();
        if (json.isEmpty() && QFileInfo::exists(acts) && QFileInfo::exists(ff)) {
            QProcess p;
            p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "com_map", "-l", "-o", dir, ff});
            p.waitForFinished(120000);
            json = findJson();
        }
        // The vision (fog, atmosphere) beside it: vision/<zone>.vision as a rawfile.
        const QString visDir = QFileInfo(dir).absolutePath() + "/vision";
        const auto findVision = [&] {
            for (QDirIterator it(visDir, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString f = it.next();
                if (f.contains("/rawfile/vision/")) return f;
            }
            return QString();
        };
        QString vision = findVision();
        if (vision.isEmpty() && QFileInfo::exists(acts) && QFileInfo::exists(ff)) {
            QProcess p;
            p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "rawfile", "-n",
                           "vision/" + zone + ".vision", "-l", "-o", visDir, ff});
            p.waitForFinished(120000);
            vision = findVision();
        }
        std::string visionText;
        if (QFile vf(vision); !vision.isEmpty() && vf.open(QIODevice::ReadOnly)) visionText = vf.readAll().toStdString();
        // The vision's grading CLUT (field NONMKTKNNM, image "gradingclut<name>", a 3D texture in the map zone).
        std::shared_ptr<GameGradingClut> grade;
        {
            std::string clutName;
            for (size_t pos = visionText.find("NONMKTKNNM"); pos != std::string::npos && clutName.empty();
                 pos = visionText.find("NONMKTKNNM", pos + 1)) {
                const size_t q1 = visionText.find('"', pos), q2 = q1 == std::string::npos ? q1 : visionText.find('"', q1 + 1);
                const size_t eol = visionText.find('\n', pos);
                if (q2 != std::string::npos && (eol == std::string::npos || q2 < eol)) clutName = visionText.substr(q1 + 1, q2 - q1 - 1);
            }
            if (!clutName.empty()) {
                const QString image = "gradingclut" + QString::fromStdString(clutName);
                const QString clutDir = QFileInfo(dir).absolutePath() + "/grading";
                const auto findDds = [&] {
                    for (QDirIterator it(clutDir, {"*.dds"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                        const QString f = it.next();
                        if (QFileInfo(f).fileName().startsWith(image + ".")) return f;
                    }
                    return QString();
                };
                QString dds = findDds();
                const QString techsets = QFileInfo(ff).absolutePath() + "/techsets_" + zone + ".ff";
                if (dds.isEmpty() && QFileInfo::exists(acts))
                    for (const QString& z : {ff, techsets}) {
                        if (!QFileInfo::exists(z)) continue;
                        QProcess p;
                        p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "image", "-n",
                                       image, "-l", "-o", clutDir, z});
                        p.waitForFinished(120000);
                        dds = findDds();
                        if (!dds.isEmpty()) break;
                    }
                std::string bytes;
                auto g = std::make_shared<GameGradingClut>();
                if (!dds.isEmpty() && readFile(dds.toStdString(), bytes) && gameGradingClutFromDds(bytes, *g)) grade = g;
            }
        }
        // The sky material's first pixel constant scales its image (tw/sky_<map>, in techsets_<map>.ff: 39.1 on
        // mp_m_speedball): the stock sky image is stored that many times darker than the game draws it.
        double skyIntensity = 0;
        {
            const QString skyDir = QFileInfo(dir).absolutePath() + "/sky_material";
            const auto findMat = [&] {
                for (QDirIterator it(skyDir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                    const QString f = it.next();
                    if (f.contains("/assets/material/")) return f;
                }
                return QString();
            };
            QString mj = findMat();
            const QString techsets = QFileInfo(ff).absolutePath() + "/techsets_" + zone + ".ff";
            if (mj.isEmpty() && QFileInfo::exists(acts))
                for (const QString& z : {techsets, ff}) {
                    if (!QFileInfo::exists(z)) continue;
                    QProcess p;
                    p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "material", "-n",
                                   skyMaterial, "-l", "-o", skyDir, z});
                    p.waitForFinished(120000);
                    mj = findMat();
                    if (!mj.isEmpty()) break;
                }
            QFile mf(mj);
            if (!mj.isEmpty() && mf.open(QIODevice::ReadOnly)) {
                const QJsonArray vals = QJsonDocument::fromJson(mf.readAll())
                                            .object()["asset"].toObject()["fields"].toObject()["constantBufferTable"].toObject()["values"].toArray();
                const QString hex = vals.isEmpty() ? QString() : vals[0].toObject()["psData"].toObject()["bytes"].toString();
                const QByteArray b = QByteArray::fromHex(hex.left(8).toLatin1());
                if (b.size() == 4) {
                    float v = 0;
                    std::memcpy(&v, b.constData(), 4);
                    if (std::isfinite(v) && v > 0 && v < 1e5) skyIntensity = v;
                }
            }
        }
        double intensity = 0;
        Vec3 color{1, 1, 1}, toward{0, 0, 1};
        QFile f(json);
        if (!json.isEmpty() && f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonArray lights = root["asset"].toObject()["fields"].toObject()["primaryLights"].toObject()["values"].toArray();
            for (const QJsonValue& lv : lights) {
                const QJsonObject l = lv.toObject();
                if (l["type"].toInt() != 1) continue;  // the sun
                intensity = l["intensity"].toDouble();
                const QJsonArray c = l["colorLinearSrgb"].toObject()["v"].toArray(), d = l["dir"].toObject()["v"].toArray();
                if (c.size() == 3) color = Vec3{c[0].toDouble(), c[1].toDouble(), c[2].toDouble()};
                if (d.size() == 3) toward = normalize(Vec3{d[0].toDouble(), d[1].toDouble(), d[2].toDouble()});
                break;
            }
        }
        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade] {
            if (!self || self->referenceZone_ != zone) return;
            if (grade) self->camera_->setGameStockGrading(grade);
            if (!visionText.empty()) self->camera_->setGameStockVision(visionText);
            if (skyIntensity > 0) self->camera_->setGameStockSkyIntensity(skyIntensity);
            if (intensity > 0) self->camera_->setGameStockSun(intensity, color, toward);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockProbes(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->probesFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const std::string path = file->toStdString();
    std::thread([self, zone, path] {
        auto set = std::make_shared<StockProbeSet>();
        std::string err;
        const bool ok = readStockProbeCache(path, *set, &err, false);  // the GPU takes the BC6H blocks as they are
        QMetaObject::invokeMethod(qApp, [self, zone, set, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (ok) self->camera_->setStockProbes(set);
            else self->ctx_.setStatus("Stock reflection probes of " + zone + " not read: " + QString::fromStdString(err));
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockReference(const QString& zone) {
    if (!worldCache_) {
        worldCache_ = new WorldCache([this](const QString& k) { return toolPath(k); }, this);
        connect(worldCache_, &WorldCache::progress, this, [this](const QString& m) { ctx_.setStatus(m); });
        connect(worldCache_, &WorldCache::ready, this, [this](const QString& z) {
            if (z == referenceZone_) loadStockReference(z);
        });
        connect(worldCache_, &WorldCache::probesReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;
            if (worldCache_->probesFile(z)) loadStockProbes(z);
            else if (!worldCache_->probesErrorOf(z).isEmpty()) ctx_.setStatus("Stock reflection probes of " + z + ": " + worldCache_->probesErrorOf(z));
        });
        connect(worldCache_, &WorldCache::lightmapReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;
            if (worldCache_->lightmapFile(z) || !worldCache_->lightmapIndices(z).isEmpty()) loadStockLightmap(z);
            else ctx_.setStatus("Stock lightmap of " + z + ": " + worldCache_->lightmapErrorOf(z));
        });
    }
    referenceZone_ = zone;
    // Props the model catalogue does not list are looked for in the map's own zone.
    if (modelCacheStock_) modelCacheStock_->setFallbackZones({zone});
    // Another map (or the same one asked for again): the fetch chain and its clock start over.
    refPropChain_.reset();
    refPropClock_.invalidate();
    refPropFetchMs_ = -1;
    refPropFailuresLogged_ = false;
    refPropBuilt_.clear();
    refPropBuiltTris_ = 0;
    if (modelCacheStock_ && !refPropTimer_) {
        // Props are redrawn shortly after the model cache has ended a batch. The timer is not restarted while it
        // runs: batches that end in quick succession cannot put the redraw off.
        refPropTimer_ = new QTimer(this);
        refPropTimer_->setSingleShot(true);
        refPropTimer_->setInterval(1500);
        connect(refPropTimer_, &QTimer::timeout, this, [this] { rebuildReferenceProps(); });
        // A model that was fetched again (Model Info > refetch) must not be drawn from its kept copy.
        connect(modelCacheStock_, &ModelCache::modelReady, this, [this](const QString& name) {
            if (const auto it = refPropBuilt_.find(name.toStdString()); it != refPropBuilt_.end()) {
                refPropBuiltTris_ -= std::min(refPropBuiltTris_, it->second->tris);
                refPropBuilt_.erase(it);
            }
        });
        connect(modelCacheStock_, &ModelCache::batchEnded, this, [this](int) {
            if (!refProps_ || referenceZone_.isEmpty()) return;
            updateStockPropLabel();  // the counts move with every batch, also one that failed as a whole
            if (!refPropTimer_->isActive()) refPropTimer_->start();
        });
        // The fixed label in the status bar (research 84): what is ready, what is being fetched, what failed. A
        // status message is gone after six seconds; this stays while the reference is shown. The slow tick keeps
        // the seconds going and hides the label once the reference has been cleared.
        refPropLabel_ = new QLabel(this);
        refPropLabel_->setObjectName("stockPropLabel");
        refPropLabel_->hide();
        statusBar()->insertPermanentWidget(0, refPropLabel_);
        auto* tick = new QTimer(this);
        tick->setInterval(1000);
        connect(tick, &QTimer::timeout, this, [this] {
            if (refPropLabel_->isVisible() || (refProps_ && !referenceZone_.isEmpty())) updateStockPropLabel();
            if (!refProps_ && !refPropBuilt_.empty()) {  // the reference was cleared in the middle of a fetch
                refPropBuilt_.clear();
                refPropBuiltTris_ = 0;
            }
        });
        tick->start();
        // Markers are not drawn in the lit preview (rebuildReferenceProps): redraw the props when it is toggled.
        if (QAction* lit = action("ToggleLitPreview"))
            connect(lit, &QAction::triggered, this, [this] {
                if (!refProps_ || referenceZone_.isEmpty()) return;
                const StockPropStatus s = stockPropStatus();
                if (s.counts.missing + s.counts.failed > 0) QTimer::singleShot(0, this, [this] { rebuildReferenceProps(); });
            });
    }
    if (!worldCache_->lookup(zone)) {
        if (const QString e = worldCache_->errorOf(zone); !e.isEmpty()) {
            ctx_.setStatus("Stock map reference " + zone + " failed: " + e);
            referenceZone_.clear();
            return;
        }
        ctx_.setStatus("Exporting " + zone + " world geometry through ACTS...");
        worldCache_->request(zone);
        return;
    }
    ctx_.setStatus("Loading " + zone + " world geometry...");
    matCache_->addMapZone(zone);  // its own terrain blends are not in the material catalog
    if (skyCache_) skyCache_->addMapZone(zone);  // its own sky (sky_mp_speed_midday) may be in no traced zone
    if (imageCache_) imageCache_->addMapZone(zone);  // decal volume images likewise
    // Parsing takes a few seconds (hundreds of MB of JSON): off the UI thread, then hand the triangles to the camera.
    QPointer<MainWindow> self(this);
    WorldCache* cache = worldCache_;
    const QString zoneDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache/" + zone;
    const StockDecalTools tools{toolPath("acts"), toolPath("replay"), toolPath("oodle")};
    std::thread([self, cache, zone, zoneDir, tools] {
        QString err;
        std::optional<StockWorld> loaded = cache->load(zone, &err);
        auto world = loaded ? std::make_shared<const StockWorld>(std::move(*loaded)) : std::shared_ptr<const StockWorld>();
        auto tris = std::make_shared<std::vector<app::GpuVertex>>();
        size_t n = world ? app::buildReferenceTris(*world, *tris) : 0;
        auto plans = std::make_shared<std::vector<ReferencePlan>>(buildReferencePlans(*tris));  // 2D backdrops (3.19)
        auto texBatches = std::make_shared<std::vector<app::ReferenceTexBatch>>();
        if (world) app::buildReferenceTexBatches(*world, *texBatches);  // textured camera reference (3.19)
        if (world) appendStockDecalVolumes(*world, zone, zoneDir, tools, *texBatches);  // projected decals (research 99)
        // Props (11.3, core stock_smodels): the static model placements of the same export.
        auto props = std::make_shared<std::vector<StockModelInstance>>();
        std::string perr;
        if (n) readStockModelInstances(zoneDir.toStdString(), *props, &perr);
        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches] {
            if (!self || self->referenceZone_ != zone) return;
            self->stockWorld_ = world;
            if (world) self->camera_->setGameStockBakedScale(world->bakedLightScale);
            self->camera_->setReferenceTextured(std::move(*texBatches));
            self->camera_->setGameStockMap(zone.toStdString());
            // Game Lighting draws the stock world with its own lightmap (research 94): cached, or made now.
            self->camera_->setStockLightmap(nullptr);
            self->camera_->setStockProbes(nullptr);
            QString skyMat;
            if (world)
                for (const auto& m : world->meshes) {
                    const QString mat = QString::fromStdString(m.material).remove(QChar(',')).trimmed();
                    if (mat.section(QChar('/'), -1).startsWith("sky_", Qt::CaseInsensitive)) {
                        skyMat = mat;
                        break;
                    }
                }
            self->loadStockSun(zone, skyMat);
            const bool haveLm = self->worldCache_->lightmapFile(zone) || !self->worldCache_->lightmapIndices(zone).isEmpty();
            if (haveLm) self->loadStockLightmap(zone);
            if (self->worldCache_->probesFile(zone)) self->loadStockProbes(zone);
            const bool needLm = !haveLm && self->worldCache_->lightmapErrorOf(zone).isEmpty();
            const bool needProbes = !self->worldCache_->probesFile(zone) && self->worldCache_->probesErrorOf(zone).isEmpty();
            if (needLm || needProbes) self->worldCache_->requestLightmap(zone);
            self->ctx_.referencePlans = std::move(*plans);
            self->ctx_.cameraMoved();  // repaints the 2D views too
            if (n == 0) {
                self->ctx_.setStatus("Stock map reference " + zone + ": " + (err.isEmpty() ? QString("no triangles") : err));
                return;
            }
            self->refProps_ = props;
            self->refPropZone_ = zone;
            self->refPropUse_ = stockModelUsage(*props);
            self->refBaseTris_ = tris;
            self->refWorldTris_ = n;
            // The flat world list is handed over once here; the props are instances and change as models arrive.
            self->camera_->setReference(std::vector<app::GpuVertex>(*tris));
            self->rebuildReferenceProps();
        }, Qt::QueuedConnection);
    }).detach();
}

QStringList MainWindow::stockSkyList() const { return stockSkyNames(); }

MainWindow::Preferences MainWindow::currentPreferences() const {
    Preferences p;
    p.cameraSpeed = ctx_.cam.speed;
    p.cameraFov = ctx_.cam.fov;
    p.autosaveMinutes = autosave_.isActive() ? autosave_.interval() / 60000 : 0;
    p.toasts = toasts_ ? toasts_->toastsEnabled() : true;
    p.textureLock = ctx_.doc.textureLock;
    p.fogPreview = ctx_.fogPreview;
    p.modelScale = Settings::instance().value("assets/modelScale", 1.0).toDouble();
    p.materialRoots = Settings::instance().value("assets/materialRoots").toStringList();
    p.modelRoots = Settings::instance().value("assets/modelRoots").toStringList();
    p.prefabRoots = Settings::instance().value("prefabRoots").toStringList();
    return p;
}

void MainWindow::applyPreferences(const Preferences& p) {
    ctx_.cam.speed = std::clamp(p.cameraSpeed, 16.0, 8000.0);
    ctx_.cam.fov = std::clamp(p.cameraFov, 40.0, 130.0);
    Settings::instance().setValue("camera/speed", ctx_.cam.speed);
    Settings::instance().setValue("camera/fov", ctx_.cam.fov);
    const int minutes = std::clamp(p.autosaveMinutes, 0, 120);
    if (minutes > 0) autosave_.start(minutes * 60 * 1000);
    else autosave_.stop();
    Settings::instance().setValue("autosave/minutes", minutes);
    // Toggles go through their menu commands so the checkmarks and saved flags stay in step.
    const auto toggle = [this](const char* command, bool want, bool have) {
        if (want != have)
            if (QAction* a = action(command)) a->trigger();
    };
    toggle("ToggleToasts", p.toasts, toasts_ ? toasts_->toastsEnabled() : true);
    toggle("ToggleTextureLock", p.textureLock, ctx_.doc.textureLock);
    toggle("ToggleFogPreview", p.fogPreview, ctx_.fogPreview);
    const bool assetsChanged = p.modelScale != Settings::instance().value("assets/modelScale", 1.0).toDouble() ||
                               p.materialRoots != Settings::instance().value("assets/materialRoots").toStringList() ||
                               p.modelRoots != Settings::instance().value("assets/modelRoots").toStringList();
    Settings::instance().setValue("assets/modelScale", p.modelScale > 0 ? p.modelScale : 1.0);
    Settings::instance().setValue("assets/materialRoots", p.materialRoots);
    Settings::instance().setValue("assets/modelRoots", p.modelRoots);
    Settings::instance().setValue("prefabRoots", p.prefabRoots);
    ctx_.scene.modelScale = Settings::instance().value("assets/modelScale", 1.0).toDouble();
    if (assetsChanged) reloadCustomAssets();
    ctx_.cameraMoved();
    ctx_.setStatus("Preferences saved");
}

void MainWindow::showPreferences() {
    const Preferences cur = currentPreferences();
    QDialog dlg(this);
    dlg.setWindowTitle("Preferences");
    dlg.setObjectName("preferencesDialog");
    auto* form = new QFormLayout(&dlg);
    auto num = [&](double lo, double hi, double v, int dec) {
        auto* s = new QDoubleSpinBox(&dlg);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setValue(v);
        return s;
    };
    auto* speed = num(16, 8000, cur.cameraSpeed, 0);
    auto* fov = num(40, 130, cur.cameraFov, 0);
    auto* autosave = num(0, 120, cur.autosaveMinutes, 0);
    autosave->setSpecialValueText("off");
    auto* scale = num(0.001, 1000, cur.modelScale, 3);
    auto* toasts = new QCheckBox(&dlg);
    toasts->setChecked(cur.toasts);
    auto* texLock = new QCheckBox(&dlg);
    texLock->setChecked(cur.textureLock);
    auto* fog = new QCheckBox(&dlg);
    fog->setChecked(cur.fogPreview);
    auto list = [&](const QStringList& l) {
        auto* e = new QLineEdit(l.join(';'), &dlg);
        e->setPlaceholderText("folders separated by ;");
        return e;
    };
    auto* mats = list(cur.materialRoots);
    auto* models = list(cur.modelRoots);
    auto* prefabs = list(cur.prefabRoots);
    form->addRow("Camera speed (units/s)", speed);
    form->addRow("Camera field of view", fov);
    form->addRow("Autosave every (minutes)", autosave);
    form->addRow("Toast notifications", toasts);
    form->addRow("Texture lock", texLock);
    form->addRow("Fog preview", fog);
    form->addRow("Custom model scale (OBJ units -> map)", scale);
    form->addRow("Extra material folders", mats);
    form->addRow("Extra model folders", models);
    form->addRow("Prefab folders", prefabs);
    // Play in game (F5): what the one-key start does (docs: User Guide, "F5 from a closed game").
    const DevmapRunner::PlayOptions play = DevmapRunner::playOptions();
    auto* playMode = new QComboBox(&dlg);
    playMode->setObjectName("prefsPlayGametype");
    playMode->setEditable(true);
    playMode->addItems({"war", "dm", "dom", "sd", "ctf", "conf", "koth"});
    playMode->setCurrentText(play.gametype);
    playMode->setToolTip("The game mode F5 starts (ui_gametype name: war = Team Deathmatch, dm = Free-for-all, dom = Domination, "
                         "sd = Search and Destroy, ctf = Capture the Flag). The map needs that mode's spawns and objects.");
    auto* playBots = new QSpinBox(&dlg);
    playBots->setObjectName("prefsPlayBots");
    playBots->setRange(0, 199);
    playBots->setValue(play.bots);
    playBots->setToolTip("Bots for a client that starts the map by itself (0 = none)");
    auto* playCover = new QCheckBox("Cover the game window until the map loads", &dlg);
    playCover->setObjectName("prefsPlayCover");
    playCover->setChecked(play.cover);
    auto* playHost = new QCheckBox("Check the local online-services host and start it when it does not answer", &dlg);
    playHost->setObjectName("prefsPlayHost");
    playHost->setChecked(play.hostCheck);
    auto* playHostCmd = new QLineEdit(play.hostCommand, &dlg);
    playHostCmd->setObjectName("prefsPlayHostCommand");
    playHostCmd->setPlaceholderText(DevmapRunner::hostCommandSetting().isEmpty() ? QStringLiteral("no host script found: type the command that starts your host")
                                                                               : DevmapRunner::hostCommandSetting());
    playHostCmd->setToolTip("The command that starts the local host. Blank uses the default shown here; a single - never starts one.");
    auto* playRequest = new QCheckBox("Let a client that can start the map by itself (faster, no menus)", &dlg);
    playRequest->setObjectName("prefsPlayRequest");
    playRequest->setChecked(play.startRequest);
    auto* realScriptables = new QCheckBox("Real stock scripted objects: carry stock doors, cones and cars into the map", &dlg);
    realScriptables->setObjectName("prefsRealScriptables");
    realScriptables->setChecked(Settings::instance().value("build/realScriptables", true).toBool());
    realScriptables->setToolTip("On: a scriptable entity whose definition lives in a stock map (a door, a traffic cone, a car) is built by copying "
                                "the stock definition with its models, collision and materials into your map, and the stock door prefab builds "
                                "the game's real door. Built exactly like the stock map's own data, but not seen in game yet: if a map with such "
                                "an object does not load, turn this off and build again. "
                                "Off: such objects are left out and the door prefab builds its scripted stand-in.");
    form->addRow("Build", realScriptables);
    // 14.1 (research 90): the collision layout of maps that do not set worldspawn iw8r_collision themselves.
    auto* collisionLayout = new QComboBox(&dlg);
    collisionLayout->setObjectName("prefsCollisionLayout");
    collisionLayout->addItem("Automatic: streamed tiles for a map wider than 20000 units", "auto");
    collisionLayout->addItem("Always one piece (all collision in the map zone)", "one");
    collisionLayout->addItem("Always streamed tiles (1,024 tile zones and the combined package)", "tiles");
    collisionLayout->setCurrentIndex(std::max(0, collisionLayout->findData(QString(collisionLayoutName(
        parseCollisionLayout(Settings::instance().value("build/collisionLayout", "auto").toString().toStdString()))))));
    collisionLayout->setToolTip("How a build lays out the collision. One piece: every core map does this. Streamed tiles: the game's own layout for "
                                "Warzone-size maps (32 x 32 tiles of 8192 units; a client loads the tiles near the player, a host the combined "
                                "package). The editor cuts the tiles by itself. A map can set its own with worldspawn iw8r_collision "
                                "(auto, one, tiles). Tiles are built like the stock maps' own but not seen in game yet.");
    form->addRow("Build: collision", collisionLayout);
    form->addRow("F5: game mode", playMode);
    form->addRow("F5: bots", playBots);
    form->addRow("F5", playCover);
    form->addRow("", playHost);
    form->addRow("F5: host command", playHostCmd);
    form->addRow("", playRequest);
    auto* note = new QLabel("Tool paths (game, ACTS, x64-zt, Oodle): Help > First-Run Setup or Tools > Tool Paths. "
                            "Keys: Help > Keyboard Shortcuts.", &dlg);
    note->setWordWrap(true);
    note->setStyleSheet("color: gray");
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    Preferences p;
    p.cameraSpeed = speed->value();
    p.cameraFov = fov->value();
    p.autosaveMinutes = int(autosave->value());
    p.toasts = toasts->isChecked();
    p.textureLock = texLock->isChecked();
    p.fogPreview = fog->isChecked();
    p.modelScale = scale->value();
    const auto split = [](QLineEdit* e) {
        QStringList out;
        for (const QString& s : e->text().split(';', Qt::SkipEmptyParts))
            if (!s.trimmed().isEmpty()) out << s.trimmed();
        return out;
    };
    p.materialRoots = split(mats);
    p.modelRoots = split(models);
    p.prefabRoots = split(prefabs);
    applyPreferences(p);
    Settings::instance().setValue("build/realScriptables", realScriptables->isChecked());
    Settings::instance().setValue("build/collisionLayout", collisionLayout->currentData().toString());
    DevmapRunner::PlayOptions po;
    po.gametype = playMode->currentText();
    po.bots = playBots->value();
    po.cover = playCover->isChecked();
    po.hostCheck = playHost->isChecked();
    po.hostCommand = playHostCmd->text();
    po.startRequest = playRequest->isChecked();
    DevmapRunner::setPlayOptions(po);
}

void MainWindow::previewModel(const QString& name) {
    previewedModel_ = name;
    if (!modelCacheStock_) return;
    const auto e = modelCacheStock_->lookup(name);
    if (!e) {
        modelCacheStock_->request({name});
        models_->setPreview({}, name + "\nexporting with ACTS...");
        return;
    }
    if (e->failed) {
        models_->setPreview({}, name + "\nnot available: " + e->error);
        return;
    }
    QString err;
    const auto m = modelCacheStock_->loadModel(name, &err);
    if (!m) {
        models_->setPreview({}, name + "\n" + err);
        return;
    }
    size_t tris = 0;
    for (const auto& mesh : m->meshes) tris += mesh.idx.size() / 3;
    const auto& b = e->bounds;
    models_->setPreview(renderModelPreview(*m, 256),
                        QString("%1\n%2 triangles, %3 x %4 x %5 units (zone %6)")
                            .arg(name)
                            .arg(tris)
                            .arg(b[3] - b[0], 0, 'f', 0)
                            .arg(b[4] - b[1], 0, 'f', 0)
                            .arg(b[5] - b[2], 0, 'f', 0)
                            .arg(e->zone));
}

void MainWindow::previewImage(const QString& name) {
    previewedImage_ = name;
    const QString dds = imageCache_->cachedDds(name);
    if (dds.isEmpty()) {
        const QString err = imageCache_->lastError(name);
        if (!err.isEmpty()) {
            images_->setPreview({}, name + "\n" + err);
            return;
        }
        imageCache_->request(name);
        images_->setPreview({}, name + "\nexporting with ACTS...");
        return;
    }
    std::string bytes;
    app::Rgba8Image img;
    if (!readFile(dds.toStdString(), bytes) || !app::decodeDds(bytes, img)) {
        images_->setPreview({}, name + "\ncannot decode " + dds + " (HDR / BC6H skies are not previewed)");
        return;
    }
    const app::Rgba8Image small = app::downscale(img, 256);
    const QImage q = QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).copy();
    images_->setPreview(q, QString("%1\n%2 x %3").arg(name).arg(img.width).arg(img.height));
}


// ---------------------------------------------------------------------------------------------------------------
// 3.18 LOD preview

int MainWindow::stockLodFor(const Entity& e, const std::string& model) {
    const bool collision = ctx_.scene.cameraMode == iw8r::app::CameraMode::Collision;
    if ((!lodPreview_ && !collision) || !modelCacheStock_) return 0;
    auto it = lodDetail_.find(model);
    if (it == lodDetail_.end()) {
        std::optional<XModelInfo> d;
        if (const auto ent = modelCacheStock_->lookup(qs(model)); ent && !ent->failed) d = modelCacheStock_->loadDetail(qs(model));
        it = lodDetail_.emplace(model, std::move(d)).first;
    }
    if (!it->second || it->second->lods.size() < 2) return 0;
    const XModelInfo& d = *it->second;
    if (collision) return d.collLod > 0 ? d.collLod : 0;
    const auto o = e.origin();
    if (!o) return 0;
    double lodScale = 1;
    if (const std::string* v = e.value("lod_scale")) lodScale = std::atof(v->c_str());
    return previewLodFor(d, length(*o - ctx_.cam.pos), lodScale);
}

void MainWindow::refreshLodPreview() {
    std::vector<int> sig;
    const bool active = lodPreview_ || ctx_.scene.cameraMode == iw8r::app::CameraMode::Collision;
    if (active)
        for (const auto& e : ctx_.doc.map().entities)
            if (const std::string* m = e.value("model"); m && !m->empty()) {
                const std::string cls = e.classname();
                if (cls == "script_model" || cls == "misc_model") sig.push_back(stockLodFor(e, *m));
            }
    if (sig == lodSig_) return;
    lodSig_ = std::move(sig);
    ctx_.scene.invalidate();
    ctx_.cameraMoved();
}

void MainWindow::setLodPreview(bool on) {
    lodPreview_ = on;
    if (QAction* a = actions_.value("ToggleLodPreview")) a->setChecked(on);
    ctx_.setStatus(on ? "LOD Preview: stock models draw the LOD their camera distance selects (lod_scale counts; past the last "
                        "LOD they show as a box); LODs are fetched once"
                      : "LOD Preview off");
    refreshLodPreview();
}


// ---------------------------------------------------------------------------------------------------------------
// 4.12 asset availability audit

QString MainWindow::assetAuditStatusFile(const QString& kind) const {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/asset_catalog"))) return d.filePath("data/asset_catalog/iw8_" + kind + "_status.tsv");
    return {};
}

void MainWindow::showAssetAudit() {
    if (auto* open = findChild<QDialog*>("assetAuditDialog")) {
        open->raise();
        open->activateWindow();
        return;
    }
    auto* d = new QDialog(this);
    d->setObjectName("assetAuditDialog");
    d->setWindowTitle("Asset Availability Audit");
    d->setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(d);
    auto* intro = new QLabel("Checks which stock assets Replay 1.20 can export (ACTS), so the browsers hide what cannot be built "
                             "and say why. The result goes to data/asset_catalog/iw8_&lt;kind&gt;_status.tsv; names already "
                             "there are skipped, so a stopped audit resumes. Sounds need no audit: the Sounds browser lists the "
                             "aliases of the always-loaded soundbanks.", d);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    lay->addWidget(intro);
    auto* row = new QHBoxLayout;
    auto* kind = new QComboBox(d);
    kind->setObjectName("assetAuditKind");
    kind->addItems({"materials", "models", "effects", "images"});
    auto* limit = new QSpinBox(d);
    limit->setObjectName("assetAuditLimit");
    limit->setRange(0, 1000000);
    limit->setSpecialValueText("all");
    limit->setPrefix("check at most ");
    auto* start = new QPushButton("Start", d);
    start->setObjectName("assetAuditStart");
    auto* stop = new QPushButton("Stop", d);
    stop->setEnabled(false);
    row->addWidget(new QLabel("Kind:", d));
    row->addWidget(kind);
    row->addWidget(limit);
    row->addStretch();
    row->addWidget(start);
    row->addWidget(stop);
    lay->addLayout(row);
    auto* summary = new QLabel(d);
    summary->setObjectName("assetAuditSummary");
    lay->addWidget(summary);
    auto* log = new QPlainTextEdit(d);
    log->setObjectName("assetAuditLog");
    log->setReadOnly(true);
    log->setMinimumSize(560, 220);
    lay->addWidget(log);
    const auto showCounts = [this, kind, summary] {
        // Counts of the existing status file(s) of the kind.
        const QString f = assetAuditStatusFile(kind->currentText());
        int ok = 0, grey = 0, missing = 0;
        const QDir dir = QFileInfo(f).absoluteDir();
        for (const QString& fn : dir.entryList({"iw8_" + kind->currentText() + "_status*.tsv"}, QDir::Files)) {
            QFile in(dir.filePath(fn));
            if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            while (!in.atEnd()) {
                const QByteArray l = in.readLine();
                if (l.startsWith('#')) continue;
                const QByteArray st = l.split('\t').value(1).trimmed();
                ok += st == "ok";
                grey += st == "grey";
                missing += st == "missing";
            }
        }
        summary->setText(ok + grey + missing == 0 ? "Not audited yet."
                                                  : QString("Recorded: %1 ok, %2 grey (no colour image), %3 not exportable").arg(ok).arg(grey).arg(missing));
    };
    connect(kind, &QComboBox::currentTextChanged, d, showCounts);
    showCounts();
    auto* proc = new QProcess(d);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, d, [proc, log] {
        log->appendPlainText(QString::fromLocal8Bit(proc->readAll()).trimmed());
    });
    connect(start, &QPushButton::clicked, d, [this, proc, kind, limit, start, stop, log] {
        const QString out = assetAuditStatusFile(kind->currentText());
        if (out.isEmpty()) {
            log->appendPlainText("No data/asset_catalog folder next to the editor.");
            return;
        }
        QStringList args{"--asset-audit", kind->currentText(), "--out", out, "--batch", "40"};
        if (limit->value() > 0) args << "--limit" << QString::number(limit->value());
        log->appendPlainText("> IW8Radiant --asset-audit " + kind->currentText() + " -> " + out);
        start->setEnabled(false);
        kind->setEnabled(false);
        stop->setEnabled(true);
        proc->start(QCoreApplication::applicationFilePath(), args);
    });
    connect(stop, &QPushButton::clicked, proc, [proc] { proc->kill(); });
    connect(proc, &QProcess::finished, d, [this, kind, start, stop, log, showCounts](int code, QProcess::ExitStatus st) {
        log->appendPlainText(st == QProcess::CrashExit ? "Stopped." : QString("Done (exit %1).").arg(code));
        start->setEnabled(true);
        kind->setEnabled(true);
        stop->setEnabled(false);
        showCounts();
        const QString k = kind->currentText();
        AssetBrowser* b = k == "materials" ? materials_ : k == "models" ? models_ : k == "effects" ? effects_ : images_;
        if (b) b->reloadCatalog();
    });
    connect(d, &QDialog::finished, proc, [proc] {
        if (proc->state() != QProcess::NotRunning) proc->kill();
        proc->waitForFinished(3000);
    });
    d->show();
}


// ---------------------------------------------------------------------------------------------------------------
// 8.2 effect editor

FxEditorDialog* MainWindow::openEffectEditor() {
    if (auto* open = findChild<FxEditorDialog*>("fxEditorDialog")) {
        open->raise();
        open->activateWindow();
        return open;
    }
    const QString folder = ctx_.doc.path().empty()
                               ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/custom_assets/fx"
                               : QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/fx";
    QDir().mkpath(folder);
    auto* d = new FxEditorDialog(folder, [this](const QString& name) {
        const Vec3 at = placementPoint();
        ctx_.doc.addEntity({{"classname", "fx_origin"}, {"fx", name.toStdString()},
                            {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z + 8)}},
                           {}, "Place effect " + name.toStdString());
        ctx_.documentEdited("Placed fx_origin " + name + " (the map's own effect; compile builds it as a Replay vfx)");
    }, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    if (ctx_.doc.path().empty()) ctx_.setStatus("Effect Editor: save the map to keep effects beside it (using " + folder + ")");
    d->show();
    return d;
}

}  // namespace iw8r::qt
