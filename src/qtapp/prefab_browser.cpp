#include "asset_browser.h"
#include <algorithm>
#include <QIcon>
#include <QPixmap>
#include <QMessageBox>
#include <QDrag>
#include <QMimeData>
#include "prefab_browser.h"
#include <QMenu>
#include <QScrollBar>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <array>
#include <map>
#include "settings.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QUrl>

#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>

#include "model_import.h"
#include "brush_ops.h"
#include "compile_iw8.h"
#include "map_text.h"
#include "prefab_bundle.h"

namespace iw8r::qt {

namespace {
constexpr int kThumb = 112;  // thumbnail side in the list
QIcon placeholderIcon();
// Item data of a listed prefab.
enum Role {
    kPath = Qt::UserRole,      // the .map file, or the bundle folder
    kDrawn,                    // the picture was drawn
    kSearch,                   // the text the filter looks in
    kIncomplete,               // drawn with a box for a model that was not cached yet
    kIsBundle,
    kBadge,                    // "" or the badge text
    kBadgeReason,
    kThumbFile,                // a bundle's thumb.png ("" = draw the contents)
    kContents,                 // the .map file the picture is drawn from
    kModelRoots,               // a bundle's own model folder
    kPlaceable,
    kFromFile,                 // the picture shown came from kThumbFile
};

// The badge drawn over a bundle's picture: a filled strip along the bottom with the word in it.
QImage withBadge(const QImage& base, const QString& badge, bool bundle) {
    QImage img = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(std::max(9, img.height() / 11));
    f.setBold(true);
    p.setFont(f);
    if (bundle) {
        // A small tag in the top left corner tells a bundle from a plain .map prefab.
        const QRect tag(3, 3, p.fontMetrics().horizontalAdvance("BUNDLE") + 8, p.fontMetrics().height() + 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 110, 190, 230));
        p.drawRoundedRect(tag, 3, 3);
        p.setPen(Qt::white);
        p.drawText(tag, Qt::AlignCenter, "BUNDLE");
    }
    if (!badge.isEmpty()) {
        const int h = p.fontMetrics().height() + 4;
        const QRect strip(0, img.height() - h, img.width(), h);
        p.setPen(Qt::NoPen);
        p.setBrush(badge == "CANNOT PLACE" ? QColor(190, 50, 50, 235) : QColor(215, 140, 20, 235));
        p.drawRect(strip);
        p.setPen(Qt::white);
        p.drawText(strip, Qt::AlignCenter, badge);
    }
    return img;
}

QString dataFolder(const char* name) {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath(QString("data/") + name))) return QDir::cleanPath(d.filePath(QString("data/") + name));
    return {};
}
}  // namespace

const PrefabStockCatalog* PrefabBrowser::stockCatalog() {
    static const std::unique_ptr<PrefabStockCatalog> catalog = []() -> std::unique_ptr<PrefabStockCatalog> {
        const QString dir = dataFolder("asset_catalog");
        if (dir.isEmpty()) return nullptr;
        auto c = std::make_unique<PrefabStockCatalog>();
        if (c->load(dir.toStdString()) == 0) return nullptr;
        return c;
    }();
    return catalog.get();
}

// Edit > Preferences > Real stock scripted objects, on unless the mapper turns it off (owner decision 2026-11-12:
// "turn it on by default"); the environment variable IW8R_REAL_SCRIPTABLES (1 or 0) overrides the saved choice for
// one run (headless builds and tests).
bool PrefabBrowser::realScriptables() {
    const QByteArray env = qgetenv("IW8R_REAL_SCRIPTABLES");
    if (!env.isEmpty()) return env != "0";
    return Settings::instance().value("build/realScriptables", true).toBool();
}

PrefabBuildSupport PrefabBrowser::buildSupport() {
    PrefabBuildSupport s;
    s.stock = stockCatalog();
    // Edit > Preferences > Real stock scripted objects: the build writes scriptable instances, carries their stock
    // definitions and starts the stock door script, so a bundle that asks for them builds its real contents.
    if (realScriptables()) {
        s.features.insert(kPrefabFeatureScriptableInstances);
        s.features.insert(kPrefabFeatureScriptableDefCarry);
        s.features.insert(kPrefabFeatureStockScriptCalls);
    }
    return s;
}

