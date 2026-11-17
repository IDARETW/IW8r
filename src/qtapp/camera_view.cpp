#include "material_tints.h"
#include "worker_guard.h"
#include "camera_view.h"
#include "preview_gpu.h"
#include "play_world.h"
#include "prefab_browser.h"
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
#include "stock_smodels.h"
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
    rendererOk_ = dev_ && renderer_->init(dev_, dc_, &err);
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
    stopRenderThread();
    if (swap_) swap_->Release();
}

void CameraView::ensureSwapChain() {
    if (!rendererOk_) return;
    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, int(width() * dpr)), h = std::max(1, int(height() * dpr));
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!IsWindow(hwnd)) return;  // e.g. the offscreen platform used by --gui-test
    rootHwnd_ = GetAncestor(hwnd, GA_ROOT);
    if (swap_ && swapHwnd_ == hwnd && w == swapW_ && h == swapH_) return;
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
    lockSwap();
    std::lock_guard<std::timed_mutex> swapLock(swapMx_, std::adopt_lock);
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

static double behaviourTimeHook();  // IW8R_BEHAVIOUR_TIME (next to timerEvent)

void CameraView::render() {
    if (!rendererOk_) return;
    std::unique_lock<std::recursive_timed_mutex> gpuLock(gpuMutex());
    // The render thread moved the fly camera while this thread was busy: carry on from there, and drop the mouse moves
    // that queued meanwhile (it has turned the view by them already).
    if (rtMoved_) {
        ctx_->cam.pos = rtCam_.pos, ctx_->cam.yaw = rtCam_.yaw, ctx_->cam.pitch = rtCam_.pitch;
        rtMoved_ = false;
        dropMouseBefore_ = GetTickCount();
        ctx_->cameraMoved();
    }
    IW8R_PROF("r.total");
    {
        IW8R_PROF("r.sync");
        if (const double bt = behaviourTimeHook(); bt >= 0) {  // test hook: the Behaviour Preview held at a time (captures)
            ctx_->behaviourPreview = true;
            ctx_->scene.behaviourTime = bt;
            updateBehaviourScript();
            ctx_->scene.invalidate();
        }
        ctx_->scene.sync(ctx_->doc);
    }
    const qreal dpr = devicePixelRatioF();
    const int w = captureW_ > 0 ? captureW_ : fixedW_ > 0 ? fixedW_ : std::max(1, int(width() * dpr));
    const int h = captureH_ > 0 ? captureH_ : fixedH_ > 0 ? fixedH_ : std::max(1, int(height() * dpr));
    renderer_->resize(w, h);
    if (texInstDirty_) {
        IW8R_PROF("r.up_texinst");
        renderer_->setReferenceTexInstances(pendingTexInst_);
        pendingTexInst_.clear();
        pendingTexInst_.shrink_to_fit();
        texInstResolved_.assign(texInstMaterials_.size(), false);
        texInstDirty_ = false;
        texInstPending_ = refPollDue_ = true;
    }
    if (pendingLgv_) {
        const app::LgvAtlasUpload& a = *pendingLgv_;
        const bool ok = renderer_->setStockLgvAtlas(a.width, a.depth, a.tex0.empty() ? nullptr : a.tex0.data(),
                                                   a.tex1.empty() ? nullptr : a.tex1.data(), a.boxes.empty() ? nullptr : a.boxes.data(),
                                                   a.boxes.size() / 20);
        if (!ok && a.width > 0) emit ctx_->statusMessage("Stock prop light grid: the atlas could not be uploaded (props use their box centre)");
        pendingLgv_.reset();
    }
    if (texLightDirty_) {
        IW8R_PROF("r.up_texlight");
        for (size_t i = 0; i < pendingTexLight_.size(); ++i)
            if (!pendingTexLight_[i].empty()) renderer_->setReferenceTexInstanceLight(i, pendingTexLight_[i]);
        pendingTexLight_.clear();
        pendingTexLight_.shrink_to_fit();
        texLightDirty_ = false;
    }
    if (probesDirty_) {
        IW8R_PROF("r.up_probes");
        renderer_->setStockProbes(pendingProbes_.get());
        pendingProbes_.reset();
        probesDirty_ = false;
    }
    if (stockLmDirty_) {
        IW8R_PROF("r.up_lightmap");
        const auto& l = pendingStockLm_;
        if (l && l->width > 0) renderer_->setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data(), l->rects);
        else renderer_->setStockLightmap(0, 0, nullptr, nullptr, nullptr);
        stockMeanLight_ = l ? l->meanIrradiance : std::array<float, 3>{0, 0, 0};
        pendingStockLm_.reset();
        stockLmDirty_ = false;
    }
    if (instDirty_) {
        IW8R_PROF("r.up_inst");
        renderer_->setReferenceInstances(pendingInst_);
        pendingInst_.clear();
        pendingInst_.shrink_to_fit();
        instDirty_ = false;
    }
    if (refDirty_) {
        IW8R_PROF("r.up_ref");
        renderer_->setReference(pendingRef_);
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
        refTexBounds_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            AABB bb;
            for (const auto& v : b.tris) bb.add(Vec3{v.x, v.y, v.z});
            refTexBounds_.push_back(bb);
            refTexLayer_.push_back(b.layer);
            RefDecalVolume dv;
            dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            dv.collection = b.decalVolume ? b.decalCollection : -1;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);
            batches.push_back(std::move(b.tris));
        }
        renderer_->setReferenceTextured(batches, &refTexLayer_);
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
        renderer_->setReferenceExtras(in);
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
                // Tool materials keep their flat colour: nothing to load (they kept the poll going on every frame).
                if (isIw8ToolMaterial(refTexMaterials_[k])) {
                    refTexResolved_[k] = true;
                    continue;
                }
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
                        renderer_->setReferenceTexture(k, cid);
                        renderer_->setReferenceDecal(k, 1, mid == checkerTex_ ? -1 : mid, p);
                        continue;
                    }
                    const float p[16] = {2.0f, dv.a, dv.b, 0.0f, dv.tint[0], dv.tint[1], dv.tint[2], 0.0f,
                                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    renderer_->setReferenceTexture(k, cid);
                    renderer_->setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    renderer_->setReferenceDecalCollection(k, dv.collection);
                    continue;
                }
                const int id = textureFor(refTexMaterials_[k]);
                if (texIds_.count(refTexMaterials_[k])) {
                    // A stock image this install does not hold (catalogued only in a zone without its payload): plain grey on
                    // the read-only stock reference rather than the missing-material checker (mp_m_fork, mp_m_speed).
                    if (id >= 0) renderer_->setReferenceTexture(k, id == checkerTex_ ? -1 : id);
                    // Its normal map next (normalFor waits for the colour image and the per-frame budget).
                    const int nid = id >= 0 && id != checkerTex_ ? normalFor(refTexMaterials_[k]) : -1;
                    if (id >= 0 && id != checkerTex_ && !nrmIds_.count(refTexMaterials_[k])) {
                        pending = true;
                        continue;
                    }
                    if (nid >= 0) renderer_->setReferenceNormal(k, nid);
                    if (ctx_ && ctx_->scene.drawClassFor) renderer_->setReferenceDrawClass(k, ctx_->scene.drawClassFor(refTexMaterials_[k]));
                    if (factsLookup_)
                        if (const auto f = factsLookup_(refTexMaterials_[k])) renderer_->setReferenceNoCast(k, f->noCastShadow);
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
                if (isIw8ToolMaterial(texInstMaterials_[k])) {  // a flat colour: nothing to load
                    texInstResolved_[k] = true;
                    continue;
                }
                const int id = textureFor(texInstMaterials_[k]);
                if (texIds_.count(texInstMaterials_[k])) {
                    if (id >= 0) renderer_->setReferenceInstTexture(k, id == checkerTex_ ? -1 : id);  // as the world: grey, not the checker
                    // Its normal map (Game Lighting: gloss, cavity, relief) and the material's constants.
                    const int nid = id >= 0 && id != checkerTex_ ? normalFor(texInstMaterials_[k]) : -1;
                    if (id >= 0 && id != checkerTex_ && !nrmIds_.count(texInstMaterials_[k])) {
                        pending = true;
                        continue;
                    }
                    if (nid >= 0) renderer_->setReferenceInstNormal(k, nid);
                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_->setReferenceInstParams(k, p->data());
                    if (factsLookup_)
                        if (const auto f = factsLookup_(texInstMaterials_[k]))
                            renderer_->setReferenceInstDecal(k, f->propDecalSortKey), renderer_->setReferenceInstNoCast(k, f->noCastShadow || f->translucent),
                                renderer_->setReferenceInstTranslucent(k, f->translucent);
                    if (ctx_ && ctx_->scene.drawClassFor) renderer_->setReferenceInstDrawClass(k, ctx_->scene.drawClassFor(texInstMaterials_[k]));
                    if (!applyMaterialExtras(texInstMaterials_[k], k, true)) {
                        pending = true;
                        continue;
                    }
                    if (!resolvePropLayers(k)) {  // model-layered: its layer images (research 132 G9)
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
    renderer_->setReferenceVisible(ctx_->scene.cameraMode != app::CameraMode::Wireframe);
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
            ownFog_ = GameFog{};
            ownFogRefdef_ = GameFogRefdef{};
            ownPostFx_ = GamePostFx{};
            // Volumetric boxes, read as the build reads them (core compile_iw8: defaults, the falloff cap, height_half).
            ownVolumes_.clear();
            for (const Entity& e : ctx_->doc.map().entities) {
                if (e.classname() != "iw8r_volume") continue;
                const auto three = [&](const char* key, double a, double b, double c) {
                    std::array<double, 3> v{a, b, c};
                    if (const std::string* s = e.value(key)) {
                        double x = 0, y = 0, z = 0;
                        if (std::sscanf(s->c_str(), "%lf %lf %lf", &x, &y, &z) == 3) v = {x, y, z};
                    }
                    return v;
                };
                const auto one = [&](const char* key, double def) {
                    const std::string* s = e.value(key);
                    char* end = nullptr;
                    const double v = s ? std::strtod(s->c_str(), &end) : def;
                    return s && end != s->c_str() && std::isfinite(v) ? v : def;
                };
                const auto mn = three("mins", -128, -128, -128), mx = three("maxs", 128, 128, 128);
                if (!(mx[0] > mn[0] && mx[1] > mn[1] && mx[2] > mn[2])) continue;
                const auto ang = three("angles", 0, 0, 0);
                const auto ax = anglesToAxis(Vec3{ang[0], ang[1], ang[2]});
                OwnVolumeBox box;
                const Vec3 o = e.origin().value_or(Vec3{});
                box.center = o + ax[0] * ((mn[0] + mx[0]) / 2) + ax[1] * ((mn[1] + mx[1]) / 2) + ax[2] * ((mn[2] + mx[2]) / 2);
                for (int k = 0; k < 3; ++k) box.axis[k] = ax[size_t(k)], box.half[k] = (mx[size_t(k)] - mn[size_t(k)]) / 2;
                box.density = std::max(one("density", 0.001), 0.0);
                const double falloff = std::min(std::max(one("falloff", 32), 1e-3), std::min({box.half[0], box.half[1], box.half[2]}));
                box.invFalloff = 1.0 / falloff;
                double fade = std::clamp(one("height_fade", 0), 0.0, 1.0);
                if (const double hh = one("height_half", 0); hh >= 6.931) fade = 6.931 / hh;
                box.heightK = -0.1 * fade;
                box.masks = volumeMasksOf(e);
                if (box.density > 0) ownVolumes_.push_back(box);
            }
            {
                // The boxes' mask pictures: the first four names in use, from the map's volume_masks folder or the
                // editor's, each scaled to the renderer's slice size (the red channel).
                std::vector<std::string> folders;
                if (!ctx_->doc.path().empty())
                    folders.push_back(QFileInfo(QString::fromStdString(ctx_->doc.path())).absoluteDir().filePath(kVolumeMaskFolder).toStdString());
                QDir data(QCoreApplication::applicationDirPath());
                for (int up = 0; up < 3 && !QFileInfo::exists(data.filePath("data/volume_masks")); ++up) data.cdUp();
                folders.push_back(data.filePath("data/volume_masks").toStdString());
                ownMaskImages_.clear();
                ownMaskScroll_ = false;
                std::vector<std::vector<uint8_t>> pictures;
                uint64_t version = 1469598103934665603ull;
                for (const OwnVolumeBox& box : ownVolumes_)
                    for (const VolumeMask& m : box.masks) {
                        if (std::find(ownMaskImages_.begin(), ownMaskImages_.end(), m.image) != ownMaskImages_.end()) {
                            ownMaskScroll_ = ownMaskScroll_ || m.scroll[0] != 0 || m.scroll[1] != 0;
                            continue;
                        }
                        if (int(ownMaskImages_.size()) >= app::SceneRenderer::kVolMaskSlices) continue;
                        const std::string file = findVolumeMaskFile(m.image, folders);
                        QImage img = file.empty() ? QImage() : QImage(QString::fromStdString(file));
                        if (img.isNull()) continue;
                        const int n = app::SceneRenderer::kVolMaskSize;
                        img = img.convertToFormat(QImage::Format_RGBA8888).scaled(n, n, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        std::vector<uint8_t> grey(size_t(n) * n);
                        for (int y = 0; y < n; ++y) {
                            const uchar* row = img.constScanLine(y);
                            for (int x = 0; x < n; ++x) grey[size_t(y) * n + x] = row[x * 4];
                        }
                        for (const char ch : m.image) version = (version ^ uint8_t(ch)) * 1099511628211ull;
                        version = (version ^ uint64_t(QFileInfo(QString::fromStdString(file)).lastModified().toMSecsSinceEpoch())) * 1099511628211ull;
                        ownMaskImages_.push_back(m.image);
                        ownMaskScroll_ = ownMaskScroll_ || m.scroll[0] != 0 || m.scroll[1] != 0;
                        pictures.push_back(std::move(grey));
                    }
                renderer_->setVolumeMasks(pictures, pictures.empty() ? 0 : version);
            }
            if (const Entity* ws = ctx_->doc.worldspawn()) {
                atmo_ = readAtmosphere(*ws);
                if (auto r = readWind(*ws)) wind_ = *r;
                // The haze as the build will write it (core compile_iw8: the preset under the user's iw8r_vision, the
                // fog's brightness from the map's sun, the ground layers at the spawn points' height).
                std::string hazeText;
                if (atmo_.haze) {
                    std::vector<double> heights;
                    for (const Entity& e : ctx_->doc.map().entities)
                        if (const std::string* cn = e.value("classname"); cn && cn->find("spawn") != std::string::npos)
                            if (auto o = e.origin()) heights.push_back(o->z);
                    double groundZ = 0;
                    if (!heights.empty()) {
                        std::nth_element(heights.begin(), heights.begin() + long(heights.size() / 2), heights.end());
                        groundZ = heights[heights.size() / 2];
                    }
                    const Vec3 sc = ctx_->scene.sunColor;
                    hazeText = hazeVisionText(*atmo_.haze, 0.2126 * sc.x + 0.7152 * sc.y + 0.0722 * sc.z, gameSunToward(), std::round(groundZ));
                }
                if (const std::string vision = hazeVisionFromOverrides(mergeVisionText(hazeText, atmo_.vision)); !vision.empty()) {
                    ownFog_ = gameFogFromVision(vision);
                    ownFogRefdef_ = gameFogRefdef(gameFogVisionFromText(vision));
                    ownPostFx_ = gamePostFxFromVision(vision);
                }
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
        // (Left out with iw8r_haze, as in the build: the game's own fog replaces it.)
        const ExpFog* fog = ctx_->fogPreview && atmo_.fog && !atmo_.haze ? &*atmo_.fog : nullptr;
        const float col[3] = {fog ? float(fog->color.x) : 0.f, fog ? float(fog->color.y) : 0.f, fog ? float(fog->color.z) : 0.f};
        renderer_->setFog(eye, fog ? float(fog->start) : 0.f, fog ? float(fog->halfway) : 0.f, col);
        renderer_->setPixelAngle(float(2.0 * std::tan(ctx_->cam.fov * 3.14159265358979 / 360.0) / std::max(1, fixedW_ > 0 ? fixedW_ : width())));
        // Colour grade preview (3.15): iw8r_film and the iw8r_lut .cube of the open map (Compile > Atmosphere).
        const FilmGrade* film = ctx_->gradePreview && atmo_.film ? &*atmo_.film : nullptr;
        const float lt[3] = {film ? float(film->lightTint.x) : 1.f, film ? float(film->lightTint.y) : 1.f, film ? float(film->lightTint.z) : 1.f};
        const float dt[3] = {film ? float(film->darkTint.x) : 1.f, film ? float(film->darkTint.y) : 1.f, film ? float(film->darkTint.z) : 1.f};
        renderer_->setGrade(film && !film->isIdentity(), film ? float(film->contrast) : 1.f, film ? float(film->brightness) : 0.f,
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
            if (lutOn && readFile(lutPath_, text) && parseCubeLut(text, size, rgba, &err)) renderer_->setGradeLut(size, rgba.data());
            else renderer_->setGradeLut(0, nullptr);
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
        (uint64_t(ctx_->texturedView) << 10) | (uint64_t(ctx_->showSpawns && !ctx_->playing) << 11) | (uint64_t(bool(imageLookup_)) << 12) |
        (uint64_t(ctx_->scene.captureMode) << 13) | (uint64_t(gameLookOn()) << 14));
    mix(ctx_->scene.pointfile.size());
    mix(ctx_->scene.collisionPreview.size());
    mix(ctx_->scene.compiledWorld.size());
    mix(ctx_->scene.navLines.size() * 3 + 1);
    mix(ctx_->scene.tomeLinesVersion * 7 + 5);
    mix(ctx_->scene.gameOutlines ? 11 : 13);
    mix(ctx_->normalMaps ? 7 : 3);
    mix((ctx_->scene.showLightRanges ? 17u : 19u) + (ctx_->scene.showSoundRanges ? 100u : 200u));  // entity range shapes
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
                pl.push_back(pixelLightOf(L, game));
            }
        mix(ctx_->scene.pixelLights ? 11 : 5);
        renderer_->setPixelLights(pl);
        // An own map in Game Lighting: its lamps through the game's light path (research 132 G8), as built.
        {
            const bool ownGame = game && !gameStockLook() && ctx_->texturedView;
            uint64_t key = 0;
            if (ownGame) {
                key = 1469598103934665603ull;
                const auto h = [&](double v) { key = (key ^ std::hash<double>{}(v)) * 1099511628211ull; };
                for (const auto& L : ctx_->scene.lights) {
                    for (double v : {L.pos.x, L.pos.y, L.pos.z, L.color.x, L.color.y, L.color.z, L.dir.x, L.dir.y, L.dir.z, L.radius, L.cosOuter,
                                     L.cosInner, L.bulbRadius, L.falloff})
                        h(v);
                    h(L.shadows ? 1.0 : 0.0);
                }
            }
            if (key != ownLampsKey_) {
                ownLampsKey_ = key;
                std::vector<StockGameLight> lamps;
                std::vector<StockLightShadow> shadows;
                if (ownGame)
                    for (size_t k = 0; k < ctx_->scene.lights.size() && lamps.size() < 254; ++k) {
                        const auto& L = ctx_->scene.lights[k];
                        const app::SceneRenderer::PixelLight p = pixelLightOf(L, true);  // the calibrated colour
                        StockPrimaryLight sp;
                        sp.index = int(k) + 2;
                        sp.type = L.cosOuter > -1.5 ? 2 : 3;
                        sp.intensity = 1.0f;
                        for (int c = 0; c < 3; ++c) sp.color[c] = p.color[c];
                        const Vec3 back = sp.type == 2 ? Vec3{-L.dir.x, -L.dir.y, -L.dir.z} : Vec3{0, 0, 1};  // toward the lamp
                        sp.dir[0] = float(back.x), sp.dir[1] = float(back.y), sp.dir[2] = float(back.z);
                        sp.origin[0] = float(L.pos.x), sp.origin[1] = float(L.pos.y), sp.origin[2] = float(L.pos.z);
                        sp.radius = float(L.radius);
                        sp.bulbRadius = float(L.bulbRadius);
                        sp.cosHalfFovOuter = sp.type == 2 ? float(L.cosOuter) : 0.0f;
                        sp.cosHalfFovInner = sp.type == 2 ? float(L.cosInner) : 0.0f;
                        sp.distanceFalloff = L.falloff > 0 ? float(L.falloff) : 1.0f;
                        sp.canUseShadowMap = L.shadows;
                        sp.isVolumetric = L.volumetric;
                        StockGameLight g;
                        if (!packStockLight(sp, nullptr, g)) continue;
                        lamps.push_back(g);
                        StockLightShadow sh;
                        if (L.shadows) sh = stockLightShadowCamera(sp, g);
                        sh.primaryIndex = sp.index;
                        sh.cast = L.shadows;
                        shadows.push_back(sh);
                    }
                // (A stock map hands its own lamps over separately: they are never cleared from here.)
                if (ownGame || (ownLampsOn_ && !gameStockLook())) renderer_->setStockLocalLights(std::move(lamps), std::move(shadows));
                ownLampsOn_ = ownGame;
            }
        }
        renderer_->setLightGain(float(ctx_->scene.lightGain));
        // Lit preview: encoded light (headroom to 2x) with exposure, and a sun shadow map over the map's bounds.
        // Game Lighting takes over from the lit preview: vertex colours carry no encoded light then (camera_geometry.cpp).
        const bool enc = ctx_->scene.litEncoded() && !ctx_->scene.captureMode && !game;
        renderer_->setLitDecode(enc ? 2.0f : 1.0f, float(ctx_->scene.exposure));
        ctx_->scene.gpuShadows = (enc || game) && ctx_->scene.litShadows;
        mix(ctx_->scene.gpuShadows ? 0x5d0u : 0x5d1u);
        if (!ctx_->scene.gpuShadows) {  // (with shadows on, the matrix is set after the geometry update below)
            const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            renderer_->setSunShadow(false, id, 0);
        }
        // Wind preview (5.23): the worldspawn iw8r_wind key (or the default breeze) drives the foliage sway.
        const WindSettings& ws = wind_;  // parsed when the document changes (above)
        const double t = windTimeOverride_ >= 0 ? windTimeOverride_ : windClock_.isValid() ? windClock_.elapsed() / 1000.0 : 0.0;
        const double yaw = ws.dirYawDeg * 3.14159265358979 / 180.0;
        renderer_->setWind(float(std::cos(yaw)), float(std::sin(yaw)),
                          ctx_->windPreview ? float(kSwayAmplitude * std::clamp(ws.strength, 0.0, kSwayMaxStrength)) : 0.0f,
                          float(2.0 * 3.14159265358979 * std::clamp(ws.gustFreq, 0.0, kSwayMaxGustHz) * t));
        // (Scrolling masks of volumetric boxes move while Game Lighting shows them.)
        const bool animate = ctx_->windPreview || ctx_->behaviourPreview || (ctx_->effectPreview && ctx_->texturedView) ||
                             (ownMaskScroll_ && game && ctx_->texturedView);
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
            if (!renderer_->setReferenceFilterShader(src, &err)) {
                renderer_->setReferenceFilterShader({}, nullptr);
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
        // An own map's sky brightness (worldspawn iw8r_sky_brightness), as the build applies it. A stock map's sky keeps
        // its own radiance (the preview's exposure is undone for it below).
        float skyBrightness = 1.0f;
        if (!gameStockLook())
            if (const Entity* ws = ctx_->doc.worldspawn()) skyBrightness = parseSkyBrightness(ws->value(kSkyBrightnessKey));
        // A stock sky in Game Lighting keeps its HDR range (RGBM faces, research 132 G10); the other views keep 8 bits.
        const bool skyHdr = gameStockLook() && gameLookOn();
        const QString skyKey = sky.isEmpty() ? QString()
                                             : sky + QString("|%1 %2 %3|%4|%5").arg(skyRot[0]).arg(skyRot[1]).arg(skyRot[2]).arg(skyBrightness).arg(skyHdr);
        if (skyKey != skyPath_) {
            skyPath_ = skyKey;
            skyTex_.clear();
            SkyConvertOptions so;
            so.faceSize = skyHdr ? 512 : 256;
            so.rgbm = skyHdr;
            // The map's sky rotation (worldspawn skyrotation; stock maps: from their entity string).
            for (int k = 0; k < 3; ++k) so.rotation[k] = skyRot[k];
            so.brightness = skyBrightness;
            // Read and converted to six faces on a worker thread (up to a second on the GUI thread for a stock sky);
            // skyDecoded() uploads them. texturesPending() and captures wait for it.
            skyDecoding_ = !sky.isEmpty();
            if (skyDecoding_) {
                QPointer<CameraView> self(this);
                std::thread(guardedWorker(__FILE__, __LINE__, [self, skyKey, file = sky, so] {
                    auto r = std::make_shared<SkyFaces>();
                    r->options = so;
                    std::string bytes, cube, err;
                    if (readFile(file.toStdString(), bytes) && skyCubemapFromDds(bytes, cube, r->options, &r->info, &err)) {
                        // Legacy DDS: 128-byte header, six faces of B8G8R8A8, one mip each.
                        const int fs = int(std::lround(std::sqrt((cube.size() - 128) / 24.0)));
                        if (fs > 0 && cube.size() == 128 + size_t(6) * size_t(fs) * size_t(fs) * 4) {
                            r->size = fs;
                            for (int f = 0; f < 6; ++f) {
                                std::vector<uint8_t> rgba(size_t(fs) * size_t(fs) * 4);
                                const uint8_t* src = reinterpret_cast<const uint8_t*>(cube.data()) + 128 + size_t(f) * rgba.size();
                                for (size_t k = 0; k < rgba.size(); k += 4) {
                                    rgba[k] = src[k + 2];
                                    rgba[k + 1] = src[k + 1];
                                    rgba[k + 2] = src[k];
                                    rgba[k + 3] = r->info.rgbm ? src[k + 3] : 255;  // RGBM: the multiplier
                                }
                                r->faces.push_back(std::move(rgba));
                            }
                        }
                        r->ok = true;
                    }
                    QMetaObject::invokeMethod(qApp, [self, skyKey, r] {
                        if (self) self->skyDecoded(skyKey, *r);
                    }, Qt::QueuedConnection);
                })).detach();
            }
            geomDirty_ = true;
        }
    }
    ctx_->scene.skyActive = ctx_->texturedView && skyTex_.size() == 6;
    mix(uint64_t(skyTex_.size()) << 20);
    const bool rebuild = key != geomKey_ || geomDirty_ || texRecheck_;
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
        // Play Preview walks without the spawn markers (the game has none; a selected start spawn filled the view:
        // map-agent report 2026-11-14).
        geo_.update(ctx_->scene, ctx_->doc, ctx_->showSpawns && !ctx_->playing, ctx_->texturedView && imageLookup_ ? &texFn_ : nullptr,
                    geomDirty_ || noInPlace, texRecheck_);
        updateSkyBox();  // probe captures reflect the sky too
        geomKey_ = key;
        // Textures still arriving: the next frame asks the materials again and makes again only the items whose
        // texture came (a full build per frame held the GUI thread 0.3-0.5 s a frame on a stock map).
        geomDirty_ = false;
        texRecheck_ = moreToLoad_;
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
        appendEffectParticles();  // their own pass (SceneRenderer::setParticles)
    } else {
        fxParticles_ = 0;
        renderer_->setParticles({});
    }
    // The gizmo is a small stream of its own, sent again only when it changed.
    gizmoLines_.scratch.clear();
    if (!cleanCapture_) addGizmoLines();
    if (!cleanCapture_) gizmoLines_.scratch.insert(gizmoLines_.scratch.end(), stockPickLines_.begin(), stockPickLines_.end());
    if (stockLightDirty_) {  // a stock map's light shapes (MainWindow::updateStockLightShapes)
        stockLightLines_.v.swap(stockLightPending_);
        stockLightPending_.clear();
        stockLightLines_.markAll();
        stockLightDirty_ = false;
    }
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
            renderer_->setSunShadow(true, m, float((2.0 + 1.5 * texel) / (4.0 * R)));
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
                renderer_->setSunShadowNear(true, mn, float((1.0 + 1.5 * texelN) / (4.0 * depthR)));
            } else {
                renderer_->setSunShadowNear(false, m, 0);
            }
        }
    }
    // The frame input; the view is made from the camera when the frame is drawn (drawPrepared).
    {
        app::FrameInput in;
        in.clear = kClear;
        in.shadowOnly = &geo_.shadowOnly;
        in.casters = &geo_.casters;
        in.faceSel = &geo_.faceSel;
        in.noCast = &geo_.noCast;
        in.lines = &geo_.lines;
        in.linesMisc = &geo_.linesMisc;
        in.linesExtra = cleanCapture_ ? nullptr : &stockLightLines_;
        in.overlay[0] = &geo_.sunArrow;
        in.overlay[1] = &geo_.selLines;
        in.overlay[2] = &geo_.overlayMisc;
        in.overlay[3] = &gizmoLines_;
        in.batches = &geo_.batches();
        in.extraBatches = &extraBatches_;
        in.casterVersion = geo_.casterVersion();
        in.clean = cleanCapture_;  // game surfaces only: no markers, tool faces, volumes or lines
        in.capture = cleanCapture_ || ctx_->scene.captureMode;  // no editor-only reference markers in a capture
        prepared_.in = in;
        prepared_.cam = ctx_->cam;
        prepared_.aspect = captureAspect_ > 0 ? captureAspect_ : double(w) / double(h);
        prepared_.geometryChanged = prepared_.geometryChanged || rebuild;  // until a draw has taken it
        prepared_.valid = true;
        prepared_.drawn = false;
        if (!maskClock_.isValid()) maskClock_.start();
        // While a material with UV animation is drawn, the view keeps redrawing (30 a second, the editor's window active).
        if (uvAnimSeen_ && !in.capture && !uvAnimTick_ && window() && window()->isActiveWindow()) {
            uvAnimTick_ = true;
            QTimer::singleShot(33, this, [this] {
                uvAnimTick_ = false;
                update();
            });
        }
    }
    lookActive_ = ctx_->looking && !ctx_->playing && !orbit_;
    flySpeed_ = ctx_->cam.speed;
    lookPixelScale_ = 1.0 / std::max(0.5, devicePixelRatioF());
    if (moreToLoad_ || refMoreToLoad_)
        QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);  // finish loading next frame
    ensureSwapChain();
    // Drawn here: captures, renderNow, a profiled benchmark, a view without a window, or IW8R_RENDER_THREAD=0.
    if (syncDraw_ || captureW_ > 0 || !swap_ || app::Prof::on || !renderThreadEnabled()) {
        bool stale = false;
        {
            IW8R_PROF("r.draw");
            stale = drawPrepared(ctx_->cam);
        }
        if (stale) QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
        if (!swap_) return;
        IW8R_PROF("r.present");
        presentFrame(true);
        return;
    }
    // Drawn and presented by the render thread.
    startRenderThread();
    {
        std::lock_guard l(frameMx_);
        frameWanted_ = true;
    }
    frameCv_.notify_one();
}

