#include "material_tints.h"
#include "worker_guard.h"
#include "camera_view.h"
#include "bake_options.h"
#include <atomic>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include "foliage_sway.h"
#include <QTimer>
#include <QTimerEvent>
#include "tool_commands.h"
#include "sky_image.h"
#include "sky_sun.h"
#include "prefab.h"
#include "scriptable_catalog.h"
#include "asset_browser.h"
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDateTime>
#include "atmosphere.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <QCursor>
#include <QMetaObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "brush_tools.h"
#include "compile_iw8.h"
#include "custom_materials.h"
#include "editor_log.h"
#include <unordered_set>
#include "ray_bvh.h"
#include "light_grid.h"
#include "lightmap_bake.h"
#include "normal_pack.h"
#include "texture_decode.h"
#include "game_look.h"
#include "game_look_assets.h"
#include <QPointer>
#include <thread>

namespace iw8r::qt {

namespace {
const float kClear[4] = {0.11f, 0.12f, 0.14f, 1.0f};

bool isCameraKey(int k) {
    switch (k) {
        case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Comma: case Qt::Key_Period: case Qt::Key_A: case Qt::Key_Z: case Qt::Key_D: case Qt::Key_C:
        case Qt::Key_W: case Qt::Key_S: case Qt::Key_Q: case Qt::Key_E:
            return true;
        default:
            return false;
    }
}
}  // namespace

CameraView::CameraView(EditorContext* ctx, ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent)
    : QWidget(parent), ctx_(ctx), dev_(dev), dc_(dc) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(64, 64);
    std::string err;
    rendererOk_ = dev_ && renderer_.init(dev_, dc_, &err);
    // A scriptable without a model key draws its stock definition's first model (read-only catalogue lookup).
    if (!ctx_->scene.scriptableModelFor)
        ctx_->scene.scriptableModelFor = [](const std::string& definition) -> std::string {
            const ScriptableDefinition* d = ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString()).find(definition);
            return d ? d->model : std::string();
        };
    connect(ctx_, &EditorContext::documentChanged, this, &CameraView::requestRender);
    connect(ctx_, &EditorContext::cameraChanged, this, &CameraView::requestRender);
}

CameraView::~CameraView() {
    if (swap_) swap_->Release();
}

void CameraView::ensureSwapChain() {
    if (!rendererOk_) return;
    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, int(width() * dpr)), h = std::max(1, int(height() * dpr));
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!IsWindow(hwnd)) return;  // e.g. the offscreen platform used by --gui-test
    if (swap_ && swapHwnd_ != hwnd) {
        dc_->OMSetRenderTargets(0, nullptr, nullptr);
        swap_->Release();
        swap_ = nullptr;
    }
    if (!swap_) {
        swapHwnd_ = hwnd;
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(dev_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev))) &&
            SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) {
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 2;
            sd.BufferDesc.Width = UINT(w);
            sd.BufferDesc.Height = UINT(h);
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd;
            sd.SampleDesc.Count = 1;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            factory->CreateSwapChain(dev_, &sd, &swap_);
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }
        if (factory) factory->Release();
        if (adapter) adapter->Release();
        if (dxgiDev) dxgiDev->Release();
        swapW_ = w;
        swapH_ = h;
    } else if (w != swapW_ || h != swapH_) {
        dc_->OMSetRenderTargets(0, nullptr, nullptr);
        swap_->ResizeBuffers(0, UINT(w), UINT(h), DXGI_FORMAT_UNKNOWN, 0);
        swapW_ = w;
        swapH_ = h;
    }
}

