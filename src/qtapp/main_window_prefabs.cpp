// IW8 Radiant (Qt shell) - the main window's prefab commands: plain .map prefabs and prefab bundles
// (docs/PREFAB_BUNDLES.md). Placing, the drag preview, placement ids on copies, the bundle panel of the Entity dock,
// Save Selection As Prefab, explode / edit / make unique, and the asset folders the placed bundles bring.
// These are MainWindow members kept in their own file (main_window.cpp is at the compiler's size limit).
#include "main_window.h"
#include <QTimer>
#include <QStatusBar>
#include <QPushButton>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "camera_view.h"
#include "compile_iw8.h"
#include "grid_view.h"
#include "map_text.h"
#include "panels.h"
#include "prefab.h"
#include "prefab_browser.h"
#include "prefab_bundle.h"
#include "prefab_bundle_panel.h"
#include "prefab_save_dialog.h"
#include "settings.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// ---- prefab bundle asset folders (docs/PREFAB_BUNDLES.md): the bundles a map places bring materials and models.

namespace {
QStringList withFolders(QStringList roots, const std::vector<std::string>& more) {
    for (const std::string& m : more) {
        const QString q = QDir::cleanPath(QString::fromStdString(m));
        bool have = false;
        for (const QString& r : roots) have = have || QDir::cleanPath(r).compare(q, Qt::CaseInsensitive) == 0;
        if (!have) roots << q;
    }
    return roots;
}
}  // namespace

QStringList MainWindow::lookupMaterialRoots() const { return withFolders(customMaterialRoots(), bundleRoots_.materials); }
QStringList MainWindow::lookupModelRoots() const { return withFolders(customModelRoots(), bundleRoots_.models); }

QString MainWindow::bundleRootsKey() const {
    // What the folders depend on: where the map is, the prefab folders, and which prefabs the map places.
    std::set<std::string> models;
    for (const auto& e : ctx_.doc.map().entities)
        if (e.classname() == "misc_prefab")
            if (const std::string* m = e.value("model")) models.insert(*m);
    QString key = qs(ctx_.doc.path()) + "\n" + PrefabBrowser::searchRoots().join(";");
    for (const std::string& m : models) key += "\n" + qs(m);
    return key;
}

void MainWindow::updateBundleRoots() {
    bundleRootsKey_ = bundleRootsKey();
    bundleRoots_ = prefabBundleAssetRoots(ctx_.doc.map(), ctx_.doc.path(), PrefabBrowser::searchRootsStd());
}

void MainWindow::bundleRootsMayHaveChanged() {
    if (bundleRootsKey() == bundleRootsKey_) return;
    const PrefabAssetRoots before = bundleRoots_;
    updateBundleRoots();
    if (before.materials == bundleRoots_.materials && before.models == bundleRoots_.models) return;
    // A bundle with its own materials or models was placed or removed: load the library and the models again.
    reloadCustomAssets();
    ctx_.scene.sync(ctx_.doc);
    camera_->update();
}

std::vector<MainWindow::PrefabParam> MainWindow::prefabParameters(Id prefabEntity, QString* err) const {
    // Every key of every entity of the prefab (worldspawn, origin and angles excluded), and its current override.
    std::vector<PrefabParam> out;
    const Entity* inst = ctx_.doc.findEntity(prefabEntity);
    const std::string* model = inst ? inst->value("model") : nullptr;
    if (!inst || inst->classname() != "misc_prefab" || !model) {
        if (err) *err = "select one misc_prefab";
        return out;
    }
    std::vector<std::string> roots;
    for (const QString& r : PrefabBrowser::searchRoots()) roots.push_back(r.toStdString());
    const std::string path = resolvePrefabPath(ctx_.doc.path(), *model, roots);
    Document pd;
    std::string lerr;
    if (path.empty() || !pd.load(path, &lerr)) {
        if (err) *err = "cannot open the prefab " + qs(*model) + (lerr.empty() ? QString() : ": " + qs(lerr));
        return out;
    }
    std::set<std::string> materials;
    for (const Entity& e : pd.map().entities) {
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                if (p->kind == PrimKind::Patch) materials.insert(p->patchMaterial);
                for (const auto& f : p->faces) materials.insert(f.material());
            }
        if (e.isWorldspawn()) continue;
        const std::string* tn = e.value("targetname");
        const std::string selector = tn && !tn->empty() ? *tn : "class=" + e.classname();
        for (const auto& it : e.items)
            if (const KeyValueLine* kv = it.kv()) {
                const std::string k = kv->name();
                if (k == "origin" || k == "angles" || k == "classname" || k == "targetname") continue;
                PrefabParam pp;
                pp.selector = selector;
                pp.key = k;
                pp.prefabValue = kv->value;
                if (const std::string* ov = inst->value(std::string(kPrefabKeyOverridePrefix) + selector + ":" + k)) pp.override = *ov, pp.overridden = true;
                out.push_back(pp);
            }
    }
    for (const std::string& m : materials) {
        if (m.empty() || isIw8ToolMaterial(m)) continue;
        PrefabParam pp;
        pp.material = true;
        pp.key = m;
        pp.prefabValue = m;
        if (const std::string* ov = inst->value(std::string(kPrefabMaterialSwapPrefix) + m)) pp.override = *ov, pp.overridden = true;
        out.push_back(pp);
    }
    return out;
}

bool MainWindow::setPrefabParameter(Id prefabEntity, const PrefabParam& p, const std::string& value, bool clear) {
    const std::string key = p.material ? std::string(kPrefabMaterialSwapPrefix) + p.key
                                       : std::string(kPrefabKeyOverridePrefix) + p.selector + ":" + p.key;
    const bool ok = clear ? ctx_.doc.removeKey(prefabEntity, key) : ctx_.doc.setKeyValue(prefabEntity, key, value);
    if (ok) {
        ctx_.scene.invalidate();
        ctx_.documentEdited();
    }
    return ok;
}