// ------------------------------------------------------------------ translate gizmo

bool CameraView::gizmoActive() const {
    return ctx_->gizmo && !ctx_->doc.selection().empty() && ctx_->doc.selectionBounds().valid() && !ctx_->clipMode;
}

void CameraView::setGizmoMode(GizmoMode m) {
    if (m == gizmoMode_) return;
    // A drag of the old mode's handle goes back first (its release would arrive in the new mode).
    if (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0) cancelViewDrag();
    clearGizmoHover();  // the hovered handle belongs to the other mode
    gizmoMode_ = m;
    update();
    emit gizmoModeChanged(int(m));
}

namespace {
// The one selected point entity (not a brush or patch of an entity), else null.
const Entity* singlePointEntity(const Document& doc) {
    if (doc.selection().size() != 1) return nullptr;
    const SelRef r = *doc.selection().begin();
    return r.item == kNoId ? doc.findEntity(r.entity) : nullptr;
}

// cos and sin of a turn in degrees; quarter turns exact (as brush_ops trigOf), so 90 degree turns leave no noise.
void gizmoTrig(double degrees, double& c, double& s) {
    const double q = degrees / 90.0;
    if (std::fabs(q - std::round(q)) < 1e-12) {
        const long long k = ((std::llround(q) % 4) + 4) % 4;
        static const double cs[4] = {1, 0, -1, 0}, sn[4] = {0, 1, 0, -1};
        c = cs[k], s = sn[k];
        return;
    }
    const double a = degrees * 3.14159265358979 / 180.0;
    c = std::cos(a), s = std::sin(a);
}
}  // namespace

Vec3 CameraView::gizmoPivot() const {
    // A single point entity turns and scales about its origin, where the gizmo is drawn (a model's bounds centre lies
    // elsewhere); anything else about the selection's centre. Both follow the selection while it is dragged.
    if (const Entity* e = singlePointEntity(ctx_->doc))
        if (const auto o = e->origin()) return *o;
    return ctx_->doc.selectionBounds().center();
}

Vec3 CameraView::ringCentre() const {
    if (ringDragAxis_ >= 0) return ringPivot_;
    // A single point entity turns in place, about its origin.
    if (const Entity* e = singlePointEntity(ctx_->doc))
        if (const auto o = e->origin()) return *o;
    // On the grid with Snap on (as Radiant rotates): grid-aligned brushes stay on the grid after 90 degree turns.
    return ctx_->snap ? snapToGrid(gizmoPivot(), ctx_->doc.grid()) : gizmoPivot();
}

Vec3 CameraView::gizmoCentre() const {
    // The rings (Rotate, All) are drawn about the turning pivot; All draws its arrows and boxes there too, so the three
    // parts share one centre.
    return gizmoMode_ == GizmoMode::Rotate || gizmoMode_ == GizmoMode::All ? ringCentre() : gizmoPivot();
}

bool CameraView::gizmoLocalFrame(Vec3 axes[3]) const {
    // Local space: one selected point entity with angles (brushes and groups keep the world axes).
    if (!ctx_->gizmoLocal) return false;
    const Entity* e = singlePointEntity(ctx_->doc);
    const std::string* a = e ? e->value("angles") : nullptr;
    Vec3 ang;
    if (!a || std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z) < 2) return false;
    const Mat3 m = anglesToMatrix(ang);
    for (int c = 0; c < 3; ++c) axes[c] = normalize(Vec3{m.m[0][c], m.m[1][c], m.m[2][c]});
    return true;
}

Vec3 CameraView::gizmoAxisDir(int axis) const {
    Vec3 axes[3];
    if (axis >= 0 && axis <= 2 && gizmoLocalFrame(axes)) return axes[axis];
    Vec3 d;
    if (axis >= 0 && axis <= 2) d[axis] = 1;
    return d;
}

void CameraView::ringFrame(int axis, Vec3& n, Vec3& u, Vec3& v) const {
    // Angle 0 at +u (the next axis), 90 at +v (the one after): a right-handed turn about +n, as rotatePoint turns.
    n = gizmoAxisDir(axis);
    u = gizmoAxisDir((axis + 1) % 3);
    v = gizmoAxisDir((axis + 2) % 3);
}

double CameraView::gizmoLength() const {
    // Constant on-screen size: a fraction of the distance to the camera.
    return std::max(8.0, length(gizmoCentre() - ctx_->cam.pos) * 0.18);
}

double CameraView::ringRadius() const {
    // All: the rings lie outside the arrow tips, so an arrow is never under a ring.
    return gizmoLength() * (gizmoMode_ == GizmoMode::All ? 1.25 : 0.9);
}

double CameraView::scaleBoxDistance() const {
    // Scale: the boxes end the handles; All: they sit on the arrows near the centre (the arrow tips move).
    return gizmoLength() * (gizmoMode_ == GizmoMode::All ? 0.45 : 1.0);
}