void CameraView::render() {
    if (!rendererOk_) return;
    IW8R_PROF("r.total");
    {
        IW8R_PROF("r.sync");
        ctx_->scene.sync(ctx_->doc);
    }
    const qreal dpr = devicePixelRatioF();
    const int w = captureW_ > 0 ? captureW_ : fixedW_ > 0 ? fixedW_ : std::max(1, int(width() * dpr));
    const int h = captureH_ > 0 ? captureH_ : fixedH_ > 0 ? fixedH_ : std::max(1, int(height() * dpr));
    renderer_.resize(w, h);
    if (texInstDirty_) {
        IW8R_PROF("r.up_texinst");
        renderer_.setReferenceTexInstances(pendingTexInst_);
        pendingTexInst_.clear();
        pendingTexInst_.shrink_to_fit();
        texInstResolved_.assign(texInstMaterials_.size(), false);
        texInstDirty_ = false;
        texInstPending_ = refPollDue_ = true;
    }
    if (texLightDirty_) {
        IW8R_PROF("r.up_texlight");
        for (size_t i = 0; i < pendingTexLight_.size(); ++i)
            if (!pendingTexLight_[i].empty()) renderer_.setReferenceTexInstanceLight(i, pendingTexLight_[i]);
        pendingTexLight_.clear();
        pendingTexLight_.shrink_to_fit();
        texLightDirty_ = false;
    }
    if (probesDirty_) {
        IW8R_PROF("r.up_probes");
        renderer_.setStockProbes(pendingProbes_.get());
        pendingProbes_.reset();
        probesDirty_ = false;
    }
    if (stockLmDirty_) {
        IW8R_PROF("r.up_lightmap");
        const auto& l = pendingStockLm_;
        if (l && l->width > 0) renderer_.setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data(), l->rects);
        else renderer_.setStockLightmap(0, 0, nullptr, nullptr, nullptr);
        stockMeanLight_ = l ? l->meanIrradiance : std::array<float, 3>{0, 0, 0};
        pendingStockLm_.reset();
        stockLmDirty_ = false;
    }
    if (instDirty_) {
        IW8R_PROF("r.up_inst");
        renderer_.setReferenceInstances(pendingInst_);
        pendingInst_.clear();
        pendingInst_.shrink_to_fit();
        instDirty_ = false;
    }
    if (refDirty_) {
        IW8R_PROF("r.up_ref");
        renderer_.setReference(pendingRef_);
        pendingRef_.clear();
        pendingRef_.shrink_to_fit();
    }
    if (refTexDirty_) {
        IW8R_PROF("r.up_reftex");
        refTexDirty_ = false;
        std::vector<std::vector<app::GpuTexVertex>> batches;
        refTexMaterials_.clear();
        // Nearest material batches first: textures are requested and loaded in this order (a stock map has ~2,000).
        {
            const Vec3 eye = ctx_->cam.pos;
            std::vector<std::pair<double, size_t>> dist;
            for (size_t k = 0; k < pendingRefTex_.size(); ++k) {
                double best = 1e300;
                const auto& t = pendingRefTex_[k].tris;
                for (size_t i = 0; i < t.size(); i += std::max<size_t>(1, t.size() / 64)) {
                    const double dx = t[i].x - eye.x, dy = t[i].y - eye.y, dz = t[i].z - eye.z;
                    best = std::min(best, dx * dx + dy * dy + dz * dz);
                }
                dist.push_back({best, k});
            }
            std::stable_sort(dist.begin(), dist.end());
            std::vector<app::ReferenceTexBatch> sorted;
            sorted.reserve(pendingRefTex_.size());
            for (const auto& [d, k] : dist) sorted.push_back(std::move(pendingRefTex_[k]));
            pendingRefTex_ = std::move(sorted);
        }
        refTexLayer_.clear();
        refTexVolume_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            refTexLayer_.push_back(b.layer);
            RefDecalVolume dv;
            dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            dv.collection = b.decalVolume ? b.decalCollection : -1;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);
            batches.push_back(std::move(b.tris));
        }
        renderer_.setReferenceTextured(batches, &refTexLayer_);
        refTexExtraFrom_ = ~size_t(0);
        extraMeta_.clear();  // the renderer dropped the extras with the old reference
        decalExtrasDirty_ = decalExtrasDirty_ || !pendingExtraGroups_.empty();
        refTexResolved_.assign(refTexMaterials_.size(), false);
        refTexPending_ = refPollDue_ = true;
        pendingRefTex_.clear();
        pendingRefTex_.shrink_to_fit();
    }
    if (decalExtrasDirty_ && !refTexDirty_) {
        IW8R_PROF("r.up_extras");
        // The extras after the world's batches, group by group: a group with the key and version it had keeps its
        // upload and its batches' state; the others are uploaded (docs/PLAN_REMAINING.md 1.1).
        decalExtrasDirty_ = false;
        const size_t from = refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexMaterials_.size() ? refTexExtraFrom_ : refTexMaterials_.size();
        struct OldSlice {
            size_t first = 0, count = 0;
            uint64_t version = 0;
        };
        std::map<uint64_t, OldSlice> oldAt;
        {
            size_t at = from;
            for (const ExtraMeta& m : extraMeta_) oldAt[m.key] = {at, m.count, m.version}, at += m.count;
        }
        const std::vector<std::string> oMat(refTexMaterials_.begin() + std::ptrdiff_t(from), refTexMaterials_.end());
        const std::vector<int> oLay(refTexLayer_.begin() + std::ptrdiff_t(std::min(from, refTexLayer_.size())), refTexLayer_.end());
        const std::vector<RefDecalVolume> oVol(refTexVolume_.begin() + std::ptrdiff_t(std::min(from, refTexVolume_.size())), refTexVolume_.end());
        const std::vector<bool> oRes(refTexResolved_.begin() + std::ptrdiff_t(std::min(from, refTexResolved_.size())), refTexResolved_.end());
        refTexMaterials_.resize(from), refTexLayer_.resize(from), refTexVolume_.resize(from), refTexResolved_.resize(from);
        std::vector<app::SceneRenderer::ExtraGroupInput> in;
        std::vector<std::vector<std::vector<app::GpuTexVertex>>> keep;
        keep.reserve(pendingExtraGroups_.size());
        std::vector<ExtraMeta> meta;
        for (const ExtraGroup& g : pendingExtraGroups_) {
            const size_t n = g.batches ? g.batches->size() : 0;
            if (n == 0) continue;
            app::SceneRenderer::ExtraGroupInput gi;
            gi.key = g.key, gi.version = g.version, gi.world = g.world, gi.nearOnly = g.nearOnly;
            const auto o = oldAt.find(g.key);
            if (o != oldAt.end() && o->second.version == g.version && o->second.count == n) {
                const size_t s0 = o->second.first - from;
                for (size_t j = 0; j < n; ++j) {
                    refTexMaterials_.push_back(s0 + j < oMat.size() ? oMat[s0 + j] : std::string());
                    refTexLayer_.push_back(s0 + j < oLay.size() ? oLay[s0 + j] : 0);
                    refTexVolume_.push_back(s0 + j < oVol.size() ? oVol[s0 + j] : RefDecalVolume{});
                    refTexResolved_.push_back(s0 + j < oRes.size() ? bool(oRes[s0 + j]) : false);
                }
            } else {
                keep.emplace_back();
                auto& vb = keep.back();
                for (const auto& b : *g.batches) {
                    refTexMaterials_.push_back(b.material);
                    RefDecalVolume dv;
                    if (g.world) {  // world batches: base / layer / decal volume as in the main reference
                        refTexLayer_.push_back(b.layer);
                        dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
                    } else {
                        refTexLayer_.push_back(0);
                        dv.on = true, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
                        dv.terrainLayer = b.terrainLayer, dv.flags = b.terrainFlags, dv.k = b.terrainK, dv.terrainBase = b.terrainBase;
                    }
                    for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
                    dv.collection = b.terrainLayer ? -1 : b.decalCollection;
                    refTexVolume_.push_back(dv);
                    refTexResolved_.push_back(false);
                    vb.push_back(b.tris);
                    gi.layers.push_back(g.world ? b.layer : 0);
                }
                gi.batches = &vb;
            }
            in.push_back(std::move(gi));
            meta.push_back({g.key, g.version, n});
        }
        renderer_.setReferenceExtras(in);
        refTexExtraFrom_ = from;
        extraMeta_ = std::move(meta);
        refTexPending_ = refPollDue_ = true;
    }
    // Per-frame texture budget, reset before the reference loop so it gets loads after a busy frame too.
    loadsThisFrame_ = 0;
    moreToLoad_ = false;
    // Reference materials still waiting for an image. They are asked again when an image arrived
    // (materialImagesChanged), on the next frame while loads are spread over frames (the per-frame budget), and
    // otherwise twice a second: not on every frame, and without asking for a new frame each time.
    static const bool refPollAlways = app::perfOff("refpoll");  // benchmark switch: ask on every frame, as before
    if ((refTexPending_ || texInstPending_) && (refPollDue_ || refPollAlways || !refPoll_.isValid() || refPoll_.elapsed() >= 500)) {
        IW8R_PROF("r.ref_textures");
        refPoll_.start();
        refPollDue_ = false;
        if (!refTexMaterials_.empty() && imageLookup_ && refTexPending_) {
            // A few stock textures per frame (the same loader and budget as the map's own surfaces).
            bool pending = false;
            for (size_t k = 0; k < refTexMaterials_.size(); ++k) {
                if (refTexResolved_[k]) continue;
                if (k < refTexLayer_.size() && refTexLayer_[k] > 0) {
                    if (resolveReferenceLayer(k)) refTexResolved_[k] = true;
                    else pending = true;
                    continue;
                }
                if (k < refTexVolume_.size() && refTexVolume_[k].on) {
                    // A decal volume: its colour and mask images, then drawn with the decals (no normal map).
                    const RefDecalVolume& dv = refTexVolume_[k];
                    const int cid = textureFor(refTexMaterials_[k]);
                    const std::string mk = dv.mask.empty() ? std::string() : "\x05" + dv.mask;
                    const int mid = mk.empty() ? -1 : textureFor(mk);
                    if (!texIds_.count(refTexMaterials_[k]) || (!mk.empty() && !texIds_.count(mk))) {
                        pending = true;
                        continue;
                    }
                    refTexResolved_[k] = true;
                    if (cid < 0 || cid == checkerTex_) continue;  // no colour image: left out
                    if (dv.terrainLayer) {
                        // A terrain layer: the layer mode (weight in the tangent length, reveal ramp / alpha by its flags),
                        // under the world's own decals (sort key 1).
                        const float p[16] = {1.0f, float(dv.flags), dv.k, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                                             1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, dv.terrainBase ? 1.0f : 0.0f, 1.0f};  // [15] = 1: a terrain layer (no probe reflection)
                        renderer_.setReferenceTexture(k, cid);
                        renderer_.setReferenceDecal(k, 1, mid == checkerTex_ ? -1 : mid, p);
                        continue;
                    }
                    const float p[16] = {2.0f, dv.a, dv.b, 0.0f, dv.tint[0], dv.tint[1], dv.tint[2], 0.0f,
                                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    renderer_.setReferenceTexture(k, cid);
                    renderer_.setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    renderer_.setReferenceDecalCollection(k, dv.collection);
                    continue;
                }
                const int id = textureFor(refTexMaterials_[k]);
                if (texIds_.count(refTexMaterials_[k])) {
                    // A stock image this install does not hold (catalogued only in a zone without its payload): plain grey on
                    // the read-only stock reference rather than the missing-material checker (mp_m_fork, mp_m_speed).
                    if (id >= 0) renderer_.setReferenceTexture(k, id == checkerTex_ ? -1 : id);
                    // Its normal map next (normalFor waits for the colour image and the per-frame budget).
                    const int nid = id >= 0 && id != checkerTex_ ? normalFor(refTexMaterials_[k]) : -1;
                    if (id >= 0 && id != checkerTex_ && !nrmIds_.count(refTexMaterials_[k])) {
                        pending = true;
                        continue;
                    }
                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    if (ctx_ && ctx_->scene.drawClassFor) renderer_.setReferenceDrawClass(k, ctx_->scene.drawClassFor(refTexMaterials_[k]));
                    if (factsLookup_)
                        if (const auto f = factsLookup_(refTexMaterials_[k])) renderer_.setReferenceNoCast(k, f->noCastShadow);
                    setBaseMaterialConstants(k);
                    if (!applyMaterialExtras(refTexMaterials_[k], k, false)) {
                        pending = true;
                        continue;
                    }
                    if (id >= 0 && id != checkerTex_ && !resolveReferenceDecal(k, id)) {
                        pending = true;
                        continue;
                    }
                    refTexResolved_[k] = true;
                } else {
                    pending = true;
                }
            }
            refTexPending_ = pending;
            refDirty_ = false;
        }
        if (!texInstMaterials_.empty() && imageLookup_ && texInstPending_) {
            // Textures of the reference props, with the same loader and per-frame budget.
            bool pending = false;
            for (size_t k = 0; k < texInstMaterials_.size(); ++k) {
                if (texInstResolved_[k]) continue;
                const int id = textureFor(texInstMaterials_[k]);
                if (texIds_.count(texInstMaterials_[k])) {
                    if (id >= 0) renderer_.setReferenceInstTexture(k, id == checkerTex_ ? -1 : id);  // as the world: grey, not the checker
                    // Its normal map (Game Lighting: gloss, cavity, relief) and the material's constants.
                    const int nid = id >= 0 && id != checkerTex_ ? normalFor(texInstMaterials_[k]) : -1;
                    if (id >= 0 && id != checkerTex_ && !nrmIds_.count(texInstMaterials_[k])) {
                        pending = true;
                        continue;
                    }
                    if (nid >= 0) renderer_.setReferenceInstNormal(k, nid);
                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_.setReferenceInstParams(k, p->data());
                    if (factsLookup_)
                        if (const auto f = factsLookup_(texInstMaterials_[k]))
                            renderer_.setReferenceInstDecal(k, f->propDecalSortKey), renderer_.setReferenceInstNoCast(k, f->noCastShadow);
                    if (ctx_ && ctx_->scene.drawClassFor) renderer_.setReferenceInstDrawClass(k, ctx_->scene.drawClassFor(texInstMaterials_[k]));
                    if (!applyMaterialExtras(texInstMaterials_[k], k, true)) {
                        pending = true;
                        continue;
                    }
                    texInstResolved_[k] = true;
                } else {
                    pending = true;
                }
            }
            texInstPending_ = pending;
        }
        // The stock reference's loads do not rebuild the map's own geometry: only its own textures do (mp_aniyah_tac
        // rebuilt everything, 175 ms and 173 MB of uploads, on every frame while 9,000 layer textures streamed in).
        refMoreToLoad_ = moreToLoad_;
        moreToLoad_ = false;
        if (refMoreToLoad_) {
            refPollDue_ = true;  // the budget stopped the loads: the next frame goes on (render() asks for it below)
        } else if ((refTexPending_ || texInstPending_) && !refPollTimerArmed_) {
            // Waiting for images that are not cached yet: look again in half a second.
            refPollTimerArmed_ = true;
            QTimer::singleShot(500, this, [this] {
                refPollTimerArmed_ = false;
                update();
            });
        }
    }
    renderer_.setReferenceVisible(ctx_->scene.cameraMode != app::CameraMode::Wireframe);
    {
        IW8R_PROF("r.atmo");
        // The worldspawn settings (fog, film grade, LUT path, wind) are parsed when the document changes, not every
        // frame. Files on disk (the grade LUT, captured probes, the sky image) are looked at again when the document
        // changes, when a frame follows a pause, and at most once a second while frames follow each other.
        static const bool noSettingsCache = app::perfOff("settings");  // benchmark switch: parse and look at files every frame
        if (noSettingsCache) settingsRev_ = 0, filesDue_ = true;
        bool docChanged = false, lutPathChanged = false;
        if (settingsRev_ != ctx_->doc.revision() || settingsPath_ != ctx_->doc.path()) {
            docChanged = true;
            const std::string lutBefore = lutPath_;
            settingsRev_ = ctx_->doc.revision();
            settingsPath_ = ctx_->doc.path();
            atmo_ = Atmosphere{};
            wind_ = WindSettings{};
            if (const Entity* ws = ctx_->doc.worldspawn()) {
                atmo_ = readAtmosphere(*ws);
                if (auto r = readWind(*ws)) wind_ = *r;
            }
            lutPath_ = atmo_.lut;
            if (!lutPath_.empty() && QFileInfo(QString::fromStdString(lutPath_)).isRelative() && !ctx_->doc.path().empty())
                lutPath_ = QFileInfo(QString::fromStdString(ctx_->doc.path())).absoluteDir().filePath(QString::fromStdString(lutPath_)).toStdString();
            lutPathChanged = lutPath_ != lutBefore;
        }
        const bool poll = filesDue_ || !frameGap_.isValid() || frameGap_.elapsed() > 100 || !filePoll_.isValid() || filePoll_.elapsed() > 1000;
        frameGap_.start();
        if (poll) {
            filePoll_.start();
            filesDue_ = false;
            scanProbes();
        }
        if (poll || lutPathChanged) lutMtime_ = lutPath_.empty() ? 0 : QFileInfo(QString::fromStdString(lutPath_)).lastModified().toMSecsSinceEpoch();
        pollFiles_ = poll || docChanged;  // the sky image is named by the worldspawn: asked again when the document changes
        // Fog preview from worldspawn iw8r_fog (Compile > Atmosphere), the same 50%-at-halfway curve as setExpFog.
        const float eye[3] = {float(ctx_->cam.pos.x), float(ctx_->cam.pos.y), float(ctx_->cam.pos.z)};
        const ExpFog* fog = ctx_->fogPreview && atmo_.fog ? &*atmo_.fog : nullptr;
        const float col[3] = {fog ? float(fog->color.x) : 0.f, fog ? float(fog->color.y) : 0.f, fog ? float(fog->color.z) : 0.f};
        renderer_.setFog(eye, fog ? float(fog->start) : 0.f, fog ? float(fog->halfway) : 0.f, col);
        renderer_.setPixelAngle(float(2.0 * std::tan(ctx_->cam.fov * 3.14159265358979 / 360.0) / std::max(1, fixedW_ > 0 ? fixedW_ : width())));
        // Colour grade preview (3.15): iw8r_film and the iw8r_lut .cube of the open map (Compile > Atmosphere).
        const FilmGrade* film = ctx_->gradePreview && atmo_.film ? &*atmo_.film : nullptr;
        const float lt[3] = {film ? float(film->lightTint.x) : 1.f, film ? float(film->lightTint.y) : 1.f, film ? float(film->lightTint.z) : 1.f};
        const float dt[3] = {film ? float(film->darkTint.x) : 1.f, film ? float(film->darkTint.y) : 1.f, film ? float(film->darkTint.z) : 1.f};
        renderer_.setGrade(film && !film->isIdentity(), film ? float(film->contrast) : 1.f, film ? float(film->brightness) : 0.f,
                           film ? float(film->desaturation) : 0.f, film && film->invert, lt, dt);
        {
            IW8R_PROF("r.probe");
            updateReflectionProbe();
        }
        const bool lutOn = ctx_->gradePreview && !lutPath_.empty();
        if (lutOn != lutLoadedOn_ || (lutOn && (lutPath_ != lutLoadedPath_ || lutMtime_ != lutLoadedMtime_))) {
            lutLoadedOn_ = lutOn;
            lutLoadedPath_ = lutPath_;
            lutLoadedMtime_ = lutMtime_;
            std::string text, err;
            int size = 0;
            std::vector<uint8_t> rgba;
            if (lutOn && readFile(lutPath_, text) && parseCubeLut(text, size, rgba, &err)) renderer_.setGradeLut(size, rgba.data());
            else renderer_.setGradeLut(0, nullptr);
        }
    }
    // Geometry is rebuilt only when something it depends on changed (9.4): the scene, the selection, the shading and
    // overlay settings. Camera moves alone reuse it; gizmo lines are added on top every frame.
    uint64_t key = 1469598103934665603ull;
    const auto mix = [&key](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
    mix(ctx_->scene.generation());
    mix(ctx_->doc.revision());
    for (const auto& r : ctx_->doc.selection()) mix(r.entity * 31 + r.item);
    mix(0xF00Du);
    for (const auto& f : ctx_->doc.faceSelection()) mix((f.entity * 31 + f.item) * 131 + f.face);
    mix(uint64_t(ctx_->scene.cameraMode) | (uint64_t(ctx_->scene.litPreview) << 8) | (uint64_t(ctx_->scene.litShadows) << 9) |
        (uint64_t(ctx_->texturedView) << 10) | (uint64_t(ctx_->showSpawns) << 11) | (uint64_t(bool(imageLookup_)) << 12) |
        (uint64_t(ctx_->scene.captureMode) << 13) | (uint64_t(gameLookOn()) << 14));
    mix(ctx_->scene.pointfile.size());
    mix(ctx_->scene.collisionPreview.size());
    mix(ctx_->scene.compiledWorld.size());
    mix(ctx_->scene.navLines.size() * 3 + 1);
    mix(ctx_->scene.tomeLinesVersion * 7 + 5);
    mix(ctx_->scene.gameOutlines ? 11 : 13);
    mix(ctx_->normalMaps ? 7 : 3);
    {
        IW8R_PROF("r.lights_wind");
        // Light entities per pixel on textured surfaces while the lit preview is on (3.14).
        std::vector<app::SceneRenderer::PixelLight>& pl = pixelLightScratch_;  // kept: no allocation per frame
        pl.clear();
        prepareGameLook();
        const bool game = gameLookOn();
        ctx_->scene.pixelLights = (ctx_->scene.litPreview || game) && ctx_->texturedView && !ctx_->scene.captureMode;
        if (ctx_->scene.pixelLights)
            for (const auto& L : ctx_->scene.lights) {
                if (pl.size() >= size_t(app::SceneRenderer::kMaxPixelLights)) break;
                app::SceneRenderer::PixelLight p{};
                p.pos[0] = float(L.pos.x), p.pos[1] = float(L.pos.y), p.pos[2] = float(L.pos.z), p.radius = float(L.radius);
                p.color[0] = float(L.color.x), p.color[1] = float(L.color.y), p.color[2] = float(L.color.z);
                p.cosOuter = float(L.cosOuter), p.cosInner = float(L.cosInner);
                p.dir[0] = float(L.dir.x), p.dir[1] = float(L.dir.y), p.dir[2] = float(L.dir.z);
                if (game) {
                    // The converter's light units (core game_look): the reference is radius / 2, or a spot's axis to the
                    // first surface (convert.cpp traces the render triangles; here the collision world).
                    double ref = L.radius * 0.5;
                    if (L.cosOuter > -1.5)
                        if (const CollisionWorld* cw = ctx_->scene.shadowWorld(ctx_->doc)) {
                            const double f = cw->traceRay(L.pos, L.pos + L.dir * ref).fraction;
                            if (f < 1.0) ref = std::max(1.0, f * ref);
                        }
                    const double gi = gameLightIntensity(L.radius, ref);
                    for (float& c : p.color) c = float(c * gi);
                }
                pl.push_back(p);
            }
        mix(ctx_->scene.pixelLights ? 11 : 5);
        renderer_.setPixelLights(pl);
        renderer_.setLightGain(float(ctx_->scene.lightGain));
        // Lit preview: encoded light (headroom to 2x) with exposure, and a sun shadow map over the map's bounds.
        // Game Lighting takes over from the lit preview: vertex colours carry no encoded light then (camera_geometry.cpp).
        const bool enc = ctx_->scene.litEncoded() && !ctx_->scene.captureMode && !game;
        renderer_.setLitDecode(enc ? 2.0f : 1.0f, float(ctx_->scene.exposure));
        ctx_->scene.gpuShadows = (enc || game) && ctx_->scene.litShadows;
        mix(ctx_->scene.gpuShadows ? 0x5d0u : 0x5d1u);
        if (!ctx_->scene.gpuShadows) {  // (with shadows on, the matrix is set after the geometry update below)
            const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            renderer_.setSunShadow(false, id, 0);
        }
        // Wind preview (5.23): the worldspawn iw8r_wind key (or the default breeze) drives the foliage sway.
        const WindSettings& ws = wind_;  // parsed when the document changes (above)
        const double t = windTimeOverride_ >= 0 ? windTimeOverride_ : windClock_.isValid() ? windClock_.elapsed() / 1000.0 : 0.0;
        const double yaw = ws.dirYawDeg * 3.14159265358979 / 180.0;
        renderer_.setWind(float(std::cos(yaw)), float(std::sin(yaw)),
                          ctx_->windPreview ? float(kSwayAmplitude * std::clamp(ws.strength, 0.0, kSwayMaxStrength)) : 0.0f,
                          float(2.0 * 3.14159265358979 * std::clamp(ws.gustFreq, 0.0, kSwayMaxGustHz) * t));
        const bool animate = ctx_->windPreview || ctx_->behaviourPreview || (ctx_->effectPreview && ctx_->texturedView);
        if (animate && !windTimer_.isActive()) {
            if (!windClock_.isValid()) windClock_.start();
            windTimer_.start(33, this);
        } else if (!animate && windTimer_.isActive()) {
            windTimer_.stop();
        }
    }
    mix(ctx_->scene.spawns.size());
    mix(ctx_->scene.lights.size());
    // The map shader's filter on the stock reference: compiled when the shader (or its file) changes.
    if (filterLookup_ && rendererOk_ && pollFiles_) {
        const std::shared_ptr<const CustomShader> fs = filterLookup_();
        const std::string key = fs ? fs->path + '\n' + fs->code : std::string();
        if (key != filterKey_) {
            filterKey_ = key;
            std::string err;
            const std::string src = fs ? customShaderFilterViewportSource(app::SceneRenderer::texShaderSource(), "float4 ps_tex(PSIn i) : SV_TARGET {", *fs)
                                       : std::string();
            if (!renderer_.setReferenceFilterShader(src, &err)) {
                renderer_.setReferenceFilterShader({}, nullptr);
                if (ctx_ && fs) emit ctx_->statusMessage(QString::fromStdString("Shader filter " + fs->name + ": " + err));
            }
        }
    }
    // Sky preview: (re)load the face textures when the sky image changes.
    if (ctx_->texturedView && skySource_ && pollFiles_) {
        // The map's sky rotation (worldspawn skyrotation; stock maps: from their entity string) is part of the key: the
        // entity string may arrive after the sky image.
        float skyRot[3] = {0, 0, 0};
        bool skyRotSet = false;
        float sunYaw = 45;  // the build's default sundirection -50 45 0
        for (const auto& e : ctx_->doc.map().entities)
            if (e.isWorldspawn()) {
                if (const std::string* r = e.value("skyrotation"))
                    skyRotSet = std::sscanf(r->c_str(), "%f %f %f", &skyRot[0], &skyRot[1], &skyRot[2]) >= 2;
                float p = 0, y = 0;
                if (const std::string* d = e.value("sundirection"); d && std::sscanf(d->c_str(), "%f %f", &p, &y) == 2) sunYaw = y;
                break;
            }
        const QString sky = skySource_();
        // No skyrotation: the build turns the sky so its sun is on the map sun's yaw (core sky_sun); the preview does too.
        if (!skyRotSet && !sky.isEmpty()) {
            const QString autoKey = sky + QString("|%1").arg(sunYaw);
            if (autoKey != skyAutoKey_) {
                skyAutoKey_ = autoKey;
                std::string bytes;
                skyAutoYaw_ = readFile(sky.toStdString(), bytes) ? float(autoSkyRotationYaw(bytes, sunYaw)) : 0.0f;
            }
            skyRot[1] = skyAutoYaw_;
        }
        const QString skyKey = sky.isEmpty() ? QString() : sky + QString("|%1 %2 %3").arg(skyRot[0]).arg(skyRot[1]).arg(skyRot[2]);
        if (skyKey != skyPath_) {
            skyPath_ = skyKey;
            skyTex_.clear();
            std::string bytes, cube, err;
            SkyConvertOptions so;
            so.faceSize = 256;
            // The map's sky rotation (worldspawn skyrotation; stock maps: from their entity string).
            for (int k = 0; k < 3; ++k) so.rotation[k] = skyRot[k];
            SkyConvertInfo skyInfo;
            if (!sky.isEmpty() && readFile(sky.toStdString(), bytes) && skyCubemapFromDds(bytes, cube, so, &skyInfo, &err)) {
                skyExposure_ = skyInfo.hdr ? skyInfo.exposure : 0.0f;
                skyMeanLum_ = skyInfo.meanLuminance, skyHighLum_ = skyInfo.highLuminance;
                // Legacy DDS: 128-byte header, six faces of B8G8R8A8, one mip each.
                const int fs = int(std::lround(std::sqrt((cube.size() - 128) / 24.0)));
                if (fs > 0 && cube.size() == 128 + size_t(6) * size_t(fs) * size_t(fs) * 4)
                    for (int f = 0; f < 6; ++f) {
                        std::vector<uint8_t> rgba(size_t(fs) * size_t(fs) * 4);
                        const uint8_t* src = reinterpret_cast<const uint8_t*>(cube.data()) + 128 + size_t(f) * rgba.size();
                        for (size_t k = 0; k < rgba.size(); k += 4) {
                            rgba[k] = src[k + 2];
                            rgba[k + 1] = src[k + 1];
                            rgba[k + 2] = src[k];
                            rgba[k + 3] = 255;
                        }
                        skyTex_.push_back(renderer_.createTexture(rgba.data(), fs, fs));
                        // Diagnosis: IW8R_DUMP_SKY=<folder> writes the six preview faces as PNG.
                        if (const QString dump = qEnvironmentVariable("IW8R_DUMP_SKY"); !dump.isEmpty())
                            QImage(rgba.data(), fs, fs, QImage::Format_RGBA8888).save(QDir(dump).filePath(QString("sky_face%1.png").arg(f)));
                    }
            }
            geomDirty_ = true;
        }
    }
    ctx_->scene.skyActive = ctx_->texturedView && skyTex_.size() == 6;
    mix(uint64_t(skyTex_.size()) << 20);
    const bool rebuild = key != geomKey_ || geomDirty_;
    if (rebuild) {
        IW8R_PROF("r.build");
        ctx_->scene.normalFor = nullptr;
        ctx_->scene.textureSizeFor = nullptr;
        if (ctx_->scene.cameraMode == app::CameraMode::Density && imageLookup_)
            ctx_->scene.textureSizeFor = [this](const std::string& m) {
                if (!texSizes_.count(m)) textureFor(m);  // decodes (and caches) the colour image once
                auto it = texSizes_.find(m);
                return it == texSizes_.end() ? std::pair<int, int>{0, 0} : it->second;
            };
        if (ctx_->texturedView && imageLookup_ && normalLookup_ && ctx_->normalMaps)
            ctx_->scene.normalFor = [this](const std::string& m) { return normalFor(m); };
        ctx_->scene.emissiveFor = ctx_->texturedView && emissiveLookup_ ? std::function<int(const std::string&)>([this](const std::string& m) { return emissiveFor(m); })
                                                                         : std::function<int(const std::string&)>();
        ctx_->scene.specFor = ctx_->texturedView && specLookup_ && ctx_->normalMaps ? std::function<int(const std::string&)>([this](const std::string& m) { return specFor(m); })
                                                                                      : std::function<int(const std::string&)>();
        ctx_->scene.shaderFor = shaderLookup_ ? std::function<int(const std::string&)>([this](const std::string& m) { return customShaderFor(m); })
                                              : std::function<int(const std::string&)>();
        // The kept geometry is brought up to date item by item (scene.h CameraGeometry); geomDirty_ (textures still
        // arriving, a changed material image, a capture) makes everything again.
        if (!texFn_) texFn_ = [this](const std::string& m) { return textureFor(m); };
        static const bool noInPlace = app::perfOff("inplace");  // benchmark switch: everything again on every change
        geo_.update(ctx_->scene, ctx_->doc, ctx_->showSpawns, ctx_->texturedView && imageLookup_ ? &texFn_ : nullptr, geomDirty_ || noInPlace);
        updateSkyBox();  // probe captures reflect the sky too
        geomKey_ = key;
        geomDirty_ = moreToLoad_;  // textures still arriving: build again next frame
        ++geometryUpdates_;
    }
    {
        IW8R_PROF("r.bake_look");
        maybeBakeGameLook();
        maybeLightModels();
    }
    {
        IW8R_PROF("r.skybox");
        updateSkyBox();  // follows the camera
    }
    extraBatches_.clear();
    if (ctx_->scene.skyActive)
        for (auto& sb : skyBatches_) extraBatches_.push_back(sb.get());
    // 8.1: effect particles, re-made every frame (their own batch, after the kept geometry).
    if (ctx_->effectPreview && ctx_->texturedView && effectSource_ && !cleanCapture_) {
        appendEffectParticles();
        if (!fxBatch_.v.empty()) extraBatches_.push_back(&fxBatch_);
    } else {
        fxParticles_ = 0;
    }
    // The gizmo is a small stream of its own, sent again only when it changed.
    gizmoLines_.scratch.clear();
    if (!cleanCapture_) addGizmoLines();
    if (gizmoLines_.scratch.size() != gizmoLines_.v.size() ||
        (!gizmoLines_.v.empty() && std::memcmp(gizmoLines_.scratch.data(), gizmoLines_.v.data(), gizmoLines_.v.size() * sizeof(app::GpuVertex)) != 0)) {
        gizmoLines_.v.swap(gizmoLines_.scratch);
        gizmoLines_.markAll();
    }
    if (ctx_->scene.gpuShadows) {
        // The sun shadow map covers the box around every item.
        const AABB& sb = geo_.sceneBounds();
        if (sb.valid()) {
            const Vec3 st = gameLookOn() ? gameSunToward() : ctx_->scene.sunToward;
            const double t[3] = {st.x, st.y, st.z};
            const double lo[3] = {sb.mins.x - 64, sb.mins.y - 64, sb.mins.z - 64}, hi[3] = {sb.maxs.x + 64, sb.maxs.y + 64, sb.maxs.z + 64};
            float m[16];
            app::sunShadowMatrix(t, lo, hi, m);
            const double R = std::max(1.0, length(sb.maxs - sb.mins) / 2 + 110);
            // Depth bias: 2 units plus one and a half shadow-map texels (2048 over the box), so a big stock scene's ground
            // (about 9 units a texel) does not shadow itself into an even half shade.
            const double texel = 2.0 * R / 2048.0;
            renderer_.setSunShadow(true, m, float((2.0 + 1.5 * texel) / (4.0 * R)));
            // The near cascade: a box of kNearRadius around the view ahead of the camera, moved in steps of an eighth of
            // it (so it is not drawn again on every small move), when the scene is much bigger than that box.
            constexpr double kNearRadius = 1400.0;
            if (R > 2.5 * kNearRadius) {
                const Vec3 fwd = ctx_->cam.forward();
                Vec3 flat{fwd.x, fwd.y, 0};
                flat = length(flat) > 1e-6 ? normalize(flat) : Vec3{1, 0, 0};
                Vec3 c = ctx_->cam.pos + flat * (0.55 * kNearRadius);
                const double step = kNearRadius / 8;
                c = Vec3{std::round(c.x / step) * step, std::round(c.y / step) * step, std::round(c.z / step) * step};
                const double cc[3] = {c.x, c.y, c.z};
                const double depthR = std::max(kNearRadius, R + length(c - sb.center()));
                float mn[16];
                app::sunShadowMatrixAround(t, cc, kNearRadius, depthR, mn);
                const double texelN = 2.0 * kNearRadius / 2048.0;
                renderer_.setSunShadowNear(true, mn, float((1.0 + 1.5 * texelN) / (4.0 * depthR)));
            } else {
                renderer_.setSunShadowNear(false, m, 0);
            }
        }
    }
    const app::Mat4f vp = app::viewProjection(ctx_->cam, captureAspect_ > 0 ? captureAspect_ : double(w) / double(h));
    {
        IW8R_PROF("r.draw");
        app::FrameInput in;
        in.viewProj = vp.m;
        in.clear = kClear;
        in.shadowOnly = &geo_.shadowOnly;
        in.casters = &geo_.casters;
        in.faceSel = &geo_.faceSel;
        in.noCast = &geo_.noCast;
        in.lines = &geo_.lines;
        in.linesMisc = &geo_.linesMisc;
        in.overlay[0] = &geo_.sunArrow;
        in.overlay[1] = &geo_.selLines;
        in.overlay[2] = &geo_.overlayMisc;
        in.overlay[3] = &gizmoLines_;
        in.batches = &geo_.batches();
        in.extraBatches = &extraBatches_;
        in.casterVersion = geo_.casterVersion();
        in.clean = cleanCapture_;  // game surfaces only: no markers, tool faces, volumes or lines
        in.capture = cleanCapture_ || ctx_->scene.captureMode;  // no editor-only reference markers in a capture
        in.geometryChanged = rebuild;
        renderer_.drawFrame(in);
    }
    if (moreToLoad_ || refMoreToLoad_)
        QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);  // finish loading next frame
    ensureSwapChain();
    if (!swap_) return;
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
        D3D11_TEXTURE2D_DESC bd{};
        back->GetDesc(&bd);
        if (int(bd.Width) == renderer_.width() && int(bd.Height) == renderer_.height()) dc_->CopyResource(back, renderer_.colorTexture());
        back->Release();
    }
    IW8R_PROF("r.present");
    swap_->Present(1, 0);
}