QString PrefabBrowser::bundleBadge(const PrefabBundle& bundle, QString* reason) {
    const auto say = [&](const QString& text) {
        if (reason) *reason = text;
    };
    if (bundle.bare) {
        say({});
        return {};
    }
    if (!bundle.placeable) {
        say(QString::fromStdString(bundle.problem));
        return "CANNOT PLACE";
    }
    const PrefabContentsChoice c = choosePrefabContents(bundle, buildSupport());
    QStringList why;
    QString badge;
    if (c.fallback) {
        badge = "FALLBACK";
        const QString note = QString::fromStdString(bundle.manifest.fallbackNote);
        QStringList lacking;
        for (const std::string& f : bundle.manifest.fallbackWhenMissing)
            if (!defaultPrefabBuildFeatures().count(f)) lacking << QString::fromStdString(f);
        why << QString("Builds its fallback contents today: %1 (missing build feature(s): %2).")
                   .arg(note.isEmpty() ? QString("the build lacks a feature the prefab needs") : note, lacking.join(", "));
    } else if (!c.missingFeatures.empty()) {
        badge = "LIMITED";
        QStringList lacking;
        for (const std::string& f : c.missingFeatures) lacking << QString::fromStdString(f);
        why << QString("Needs the build feature(s) %1, which this build does not have, and has no fallback for them: "
                       "its main contents are built as they are.").arg(lacking.join(", "));
    }
    if (!c.missingAssets.empty()) {
        if (badge.isEmpty()) badge = "MISSING";
        QStringList names;
        for (const std::string& a : c.missingAssets) names << QString::fromStdString(a);
        why << QString("Not in the stock catalogue: %1.").arg(names.join(", "));
    }
    say(why.join(" "));
    return badge;
}

PrefabBrowser::PrefabBrowser(RootsFn roots, InsertFn insert, QWidget* parent)
    : QWidget(parent), roots_(std::move(roots)), insert_(std::move(insert)) {
    setObjectName("prefabBrowser");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Filter prefabs (name, tag, category)");
    filter_->setClearButtonEnabled(true);
    auto* rescan = new QPushButton("Rescan", this);
    top->addWidget(filter_, 1);
    top->addWidget(rescan);
    lay->addLayout(top);
    // Drags carry the prefab file (asset kind "prefabs") so the camera and grid views can take the drop.
    class DragList : public QListWidget {
    public:
        using QListWidget::QListWidget;

    protected:
        void startDrag(Qt::DropActions) override {
            const QListWidgetItem* it = currentItem();
            if (!it) return;
            if (it->data(kIsBundle).toBool() && !it->data(kPlaceable).toBool()) return;  // a bundle that cannot be placed
            auto* mime = new QMimeData;
            mime->setData(kAssetMimeType, (QString("prefabs\t") + it->data(Qt::UserRole).toString() + "\t0").toUtf8());
            auto* drag = new QDrag(this);
            drag->setMimeData(mime);
            if (const QIcon icon = it->icon(); !icon.isNull()) drag->setPixmap(icon.pixmap(64, 64));
            drag->exec(Qt::CopyAction);
        }
    };
    list_ = new DragList(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(kThumb, kThumb));
    list_->setGridSize(QSize(kThumb + 28, kThumb + 44));
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setSpacing(4);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    // QListView::setMovement(Static) turns dragging off (owner playtest 2: prefabs could not be dragged): on again.
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setWordWrap(true);
    list_->setToolTip("Drag a prefab into the camera or a grid view, or double-click to place it in front of the camera "
                      "(misc_prefab, one undo step)");
    list_->setTextElideMode(Qt::ElideMiddle);
    thumbTimer_ = new QTimer(this);
    thumbTimer_->setInterval(30);
    connect(thumbTimer_, &QTimer::timeout, this, [this] { loadVisibleThumbnails(); });
    connect(list_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { thumbTimer_->start(); });
    lay->addWidget(list_, 1);
    // How many are listed, or why the list is empty (no prefab folders, or nothing matches the filter).
    count_ = new QLabel(this);
    count_->setObjectName("prefabCount");
    count_->setStyleSheet("color: gray");
    lay->addWidget(count_);
    connect(filter_, &QLineEdit::textChanged, this, &PrefabBrowser::setFilter);
    connect(rescan, &QPushButton::clicked, this, &PrefabBrowser::refresh);
    // Double-click or Enter (activated covers both, so a double-click stamps once).
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        QString err;
        if (!stamp(list_->row(it), &err) && !err.isEmpty()) QMessageBox::warning(this, "Insert Prefab", err);
    });
    focusListOnDown(filter_, list_);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QListWidgetItem* it = list_->itemAt(at);
        if (!it) return;
        const int row = list_->row(it);
        QMenu menu(this);
        menu.addAction("Place", this, [this, row] {
            QString err;
            if (!stamp(row, &err) && !err.isEmpty()) QMessageBox::warning(this, "Insert Prefab", err);
        });
        const QString file = previewFileAt(row);
        QAction* pv = menu.addAction("Preview in 3D...", this, [this, file] { emit previewRequested(file); });
        pv->setEnabled(!file.isEmpty());
        menu.addSeparator();
        const QString name = titleAt(row);
        menu.addAction("Copy Name", this, [name] { QGuiApplication::clipboard()->setText(name); });
        // A bundle opens its own folder; a .map prefab the folder it is in.
        const QFileInfo path(fileAt(row));
        const QString folder = path.isDir() ? path.absoluteFilePath() : path.absolutePath();
        QAction* show = menu.addAction("Show in Folder", this, [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
        show->setEnabled(path.exists());
        menu.exec(list_->viewport()->mapToGlobal(at));
    });
}