CameraView::GizmoAxis CameraView::gizmoAxisOnScreen(int axis) const {
    GizmoAxis out;
    if (!gizmoActive() || axis < 0 || axis > 2) return out;
    const Vec3 c = gizmoCentre();
    const auto a = projectToWidget(c), b = projectToWidget(c + gizmoAxisDir(axis) * gizmoLength());
    out.visible = a && b;
    if (out.visible) out.from = *a, out.to = *b;
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

std::optional<QPointF> CameraView::gizmoRingPoint(int axis, double degrees) const {
    if (!gizmoActive() || axis < 0 || axis > 2) return std::nullopt;
    Vec3 n, u, v;
    ringFrame(axis, n, u, v);
    const double r = ringRadius(), a = degrees * 3.14159265358979 / 180.0;
    return projectToWidget(ringCentre() + u * (r * std::cos(a)) + v * (r * std::sin(a)));
}

std::optional<QPointF> CameraView::gizmoScaleBoxPoint(int axis) const {
    if (!gizmoActive() || axis < 0 || axis > 2 || (gizmoMode_ != GizmoMode::Scale && gizmoMode_ != GizmoMode::All)) return std::nullopt;
    return projectToWidget(gizmoCentre() + gizmoAxisDir(axis) * scaleBoxDistance());
}

int CameraView::gizmoRingAt(const QPointF& p) const {
    if (!gizmoActive() || (gizmoMode_ != GizmoMode::Rotate && gizmoMode_ != GizmoMode::All)) return -1;
    // Rings seen edge-on project to a line through the centre and pass over the other rings' points; when two
    // rings are about as close, prefer the one facing the camera (its angle is well defined there).
    int best = -1;
    double bestScore = 1e9;
    const Vec3 view = ctx_->cam.forward();
    for (int a = 0; a < 3; ++a) {
        const double facing = std::fabs(dot(view, gizmoAxisDir(a)));  // |cos| between the ring's axis and the view
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
    return best;
}

int CameraView::gizmoHandleAt(const QPointF& p) const {
    // The move arrows (Move, All) or the scale handles (Scale: the whole handle up to its box).
    if (!gizmoActive() || gizmoMode_ == GizmoMode::Rotate) return -1;
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
    return best;
}

int CameraView::gizmoPartAt(const QPointF& p, int& axis) const {
    axis = -1;
    if (!gizmoActive()) return -1;
    if (gizmoMode_ == GizmoMode::All) {
        // The scale boxes sit on the arrows: within 7 px of a box's centre the box wins.
        double bestD = 7.0;
        for (int a = 0; a < 3; ++a)
            if (const auto q = gizmoScaleBoxPoint(a)) {
                const double d = std::hypot(p.x() - q->x(), p.y() - q->y());
                if (d < bestD) bestD = d, axis = a;
            }
        if (axis >= 0) return 2;
    }
    if ((axis = gizmoHandleAt(p)) >= 0) return gizmoMode_ == GizmoMode::Scale ? 2 : 0;
    if ((axis = gizmoRingAt(p)) >= 0) return 1;
    return -1;
}

void CameraView::updateGizmoHover(const QPointF& p, Qt::KeyboardModifiers mods) {
    // What a left press here would grab (Shift / Ctrl clicks select instead).
    const bool grabs = !(mods & (Qt::ShiftModifier | Qt::ControlModifier)) && !ctx_->looking && !ctx_->playing;
    int axis = -1;
    const int part = grabs ? gizmoPartAt(p, axis) : -1;
    if (part < 0) axis = -1;
    if (axis >= 0) setCursor(part == 1 ? Qt::OpenHandCursor : part == 2 ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
    else if (hoverAxis_ >= 0) unsetCursor();
    if (axis == hoverAxis_ && part == hoverPart_) return;
    hoverAxis_ = axis;
    hoverPart_ = part;
    update();  // the hovered handle is drawn highlighted
}

void CameraView::clearGizmoHover() {
    if (hoverAxis_ < 0) return;
    hoverAxis_ = -1;
    hoverPart_ = -1;
    unsetCursor();
    update();
}

std::optional<double> CameraView::ringAngleForMouse(const QPointF& p, int axis) const {
    const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
    const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
    Vec3 n, u, v;
    ringFrame(axis, n, u, v);
    const double denom = dot(n, ray.dir);
    if (std::fabs(denom) < 1e-6) return std::nullopt;
    const double t = dot(n, ringPivot_ - ray.origin) / denom;
    const Vec3 hit = ray.origin + ray.dir * t;
    return std::atan2(dot(hit - ringPivot_, v), dot(hit - ringPivot_, u)) * 180.0 / 3.14159265358979;
}

bool CameraView::rotateSelectionByGizmo(int axis, double degrees, const Vec3& pivot) {
    if (axis < 0 || axis > 2 || degrees == 0) return false;
    Vec3 n, u, v;
    ringFrame(axis, n, u, v);  // the dragged ring's axis: a world axis, or the entity's own (local space)
    // The turn about n (Rodrigues; right-handed, from +u toward +v for positive degrees as the ring's angles run).
    double c = 1, s = 0;
    gizmoTrig(degrees, c, s);
    const double t = 1 - c;
    Mat3 R;
    R.m[0][0] = t * n.x * n.x + c, R.m[0][1] = t * n.x * n.y - s * n.z, R.m[0][2] = t * n.x * n.z + s * n.y;
    R.m[1][0] = t * n.x * n.y + s * n.z, R.m[1][1] = t * n.y * n.y + c, R.m[1][2] = t * n.y * n.z - s * n.x;
    R.m[2][0] = t * n.x * n.z - s * n.y, R.m[2][1] = t * n.y * n.z + s * n.x, R.m[2][2] = t * n.z * n.z + c;
    struct Point {
        Id id;
        std::optional<Vec3> origin;
        Vec3 angles;
    };
    std::vector<Point> points;
    bool prims = false;
    for (const SelRef& r : ctx_->doc.selection()) {
        if (r.item != kNoId) {
            prims = true;
            continue;
        }
        const Entity* e = ctx_->doc.findEntity(r.entity);
        if (!e) continue;
        Point p{r.entity, e->origin(), {}};
        if (const std::string* a = e->value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &p.angles.x, &p.angles.y, &p.angles.z);
        points.push_back(p);
    }
    ctx_->doc.beginGroup("Rotate");
    // Brushes and patches turn about the world axis (local space needs one point entity alone, so the ring is a world
    // one whenever primitives are selected).
    bool any = prims && rotateSelectionAbout(*ctx_, axis, degrees, pivot);
    // Point entities: the origin about the pivot and the angles as the exact turn about n. (rotateSelectionAbout adds
    // the turn to one angle, which is a turn about the world axis only for yaw; the ring drawn on world X or Y turned a
    // yawed entity about its own axis.)
    const auto angle = [](double a) {
        a = std::round(a * 10000.0) / 10000.0;
        a = std::fmod(a, 360.0);
        if (a < 0) a += 360.0;
        if (std::fabs(a) < 1e-9 || std::fabs(a - 360.0) < 1e-9) a = 0;
        return formatNumber(a);
    };
    for (const Point& p : points) {
        if (p.origin) {
            const Vec3 o = pivot + R * (*p.origin - pivot);
            if (length(o - *p.origin) > 1e-9)
                any |= ctx_->doc.setKeyValue(p.id, "origin", formatNumber(o.x) + " " + formatNumber(o.y) + " " + formatNumber(o.z));
        }
        const Mat3 m = R * anglesToMatrix(p.angles);
        const Vec3 a = axisToAngles({Vec3{m.m[0][0], m.m[1][0], m.m[2][0]}, Vec3{m.m[0][1], m.m[1][1], m.m[2][1]},
                                     Vec3{m.m[0][2], m.m[1][2], m.m[2][2]}});
        any |= ctx_->doc.setKeyValue(p.id, "angles", angle(a.x) + " " + angle(a.y) + " " + angle(a.z));
    }
    ctx_->doc.endGroup();
    return any;
}

void CameraView::addGizmoLines() {
    scalePreviewEdges_ = 0;
    if (!gizmoActive()) return;
    const Vec3 c = gizmoCentre();
    const double L = gizmoLength();
    const bool arrows = gizmoMode_ == GizmoMode::Move || gizmoMode_ == GizmoMode::All;
    const bool rings = gizmoMode_ == GizmoMode::Rotate || gizmoMode_ == GizmoMode::All;
    const bool boxes = gizmoMode_ == GizmoMode::Scale || gizmoMode_ == GizmoMode::All;
    const bool dragging = gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0;
    // A part is drawn highlighted while it is dragged, or under a still mouse (part 0 arrow, 1 ring, 2 scale box).
    const auto hot = [&](int part, int a) {
        if (dragging) return part == 1 ? ringDragAxis_ == a : gizmoDragAxis_ == a && (part == 2) == dragScale_;
        return hoverAxis_ == a && hoverPart_ == part;
    };
    const uint32_t cols[3] = {0xff3040ffu, 0xff40ff40u, 0xffff6040u};  // X red, Y green, Z blue (ABGR)
    const uint32_t hotCol = 0xff40ffffu;
    Vec3 ax[3];
    for (int a = 0; a < 3; ++a) ax[a] = gizmoAxisDir(a);  // the world axes, or the entity's own in local space
    const auto line = [&](const Vec3& p, const Vec3& q, uint32_t col) {
        gizmoLines_.scratch.push_back(app::GpuVertex{float(p.x), float(p.y), float(p.z), col});
        gizmoLines_.scratch.push_back(app::GpuVertex{float(q.x), float(q.y), float(q.z), col});
    };
    const auto dim = [](uint32_t col) {  // the same colour at 55 %, for the secondary strokes
        const auto ch = [&](int sh) { return uint32_t(((col >> sh) & 0xffu) * 55 / 100) << sh; };
        return (col & 0xff000000u) | ch(0) | ch(8) | ch(16);
    };
    if (rings) {
        const Vec3 rc = ringCentre();
        const double r = ringRadius();
        for (int a = 0; a < 3; ++a) {
            const Vec3 u = ax[(a + 1) % 3], v = ax[(a + 2) % 3];
            const uint32_t col = hot(1, a) ? hotCol : cols[a];
            for (int k = 0; k < 64; ++k) {
                const double a0 = k * 2 * 3.14159265358979 / 64, a1 = (k + 1) * 2 * 3.14159265358979 / 64;
                line(rc + u * (r * std::cos(a0)) + v * (r * std::sin(a0)), rc + u * (r * std::cos(a1)) + v * (r * std::sin(a1)), col);
            }
            if (ringDragAxis_ == a) {
                // Spokes at the start angle (dim) and at the current snapped angle, joined by an arc: the turn as a wedge.
                const double from = ringStartAngle_ * 3.14159265358979 / 180.0;
                const double to = (ringStartAngle_ + ringDeltaDeg_) * 3.14159265358979 / 180.0;
                const auto at = [&](double ang, double rr) { return rc + u * (rr * std::cos(ang)) + v * (rr * std::sin(ang)); };
                line(rc, at(from, r), dim(col));
                line(rc, at(to, r), col);
                const int steps = std::max(1, int(std::ceil(std::fabs(ringDeltaDeg_) / 5.0)));
                for (int k = 0; k < steps; ++k)
                    line(at(from + (to - from) * k / steps, r * 0.6), at(from + (to - from) * (k + 1) / steps, r * 0.6), col);
            }
        }
    }
    if (arrows) {
        for (int a = 0; a < 3; ++a) {
            const Vec3& d = ax[a];
            const uint32_t col = hot(0, a) ? hotCol : cols[a];
            const Vec3 tip = c + d * L;
            line(c, tip, col);
            // Arrow head: a four-sided pyramid in the plane of the other two axes (turned with them in local space).
            const Vec3 base = tip - d * (L * 0.16);
            const Vec3 fin[4] = {base + ax[(a + 1) % 3] * (L * 0.06), base + ax[(a + 2) % 3] * (L * 0.06), base - ax[(a + 1) % 3] * (L * 0.06),
                                 base - ax[(a + 2) % 3] * (L * 0.06)};
            for (int k = 0; k < 4; ++k) {
                line(tip, fin[k], col);
                line(fin[k], fin[(k + 1) % 4], col);
            }
        }
    }
    if (boxes) {
        const double boxHalf = L * (gizmoMode_ == GizmoMode::All ? 0.05 : 0.06);
        for (int a = 0; a < 3; ++a) {
            const bool dragged = gizmoDragAxis_ == a && dragScale_;
            const uint32_t col = hot(2, a) ? hotCol : cols[a];
            if (dragged && scaleStartBounds_.valid()) {
                // Preview: the selection's bounds as they will be after release. One point entity (a model) scales
                // uniformly about its origin; anything else along this axis about the pivot.
                AABB b = scaleStartBounds_;
                const Vec3& p0 = gizmoStartPivot_;
                if (singlePointEntity(ctx_->doc)) {
                    b.mins = p0 + (scaleStartBounds_.mins - p0) * scaleFactor_;
                    b.maxs = p0 + (scaleStartBounds_.maxs - p0) * scaleFactor_;
                } else {
                    const double lo = p0[a] + (b.mins[a] - p0[a]) * scaleFactor_, hi = p0[a] + (b.maxs[a] - p0[a]) * scaleFactor_;
                    b.mins[a] = std::min(lo, hi), b.maxs[a] = std::max(lo, hi);
                }
                Vec3 k[8];
                for (int i = 0; i < 8; ++i) k[i] = Vec3{(i & 1) ? b.maxs.x : b.mins.x, (i & 2) ? b.maxs.y : b.mins.y, (i & 4) ? b.maxs.z : b.mins.z};
                const int boxEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto& ed : boxEdges) line(k[ed[0]], k[ed[1]], 0xff40ffffu);
                scalePreviewEdges_ = 12;
            }
            const Vec3 tip = c + ax[a] * (scaleBoxDistance() * (dragged ? scaleFactor_ : 1.0));
            // Scale: the handle is a line ending in the box; All: the arrow is the shaft (drawn while dragged, as the
            // box leaves it).
            if (gizmoMode_ == GizmoMode::Scale || dragged) line(c, tip, col);
            // The box, turned with the axes.
            const Vec3 e0 = ax[0] * boxHalf, e1 = ax[1] * boxHalf, e2 = ax[2] * boxHalf;
            Vec3 k[8];
            for (int i = 0; i < 8; ++i) k[i] = tip + ((i & 1) ? e0 : e0 * -1.0) + ((i & 2) ? e1 : e1 * -1.0) + ((i & 4) ? e2 : e2 * -1.0);
            const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& ed : edges) line(k[ed[0]], k[ed[1]], col);
        }
    }
}

// ------------------------------------------------------------------ play preview

// The play preview's collision as the built map has it (core play_world): the map's brushes, the contents of its
// prefabs and its placed models' hulls.
void CameraView::buildPlayWorld() {
    // The movers first: their brushes are left out of the static world and added back as moving groups. (The map's path
    // finds its own script, which runs on the same clock.)
    playBehaviours_.read(ctx_->doc.map(), ctx_->doc.path());
    iw8r::PlayWorldOptions o;
    o.skip = [this](const Entity& e) { return playBehaviours_.moves(e); };
    o.mapPath = ctx_->doc.path();
    for (const QString& r : PrefabBrowser::searchRoots()) o.prefabRoots.push_back(r.toStdString());
    o.modelOf = ctx_->scene.modelSource;
    o.modelScale = ctx_->scene.modelScale;
    iw8r::PlayWorldStats st;
    iw8r::buildPlayWorld(ctx_->doc.map(), o, world_, &st);
    if (playWorldExtras_) playWorldExtras_(world_);  // a stock map's world (MainWindow)
    playWorldModels_ = st.models;
    playBehaviours_.attach(ctx_->doc.map(), world_);
}

void CameraView::startPlay() {
    if (ctx_->playing) return;
    // P mid-drag: the drag goes back first (its release would arrive while playing and leave the document's drag open).
    cancelViewDrag();
    clearGizmoHover();
    playMode_ = nullptr;
    buildPlayWorld();
    worldRevision_ = ctx_->doc.revision();
    // Spawn: a selected spawn entity (its origin and yaw), else below the camera; when the camera is outside the map
    // (or above all of it, as after opening a map), the spawn nearest the camera.
    Vec3 from = ctx_->cam.pos;
    double yaw = ctx_->cam.yaw;
    bool chosen = false;
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
            chosen = true;
            break;
        }
    }
    player_ = PlayerState{};
    bool floor = false;
    if (!chosen) {
        const AABB wb = world_.bounds();
        // Inside the map's footprint and not above all of it (a camera under the ceiling of a closed room counts).
        const bool inside = wb.valid() && from.x > wb.mins.x && from.x < wb.maxs.x && from.y > wb.mins.y && from.y < wb.maxs.y &&
                            from.z <= wb.maxs.z + 1;
        floor = inside && placePlayer(world_, player_, from, params_);
        if (!floor) {
            // The nearest spawn: the document's spawn entities, then a stock map's compiled spawns.
            double best = 1e300;
            std::optional<std::pair<Vec3, double>> spawn;
            const auto consider = [&](const std::string& cls, const Vec3& o, double y) {
                if (!(cls.rfind("mp_", 0) == 0 && cls.find("spawn") != std::string::npos) && cls != "info_player_start") return;
                const double d = length(o - ctx_->cam.pos);
                if (d < best) best = d, spawn = std::pair<Vec3, double>{o, y};
            };
            for (const auto& e : ctx_->doc.map().entities)
                if (auto o = e.origin()) {
                    double p = 0, y = 0, rr = 0;
                    if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &rr);
                    consider(e.classname(), *o, y);
                }
            for (const auto& s : ctx_->scene.spawns) consider(s.name, s.origin, s.angles.y);
            if (spawn) {
                from = spawn->first + Vec3{0, 0, 8};
                yaw = spawn->second;
                player_ = PlayerState{};
                floor = placePlayer(world_, player_, from, params_);
            }
        }
        if (!floor && !inside) player_ = PlayerState{}, floor = placePlayer(world_, player_, ctx_->cam.pos, params_);
    } else {
        floor = placePlayer(world_, player_, from, params_);
    }
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
    setMouseTracking(true);  // stays on: the gizmo hover needs moves without a button
    unsetCursor();
    emit ctx_->playModeChanged(false);
    ctx_->setStatus("Play preview ended");
    if (!ctx_->scene.playPoses.empty()) {
        ctx_->scene.playPoses.clear();  // the movers are drawn at rest again
        ctx_->scene.invalidate();
    }
    ctx_->cameraMoved();
}

// Play preview keys: the game's default keyboard binds (default_keys.cfg of Replay 1.20, research 140 section 6).
static int playQtKey(PlayKey k) {
    switch (k) {
        case PlayKey::Forward: return Qt::Key_W;
        case PlayKey::Back: return Qt::Key_S;
        case PlayKey::Left: return Qt::Key_A;
        case PlayKey::Right: return Qt::Key_D;
        case PlayKey::Jump: return Qt::Key_Space;      // +gostand
        case PlayKey::Crouch: return Qt::Key_C;        // +movedown / togglecrouch
        case PlayKey::Prone: return Qt::Key_Control;   // +prone (LCTRL)
        case PlayKey::Sprint: return Qt::Key_Shift;    // +breath_sprint (LSHIFT)
        case PlayKey::Use: return Qt::Key_F;           // +activate
        case PlayKey::AutoForward: return Qt::Key_H;   // autoforward
        case PlayKey::Count: break;
    }
    return 0;
}

static PlayKey playKeyForQt(int qtKey) {
    for (int k = 0; k < int(PlayKey::Count); ++k)
        if (playQtKey(PlayKey(k)) == qtKey) return PlayKey(k);
    return PlayKey::Count;
}