void MainWindow::showPrefabParameters() {
    Id pid = kNoId;
    for (const auto& r : ctx_.doc.selection())
        if (const Entity* e = ctx_.doc.findEntity(r.entity); e && e->classname() == "misc_prefab") pid = r.entity;
    QString err;
    const auto params = prefabParameters(pid, &err);
    if (params.empty()) return ctx_.setStatus("Prefab Parameters: " + (err.isEmpty() ? QString("the prefab has no keys or materials to override") : err));
    QDialog dlg(this);
    dlg.setObjectName("prefabParamsDialog");
    dlg.setWindowTitle("Prefab Parameters");
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel("Values in the Override column apply to this instance only (stored as iw8r_ovr / iw8r_mat keys on the "
                              "misc_prefab). Clear a cell to go back to the prefab's value. A single space removes the key on this instance.",
                              &dlg));
    auto* table = new QTableWidget(int(params.size()), 4, &dlg);
    table->setHorizontalHeaderLabels({"Entity / material", "Key", "Prefab value", "Override"});
    for (int i = 0; i < int(params.size()); ++i) {
        const PrefabParam& p = params[size_t(i)];
        auto ro = [&](int c, const QString& t) {
            auto* it = new QTableWidgetItem(t);
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, c, it);
        };
        ro(0, p.material ? QString("(material)") : qs(p.selector));
        ro(1, p.material ? qs(p.key) : qs(p.key));
        ro(2, qs(p.prefabValue));
        table->setItem(i, 3, new QTableWidgetItem(p.overridden ? qs(p.override) : QString()));
    }
    table->resizeColumnsToContents();
    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(720, 440);
    if (dlg.exec() != QDialog::Accepted) return;
    ctx_.doc.beginGroup("Prefab Parameters");
    int changed = 0;
    for (int i = 0; i < int(params.size()); ++i) {
        const PrefabParam& p = params[size_t(i)];
        const QString v = table->item(i, 3) ? table->item(i, 3)->text() : QString();
        if (v.isEmpty()) {
            if (p.overridden) changed += setPrefabParameter(pid, p, "", true);
        } else if (!p.overridden || v.toStdString() != p.override) {
            changed += setPrefabParameter(pid, p, v == " " ? std::string() : v.toStdString(), false);
        }
    }
    ctx_.doc.endGroup();
    ctx_.setStatus(QString("Prefab Parameters: %1 override(s) changed (one undo step)").arg(changed));
}

// While a prefab is edited: a button in the status bar that leaves it (also Esc with nothing selected, Ctrl+Shift+Q).
void MainWindow::updatePrefabBanner() {
    if (!prefabLeave_) {
        prefabLeave_ = new QPushButton(this);
        prefabLeave_->setObjectName("prefabLeaveButton");
        prefabLeave_->setToolTip("Edit > Leave Prefab (Ctrl+Shift+Q, or Esc with nothing selected): back to the map that places it");
        connect(prefabLeave_, &QPushButton::clicked, this, [this] { leavePrefab(); });
        statusBar()->insertPermanentWidget(0, prefabLeave_);
    }
    prefabLeave_->setText(prefabStack_.empty() ? QString()
                                               : QString("Leave Prefab (back to %1)").arg(QFileInfo(prefabStack_.back().path).fileName()));
    prefabLeave_->setVisible(!prefabStack_.empty());
}

bool MainWindow::enterPrefab() {
    std::string model;
    for (const SelRef& r : ctx_.doc.selection())
        if (const Entity* e = ctx_.doc.findEntity(r.entity); e && e->classname() == "misc_prefab" && e->value("model")) {
            model = *e->value("model");
            break;
        }
    if (model.empty()) {
        ctx_.setStatus("Select a misc_prefab to edit");
        return false;
    }
    // A prefab bundle is edited in its main contents (prefab.map), also when a build makes its fallback contents today.
    const PrefabRef ref = resolvePrefabReference(ctx_.doc.path(), model, PrefabBrowser::searchRootsStd());
    const std::string path = ref.mapFile;
    if (ref.kind == PrefabRef::Kind::Missing || path.empty()) {
        ctx_.setStatus("Prefab not found: " + qs(model));
        return false;
    }
    if (ctx_.doc.path().empty()) {
        ctx_.setStatus("Save the map first: Leave Prefab needs a file to return to");
        return false;
    }
    if (!confirmDiscard()) return false;
    prefabStack_.push_back({qs(ctx_.doc.path()), ctx_.cam});
    QTimer::singleShot(0, this, [this] { updatePrefabBanner(); });
    loadDocument(qs(path));
    QString note;
    if (ref.kind == PrefabRef::Kind::Bundle) {
        PrefabBundle b;
        loadPrefabBundle(ref.path, b, nullptr);
        note = choosePrefabContents(b, PrefabBrowser::buildSupport()).fallback
                   ? QString(" This is the bundle's main contents; a build makes its fallback contents today (%1).")
                         .arg(QFileInfo(qs(b.fallbackMapFile)).fileName())
                   : QString(" This is the contents file of the bundle %1.").arg(QFileInfo(qs(ref.path)).fileName());
    }
    ctx_.setStatus(QString("Editing prefab %1 (depth %2). Edit > Leave Prefab returns to %3.%4")
                       .arg(qs(model)).arg(prefabStack_.size()).arg(QFileInfo(prefabStack_.back().path).fileName(), note));
    return true;
}

bool MainWindow::leavePrefab() {
    if (prefabStack_.empty()) {
        ctx_.setStatus("Not editing a prefab");
        return false;
    }
    if (!confirmDiscard()) return false;
    const PrefabParent parent = prefabStack_.back();
    prefabStack_.pop_back();
    QTimer::singleShot(0, this, [this] { updatePrefabBanner(); });
    prefabCache_.clear();  // instances re-read the edited prefab
    loadDocument(parent.path);
    ctx_.cam = parent.cam;
    ctx_.cameraMoved();
    ctx_.setStatus("Back in " + QFileInfo(parent.path).fileName() + (prefabStack_.empty() ? QString() : QString(" (prefab depth %1)").arg(prefabStack_.size())));
    return true;
}