QString PrefabBrowser::previewFileAt(int row) const {
    const QString f = fileAt(row);
    if (f.isEmpty()) return {};
    const QFileInfo fi(f);
    if (!fi.isDir()) return f;
    // A bundle folder: its main contents (a .map named like the folder, else contents.map, else the first .map).
    const QDir d(f);
    for (const QString& name : {fi.fileName() + ".map", QString("contents.map"), QString("prefab.map")})
        if (d.exists(name)) return d.filePath(name);
    const QStringList maps = d.entryList({"*.map"}, QDir::Files, QDir::Name);
    return maps.isEmpty() ? QString() : d.filePath(maps.front());
}

void PrefabBrowser::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!scanned_) refresh();
}

namespace {
QIcon placeholderIcon() {
    static const QIcon icon = [] {
        QPixmap pm(kThumb, kThumb);
        pm.fill(QColor(32, 34, 38));
        return QIcon(pm);
    }();
    return icon;
}
PrefabBrowser::ModelBoundsFn& modelBoundsFn() {
    static PrefabBrowser::ModelBoundsFn fn;
    return fn;
}
}  // namespace

void PrefabBrowser::setModelBounds(ModelBoundsFn fn) { modelBoundsFn() = std::move(fn); }

namespace {
PrefabBrowser::ModelMeshFn& modelMeshFn() {
    static PrefabBrowser::ModelMeshFn fn;
    return fn;
}
bool g_thumbIncomplete = false;
PrefabBrowser::PictureFn g_pictureSource;
}  // namespace

void PrefabBrowser::setModelMesh(ModelMeshFn fn) { modelMeshFn() = std::move(fn); }
bool PrefabBrowser::lastThumbnailIncomplete() { return g_thumbIncomplete; }
void PrefabBrowser::setPictureSource(PictureFn fn) { g_pictureSource = std::move(fn); }

void PrefabBrowser::refreshIncomplete() {
    bool any = false;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (!it->data(Qt::UserRole + 3).toBool()) continue;
        it->setData(Qt::UserRole + 1, false);
        any = true;
    }
    if (any && thumbTimer_) thumbTimer_->start();
}