void CameraView::tickPlay(double dt) {
    if (!ctx_->playing) return;
    if (worldRevision_ != ctx_->doc.revision()) {  // edits while playing (undo, live changes)
        buildPlayWorld();
        worldRevision_ = ctx_->doc.revision();
    }
    if (!playMode_) playControls_.reset();  // a new play session (startPlay clears playMode_)
    // The game's default keyboard binds (default_keys.cfg; research 140): the held keys go through PlayControls, the
    // game's client side (toggle crouch and prone, Space stands up before it jumps, tap C to slide, Shift again for
    // tactical sprint, H autoforward), which makes the command for each movement step.
    auto down = [&](int k) { return ctx_->heldKeys.contains(k); };
    bool held[int(PlayKey::Count)] = {};
    for (int k = 0; k < int(PlayKey::Count); ++k)
        if (const int qk = playQtKey(PlayKey(k))) held[k] = down(qk);
    player_.yaw = ctx_->cam.yaw;
    player_.pitch = ctx_->cam.pitch;
    // The map's scripted objects (core play_behaviour): F is the game's Use key.
    {
        const bool useDown = down(Qt::Key_F);
        const bool use = useDown && !playUseHeld_;
        playUseHeld_ = useDown;
        const auto events = playBehaviours_.tick(std::min(dt, 0.25), player_.origin, playerMins(params_), playerMaxs(params_, player_.crouched),
                                                 use, world_);
        for (const auto& ev : events) {
            if (ev.kind == PlayBehaviours::Event::Teleport) {
                player_.origin = ev.position;
                player_.velocity = Vec3{};
                player_.yaw = ctx_->cam.yaw = ev.yaw;
                ctx_->setStatus("Play: teleported by " + QString::fromStdString(ev.name));
            } else if (ev.kind == PlayBehaviours::Event::Launch) {
                player_.velocity = ev.velocity;
                player_.onGround = false;
            }
        }
        if (!playBehaviours_.poses().empty()) {
            for (const auto& p : playBehaviours_.poses()) {
                app::SceneCache::PlayPose& pp = ctx_->scene.playPoses[p.entity];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) pp.R[i][j] = p.R[i][j];
                pp.pivot = p.pivot;
                pp.move = p.move;
                pp.visible = p.visible;  // hidden by the map script
            }
            ctx_->scene.invalidate();
        }
        for (const GscNote& n : playBehaviours_.takeScriptNotes())
            EditorLog::instance().add(LogLevel::Info, "script",
                                      QString("Play preview, map script%1: %2")
                                          .arg(n.line > 0 ? QString(" line %1").arg(n.line) : QString(), QString::fromStdString(n.text)));
    }
    // Fixed 1/60 s steps (the mover clamps long frames; this keeps jumps identical at any frame rate).
    double left = std::min(dt, 0.25);
    do {
        const double step = std::min(left, 1.0 / 60.0);
        const MoveInput in = playControls_.input(held, player_, step);  // every step: the stance follows a slide's end
        if (step > 0) playerMove(world_, player_, in, params_);
        left -= step;
    } while (left > 1e-9);
    ctx_->cam.pos = player_.origin + Vec3{0, 0, player_.noclip ? 0.0 : eyeHeight(params_, player_)};
    // Movement mode in the status bar when it changes (mantle / slide / sprint / stance).
    const char* mode = player_.noclip ? "noclip" : player_.mantling ? "mantle" : player_.onLadder ? "ladder" : player_.sliding ? "slide"
                       : player_.tacticalSprinting ? "tactical sprint" : player_.sprinting ? "sprint" : player_.prone ? "prone"
                       : player_.crouched ? "crouch" : !player_.onGround && !player_.standing ? "air" : "walk";
    if (mode != playMode_) {
        playMode_ = mode;
        ctx_->setStatus(QString("Play: %1  (game keys: WASD, Space jump/stand, C crouch/slide, Ctrl prone, Shift sprint (again: tactical), "
                                "F use, H autoforward; N noclip, Esc exit)")
                            .arg(mode));
    }
    ctx_->cameraMoved();
}

std::optional<std::array<float, 16>> CameraView::materialConstantsFor(const std::string& m) const {
    // The stock material's layer-0 constants (spec / gloss remap, normal strength) for the Game Lighting shader.
    if (m.empty() || !factsLookup_ || m.find_first_of("\x01\x02\x05\x06\x07") != std::string::npos) return std::nullopt;
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
    if (m.empty() || !factsLookup_ || m.find_first_of("\x01\x02\x05\x06\x07") != std::string::npos) return true;
    const auto f = factsLookup_(m);
    const bool uvAnim = f && (f->uvAnim[0] != 0 || f->uvAnim[1] != 0 || f->uvAnim[2] != 0);
    const bool waves = f && propMesh && (f->wave[7] != 0 || f->wave[11] != 0 || f->wave[15] != 0);  // c80..c82 amplitudes
    if (waves) {
        renderer_->setReferenceInstWave(index, f->wave.data());
        uvAnimSeen_ = true;  // the view keeps redrawing while it moves
    }
    // Static-model displacement (research 133 section 3): the height image and constant 45, moved along the normal in
    // the props' vertex shader.
    static const bool noDisp = std::getenv("IW8R_NO_SMODEL_DISP") != nullptr;  // A/B: undisplaced, as before
    if (f && propMesh && !noDisp && !f->dispImage.empty()) {
        const std::string key = "\x07" + f->dispImage;
        const int id = textureFor(key);
        if (!texIds_.count(key)) return false;  // still loading
        if (id >= 0 && id != checkerTex_) renderer_->setReferenceInstDisp(index, id, f->disp[0], f->disp[1]);
    }
    static const bool noDual = std::getenv("IW8R_NO_DUAL_NORMAL") != nullptr;  // A/B: one normal layer as before
    if (f && !propMesh && !noDual && (f->dualNormal[0] > 0 || f->dualNormal[4] > 0)) {
        renderer_->setReferenceBaseDualNormal(index, f->dualNormal.data());
        uvAnimSeen_ = true;  // the layers scroll
    }
    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0 && !f->unlit && !uvAnim)) return true;
    if (uvAnim) uvAnimSeen_ = true;
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
                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, f->unlit ? 1.0f : 0.0f,
                   f->uvAnim[0], f->uvAnim[1], f->uvAnim[2]};
    if (propMesh) renderer_->setReferenceInstExtra(index, x, ids[0], ids[1]);
    else renderer_->setReferenceBaseExtra(index, x, ids[0], ids[1]);
    return true;
}

void CameraView::setBaseMaterialConstants(size_t k) {
    if (k >= refTexMaterials_.size()) return;
    if (const auto p = materialConstantsFor(refTexMaterials_[k])) renderer_->setReferenceBaseParams(k, p->data());
}

bool CameraView::resolvePropLayers(size_t k) {
    if (!layerLookup_ || k >= texInstMaterials_.size()) return true;
    const std::string& base = texInstMaterials_[k];
    if (base.empty() || base.find_first_of("\x01\x02\x05\x06\x07") != std::string::npos) return true;
    const std::optional<StockLayeredMaterial> c = layerLookup_(base);
    if (!c || c->layers < 2) return true;
    // Research 136: the layer kinds and UV sets from the technique set name, the slot-126 mask.
    const auto facts = factsLookup_ ? factsLookup_(base) : std::nullopt;
    static const bool noLerp = std::getenv("IW8R_NO_LERP_LAYERS") != nullptr;  // A/B: multiply layers only, as before
    int maskId = -1;
    if (facts && !facts->layerMaskImage.empty()) {
        const std::string mk = "\x07" + facts->layerMaskImage;  // stored sRGB: read linear, as the game's sampler does
        const int id = textureFor(mk);
        if (!texIds_.count(mk)) return false;  // still loading
        maskId = id == checkerTex_ ? -1 : id;
    }
    std::vector<app::PropLayer> out;
    for (int L = 1; L < c->layers && size_t(L) < c->layer.size(); ++L) {
        const StockLayerConstants& lc = c->layer[size_t(L)];
        const int kind = facts && size_t(L) < facts->layerKind.size() ? facts->layerKind[size_t(L)] : 0;
        const uint32_t hex = facts && size_t(L) < facts->layerHex.size() ? facts->layerHex[size_t(L)] : 0u;
        // A code layer is a clear coat (research 137): its own block, no colour; the emissive kind is left out.
        if (kind == 2 && (hex & 0x20u)) continue;
        // Overlay layers (soft light) ignore the colour mode; standard layers: 0 lerp, 1 multiply, 2 / 3 normal and gloss
        // only (not drawn: a blended pass cannot change the normal under the shaded pixel).
        // kind 3: modes 2 / 3, normal / cavity / gloss only (folded into the mesh's pass; never a pass of its own).
        float drawKind = kind == 1 ? 2.0f : kind == 2 ? 4.0f : 0.0f;
        if (kind == 0) {
            if (lc.colorMode >= 2) drawKind = 3.0f;
            else if (lc.colorMode == 1) drawKind = 1.0f;
            else if (noLerp) continue;
        }
        const std::string ck = base + '\x01' + std::to_string(L), rk = base + '\x02' + std::to_string(L);
        const int cid = textureFor(ck), rid = textureFor(rk);
        if (!texIds_.count(ck) || !texIds_.count(rk)) return false;  // still loading
        const bool noColour = cid < 0 || cid == checkerTex_;
        if (noColour && drawKind < 2.5f) continue;  // no colour image
        // The layer's normal + gloss + cavity map (research 136): only a folded layer reads it. A coat has one only with
        // hex bit 0x10 (research 137); without it the coat takes the vertex normal and a fixed gloss.
        int nid = -1;
        if (drawKind != 4.0f || (hex & 0x10u)) {
            nid = normalFor(ck);
            if (!nrmIds_.count(ck)) return false;  // still loading
        }
        app::PropLayer pl;
        pl.texture = noColour ? -1 : cid;
        pl.reveal = rid == checkerTex_ ? -1 : rid;
        pl.normal = nid;
        if ((lc.flags & 64u) && facts && size_t(L) < facts->layerTintMaskImage.size() && !facts->layerTintMaskImage[size_t(L)].empty()) {
            const std::string tk = "\x07" + facts->layerTintMaskImage[size_t(L)];
            const int tid = textureFor(tk);
            if (!texIds_.count(tk)) return false;  // still loading
            pl.tintMask = tid == checkerTex_ ? -1 : tid;
        }
        pl.nrm[0] = nid >= 0 ? float(lc.normalMode + 1) : 0.0f;
        pl.nrm[1] = lc.normalStrength, pl.nrm[2] = lc.glossScale, pl.nrm[3] = lc.glossBias;
        const float p[16] = {1.0f, float(lc.flags), lc.sharpness, drawKind, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                             lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
        std::copy(p, p + 16, pl.params);
        pl.aux[0] = facts && size_t(L) < facts->layerUv1.size() && facts->layerUv1[size_t(L)] ? 1.0f : 0.0f;
        if (maskId >= 0 && L <= 3) pl.mask = maskId, pl.aux[1] = float(L);  // channel L - 1, stored + 1
        out.push_back(pl);
    }
    renderer_->setReferenceInstLayers(k, std::move(out));
    return true;
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
    if (nid >= 0) renderer_->setReferenceNormal(k, nid);
    // A decal material's layers draw with the decal, in its sort order (research 98).
    if (factsLookup_)
        if (const auto f = factsLookup_(base); f && f->decalSortKey > 0) {
            const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], f->ssr ? 1.0f : 0.0f,
                                 lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
            renderer_->setReferenceTexture(k, cid);
            renderer_->setReferenceDecal(k, f->decalSortKey, rid == checkerTex_ ? -1 : rid, p);
            return true;
        }
    const bool layerSsr = factsLookup_ && [&] { const auto f = factsLookup_(base); return f && f->ssr; }();
    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], layerSsr ? 1.0f : 0.0f,
                         lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
    renderer_->setReferenceTexture(k, cid);
    renderer_->setReferenceLayer(k, rid == checkerTex_ ? -1 : rid, p);
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
    renderer_->setReferenceDecal(k, f->decalSortKey, rid == checkerTex_ ? -1 : rid, p);
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
    // The own materials' shader state again (the library may have been reloaded with changed definitions).
    for (const std::string& m : std::vector<std::string>(stateMaterials_.begin(), stateMaterials_.end()))
        if (const auto it = texIds_.find(m); it != texIds_.end()) applyMaterialState(m, it->second);
    update();
}