// ------------------------------------------------------------------ translate gizmo

bool CameraView::gizmoActive() const {
    return ctx_->gizmo && !ctx_->doc.selection().empty() && ctx_->doc.selectionBounds().valid() && !ctx_->clipMode;
}

Vec3 CameraView::gizmoPivot() const {
    if (gizmoDragAxis_ >= 0) {
        // Follow the selection while dragging.
        return ctx_->doc.selectionBounds().center();
    }
    return ctx_->doc.selectionBounds().center();
}

Vec3 CameraView::gizmoAxisDir(int axis) const {
    Vec3 d;
    d[axis] = 1;
    // Local space applies to moves of one selected point entity with angles (rings and scale stay on world axes).
    if (!ctx_->gizmoLocal || gizmoRotate_ || gizmoScale_ || ctx_->doc.selection().size() != 1) return d;
    const SelRef r = *ctx_->doc.selection().begin();
    if (r.item != kNoId) return d;
    const Entity* e = ctx_->doc.findEntity(r.entity);
    const std::string* a = e ? e->value("angles") : nullptr;
    if (!a) return d;
    Vec3 ang;
    if (std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z) < 2) return d;
    return normalize(anglesToMatrix(ang) * d);
}

double CameraView::gizmoLength() const {
    // Constant on-screen size: a fraction of the distance to the camera.
    return std::max(8.0, length(gizmoPivot() - ctx_->cam.pos) * 0.18);
}

CameraView::GizmoAxis CameraView::gizmoAxisOnScreen(int axis) const {
    GizmoAxis out;
    if (!gizmoActive() || axis < 0 || axis > 2) return out;
    const double aspect = double(std::max(1, width())) / std::max(1, height());
    const app::Mat4f m = app::viewProjection(ctx_->cam, aspect);
    auto project = [&](const Vec3& p, QPointF& px) {
        const double x = p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12];
        const double y = p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13];
        const double w = p.x * m.m[3] + p.y * m.m[7] + p.z * m.m[11] + m.m[15];
        if (w <= 1e-6) return false;
        px = QPointF((x / w * 0.5 + 0.5) * width(), (0.5 - y / w * 0.5) * height());
        return true;
    };
    const Vec3 dir = gizmoAxisDir(axis);
    const Vec3 c = gizmoPivot();
    out.visible = project(c, out.from) && project(c + dir * gizmoLength(), out.to);
    return out;
}

double CameraView::axisParamForMouse(const QPointF& p, int axis) const {
    const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
    const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
    const Vec3 a = gizmoAxisDir(axis);
    // Closest points between the axis line (P + s a) and the ray (O + u d).
    const Vec3 w0 = gizmoStartPivot_ - ray.origin;
    const double b = dot(a, ray.dir), c = dot(ray.dir, ray.dir), d = dot(a, w0), e = dot(ray.dir, w0);
    const double denom = c - b * b;  // |a| = 1
    if (std::fabs(denom) < 1e-9) return 0;
    return (b * e - c * d) / denom;
}

std::optional<QPointF> CameraView::projectToWidget(const Vec3& p) const {
    const double aspect = double(std::max(1, width())) / std::max(1, height());
    const app::Mat4f m = app::viewProjection(ctx_->cam, aspect);
    const double x = p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12];
    const double y = p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13];
    const double w = p.x * m.m[3] + p.y * m.m[7] + p.z * m.m[11] + m.m[15];
    if (w <= 1e-6) return std::nullopt;
    return QPointF((x / w * 0.5 + 0.5) * width(), (0.5 - y / w * 0.5) * height());
}

namespace {
// Ring basis: u = next axis, v = the one after (angle 0 at +u, 90 at +v; matches rotatePoint's sense).
void ringBasis(int axis, Vec3& u, Vec3& v) {
    u = Vec3{};
    v = Vec3{};
    u[(axis + 1) % 3] = 1;
    v[(axis + 2) % 3] = 1;
}
}  // namespace

std::optional<QPointF> CameraView::gizmoRingPoint(int axis, double degrees) const {
    if (!gizmoActive()) return std::nullopt;
    Vec3 u, v;
    ringBasis(axis, u, v);
    const double r = gizmoLength() * 0.9, a = degrees * 3.14159265358979 / 180.0;
    return projectToWidget(gizmoPivot() + u * (r * std::cos(a)) + v * (r * std::sin(a)));
}

std::optional<double> CameraView::ringAngleForMouse(const QPointF& p, int axis) const {
    const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
    const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
    Vec3 n;
    n[axis] = 1;
    const double denom = dot(n, ray.dir);
    if (std::fabs(denom) < 1e-6) return std::nullopt;
    const double t = dot(n, ringPivot_ - ray.origin) / denom;
    const Vec3 hit = ray.origin + ray.dir * t;
    Vec3 u, v;
    ringBasis(axis, u, v);
    return std::atan2(dot(hit - ringPivot_, v), dot(hit - ringPivot_, u)) * 180.0 / 3.14159265358979;
}

void CameraView::addGizmoLines() {
    if (!gizmoActive()) return;
    const Vec3 c = gizmoPivot();
    const double L = gizmoLength();
    if (gizmoRotate_) {
        const uint32_t ringCols[3] = {0xff3040ffu, 0xff40ff40u, 0xffff6040u};
        for (int a = 0; a < 3; ++a) {
            Vec3 u, v;
            ringBasis(a, u, v);
            const uint32_t col = ringDragAxis_ == a ? 0xff40ffffu : ringCols[a];
            const double r = L * 0.9;
            for (int k = 0; k < 48; ++k) {
                const double a0 = k * 2 * 3.14159265358979 / 48, a1 = (k + 1) * 2 * 3.14159265358979 / 48;
                const Vec3 p = c + u * (r * std::cos(a0)) + v * (r * std::sin(a0));
                const Vec3 q = c + u * (r * std::cos(a1)) + v * (r * std::sin(a1));
                gizmoLines_.scratch.push_back(app::GpuVertex{float(p.x), float(p.y), float(p.z), col});
                gizmoLines_.scratch.push_back(app::GpuVertex{float(q.x), float(q.y), float(q.z), col});
            }
            if (ringDragAxis_ == a) {
                // Spoke at the current snapped angle.
                const double ang = (ringStartAngle_ + ringDeltaDeg_) * 3.14159265358979 / 180.0;
                const Vec3 p = c + u * (r * std::cos(ang)) + v * (r * std::sin(ang));
                gizmoLines_.scratch.push_back(app::GpuVertex{float(c.x), float(c.y), float(c.z), col});
                gizmoLines_.scratch.push_back(app::GpuVertex{float(p.x), float(p.y), float(p.z), col});
            }
        }
        return;
    }
    const uint32_t cols[3] = {0xff3040ffu, 0xff40ff40u, 0xffff6040u};  // X red, Y green, Z blue (ABGR)
    for (int a = 0; a < 3; ++a) {
        const Vec3 d = gizmoAxisDir(a);
        const uint32_t col = gizmoDragAxis_ == a ? 0xff40ffffu : cols[a];
        auto line = [&](const Vec3& p, const Vec3& q) {
            gizmoLines_.scratch.push_back(app::GpuVertex{float(p.x), float(p.y), float(p.z), col});
            gizmoLines_.scratch.push_back(app::GpuVertex{float(q.x), float(q.y), float(q.z), col});
        };
        if (gizmoScale_) {
            const double len = L * (gizmoDragAxis_ == a ? scaleFactor_ : 1.0);
            const Vec3 tip = c + d * len;
            line(c, tip);
            // Box handle at the end.
            const double b = L * 0.06;
            const Vec3 corners[8] = {tip + Vec3{-b, -b, -b}, tip + Vec3{b, -b, -b}, tip + Vec3{b, b, -b}, tip + Vec3{-b, b, -b},
                                     tip + Vec3{-b, -b, b},  tip + Vec3{b, -b, b},  tip + Vec3{b, b, b},  tip + Vec3{-b, b, b}};
            const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& ed : edges) line(corners[ed[0]], corners[ed[1]]);
            continue;
        }
        const Vec3 tip = c + d * L;
        line(c, tip);
        // Arrow head: two short fins in a perpendicular plane.
        Vec3 side;
        side[(a + 1) % 3] = 1;
        Vec3 side2;
        side2[(a + 2) % 3] = 1;
        line(tip, tip - d * (L * 0.15) + side * (L * 0.06));
        line(tip, tip - d * (L * 0.15) - side * (L * 0.06));
        line(tip, tip - d * (L * 0.15) + side2 * (L * 0.06));
        line(tip, tip - d * (L * 0.15) - side2 * (L * 0.06));
    }
}

// ------------------------------------------------------------------ play preview

void CameraView::startPlay() {
    if (ctx_->playing) return;
    playMode_ = nullptr;
    world_.build(ctx_->doc.map());
    worldRevision_ = ctx_->doc.revision();
    // Spawn: a selected spawn entity (its origin and yaw), else below the camera.
    Vec3 from = ctx_->cam.pos;
    double yaw = ctx_->cam.yaw;
    for (const auto& r : ctx_->doc.selection()) {
        const Entity* e = ctx_->doc.findEntity(r.entity);
        if (!e || r.item != kNoId) continue;
        const std::string c = e->classname();
        if (c.rfind("mp_", 0) == 0 || c == "info_player_start") {
            if (auto o = e->origin()) from = *o + Vec3{0, 0, 8};
            if (const std::string* a = e->value("angles")) {
                double p = 0, y = 0, rr = 0;
                if (std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &rr) >= 2) yaw = y;
            }
            break;
        }
    }
    player_ = PlayerState{};
    const bool floor = placePlayer(world_, player_, from, params_);
    player_.yaw = yaw;
    player_.pitch = 0;
    if (!floor) player_.noclip = true;  // nothing below: fly instead of falling forever
    ctx_->playing = true;
    ctx_->heldKeys.clear();
    ctx_->doc.deselectAll();
    ctx_->cam.yaw = yaw;
    ctx_->cam.pitch = 0;
    // Mouse look without holding a button.
    setMouseTracking(true);
    ctx_->looking = true;
    lockPos_ = QPoint(width() / 2, height() / 2);
    if (isVisible()) {
        QCursor::setPos(mapToGlobal(lockPos_));
        setCursor(Qt::BlankCursor);
        setFocus(Qt::OtherFocusReason);
    }
    emit ctx_->playModeChanged(true);
    ctx_->setStatus(QString("PLAY: mouse look, WASD move, Space jump, C crouch, Shift sprint, N noclip, Esc exit%1")
                        .arg(floor ? QString() : QString(" (no floor below: noclip)")));
    tickPlay(0);
}

void CameraView::stopPlay() {
    if (!ctx_->playing) return;
    ctx_->playing = false;
    ctx_->looking = false;
    ctx_->heldKeys.clear();
    setMouseTracking(false);
    unsetCursor();
    emit ctx_->playModeChanged(false);
    ctx_->setStatus("Play preview ended");
    ctx_->cameraMoved();
}