QImage PrefabBrowser::thumbnail(const QString& mapFile, int size, const QStringList& modelRoots) {
    std::string text;
    if (!readFile(mapFile.toStdString(), text)) return {};
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return {};
    g_thumbIncomplete = false;
    // The mapper's own models (a bundle's models folder): the file's mesh, scaled as the editor and the build scale it.
    std::vector<std::string> ownRoots;
    for (const QString& r : modelRoots)
        if (QFileInfo(r).isDir()) ownRoots.push_back(r.toStdString());
    const double ownScale = Settings::instance().value("assets/modelScale", 1.0).toDouble();
    std::map<std::string, std::shared_ptr<const ImportedModel>> ownMeshes;
    const auto ownMesh = [&](const std::string& name) -> std::shared_ptr<const ImportedModel> {
        if (ownRoots.empty()) return nullptr;
        if (const auto it = ownMeshes.find(name); it != ownMeshes.end()) return it->second;
        std::shared_ptr<const ImportedModel> out;
        if (const std::string file = findCustomModelFile(ownRoots, name); !file.empty()) {
            auto im = std::make_shared<ImportedModel>();
            if (importModelFile(file, *im, nullptr)) {
                for (auto& part : im->meshes)
                    for (Vec3& v : part.pos) v = v * ownScale;
                out = im;
            }
        }
        ownMeshes[name] = out;
        return out;
    };
    // A three-quarter view from above (owner playtest 2026-11-11: the top-down outlines were unreadable: a window was a
    // line). Brush faces and model boxes are filled, shaded by their facing and painted back to front.
    struct Face {
        std::vector<Vec3> pts;
        Vec3 normal;
        int kind = 0;  // 0 brush, 1 model box, 2 model triangle, 3 volume (trigger, clip, hint, skip): outline only
    };
    // A brush whose every face has a trigger / clip / hint / skip material is a volume, not something the player sees:
    // it is drawn as an outline (as the camera draws it), so a trigger around a prefab does not hide the prefab.
    const auto volumeMaterial = [](const std::string& material) {
        std::string m;
        for (char c : material) m += char(std::tolower(static_cast<unsigned char>(c)));
        if (const size_t slash = m.find_last_of('/'); slash != std::string::npos) m = m.substr(slash + 1);
        return m.rfind("trigger", 0) == 0 || m.rfind("clip", 0) == 0 || m == "hint" || m == "skip";
    };
    std::vector<Face> faces;
    std::vector<Vec3> points;
    const auto addBox = [&](const std::array<Vec3, 8>& c) {
        static const int quads[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
        Vec3 centre{0, 0, 0};
        for (const Vec3& v : c) centre = centre + v * 0.125;
        for (const auto& q : quads) {
            Face f;
            for (int k = 0; k < 4; ++k) f.pts.push_back(c[size_t(q[k])]);
            Vec3 n = cross(f.pts[1] - f.pts[0], f.pts[2] - f.pts[0]);
            const Vec3 mid = (f.pts[0] + f.pts[2]) * 0.5;
            if (dot(n, mid - centre) < 0) n = n * -1.0;
            f.normal = length(n) > 1e-9 ? normalize(n) : Vec3{0, 0, 1};
            f.kind = 1;
            faces.push_back(std::move(f));
        }
    };
    for (const auto& e : pr.map->entities) {
        if (!e.isWorldspawn())
            if (auto o = e.origin()) {
                double b[6];
                const std::string* m = e.value("model");
                // The model's own triangles when its mesh is cached (at most about 4,000 per model: every n-th one).
                const std::shared_ptr<const ImportedModel> own = m ? ownMesh(*m) : nullptr;
                if (m && (own || modelMeshFn())) {
                    if (const auto mesh = own ? own : modelMeshFn()(*m)) {
                        Vec3 ang;
                        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
                        double sc = 1;
                        if (const std::string* ms = e.value("modelscale")) sc = std::max(0.001, std::atof(ms->c_str()));
                        const double yaw = ang.y * 3.14159265358979 / 180.0, c = std::cos(yaw), sn = std::sin(yaw);
                        const auto place = [&](const Vec3& p) {
                            const Vec3 z{p.x, -p.z, p.y};  // glTF Y-up -> Z-up
                            return *o + Vec3{(z.x * c - z.y * sn) * sc, (z.x * sn + z.y * c) * sc, z.z * sc};
                        };
                        size_t total = 0;
                        for (const auto& part : mesh->meshes) total += part.idx.size() / 3;
                        const size_t step = std::max<size_t>(1, total / 4000);
                        size_t n = 0;
                        for (const auto& part : mesh->meshes)
                            for (size_t i = 0; i + 2 < part.idx.size(); i += 3, ++n) {
                                if (n % step) continue;
                                Face f;
                                f.pts = {place(part.pos[part.idx[i]]), place(part.pos[part.idx[i + 1]]), place(part.pos[part.idx[i + 2]])};
                                const Vec3 nn = cross(f.pts[1] - f.pts[0], f.pts[2] - f.pts[0]);
                                if (length(nn) < 1e-9) continue;
                                f.normal = normalize(nn);
                                f.kind = 2;
                                faces.push_back(std::move(f));
                            }
                        if (total > 0) continue;
                    } else {
                        g_thumbIncomplete = true;
                    }
                }
                if (m && modelBoundsFn() && modelBoundsFn()(*m, b)) {
                    Vec3 ang;
                    if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
                    double sc = 1;
                    if (const std::string* ms = e.value("modelscale")) sc = std::max(0.001, std::atof(ms->c_str()));
                    const double yaw = ang.y * 3.14159265358979 / 180.0, c = std::cos(yaw), sn = std::sin(yaw);
                    std::array<Vec3, 8> corners;
                    const double xs[4] = {b[0], b[3], b[3], b[0]}, ys[4] = {b[1], b[1], b[4], b[4]};
                    for (int k = 0; k < 8; ++k) {
                        const int q = k % 4;
                        corners[size_t(k)] = *o + Vec3{(xs[q] * c - ys[q] * sn) * sc, (xs[q] * sn + ys[q] * c) * sc, (k < 4 ? b[2] : b[5]) * sc};
                    }
                    addBox(corners);
                    continue;
                }
                points.push_back(*o);
            }
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            const ConvexBrush cb = brushFromPrimitive(*p);
            if (!isValidBrush(cb)) continue;
            const std::vector<Winding> ws = brushWindings(cb);
            const bool volume = !p->faces.empty() &&
                                std::all_of(p->faces.begin(), p->faces.end(), [&](const auto& f) { return volumeMaterial(f.material()); });
            for (size_t i = 0; i < ws.size() && i < cb.size(); ++i) {
                if (ws[i].size() < 3) continue;
                Face f;
                f.pts = ws[i];
                f.normal = cb[i].plane.n;
                if (volume) f.kind = 3;
                faces.push_back(std::move(f));
            }
        }
    }
    if (faces.empty() && points.empty()) return {};
    // View: looking down 30 degrees from the south-west. right / up span the image, toward points at the viewer.
    const double yaw = 225.0 * 3.14159265358979 / 180.0, pitch = 30.0 * 3.14159265358979 / 180.0;
    const Vec3 toward{-std::cos(pitch) * std::cos(yaw), -std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    const Vec3 right = normalize(cross(Vec3{0, 0, 1}, toward));
    const Vec3 up = normalize(cross(toward, right));
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    const auto grow = [&](const Vec3& v) {
        const double x = dot(v, right), y = dot(v, up);
        lo[0] = std::min(lo[0], x);
        hi[0] = std::max(hi[0], x);
        lo[1] = std::min(lo[1], y);
        hi[1] = std::max(hi[1], y);
    };
    for (const Face& f : faces)
        for (const Vec3& v : f.pts) grow(v);
    for (const Vec3& v : points) grow(v);
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(32, 34, 38));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = std::max(1.0, hi[0] - lo[0]), h = std::max(1.0, hi[1] - lo[1]);
    const double k = (size - 10) / std::max(w, h);
    const double ox = (size - w * k) / 2, oy = (size - h * k) / 2;
    const auto map = [&](const Vec3& v) { return QPointF(ox + (dot(v, right) - lo[0]) * k, size - (oy + (dot(v, up) - lo[1]) * k)); };
    // Back to front by the face centre along the view direction; faces turned away are skipped.
    std::vector<std::pair<double, const Face*>> order;
    for (const Face& f : faces) {
        if (f.kind != 2 && dot(f.normal, toward) <= 0.01) continue;  // model triangles are drawn from both sides
        Vec3 c{0, 0, 0};
        for (const Vec3& v : f.pts) c = c + v * (1.0 / double(f.pts.size()));
        order.push_back({dot(c, toward), &f});
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const Vec3 light = normalize(Vec3{-0.35, -0.55, 0.75});
    // Only volumes in the prefab (a trigger saved on its own): they are filled after all, or the picture is nearly empty.
    const bool onlyVolumes = points.empty() && std::all_of(faces.begin(), faces.end(), [](const Face& f) { return f.kind == 3; });
    for (const auto& [depth, f] : order) {
        (void)depth;
        if (f->kind == 3 && !onlyVolumes) continue;  // outlines are drawn on top, below
        const double facing = f->kind == 2 ? std::fabs(dot(f->normal, light)) : std::max(0.0, dot(f->normal, light));
        const double shade = 0.45 + 0.55 * facing;
        const QColor base = f->kind == 1 ? QColor(230, 170, 80) : f->kind == 2 ? QColor(214, 190, 150) : QColor(170, 178, 192);
        const QColor fill(int(base.red() * shade), int(base.green() * shade), int(base.blue() * shade));
        QPolygonF poly;
        for (const Vec3& v : f->pts) poly << map(v);
        p.setPen(f->kind == 2 ? QPen(fill, 0.3) : QPen(fill.darker(160), 0.8));  // no outlines on model triangles
        p.setBrush(fill);
        p.drawPolygon(poly);
    }
    if (!onlyVolumes) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(240, 200, 70, 200), 1.0, Qt::DashLine));
        for (const Face& f : faces) {
            if (f.kind != 3) continue;
            QPolygonF poly;
            for (const Vec3& v : f.pts) poly << map(v);
            p.drawPolygon(poly);
        }
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(90, 200, 120));
    for (const Vec3& v : points) p.drawEllipse(map(v), 2.5, 2.5);
    return img;
}

