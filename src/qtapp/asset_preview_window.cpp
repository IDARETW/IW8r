#include "asset_preview_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

#include "camera_view.h"
#include "convex_hull.h"
#include "map_text.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

// A box brush in map text, every face with `material`.
std::string boxBrush(const AABB& b, const std::string& material) {
    const std::string m = " " + material + " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    const auto p = [](double x, double y, double z) { return "( " + fmt(x) + " " + fmt(y) + " " + fmt(z) + " )"; };
    const double x0 = b.mins.x, y0 = b.mins.y, z0 = b.mins.z, x1 = b.maxs.x, y1 = b.maxs.y, z1 = b.maxs.z;
    return "{\n " + p(x1, y1, z0) + " " + p(x0, y1, z0) + " " + p(x0, y0, z0) + m +   // bottom
           " " + p(x0, y0, z1) + " " + p(x0, y1, z1) + " " + p(x1, y1, z1) + m +       // top
           " " + p(x0, y0, z1) + " " + p(x1, y0, z1) + " " + p(x1, y0, z0) + m +       // -y
           " " + p(x1, y1, z1) + " " + p(x0, y1, z1) + " " + p(x0, y1, z0) + m +       // +y
           " " + p(x1, y0, z1) + " " + p(x1, y1, z1) + " " + p(x1, y1, z0) + m +       // +x
           " " + p(x0, y1, z1) + " " + p(x0, y0, z1) + " " + p(x0, y0, z0) + m + "}\n";  // -x
}

// A model's bounds in map axes (the cache's Y-up file axes turned with (x, -z, y)), scaled.
AABB mapBounds(const ImportedModel& m, double scale) {
    AABB b;
    for (const auto& mesh : m.meshes)
        for (const Vec3& p : mesh.pos) b.add(Vec3{p.x, -p.z, p.y} * scale);
    return b;
}

// The floor under a box: a slab 8 units thick whose top is the box's bottom, twice as wide as the box.
std::string floorBrush(const AABB& asset) {
    const Vec3 c = asset.center();
    const double half = std::max({asset.maxs.x - asset.mins.x, asset.maxs.y - asset.mins.y, 64.0});
    AABB f;
    f.add(Vec3{c.x - half, c.y - half, asset.mins.z - 8});
    f.add(Vec3{c.x + half, c.y + half, asset.mins.z});
    return boxBrush(f, "iw8r/preview_floor");
}

}  // namespace