size_t CameraView::texturedTriangles() const {
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
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

int CameraView::textureSideNow() const { return renderer_->textureBytes() > textureBudget_ ? 128 : kViewTextureSide; }

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
    if (material == "iw8r/techset_preview") {
        const auto it = texIds_.find(material); if (it != texIds_.end()) return it->second;
        const uint8_t white[4] = {255,255,255,255};
        return texIds_[material] = renderer_->createTexture(white,1,1);
    }

    if (material.empty() || isIw8ToolMaterial(material)) return -1;  // tool faces keep their flat colour
    if (auto it = texIds_.find(material); it != texIds_.end()) return it->second;
    std::optional<QString> path = imageLookup_ ? imageLookup_(material) : std::nullopt;
    if (!path) {
        if (imageRequest_ && requested_.insert(material).second) imageRequest_(material);
        // A known-unavailable asset can answer synchronously. Its signal invalidates geometry while it is
        // being built; consume that answer now instead of retaining a flat batch until the next edit.
        path = imageLookup_ ? imageLookup_(material) : std::nullopt;
        if (!path) return -1;  // an asynchronous answer calls materialImagesChanged()
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
            checkerTex_ = renderer_->createTexture(px.data(), 64, 64);
        }
        return texIds_[material] = checkerTex_;
    }
    // Layer, decal and decal volume images ("\x01" / "\x02" / "\x05" keys) are plain files, never tinted or masked
    // per material: one texture per file (mp_aniyah_tac decoded the same layer images once per material, 37,000
    // textures, and ran out of video memory).
    const bool shareable = material.find_first_of("\x01\x02\x05\x06\x07") != std::string::npos;
    // A data mask ("\x07") stored in an sRGB format: the game's sampler linearises it, so the upload does too. Shared
    // under its own path key (the same file as a colour image stays as stored).
    static const bool rawMasks = qEnvironmentVariableIsSet("IW8R_RAW_LAYER_MASKS");  // A/B: the stored bytes, as before
    const bool srgbData = !rawMasks && material[0] == '\x07' && path->endsWith(".dds", Qt::CaseInsensitive) && app::ddsIsSrgb(path->toStdString());
    const std::string pathKey = (srgbData ? "srgb:" : "") + path->toStdString();
    if (shareable)
        if (const auto it = texByPath_.find(pathKey); it != texByPath_.end()) return texIds_[material] = it->second;
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
        if (const auto it = texByPathBc_.find(pathKey); it != texByPathBc_.end()) {
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
            // BC1 / BC2 / BC3 / BC7 UNORM + 1 = the _SRGB format.
            id = renderer_->createTextureBlocks(blocks.dxgiFormat + (srgbData ? 1 : 0), blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {
                texSizes_[material] = {fullW, fullH};
                if (textureSideNow() < kViewTextureSide) ++budgetReduced_;
                g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
                if (shareable) texByPath_[pathKey] = id;
                texByPathBc_[pathKey] = id;
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
                // A layer-0 "alpha + reveal" image (slot 27 of a blended decal, research 97: alpha in red, reveal in
                // green), its alpha a flat white (`white&..._r`): the shape is the reveal. Shipment's container logos
                // drew as solid rectangles from the white red channel (owner 2026-11-15).
                if (maskChannel == 0) {
                    uint8_t rMin = 255, rMax = 0, gMin = 255, gMax = 0;
                    for (size_t k = 0; k + 3 < mask.pixels.size(); k += 4) {
                        rMin = std::min(rMin, mask.pixels[k]), rMax = std::max(rMax, mask.pixels[k]);
                        gMin = std::min(gMin, mask.pixels[k + 1]), gMax = std::max(gMax, mask.pixels[k + 1]);
                    }
                    if (rMin == rMax && gMax > gMin) maskChannel = 1;
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
            if (srgbData) {  // a data mask stored sRGB: the linear values the game's sampler returns
                static const auto lin = [] {
                    std::array<uint8_t, 256> t{};
                    for (int v = 0; v < 256; ++v) {
                        const double s = v / 255.0, l = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
                        t[size_t(v)] = uint8_t(std::lround(l * 255.0));
                    }
                    return t;
                }();
                for (size_t k = 0; k + 3 < img.pixels.size(); k += 4)
                    for (int c = 0; c < 3; ++c) img.pixels[k + size_t(c)] = lin[img.pixels[k + size_t(c)]];
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
        id = renderer_->createTexture(img.pixels.data(), img.width, img.height);
    }
    g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
    if (shareable && id >= 0) texByPath_[pathKey] = id;
    if (id >= 0 && !shareable) applyMaterialState(material, id);
    return texIds_[material] = id;
}

void CameraView::applyMaterialState(const std::string& material, int texture) {
    if (!stateLookup_ || texture < 0) return;
    if (const auto s = stateLookup_(material)) {
        renderer_->setTextureMaterial(texture, *s);
        stateMaterials_.insert(material);
    } else if (stateMaterials_.erase(material)) {
        renderer_->setTextureMaterial(texture, MaterialState{});  // no longer an own material: the plain state
    }
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

int CameraView::particleTexture(const std::string& material, const ParticleMaterial& pm) {
    // "\x08<material>": the sprite as the effect draws it. Lit: the colour image with its opacity (slot 27's alpha when
    // it varies, else its red; without one the colour image's own alpha). Unlit: the emissive image, opaque.
    // Returns -2 while an image is still being read (the particle is not drawn yet), -1 when the material has none.
    const std::string key = "\x08" + material;
    if (const auto it = texIds_.find(key); it != texIds_.end()) return it->second;
    if (pm.distortion) {
        // The distortion image as data (research 139 section 2): stored sRGB, it is read linear, as the game's sampler does.
        if (pm.distortionFile.empty()) return texIds_[key] = -1;
        DecodedTexture d;
        if (!decodedTexture(pm.distortionFile, kViewTextureSide, 1, d)) {
            moreToLoad_ = true;
            return -2;
        }
        if (!d.imageOk || d.image.width <= 0 || d.image.height <= 0) return texIds_[key] = -1;
        if (app::ddsIsSrgb(pm.distortionFile)) {
            uint8_t lin[256];
            for (int i = 0; i < 256; ++i) {
                const double c = i / 255.0;
                lin[i] = uint8_t(std::lround(255.0 * (c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4))));
            }
            for (size_t k = 0; k < d.image.pixels.size(); ++k)
                if (k % 4 != 3) d.image.pixels[k] = lin[d.image.pixels[k]];
        }
        // The strength mask: the world's distortion images (BC1) keep it in blue, which the decoded shader reads; the
        // effect images (BC3) keep it in alpha with blue empty or flat (research 139 section 2.2; vfx_dist_heat_3_c:
        // blue 123-131 of 255 everywhere, so the heat haze of vfx_safehouse_finale_drone_heat_dist moved nothing):
        // moved into blue (the same rule as the side bar's preview, vfx_textures.cpp prepareVfxDistortion).
        uint8_t blueMin = 255, blueMax = 0, alphaMin = 255, alphaMax = 0;
        for (size_t k = 0; k + 3 < d.image.pixels.size(); k += 4) {
            blueMin = std::min(blueMin, d.image.pixels[k + 2]), blueMax = std::max(blueMax, d.image.pixels[k + 2]);
            alphaMin = std::min(alphaMin, d.image.pixels[k + 3]), alphaMax = std::max(alphaMax, d.image.pixels[k + 3]);
        }
        if ((blueMax < 8 && alphaMax > alphaMin) || (blueMax - blueMin < 16 && alphaMax - alphaMin > 32))
            for (size_t k = 0; k + 3 < d.image.pixels.size(); k += 4) d.image.pixels[k + 2] = d.image.pixels[k + 3];
        return texIds_[key] = renderer_->createTexture(d.image.pixels.data(), d.image.width, d.image.height);
    }
    const std::string& main = pm.lit ? pm.colorFile : (pm.emissiveFile.empty() ? pm.colorFile : pm.emissiveFile);
    if (main.empty()) return texIds_[key] = -1;
    DecodedTexture a, m;
    if (!decodedTexture(main, kViewTextureSide, 1, a)) {
        moreToLoad_ = true;
        return -2;
    }
    const bool withMask = pm.lit && !pm.alphaFile.empty();
    if (withMask && !decodedTexture(pm.alphaFile, kViewTextureSide, 1, m)) {
        moreToLoad_ = true;
        return -2;
    }
    DecodedTexture dn;
    const bool withDensity = pm.lit && !withMask && !pm.densityFile.empty();
    if (withDensity && !decodedTexture(pm.densityFile, kViewTextureSide, 1, dn)) {
        moreToLoad_ = true;
        return -2;
    }
    if (!a.imageOk || a.image.width <= 0 || a.image.height <= 0) return texIds_[key] = -1;
    app::Rgba8Image& img = a.image;
    if (withMask && m.imageOk && m.image.width > 0 && m.image.height > 0) {
        int ch = 0;  // red, unless the alpha varies
        for (size_t k = 3; k < m.image.pixels.size(); k += 4)
            if (m.image.pixels[k] < 250) {
                ch = 3;
                break;
            }
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const int mx = int(int64_t(x) * m.image.width / img.width), my = int(int64_t(y) * m.image.height / img.height);
                img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3] = m.image.pixels[(size_t(my) * size_t(m.image.width) + size_t(mx)) * 4 + size_t(ch)];
            }
    } else if (withDensity && dn.imageOk && dn.image.width > 0 && dn.image.height > 0) {
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const int mx = int(int64_t(x) * dn.image.width / img.width), my = int(int64_t(y) * dn.image.height / img.height);
                uint8_t& a = img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3];
                // Slot 68 is a transmittance (255 at the cells' edges, near 0 in the smoke): alpha x (1 - T).
                a = uint8_t((int(a) * (255 - int(dn.image.pixels[(size_t(my) * size_t(dn.image.width) + size_t(mx)) * 4])) + 127) / 255);
            }
    } else if (!pm.lit) {
        for (size_t k = 3; k < img.pixels.size(); k += 4) img.pixels[k] = 255;
    }
    // Never a hard square (owner 2026-11-15, "some vfx still show as squares"): a sprite whose image has no edge of its
    // own, a lit one without any opacity (no slot 27 / 68 read, its colour image opaque) or an unlit one bright up to its
    // border, gets a soft round edge in each atlas cell. A stand-in (INFERRED): the game's sprites carry their shape in
    // these images; this only keeps a missing or unread one from drawing as a square.
    {
        const int cols = std::max(1, pm.cols), rows = std::max(1, pm.rows);
        const double cw = double(img.width) / cols, ch = double(img.height) / rows;
        bool opaque = true;
        for (size_t k = 3; k < img.pixels.size() && opaque; k += 4) opaque = img.pixels[k] >= 250;
        // The mean brightness of the pixels on the cells' borders (an unlit sprite adds: a bright border is a square).
        double border = 0;
        size_t borderN = 0;
        for (int y = 0; y < img.height; ++y) {
            const double ly = std::fmod(y + 0.5, ch);
            const bool rowEdge = ly < 1.5 || ly > ch - 1.5;
            for (int x = 0; x < img.width; ++x) {
                const double lx = std::fmod(x + 0.5, cw);
                if (!rowEdge && lx >= 1.5 && lx <= cw - 1.5) continue;
                const uint8_t* p = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
                border += (p[0] + p[1] + p[2]) / 3.0;
                ++borderN;
            }
        }
        const bool brightEdge = borderN && border / double(borderN) > 24.0;
        // (A dirty lens image is read at the screen's UV under its own round mask: left as it is.)
        static const bool plainQuads = qEnvironmentVariableIsSet("IW8R_PLAIN_PARTICLE_QUADS");  // A/B: as before
        const bool soften = !plainQuads &&
                            (pm.lit ? (opaque && !(withMask && m.imageOk) && !(withDensity && dn.imageOk)) : (brightEdge && !pm.screenMask));
        if (soften)
            EditorLog::instance().add(LogLevel::Info, "effects",
                                      QString("Effect preview: %1 has no edge of its own (%2): drawn with a soft round edge")
                                          .arg(QString::fromStdString(material), pm.lit ? "lit, its images give no opacity" : "unlit, bright to its border"));
        if (soften)
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    const double u = std::fmod(double(x) + 0.5, double(img.width) / cols) * cols / img.width - 0.5;
                    const double v = std::fmod(double(y) + 0.5, double(img.height) / rows) * rows / img.height - 0.5;
                    const double w = std::clamp((1.0 - 2.0 * std::sqrt(u * u + v * v)) / 0.4, 0.0, 1.0);
                    uint8_t* p = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
                    if (pm.lit) p[3] = uint8_t(std::lround(p[3] * w));
                    else
                        for (int c = 0; c < 3; ++c) p[c] = uint8_t(std::lround(p[c] * w));
                }
    }
    const int id = renderer_->createTexture(img.pixels.data(), img.width, img.height);
    return texIds_[key] = id;
}