void PrefabBrowser::refresh() {
    scanned_ = true;
    list_->clear();
    const QStringList roots = roots_ ? roots_() : QStringList();
    // Bundles first: each .iw8prefab folder is one entry with its manifest's title, description and tags.
    {
        std::vector<std::string> std_roots;
        for (const QString& r : roots) std_roots.push_back(r.toStdString());
        for (const PrefabBundleEntry& e : listPrefabBundles(std_roots)) {
            const PrefabBundle& b = e.bundle;
            const PrefabManifest& m = b.manifest;
            const QString folder = QDir::cleanPath(QString::fromStdString(b.folder));
            const QString title = QString::fromStdString(b.title);
            QStringList tags;
            for (const std::string& t : m.tags) tags << QString::fromStdString(t);
            QString reason;
            const QString badge = bundleBadge(b, &reason);
            auto* item = new QListWidgetItem(placeholderIcon(), title, list_);
            item->setData(kPath, folder);
            item->setData(kIsBundle, true);
            item->setData(kSearch, QStringList{title, tags.join(' '), QString::fromStdString(b.category), QString::fromStdString(m.id),
                                               QString::fromStdString(m.description), QString::fromStdString(e.model), "bundle"}
                                       .join('\n'));
            item->setData(kBadge, badge);
            item->setData(kBadgeReason, reason);
            item->setData(kPlaceable, b.placeable);
            item->setData(kThumbFile, QString::fromStdString(b.thumbFile));
            // The picture shows what a build makes: the fallback contents when they are the ones built.
            item->setData(kContents, b.placeable ? QString::fromStdString(choosePrefabContents(b, buildSupport()).mapFile) : QString());
            item->setData(kModelRoots, b.modelsDir.empty() ? QStringList() : QStringList{QString::fromStdString(b.modelsDir)});
            QString tip = "<qt><b>" + title.toHtmlEscaped() + "</b> (prefab bundle)";
            if (!m.description.empty()) tip += "<br>" + QString::fromStdString(m.description).toHtmlEscaped();
            if (!tags.isEmpty()) tip += "<br>Tags: " + tags.join(", ").toHtmlEscaped();
            if (!m.parameters.empty()) {
                QStringList names;
                for (const PrefabParameter& p : m.parameters) names << QString::fromStdString(p.label.empty() ? p.name : p.label);
                tip += "<br>Parameters: " + names.join("; ").toHtmlEscaped();
            }
            if (!reason.isEmpty()) tip += "<br><b>" + badge + ":</b> " + reason.toHtmlEscaped();
            for (const std::string& w : b.warnings) tip += "<br>Note: " + QString::fromStdString(w).toHtmlEscaped();
            tip += "<br>" + (QString::fromStdString(b.category) + " / " + QString::fromStdString(e.model)).toHtmlEscaped() + "<br>" +
                   QDir::toNativeSeparators(folder).toHtmlEscaped() + "</qt>";
            item->setToolTip(tip);
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setSizeHint(QSize(kThumb + 24, kThumb + 40));
            if (!b.placeable) {
                item->setForeground(QColor(140, 140, 140));
                item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
            }
        }
    }
    QStringList seen;
    for (const QString& root : roots) {
        if (!QFileInfo(root).isDir()) continue;
        QDirIterator it(root, {"*.map"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = QFileInfo(it.next()).absoluteFilePath();
            // The .map files inside a bundle folder belong to the bundle, which is listed above as one entry.
            if (f.contains(QString(kPrefabBundleSuffix) + "/", Qt::CaseInsensitive)) continue;
            if (seen.contains(f, Qt::CaseInsensitive)) continue;
            seen << f;
            // "category / name": the folder under the root names the category (doors, windows, ...).
            const QString rel = QDir(root).relativeFilePath(QFileInfo(f).absolutePath());
            const QString label = rel.isEmpty() || rel == "." ? QFileInfo(f).completeBaseName()
                                                              : rel + " / " + QFileInfo(f).completeBaseName();
            // The name under the picture is the prefab's own name; the category and path are in the tooltip.
            auto* item = new QListWidgetItem(placeholderIcon(), QFileInfo(f).completeBaseName(), list_);
            item->setData(Qt::UserRole, f);
            item->setData(Qt::UserRole + 2, label);
            item->setToolTip(label + "\n" + f);
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setSizeHint(QSize(kThumb + 24, kThumb + 40));
            // Thumbnail drawn later, only when the row is visible (loadVisibleThumbnails).
        }
    }
    setFilter(filter_->text());
    thumbTimer_->start();
}

void PrefabBrowser::loadVisibleThumbnails() {
    // A few parses per tick, visible rows first; stops when every visible row has its thumbnail.
    int done = 0;
    const QRect view = list_->viewport()->rect();
    for (int i = 0; i < list_->count() && done < 6; ++i) {
        QListWidgetItem* it = list_->item(i);
        if (it->isHidden() || it->data(Qt::UserRole + 1).toBool()) continue;
        if (!view.intersects(list_->visualItemRect(it))) continue;
        loadThumbnail(it);
        ++done;
    }
    if (done == 0) thumbTimer_->stop();
}

void PrefabBrowser::loadThumbnail(QListWidgetItem* it) {
    it->setData(kDrawn, true);
    // The textured picture of a .map file (fitted into the tile), or none yet (pending: a placeholder until it is made).
    const auto textured = [](const QString& mapFile, bool* pending) -> QImage {
        if (!g_pictureSource) return {};
        const QImage pic = g_pictureSource(mapFile, pending);
        if (pic.isNull()) return {};
        QImage base(kThumb, kThumb, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(32, 34, 38));
        const QImage fit = pic.scaled(kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter p(&base);
        p.drawImage((kThumb - fit.width()) / 2, (kThumb - fit.height()) / 2, fit);
        return base;
    };
    if (!it->data(kIsBundle).toBool()) {
        if (g_pictureSource) {
            bool pending = false;
            const QImage pic = textured(it->data(kPath).toString(), &pending);
            it->setData(kIncomplete, pending);
            it->setIcon(pic.isNull() ? placeholderIcon() : QIcon(QPixmap::fromImage(pic)));
            return;
        }
        const QImage thumb = thumbnail(it->data(kPath).toString(), kThumb);
        it->setData(kIncomplete, lastThumbnailIncomplete());
        if (!thumb.isNull()) it->setIcon(QIcon(QPixmap::fromImage(thumb)));
        return;
    }
    // A bundle: its thumb.png when it has one, else a picture drawn from its contents; the badge goes on top.
    QImage base;
    bool fromFile = false, incomplete = false;
    if (const QString file = it->data(kThumbFile).toString(); !file.isEmpty()) {
        const QImage png(file);
        if (!png.isNull()) {
            base = QImage(kThumb, kThumb, QImage::Format_ARGB32_Premultiplied);
            base.fill(QColor(32, 34, 38));
            const QImage fit = png.scaled(kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter p(&base);
            p.drawImage((kThumb - fit.width()) / 2, (kThumb - fit.height()) / 2, fit);
            fromFile = true;
        }
    }
    if (base.isNull())
        if (const QString contents = it->data(kContents).toString(); !contents.isEmpty()) {
            if (g_pictureSource) {
                base = textured(contents, &incomplete);
            } else {
                base = thumbnail(contents, kThumb, it->data(kModelRoots).toStringList());
                incomplete = lastThumbnailIncomplete();
            }
        }
    if (base.isNull()) {
        base = QImage(kThumb, kThumb, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(32, 34, 38));
    }
    it->setData(kFromFile, fromFile);
    it->setData(kIncomplete, incomplete);
    it->setIcon(QIcon(QPixmap::fromImage(withBadge(base, it->data(kBadge).toString(), true))));
}

bool PrefabBrowser::shownAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it && !it->isHidden();
}

bool PrefabBrowser::isBundleAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it && it->data(kIsBundle).toBool();
}

int PrefabBrowser::rowOfFile(const QString& file) const {
    const QString want = QDir::cleanPath(QFileInfo(file).absoluteFilePath());
    for (int i = 0; i < list_->count(); ++i)
        if (QDir::cleanPath(list_->item(i)->data(kPath).toString()).compare(want, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QString PrefabBrowser::titleAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it ? it->text() : QString();
}

QString PrefabBrowser::badgeAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it ? it->data(kBadge).toString() : QString();
}

QString PrefabBrowser::badgeReasonAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it ? it->data(kBadgeReason).toString() : QString();
}

QString PrefabBrowser::toolTipAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it ? it->toolTip() : QString();
}