void CameraView::tickPlay(double dt) {
    if (!ctx_->playing) return;
    if (worldRevision_ != ctx_->doc.revision()) {  // edits while playing (undo, live changes)
        world_.build(ctx_->doc.map());
        worldRevision_ = ctx_->doc.revision();
    }
    auto down = [&](int k) { return ctx_->heldKeys.contains(k); };
    MoveInput in;
    in.forward = (down(Qt::Key_W) || down(Qt::Key_Up) ? 1 : 0) - (down(Qt::Key_S) || down(Qt::Key_Down) ? 1 : 0);
    in.right = (down(Qt::Key_D) || down(Qt::Key_Right) ? 1 : 0) - (down(Qt::Key_A) || down(Qt::Key_Left) ? 1 : 0);
    in.jump = down(Qt::Key_Space);
    in.crouch = down(Qt::Key_C) || down(Qt::Key_Control);
    in.prone = down(Qt::Key_Z);
    in.sprint = down(Qt::Key_Shift);
    in.highMantle = in.jump && !player_.onGround;  // Space in the air grabs a high ledge
    in.up = (in.jump ? 1 : 0) - (in.crouch ? 1 : 0);  // noclip vertical
    if (player_.noclip) in.crouch = false;
    player_.yaw = ctx_->cam.yaw;
    player_.pitch = ctx_->cam.pitch;
    // Fixed 1/60 s steps (the mover clamps long frames; this keeps jumps identical at any frame rate).
    double left = std::min(dt, 0.25);
    do {
        in.dt = std::min(left, 1.0 / 60.0);
        if (in.dt > 0) playerMove(world_, player_, in, params_);
        left -= in.dt;
    } while (left > 1e-9);
    ctx_->cam.pos = player_.origin + Vec3{0, 0, player_.noclip ? 0.0 : eyeHeight(params_, player_)};
    // Movement mode in the status bar when it changes (mantle / slide / sprint / stance).
    const char* mode = player_.noclip ? "noclip" : player_.mantling ? "mantle" : player_.sliding ? "slide" : player_.sprinting ? "sprint"
                       : player_.prone ? "prone" : player_.crouched ? "crouch" : !player_.onGround ? "air" : "walk";
    if (mode != playMode_) {
        playMode_ = mode;
        ctx_->setStatus(QString("Play: %1  (WASD, Space jump/mantle, Shift sprint, C crouch/slide, Z prone, N noclip, Esc exit)").arg(mode));
    }
    ctx_->cameraMoved();
}

std::optional<std::array<float, 16>> CameraView::materialConstantsFor(const std::string& m) const {
    // The stock material's layer-0 constants (spec / gloss remap, normal strength) for the Game Lighting shader.
    if (m.empty() || !factsLookup_ || m.find_first_of("\x01\x02\x05\x06") != std::string::npos) return std::nullopt;
    const auto f = factsLookup_(m);
    if (f && f->ssr && f->constants.layer.empty())  // no constants, but it traces SSR (layerP.z)
        return std::array<float, 16>{0.0f, 0.0f, 1.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!f || f->constants.layer.empty()) return std::nullopt;
    const auto& c = f->constants.layer[0];
    const bool masked = maskLookup_ && !maskLookup_(m).isEmpty();
    const bool blended = ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(m) != 0;
    return std::array<float, 16>{0.0f, !masked && !blended ? 1.0f : 0.0f, f->ssr ? 1.0f : 0.0f, 0.0f, c.specScale, c.specBias, c.glossScale, c.glossBias,
                                 0.0f, 0.0f, 0.0f, 0.0f, c.normalStrength, 1.0f, 0.0f, 0.0f};
}

bool CameraView::applyMaterialExtras(const std::string& m, size_t index, bool propMesh) {
    if (m.empty() || !factsLookup_ || m.find_first_of("\x01\x02\x05\x06") != std::string::npos) return true;
    const auto f = factsLookup_(m);
    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0)) return true;
    int ids[2] = {-1, -1};
    const std::string files[2] = {f->emissiveScale[3] > 0 ? f->emissiveFile : std::string(), f->transTint[3] > 0 ? f->transmissionFile : std::string()};
    for (int k = 0; k < 2; ++k) {
        if (files[k].empty()) continue;
        const std::string key = "\x06" + files[k];
        const int id = textureFor(key);
        if (!texIds_.count(key)) return false;  // still loading
        ids[k] = id == checkerTex_ ? -1 : id;
    }
    float x[16] = {f->emissiveScale[0], f->emissiveScale[1], f->emissiveScale[2], ids[0] >= 0 ? 1.0f : 0.0f,
                   f->transTint[0], f->transTint[1], f->transTint[2], ids[1] >= 0 ? 1.0f : 0.0f,
                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, 0, 0, 0, 0};
    if (propMesh) renderer_.setReferenceInstExtra(index, x, ids[0], ids[1]);
    else renderer_.setReferenceBaseExtra(index, x, ids[0], ids[1]);
    return true;
}

void CameraView::setBaseMaterialConstants(size_t k) {
    if (k >= refTexMaterials_.size()) return;
    if (const auto p = materialConstantsFor(refTexMaterials_[k])) renderer_.setReferenceBaseParams(k, p->data());
}

bool CameraView::resolveReferenceLayer(size_t k) {
    const std::string& base = refTexMaterials_[k];
    const int L = refTexLayer_[k];
    if (!layerLookup_) return true;  // no layer data: the layer is left out
    const std::optional<StockLayeredMaterial> c = layerLookup_(base);
    if (!c) {
        if (imageRequest_ && requested_.insert(base).second) imageRequest_(base);
        return false;
    }
    StockLayerConstants lc;
    if (c->layers > L && size_t(L) < c->layer.size()) lc = c->layer[size_t(L)];
    // Colour modes 2 and 3 leave the colour below (their normal / gloss blend only): not drawn. Multiply (1) is.
    if (lc.colorMode >= 2) return true;
    const std::string ck = base + '\x01' + std::to_string(L), rk = base + '\x02' + std::to_string(L);
    const int cid = textureFor(ck), rid = textureFor(rk);
    if (!texIds_.count(ck) || !texIds_.count(rk)) return false;
    if (cid < 0 || cid == checkerTex_) return true;  // no colour image: left out rather than drawn as the checker
    const int nid = normalFor(ck);
    if (!nrmIds_.count(ck)) return false;  // waits for the per-frame budget
    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
    // A decal material's layers draw with the decal, in its sort order (research 98).
    if (factsLookup_)
        if (const auto f = factsLookup_(base); f && f->decalSortKey > 0) {
            const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], f->ssr ? 1.0f : 0.0f,
                                 lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
            renderer_.setReferenceTexture(k, cid);
            renderer_.setReferenceDecal(k, f->decalSortKey, rid == checkerTex_ ? -1 : rid, p);
            return true;
        }
    const bool layerSsr = factsLookup_ && [&] { const auto f = factsLookup_(base); return f && f->ssr; }();
    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], layerSsr ? 1.0f : 0.0f,
                         lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
    renderer_.setReferenceTexture(k, cid);
    renderer_.setReferenceLayer(k, rid == checkerTex_ ? -1 : rid, p);
    return true;
}

bool CameraView::resolveReferenceDecal(size_t k, int textureId) {
    if (!factsLookup_ || k >= refTexMaterials_.size()) return true;
    const std::string& base = refTexMaterials_[k];
    const auto f = factsLookup_(base);
    if (!f) return true;  // the colour resolved, so the material is known: no facts = not a decal
    if (f->decalSortKey <= 0) return true;
    // Layer 0's alpha + reveal map (slot 27) and its blend constants.
    const std::string rk = base + '\x02' + "0";
    const int rid = textureFor(rk);
    if (!texIds_.count(rk)) return false;
    StockLayerConstants lc;
    if (!f->constants.layer.empty()) lc = f->constants.layer[0];
    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, 0.0f, 1.0f, 1.0f, 1.0f, f->ssr ? 1.0f : 0.0f,
                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // layer 0: its tint is in the image already
    (void)textureId;
    renderer_.setReferenceDecal(k, f->decalSortKey, rid == checkerTex_ ? -1 : rid, p);
    return true;
}

void CameraView::setMaterialImages(ImageLookup lookup, ImageRequest request) {
    imageLookup_ = std::move(lookup);
    imageRequest_ = std::move(request);
    materialImagesChanged();
}

void CameraView::materialImagesChanged() {
    geomDirty_ = true;
    filesDue_ = true;
    refPollDue_ = true;  // reference materials that waited for an image are asked again
    refTexPending_ = refTexPending_ || !refTexMaterials_.empty();
    texInstPending_ = texInstPending_ || !texInstMaterials_.empty();
    for (auto it = texIds_.begin(); it != texIds_.end();) {
        if (it->second < 0) it = texIds_.erase(it);  // ask again: an image may have arrived
        else ++it;
    }
    for (auto it = nrmIds_.begin(); it != nrmIds_.end();) {
        if (it->second < 0) it = nrmIds_.erase(it);
        else ++it;
    }
    for (auto it = emiIds_.begin(); it != emiIds_.end();) {
        if (it->second < 0) it = emiIds_.erase(it);
        else ++it;
    }
    for (auto it = specIds_.begin(); it != specIds_.end();) {
        if (it->second < 0) it = specIds_.erase(it);
        else ++it;
    }
    update();
}

size_t CameraView::texturedTriangles() const {
    size_t n = 0;
    for (const app::SceneBatch* b : geo_.batches()) n += b->v.size() / 3;
    if (ctx_->scene.skyActive)
        for (const auto& b : skyBatches_) n += b->v.size() / 3;
    return n;
}

namespace {
// Viewport textures are capped at this size on their larger side.
constexpr int kViewTextureSide = 512;
// Texture loading per frame: at least this many loads (what a frame did before previews were decoded at their
// own size; off-screen captures that render a fixed number of frames count on it), then more while the frame has
// spent less than the time budget on loading.
constexpr int kMinTextureLoadsPerFrame = 6;
constexpr int kTextureLoadBudgetMs = 12;
qint64 g_frameLoadMs = 0;  // loading time of the frame being built (reset with loadsThisFrame_)

// True when the frame may take on another load. `loads` is CameraView::loadsThisFrame_.
bool textureLoadAllowed(int loads) {
    if (loads == 0) g_frameLoadMs = 0;
    return loads < kMinTextureLoadsPerFrame || g_frameLoadMs < kTextureLoadBudgetMs;
}
}  // namespace

int CameraView::videoMemoryMB() const {
    if (!dev_) return 0;
    IDXGIDevice* dxgiDev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    int mb = 0;
    if (SUCCEEDED(dev_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev))) && SUCCEEDED(dxgiDev->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC d{};
        if (SUCCEEDED(adapter->GetDesc(&d))) mb = int(d.DedicatedVideoMemory / (1024 * 1024));
    }
    if (adapter) adapter->Release();
    if (dxgiDev) dxgiDev->Release();
    return mb;
}

int CameraView::systemMemoryMB() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? int(ms.ullTotalPhys / (1024 * 1024)) : 0;
}

int CameraView::textureSideNow() const { return renderer_.textureBytes() > textureBudget_ ? 128 : kViewTextureSide; }

namespace {
std::atomic<bool> g_backgroundDecode{true};
}
bool CameraView::backgroundDecodeSelfTest(const QString& imageFile, QString* note) {
    // An image file through the worker path, whatever the process-wide switch says: not ready at once, ready soon after.
    const bool was = g_backgroundDecode;
    g_backgroundDecode = true;
    const int before = backgroundDecodes_;
    DecodedTexture d;
    const std::string path = imageFile.toStdString();
    const bool first = decodedTexture(path, 512, 2, d);
    QElapsedTimer t;
    t.start();
    bool got = first;
    while (!got && t.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        got = decodedTexture(path, 512, 2, d);
    }
    g_backgroundDecode = was;
    if (note)
        *note = QString("first answer %1, ready after %2 ms, %3 x %4, counted %5")
                    .arg(first ? "ready" : "pending").arg(t.elapsed()).arg(d.image.width).arg(d.image.height).arg(backgroundDecodes_ - before);
    return !first && got && d.imageOk && d.image.width > 0 && d.image.pixels.size() == size_t(d.image.width) * size_t(d.image.height) * 4 &&
           backgroundDecodes_ == before + 1;
}
void CameraView::setBackgroundTextureDecode(bool on) { g_backgroundDecode = on; }
bool CameraView::backgroundTextureDecode() { return g_backgroundDecode; }

bool CameraView::decodedTexture(const std::string& path, int side, int mode, DecodedTexture& out) {
    static const bool noBc = qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES");
    const auto job = [path, side, mode] {
        DecodedTexture d;
        if (mode == 2) {
            std::string err;
            d.imageOk = decodeImageFile(path, d.image.width, d.image.height, d.image.pixels, &err) && d.image.width > 0 && d.image.height > 0;
            d.fullW = d.image.width, d.fullH = d.image.height;
            return d;
        }
        if (mode == 0 && !noBc) d.blocksOk = app::loadDdsBlocks(path, side, d.blocks, &d.fullW, &d.fullH);
        if (!d.blocksOk) d.imageOk = app::loadDdsPreview(path, side, d.image, &d.fullW, &d.fullH) && d.image.width > 0 && d.image.height > 0;
        return d;
    };
    // A render into an image (a screenshot, a test, a probe capture) draws a fixed number of frames: it waits.
    const bool wait = !g_backgroundDecode || captureW_ > 0 || cleanCapture_ || (ctx_ && ctx_->scene.captureMode);
    if (!decodePool_) {
        if (wait) {
            try {
                out = job();
            } catch (...) {
                out = DecodedTexture{};
            }
            return true;
        }
        decodePool_ = std::make_unique<KeyedJobPool<DecodedTexture>>();
        QPointer<CameraView> self(this);
        decodePool_->setReadyCallback([self] {
            QMetaObject::invokeMethod(qApp, [self] {
                if (self) self->update();  // the frame that went on without the texture asks again
            }, Qt::QueuedConnection);
        });
    }
    // The preview size changed (the texture budget was reached): what was decoded at the old size will not be asked
    // for again, and would fill the pool until it went stale.
    if (side != decodeSide_) {
        if (decodeSide_ != 0) decodePool_->clear();
        decodeSide_ = side;
    }
    const std::string key = std::to_string(mode) + ":" + std::to_string(side) + ":" + path;
    if (!decodePool_->take(key, job, out, wait)) return false;
    if (!wait) ++backgroundDecodes_;
    return true;
}

// A stock colour image no step edits: no cut-out mask, not a blended material, no tint (catalogue or the material's own).
bool CameraView::colourImageKeptCompressed(const std::string& material) const {
    static const bool off = qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES");  // A/B switch
    if (off) return false;
    if (maskLookup_ && !maskLookup_(material).isEmpty()) return false;
    if (ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(material) != 0) return false;
    if (MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(material)) return false;
    if (factsLookup_ && material.find_first_of("\x01\x02") == std::string::npos)
        if (const auto f = factsLookup_(material); f && f->tint >= 0) return false;
    return true;
}