AssetPreviewWindow::AssetPreviewWindow(ID3D11Device* dev, ID3D11DeviceContext* dc, CameraView* hooksFrom,
                                       const app::SceneCache& sceneHooks, Sources sources, QWidget* parent)
    : QWidget(parent, Qt::Window), sources_(std::move(sources)) {
    setObjectName("assetPreviewWindow");
    setWindowTitle("Asset Preview");
    resize(1100, 720);
    // The scene hooks of the main view: tags, foliage, blend classes, prefabs, scriptable models; the model source is
    // this window's own (it picks the level of detail).
    ctx_.scene.tagsFor = sceneHooks.tagsFor;
    ctx_.scene.foliageFor = sceneHooks.foliageFor;
    ctx_.scene.drawClassFor = sceneHooks.drawClassFor;
    ctx_.scene.blendFor = sceneHooks.blendFor;
    ctx_.scene.opaqueFor = sceneHooks.opaqueFor;
    ctx_.scene.prefabSource = sceneHooks.prefabSource;
    ctx_.scene.scriptableModelFor = sceneHooks.scriptableModelFor;
    ctx_.scene.modelScale = sceneHooks.modelScale;
    modelFallback_ = sceneHooks.modelSource;
    ctx_.scene.modelSource = [this](const Entity& e) -> std::shared_ptr<const ImportedModel> {
        const std::string* m = e.value("model");
        if (!m || m->empty()) return nullptr;
        if (previewModel_ && qs(*m) == name_) return previewModel_;
        const int lod = lod_ ? std::max(0, lod_->currentIndex()) : 0;
        if (!prefab_ && sources_.model && qs(*m) == name_) {
            if (auto im = sources_.model(*m, lod)) return im;
        }
        return modelFallback_ ? modelFallback_(e) : nullptr;
    };
    ctx_.texturedView = true;
    ctx_.showSpawns = false;
    ctx_.gizmo = false;
    ctx_.scene.litPreview = true;

    view_ = new CameraView(&ctx_, dev, dc, this);
    view_->setObjectName("assetPreviewView");
    if (hooksFrom) view_->copyHooksFrom(*hooksFrom);
    view_->setOrbitMode(true);

    auto* side = new QVBoxLayout();
    mode_ = new QComboBox(this);
    mode_->setObjectName("assetPreviewMode");
    mode_->addItems({"Lit", "Game Lighting", "Textured (unlit)", "Wireframe", "Normals"});
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int i) { applyMode(i); });
    lod_ = new QComboBox(this);
    lod_->setObjectName("assetPreviewLod");
    connect(lod_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!prefab_) rebuildDocument();
    });
    floor_ = new QCheckBox("Floor", this);
    floor_->setObjectName("assetPreviewFloor");
    floor_->setChecked(true);
    connect(floor_, &QCheckBox::toggled, this, [this](bool) { rebuildDocument(); });
    hull_ = new QCheckBox("Collision hull (as built)", this);
    hull_->setObjectName("assetPreviewHull");
    hull_->setToolTip("The convex pieces the build gives this model as player collision (Compile: model collision hulls)");
    connect(hull_, &QCheckBox::toggled, this, [this](bool) { rebuildDocument(); });
    auto* frameButton = new QPushButton("Frame (F)", this);
    connect(frameButton, &QPushButton::clicked, this, [this] { frame(); });
    info_ = new QLabel(this);
    info_->setObjectName("assetPreviewInfo");
    info_->setWordWrap(true);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    materials_ = new QListWidget(this);
    materials_->setObjectName("assetPreviewMaterials");
    swatch_ = new QLabel(this);
    swatch_->setFixedSize(160, 160);
    swatch_->setAlignment(Qt::AlignCenter);
    swatch_->setStyleSheet("background: #202226; border: 1px solid #3a3d44; color: gray");
    connect(materials_, &QListWidget::currentTextChanged, this, [this](const QString& m) {
        if (onMaterialSelected) onMaterialSelected(m);
        const QImage img = sources_.materialImage && !m.isEmpty() ? sources_.materialImage(m.toStdString()) : QImage();
        if (img.isNull()) swatch_->setText("no image (yet)");
        else swatch_->setPixmap(QPixmap::fromImage(img.scaled(158, 158, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    });
    auto* form = new QHBoxLayout();
    form->addWidget(new QLabel("View", this));
    form->addWidget(mode_, 1);
    side->addLayout(form);
    auto* lodRow = new QHBoxLayout();
    lodRow->addWidget(new QLabel("Detail", this));
    lodRow->addWidget(lod_, 1);
    side->addLayout(lodRow);
    side->addWidget(floor_);
    side->addWidget(hull_);
    side->addWidget(frameButton);
    side->addWidget(info_);
    side->addWidget(new QLabel("Materials", this));
    side->addWidget(materials_, 1);
    auto* techsetButton = new QPushButton("Open in Techset Editor", this);
    side->addWidget(techsetButton);
    connect(techsetButton, &QPushButton::clicked, this, [this] {
        if (onEditTechset && materials_->currentItem()) onEditTechset(materials_->currentItem()->text());
    });
    side->addWidget(swatch_, 0, Qt::AlignHCenter);
    auto* help = new QLabel("Left drag: orbit · Middle or Shift+drag: pan · Wheel: zoom · F: frame · R: reset", this);
    help->setWordWrap(true);
    help->setStyleSheet("color: gray");
    side->addWidget(help);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(260);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(view_, 1);
    row->addWidget(sideWidget);
    connect(view_, &CameraView::orbitFrameRequested, this, [this] { frame(); });
}

AssetPreviewWindow::~AssetPreviewWindow() = default;

void AssetPreviewWindow::useGameLighting(){gameLightingPreview_=true;mode_->setCurrentIndex(1);applyMode(1);}

void AssetPreviewWindow::refreshHooks(CameraView* camera,const app::SceneCache& scene) {
    if(camera)view_->copyHooksFrom(*camera);
    ctx_.scene.tagsFor=scene.tagsFor;ctx_.scene.foliageFor=scene.foliageFor;
    ctx_.scene.drawClassFor=scene.drawClassFor;ctx_.scene.blendFor=scene.blendFor;ctx_.scene.opaqueFor=scene.opaqueFor;
    ctx_.scene.prefabSource=scene.prefabSource;ctx_.scene.scriptableModelFor=scene.scriptableModelFor;
    ctx_.scene.modelScale=scene.modelScale;modelFallback_=scene.modelSource;
    ctx_.scene.invalidate();view_->materialImagesChanged();
}
void AssetPreviewWindow::refreshAssetData(){const bool needsFrame=!assetBox_.valid();rebuildDocument();if(needsFrame)frame();}

void AssetPreviewWindow::showModel(const QString& name) {
    previewModel_.reset();
    if(gameLightingPreview_)mode_->setCurrentIndex(1);
    prefab_ = false;
    name_ = name;
    setWindowTitle("Asset Preview - " + name);
    lod_->blockSignals(true);
    lod_->clear();
    const int lods = sources_.lodCount ? std::max(1, sources_.lodCount(name.toStdString())) : 1;
    for (int i = 0; i < lods; ++i) lod_->addItem(i == 0 ? "LOD 0 (full)" : QString("LOD %1").arg(i));
    lod_->setEnabled(lods > 1);
    lod_->blockSignals(false);
    hull_->setEnabled(true);
    rebuildDocument();
    frame();
}

bool AssetPreviewWindow::showPrefab(const QString& path, QString* err) {
    previewModel_.reset();
    if(gameLightingPreview_)mode_->setCurrentIndex(1);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    prefabText_ = f.readAll().toStdString();
    prefabPath_ = path.toStdString();
    prefab_ = true;
    name_ = QFileInfo(path).completeBaseName();
    setWindowTitle("Asset Preview - prefab " + QFileInfo(path).fileName());
    lod_->blockSignals(true);
    lod_->clear();
    lod_->addItem("as placed");
    lod_->setEnabled(false);
    lod_->blockSignals(false);
    hull_->setEnabled(false);
    rebuildDocument();
    if (ctx_.doc.map().entities.empty()) {
        if (err) *err = "the prefab does not parse";
        return false;
    }
    frame();
    return true;
}

void AssetPreviewWindow::showShape(int shape) {
    auto model = std::make_shared<ImportedModel>();
    ImportedMesh mesh; mesh.material = "iw8r/techset_preview";
    const double radius = 64.0 / std::max(ctx_.scene.modelScale, 1e-6);
    const auto vertex = [&](Vec3 p, Vec3 n, double u, double v) {
        mesh.pos.push_back(p * radius); mesh.normal.push_back(n); mesh.uv.push_back({u,v});
    };
    if (shape == 0) {
        constexpr int rows=24, cols=48;
        for(int y=0;y<=rows;++y) for(int x=0;x<=cols;++x) {
            const double u=double(x)/cols,v=double(y)/rows,phi=u*6.28318530718,theta=v*3.14159265359;
            Vec3 n{std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)}; vertex(n,n,u,1-v);
        }
        for(int y=0;y<rows;++y) for(int x=0;x<cols;++x) {
            const uint32_t a=uint32_t(y*(cols+1)+x),b=a+cols+1;
            mesh.idx.insert(mesh.idx.end(),{a,b,a+1,a+1,b,b+1});
        }
    } else {
        const Vec3 normals[6]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        for(int face=0;face<(shape==1?1:6);++face) {
            const Vec3 n=normals[face],u=std::abs(n.y)<0.9?normalize(cross(Vec3{0,1,0},n)):Vec3{1,0,0},v=cross(n,u);
            const auto first=uint32_t(mesh.pos.size());const Vec3 center=shape==1?Vec3{}:n;
            vertex(center-u-v,n,0,0);vertex(center+u-v,n,1,0);vertex(center+u+v,n,1,1);vertex(center-u+v,n,0,1);
            mesh.idx.insert(mesh.idx.end(),{first,first+1,first+2,first,first+2,first+3});
        }
    }
    for(const auto& p:mesh.pos) model->bounds.add(p);
    model->meshes.push_back(std::move(mesh));previewModel_=model;prefab_=false;name_="iw8r_preview_shape";
    {QSignalBlocker b(floor_);floor_->setChecked(false);}
    {QSignalBlocker b(lod_);lod_->clear();lod_->addItem("Preview shape");lod_->setEnabled(false);}
    hull_->setChecked(false);rebuildDocument();mode_->setCurrentIndex(1);frame();
}
void AssetPreviewWindow::showEffect(const QString& name) {
    previewModel_.reset();prefab_=true;name_=name;prefabPath_.clear();
    prefabText_="iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"fx_origin\"\n\"fx\" \""+name.toStdString()+"\"\n\"origin\" \"0 0 0\"\n}\n";
    rebuildDocument();assetBox_=AABB{};assetBox_.add(Vec3{-64,-64,-64});assetBox_.add(Vec3{64,64,128});mode_->setCurrentIndex(1);frame();
}

AABB AssetPreviewWindow::assetBounds() const { return assetBox_; }

void AssetPreviewWindow::rebuildDocument() {
    std::string text;
    std::set<std::string> materials;
    size_t triangles = 0;
    assetBox_ = AABB{};
    if (prefab_) {
        text = prefabText_;
        // The floor: under the prefab's contents, measured once without it.
        if (floor_->isChecked()) {
            std::string err;
            ctx_.doc.loadFromText(text, prefabPath_, &err);
            ctx_.scene.invalidate();
            ctx_.scene.sync(ctx_.doc);
            AABB b;
            for (const app::PrimGeom* g : ctx_.scene.items())
                if (g->bounds.valid()) b.add(g->bounds);
            assetBox_ = b;
            const std::string ws = "\"classname\" \"worldspawn\"";
            const size_t at = text.find(ws);
            if (b.valid() && at != std::string::npos) {
                const size_t eol = text.find('\n', at);
                if (eol != std::string::npos) text.insert(eol + 1, floorBrush(b));
            }
        }
    } else {
        const std::string model = name_.toStdString();
        const int lod = std::max(0, lod_->currentIndex());
        std::shared_ptr<const ImportedModel> m = previewModel_ ? previewModel_ : sources_.model ? sources_.model(model, lod) : nullptr;
        const double scale = ctx_.scene.modelScale > 0 ? ctx_.scene.modelScale : 1.0;
        if (m) assetBox_ = mapBounds(*m, scale);  // the framing (the model alone, without the floor)
        text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
        if (m && floor_->isChecked()) text += floorBrush(mapBounds(*m, scale));
        text += "}\n{\n\"classname\" \"misc_model\"\n\"model\" \"" + model + "\"\n\"origin\" \"0 0 0\"\n}\n";
        if (m && hull_->isChecked()) {
            // The build's pieces (compile_iw8.cpp piecesOf: 4 pieces, 24 planes) in map axes.
            std::vector<std::vector<Plane>> pieces = convexPieces(*m, 4, 24);
            for (auto& piece : pieces)
                for (Plane& pl : piece) pl.n = Vec3{pl.n.x, -pl.n.z, pl.n.y};
            const std::vector<Primitive> brushes = hullBrushes(pieces, Vec3{}, Vec3{}, scale, "clip", "\n");
            if (!brushes.empty()) {
                text += "{\n\"classname\" \"func_group\"\n";
                for (const Primitive& p : brushes) text += p.text();
                text += "}\n";
            }
        }
        if (m)
            for (const auto& mesh : m->meshes) {
                triangles += mesh.idx.size() / 3;
                if (!mesh.material.empty()) materials.insert(mesh.material);
            }
    }
    std::string err;
    ctx_.doc.loadFromText(text, prefab_ ? prefabPath_ : std::string(), &err);
    ctx_.scene.invalidate();
    ctx_.scene.sync(ctx_.doc);
    if (prefab_) {
        for (const Entity& e : ctx_.doc.map().entities)
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) {
                    for (const auto& face : p->faces)
                        if (face.material() != "iw8r/preview_floor") materials.insert(face.material());
                    if (!p->patchMaterial.empty()) materials.insert(p->patchMaterial);
                }
        // The prefab's models: their meshes' materials and triangles.
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.value("model") && ctx_.scene.modelSource)
                if (const auto m = ctx_.scene.modelSource(e))
                    for (const auto& mesh : m->meshes)
                        if (!mesh.material.empty()) materials.insert(mesh.material);
        for (const app::PrimGeom* g : ctx_.scene.items()) triangles += g->polys.size();
        if (!assetBox_.valid())
            for (const app::PrimGeom* g : ctx_.scene.items())
                if (g->bounds.valid()) assetBox_.add(g->bounds);
    }
    materials_->clear();
    for (const std::string& m : materials) materials_->addItem(qs(m));
    view_->materialImagesChanged();
    ctx_.cameraMoved();
    updateInfo();
    info_->setProperty("triangles", qulonglong(triangles));
    info_->setText(info_->text().replace("%TRIS%", QString::number(triangles)));
}