bool PrefabBrowser::placeableAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it && (!it->data(kIsBundle).toBool() || it->data(kPlaceable).toBool());
}

bool PrefabBrowser::pictureFromFileAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it && it->data(kFromFile).toBool();
}

QImage PrefabBrowser::pictureAt(int row) {
    QListWidgetItem* it = list_->item(row);
    if (!it) return {};
    if (!it->data(kDrawn).toBool()) loadThumbnail(it);
    return it->icon().pixmap(kThumb, kThumb).toImage();
}

int PrefabBrowser::thumbnailsLoaded() const {
    int n = 0;
    for (int i = 0; i < list_->count(); ++i) n += list_->item(i)->data(Qt::UserRole + 1).toBool() ? 1 : 0;
    return n;
}

int PrefabBrowser::count() const { return list_->count(); }

QString PrefabBrowser::fileAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void PrefabBrowser::setFilter(const QString& text) {
    if (thumbTimer_) thumbTimer_->start();
    // Every word of the filter must be found (a bundle: in its title, tags, category, id or description).
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    int shown = 0, bundles = 0;
    for (int i = 0; i < list_->count(); ++i) {
        const QString hay = list_->item(i)->data(kSearch).toString();
        bool match = true;
        for (const QString& word : words) match = match && hay.contains(word, Qt::CaseInsensitive);
        list_->item(i)->setHidden(!match);
        shown += match;
        bundles += list_->item(i)->data(kIsBundle).toBool();
    }
    // The count line: everything listed, part of it, or why nothing is.
    const int total = list_->count();
    if (total == 0) count_->setText("No prefabs in the map's prefabs folder or the prefab folders (Edit > Preferences)");
    else if (shown == total) count_->setText(QString("%1 prefabs (%2 bundles)").arg(total).arg(bundles));
    else if (shown == 0) count_->setText(QString("No prefabs match \"%1\" (of %2)").arg(text.simplified()).arg(total));
    else count_->setText(QString("%1 of %2 shown").arg(shown).arg(total));
}

int PrefabBrowser::visibleCount() const {
    int n = 0;
    for (int i = 0; i < list_->count(); ++i) n += !list_->item(i)->isHidden();
    return n;
}

bool PrefabBrowser::stamp(int row, QString* err) {
    const QString f = fileAt(row);
    if (f.isEmpty() || !insert_) return false;
    if (!placeableAt(row)) {
        if (err) *err = badgeReasonAt(row);
        return false;
    }
    return insert_(f, err);
}

QStringList PrefabBrowser::searchRoots() {
    QStringList roots = Settings::instance().value("prefabRoots").toStringList();
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/prefabs"))) {
            roots << QDir::cleanPath(d.filePath("data/prefabs"));
            break;
        }
    return roots;
}

std::vector<std::string> PrefabBrowser::searchRootsStd() {
    std::vector<std::string> out;
    for (const QString& r : searchRoots()) out.push_back(r.toStdString());
    return out;
}

}  // namespace iw8r::qt