int CameraView::textureFor(const std::string& material) {
    if (material.empty() || isIw8ToolMaterial(material)) return -1;  // tool faces keep their flat colour
    if (auto it = texIds_.find(material); it != texIds_.end()) return it->second;
    const std::optional<QString> path = imageLookup_ ? imageLookup_(material) : std::nullopt;
    if (!path) {
        if (imageRequest_ && requested_.insert(material).second) imageRequest_(material);
        return -1;  // not cached in texIds_: asked again after materialImagesChanged()
    }
    if (path->isEmpty()) {
        // Known missing: the Radiant missing-material checker.
        if (checkerTex_ < 0) {
            std::vector<uint8_t> px(64 * 64 * 4);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    const bool on = ((x / 8) + (y / 8)) % 2 == 0;
                    uint8_t* q = &px[size_t((y * 64 + x) * 4)];
                    q[0] = on ? 230 : 40;
                    q[1] = on ? 40 : 40;
                    q[2] = on ? 230 : 40;
                    q[3] = 255;
                }
            checkerTex_ = renderer_.createTexture(px.data(), 64, 64);
        }
        return texIds_[material] = checkerTex_;
    }
    // Layer, decal and decal volume images ("\x01" / "\x02" / "\x05" keys) are plain files, never tinted or masked
    // per material: one texture per file (mp_aniyah_tac decoded the same layer images once per material, 37,000
    // textures, and ran out of video memory).
    const bool shareable = material.find_first_of("\x01\x02\x05\x06") != std::string::npos;
    if (shareable)
        if (const auto it = texByPath_.find(path->toStdString()); it != texByPath_.end()) return texIds_[material] = it->second;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;  // keep frames short: the rest loads in the next frames
        return -1;
    }
    ++loadsThisFrame_;
    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    int fullW = 0, fullH = 0;  // size of the whole image (Texel Density reads it), not of the level shown
    bool decoded = false;
    const bool isDds = path->endsWith(".dds", Qt::CaseInsensitive);
    const bool compressed = isDds && colourImageKeptCompressed(material);
    if (compressed)
        if (const auto it = texByPathBc_.find(path->toStdString()); it != texByPathBc_.end()) {
            --loadsThisFrame_;
            return texIds_[material] = it->second;
        }
    // The file is read and decoded by a worker (decodedTexture); this frame goes on without it until it is ready.
    DecodedTexture dec;
    if (!decodedTexture(path->toStdString(), textureSideNow(), !isDds ? 2 : compressed ? 0 : 1, dec)) {
        --loadsThisFrame_;
        moreToLoad_ = true;
        return -1;
    }
    fullW = dec.fullW, fullH = dec.fullH;
    if (compressed) {
        // A stock image nothing edits (no cut-out mask, not blended, no tint): uploaded block-compressed as stored,
        // its alpha the specular code (Verdansk with its streamed tiles ran past the texture budget decoded to RGBA).
        // Nothing per material changes it, so materials that use the same file share one texture.
        app::DdsBlockLevels& blocks = dec.blocks;
        if (dec.blocksOk) {
            id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {
                texSizes_[material] = {fullW, fullH};
                if (textureSideNow() < kViewTextureSide) ++budgetReduced_;
                g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
                if (shareable) texByPath_[path->toStdString()] = id;
                texByPathBc_[path->toStdString()] = id;
                return texIds_[material] = id;
            }
        }
    }
    if (isDds) {
        // Only the mip level the viewport texture needs is read and decoded: the top level of a 2048x2048 stock
        // image is 16 MB of RGBA and cost 100 ms and more here, to be shrunk to 512 right after.
        if (dec.imageOk) {
            img = std::move(dec.image);
            decoded = true;
        } else if (dec.blocksOk) {  // the compressed upload failed: decode it here after all
            decoded = app::loadDdsPreview(path->toStdString(), textureSideNow(), img, &fullW, &fullH);
        }
        if (decoded && img.width > 0 && img.height > 0) {
            // Stock colour: alpha is specular, never opacity; a cut-out mask (when the material has one) replaces it.
            const QString maskPath = maskLookup_ ? maskLookup_(material) : QString();
            app::Rgba8Image mask;
            const bool haveMask = !maskPath.isEmpty() && app::loadDdsPreview(maskPath.toStdString(), kViewTextureSide, mask) &&
                                  mask.width > 0 && mask.height > 0 &&
                                  mask.pixels.size() >= size_t(mask.width) * size_t(mask.height) * 4;
            // Research 66: the opacity image (texture slot 27) is either one channel (_sdf, _opacity, _a, _m: red) or a
            // colour image whose alpha is the opacity (Ukraine trees, effect sheets): read alpha when it varies.
            // Alpha-tested sets cut at half (as the game's alpha test); blended sets keep the soft value.
            int maskChannel = 0;
            if (haveMask) {
                for (size_t k = 3; k < mask.pixels.size(); k += 4)
                    if (mask.pixels[k] < 250) {
                        maskChannel = 3;
                        break;
                    }
            }
            const bool softMask = ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(material) != 0;
            // Opaque stock images keep their specular code (research 95: metal / f0) as 192..254: never under any
            // cut-out threshold, and 255 still means "no code" (masked images, editor textures).
            const bool keepSpec = !haveMask && !softMask;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    uint8_t a = 255;
                    if (keepSpec) {
                        const uint8_t s = img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3];
                        a = uint8_t(192 + (int(s) * 62 + 127) / 255);
                    }
                    if (haveMask) {
                        const int mx = int(int64_t(x) * mask.width / img.width), my = int(int64_t(y) * mask.height / img.height);
                        const uint8_t v = mask.pixels[(size_t(my) * size_t(mask.width) + size_t(mx)) * 4 + size_t(maskChannel)];
                        a = softMask ? v : (v >= 128 ? 255 : 0);
                    }
                    img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3] = a;
                }
        }
    } else {
        // Custom materials (doc 21): PNG / TGA / JPG / BMP through the same decoder the compiler uses.
        decoded = dec.imageOk;
        if (decoded) img = std::move(dec.image);
        if (decoded && customTint_)
            if (const auto tint = customTint_(material)) {
                const size_t texels = img.pixels.size() / 4;
                for (size_t i = 0; i < texels; ++i)
                    for (int c = 0; c < 3; ++c)
                        img.pixels[i * 4 + size_t(c)] = uint8_t(std::clamp(img.pixels[i * 4 + size_t(c)] * (*tint)[size_t(c)], 0.0, 255.0));
            }
        fullW = img.width;
        fullH = img.height;
    }
    if (decoded) {
        // A stock material's colour tint (core material_tints): the camera shows what the build bakes in.
        if (const auto tint = MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(material);
            tint && img.pixels.size() >= size_t(img.width) * size_t(img.height) * 4) {
            applyMaterialTint(img.pixels.data(), size_t(img.width) * size_t(img.height), *tint);
        } else if (factsLookup_ && material.find_first_of("\x01\x02") == std::string::npos &&
                   img.pixels.size() >= size_t(img.width) * size_t(img.height) * 4) {
            // The material's own tint (constant 9) when no tint mask limits it: painted walls and roofs (owner
            // comparison 2026-11-12: Speedball's REMONT building came out white instead of green wood and a red roof).
            if (const auto f = factsLookup_(material); f && f->tint >= 0) {
                // The tint multiplies linear albedo; the image is sRGB: its 1/2.2 power in display terms.
                const double t[3] = {std::pow(double(f->tint >> 16 & 255) / 255.0, 1.0 / 2.2), std::pow(double(f->tint >> 8 & 255) / 255.0, 1.0 / 2.2),
                                     std::pow(double(f->tint & 255) / 255.0, 1.0 / 2.2)};
                app::Rgba8Image mask;
                const bool masked = f->tintMasked && !f->tintMaskFile.empty() &&
                                    app::loadDdsPreview(f->tintMaskFile, std::max(img.width, img.height), mask) && mask.width > 0 && mask.height > 0;
                if (!f->tintMasked || masked) {
                    // albedo x lerp(1, tint, mask.r) (research 95); a masked tint without its mask image is left out.
                    for (int y = 0; y < img.height; ++y)
                        for (int x = 0; x < img.width; ++x) {
                            double m = 1.0;
                            if (masked) {
                                const int mx = std::min(mask.width - 1, x * mask.width / std::max(1, img.width));
                                const int my = std::min(mask.height - 1, y * mask.height / std::max(1, img.height));
                                m = mask.pixels[(size_t(my) * size_t(mask.width) + size_t(mx)) * 4] / 255.0;
                            }
                            uint8_t* q = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
                            for (int c = 0; c < 3; ++c) q[c] = uint8_t(std::clamp(q[c] * (1.0 + (t[c] - 1.0) * m), 0.0, 255.0));
                        }
                }
            }
        }
        texSizes_[material] = {fullW, fullH};
        const int side = textureSideNow();
        if (side < kViewTextureSide) ++budgetReduced_;
        if (std::max(img.width, img.height) > side) img = app::downscale(img, side);
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    }
    g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
    if (shareable && id >= 0) texByPath_[path->toStdString()] = id;
    return texIds_[material] = id;
}

void CameraView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kAssetMimeType)) e->acceptProposedAction();
}
void CameraView::dragMoveEvent(QDragMoveEvent* e) {
    if (!e->mimeData()->hasFormat(kAssetMimeType)) return;
    e->acceptProposedAction();
    const QStringList f = QString::fromUtf8(e->mimeData()->data(kAssetMimeType)).split('\t');
    if (f.size() >= 2) emit assetHovered(f[0], f[1], e->position());
}
void CameraView::dragLeaveEvent(QDragLeaveEvent*) { emit assetHoverEnded(); }
void CameraView::dropEvent(QDropEvent* e) {
    const QStringList f = QString::fromUtf8(e->mimeData()->data(kAssetMimeType)).split('\t');
    if (f.size() < 2) return;
    e->acceptProposedAction();
    emit assetDropped(f[0], f[1], f.value(2) == "1", e->position());
}

size_t CameraView::loadedNormalMaps() const {
    size_t n = 0;
    for (const auto& [m, id] : nrmIds_) n += id >= 0;
    return n;
}