void CameraView::appendEffectParticles() {
    // Research 138: every placed effect's particles as world quads, sorted far to near, one batch per run of a sprite.
    if (whiteTex_ < 0) {
        const uint8_t px[4] = {255, 255, 255, 255};
        whiteTex_ = renderer_->createTexture(px, 1, 1);
    }
    const double yaw = ctx_->cam.yaw * 3.14159265358979 / 180.0, pitch = ctx_->cam.pitch * 3.14159265358979 / 180.0;
    const Vec3 fwd{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    const Vec3 right = normalize(cross(fwd, Vec3{0, 0, 1}));
    const Vec3 up = normalize(cross(right, fwd));
    if (!fxClock_.isValid()) fxClock_.start();
    const double t = fxTimeOverride_ >= 0 ? fxTimeOverride_ : fxClock_.elapsed() / 1000.0;
    fxParticles_ = 0;
    fxGridLit_ = 0, fxDistortion_ = 0, fxFlares_ = 0, fxDistAlphaMax_ = 0, fxDistAlphaSum_ = 0;
    std::set<std::string> sunFlaresDrawn;  // sun flare effects already drawn this frame (one placement each)
    // Flares (research 139 section 1) are placed on screen with the frame's own projection.
    const double aspect = captureAspect_ > 0 ? captureAspect_ : double(std::max(1, width())) / double(std::max(1, height()));
    const app::Mat4f fvp = app::viewProjection(ctx_->cam, aspect);
    const auto toClip = [&](const Vec3& p, double out[4]) {
        for (int k = 0; k < 4; ++k) out[k] = p.x * fvp.m[k] + p.y * fvp.m[4 + k] + p.z * fvp.m[8 + k] + fvp.m[12 + k];
    };
    uint32_t seed = 1;
    // Research 139 section 3: in Game Lighting on a stock map a lit sprite takes the stock light grid's light at its
    // centre (averaged over directions: a particle has no normal), x the map's baked scale.
    static const bool noParticleLight = qEnvironmentVariableIsSet("IW8R_NO_PARTICLE_LIGHT");  // A/B, as the renderer
    const std::shared_ptr<const StockGpuGrid> grid =
        !noParticleLight && ctx_->scene.gameLook && stockGrid_ && stockGridZone_ == gameStockMap_ ? stockGrid_ : nullptr;
    const double gridScale = stockBakedScale_;
    struct Placed {
        Vec3 origin, angles;
        const std::string* fx;
        uint64_t object = 0;
        double repeat = 0;  // an fx_origin's delay key (seconds between starts, as the build's script repeats it); 0 = none
    };
    // The mapper's fx_origin entities, then the stock map's own placed effects.
    std::vector<Placed> placed;
    for (const Entity& e : ctx_->doc.map().entities) {
        if (e.classname() != "fx_origin") continue;
        const std::string* fx = e.value("fx");
        const auto o = e.origin();
        Vec3 ang{0, 0, 0};
        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
        double repeat = 0;
        if (const std::string* d = e.value("delay")) repeat = std::max(0.0, std::atof(d->c_str()));
        if (fx && o) placed.push_back({*o, ang, fx, e.id, repeat});
    }
    for (const StockEffect& s : stockEffects_) placed.push_back({s.origin, s.angles, &s.name, 0, 0.0});
    const Vec3 eye = ctx_->cam.pos;
    struct Quad {
        uint64_t object = 0;
        std::string material;
        float depth;
        int texture;
        bool lit;
        bool distortion;
        bool screen;
        float emissive[3];
        float distScale[2];
        bool screenMask = false;  // a dirty lens piece: the image at the screen's UV, masked by `mask` (slot 145)
        int mask = -1;
        app::ParticleVertex v[6];
    };
    std::vector<Quad> quads;
    // A dirty lens set's slot 145 image (research 139 section 5): its red channel is the piece's round mask.
    const auto maskTexture = [&](const std::string& file) -> int {
        if (file.empty()) return -1;
        const std::string key = "\x09" + file;
        if (const auto it = texIds_.find(key); it != texIds_.end()) return it->second;
        DecodedTexture d;
        if (!decodedTexture(file, kViewTextureSide, 1, d)) {
            moreToLoad_ = true;
            return -1;
        }
        if (!d.imageOk || d.image.width <= 0 || d.image.height <= 0) return texIds_[key] = -1;
        return texIds_[key] = renderer_->createTexture(d.image.pixels.data(), d.image.width, d.image.height);
    };
    // The colour an unlit set adds: the particle colour x the emissive graph's RGB x intensity (its pixel shaders read a
    // packed emissive colour and only the alpha of the particle colour, research 139 section 5; INFERRED that the
    // packed colour is the two together, so a fire's orange colour graph stays with its white emissive graph).
    const auto addColour = [](const VfxParticle& p, bool unlit, int k) { return unlit && p.hasEmissive ? p.rgba[k] * p.emissive[k] : p.rgba[k]; };
    const auto materialOf = [&](const std::string& name) -> const ParticleMaterial* {
        if (name.empty() || !particleMaterial_) return nullptr;
        if (const auto it = particleMats_.find(name); it != particleMats_.end()) return &it->second;
        ParticleMaterial pm = particleMaterial_(name);
        if (!pm.ready) {
            moreToLoad_ = true;  // asked again next frame
            return nullptr;
        }
        return &(particleMats_[name] = pm);
    };
    for (const auto& pl : placed) {
        ++seed;
        // Far effects are left out (the game culls them by distance too); the 20,000 particle budget goes to near ones.
        const double dist = length(pl.origin - eye);
        if (dist > 12000) continue;
        const auto def = effectSource_(*pl.fx);
        if (!def || def->emitters.empty()) continue;
        // A one-shot effect plays again every `delay` seconds (the fx_origin's key, as the built map's script repeats it),
        // else after its run plus 1.5 s, so that a short burst keeps showing; a looping effect runs on without a restart
        // (it used to start over every 60 s, every particle vanishing at once).
        const double oneShot = def->duration >= 0 ? std::max(def->duration, 0.05) : -1.0;
        const double period = oneShot > 0 ? (pl.repeat > 0 ? std::max(pl.repeat, oneShot + 0.1) : oneShot + 1.5) : 0.0;
        const double at = period > 0 ? std::fmod(t, period) : t;
        const VfxAxes ax = vfxAxesFromAngles(pl.angles.x, pl.angles.y, pl.angles.z);
        for (const VfxParticle& p : sampleVfx(*def, at, seed, &ax)) {
            const VfxEmitterSim& em = def->emitters[size_t(p.emitter)];
            if (em.fadeOutMaxDistance > 0 && dist > em.fadeOutMaxDistance) continue;
            const bool flare = em.element == 3 && em.flare.present;
            if (em.element != 0 && em.element != 8 && em.element != 10 && !flare) continue;  // models, lights, runners, decals
            const std::string& matName = size_t(p.material) < em.materials.size() ? em.materials[size_t(p.material)] : em.material;
            const ParticleMaterial* pm = materialOf(matName);
            if (pm && pm->skip) continue;
            // A material the cache is still resolving: nothing yet (it used to be a plain square in the meantime).
            // IW8R_PLAIN_PARTICLE_QUADS: the plain quads of before (a sprite or flare without its image), for A/B.
            static const bool plainQuads = qEnvironmentVariableIsSet("IW8R_PLAIN_PARTICLE_QUADS");
            if (!pm && !matName.empty() && particleMaterial_ && !plainQuads) continue;
            if (flare) {
                // A flare piece (research 139 section 1.3): its source on screen, the piece along the line from there
                // through the centre, sized in screen heights. Behind the camera or off the screen: not drawn.
                // A sun flare's source is the sun (research 139 section 1.2, INFERRED: Shipment places its sun flare near
                // the ground with angles 0 0 0): far along the sun's direction from the eye. One placement per sun flare
                // effect draws (each would put its pieces on the same spot). Other flares start at their effect.
                const bool sunFlare = pl.fx->find("sunflare") != std::string::npos || pl.fx->find("sun_flare") != std::string::npos;
                if (sunFlare && p.emitter == 0 && !sunFlaresDrawn.insert(*pl.fx).second) break;
                const Vec3 sunDir = gameLookOn() ? gameSunToward() : ctx_->scene.sunToward;
                const Vec3 src = sunFlare ? eye + sunDir * 1.0e6 : pl.origin + Vec3{p.pos[0], p.pos[1], p.pos[2]};
                double clip[4];
                toClip(src, clip);
                if (clip[3] <= 1e-6) continue;
                const double ndc[2] = {clip[0] / clip[3], clip[1] / clip[3]};
                if (std::fabs(ndc[0]) > 1.0 || std::fabs(ndc[1]) > 1.0) continue;
                const VfxFlareSprite fs = vfxFlareSprite(em, p.size2, ndc, aspect, length(src - eye));
                // Without its image (still read, or none) a flare piece draws nothing: a white screen square before.
                int ftex = pm ? particleTexture(matName, *pm) : -1;
                if (ftex < 0 && plainQuads) ftex = whiteTex_;
                if (ftex < 0) continue;
                // The occlusion test's area: the query's world size x scale, as seen at the source (at least a pixel).
                float area[3] = {0, 0, 0};
                static const bool noOcclusion = qEnvironmentVariableIsSet("IW8R_NO_FLARE_OCCLUSION");  // A/B: always seen
                if (em.occlusion.present && !noOcclusion) {
                    const double half = 0.5 * em.occlusion.scale * std::max(em.occlusion.worldSize[0], em.occlusion.worldSize[1]);
                    double c2[4];
                    toClip(src + right * half, c2);
                    const double dx = c2[3] > 1e-6 ? std::fabs(c2[0] / c2[3] - ndc[0]) : 0.0;
                    const double px = 2.0 / std::max(1, height());
                    area[0] = float(std::max(px / aspect, std::min(dx, 0.25))), area[1] = float(std::max(px, std::min(dx * aspect, 0.25)));
                    area[2] = 1;
                }
                Quad qd; qd.object = pl.object; qd.material = matName;
                qd.depth = -1e30f;  // over everything: last in the far-to-near order
                qd.texture = ftex;
                qd.lit = false, qd.distortion = false, qd.screen = true;
                for (int k = 0; k < 3; ++k) qd.emissive[k] = pm ? pm->emissive[k] : 1.0f;
                qd.distScale[0] = qd.distScale[1] = 0;
                // A dirty lens piece (research 139 section 5): its image is lens grime fixed to the screen, seen through
                // the round slot 145 mask. Drawn without the mask it was a square of grime (the owner's "square-like"
                // glare), and it turned with the source's angle about the centre (m_angularRotCount), so the square
                // spun as a lamp passed the middle of the view.
                // IW8R_OLD_FLARES: the pieces as drawn before (no mask, turned, the particle colour, alpha 1), for A/B.
                static const bool oldFlares = qEnvironmentVariableIsSet("IW8R_OLD_FLARES");
                qd.screenMask = pm && pm->screenMask && !oldFlares;
                qd.mask = qd.screenMask ? maskTexture(pm->maskFile) : -1;
                const double angle = qd.screenMask ? 0.0 : fs.angle;  // a round mask: its turn would show nothing but the grime's
                const double ca = std::cos(angle), sa = std::sin(angle);
                // The occlusion test's depth: the source's, but no farther than just in front of the sky box (drawn 60,000
                // units out, it writes depth): a sun at infinity is hidden by the world, not by the sky.
                double occ[4];
                const double srcDist = length(src - eye);
                toClip(eye + (src - eye) * (std::min(srcDist, 50000.0) / std::max(srcDist, 1e-6)), occ);
                const float srcAux[3] = {float(ndc[0]), float(ndc[1]), float(occ[3] > 1e-6 ? occ[2] / occ[3] : 1.0)};
                const auto fc = [&](double sx, double sy, float u, float v) {
                    // Rotated in screen-height units, then back to NDC x.
                    const double hx = fs.half[0] * aspect * sx, hy = fs.half[1] * sy;
                    app::ParticleVertex pv{float(fs.centre[0] + (hx * ca - hy * sa) / aspect), float(fs.centre[1] + hx * sa + hy * ca), 0.0f,
                                           u, v, addColour(p, !oldFlares, 0), addColour(p, !oldFlares, 1), addColour(p, !oldFlares, 2),
                                           oldFlares ? 1.0f : p.rgba[3]};
                    std::copy(srcAux, srcAux + 3, pv.aux0), std::copy(area, area + 3, pv.aux1);
                    return pv;
                };
                const app::ParticleVertex a = fc(-1, -1, 0, 1), b = fc(1, -1, 1, 1), cc = fc(1, 1, 1, 0), d = fc(-1, 1, 0, 0);
                qd.v[0] = a, qd.v[1] = b, qd.v[2] = cc, qd.v[3] = a, qd.v[4] = cc, qd.v[5] = d;
                quads.push_back(qd);
                ++fxFlares_;
                if (++fxParticles_ >= 20000) break;
                continue;
            }
            const bool distortion = pm && pm->distortion;
            int tex = pm ? particleTexture(matName, *pm) : -1;
            if (distortion && tex < 0) continue;  // its image is still loading: nothing to move the scene by yet
            if (tex == -2 && !plainQuads) continue;  // its image is still being read: nothing yet (a plain square before)
            const bool lit = pm ? pm->lit : true;
            int cols = pm ? std::max(1, pm->cols) : 1, rows = pm ? std::max(1, pm->rows) : 1;
            if (tex < 0 && plainQuads) tex = whiteTex_, cols = rows = 1;
            if (tex < 0) {
                // No image at all (the material failed or has none): a soft round sprite of the particle's colour.
                const std::string softKey = "\x0Asoft";
                if (const auto it = texIds_.find(softKey); it != texIds_.end()) {
                    tex = it->second;
                } else {
                    constexpr int kSide = 64;
                    std::vector<uint8_t> px(size_t(kSide) * kSide * 4);
                    for (int y = 0; y < kSide; ++y)
                        for (int x = 0; x < kSide; ++x) {
                            const double u = (x + 0.5) / kSide - 0.5, v = (y + 0.5) / kSide - 0.5;
                            const double s = std::clamp(1.0 - 2.0 * std::sqrt(u * u + v * v), 0.0, 1.0);
                            const uint8_t a = uint8_t(std::lround(255.0 * s * s));
                            uint8_t* p = &px[(size_t(y) * kSide + size_t(x)) * 4];
                            p[0] = p[1] = p[2] = a, p[3] = a;
                        }
                    tex = texIds_[softKey] = renderer_->createTexture(px.data(), kSide, kSide);
                }
                cols = rows = 1;
                if (tex < 0) continue;
            }
            const Vec3 c = pl.origin + Vec3{p.pos[0], p.pos[1], p.pos[2]};
            const double w = std::max(0.25, p.size2[0] * 0.5), h = std::max(0.25, p.size2[1] * 0.5);
            Vec3 ax0, ax1;  // half extents
            if (em.element == 10) {
                // A tail: long along the velocity seen from the camera, behind the particle.
                const Vec3 vel{p.vel[0], p.vel[1], p.vel[2]};
                const double speed = length(vel);
                Vec3 dir = speed > 1e-6 ? vel - fwd * dot(vel, fwd) : up;
                dir = length(dir) > 1e-6 ? normalize(dir) : up;
                const Vec3 side = normalize(cross(dir, fwd));
                const double len = std::max(h, em.tailScaleWithVelocity ? speed * 0.033 * 0.5 : h);
                ax0 = side * w, ax1 = dir * len;
            } else if (em.element == 8) {
                // An oriented sprite: the quaternion's y and z axes in the effect's frame (INFERRED facing).
                const double* q = em.orientQuat;
                const auto rot = [&](const double v[3], double o[3]) {
                    const double qx = q[0], qy = q[1], qz = q[2], qw = q[3];
                    const double tx = 2 * (qy * v[2] - qz * v[1]), ty = 2 * (qz * v[0] - qx * v[2]), tz = 2 * (qx * v[1] - qy * v[0]);
                    o[0] = v[0] + qw * tx + (qy * tz - qz * ty), o[1] = v[1] + qw * ty + (qz * tx - qx * tz), o[2] = v[2] + qw * tz + (qx * ty - qy * tx);
                };
                const double ly[3] = {0, 1, 0}, lz[3] = {0, 0, 1};
                double ry[3], rz[3];
                rot(ly, ry), rot(lz, rz);
                const auto world = [&](const double l[3]) {
                    return Vec3{ax.x[0] * l[0] + ax.y[0] * l[1] + ax.z[0] * l[2], ax.x[1] * l[0] + ax.y[1] * l[1] + ax.z[1] * l[2],
                                ax.x[2] * l[0] + ax.y[2] * l[1] + ax.z[2] * l[2]};
                };
                const double ca = std::cos(p.angle), sa = std::sin(p.angle);
                const Vec3 a0 = world(ry), a1 = world(rz);
                ax0 = (a0 * ca + a1 * sa) * w, ax1 = (a1 * ca - a0 * sa) * h;
            } else {
                // A billboard, turned by the particle's angle.
                const double ca = std::cos(p.angle), sa = std::sin(p.angle);
                ax0 = (right * ca + up * sa) * w, ax1 = (up * ca - right * sa) * h;
            }
            // The atlas cell, mirrored as the particle says.
            const int frames = cols * rows;
            const int f = vfxAtlasFrame(em, p, frames);
            const int col = f % cols, row = f / cols;
            float u0 = float(col) / float(cols), u1 = float(col + 1) / float(cols);
            float v0 = float(row) / float(rows), v1 = float(row + 1) / float(rows);
            if (p.flipU) std::swap(u0, u1);
            if (p.flipV) std::swap(v0, v1);
            Quad qd; qd.object = pl.object; qd.material = matName;
            qd.depth = float(dot(c - eye, fwd));
            qd.texture = tex;
            qd.lit = lit && !distortion;
            qd.distortion = distortion;
            qd.screen = false;
            for (int k = 0; k < 3; ++k) qd.emissive[k] = pm ? pm->emissive[k] : 1.0f;
            qd.distScale[0] = distortion ? pm->distScale[0] : 0.0f, qd.distScale[1] = distortion ? pm->distScale[1] : 0.0f;
            // aux0 / aux1: the centre and the grid's light of a sprite, the tangent frame of a distortion quad.
            float aux0[3] = {float(c.x), float(c.y), float(c.z)}, aux1[3] = {-1, -1, -1};
            if (distortion) {
                const Vec3 T = length(ax0) > 1e-9 ? normalize(ax0) : right, B = length(ax1) > 1e-9 ? normalize(ax1) : up;
                aux0[0] = float(T.x), aux0[1] = float(T.y), aux0[2] = float(T.z);
                aux1[0] = float(B.x), aux1[1] = float(B.y), aux1[2] = float(B.z);
                ++fxDistortion_;
                fxDistAlphaMax_ = std::max(fxDistAlphaMax_, double(p.rgba[3]));
                fxDistAlphaSum_ += p.rgba[3];
            } else if (qd.lit && grid) {
                const double at[3] = {c.x, c.y, c.z};
                StockLgvSh sh;
                if (sampleStockGpuGrid(*grid, at, sh)) {
                    double rgb[3];
                    stockLgvAverage(sh, rgb);
                    for (int k = 0; k < 3; ++k) aux1[k] = float(rgb[k] * gridScale);
                    ++fxGridLit_;
                }
            }
            const auto corner = [&](double sx, double sy, float u, float v) {
                const Vec3 q = c + ax0 * sx + ax1 * sy;
                const bool unlit = !qd.lit && !distortion;
                app::ParticleVertex pv{float(q.x), float(q.y), float(q.z), u, v, addColour(p, unlit, 0), addColour(p, unlit, 1), addColour(p, unlit, 2),
                                       p.rgba[3]};
                std::copy(aux0, aux0 + 3, pv.aux0), std::copy(aux1, aux1 + 3, pv.aux1);
                return pv;
            };
            const app::ParticleVertex a = corner(-1, -1, u0, v1), b = corner(1, -1, u1, v1), cc = corner(1, 1, u1, v0), d = corner(-1, 1, u0, v0);
            qd.v[0] = a, qd.v[1] = b, qd.v[2] = cc, qd.v[3] = a, qd.v[4] = cc, qd.v[5] = d;
            quads.push_back(qd);
            if (++fxParticles_ >= 20000) break;
        }
        if (fxParticles_ >= 20000) break;
    }
    // Far to near, then runs of one sprite and kind become one batch (the premultiplied blend keeps the order right).
    std::stable_sort(quads.begin(), quads.end(), [](const Quad& x, const Quad& y) { return x.depth > y.depth; });
    std::vector<app::ParticleBatch> batches;
    for (const Quad& q : quads) {
        if (batches.empty() || batches.back().previewObject != q.object || batches.back().previewMaterial != q.material || batches.back().texture != q.texture || batches.back().lit != q.lit || batches.back().distortion != q.distortion ||
            batches.back().screen != q.screen || batches.back().screenMask != q.screenMask || batches.back().mask != q.mask) {
            batches.emplace_back();
            batches.back().previewObject = q.object; batches.back().previewMaterial = q.material;
            batches.back().texture = q.texture, batches.back().lit = q.lit, batches.back().distortion = q.distortion;
            batches.back().screen = q.screen;
            batches.back().screenMask = q.screenMask, batches.back().mask = q.mask;
            std::copy(q.emissive, q.emissive + 3, batches.back().emissive);
            std::copy(q.distScale, q.distScale + 2, batches.back().distScale);
        }
        batches.back().v.insert(batches.back().v.end(), q.v, q.v + 6);
    }
    renderer_->setParticles(std::move(batches));
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
    // The capture's face size (kIw8ProbeCaptureSize since research 123; a 64-texel capture of an older build still shows).
    int side = 0;
    if (best && readFile(best->rgba.toStdString(), bytes))
        for (const int candidate : {kIw8ProbeCaptureSize, 64})
            if (bytes.size() == size_t(6) * size_t(candidate) * size_t(candidate) * 4) side = candidate;
    if (side > 64) {
        // The preview's shader blurs by levels of a 64-texel cube (the game's rule for its probes): halve the capture.
        std::string half(size_t(6) * 64 * 64 * 4, '\0');
        const int step = side / 64;
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    for (int c = 0; c < 4; ++c) {
                        unsigned sum = 0;
                        for (int sy = 0; sy < step; ++sy)
                            for (int sx = 0; sx < step; ++sx)
                                sum += uint8_t(bytes[((size_t(f) * size_t(side) + size_t(y * step + sy)) * size_t(side) + size_t(x * step + sx)) * 4 + size_t(c)]);
                        half[((size_t(f) * 64 + size_t(y)) * 64 + size_t(x)) * 4 + size_t(c)] = char((sum + unsigned(step * step) / 2) / unsigned(step * step));
                    }
        bytes.swap(half);
        side = 64;
    }
    if (side > 0)
        renderer_->setReflectionProbe(reinterpret_cast<const uint8_t*>(bytes.data()), side);
    else
        renderer_->setReflectionProbe(nullptr, 0);
}

void CameraView::setPreviewProgram(std::shared_ptr<app::PreviewGpuProgram> program, app::PreviewOverrideSet targets) {
    std::lock_guard lock(gpuMutex());
    if(ctx_) {
        if(program&&!livePreview_){previewPreviousGameLook_=ctx_->scene.gameLook;previewPreviousTextured_=ctx_->texturedView;}
        if(program){ctx_->scene.gameLook=true;ctx_->texturedView=true;}
        else if(livePreview_){ctx_->scene.gameLook=previewPreviousGameLook_;ctx_->texturedView=previewPreviousTextured_;}
    }
    livePreview_ = std::move(program);
    renderer_->setPreviewProgram(livePreview_, std::move(targets));
    renderer_->setPreviewMaterialNames(refTexMaterials_, texInstMaterials_);
    if (ctx_) { ctx_->scene.previewIdentity = bool(livePreview_); ctx_->scene.invalidate(); }
    if (!livePreviewTimer_) {
        livePreviewTimer_ = new QTimer(this);
        livePreviewTimer_->setInterval(33);
        connect(livePreviewTimer_, &QTimer::timeout, this, [this] {
            std::lock_guard lock(gpuMutex());
            renderer_->setPreviewMaterialNames(refTexMaterials_, texInstMaterials_);
            update();
        });
    }
    if (livePreview_) livePreviewTimer_->start(); else livePreviewTimer_->stop();
    update();
}
void CameraView::updatePreviewParameters(const std::vector<ShaderParameter>& parameters) {
    std::lock_guard lock(gpuMutex());
    if (livePreview_) livePreview_->parameters(parameters);
    update();
}
app::PreviewOverrideSet CameraView::selectedPreviewTargets(const std::string& material) const {
    app::PreviewOverrideSet targets; targets.enabled = true; targets.material = material;
    if (ctx_) for (const auto& r : ctx_->doc.selection()) {
        targets.targets.push_back({app::PreviewFamily::Geometry, r.entity, r.item, material});
        targets.targets.push_back({app::PreviewFamily::Particle, r.entity, 0, material});
    }
    return targets;
}

int CameraView::customShaderFor(const std::string& material) {
    if (!shaderLookup_ || !rendererOk_) return -1;
    const std::shared_ptr<const CustomShader> shader = shaderLookup_(material);
    if (!shader || !customShaderHasSurface(shader->code)) return -1;  // a filter alone lights nothing itself
    const std::string key = shader->path + '\n' + shader->code;
    if (const auto it = customShaderIds_.find(key); it != customShaderIds_.end()) return it->second;
    std::string err;
    const std::string src = customShaderViewportSource(app::SceneRenderer::texShaderSource(), "float4 ps_tex(PSIn i) : SV_TARGET {", *shader);
    const int id = src.empty() ? -1 : renderer_->createCustomShader(src, &err);
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
    if (dec.blocksOk) id = renderer_->createTextureBlocks(dec.blocks.dxgiFormat, dec.blocks.width, dec.blocks.height, dec.blocks.levels);
    if (id < 0 && dec.imageOk) id = renderer_->createTexture(dec.image.pixels.data(), dec.image.width, dec.image.height);
    if (id < 0 && dec.blocksOk && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_->createTexture(img.pixels.data(), img.width, img.height);
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
    if (decodeImageFile(path.toStdString(), w, h, px, &err)) id = renderer_->createTexture(px.data(), w, h);
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
    // Only once the colour image is up: the material is then known to the cache, and colour loads go first. A model
    // layer's key ("<material>\x01<L>") only waits for that layer's colour lookup (a mode 3 layer has no colour image).
    if (auto t = texIds_.find(material);
        t == texIds_.end() || (material.find('\x01') == std::string::npos && (t->second < 0 || t->second == checkerTex_)))
        return -1;
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
            id = renderer_->createTexture(px.data(), w, h);
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
    if (dec.blocksOk) id = renderer_->createTextureBlocks(dec.blocks.dxgiFormat, dec.blocks.width, dec.blocks.height, dec.blocks.levels);
    if (id < 0 && dec.imageOk) id = renderer_->createTexture(dec.image.pixels.data(), dec.image.width, dec.image.height);
    if (id < 0 && dec.blocksOk && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_->createTexture(img.pixels.data(), img.width, img.height);
    g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
    if (id >= 0) nrmByPath_[path.toStdString()] = id;
    return nrmIds_[material] = id;
}

QString CameraView::geometrySelfCheck() {
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
    std::vector<app::GpuVertex> t1, l1, o1, t2, l2, o2;
    std::vector<app::TexturedBatch> x1, x2;
    geo_.flatten(t1, l1, o1, x1);
    const size_t shadowOnly = ctx_->scene.shadowOnlyVertices, casters = ctx_->scene.shadowCasterVertices;
    if (ctx_->texturedView && imageLookup_)
        app::buildCameraGeometry(ctx_->scene, ctx_->doc, ctx_->showSpawns && !ctx_->playing, t2, l2, o2, [this](const std::string& m) { return textureFor(m); }, x2);
    else
        app::buildCameraGeometry(ctx_->scene, ctx_->doc, ctx_->showSpawns && !ctx_->playing, t2, l2, o2);
    const auto same = [](const std::vector<app::GpuVertex>& a, const std::vector<app::GpuVertex>& b) {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(app::GpuVertex)) == 0);
    };
    if (!same(t1, t2)) {
        QString details;
        for (const auto* g : ctx_->scene.items())
            if (ctx_->doc.isSelected(g->ref) && !g->polyMaterial.empty()) {
                const auto& m = g->polyMaterial.front();
                details += QString("; selected material %1, texture %2").arg(QString::fromStdString(m)).arg(textureFor(m));
            }
        return QString("flat triangles differ (%1 / %2 vertices, textured batches %3 / %4, waiting %5)%6")
            .arg(t1.size()).arg(t2.size()).arg(x1.size()).arg(x2.size()).arg(texturesWaiting()).arg(details);
    }
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
    renderNow();  // drawn here, not by the render thread: the pixels are counted at once
    return renderer_->countNonClearPixels(kClear);
}

void CameraView::skyDecoded(const QString& key, const SkyFaces& r) {
    if (key != skyPath_) return;  // the sky changed again meanwhile: a newer decode is under way
    skyDecoding_ = false;
    if (r.ok) {
        skyExposure_ = r.info.hdr ? r.info.exposure : 0.0f;
        skyRgbmRange_ = r.info.rgbm ? r.options.rgbmRange : 1.0f;
        skyMeanLum_ = r.info.meanLuminance, skyHighLum_ = r.info.highLuminance;
        for (size_t f = 0; f < r.faces.size(); ++f) {
            skyTex_.push_back(renderer_->createTexture(r.faces[f].data(), r.size, r.size));
            if (r.info.rgbm) renderer_->setTextureRawAlpha(skyTex_.back(), true);
            // Diagnosis: IW8R_DUMP_SKY=<folder> writes the six preview faces as PNG.
            if (const QString dump = qEnvironmentVariable("IW8R_DUMP_SKY"); !dump.isEmpty())
                QImage(r.faces[f].data(), r.size, r.size, QImage::Format_RGBA8888).save(QDir(dump).filePath(QString("sky_face%1.png").arg(f)));
        }
    }
    geomDirty_ = true;
    update();
}

QImage CameraView::renderImage(int w, int h, bool clean, double aspect, int settleMs, bool keepGeometry) {
    if (!rendererOk_ || w < 1 || h < 1 || w > 16384 || h > 16384) return {};
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
    captureW_ = w, captureH_ = h, captureAspect_ = aspect, cleanCapture_ = clean;
    const bool remake = clean || !keepGeometry;
    if (remake) geomDirty_ = true;
    QElapsedTimer t;
    t.start();
    render();
    // A sky being decoded on its worker thread is waited for: a capture shows the sky it was taken with.
    while (skyDecoding_ && t.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        render();
    }
    while ((moreToLoad_ || refMoreToLoad_) && t.elapsed() < settleMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        geomDirty_ = true;
        render();
    }
    std::vector<uint8_t> px;
    int rw = 0, rh = 0;
    const bool ok = renderer_->readback(px, rw, rh);
    captureW_ = captureH_ = 0, captureAspect_ = 0, cleanCapture_ = false;
    if (remake) geomDirty_ = true;
    update();
    if (!ok || rw != w || rh != h) return {};
    QImage img(px.data(), w, h, w * 4, QImage::Format_RGBA8888);
    return img.convertToFormat(QImage::Format_RGB888);  // opaque: the clear colour's alpha is not meaningful
}

int CameraView::texturesWaiting() const {
    int n = moreToLoad_ || refMoreToLoad_ ? 1 : 0;
    if (imageLookup_)
        for (const std::string& m : requested_)
            if (!texIds_.count(m) && !imageLookup_(m)) ++n;
    return n;
}

QImage CameraView::grabFrame() {
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
    if (prepared_.valid && !prepared_.drawn) drawPrepared(prepared_.cam);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!renderer_->readback(px, w, h)) return {};
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
        renderNow();  // read back at once
        if (!renderer_->readback(frames[size_t(f)], fw, fh) || fw <= 0 || fh <= 0) {
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
                uint8_t* o = &out[((size_t(f) * size_t(size) + size_t(y)) * size_t(size) + size_t(x)) * 4];
                o[3] = 255;
                // The mean of four samples inside the texel (the frame has several pixels per texel: one sample
                // made sparkling reflections).
                unsigned sum[3] = {0, 0, 0}, taken = 0;
                for (int sub = 0; sub < 4; ++sub) {
                    const double u = 2.0 * (x + (sub & 1 ? 0.75 : 0.25)) / size - 1.0, v = 2.0 * (y + (sub & 2 ? 0.75 : 0.25)) / size - 1.0;
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
                    if (cw <= 1e-6) continue;
                    const int px = std::clamp(int((cx / cw * 0.5 + 0.5) * fw), 0, fw - 1);
                    const int py = std::clamp(int((0.5 - cy / cw * 0.5) * fh), 0, fh - 1);
                    const uint8_t* s = &frames[size_t(f)][(size_t(py) * size_t(fw) + size_t(px)) * 4];
                    sum[0] += s[0], sum[1] += s[1], sum[2] += s[2];
                    ++taken;
                }
                if (!taken) continue;
                o[0] = uint8_t((sum[0] + taken / 2) / taken);
                o[1] = uint8_t((sum[1] + taken / 2) / taken);
                o[2] = uint8_t((sum[2] + taken / 2) / taken);
            }
    }
    return out;
}