void AssetPreviewWindow::updateInfo() {
    const AABB b = assetBounds();
    QString text = "<b>" + name_.toHtmlEscaped() + "</b><br>";
    if (prefab_) {
        size_t models = 0, brushes = 0, entities = 0;
        for (const Entity& e : ctx_.doc.map().entities) {
            if (!e.isWorldspawn()) ++entities;
            if (e.value("model")) ++models;
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush) ++brushes;
        }
        text += QString("prefab: %1 entities, %2 models, %3 brushes<br>").arg(entities).arg(models).arg(brushes - (floor_->isChecked() ? 1 : 0));
    }
    text += "%TRIS% triangles<br>";
    if (b.valid())
        text += QString("%1 x %2 x %3 units").arg(b.maxs.x - b.mins.x, 0, 'f', 1).arg(b.maxs.y - b.mins.y, 0, 'f', 1).arg(b.maxs.z - b.mins.z, 0, 'f', 1);
    info_->setText(text);
}

QString AssetPreviewWindow::infoText() const { return info_->text(); }
int AssetPreviewWindow::materialCount() const { return materials_->count(); }

void AssetPreviewWindow::applyMode(int mode) {
    ctx_.scene.gameLook = mode == 1;
    ctx_.scene.litPreview = mode == 0 || mode == 1;
    ctx_.texturedView = mode != 3;
    ctx_.scene.cameraMode = mode == 3 ? app::CameraMode::Wireframe : mode == 4 ? app::CameraMode::Normals : app::CameraMode::Textured;
    ctx_.scene.invalidate();
    view_->materialImagesChanged();
    ctx_.cameraMoved();
}