void CameraView::appendEffectParticles() {
    // Camera-facing quads per particle, in its colour, one blended batch over a white texture.
    if (whiteTex_ < 0) {
        const uint8_t px[4] = {255, 255, 255, 255};
        whiteTex_ = renderer_.createTexture(px, 1, 1);
    }
    const double yaw = ctx_->cam.yaw * 3.14159265358979 / 180.0, pitch = ctx_->cam.pitch * 3.14159265358979 / 180.0;
    const Vec3 fwd{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    const Vec3 right = normalize(cross(fwd, Vec3{0, 0, 1}));
    const Vec3 up = normalize(cross(right, fwd));
    if (!fxClock_.isValid()) fxClock_.start();
    const double t = fxTimeOverride_ >= 0 ? fxTimeOverride_ : fxClock_.elapsed() / 1000.0;
    app::SceneBatch& b = fxBatch_;
    b.v.clear();
    b.markAll();
    b.texture = whiteTex_;
    b.blend = 1;
    fxParticles_ = 0;
    uint32_t seed = 1;
    for (const Entity& e : ctx_->doc.map().entities) {
        if (e.classname() != "fx_origin") continue;
        const std::string* fx = e.value("fx");
        const auto o = e.origin();
        if (!fx || !o) continue;
        const auto def = effectSource_(*fx);
        ++seed;
        if (!def || def->emitters.empty()) continue;
        const double loop = def->duration > 0 ? def->duration + 0.5 : 4.0;  // one-shots replay after a short pause
        for (const VfxParticle& p : sampleVfx(*def, std::fmod(t, loop), seed)) {
            const Vec3 c = *o + Vec3{p.pos[0], p.pos[1], p.pos[2]};
            const double h = std::max(1.0, p.size * 0.5);
            const uint32_t rgba = uint32_t(std::clamp(p.rgba[0], 0.f, 1.f) * 255) | (uint32_t(std::clamp(p.rgba[1], 0.f, 1.f) * 255) << 8) |
                                  (uint32_t(std::clamp(p.rgba[2], 0.f, 1.f) * 255) << 16) | 0xff000000u;
            auto v = [&](double sx, double sy, float u, float vv) {
                const Vec3 q = c + right * (sx * h) + up * (sy * h);
                return app::GpuTexVertex{float(q.x), float(q.y), float(q.z), u, vv, rgba};
            };
            const app::GpuTexVertex a = v(-1, -1, 0, 1), bb = v(1, -1, 1, 1), cc = v(1, 1, 1, 0), d = v(-1, 1, 0, 0);
            b.v.insert(b.v.end(), {a, bb, cc, a, cc, d});
            if (++fxParticles_ >= 20000) break;
        }
    }
}

void CameraView::scanProbes() {
    // The captured probes on disk: their positions and files (looked at again by render() now and then).
    probes_.clear();
    const QString dir = probeFolder_ ? probeFolder_() : QString();
    if (dir.isEmpty()) return;
    for (const QString& f : QDir(dir).entryList({"probe*.pos"}, QDir::Files)) {
        QFile pf(dir + "/" + f);
        if (!pf.open(QIODevice::ReadOnly)) continue;
        const QStringList v = QString::fromUtf8(pf.readAll()).split(' ', Qt::SkipEmptyParts);
        if (v.size() < 3) continue;
        const QString rgba = dir + "/" + QFileInfo(f).completeBaseName() + ".rgba";
        const QFileInfo info(rgba);
        if (!info.exists()) continue;
        probes_.push_back({Vec3{v[0].toDouble(), v[1].toDouble(), v[2].toDouble()}, rgba, info.lastModified().toMSecsSinceEpoch()});
    }
}

void CameraView::updateReflectionProbe() {
    // The probe nearest the camera (textured view only), from the scanned list: no file access per frame.
    const ProbeFile* best = nullptr;
    double bestD = 1e300;
    if (ctx_->texturedView)
        for (const ProbeFile& p : probes_) {
            const double d = length(p.pos - ctx_->cam.pos);
            if (d < bestD) bestD = d, best = &p;
        }
    if (best ? (activeProbePath_ == best->rgba && activeProbeMtime_ == best->mtime) : activeProbe_.isEmpty()) return;
    activeProbePath_ = best ? best->rgba : QString();
    activeProbeMtime_ = best ? best->mtime : 0;
    activeProbe_ = best ? best->rgba + "|" + QString::number(best->mtime) : QString();
    std::string bytes;
    if (best && readFile(best->rgba.toStdString(), bytes) && bytes.size() == size_t(6) * 64 * 64 * 4)
        renderer_.setReflectionProbe(reinterpret_cast<const uint8_t*>(bytes.data()), 64);
    else
        renderer_.setReflectionProbe(nullptr, 0);
}

int CameraView::customShaderFor(const std::string& material) {
    if (!shaderLookup_ || !rendererOk_) return -1;
    const std::shared_ptr<const CustomShader> shader = shaderLookup_(material);
    if (!shader || !customShaderHasSurface(shader->code)) return -1;  // a filter alone lights nothing itself
    const std::string key = shader->path + '\n' + shader->code;
    if (const auto it = customShaderIds_.find(key); it != customShaderIds_.end()) return it->second;
    std::string err;
    const std::string src = customShaderViewportSource(app::SceneRenderer::texShaderSource(), "float4 ps_tex(PSIn i) : SV_TARGET {", *shader);
    const int id = src.empty() ? -1 : renderer_.createCustomShader(src, &err);
    customShaderIds_[key] = id;
    if (id < 0) {
        customShaderError_ = shader->name + ": " + (err.empty() ? std::string("cannot be built into the viewport shader") : err);
        if (ctx_) emit ctx_->statusMessage(QString::fromStdString("Custom shader " + customShaderError_));
    }
    return id;
}

int CameraView::emissiveFor(const std::string& material) {
    if (material.empty() || isIw8ToolMaterial(material)) return -1;
    if (auto it = emiIds_.find(material); it != emiIds_.end()) return it->second;
    if (auto t = texIds_.find(material); t == texIds_.end() || t->second < 0 || t->second == checkerTex_) return -1;
    const QString path = emissiveLookup_ ? emissiveLookup_(material) : QString();
    if (path.isEmpty()) return emiIds_[material] = -1;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    int id = -1;
    app::Rgba8Image img;
    // The texture budget's size rule as the other images (emissive images always loaded at full preview size).
    DecodedTexture dec;
    if (!decodedTexture(path.toStdString(), textureSideNow(), 0, dec)) {  // a worker is decoding it
        --loadsThisFrame_;
        moreToLoad_ = true;
        return -1;
    }
    if (dec.blocksOk) id = renderer_.createTextureBlocks(dec.blocks.dxgiFormat, dec.blocks.width, dec.blocks.height, dec.blocks.levels);
    if (id < 0 && dec.imageOk) id = renderer_.createTexture(dec.image.pixels.data(), dec.image.width, dec.image.height);
    if (id < 0 && dec.blocksOk && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    return emiIds_[material] = id;
}

Vec3 CameraView::bakeAlbedoFor(const std::string& material) {
    const Vec3 unknown{-1, -1, -1};
    if (material.empty() || !imageLookup_) return unknown;
    const std::optional<QString> path = imageLookup_(material);
    if (!path || path->isEmpty()) return unknown;  // not exported yet, or no image
    const std::string file = path->toStdString();
    if (const auto it = bakeAlbedo_.find(file); it != bakeAlbedo_.end()) return it->second;
    Vec3 a = unknown;
    if (path->endsWith(".dds", Qt::CaseInsensitive)) {
        app::Rgba8Image img;  // a small level is enough for an average
        if (app::loadDdsPreview(file, 32, img) && img.width > 0 && img.height > 0)
            a = bakeAlbedoFromRgba(img.pixels.data(), size_t(img.width) * size_t(img.height));
    } else {
        int w = 0, h = 0;
        std::vector<uint8_t> px;
        std::string err;
        if (decodeImageFile(file, w, h, px, &err) && w > 0 && h > 0)
            a = bakeAlbedoFromRgba(px.data(), size_t(w) * size_t(h), std::max<size_t>(1, size_t(w) * size_t(h) / 4096));
    }
    return bakeAlbedo_[file] = a;
}

int CameraView::specFor(const std::string& material) {
    if (material.empty() || isIw8ToolMaterial(material)) return -1;
    if (auto it = specIds_.find(material); it != specIds_.end()) return it->second;
    // Only once the colour image is up, as the other maps.
    if (auto t = texIds_.find(material); t == texIds_.end() || t->second < 0 || t->second == checkerTex_) return -1;
    const QString path = specLookup_ ? specLookup_(material) : QString();
    if (path.isEmpty()) return specIds_[material] = -1;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    // The image as it is stored: the build's shader reads the specular map's bytes without a colour conversion.
    int w = 0, h = 0, id = -1;
    std::vector<uint8_t> px;
    std::string err;
    if (decodeImageFile(path.toStdString(), w, h, px, &err)) id = renderer_.createTexture(px.data(), w, h);
    return specIds_[material] = id;
}

std::pair<int, int> CameraView::materialMapIds(const std::string& material) {
    textureFor(material);
    loadsThisFrame_ = 0;
    const int n = normalFor(material);
    loadsThisFrame_ = 0;
    const int s = specFor(material);
    return {nrmIds_.count(material) ? n : -2, specIds_.count(material) ? s : -2};
}

int CameraView::normalFor(const std::string& material) {
    if (material.empty() || isIw8ToolMaterial(material)) return -1;
    if (auto it = nrmIds_.find(material); it != nrmIds_.end()) return it->second;
    // Only once the colour image is up: the material is then known to the cache, and colour loads go first.
    if (auto t = texIds_.find(material); t == texIds_.end() || t->second < 0 || t->second == checkerTex_) return -1;
    const QString path = normalLookup_ ? normalLookup_(material) : QString();
    if (path.isEmpty()) return nrmIds_[material] = -1;
    // Normal maps are plain files: one texture per file (shared between the materials and layers that use it).
    if (const auto it = nrmByPath_.find(path.toStdString()); it != nrmByPath_.end()) return nrmIds_[material] = it->second;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    if (path.startsWith("slope:")) {
        // An own material's normal image. The build reads it as an IW3 slope map (slope x = 4.08 R - 2.08, slope y =
        // 4.0645 G - 2.0645, the converter's world shader); packed here the way the stock normal maps are, which is
        // what the viewport's shaders decode. No gloss in it (that is the specular map's alpha), full occlusion.
        int w = 0, h = 0, id = -1;
        std::vector<uint8_t> px;
        std::string err;
        if (decodeImageFile(path.mid(6).toStdString(), w, h, px, &err)) {
            for (size_t k = 0; k + 3 < px.size(); k += 4) {
                uint8_t g = 0, a = 0;
                replayNormalPack(4.08 * px[k] / 255.0 - 2.08, 4.06451607 * px[k + 1] / 255.0 - 2.06451607, g, a);
                px[k] = 0, px[k + 1] = g, px[k + 2] = 255, px[k + 3] = a;
            }
            id = renderer_.createTexture(px.data(), w, h);
        }
        if (id >= 0) nrmByPath_[path.toStdString()] = id;
        return nrmIds_[material] = id;
    }
    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    // The same size rule as the colour images (they had always loaded at full preview size, over the budget). BC1 /
    // BC3 / BC7 normal maps go up compressed (every channel as stored); BC5 and the rest are decoded.
    DecodedTexture dec;
    if (!decodedTexture(path.toStdString(), textureSideNow(), 0, dec)) {  // a worker is decoding it
        --loadsThisFrame_;
        moreToLoad_ = true;
        return -1;
    }
    if (dec.blocksOk) id = renderer_.createTextureBlocks(dec.blocks.dxgiFormat, dec.blocks.width, dec.blocks.height, dec.blocks.levels);
    if (id < 0 && dec.imageOk) id = renderer_.createTexture(dec.image.pixels.data(), dec.image.width, dec.image.height);
    if (id < 0 && dec.blocksOk && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
    if (id >= 0) nrmByPath_[path.toStdString()] = id;
    return nrmIds_[material] = id;
}

QString CameraView::geometrySelfCheck() {
    std::vector<app::GpuVertex> t1, l1, o1, t2, l2, o2;
    std::vector<app::TexturedBatch> x1, x2;
    geo_.flatten(t1, l1, o1, x1);
    const size_t shadowOnly = ctx_->scene.shadowOnlyVertices, casters = ctx_->scene.shadowCasterVertices;
    if (ctx_->texturedView && imageLookup_)
        app::buildCameraGeometry(ctx_->scene, ctx_->doc, ctx_->showSpawns, t2, l2, o2, [this](const std::string& m) { return textureFor(m); }, x2);
    else
        app::buildCameraGeometry(ctx_->scene, ctx_->doc, ctx_->showSpawns, t2, l2, o2);
    const auto same = [](const std::vector<app::GpuVertex>& a, const std::vector<app::GpuVertex>& b) {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(app::GpuVertex)) == 0);
    };
    if (!same(t1, t2)) return QString("flat triangles differ (%1 / %2 vertices)").arg(t1.size()).arg(t2.size());
    if (!same(l1, l2)) return QString("lines differ (%1 / %2 vertices)").arg(l1.size()).arg(l2.size());
    if (!same(o1, o2)) return QString("on-top lines differ (%1 / %2 vertices)").arg(o1.size()).arg(o2.size());
    if (shadowOnly != ctx_->scene.shadowOnlyVertices || casters != ctx_->scene.shadowCasterVertices)
        return QString("shadow caster counts differ (%1 %2 / %3 %4)").arg(shadowOnly).arg(casters).arg(ctx_->scene.shadowOnlyVertices).arg(ctx_->scene.shadowCasterVertices);
    if (x1.size() != x2.size()) return QString("textured batch counts differ (%1 / %2)").arg(x1.size()).arg(x2.size());
    for (size_t k = 0; k < x1.size(); ++k) {
        const app::TexturedBatch& a = x1[k];
        const app::TexturedBatch& b = x2[k];
        if (a.texture != b.texture || a.normalTexture != b.normalTexture || a.emissiveTexture != b.emissiveTexture || a.specTexture != b.specTexture ||
            a.blend != b.blend ||
            a.sway != b.sway || a.baseZ != b.baseZ)
            return QString("textured batch %1: settings differ").arg(k);
        if (a.tris.size() != b.tris.size() || (!a.tris.empty() && std::memcmp(a.tris.data(), b.tris.data(), a.tris.size() * sizeof(app::GpuTexVertex)) != 0)) {
            QString where;
            for (size_t i = 0; i < std::min(a.tris.size(), b.tris.size()); ++i)
                if (std::memcmp(&a.tris[i], &b.tris[i], sizeof(app::GpuTexVertex)) != 0) {
                    const app::GpuTexVertex &p = a.tris[i], &q = b.tris[i];
                    where = QString(", first at %1: rgba %2/%3 uv %4,%5/%6,%7 lm %8,%9/%10,%11").arg(i).arg(p.rgba, 0, 16).arg(q.rgba, 0, 16)
                                .arg(p.u).arg(p.v).arg(q.u).arg(q.v).arg(p.lu).arg(p.lv).arg(q.lu).arg(q.lv);
                    break;
                }
            return QString("textured batch %1: vertices differ (%2 / %3%4)").arg(k).arg(a.tris.size()).arg(b.tris.size()).arg(where);
        }
    }
    return {};
}

size_t CameraView::renderAndCount() {
    render();
    return renderer_.countNonClearPixels(kClear);
}

QImage CameraView::renderImage(int w, int h, bool clean, double aspect, int settleMs, bool keepGeometry) {
    if (!rendererOk_ || w < 1 || h < 1 || w > 16384 || h > 16384) return {};
    captureW_ = w, captureH_ = h, captureAspect_ = aspect, cleanCapture_ = clean;
    const bool remake = clean || !keepGeometry;
    if (remake) geomDirty_ = true;
    QElapsedTimer t;
    t.start();
    render();
    while ((moreToLoad_ || refMoreToLoad_) && t.elapsed() < settleMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        geomDirty_ = true;
        render();
    }
    std::vector<uint8_t> px;
    int rw = 0, rh = 0;
    const bool ok = renderer_.readback(px, rw, rh);
    captureW_ = captureH_ = 0, captureAspect_ = 0, cleanCapture_ = false;
    if (remake) geomDirty_ = true;
    update();
    if (!ok || rw != w || rh != h) return {};
    QImage img(px.data(), w, h, w * 4, QImage::Format_RGBA8888);
    return img.convertToFormat(QImage::Format_RGB888);  // opaque: the clear colour's alpha is not meaningful
}

QImage CameraView::grabFrame() {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!renderer_.readback(px, w, h)) return {};
    return QImage(px.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
}

std::optional<Vec3> CameraView::surfacePoint(const QPointF& p, SelRef* hit) const {
    const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
    const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
    double t = 0;
    const auto r = ctx_->scene.pick(ray, &t);
    if (!r) return std::nullopt;
    if (hit) *hit = *r;
    return ray.origin + ray.dir * t;
}

void CameraView::updateSkyBox() {
    if (!ctx_->scene.skyActive || skyTex_.size() != 6) return;
    // A box centred on the camera, inside the 131072 far plane: the sky is infinitely far, as in game (the stock sky
    // shader samples by view direction), so moving never slides the image (owner report 2026-11-12: the map's own box
    // made the image look bound to the cube). Re-centred every frame; only a moved camera uploads its 72 vertices.
    const Vec3 c = ctx_->cam.pos;
    const double R = 60000;
    while (skyBatches_.size() < 6) skyBatches_.push_back(std::make_unique<app::SceneBatch>());
    for (int f = 0; f < 6; ++f) {
        // Corner (u, v) of face f -> world direction, inverting the build's cube sampling (x64-zt SampleCubemapLinear).
        auto dir = [f](double u, double v) -> Vec3 {
            switch (f) {
                case 0: return {1, -v, -u};
                case 1: return {-1, -v, u};
                case 2: return {u, 1, v};
                case 3: return {u, -1, -v};
                case 4: return {u, -v, 1};
                default: return {-u, -v, -1};
            }
        };
        app::SceneBatch& b = *skyBatches_[size_t(f)];
        auto vtx = [&](double u, double v) {
            const Vec3 p = c + dir(u, v) * R;
            // Alpha 0 marks sky in the textured shader (no fog).
            return app::GpuTexVertex{float(p.x), float(p.y), float(p.z), float((u + 1) / 2), float((v + 1) / 2), 0x00ffffffu};
        };
        const app::GpuTexVertex a = vtx(-1, -1), bb = vtx(1, -1), cc = vtx(1, 1), d = vtx(-1, 1);
        // Seen from inside: both windings, so culling never hides a face.
        const app::GpuTexVertex quad[12] = {a, bb, cc, a, cc, d, a, cc, bb, a, d, cc};
        if (b.texture == skyTex_[size_t(f)] && b.v.size() == 12 && std::memcmp(b.v.data(), quad, sizeof(quad)) == 0) continue;
        b.texture = skyTex_[size_t(f)];
        b.v.assign(quad, quad + 12);
        b.markAll();
        b.structural = true;
    }
}

std::vector<uint8_t> CameraView::captureCube(const Vec3& pos, int size) {
    const int w = std::max(1, width()), h = std::max(1, height());
    const double aspect = double(w) / h;
    const app::Camera saved = ctx_->cam;
    const std::set<SelRef> savedSel = ctx_->doc.selection();
    ctx_->doc.deselectAll();
    // A face spans +-45 degrees both ways: widen the horizontal FOV so the vertical one covers it with margin.
    const double halfV = 50.0 * 3.14159265358979 / 180.0;
    const double fovH = 2.0 * std::atan(aspect * std::tan(halfV)) * 180.0 / 3.14159265358979;
    const double views[6][2] = {{0, 0}, {180, 0}, {90, 0}, {270, 0}, {0, 89.99}, {0, -89.99}};  // yaw, pitch for +X -X +Y -Y +Z -Z
    std::array<std::vector<uint8_t>, 6> frames;
    std::array<app::Camera, 6> cams;
    ctx_->scene.captureMode = true;
    struct Restore {
        app::SceneCache& s;
        ~Restore() { s.captureMode = false; }
    } restoreCapture{ctx_->scene};
    int fw = 0, fh = 0;
    for (int f = 0; f < 6; ++f) {
        app::Camera c = saved;
        c.pos = pos;
        c.yaw = views[f][0];
        c.pitch = views[f][1];
        c.fov = fovH;
        ctx_->cam = c;
        cams[size_t(f)] = c;
        render();
        if (!renderer_.readback(frames[size_t(f)], fw, fh) || fw <= 0 || fh <= 0) {
            ctx_->cam = saved;
            for (const SelRef& r : savedSel) ctx_->doc.select(r, true);
            update();
            return {};
        }
    }
    ctx_->cam = saved;
    for (const SelRef& r : savedSel) ctx_->doc.select(r, true);
    filesDue_ = true;  // captures are written to disk next: the frame after looks for them
    update();
    std::vector<uint8_t> out(size_t(6) * size_t(size) * size_t(size) * 4);
    for (int f = 0; f < 6; ++f) {
        const app::Mat4f vp = app::viewProjection(cams[size_t(f)], aspect);
        const float* m = vp.m;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const double u = 2.0 * (x + 0.5) / size - 1.0, v = 2.0 * (y + 0.5) / size - 1.0;
                // Inverse of the converter's per-face (u, v) for a direction on that face.
                Vec3 d;
                switch (f) {
                    case 0: d = {1, -v, -u}; break;
                    case 1: d = {-1, -v, u}; break;
                    case 2: d = {u, 1, v}; break;
                    case 3: d = {u, -1, -v}; break;
                    case 4: d = {u, -v, 1}; break;
                    default: d = {-u, -v, -1}; break;
                }
                const Vec3 p = pos + normalize(d) * 1024.0;
                // Row-vector times row-major matrix.
                const double cx = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
                const double cy = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
                const double cw = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
                uint8_t* o = &out[((size_t(f) * size_t(size) + size_t(y)) * size_t(size) + size_t(x)) * 4];
                o[3] = 255;
                if (cw <= 1e-6) continue;
                const int px = std::clamp(int((cx / cw * 0.5 + 0.5) * fw), 0, fw - 1);
                const int py = std::clamp(int((0.5 - cy / cw * 0.5) * fh), 0, fh - 1);
                const uint8_t* s = &frames[size_t(f)][(size_t(py) * size_t(fw) + size_t(px)) * 4];
                o[0] = s[0];
                o[1] = s[1];
                o[2] = s[2];
            }
    }
    return out;
}

void CameraView::paintEvent(QPaintEvent*) { render(); }

void CameraView::resizeEvent(QResizeEvent*) { update(); }

void CameraView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    if (ctx_->playing) return;  // the mouse only looks around while playing
    if (e->button() == Qt::RightButton) {
        ctx_->looking = true;
        lookTravel_ = 0;
        lockPos_ = e->position().toPoint();
        setCursor(Qt::BlankCursor);
    } else if (e->button() == Qt::LeftButton) {
        lmbDown_ = true;
        pressPos_ = e->position().toPoint();
        // Rotate mode: grab a ring within 8 px.
        if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) && gizmoActive() && gizmoRotate_) {
            const QPointF p = e->position();
            // Rings seen edge-on project to a line through the centre and pass over the other rings' points; when two
            // rings are about as close, prefer the one facing the camera (its angle is well defined there).
            int best = -1;
            double bestScore = 1e9;
            const Vec3 view = ctx_->cam.forward();
            for (int a = 0; a < 3; ++a) {
                const double facing = std::fabs(view[a]);  // |cos| between the ring's axis and the view direction
                for (int k = 0; k < 72; ++k) {
                    const auto q = gizmoRingPoint(a, k * 5.0);
                    if (!q) continue;
                    const double dist = std::hypot(p.x() - q->x(), p.y() - q->y());
                    if (dist >= 8.0) continue;
                    const double score = dist - 4.0 * facing;
                    if (score < bestScore) {
                        bestScore = score;
                        best = a;
                    }
                }
            }
            if (best >= 0) {
                ringDragAxis_ = best;
                ringPivot_ = snapToGrid(gizmoPivot(), ctx_->doc.grid());
                ringStartAngle_ = ringAngleForMouse(p, best).value_or(0);
                ringDeltaDeg_ = 0;
                lmbDown_ = false;
                update();
                return;
            }
        }
        // Grab a gizmo handle: within 8 px of an axis segment on screen.
        if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) && gizmoActive() && !gizmoRotate_) {
            const QPointF p = e->position();
            int best = -1;
            double bestD = 8.0;
            for (int a = 0; a < 3; ++a) {
                const GizmoAxis g = gizmoAxisOnScreen(a);
                if (!g.visible) continue;
                const QPointF ab = g.to - g.from;
                const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
                if (len2 < 4) continue;
                double t = ((p.x() - g.from.x()) * ab.x() + (p.y() - g.from.y()) * ab.y()) / len2;
                t = std::clamp(t, 0.15, 1.0);  // ignore the shared centre
                const QPointF q = g.from + ab * t;
                const double dist = std::hypot(p.x() - q.x(), p.y() - q.y());
                if (dist < bestD) {
                    bestD = dist;
                    best = a;
                }
            }
            if (best >= 0 && gizmoScale_) {
                gizmoDragAxis_ = best;
                gizmoStartPivot_ = gizmoPivot();
                gizmoStartT_ = axisParamForMouse(p, best);
                scaleFactor_ = 1.0;
                lmbDown_ = false;
                update();
            } else if (best >= 0) {
                gizmoDragAxis_ = best;
                gizmoStartPivot_ = gizmoPivot();
                gizmoStartT_ = axisParamForMouse(p, best);
                ctx_->doc.beginDrag();
                lmbDown_ = false;  // not a click-select
                update();
            }
        }
    }
}

void CameraView::mouseMoveEvent(QMouseEvent* e) {
    if (ringDragAxis_ >= 0) {
        if (auto a = ringAngleForMouse(e->position(), ringDragAxis_)) {
            double d = std::remainder(*a - ringStartAngle_, 360.0);
            d = ctx_->snap ? std::round(d / 15.0) * 15.0 : d;  // Radiant rotation snap
            ringDeltaDeg_ = d;
            ctx_->setStatus(QString("Rotate %1 %2 degrees").arg(QChar("XYZ"[ringDragAxis_])).arg(d));
            update();
        }
        return;
    }
    if (gizmoDragAxis_ >= 0 && gizmoScale_) {
        // Factor from how far the handle moved relative to the gizmo length; snaps to 0.05 steps.
        const double t = axisParamForMouse(e->position(), gizmoDragAxis_);
        double f = std::max(0.05, 1.0 + (t - gizmoStartT_) / std::max(1.0, gizmoLength()));
        if (ctx_->snap) f = std::max(0.05, std::round(f * 20.0) / 20.0);
        scaleFactor_ = f;
        ctx_->setStatus(QString("Scale %1 x%2").arg(QChar("XYZ"[gizmoDragAxis_])).arg(f, 0, 'f', 2));
        update();
        return;
    }
    if (gizmoDragAxis_ >= 0) {
        const double t = axisParamForMouse(e->position(), gizmoDragAxis_);
        const double s = ctx_->snap ? snapToGrid(t - gizmoStartT_, ctx_->doc.grid()) : t - gizmoStartT_;
        const Vec3 d = gizmoAxisDir(gizmoDragAxis_) * s;  // world axes: s along X/Y/Z; local: along the entity's axis
        ctx_->doc.updateDrag(d);
        ctx_->documentEdited(QString("Move %1 %2").arg(QChar("XYZ"[gizmoDragAxis_])).arg(QString::fromStdString(formatNumber(d[gizmoDragAxis_]))));
        return;
    }
    if (!ctx_->looking) return;
    const QPoint p = e->position().toPoint();
    const QPoint d = p - lockPos_;
    if (d.isNull()) return;
    lookTravel_ += std::abs(d.x()) + std::abs(d.y());
    auto& cam = ctx_->cam;
    if ((e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)) {
        // Stock Ctrl+RMB: drag forward/back along the view and yaw.
        cam.yaw -= d.x() * 0.25;
        cam.pos += cam.forward() * (-d.y() * cam.speed / 250.0);
    } else {
        cam.yaw -= d.x() * 0.2;
        cam.pitch = std::clamp(cam.pitch - d.y() * 0.2, -89.0, 89.0);
    }
    cam.yaw = std::fmod(cam.yaw + 360.0, 360.0);
    QCursor::setPos(mapToGlobal(lockPos_));
    ctx_->cameraMoved();
}