std::shared_ptr<const MapFile> MainWindow::loadPrefab(const std::string& model) {
    std::vector<std::string> roots;
    for (const QString& r : PrefabBrowser::searchRoots()) roots.push_back(r.toStdString());
    const std::string path = resolvePrefabPath(ctx_.doc.path(), model, roots);
    if (path.empty()) return nullptr;
    if (auto it = prefabCache_.find(path); it != prefabCache_.end()) return it->second;
    std::shared_ptr<const MapFile> out;
    std::string text;
    if (readFile(path, text)) {
        Id next = 1;
        ParseResult pr = parseMap(text, next);
        if (pr.map) out = std::make_shared<MapFile>(std::move(*pr.map));
    }
    prefabCache_[path] = out;  // failures are remembered too, until the next load
    return out;
}

QString MainWindow::prefabModelFor(const QString& file) const {
    // CoD convention: the model key is relative to the map's folder (often map_source/prefabs/...).
    const QString abs = QFileInfo(file).absoluteFilePath();
    const QString mapDir = ctx_.doc.path().empty() ? QString() : QFileInfo(qs(ctx_.doc.path())).absolutePath();
    if (!mapDir.isEmpty()) {
        const QString rel = QDir(mapDir).relativeFilePath(abs);
        if (!rel.startsWith("..")) return rel;
    }
    const int ms = abs.indexOf("/map_source/", 0, Qt::CaseInsensitive);
    if (ms >= 0) return abs.mid(ms + 12);
    // A library prefab (bundled data/prefabs or a Preferences folder): relative to that root, so the map stays portable.
    for (const QString& root : PrefabBrowser::searchRoots()) {
        const QString rel = QDir(root).relativeFilePath(abs);
        if (!rel.startsWith("..") && !QDir::isAbsolutePath(rel)) return rel;
    }
    return abs;
}

namespace {
// The bundle folder a path belongs to: the .iw8prefab folder itself, or the folder of a file directly inside one
// (prefab.json, prefab.map). "" for anything else.
QString bundleFolderOfPath(const QString& path) {
    const QFileInfo fi(path);
    const QString suffix = kPrefabBundleSuffix;
    if (fi.isDir() && fi.fileName().endsWith(suffix, Qt::CaseInsensitive)) return fi.absoluteFilePath();
    if (fi.isFile() && fi.absoluteDir().dirName().endsWith(suffix, Qt::CaseInsensitive)) return fi.absolutePath();
    return {};
}
}  // namespace

std::optional<Vec3> MainWindow::surfaceNormalAt(const SelRef& hit, const Vec3& point) const {
    const EntityItem* it = hit.item ? ctx_.doc.findItem(hit.entity, hit.item) : nullptr;
    const Primitive* p = it ? it->prim() : nullptr;
    if (!p || p->kind != PrimKind::Brush) return std::nullopt;
    std::optional<Vec3> best;
    double bestDist = 1e30;
    for (size_t i = 0; i < p->faces.size(); ++i)
        if (const auto pl = facePlane(*p, i)) {
            const double d = std::fabs(dot(pl->n, point) - pl->d);
            if (d < bestDist) bestDist = d, best = pl->n;
        }
    if (bestDist > 2.0) return std::nullopt;  // the point is not on this brush's surface
    return best;
}