void AssetPreviewWindow::setPictureStyle() {
    floor_->blockSignals(true);
    floor_->setChecked(false);
    floor_->blockSignals(false);
    // The Textured view (owner 2026-11-14: textures only, no Lit or Game Lighting).
    mode_->setCurrentIndex(2);
    applyMode(2);
    frameScale_ = 0.8;  // a picture: the asset fills the tile (the bounding sphere is loose around most shapes)
}

QImage AssetPreviewWindow::picture(int side) { return view_->renderImage(side, side, true, 1.0, 0, false); }

int AssetPreviewWindow::texturesWaiting() const { return view_->texturesWaiting(); }

void AssetPreviewWindow::materialsChanged() { view_->materialImagesChanged(); }

QStringList AssetPreviewWindow::modelNames() const {
    QStringList out;
    for (const Entity& e : ctx_.doc.map().entities)
        if (const std::string* m = e.value("model"); m && !m->empty() && (*m)[0] != '*') out << qs(*m);
    out.removeDuplicates();
    return out;
}

void AssetPreviewWindow::frame() {
    const AABB b = assetBounds();
    if (!b.valid()) {
        view_->setOrbit(Vec3{0, 0, 32}, 256, 45, -25);
        return;
    }
    const Vec3 c = b.center();
    const double radius = std::max(8.0, 0.5 * length(b.maxs - b.mins));
    const double halfFov = std::max(10.0, ctx_.cam.fov * 0.5) * 3.14159265358979 / 180.0;
    if (frameScale_ < 1.0) {
        // A square picture: the nearest distance at which all eight corners of the box are inside the view (a sphere
        // fit leaves long assets small or cuts their ends), with a small margin.
        app::Camera probe = ctx_.cam;
        probe.yaw = 45, probe.pitch = -22;
        const Vec3 f = probe.forward(), r = probe.right(), u = probe.up();
        const double t = std::tan(halfFov);
        double d = 8.0;
        for (int k = 0; k < 8; ++k) {
            const Vec3 p = Vec3{k & 1 ? b.maxs.x : b.mins.x, k & 2 ? b.maxs.y : b.mins.y, k & 4 ? b.maxs.z : b.mins.z} - c;
            const double depth = dot(p, f);  // along the view from the centre (negative: toward the camera, needs more room)
            d = std::max({d, std::fabs(dot(p, r)) / t - depth, std::fabs(dot(p, u)) / t - depth});
        }
        view_->setOrbit(c, d * 1.08, 45, -22);
        return;
    }
    view_->setOrbit(c, radius / std::sin(halfFov) * frameScale_, 45, -22);
}

}  // namespace iw8r::qt