void CameraView::mouseReleaseEvent(QMouseEvent* e) {
    if (ctx_->playing) return;
    if (e->button() == Qt::LeftButton && ringDragAxis_ >= 0) {
        const int axis = ringDragAxis_;
        ringDragAxis_ = -1;
        const bool done = ringDeltaDeg_ != 0 && rotateSelectionAbout(*ctx_, axis, ringDeltaDeg_, ringPivot_);
        ctx_->documentEdited(done ? QString("Rotated %1 degrees about %2 (undoable)").arg(ringDeltaDeg_).arg(QChar("XYZ"[axis])) : QString());
        ringDeltaDeg_ = 0;
        return;
    }
    if (e->button() == Qt::LeftButton && gizmoDragAxis_ >= 0 && gizmoScale_) {
        const int axis = gizmoDragAxis_;
        gizmoDragAxis_ = -1;
        const double f = scaleFactor_;
        scaleFactor_ = 1.0;
        const bool done = scaleSelectionAbout(*ctx_, axis, f, gizmoStartPivot_);
        ctx_->documentEdited(done ? QString("Scaled %1 x%2 (models: modelscale; undoable)").arg(QChar("XYZ"[axis])).arg(f, 0, 'f', 2) : QString());
        return;
    }
    if (e->button() == Qt::LeftButton && gizmoDragAxis_ >= 0) {
        gizmoDragAxis_ = -1;
        const bool moved = ctx_->doc.endDrag(true);
        ctx_->documentEdited(moved ? QStringLiteral("Moved with the gizmo (undoable)") : QString());
        return;
    }
    if (e->button() == Qt::RightButton) {
        ctx_->looking = false;
        unsetCursor();
        if (lookTravel_ < 4) emit contextMenuAt(mapToGlobal(lockPos_), QPointF(lockPos_));
        return;
    }
    if (e->button() != Qt::LeftButton || !lmbDown_) return;
    lmbDown_ = false;
    const QPoint p = e->position().toPoint();
    if ((p - pressPos_).manhattanLength() > 4) return;
    const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
    const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
    ctx_->scene.sync(ctx_->doc);
    if (ctx_->decalMode && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))) {
        double t = 0;
        if (const auto hit = ctx_->scene.pick(ray, &t)) {
            const int n = ctx_->decalAt(ray, t, *hit);
            if (n) ctx_->documentEdited(QString("Decal %1 placed (%2 piece(s), one undo step; Brush > Decal Mode to stop)").arg(QString::fromStdString(ctx_->currentMaterial)).arg(n));
            else ctx_->setStatus(ctx_->currentMaterial.empty() ? "Decal mode: pick the decal material first (Materials browser)" : "Decal mode: nothing to place there");
        }
        return;
    }
    if (ctx_->vertexPaint && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))) {
        double t = 0;
        if (ctx_->scene.pick(ray, &t)) {
            const int n = ctx_->vertexPaintAt(ray.origin + ray.dir * t);
            if (n) ctx_->documentEdited(QString("Vertex paint: %1 control point(s) (one undo step; Patch > Vertex Paint Mode to stop)").arg(n));
            else ctx_->setStatus("Vertex paint: no patch control point within the brush radius");
        }
        return;
    }
    if (!ctx_->paintModel.isEmpty() && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))) {
        // Model paint mode (8.3): scatter around the clicked surface instead of selecting.
        double t = 0;
        if (ctx_->scene.pick(ray, &t)) {
            const Vec3 at = ray.origin + ray.dir * t;
            const size_t n = paintModelsAt(*ctx_, ctx_->paintModel, at, ctx_->paintCount, ctx_->paintRadius,
                                           uint32_t(QDateTime::currentMSecsSinceEpoch()));
            ctx_->documentEdited(QString("Painted %1 x %2 (one undo step; Tools > Model Paint Mode to stop)").arg(n).arg(ctx_->paintModel));
        }
        return;
    }
    if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::ControlModifier)) {
        // Radiant face selection: Ctrl+Shift+LMB toggles one brush face.
        if (auto face = ctx_->scene.pickFace(ray)) {
            ctx_->doc.toggleFace(*face);
            const EntityItem* it = ctx_->doc.findItem(face->entity, face->item);
            const Primitive* p = it ? it->prim() : nullptr;
            ctx_->documentEdited(QString("%1 face(s) selected%2")
                                     .arg(ctx_->doc.faceSelection().size())
                                     .arg(p && face->face < p->faces.size() ? " - " + QString::fromStdString(p->faces[face->face].material()) : QString()));
        }
        return;
    }
    if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::AltModifier)) {
        // Radiant select cycle: the next thing behind the current selection along this ray.
        const auto hits = ctx_->scene.pickAll(ray);
        if (!hits.empty()) {
            size_t next = 0;
            for (size_t i = 0; i < hits.size(); ++i)
                if (ctx_->doc.isSelected(hits[i])) next = (i + 1) % hits.size();
            ctx_->doc.select(hits[next], false);
            ctx_->documentEdited(QString("Select cycle: %1 of %2 under the cursor").arg(next + 1).arg(hits.size()));
        }
        return;
    }
    if (auto hit = ctx_->scene.pick(ray)) {
        if (e->modifiers() & Qt::ShiftModifier) ctx_->doc.toggle(*hit);
        else ctx_->doc.select(*hit, false);
    } else if (!(e->modifiers() & Qt::ShiftModifier)) {
        ctx_->doc.deselectAll();
    }
    ctx_->documentEdited();
}

void CameraView::wheelEvent(QWheelEvent* e) {
    ctx_->cam.pos += ctx_->cam.forward() * (e->angleDelta().y() / 120.0 * ctx_->cam.speed * 0.25);
    ctx_->cameraMoved();
}

void CameraView::keyPressEvent(QKeyEvent* e) {
    if (ctx_->playing) {
        if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_P) {
            stopPlay();
            return;
        }
        if (e->key() == Qt::Key_N && !e->isAutoRepeat()) {
            player_.noclip = !player_.noclip;
            if (!player_.noclip) placePlayer(world_, player_, player_.origin, params_);
            ctx_->setStatus(player_.noclip ? "PLAY: noclip on" : "PLAY: noclip off");
            return;
        }
        if (!e->isAutoRepeat()) ctx_->heldKeys.insert(e->key());
        return;
    }
    if (!e->isAutoRepeat() && isCameraKey(e->key()) && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        ctx_->heldKeys.insert(e->key());
        return;
    }
    QWidget::keyPressEvent(e);
}

void CameraView::keyReleaseEvent(QKeyEvent* e) {
    if (!e->isAutoRepeat()) ctx_->heldKeys.remove(e->key());
    QWidget::keyReleaseEvent(e);
}

void CameraView::focusOutEvent(QFocusEvent* e) {
    ctx_->heldKeys.clear();
    if (ctx_->playing) stopPlay();
    ctx_->looking = false;
    unsetCursor();
    QWidget::focusOutEvent(e);
}

void CameraView::timerEvent(QTimerEvent* e) {
    if (e->timerId() == windTimer_.timerId()) {
        if (ctx_->behaviourPreview) {
            // Moving models are rebuilt at their pose for this moment.
            ctx_->scene.behaviourTime = windClock_.isValid() ? windClock_.elapsed() / 1000.0 : 0.0;
            ctx_->scene.invalidate();
        }
        update();  // the sway is a shader uniform: no geometry rebuild
        return;
    }
    QWidget::timerEvent(e);
}

size_t CameraView::referenceTexturesResolved() const {
    size_t n = 0;
    for (bool b : refTexResolved_) n += b ? 1 : 0;
    return n;
}

// ---- View > Game Lighting (game_lightmap.h, core game_look; research 95 "Editor maps in game")

bool CameraView::gameLookOn() const {
    ctx_->scene.gameLookAuto = !gameStockMap_.empty();
    return ctx_->scene.gameLookActive() && ctx_->texturedView && !ctx_->scene.captureMode;
}

const StockMapLook* CameraView::gameStockLook() const {
    if (gameStockMap_.empty()) return nullptr;
    if (const StockMapLook* l = stockMapLook(gameStockMap_)) return l;
    return stockSunKnown_ ? &stockMapLookNearest(stockSunIntensity_) : nullptr;
}

Vec3 CameraView::gameSunToward() const {
    return !gameStockMap_.empty() && stockSunKnown_ ? stockSunToward_ : ctx_->scene.sunToward;
}

namespace {
// The worldspawn's "r g b intensity" sky for the bake (iw8r_bake_sky), default a pale blue at 0.35 of the sun.
// The map's bake settings (worldspawn keys, core bake_options: the reader the build uses).
iw8r::MapBakeOptions gameBakeOptionsOf(const iw8r::Document& doc) {
    for (const auto& e : doc.map().entities)
        if (e.isWorldspawn()) return iw8r::mapBakeOptions(&e);
    return iw8r::mapBakeOptions(nullptr);
}
iw8r::Vec3 gameSkyOf(const iw8r::Document& doc) { return gameBakeOptionsOf(doc).skyLight(); }
}  // namespace

void CameraView::setStockGpuGrid(const std::string& zone, std::shared_ptr<const StockGpuGrid> grid) {
    stockGridLoading_ = false;
    if (zone != gameStockMap_) return;  // another map was opened meanwhile: read again at the next bake
    stockGridZone_ = zone;
    stockGrid_ = std::move(grid);
    if (stockGrid_ && !stockGrid_->empty()) {
        emit ctx_->statusMessage(QString("Game lighting: brushes added to %1 take their light from the map's light grid (%2 probes)")
                                     .arg(QString::fromStdString(zone)).arg(stockGrid_->pos.size()));
        rebakeGameLighting();
    } else {
        stockGrid_.reset();
    }
}

bool CameraView::gameLookUnbaked() const { return !gameStockLook() && !gameBakeOptionsOf(ctx_->doc).bake; }

void CameraView::prepareGameLook() {
    const bool game = gameLookOn();
    if (game != gameWasOn_) {
        gameWasOn_ = game;
        geomDirty_ = true;  // vertex colours and lightmap UVs change
    }
    ctx_->scene.gameLightmap = game ? gameLm_ : nullptr;
    app::SceneRenderer::GameLook g;
    g.on = game;
    if (game) {
        if (!gameLutReady_) {
            const std::vector<uint8_t> lut = buildGameTonemapLut(64, !gameStockMap_.empty() && stockGrade_ ? stockGrade_.get() : nullptr);
            renderer_.setGameTonemapLut(64, lut.data());
            gameLutReady_ = true;
        }
        // The stock world shader's environment BRDF table, read from the install once (ACTS, a few seconds, cached).
        if (!envBrdfAsked_ && toolPathFn_) {
            envBrdfAsked_ = true;
            QPointer<CameraView> self(this);
            auto tools = toolPathFn_;
            std::thread(guardedWorker(__FILE__, __LINE__, [self, tools] {
                EnvBrdfLut lut = loadEnvBrdfLut(tools);
                QMetaObject::invokeMethod(qApp, [self, lut] {
                    if (!self) return;
                    if (lut.width > 0) {
                        self->pendingEnvBrdf_ = std::make_shared<std::vector<uint8_t>>(lut.rg);
                        self->pendingEnvBrdfW_ = lut.width, self->pendingEnvBrdfH_ = lut.height;
                        self->update();
                    } else {
                        emit self->ctx_->statusMessage("Game Lighting: " + lut.error + " (an approximation is used)");
                    }
                }, Qt::QueuedConnection);
            })).detach();
        }
        if (pendingEnvBrdf_) {
            renderer_.setEnvBrdfLut(pendingEnvBrdfW_, pendingEnvBrdfH_, pendingEnvBrdf_->data());
            pendingEnvBrdf_.reset();
        }
        // Its specular occlusion table (gtso_lut), the same way.
        if (!gtsoAsked_ && toolPathFn_) {
            gtsoAsked_ = true;
            QPointer<CameraView> self(this);
            auto tools = toolPathFn_;
            std::thread(guardedWorker(__FILE__, __LINE__, [self, tools] {
                auto lut = std::make_shared<GtsoLut>(loadGtsoLut(tools));
                QMetaObject::invokeMethod(qApp, [self, lut] {
                    if (!self) return;
                    if (lut->width > 0) {
                        self->pendingGtso_ = lut;
                        self->update();
                    } else {
                        emit self->ctx_->statusMessage("Game Lighting: specular occlusion table: " + lut->error + " (an approximation is used)");
                    }
                }, Qt::QueuedConnection);
            })).detach();
        }
        if (pendingGtso_) {
            renderer_.setGtsoLut(pendingGtso_->width, pendingGtso_->height, pendingGtso_->depth, pendingGtso_->r.data());
            pendingGtso_.reset();
        }
        if (pendingLights_) {
            renderer_.setStockLocalLights(*pendingLights_, pendingShadows_ ? *pendingShadows_ : std::vector<StockLightShadow>{});
            renderer_.setIesStrip(pendingIes_ ? int(pendingIes_->size()) : 0, pendingIes_ && !pendingIes_->empty() ? pendingIes_->data() : nullptr);
            pendingLights_.reset();
            pendingIes_.reset();
            pendingShadows_.reset();
        }
        const Vec3 t = gameSunToward();
        // A stock map: its own sun (read from its ComWorld, else the shipped table) and the exposure of its vision (or
        // of the shipped map with the nearest sun); an own map: suncolor and the converter's fixed exposure (EV 10).
        const StockMapLook* stock = gameStockLook();
        Vec3 c = ctx_->scene.sunColor;
        GameExposure exposure;
        if (stock) {
            c = stockSunKnown_ ? stockSunColor_ * stockSunIntensity_
                               : Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]} * stock->sunIntensity;
            exposure = stock->exposure;
        }
        g.sunToward[0] = float(t.x), g.sunToward[1] = float(t.y), g.sunToward[2] = float(t.z);
        g.sunColor[0] = float(c.x), g.sunColor[1] = float(c.y), g.sunColor[2] = float(c.z);
        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        g.skyIsUnbaked = gameLookUnbaked();
        if (stock) {  // the stock map's bloom veil and vignette (research 109)
            g.veil = float(stockPostFx_.veil);
            g.grain = float(stockPostFx_.grain);
        }
        {
            // The white balance (research 120): the stock map's vision, or an own map's worldspawn iw8r_vision.
            GameWhiteBalance wb = stockPostFx_.whiteBalance;
            if (!stock) {
                const Entity* ws = ctx_->doc.worldspawn();
                const std::string* ov = ws ? ws->value("iw8r_vision") : nullptr;
                wb = ov ? gameWhiteBalanceFromOverrides(*ov) : GameWhiteBalance{};
            }
            if (gameWhiteBalanceActive(wb)) {
                double wbm[9];
                gameWhiteBalanceMatrix(wb, wbm);
                g.whiteBalanceOn = true;
                for (int k = 0; k < 9; ++k) g.whiteBalance[k] = float(wbm[k]);
            }
        }
        if (stock) {
            const GamePostFx& p = stockPostFx_;
            if (p.volAir > 0) {  // the volumetric fog (research 109 section 2)
                const float hExp = p.volHeightHalf != 0 ? float(-0.693147 / std::max(p.volHeightHalf, 0.1)) : 0.0f;
                const float vol[3][4] = {{float(p.volAlbedo[0]), float(p.volAlbedo[1]), float(p.volAlbedo[2]), float(p.volAir * 0.01)},
                                         {float(p.volAbsorption), float(p.volAmbient), float(p.volHeightBase), hExp},
                                         {float(p.volSun), float(p.volAnisotropy), 1.0f, 0.0f}};
                std::memcpy(g.vol, vol, sizeof(vol));
            }
            // The shape of what is being drawn: a capture's own, else the widget's.
            double aspect = height() > 0 ? double(width()) / double(height()) : 16.0 / 9.0;
            if (captureW_ > 0 && captureH_ > 0) aspect = captureAspect_ > 0 ? captureAspect_ : double(captureW_) / double(captureH_);
            gameVignetteConstants(stockPostFx_, aspect, 1.0, g.vignette);
        }
        if (stock && stockFog_.on) {
            const GameFog& f = stockFog_;
            const double s = gameFogInscatter(f);
            const float fog[6][4] = {{float(f.start), float(f.halfPlane), float(f.maxOpacity), 1.0f},
                                     {float(f.color[0] * s), float(f.color[1] * s), float(f.color[2] * s), float(f.mieBlend)},
                                     {float(f.rayleigh[0]), float(f.rayleigh[1]), float(f.rayleigh[2]), float(f.mieAnisotropy)},
                                     {float(f.mie[0]), float(f.mie[1]), float(f.mie[2]), float(f.desaturation)},
                                     {float(f.heightBase), float(f.heightHalf), float(f.skyDistance), float(f.skyIntensity)},
                                     {float(f.maxDistance), float(f.skyMinAngle), float(f.skyMaxAngle), 0}};
            std::memcpy(g.fog, fog, sizeof(fog));
            // The game's own constants (research 111); 135.w follows the camera's height.
            const GameFogConstants k = gameFogConstants(stockFogRefdef_, float(ctx_->cam.pos.z));
            std::memcpy(g.fogK, k.c, sizeof(g.fogK));
        }
        // The sky preview is an 8-bit copy of the HDR stock sky times its conversion exposure: undo it, so the sky has
        // its own radiance in the game's light units (and the post chain exposes it like the rest).
        if (stock) g.bakedScale = float(stockBakedScale_);
        if (stock && skyExposure_ > 0) g.skyScale = float((stockSkyIntensity_ > 0 ? stockSkyIntensity_ : 1.0) / skyExposure_);
        if (stock && stockMeanLight_[0] + stockMeanLight_[1] + stockMeanLight_[2] > 0) {
            for (int k = 0; k < 3; ++k) g.sky[k] = float(stockMeanLight_[size_t(k)] * stockBakedScale_);
            g.skyIsStock = true;
        }
        // Stock maps: their vision's auto exposure (metered from the frame, as the game does) while the stock sky fills
        // the background; without it the frame would count the editor's empty background, so the exposure their vision
        // gives a mid-bright scene (its mid EV) is held instead. Owner 2026-11-12: Hideout's desert (mid EV 10.7, adapting
        // up to 15 in the sun) came out washed out at its mid EV.
        const bool meterStock = stock && stock->exposure.autoExposure && ctx_->scene.skyActive;
        exposure.autoExposure = meterStock;
        if (stock && !meterStock) exposure.ev = stock->exposure.midEv, exposure.exposureAdjust = stock->exposure.midAdjust + stock->exposure.exposureAdjust;
        if (std::isfinite(gameExposureOverride_)) {  // View > Game Lighting > Exposure Override
            exposure.autoExposure = false;
            exposure.ev = gameExposureOverride_, exposure.exposureAdjust = 0;
        }
        if (exposure.autoExposure != gameMetering_) {
            gameMetering_ = exposure.autoExposure;
            if (gameMetering_) {
                const std::vector<float> inv = gameTonemapInverseGrey(256);
                renderer_.setGameMetering(inv.data());
            } else {
                renderer_.setGameMetering(nullptr);
            }
            gameExposure_ = 0;
            gameExposureSnap_ = true;
        }
        // Auto exposure: the metered log-average of the frames before (one or two frames late, as the game's own),
        // adapted over time at the vision's speed; until a measurement arrives, the exposure of a mid-grey scene.
        double avg = 0;
        if (renderer_.takeMeteredLuminance(avg)) {
            gameExposureTarget_ = gameExposureFor(exposure, avg);
            if (gameExposureSnap_) gameExposure_ = gameExposureTarget_, gameExposureSnap_ = false;  // a map just opened
        }
        if (!exposure.autoExposure || gameExposureTarget_ <= 0)
            gameExposureTarget_ = gameExposureFor(exposure, 0.001 * std::exp2(exposure.midEv - 2.84));
        const double dt = gameExposureClock_.isValid() ? gameExposureClock_.restart() / 1000.0 : 0.0;
        if (!gameExposureClock_.isValid()) gameExposureClock_.start();
        gameExposure_ = gameExposure_ > 0 ? gameAdaptExposure(gameExposure_, gameExposureTarget_, std::min(dt, 0.25), exposure.adaptSpeed)
                                          : gameExposureTarget_;
        if (exposure.autoExposure && std::fabs(std::log(gameExposure_ / gameExposureTarget_)) > 0.01)
            QTimer::singleShot(16, this, [this] { update(); });  // still adapting
        else if (exposure.autoExposure)
            QTimer::singleShot(250, this, [this] { update(); });  // keep metering slowly while the view is still
        g.lutScale = float(gameExposure_ * std::exp2(kGameBaseExposureEv));
    }
    renderer_.setGameLook(g);
}