void CameraView::paintEvent(QPaintEvent*) { render(); }

void CameraView::resizeEvent(QResizeEvent*) { update(); }

void CameraView::copyHooksFrom(const CameraView& o) {
    shaderLookup_ = o.shaderLookup_;
    layerLookup_ = o.layerLookup_;
    factsLookup_ = o.factsLookup_;
    maskLookup_ = o.maskLookup_;
    normalLookup_ = o.normalLookup_;
    emissiveLookup_ = o.emissiveLookup_;
    specLookup_ = o.specLookup_;
    stateLookup_ = o.stateLookup_;
    effectSource_ = o.effectSource_;
    particleMaterial_ = o.particleMaterial_;
    toolPathFn_ = o.toolPathFn_;
    customTint_ = o.customTint_;
    imageLookup_ = o.imageLookup_;
    imageRequest_ = o.imageRequest_;
    materialImagesChanged();
}

void CameraView::setOrbit(const Vec3& target, double distance, double yawDeg, double pitchDeg) {
    orbitTarget_ = orbitHomeTarget_ = target;
    orbitDist_ = orbitHomeDist_ = std::max(1.0, distance);
    orbitYaw_ = orbitHomeYaw_ = yawDeg;
    orbitPitch_ = orbitHomePitch_ = std::clamp(pitchDeg, -89.0, 89.0);
    applyOrbit();
}

void CameraView::applyOrbit() {
    ctx_->cam.yaw = orbitYaw_;
    ctx_->cam.pitch = orbitPitch_;
    ctx_->cam.pos = orbitTarget_ - ctx_->cam.forward() * orbitDist_;
    ctx_->cameraMoved();
    update();
}

void CameraView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    if (orbit_) {
        orbitLast_ = e->position().toPoint();
        return;
    }
    if (ctx_->playing) return;  // the mouse only looks around while playing
    if (e->button() == Qt::RightButton) {
        // Mid-drag the right button would turn the view under the drag and open the menu on release.
        if (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0) return;
        clearGizmoHover();
        ctx_->looking = true;
        lookTravel_ = 0;
        lmbDown_ = false;  // the view turns: a left release now would select whatever is under the cursor then
        lockPos_ = e->position().toPoint();
        if (POINT cp{}; GetCursorPos(&cp)) lookLockX_ = int(cp.x), lookLockY_ = int(cp.y);
        setCursor(Qt::BlankCursor);
    } else if (e->button() == Qt::LeftButton) {
        if (ctx_->looking) return;  // a left click while right-drag looking is not a select
        lmbDown_ = true;
        pressPos_ = e->position().toPoint();
        const QPointF p = e->position();
        const bool grab = !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier));
        // The gizmo part within 8 px (scale boxes, then arrows, then rings; each mode shows its own parts).
        int partAxis = -1;
        const int part = grab ? gizmoPartAt(p, partAxis) : -1;
        // A rotate ring: turn about its axis (the world's, or the entity's own in local space).
        if (part == 1) {
            const int ring = partAxis;
            ringPivot_ = ringCentre();
            ringDragAxis_ = ring;
            ringStartAngle_ = ringAngleForMouse(p, ring).value_or(0);
            ringDeltaDeg_ = 0;
            lmbDown_ = false;
            update();
            return;
        }
        // A move arrow or a scale box: dragged along its axis.
        if (part == 0 || part == 2) {
            const int best = partAxis;
            gizmoDragAxis_ = best;
            dragScale_ = part == 2;
            gizmoStartPivot_ = gizmoCentre();
            gizmoStartT_ = axisParamForMouse(p, best);
            lmbDown_ = false;  // not a click-select
            if (dragScale_) {
                scaleFactor_ = 1.0;
                scaleStartBounds_ = ctx_->doc.selectionBounds();  // drawn scaled while the handle moves
            } else {
                // A move along a world axis (or a local axis lying along one) lands the selection on the grid (Snap To
                // Grid's reference: the primitives' bounds minimum, else the lowest point entity origin), so an off-grid
                // object snaps on. A turned local axis moves in whole grid steps instead.
                const Vec3 dir = gizmoAxisDir(best);
                gizmoSnapAxis_ = -1;
                for (int k = 0; k < 3; ++k)
                    if (std::fabs(dir[k]) > 1.0 - 1e-9) gizmoSnapAxis_ = k;
                const int k = gizmoSnapAxis_ >= 0 ? gizmoSnapAxis_ : best;
                AABB prims;
                std::optional<double> origin;
                for (const SelRef& r : ctx_->doc.selection()) {
                    if (r.item == kNoId) {
                        if (const Entity* en = ctx_->doc.findEntity(r.entity))
                            if (auto o = en->origin()) origin = std::min(origin.value_or((*o)[k]), (*o)[k]);
                    } else if (const EntityItem* it = ctx_->doc.findItem(r.entity, r.item); it && it->prim()) {
                        prims.add(it->prim()->bounds());
                    }
                }
                gizmoSnapFrom_ = prims.valid() ? prims.mins[k] : origin.value_or(gizmoStartPivot_[k]);
                gizmoMoveBy_ = 0;
                ctx_->doc.beginDrag();
            }
            update();
        }
    }
}

void CameraView::mouseMoveEvent(QMouseEvent* e) {
    if (orbit_) {
        const QPoint p = e->position().toPoint();
        const QPoint d = p - orbitLast_;
        orbitLast_ = p;
        const bool pan = (e->buttons() & Qt::MiddleButton) || ((e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ShiftModifier));
        if (pan) {
            const double k = orbitDist_ * 0.0018;
            orbitTarget_ = orbitTarget_ - ctx_->cam.right() * (d.x() * k) + ctx_->cam.up() * (d.y() * k);
            applyOrbit();
        } else if (e->buttons() & (Qt::LeftButton | Qt::RightButton)) {
            orbitYaw_ = std::fmod(orbitYaw_ - d.x() * 0.4, 360.0);
            orbitPitch_ = std::clamp(orbitPitch_ - d.y() * 0.4, -89.0, 89.0);
            applyOrbit();
        }
        return;
    }
    if (ringDragAxis_ >= 0) {
        if (auto a = ringAngleForMouse(e->position(), ringDragAxis_)) {
            double d = std::remainder(*a - ringStartAngle_, 360.0);
            d = ctx_->snap ? std::round(d / 15.0) * 15.0 : d;  // Radiant rotation snap
            ringDeltaDeg_ = d;
            // Drag progress goes to the status bar only (a Console line per mouse move would flood the log).
            Vec3 frame[3];
            ctx_->setTransientStatus(QString("Rotate %1%2 %3 degrees").arg(gizmoLocalFrame(frame) ? "local " : "").arg(QChar("XYZ"[ringDragAxis_])).arg(d));
            update();
        }
        return;
    }
    if (gizmoDragAxis_ >= 0 && dragScale_) {
        // Factor from how far the box moved relative to its distance from the centre; snaps to 0.05 steps.
        const double t = axisParamForMouse(e->position(), gizmoDragAxis_);
        double f = std::max(0.05, 1.0 + (t - gizmoStartT_) / std::max(1.0, scaleBoxDistance()));
        if (ctx_->snap) f = std::max(0.05, std::round(f * 20.0) / 20.0);
        scaleFactor_ = f;
        ctx_->setTransientStatus(QString("Scale %1 x%2").arg(QChar("XYZ"[gizmoDragAxis_])).arg(f, 0, 'f', 2));
        update();
        return;
    }
    if (gizmoDragAxis_ >= 0) {
        const int axis = gizmoDragAxis_;
        const double raw = axisParamForMouse(e->position(), axis) - gizmoStartT_;
        const Vec3 dir = gizmoAxisDir(axis);  // world axes: along X/Y/Z; local: along the entity's axis
        const double g = ctx_->doc.grid();
        double s = raw;
        if (ctx_->snap && g > 0) {
            // Along a world axis (or a local axis lying along one): snap where the selection ends up; a turned local
            // axis keeps whole grid steps of travel.
            if (gizmoSnapAxis_ >= 0) {
                const double sign = dir[gizmoSnapAxis_] > 0 ? 1.0 : -1.0;
                s = sign * (std::round((gizmoSnapFrom_ + sign * raw) / g) * g - gizmoSnapFrom_);
            } else {
                s = snapToGrid(raw, g);
            }
        }
        gizmoMoveBy_ = s;
        ctx_->doc.updateDrag(dir * s);
        ctx_->documentEdited();
        Vec3 frame[3];
        ctx_->setTransientStatus(QString("Move %1%2 %3").arg(gizmoLocalFrame(frame) ? "local " : "").arg(QChar("XYZ"[axis])).arg(QString::fromStdString(formatNumber(s))));
        return;
    }
    if (!ctx_->looking) {
        if (e->buttons() == Qt::NoButton) updateGizmoHover(e->position(), e->modifiers());
        return;
    }
    if (const unsigned long drop = dropMouseBefore_.load(); drop && e->timestamp() <= drop) return;  // applied already
    const QPoint p = e->position().toPoint();
    const QPoint d = p - lockPos_;
    if (d.isNull()) return;
    lookTravel_ += std::abs(d.x()) + std::abs(d.y());
    auto& cam = ctx_->cam;
    if (ctx_->playing) {
        // The game's mouse look (CL_InputMP_ApplyLookInputToCmd, research 140): m_yaw / m_pitch 0.022 x the default
        // mouse sensitivity 12 x 0.3 = 0.0792 degrees per count (a physical pixel of cursor travel at the default
        // Windows pointer speed), pitch held to +-85 (PM_ClampViewPitch). Ctrl is prone here, not the stock drag.
        const double k = mouseDegreesPerCount() * devicePixelRatioF();
        cam.yaw -= d.x() * k;
        cam.pitch = std::clamp(cam.pitch - d.y() * k, -params_.pitchLimit, params_.pitchLimit);
    } else if ((e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)) {
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
    if (orbit_) return;
    if (ctx_->playing) return;
    // The drags log one Console line here, at the end (their moves only update the status bar).
    if (e->button() == Qt::LeftButton && ringDragAxis_ >= 0) {
        const int axis = ringDragAxis_;
        ringDragAxis_ = -1;
        Vec3 frame[3];
        const bool local = gizmoLocalFrame(frame);
        const bool done = ringDeltaDeg_ != 0 && rotateSelectionByGizmo(axis, ringDeltaDeg_, ringPivot_);
        ctx_->documentEdited(done ? QString("Rotated %1 degrees about %2%3 (undoable)").arg(ringDeltaDeg_).arg(local ? "local " : "").arg(QChar("XYZ"[axis]))
                                  : QString());
        ringDeltaDeg_ = 0;
        updateGizmoHover(e->position(), e->modifiers());
        return;
    }
    if (e->button() == Qt::LeftButton && gizmoDragAxis_ >= 0 && dragScale_) {
        const int axis = gizmoDragAxis_;
        gizmoDragAxis_ = -1;
        dragScale_ = false;
        const double f = scaleFactor_;
        scaleFactor_ = 1.0;
        const bool done = scaleSelectionAbout(*ctx_, axis, f, gizmoStartPivot_);
        ctx_->documentEdited(done ? QString("Scaled %1 x%2 (models: modelscale; undoable)").arg(QChar("XYZ"[axis])).arg(f, 0, 'f', 2) : QString());
        updateGizmoHover(e->position(), e->modifiers());
        return;
    }
    if (e->button() == Qt::LeftButton && gizmoDragAxis_ >= 0) {
        const int axis = gizmoDragAxis_;
        gizmoDragAxis_ = -1;
        const bool moved = ctx_->doc.endDrag(true);
        ctx_->documentEdited(moved ? QString("Moved %1 %2 with the gizmo (undoable)").arg(QChar("XYZ"[axis])).arg(QString::fromStdString(formatNumber(gizmoMoveBy_)))
                                   : QString());
        updateGizmoHover(e->position(), e->modifiers());
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (!ctx_->looking) return;  // its press was ignored (mid-drag), or the look already ended: no menu
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
            const size_t before = ctx_->doc.faceSelection().size();
            ctx_->doc.toggleFace(*face);
            const size_t after = ctx_->doc.faceSelection().size();
            const EntityItem* it = ctx_->doc.findItem(face->entity, face->item);
            const Primitive* p = it ? it->prim() : nullptr;
            ctx_->documentEdited(QString("Face %1; %2 face(s) selected%3")
                                     .arg(after >= before ? "selected" : "deselected")
                                     .arg(after)
                                     .arg(p && face->face < p->faces.size() ? " - " + QString::fromStdString(p->faces[face->face].material()) : QString()));
        } else {
            ctx_->setStatus("No brush face under the cursor (Ctrl+Shift+click picks faces of brushes only)");
        }
        return;
    }
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    const bool cycleKey = mods == (Qt::ShiftModifier | Qt::AltModifier);
    if (mods == Qt::NoModifier || cycleKey) {
        // Overlapping objects (owner 2026-11-15): a click on the same spot again (within 4 px, the same objects under
        // the cursor) selects the next one behind, round robin; Shift+Alt+click always steps to the one after the
        // selection. A brush of an entity selects the whole entity, so an entity is one stop however many of its
        // brushes the ray crosses.
        std::vector<SelRef> hits;
        for (const SelRef& h : ctx_->scene.pickAll(ray)) {
            const std::vector<SelRef> unit = ctx_->doc.selectionUnit(h);
            const bool seen = std::any_of(hits.begin(), hits.end(), [&](const SelRef& o) {
                return std::find(unit.begin(), unit.end(), o) != unit.end();
            });
            if (!seen) hits.push_back(h);
        }
        if (hits.empty()) {
            cycleHits_.clear();
            if (mods == Qt::NoModifier) ctx_->doc.deselectAll();
            ctx_->documentEdited();
            // A stock map: its own surface under the cursor (the material in the status bar and the Stock Map panel).
            if (!(mods == Qt::NoModifier && stockSurfacePicker_ && stockSurfacePicker_(ray)) && !stockPickLines_.empty())
                setStockPickOutline({});
            return;
        }
        const bool again = (p - cyclePos_).manhattanLength() <= 4 && hits == cycleHits_;
        size_t index = 0;
        if (again) {
            index = (cycleIndex_ + 1) % hits.size();
        } else if (cycleKey) {
            for (size_t i = 0; i < hits.size(); ++i)
                if (ctx_->doc.isSelected(hits[i])) index = (i + 1) % hits.size();
        }
        cyclePos_ = p, cycleHits_ = hits, cycleIndex_ = index;
        if (!stockPickLines_.empty()) setStockPickOutline({});
        ctx_->doc.select(hits[index], false);
        if (hits.size() > 1)
            ctx_->documentEdited(QString("Selected %1 of %2 overlapping objects under the cursor (click the same spot again for the next)")
                                     .arg(index + 1)
                                     .arg(hits.size()));
        else
            ctx_->documentEdited();
        return;
    }
    cycleHits_.clear();
    if (auto hit = ctx_->scene.pick(ray)) {
        if (e->modifiers() & Qt::ShiftModifier) ctx_->doc.toggle(*hit);
        else ctx_->doc.select(*hit, false);
    } else if (!(e->modifiers() & Qt::ShiftModifier)) {
        ctx_->doc.deselectAll();
    }
    ctx_->documentEdited();
}