Id MainWindow::addPrefabPlacement(const QString& file, const Vec3& point, QString* err, const std::optional<Vec3>& normal) {
    // A bundle is placed by its folder, whichever of its files was picked.
    const QString bundleFolder = bundleFolderOfPath(file);
    const QString model = prefabModelFor(bundleFolder.isEmpty() ? file : bundleFolder);
    Vec3 at = point;
    std::vector<std::pair<std::string, std::string>> keys = {{"classname", "misc_prefab"}, {"model", model.toStdString()}};
    if (!bundleFolder.isEmpty()) {
        PrefabBundle bundle;
        std::string berr;
        if (!loadPrefabBundle(bundleFolder.toStdString(), bundle, &berr)) {
            if (err) *err = qs(berr);
            return kNoId;
        }
        // The manifest's placement hints: surface (a wall prefab faces out of the wall), yaw step and snap.
        const PrefabPlacement place = prefabPlacementFor(bundle.manifest, at, normal);
        at = place.origin;
        if (place.yaw != 0) keys.push_back({"angles", "0 " + formatNumber(place.yaw) + " 0"});
        if (!place.note.empty()) ctx_.setStatus("Prefab: " + qs(place.note));
    }
    if (!loadPrefab(model.toStdString())) {
        if (err) *err = "cannot read prefab " + file;
        return kNoId;
    }
    keys.push_back({"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z)});
    // Every placement of a bundle has its own id: the names inside it are made unique with it when the map is built.
    if (!bundleFolder.isEmpty()) keys.push_back({kPrefabPlacementIdKey, nextPrefabPlacementId(ctx_.doc.map())});
    return ctx_.doc.addEntity(keys, {}, "Insert Prefab");
}

bool MainWindow::insertPrefab(const QString& file, QString* err) {
    // A drop gives the point; a double-click places it on the surface at the middle of the camera view (else the grid
    // centre), so the prefab appears where the mapper is looking.
    Vec3 at = placementPoint();
    std::optional<Vec3> normal;
    if (!dropPoint_ && camera_ && camera_->isVisible()) {
        SelRef surface;
        if (const auto hit = camera_->surfacePoint(QPointF(camera_->width() / 2.0, camera_->height() / 2.0), &surface)) {
            at = *hit;
            normal = surfaceNormalAt(surface, at);
            if (ctx_.snap)
                for (int a = 0; a < 3; ++a) at[a] = snapToGrid(at[a], ctx_.doc.grid());
        }
    }
    const Id id = addPrefabPlacement(file, at, err, normal);
    if (id == kNoId) return false;
    const Entity* e = ctx_.doc.findEntity(id);
    const std::string* model = e ? e->value("model") : nullptr;
    const std::string* pid = e ? e->value(kPrefabPlacementIdKey) : nullptr;
    ctx_.documentEdited("Inserted prefab " + (model ? qs(*model) : file) + (pid ? " as placement " + qs(*pid) : QString()) + " (undoable)");
    return true;
}

bool MainWindow::previewPrefabAtPoint(const QString& file, const Vec3& at) {
    if (prefabPreview_.active && prefabPreview_.file == file && length(prefabPreview_.at - at) < 1e-6) return true;
    endPrefabPreview(false);
    const size_t pos = ctx_.doc.undoPosition();
    QString err;
    const Id id = addPrefabPlacement(file, at, &err);
    if (id == kNoId || ctx_.doc.undoPosition() != pos + 1) {
        if (!err.isEmpty()) ctx_.setStatus("Prefab not placed: " + err);
        return false;
    }
    prefabPreview_ = {true, file, at, pos, id};
    // No documentEdited here: the docks need not follow every step of a drag. The views show the placement.
    ctx_.scene.sync(ctx_.doc);
    camera_->update();
    if (grid_) grid_->update();
    return true;
}

void MainWindow::endPrefabPreview(bool keep) {
    if (!prefabPreview_.active) return;
    const PrefabPreview was = prefabPreview_;
    prefabPreview_ = PrefabPreview{};
    if (keep) return;
    // The preview is the newest undo step (nothing else edits during a drag): take exactly that step back.
    if (ctx_.doc.undoPosition() == was.undoPos + 1) ctx_.doc.undo();
    ctx_.scene.sync(ctx_.doc);
    camera_->update();
    if (grid_) grid_->update();
}

QString MainWindow::convertPlainPrefabToBundle(const QString& mapPath, QString* err) {
    const auto fail = [&](const QString& why) {
        if (err) *err = why;
        return QString();
    };
    PrefabSaveOptions opt;
    opt.grid = 0;
    for (const QString& r : lookupMaterialRoots()) opt.materialRoots.push_back(r.toStdString());
    for (const QString& r : lookupModelRoots()) opt.modelRoots.push_back(r.toStdString());
    const QString dir = QFileInfo(mapPath).absolutePath();
    opt.soundRoots.push_back((dir + "/sounds").toStdString());
    opt.fxRoots.push_back((dir + "/fx").toStdString());
    opt.stock = PrefabBrowser::stockCatalog();
    const PrefabSaveResult result = prefabBundleFromPlainMap(mapPath.toStdString(), opt);
    if (!result.ok) return fail(qs(result.error));
    // Beside the .map file, named after it.
    const QString folder = dir + "/" + QFileInfo(mapPath).completeBaseName() + kPrefabBundleSuffix;
    if (QFileInfo::exists(folder + "/" + kPrefabManifestFile)) return fail("A bundle '" + folder + "' already exists; it is not replaced.");
    std::string werr;
    if (!writePrefabBundleFolder(folder.toStdString(), result, &werr)) return fail(qs(werr));
    return folder;
}

bool MainWindow::exportPrefabBundleAsZip(const QString& bundleFolder, const QString& zipPath, QString* err) {
    std::string e;
    if (exportPrefabBundleZip(QDir::cleanPath(bundleFolder).toStdString(), zipPath.toStdString(), &e)) return true;
    if (err) *err = qs(e);
    return false;
}

QStringList MainWindow::importPrefabBundlesFromZip(const QString& zipPath, QString* err) {
    QString ferr;
    const QString dest = prefabSaveFolder(&ferr);
    if (dest.isEmpty()) {
        if (err) *err = ferr;
        return {};
    }
    std::vector<std::string> written;
    std::string e;
    if (!importPrefabBundleZip(zipPath.toStdString(), dest.toStdString(), &written, &e)) {
        if (err) *err = qs(e);
        return {};
    }
    QStringList out;
    for (const std::string& w : written) out << qs(w);
    return out;
}

int MainWindow::rotateSelectedPrefabsByStep() {
    std::vector<std::pair<Id, std::string>> edits;
    for (const auto& r : ctx_.doc.selection()) {
        if (r.item) continue;
        const Entity* e = ctx_.doc.findEntity(r.entity);
        if (!e || e->classname() != "misc_prefab") continue;
        double step = 90;
        PrefabBundle bundle;
        if (prefabBundleOf(e->id, bundle) && bundle.manifest.yawStep > 0) step = bundle.manifest.yawStep;
        double a[3] = {0, 0, 0};
        if (const std::string* av = e->value("angles")) std::sscanf(av->c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]);
        const double yaw = std::fmod(a[1] + step + 360.0, 360.0);
        edits.push_back({e->id, formatNumber(a[0]) + " " + formatNumber(std::fabs(yaw) < 1e-9 ? 0.0 : yaw) + " " + formatNumber(a[2])});
    }
    if (edits.empty()) return 0;
    ctx_.doc.beginGroup("Rotate Prefab");
    for (const auto& [id, angles] : edits) ctx_.doc.setKeyValue(id, "angles", angles);
    ctx_.doc.endGroup();
    ctx_.documentEdited(QString("Rotated %1 prefab placement(s) by their yaw step (one undo step)").arg(edits.size()));
    return int(edits.size());
}

void MainWindow::insertPrefabDialog() {
    const QString start = ctx_.doc.path().empty() ? Settings::instance().value("lastDir").toString()
                                                  : QFileInfo(qs(ctx_.doc.path())).absolutePath();
    // A bundle is picked by the prefab.json in its folder.
    const QString f = QFileDialog::getOpenFileName(this, "Insert Prefab", start,
                                                   "Prefabs (*.map prefab.json);;Prefab bundles (prefab.json);;Prefab maps (*.map);;All files (*)");
    if (f.isEmpty()) return;
    QString err;
    if (!insertPrefab(f, &err)) QMessageBox::warning(this, "Insert Prefab", err);
}

bool MainWindow::prefabBundleOf(Id prefabEntity, PrefabBundle& out) const {
    const Entity* e = ctx_.doc.findEntity(prefabEntity);
    const std::string* model = e && e->classname() == "misc_prefab" ? e->value("model") : nullptr;
    if (!model) return false;
    const PrefabRef ref = resolvePrefabReference(ctx_.doc.path(), *model, PrefabBrowser::searchRootsStd());
    if (ref.kind != PrefabRef::Kind::Bundle) return false;
    loadPrefabBundle(ref.path, out, nullptr);  // a bundle that cannot be placed is still returned, with its problem
    return true;
}

bool MainWindow::setBundleParameter(Id prefabEntity, const PrefabParameter& p, const std::optional<std::string>& value) {
    const Entity* e = ctx_.doc.findEntity(prefabEntity);
    if (!e || e->classname() != "misc_prefab") return false;
    const std::string label = p.label.empty() ? p.name : p.label;
    bool ok = false;
    if (value) {
        const auto [key, text] = prefabParameterKey(p, *value);
        const std::string* have = e->value(key);
        if (have && *have == text) return false;
        ok = ctx_.doc.setKeyValue(prefabEntity, key, text);
        if (ok) ctx_.scene.invalidate(), ctx_.documentEdited(QString("Prefab parameter '%1' = %2 (undoable)").arg(qs(label), qs(*value)));
    } else {
        ok = ctx_.doc.removeKey(prefabEntity, prefabParameterKey(p, "").first);
        if (ok) ctx_.scene.invalidate(), ctx_.documentEdited(QString("Prefab parameter '%1' back to the bundle's value (undoable)").arg(qs(label)));
    }
    return ok;
}

size_t MainWindow::renewPlacementIds() {
    // How often each id is held, and the selected placements in map order.
    std::map<std::string, int> held;
    std::vector<std::pair<Id, std::string>> selected;
    for (const auto& e : ctx_.doc.map().entities) {
        if (e.classname() != "misc_prefab") continue;
        const std::string* pid = e.value(kPrefabPlacementIdKey);
        if (!pid) continue;
        ++held[*pid];
        if (ctx_.doc.isSelected({e.id, kNoId})) selected.push_back({e.id, *pid});
    }
    // The new ids are the editor's doing, not a step of the mapper's: they are not recorded into a macro.
    auto sink = std::move(ctx_.doc.macroSink);
    ctx_.doc.macroSink = nullptr;
    size_t changed = 0;
    for (const auto& [id, pid] : selected) {
        if (held[pid] < 2) continue;
        const std::string fresh = nextPrefabPlacementId(ctx_.doc.map());
        if (!ctx_.doc.setKeyValue(id, kPrefabPlacementIdKey, fresh)) continue;
        --held[pid];
        ++held[fresh];
        ++changed;
    }
    ctx_.doc.macroSink = std::move(sink);
    return changed;
}

bool MainWindow::duplicateWithNewIds(const std::string& undoName, const std::function<bool()>& edit) {
    bool ids = false;
    for (const SelRef& r : ctx_.doc.selection())
        if (const Entity* e = r.item == kNoId ? ctx_.doc.findEntity(r.entity) : nullptr)
            ids = ids || (e->classname() == "misc_prefab" && e->value(kPrefabPlacementIdKey));
    if (!ids) return edit();
    ctx_.doc.beginGroup(undoName);
    const bool ok = edit();
    if (ok) renewPlacementIds();
    ctx_.doc.endGroup();
    return ok;
}

bool MainWindow::cloneSelection(const Vec3& offset) {
    return duplicateWithNewIds("Clone", [&] { return ctx_.doc.cloneSelection(offset); });
}

QString MainWindow::selectedBundleFolder() const {
    for (const SelRef& r : ctx_.doc.selection()) {
        PrefabBundle b;
        if (r.item == kNoId && prefabBundleOf(r.entity, b)) return QDir::cleanPath(qs(b.folder));
    }
    return {};
}

bool MainWindow::openSelectedBundleFolder() {
    const QString folder = selectedBundleFolder();
    if (folder.isEmpty()) {
        ctx_.setStatus("Select a placement of a prefab bundle to open its folder");
        return false;
    }
    ctx_.setStatus("Bundle folder: " + QDir::toNativeSeparators(folder));
    return QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

bool MainWindow::explodeSelectedPrefab(QString* errOut) {
    for (const auto& r : ctx_.doc.selection()) {
        const Entity* e = ctx_.doc.findEntity(r.entity);
        if (!e || e->classname() != "misc_prefab") continue;
        const std::string* m = e->value("model");
        auto prefab = m ? loadPrefab(*m) : nullptr;
        std::string err = prefab ? std::string() : "cannot resolve prefab " + (m ? *m : std::string("(no model key)"));
        // A bundle placement explodes into entities with the names the build gives them (p3_door), so links between
        // them keep working and never meet the names of another placement.
        PrefabBundle bundle;
        PrefabNameScope scope;
        bool scoped = false;
        if (prefab && prefabBundleOf(e->id, bundle) && bundle.placeable) {
            const std::string* pid = e->value(kPrefabPlacementIdKey);
            scope = prefabNameScope(*prefab, bundle.manifest,
                                    pid && isValidPrefabPlacementId(*pid) ? *pid : nextPrefabPlacementId(ctx_.doc.map()));
            scoped = true;
        }
        if (prefab && explodePrefab(ctx_.doc, e->id, *prefab, &err, scoped ? &scope : nullptr)) {
            QString what = scoped && !scope.local.empty()
                               ? QString("Exploded prefab; its %1 own name(s) start with %2_ (one undo step)").arg(scope.local.size()).arg(qs(scope.pid))
                               : QString("Exploded prefab (one undo step)");
            // A bundle's script is started per placement: loose objects are no placement any more.
            if (scoped && !choosePrefabContents(bundle, PrefabBrowser::buildSupport()).scriptFile.empty())
                what += ". The bundle's script does not run for the loose objects";
            ctx_.documentEdited(what);
            return true;
        }
        if (errOut) *errOut = qs(err);
        ctx_.setStatus("Explode failed: " + qs(err));
        return false;
    }
    if (errOut) *errOut = "no misc_prefab selected";
    ctx_.setStatus("Select a misc_prefab to explode");
    return false;
}

bool MainWindow::saveSelectionAsPrefab(const QString& file, QString* err) {
    if (ctx_.doc.selection().empty()) {
        if (err) *err = "nothing selected";
        return false;
    }
    const AABB b = ctx_.doc.selectionBounds();
    Vec3 origin = b.center();
    for (int a = 0; a < 3; ++a) origin[a] = snapToGrid(origin[a], ctx_.doc.grid());
    origin.z = b.mins.z;
    std::string werr;
    if (!writeFileAtomic(file.toStdString(), selectionToPrefabText(ctx_.doc, origin), &werr)) {
        if (err) *err = qs(werr);
        return false;
    }
    prefabCache_.clear();
    // Radiant replaces the selection with the new misc_prefab (one undo step).
    const QString model = prefabModelFor(file);
    ctx_.doc.beginGroup("Create Prefab");
    ctx_.doc.deleteSelection();
    ctx_.doc.addEntity({{"classname", "misc_prefab"}, {"model", model.toStdString()},
                        {"origin", formatNumber(origin.x) + " " + formatNumber(origin.y) + " " + formatNumber(origin.z)}},
                       {}, "Create Prefab");
    ctx_.doc.endGroup();
    ctx_.documentEdited("Saved " + file + " and replaced the selection with a misc_prefab (undoable)");
    return true;
}

void MainWindow::saveSelectionAsMapPrefabDialog() {
    if (ctx_.doc.selection().empty()) return ctx_.setStatus("Select brushes, patches or entities to save as a prefab");
    const QString start = ctx_.doc.path().empty() ? Settings::instance().value("lastDir").toString()
                                                  : QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/prefabs";
    QDir().mkpath(start);
    const QString f = QFileDialog::getSaveFileName(this, "Save Selection As Plain .map Prefab", start, "Prefab maps (*.map)");
    if (f.isEmpty()) return;
    QString err;
    if (!saveSelectionAsPrefab(f, &err)) QMessageBox::warning(this, "Save Prefab", err);
}

// ---- Save Selection As Prefab: a bundle folder (docs/PREFAB_BUNDLES.md)

QString MainWindow::prefabSaveFolder(QString* err) const {
    for (const QString& root : Settings::instance().value("prefabRoots").toStringList())
        if (!root.trimmed().isEmpty()) return QDir::cleanPath(root.trimmed());
    if (!ctx_.doc.path().empty()) return QDir::cleanPath(QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/prefabs");
    if (err)
        *err = "There is no folder to save the prefab in: save the map first (the prefab then goes into the folder 'prefabs' beside "
               "it), or name a prefab folder in Edit > Preferences.";
    return {};
}

QString MainWindow::bundleFolderFor(const QString& id, const QString& category, bool* exists) const {
    if (exists) *exists = false;
    const QString dest = prefabSaveFolder();
    if (dest.isEmpty() || id.isEmpty()) return {};
    // A bundle with this id anywhere in the save folder is the one a save replaces.
    for (const PrefabBundleEntry& e : listPrefabBundles({dest.toStdString()}))
        if (e.bundle.manifest.id == id.toStdString()) {
            if (exists) *exists = true;
            return QDir::cleanPath(qs(e.bundle.folder));
        }
    QString sub = category.trimmed().toLower();
    sub.replace(QRegularExpression("[^a-z0-9_\\- ]"), "_");
    const QString folder = QDir::cleanPath(dest + (sub.isEmpty() ? QString() : "/" + sub) + "/" + id + kPrefabBundleSuffix);
    if (exists) *exists = QFileInfo::exists(folder + "/" + kPrefabManifestFile);
    return folder;
}

PrefabSaveResult MainWindow::scanSelectionForBundle(const PrefabBundleSave& s) const {
    PrefabSaveOptions opt;
    opt.name = (s.title.isEmpty() ? s.id : s.title).toStdString();
    opt.id = s.id.toStdString();
    opt.description = s.description.toStdString();
    opt.category = s.category.toStdString();
    for (const QString& t : s.tags) opt.tags.push_back(t.toStdString());
    opt.grid = ctx_.snap ? ctx_.doc.grid() : 0;
    // The mapper's own asset folders (the folders of placed bundles too, so a prefab made from parts of another
    // bundle takes those files along); every other name is a stock asset and is only named.
    for (const QString& r : lookupMaterialRoots()) opt.materialRoots.push_back(r.toStdString());
    for (const QString& r : lookupModelRoots()) opt.modelRoots.push_back(r.toStdString());
    if (!ctx_.doc.path().empty()) {
        const QString dir = QFileInfo(qs(ctx_.doc.path())).absolutePath();
        opt.soundRoots.push_back((dir + "/sounds").toStdString());
        opt.fxRoots.push_back((dir + "/fx").toStdString());
    }
    for (const std::string& r : bundleRoots_.sounds) opt.soundRoots.push_back(r);
    for (const std::string& r : bundleRoots_.effects) opt.fxRoots.push_back(r);
    opt.stock = PrefabBrowser::stockCatalog();
    return prefabBundleFromSelection(ctx_.doc, opt);
}

QString MainWindow::saveSelectionAsBundle(const PrefabBundleSave& s, QString* err) {
    const auto fail = [&](const QString& why) {
        if (err) *err = why;
        return QString();
    };
    if (ctx_.doc.selection().empty()) return fail("Nothing is selected: select the brushes and entities the prefab should hold.");
    if (!isValidPrefabId(s.id.toStdString())) return fail("The id needs 1 to 40 characters of a-z, 0-9 and _.");
    QString derr;
    if (prefabSaveFolder(&derr).isEmpty()) return fail(derr);
    bool exists = false;
    const QString folder = bundleFolderFor(s.id, s.category, &exists);
    if (exists && !s.overwrite)
        return fail(QString("A prefab with the id '%1' is already in %2. It is replaced only when you say so.").arg(s.id, QDir::toNativeSeparators(folder)));
    // Linked objects the mapper ticked join the selection, so they travel (and are replaced) with the rest.
    for (const Id id : s.alsoEntities)
        if (ctx_.doc.findEntity(id)) ctx_.doc.select({id, kNoId}, true);
    PrefabSaveResult result = scanSelectionForBundle(s);
    if (!result.ok) return fail(qs(result.error));
    if (exists) {
        // What only a hand can write into prefab.json today stays when the bundle is saved again: its parameters,
        // script and fallback settings, placement hints, and every field this editor does not know.
        PrefabBundle old;
        loadPrefabBundle(folder.toStdString(), old, nullptr);
        const PrefabManifest& o = old.manifest;
        if (o.id == result.manifest.id) {
            PrefabManifest& m = result.manifest;
            m.raw = o.raw;
            m.parameters = o.parameters;
            m.scriptFile = o.scriptFile, m.scriptEntry = o.scriptEntry, m.scriptPrefix = o.scriptPrefix, m.scriptOnce = o.scriptOnce;
            m.fallbackWhenMissing = o.fallbackWhenMissing, m.fallbackMap = o.fallbackMap, m.fallbackScript = o.fallbackScript;
            m.fallbackNote = o.fallbackNote;
            m.surface = o.surface, m.snap = o.snap, m.yawStep = o.yawStep;
            m.nameKeys = o.nameKeys;
            m.gameModes = o.gameModes;
            if (m.author.empty()) m.author = o.author;
            m.version = o.version;
            for (const std::string& f : o.buildFeatures)
                if (std::find(m.buildFeatures.begin(), m.buildFeatures.end(), f) == m.buildFeatures.end()) m.buildFeatures.push_back(f);
            m.scriptableDefs = o.scriptableDefs.empty() ? m.scriptableDefs : o.scriptableDefs;
            m.stockStrings = o.stockStrings, m.stockScripts = o.stockScripts;
        }
    }
    std::string werr;
    if (!writePrefabBundleFolder(folder.toStdString(), result, &werr, exists)) return fail(qs(werr));
    // The picture: drawn from the saved contents. It is kept as thumb.png only when every model in it could be drawn;
    // otherwise the Prefabs dock draws the picture itself, again when the missing models have arrived.
    {
        const QString png = folder + "/thumb.png";
        const QImage img = PrefabBrowser::thumbnail(folder + "/prefab.map", 256, QStringList{folder + "/models"});
        if (!img.isNull() && !PrefabBrowser::lastThumbnailIncomplete()) img.save(png);
        else QFile::remove(png);
    }
    prefabCache_.clear();
    if (prefabs_) prefabs_->refresh();
    QString what = QString("Saved the prefab '%1' to %2").arg(s.title.isEmpty() ? s.id : s.title, QDir::toNativeSeparators(folder));
    if (s.replaceSelection) {
        // The selection becomes one placement of the new bundle (one undo step); the map looks the same.
        const QString model = prefabModelFor(folder);
        ctx_.doc.beginGroup("Create Prefab");
        ctx_.doc.deleteSelection();
        ctx_.doc.addEntity({{"classname", "misc_prefab"},
                            {"model", model.toStdString()},
                            {"origin", formatNumber(result.pivot.x) + " " + formatNumber(result.pivot.y) + " " + formatNumber(result.pivot.z)},
                            {kPrefabPlacementIdKey, nextPrefabPlacementId(ctx_.doc.map())}},
                           {}, "Create Prefab");
        ctx_.doc.endGroup();
        what += " and replaced the selection with one placement of it (undoable)";
    }
    ctx_.scene.invalidate();
    ctx_.documentEdited(what);
    return folder;
}

void MainWindow::saveSelectionAsBundleDialog() {
    if (ctx_.doc.selection().empty()) return ctx_.setStatus("Select brushes, patches or entities to save as a prefab");
    QString err;
    if (prefabSaveFolder(&err).isEmpty()) {
        QMessageBox::information(this, "Save Selection As Prefab", err);
        return;
    }
    PrefabBundleSave first;
    first.title = "New prefab";
    first.id = qs(prefabIdFromName(first.title.toStdString()));
    const PrefabSaveResult scan = scanSelectionForBundle(first);
    if (!scan.ok) {
        QMessageBox::warning(this, "Save Selection As Prefab", qs(scan.error));
        return;
    }
    PrefabSaveDialog dlg(scan, [this](const QString& id, const QString& category, bool* exists) { return bundleFolderFor(id, category, exists); },
                         this);
    for (;;) {
        const int rc = dlg.exec();
        if (rc == PrefabSaveDialog::kPlainMap) return saveSelectionAsMapPrefabDialog();
        if (rc != QDialog::Accepted) return;
        PrefabBundleSave s;
        s.id = dlg.id();
        s.title = dlg.title();
        s.description = dlg.description();
        s.category = dlg.category();
        s.tags = dlg.tags();
        s.alsoEntities = dlg.tickedLinkedEntities();
        s.replaceSelection = dlg.replaceSelection();
        bool exists = false;
        const QString folder = bundleFolderFor(s.id, s.category, &exists);
        if (exists) {
            // Saving again under an id that is taken: ask before anything is replaced.
            const auto answer = QMessageBox::question(
                this, "Replace Prefab",
                QString("A prefab with the id '%1' is already saved in\n%2\n\nReplace it? Every map that places it then shows the new "
                        "contents. Its parameters, script and fallback settings are kept.")
                    .arg(s.id, QDir::toNativeSeparators(folder)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) continue;  // back to the dialog: another id can be typed
            s.overwrite = true;
        }
        if (!saveSelectionAsBundle(s, &err).isEmpty()) return;
        QMessageBox::warning(this, "Save Selection As Prefab", err);
    }
}

bool MainWindow::makeSelectedPrefabUnique(QString* err) {
    const auto fail = [&](const QString& why) {
        if (err) *err = why;
        ctx_.setStatus("Make Unique: " + why);
        return false;
    };
    Id target = kNoId;
    std::string model;
    for (const SelRef& r : ctx_.doc.selection())
        if (const Entity* e = ctx_.doc.findEntity(r.entity); e && e->classname() == "misc_prefab" && e->value("model")) {
            target = e->id;
            model = *e->value("model");
            break;
        }
    if (target == kNoId) return fail("Select one prefab placement first.");
    if (ctx_.doc.path().empty()) return fail("Save the map first: the copy goes into the folder 'prefabs' beside it.");
    const PrefabRef ref = resolvePrefabReference(ctx_.doc.path(), model, PrefabBrowser::searchRootsStd());
    if (ref.kind == PrefabRef::Kind::Missing) return fail("The prefab " + qs(model) + " was not found.");
    const QString dest = QDir::cleanPath(QFileInfo(qs(ctx_.doc.path())).absolutePath() + "/prefabs");
    QDir().mkpath(dest);
    QString copy;
    if (ref.kind == PrefabRef::Kind::Bundle) {
        PrefabBundle b;
        std::string berr;
        if (!loadPrefabBundle(ref.path, b, &berr)) return fail(qs(berr));
        // A new id: "<id>_u2", "<id>_u3", ... the first one no bundle beside the map has.
        std::set<std::string> taken;
        for (const PrefabBundleEntry& e : listPrefabBundles({dest.toStdString()})) taken.insert(e.bundle.manifest.id);
        std::string id;
        for (int n = 2; n < 10000; ++n) {
            const std::string tail = "_u" + std::to_string(n);
            id = b.manifest.id.substr(0, 40 - tail.size()) + tail;
            if (!taken.count(id) && !QFileInfo::exists(dest + "/" + qs(id) + kPrefabBundleSuffix)) break;
        }
        copy = dest + "/" + qs(id) + kPrefabBundleSuffix;
        // Copy the whole folder, then give the copy its own id (the id is part of its script function names).
        const QDir from(qs(b.folder));
        QDirIterator it(from.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString src = it.next();
            const QString to = copy + "/" + from.relativeFilePath(src);
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (!QFile::copy(src, to)) return fail("Cannot copy " + QDir::toNativeSeparators(src) + " to " + QDir::toNativeSeparators(to));
        }
        PrefabManifest m = b.manifest;
        m.id = id;
        m.title = b.title + " (copy)";
        std::string werr;
        if (!writeFileAtomic((copy + "/" + kPrefabManifestFile).toStdString(), prefabManifestText(m), &werr)) return fail(qs(werr));
    } else {
        const QFileInfo src(qs(ref.path));
        for (int n = 2; n < 10000; ++n) {
            copy = dest + "/" + src.completeBaseName() + QString("_u%1.map").arg(n);
            if (!QFileInfo::exists(copy)) break;
        }
        if (!QFile::copy(src.absoluteFilePath(), copy)) return fail("Cannot copy " + QDir::toNativeSeparators(src.absoluteFilePath()));
    }
    prefabCache_.clear();
    if (!ctx_.doc.setKeyValue(target, "model", prefabModelFor(copy).toStdString())) return fail("The placement could not be changed.");
    if (prefabs_) prefabs_->refresh();
    ctx_.scene.invalidate();
    ctx_.documentEdited("This placement now uses its own copy: " + QDir::toNativeSeparators(copy) + " (undoable; the copy stays on disk)");
    return true;
}

// The Entity dock's widget: the prefab bundle panel (description and parameters of a selected bundle placement; it
// hides itself for everything else) above the entity inspector.
QWidget* MainWindow::makeEntityDockHost() {
    auto* host = new QWidget(this);
    PrefabBundlePanel::Hooks hooks;
    hooks.bundleOf = [this](Id e, PrefabBundle& out) { return prefabBundleOf(e, out); };
    hooks.setParameter = [this](Id e, const PrefabParameter& p, const std::optional<std::string>& v) { return setBundleParameter(e, p, v); };
    hooks.editPrefab = [this] { enterPrefab(); };
    hooks.openFolder = [this] { openSelectedBundleFolder(); };
    hooks.explode = [this] { explodeSelectedPrefab(); };
    hooks.makeUnique = [this] {
        QString err;
        if (!makeSelectedPrefabUnique(&err)) QMessageBox::warning(this, "Make Prefab Unique", err);
    };
    bundlePanel_ = new PrefabBundlePanel(&ctx_, hooks, host);
    auto* lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(bundlePanel_);
    lay->addWidget(inspector_, 1);
    return host;
}

// A prefab dragged over the camera: the prefab itself follows the cursor, as a provisional placement with its real
// contents. The ray must not land on that placement, so it is hidden from picking while the point under the cursor
// is found.
void MainWindow::prefabDragHover(const QString& file, const QPointF& pos) {
    SelRef hit{kNoId, kNoId};
    std::optional<Vec3> at = camera_->surfacePoint(pos, &hit);
    if (at && prefabPreview_.active && hit.entity == prefabPreview_.entity) {
        ctx_.scene.hidden.insert(prefabPreview_.entity);
        ctx_.scene.invalidate();
        ctx_.scene.sync(ctx_.doc);
        at = camera_->surfacePoint(pos, &hit);
        ctx_.scene.hidden.erase(prefabPreview_.entity);
        ctx_.scene.invalidate();
        ctx_.scene.sync(ctx_.doc);
    }
    if (!at) return endPrefabPreview(false);
    Vec3 p = *at;
    if (ctx_.snap)
        for (int a = 0; a < 3; ++a) p[a] = snapToGrid(p[a], ctx_.doc.grid());
    previewPrefabAtPoint(file, p);
}

// A prefab dropped on the camera while its preview is shown: the drop keeps exactly the placement the mapper saw.
bool MainWindow::prefabDropKeepsPreview(const QString& file) {
    if (!prefabPreview_.active || prefabPreview_.file != file) return false;
    return dropAsset("prefabs", file, false, prefabPreview_.at, {kNoId, kNoId});
}

}  // namespace iw8r::qt