void CameraView::maybeBakeGameLook() {
    if (!gameLookOn() || gameBaking_) return;
    // An own map without "Bake lightmaps": the build makes no lightmap, and the game lights the map with the
    // converter's constant sky fill and the live sun. Nothing to bake; a lightmap of an earlier bake goes.
    if (gameLookUnbaked()) {
        if (gameLm_) {
            gameLm_.reset();
            renderer_.setGameLightmap(0, 0, nullptr, nullptr, nullptr);
            geomDirty_ = true;
            update();
        }
        if (!gameUnbakedTold_) {
            gameUnbakedTold_ = true;
            emit ctx_->statusMessage("Game lighting: this map is built without lightmaps (Compile > Bake lightmaps is off), so the "
                                     "game's flat sky light and the live sun are shown");
        }
        return;
    }
    gameUnbakedTold_ = false;
    const uint64_t rev = ctx_->doc.revision() * 1000003ull ^ ctx_->scene.generation();
    if (rev == gameBakeRev_) return;
    if (rev != gameSeenRev_) {
        gameSeenRev_ = rev;
        gameSeenAt_.restart();
    }
    // Wait until edits settle (a drag changes the revision every frame); the first bake starts at once.
    if (gameLm_ && gameSeenAt_.isValid() && gameSeenAt_.elapsed() < 600) {
        QTimer::singleShot(650, this, [this] { update(); });
        return;
    }
    // The settings first; the geometry is gathered (about a second on a stock map: every model triangle is an
    // occluder) only when it or the settings changed. A revision bump alone (textures arriving) does not re-bake.
    const bool forcedBake = gameBakeRev_ == ~0ull;
    app::GameBakeInput in;
    // The game draws the sun (suncolor) and the lights in real time; the bake holds the sky, their bounce and the sun
    // visibility, as the build's bake (compile_iw8.cpp) does.
    in.scene.sunDir = ctx_->scene.sunToward;
    in.scene.sunColor = ctx_->scene.sunColor;
    in.scene.sunIntensity = 1.0;
    in.scene.sunDirect = false;
    const Vec3 sky = gameSkyOf(ctx_->doc);
    in.scene.skyColor = sky;
    in.scene.skyIntensity = 1.0;
    // Brushes added to a stock map are lit in that map's units: its sun (colour x intensity) and a sky in the same
    // proportion, so they sit next to the stock world's own lightmap (whose sky light is in those units too).
    if (const StockMapLook* stock = gameStockLook()) {
        in.scene.sunColor = stockSunKnown_ ? stockSunColor_ : Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]};
        in.scene.sunIntensity = stockSunKnown_ ? stockSunIntensity_ : stock->sunIntensity;
        in.scene.skyIntensity = in.scene.sunIntensity;
        in.scene.sunDir = gameSunToward();
        in.key ^= 0x5157u;
        // The game builds these brushes as a script model and lights models from the map's tetrahedral light grid
        // (research 105 section 2.1): the same grid gives the indirect light here, where it covers the point. The
        // grid is read once per map, in the background; until it is there the sky bake above stands in.
        if (stockGridZone_ != gameStockMap_ && !stockGridLoading_) {
            stockGrid_.reset();
            stockGridZone_ = gameStockMap_;
            // IW8R_NO_STOCK_GRID: a bench switch, the sky bake lights the brushes as before.
            const QString file = stockGridSource_ && !qEnvironmentVariableIsSet("IW8R_NO_STOCK_GRID") ? stockGridSource_(QString::fromStdString(gameStockMap_)) : QString();
            if (!file.isEmpty()) {
                stockGridLoading_ = true;
                QPointer<CameraView> self(this);
                const std::string zone = gameStockMap_;
                std::thread(guardedWorker(__FILE__, __LINE__, [self, file, zone]() {
                    auto grid = std::make_shared<StockGpuGrid>();
                    QFile f(file);
                    std::string err;
                    bool ok = false;
                    if (f.open(QIODevice::ReadOnly)) {
                        const QByteArray text = f.readAll();
                        ok = parseStockGpuGrid(std::string(text.constData(), size_t(text.size())), *grid, &err);
                    }
                    QMetaObject::invokeMethod(qApp, [self, zone, grid, ok]() {
                        if (self) self->setStockGpuGrid(zone, ok ? grid : nullptr);
                    }, Qt::QueuedConnection);
                })).detach();
            }
        }
        if (stockGrid_ && stockGridZone_ == gameStockMap_) {
            const std::shared_ptr<const StockGpuGrid> grid = stockGrid_;
            const double scale = stockBakedScale_;
            in.scene.ambientAt = [grid, scale](const Vec3& pos, const Vec3& normal, Vec3& ambient) {
                // Half a unit off the face, as the bake's own rays start.
                const double p[3] = {pos.x + normal.x * 0.5, pos.y + normal.y * 0.5, pos.z + normal.z * 0.5};
                StockLgvSh sh;
                if (!sampleStockGpuGrid(*grid, p, sh)) return false;
                const double n[3] = {normal.x, normal.y, normal.z};
                double rgb[3];
                evalStockLgv(sh, n, rgb);
                ambient = Vec3{rgb[0] * scale, rgb[1] * scale, rgb[2] * scale};
                return true;
            };
            in.key ^= 0x67726964u ^ uint64_t(grid->tets.size());
        }
    }
    for (const auto& L : ctx_->scene.lights) {
        BakeLight b;
        b.pos = L.pos;
        b.color = L.color;
        b.radius = L.radius;
        b.spot = L.cosOuter > -1.5;
        b.dir = L.dir;
        b.cosOuter = L.cosOuter, b.cosInner = L.cosInner;
        b.direct = in.scene.lights.size() >= 254;  // past the real-time limit: baked whole
        in.scene.lights.push_back(b);
    }
    {
        const MapBakeOptions opt = gameBakeOptionsOf(ctx_->doc);
        in.settings.texelSize = opt.texelSize;
        in.settings.skyRays = opt.skyRays;
    }
    {
        uint64_t h = app::gameBakeGeometryKey(ctx_->scene);
        const auto mixD = [&h](double v) {
            uint64_t b = 0;
            std::memcpy(&b, &v, sizeof(b));
            h ^= b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h *= 0x100000001b3ull;
        };
        for (const Vec3& v : {in.scene.sunDir, in.scene.sunColor, in.scene.skyColor}) mixD(v.x), mixD(v.y), mixD(v.z);
        mixD(in.scene.sunIntensity), mixD(in.scene.skyIntensity), mixD(in.settings.texelSize), mixD(double(in.settings.skyRays));
        mixD(double(in.key));
        mixD(in.scene.ambientAt ? 1.0 : 0.0);
        // The bounce colours come from the materials' images: bake again when more of them have arrived.
        {
            size_t loaded = 0;
            for (const auto& [m, id] : texIds_) loaded += id >= 0 && id != checkerTex_;
            mixD(double(loaded));
        }
        for (const BakeLight& b : in.scene.lights)
            mixD(b.pos.x), mixD(b.pos.y), mixD(b.pos.z), mixD(b.color.x), mixD(b.color.y), mixD(b.color.z), mixD(b.radius),
                mixD(b.dir.x), mixD(b.dir.y), mixD(b.dir.z), mixD(b.cosOuter), mixD(b.cosInner);
        if (!forcedBake && gameLm_ && h == gameBakeInputKey_) {
            gameBakeRev_ = rev;
            return;
        }
        gameBakeInputKey_ = h;
    }
    {
        app::GameBakeInput g = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor, [this](const std::string& m) { return bakeAlbedoFor(m); });
        in.polys = std::move(g.polys);
        in.occluders = std::move(g.occluders);
        in.key ^= g.key;
    }
    gameBaking_ = true;
    gameBakeRev_ = rev;
    emit ctx_->statusMessage(QString("Game lighting: baking %1 faces...").arg(in.polys.size()));
    QPointer<CameraView> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, in = std::move(in)]() {
        std::shared_ptr<const app::GameLightmap> lm = app::bakeGameLightmap(in);
        QMetaObject::invokeMethod(qApp, [self, lm]() {
            if (self) self->gameBakeDone(lm);
        }, Qt::QueuedConnection);
    })).detach();
}

void CameraView::maybeLightModels() {
    const auto clear = [this] {
        if (!ctx_->scene.modelLights.empty()) {
            ctx_->scene.modelLights.clear();
            ++ctx_->scene.modelLightsRev;
            geomDirty_ = true;
            update();
        }
        modelLightKey_ = 0;
        modelLightRev_ = ~0ull;
    };
    if (!gameLookOn() || gameStockLook()) {  // off, or a stock map (its own light grid lights its props)
        clear();
        return;
    }
    if (modelLighting_) return;
    const uint64_t rev = ctx_->doc.revision() * 1000003ull ^ ctx_->scene.generation();
    if (rev == modelLightRev_) return;
    if (rev != modelLightSeenRev_) {
        modelLightSeenRev_ = rev;
        modelLightSeenAt_.restart();
    }
    // Wait until edits settle, as the lightmap bake does; the first lights are made at once.
    if (!ctx_->scene.modelLights.empty() && modelLightSeenAt_.isValid() && modelLightSeenAt_.elapsed() < 600) {
        QTimer::singleShot(650, this, [this] { update(); });
        return;
    }
    modelLightRev_ = rev;
    // The models: every item with model triangles, by its entity, at the middle of its bounds.
    std::vector<std::pair<Id, Vec3>> models;
    {
        std::unordered_set<Id> seen;
        for (const app::PrimGeom* g : ctx_->scene.items())
            if (g->hasMeshPolys() && g->bounds.valid() && seen.insert(g->ref.entity).second)
                models.push_back({g->ref.entity, (g->bounds.mins + g->bounds.maxs) * 0.5});
    }
    if (models.empty()) {
        clear();
        modelLightRev_ = rev;
        return;
    }
    const Vec3 sunDir = ctx_->scene.sunToward, sunColour = ctx_->scene.sunColor, sky = gameSkyOf(ctx_->doc);
    uint64_t h = app::gameBakeGeometryKey(ctx_->scene);
    const auto mixD = [&h](double v) {
        uint64_t b = 0;
        std::memcpy(&b, &v, sizeof(b));
        h ^= b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h *= 0x100000001b3ull;
    };
    for (const Vec3& v : {sunDir, sunColour, sky}) mixD(v.x), mixD(v.y), mixD(v.z);
    {
        size_t loaded = 0;  // the bounce colours come from the materials' images
        for (const auto& [m, id] : texIds_) loaded += id >= 0 && id != checkerTex_;
        mixD(double(loaded));
    }
    if (h == modelLightKey_) return;
    // The world the build's grid sees: the drawn brush faces and patches (models are not in it), in their colours.
    app::GameBakeInput g = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor, [this](const std::string& m) { return bakeAlbedoFor(m); });
    auto tris = std::make_shared<std::vector<RayBvh::Triangle>>();
    auto albedos = std::make_shared<std::vector<Vec3>>();
    AABB bounds;
    for (const auto& p : g.polys) {
        uint32_t id = 0;
        if (p.albedo.x >= 0) {
            albedos->push_back(p.albedo);
            id = uint32_t(albedos->size());
        }
        for (const Vec3& q : p.pts) bounds.add(q);
        for (size_t k = 1; k + 1 < p.pts.size(); ++k) tris->push_back({p.pts[0], p.pts[k], p.pts[k + 1], id});
    }
    if (tris->empty() || !bounds.valid()) {
        clear();
        modelLightRev_ = rev;
        return;
    }
    const double reach = 2.0 * length(bounds.maxs - bounds.mins) + 1024.0;
    modelLighting_ = true;
    QPointer<CameraView> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, tris, albedos, models = std::move(models), sunDir, sunColour, sky, reach, h]() {
        RayBvh bvh;
        bvh.build(*tris);
        auto out = std::make_shared<std::unordered_map<Id, std::array<std::array<float, 3>, 56>>>();
        for (const auto& [id, at] : models) lightGridSamplesAt(bvh, *albedos, sunDir, sunColour, sky, reach, at, (*out)[id]);
        QMetaObject::invokeMethod(qApp, [self, out, h]() {
            if (self) self->modelLightsDone(out, h);
        }, Qt::QueuedConnection);
    })).detach();
}

void CameraView::modelLightsDone(std::shared_ptr<const std::unordered_map<Id, std::array<std::array<float, 3>, 56>>> lights, uint64_t key) {
    modelLighting_ = false;
    modelLightKey_ = key;
    if (!lights || !gameLookOn() || gameStockLook()) return;
    if (*lights == ctx_->scene.modelLights) return;  // nothing changed for any model: the geometry stays
    ctx_->scene.modelLights = *lights;
    ++ctx_->scene.modelLightsRev;
    const QString told = QString("Game lighting: %1 model(s) lit from the light grid at their places").arg(lights->size());
    emit ctx_->statusMessage(told);
    EditorLog::instance().add(LogLevel::Info, "lighting", told);
    geomDirty_ = true;
    update();
}

void CameraView::gameBakeDone(std::shared_ptr<const app::GameLightmap> lm) {
    gameBaking_ = false;
    if (!lm) return;
    if (!lm->error.empty()) {
        emit ctx_->statusMessage(QString("Game lighting: %1").arg(QString::fromStdString(lm->error)));
    } else {
        renderer_.setGameLightmap(lm->width, lm->height, lm->a.data(), lm->b.data(), lm->d.data());
        emit ctx_->statusMessage(QString("Game lighting: baked %1 x %2 lightmap, %3 faces, %4 units per texel, %5 s")
                                     .arg(lm->width).arg(lm->height).arg(lm->polys).arg(lm->texelSize).arg(lm->seconds, 0, 'f', 1));
    }
    gameLm_ = lm;
    geomDirty_ = true;
    update();
}

}  // namespace iw8r::qt