void CameraView::mouseDoubleClickEvent(QMouseEvent* e) {
    // A double-click on a stock map's prop (nothing of the document under the cursor) makes it a model of the document.
    if (e->button() == Qt::LeftButton && !orbit_ && !ctx_->playing && stockSurfaceEditor_ && !(e->modifiers() & ~Qt::KeypadModifier)) {
        const QPoint p = e->position().toPoint();
        const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
        const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
        ctx_->scene.sync(ctx_->doc);
        if (ctx_->scene.pickAll(ray).empty() && stockSurfaceEditor_(ray)) {
            setStockPickOutline({});
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CameraView::wheelEvent(QWheelEvent* e) {
    if (orbit_) {
        orbitDist_ = std::clamp(orbitDist_ * std::pow(0.87, e->angleDelta().y() / 120.0), 1.0, 1e6);
        applyOrbit();
        return;
    }
    if (ctx_->playing) return;  // the player's eye is the camera while playing
    const double step = e->angleDelta().y() / 120.0 * ctx_->cam.speed * 0.25;
    if (step == 0) return;
    ctx_->cam.pos += ctx_->cam.forward() * step;
    ctx_->cameraMoved();
    ctx_->setTransientStatus(QString("Camera %1 %2 units (mouse wheel; the step follows the camera speed)")
                                 .arg(step > 0 ? "forward" : "back")
                                 .arg(QString::number(std::round(std::fabs(step) * 10) / 10)));
}

bool CameraView::event(QEvent* e) {
    if (orbit_ && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    // Playing: the game's keys (with Ctrl held for prone: Ctrl+W, Ctrl+S ...) reach the play input, not the window's
    // shortcuts; other combinations (Ctrl+Z undo) still work.
    if (ctx_->playing && e->type() == QEvent::ShortcutOverride) {
        const int k = static_cast<QKeyEvent*>(e)->key();
        if (playKeyForQt(k) != PlayKey::Count || k == Qt::Key_Escape || k == Qt::Key_P || k == Qt::Key_N) {
            e->accept();
            return true;
        }
    }
    // Escape during a gizmo or ring drag cancels the drag here, before the window's Escape (deselect) shortcut runs.
    if (e->type() == QEvent::ShortcutOverride && (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0))
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            e->accept();
            return true;
        }
    return QWidget::event(e);
}

void CameraView::cancelViewDrag() {
    if (ringDragAxis_ >= 0) {
        ringDragAxis_ = -1;
        ringDeltaDeg_ = 0;
        ctx_->setStatus("Rotation cancelled");
        update();
    }
    if (gizmoDragAxis_ >= 0) {
        const bool scaling = dragScale_;
        gizmoDragAxis_ = -1;
        dragScale_ = false;
        scaleFactor_ = 1.0;
        if (!scaling) ctx_->doc.endDrag(false);  // the translate drag moved a snapshot: put it back
        ctx_->documentEdited(scaling ? QStringLiteral("Scale cancelled") : QStringLiteral("Move cancelled"));
    }
    lmbDown_ = false;
}

void CameraView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0)) {
        cancelViewDrag();
        return;
    }
    if (orbit_) {
        if (e->key() == Qt::Key_F) emit orbitFrameRequested();
        else if (e->key() == Qt::Key_R) setOrbit(orbitHomeTarget_, orbitHomeDist_, orbitHomeYaw_, orbitHomePitch_);
        return;
    }
    if (ctx_->playing) {
        if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_P) {
            stopPlay();
            return;
        }
        if (e->key() == Qt::Key_N && !e->isAutoRepeat()) {
            player_.noclip = !player_.noclip;
            if (!player_.noclip) placePlayer(world_, player_, player_.origin, params_);
            playControls_.stance = PlayStance::Stand;  // placePlayer stands the player up
            ctx_->setStatus(player_.noclip ? "PLAY: noclip on" : "PLAY: noclip off");
            return;
        }
        if (!e->isAutoRepeat()) {
            ctx_->heldKeys.insert(e->key());
            playControls_.press(playKeyForQt(e->key()));  // a tap shorter than a frame still counts
        }
        return;
    }
    if (!e->isAutoRepeat() && isCameraKey(e->key()) && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        ctx_->heldKeys.insert(e->key());
        // W S Q E only fly while the right button is held (without it A and D are the stock pitch and up keys).
        const int k = e->key();
        if (!ctx_->looking && (k == Qt::Key_W || k == Qt::Key_S || k == Qt::Key_Q || k == Qt::Key_E))
            ctx_->setTransientStatus("Hold the right mouse button to fly with W A S D");
        return;
    }
    QWidget::keyPressEvent(e);
}

void CameraView::keyReleaseEvent(QKeyEvent* e) {
    if (!e->isAutoRepeat()) ctx_->heldKeys.remove(e->key());
    QWidget::keyReleaseEvent(e);
}

void CameraView::focusOutEvent(QFocusEvent* e) {
    // A drag whose release may never arrive (a dialog opened, the window lost focus): back to where it started.
    if (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0) cancelViewDrag();
    ctx_->heldKeys.clear();
    if (ctx_->playing) stopPlay();
    ctx_->looking = false;
    clearGizmoHover();
    unsetCursor();
    QWidget::focusOutEvent(e);
}

void CameraView::leaveEvent(QEvent* e) {
    clearGizmoHover();  // no highlighted handle or move cursor left behind
    QWidget::leaveEvent(e);
}

// IW8R_BEHAVIOUR_TIME=<seconds> (test hook): the Behaviour Preview held at that time (-1 = not set).
static double behaviourTimeHook() {
    static const double t = [] {
        bool ok = false;
        const double v = qEnvironmentVariable("IW8R_BEHAVIOUR_TIME").toDouble(&ok);
        return ok && v >= 0 ? v : -1.0;
    }();
    return t;
}

void CameraView::timerEvent(QTimerEvent* e) {
    if (e->timerId() == windTimer_.timerId()) {
        if (ctx_->behaviourPreview) {
            // Moving models are rebuilt at their pose for this moment.
            ctx_->scene.behaviourTime = behaviourTimeHook() >= 0 ? behaviourTimeHook()
                                        : windClock_.isValid() ? windClock_.elapsed() / 1000.0 : 0.0;
            updateBehaviourScript();
            ctx_->scene.invalidate();
        }
        update();  // the sway is a shader uniform: no geometry rebuild
        return;
    }
    QWidget::timerEvent(e);
}

// The map's own GSC (worldspawn iw8r_script, core gsc_emulate) at the Behaviour Preview's time: where it has the
// entities it moves, turns and hides. What it skipped (calls and statements it does not emulate) goes to the Console,
// each once.
void CameraView::updateBehaviourScript() {
    behaviourScript_.update(ctx_->doc.map(), ctx_->doc.path(), ctx_->doc.revision(), std::max(0.0, ctx_->scene.behaviourTime));
    ctx_->scene.scriptPoses.clear();
    for (const GscPose& p : behaviourScript_.poses()) {
        app::SceneCache::PlayPose& sp = ctx_->scene.scriptPoses[p.entity];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) sp.R[i][j] = p.R[i][j];
        sp.pivot = p.pivot;
        sp.move = p.move;
        sp.visible = p.visible;
    }
    const QString file = QFileInfo(QString::fromStdString(behaviourScript_.scriptPath())).fileName();
    for (const GscNote& n : behaviourScript_.takeNewNotes())
        EditorLog::instance().add(LogLevel::Info, "script",
                                  QString("Behaviour Preview, map script %1%2: %3")
                                      .arg(file, n.line > 0 ? QString(" line %1").arg(n.line) : QString(), QString::fromStdString(n.text)));
}

size_t CameraView::referenceTexturesResolved() const {
    size_t n = 0;
    for (bool b : refTexResolved_) n += b ? 1 : 0;
    return n;
}

// ---- View > Game Lighting (game_lightmap.h, core game_look; research 95 "Editor maps in game")

app::SceneRenderer::PixelLight CameraView::pixelLightOf(const app::SceneCache::PreviewLight& L, bool game) const {
    app::SceneRenderer::PixelLight p{};
    p.pos[0] = float(L.pos.x), p.pos[1] = float(L.pos.y), p.pos[2] = float(L.pos.z), p.radius = float(L.radius);
    p.color[0] = float(L.color.x), p.color[1] = float(L.color.y), p.color[2] = float(L.color.z);
    p.cosOuter = float(L.cosOuter), p.cosInner = float(L.cosInner);
    p.dir[0] = float(L.dir.x), p.dir[1] = float(L.dir.y), p.dir[2] = float(L.dir.z);
    if (game) {
        // The converter's light units (core game_look): the reference is radius / 2, or a spot's axis to the first
        // surface (convert.cpp traces the render triangles; here the collision world).
        double ref = L.radius * 0.5;
        if (L.cosOuter > -1.5)
            if (const CollisionWorld* cw = ctx_->scene.shadowWorld(ctx_->doc)) {
                const double f = cw->traceRay(L.pos, L.pos + L.dir * ref).fraction;
                if (f < 1.0) ref = std::max(1.0, f * ref);
            }
        const double gi = gameLightIntensity(L.radius, ref);
        for (float& c : p.color) c = float(c * gi);
    }
    return p;
}

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
            renderer_->setGameTonemapLut(64, lut.data());
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
            renderer_->setEnvBrdfLut(pendingEnvBrdfW_, pendingEnvBrdfH_, pendingEnvBrdf_->data());
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
            renderer_->setGtsoLut(pendingGtso_->width, pendingGtso_->height, pendingGtso_->depth, pendingGtso_->r.data());
            pendingGtso_.reset();
        }
        if (pendingLights_) {
            renderer_->setStockLocalLights(*pendingLights_, pendingShadows_ ? *pendingShadows_ : std::vector<StockLightShadow>{});
            renderer_->setIesStrip(pendingIes_ ? int(pendingIes_->size()) : 0, pendingIes_ && !pendingIes_->empty() ? pendingIes_->data() : nullptr);
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
        } else if (const Entity* ws = ctx_->doc.worldspawn()) {
            // An own map: Map Info's exposure (worldspawn iw8r_exposure_adjust, EV: +1 doubles the picture), as the build
            // hands it to the converter.
            if (const std::string* ea = ws->value("iw8r_exposure_adjust")) {
                char* end = nullptr;
                const double v = std::strtod(ea->c_str(), &end);
                if (end != ea->c_str() && std::isfinite(v) && std::fabs(v) <= 8) exposure.exposureAdjust = v;
            }
        }
        g.sunToward[0] = float(t.x), g.sunToward[1] = float(t.y), g.sunToward[2] = float(t.z);
        g.sunColor[0] = float(c.x), g.sunColor[1] = float(c.y), g.sunColor[2] = float(c.z);
        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        g.skyIsUnbaked = gameLookUnbaked();
        // The vision's post effects and fog: a stock map's own vision, an own map's haze (research 126).
        const GamePostFx& postFx = stock ? stockPostFx_ : ownPostFx_;
        const GameFog& gameFog = stock ? stockFog_ : ownFog_;
        g.veil = float(postFx.veil);  // the bloom veil and vignette (research 109)
        if (stock) g.grain = float(stockPostFx_.grain);
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
        {
            const GamePostFx& p = postFx;
            // An own map's volumetric boxes (research 127): the 8 nearest to the camera go to the march.
            if (!stock && !ownVolumes_.empty()) {
                std::vector<const OwnVolumeBox*> closest;
                for (const OwnVolumeBox& b : ownVolumes_) closest.push_back(&b);
                const Vec3 eye = ctx_->cam.pos;
                const auto far2 = [&](const OwnVolumeBox* b) {
                    const Vec3 d = b->center - eye;
                    return d.x * d.x + d.y * d.y + d.z * d.z;
                };
                std::sort(closest.begin(), closest.end(), [&](const OwnVolumeBox* a, const OwnVolumeBox* b) { return far2(a) < far2(b); });
                g.volBoxCount = int(std::min<size_t>(closest.size(), 8));
                for (int i = 0; i < g.volBoxCount; ++i) {
                    const OwnVolumeBox& b = *closest[size_t(i)];
                    float* r = g.volBoxes[i];
                    r[0] = float(b.center.x), r[1] = float(b.center.y), r[2] = float(b.center.z), r[3] = float(b.density);
                    for (int k = 0; k < 3; ++k)
                        r[4 + k * 4] = float(b.axis[k].x), r[5 + k * 4] = float(b.axis[k].y), r[6 + k * 4] = float(b.axis[k].z), r[7 + k * 4] = float(b.half[k]);
                    r[16] = float(b.invFalloff), r[17] = float(b.heightK);
                    // Masks: the code of each (its picture's slice and its axis) and where it reads the picture now.
                    float* mk = g.volBoxMasks[i];
                    for (int k = 0; k < 4; ++k) mk[k] = -1.0f;
                    if (!maskClock_.isValid()) maskClock_.start();
                    const double now = ctx_->scene.captureMode ? 0.0 : maskClock_.elapsed() / 1000.0;
                    for (size_t k = 0; k < b.masks.size() && k < 4; ++k) {
                        const auto found = std::find(ownMaskImages_.begin(), ownMaskImages_.end(), b.masks[k].image);
                        if (found == ownMaskImages_.end()) continue;  // no picture: the mask does nothing in the preview
                        mk[k] = float((found - ownMaskImages_.begin()) * 4 + b.masks[k].axis);
                        volumeMaskTerms(b.masks[k], now, mk + 4 + k * 4);
                    }
                }
            }
            // An own map's lamps that light the air (iw8r_volumetric), in the units the surfaces get: the 8 nearest.
            if (!stock && (p.volAir > 0 || g.volBoxCount > 0)) {
                // (Straight from the light entities: this runs before the frame's per-pixel light list is filled.)
                std::vector<std::pair<double, size_t>> order;
                for (size_t k = 0; k < ctx_->scene.lights.size(); ++k)
                    if (ctx_->scene.lights[k].volumetric) {
                        const Vec3 d = ctx_->scene.lights[k].pos - ctx_->cam.pos;
                        order.push_back({d.x * d.x + d.y * d.y + d.z * d.z, k});
                    }
                std::sort(order.begin(), order.end());
                g.volLampCount = int(std::min<size_t>(order.size(), 8));
                for (int i = 0; i < g.volLampCount; ++i) {
                    const app::SceneRenderer::PixelLight l = pixelLightOf(ctx_->scene.lights[order[size_t(i)].second], true);
                    float* r = g.volLamps[i];
                    r[0] = l.pos[0], r[1] = l.pos[1], r[2] = l.pos[2], r[3] = l.radius;
                    r[4] = l.color[0], r[5] = l.color[1], r[6] = l.color[2], r[7] = l.cosOuter;
                    r[8] = l.dir[0], r[9] = l.dir[1], r[10] = l.dir[2], r[11] = l.cosInner;
                }
                g.volLampBrightness[0] = float(p.volSpot), g.volLampBrightness[1] = float(p.volOmni);
            }
            if (p.volAir > 0 || g.volBoxCount > 0) {  // the volumetric fog (research 109 section 2)
                const float hExp = p.volHeightHalf != 0 ? float(-0.693147 / std::max(p.volHeightHalf, 0.1)) : 0.0f;
                const float vol[3][4] = {{float(p.volAlbedo[0]), float(p.volAlbedo[1]), float(p.volAlbedo[2]), float(p.volAir * 0.01)},
                                         {float(p.volAbsorption), float(p.volAmbient), float(p.volHeightBase), hExp},
                                         {float(p.volSun), float(p.volAnisotropy), 1.0f, 0.0f}};
                std::memcpy(g.vol, vol, sizeof(vol));
                g.volOwn = !stock;
            }
            // The shape of what is being drawn: a capture's own, else the widget's.
            double aspect = height() > 0 ? double(width()) / double(height()) : 16.0 / 9.0;
            if (captureW_ > 0 && captureH_ > 0) aspect = captureAspect_ > 0 ? captureAspect_ : double(captureW_) / double(captureH_);
            gameVignetteConstants(postFx, aspect, 1.0, g.vignette);
        }
        if (gameFog.on) {
            const GameFog& f = gameFog;
            const double s = gameFogInscatter(f);
            const float fog[6][4] = {{float(f.start), float(f.halfPlane), float(f.maxOpacity), 1.0f},
                                     {float(f.color[0] * s), float(f.color[1] * s), float(f.color[2] * s), float(f.mieBlend)},
                                     {float(f.rayleigh[0]), float(f.rayleigh[1]), float(f.rayleigh[2]), float(f.mieAnisotropy)},
                                     {float(f.mie[0]), float(f.mie[1]), float(f.mie[2]), float(f.desaturation)},
                                     {float(f.heightBase), float(f.heightHalf), float(f.skyDistance), float(f.skyIntensity)},
                                     {float(f.maxDistance), float(f.skyMinAngle), float(f.skyMaxAngle), 0}};
            std::memcpy(g.fog, fog, sizeof(fog));
            // The game's own constants (research 111); 135.w follows the camera's height.
            const GameFogConstants k = gameFogConstants(stock ? stockFogRefdef_ : ownFogRefdef_, float(ctx_->cam.pos.z));
            std::memcpy(g.fogK, k.c, sizeof(g.fogK));
        }
        // The sky preview is an 8-bit copy of the HDR stock sky times its conversion exposure: undo it, so the sky has
        // its own radiance in the game's light units (and the post chain exposes it like the rest).
        if (stock) g.bakedScale = float(stockBakedScale_);
        if (stock && skyExposure_ > 0)
            g.skyScale = float((stockSkyIntensity_ > 0 ? stockSkyIntensity_ : 1.0) / skyExposure_ * skyRgbmRange_);  // RGBM: x range
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
                renderer_->setGameMetering(inv.data());
            } else {
                renderer_->setGameMetering(nullptr);
            }
            gameExposure_ = 0;
            gameExposureSnap_ = true;
        }
        // Auto exposure: the metered log-average of the frames before (one or two frames late, as the game's own),
        // adapted over time at the vision's speed; until a measurement arrives, the exposure of a mid-grey scene.
        double avg = 0;
        if (renderer_->takeMeteredLuminance(avg)) {
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
    renderer_->setGameLook(g);
}

void CameraView::maybeBakeGameLook() {
    if (!gameLookOn() || gameBaking_) return;
    // An own map without "Bake lightmaps": the build makes no lightmap, and the game lights the map with the
    // converter's constant sky fill and the live sun. Nothing to bake; a lightmap of an earlier bake goes.
    if (gameLookUnbaked()) {
        if (gameLm_) {
            gameLm_.reset();
            renderer_->setGameLightmap(0, 0, nullptr, nullptr, nullptr);
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
        renderer_->setGameLightmap(lm->width, lm->height, lm->a.data(), lm->b.data(), lm->d.data());
        emit ctx_->statusMessage(QString("Game lighting: baked %1 x %2 lightmap, %3 faces, %4 units per texel, %5 s")
                                     .arg(lm->width).arg(lm->height).arg(lm->polys).arg(lm->texelSize).arg(lm->seconds, 0, 'f', 1));
    }
    gameLm_ = lm;
    geomDirty_ = true;
    update();
}

}  // namespace iw8r::qt
