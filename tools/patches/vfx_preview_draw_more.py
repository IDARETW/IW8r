import io

# The effect preview draws what it skipped (owner 2026-11-16: "a lot of the vfx dont show anything in the preview"):
#   - runners (element 9): each runner particle plays its child effect (exported from the parent's zone when needed) at
#     its own place, from its own age; children of children too (3 levels, 24 effects at most);
#   - models (7): the particle's XModel from the model cache, flat-shaded triangles behind the sprites;
#   - lights (5 / 6): an additive glow in the particle's colour x the light's brightness;
#   - decals (2): the decal's material flat on the ground under the particle.
# The sources of child effects and models are set by the main window (setEffectSource / setModelSource).

ROOT = 'E:/IW8-Radiant/src/'


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


rw('qtapp/vfx_preview.h', [
('''#include <vector>

#include "vfx_sim.h"

class QTimer;

namespace iw8r::qt {''', '''#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "vfx_sim.h"

class QTimer;

namespace iw8r {
struct ImportedModel;
}

namespace iw8r::qt {'''),
('''    static void setMaterialCache(MaterialCache* cache);
    void setEffect(const VfxSimDef& def);  // restarts playback and refits the view''', '''    static void setMaterialCache(MaterialCache* cache);
    // Child effects of runner particles and the models of model particles (MainWindow sets both: the effect exports,
    // asked for from the parent's zone; the stock model cache). Null with *pending set while being exported.
    using EffectSource = std::function<std::shared_ptr<const VfxSimDef>(const std::string& child, const std::string& parent, bool* pending)>;
    using ModelSource = std::function<std::shared_ptr<const iw8r::ImportedModel>(const std::string& model, bool* pending)>;
    static void setEffectSource(EffectSource fn);
    static void setModelSource(ModelSource fn);
    void setEffect(const VfxSimDef& def);  // restarts playback and refits the view'''),
('''    const VfxSimDef& effect() const { return def_; }''', '''    const VfxSimDef& effect() const { return root_; }  // as set (the children of its runners are drawn, not listed)
    int childEffects() const { return int(nodes_.size()) - 1; }  // child effects the runners play (tests)
    int modelsDrawn() const { return modelsDrawn_; }   // model particles drawn in the last frame
    int lightsDrawn() const { return lightsDrawn_; }   // light glows drawn in the last frame
    int decalsDrawn() const { return decalsDrawn_; }   // decals drawn in the last frame'''),
('''    QString statusText() const;
    QString noteText() const;

    VfxSimDef def_;''', '''    QString statusText() const;
    QString noteText() const;
    // The particles at t: the effect's own, and for each runner particle its child effect's at the particle's age and
    // place (emitter indices into def_, which lists the children's emitters after the effect's).
    std::vector<VfxParticle> sampleAll(double t) const;
    void buildTree();               // the runners' children into nodes_ and def_ (again when an export arrives)

    VfxSimDef root_;                // the effect as set
    VfxSimDef def_;                 // its emitters, then the emitters of every child effect
    struct Node {
        std::shared_ptr<const VfxSimDef> def;
        int offset = 0;             // its first emitter in def_
        std::vector<int> child;     // per emitter: the node its runner plays (-1 none)
    };
    std::vector<Node> nodes_;
    bool childrenPending_ = false, modelsPending_ = false;
    QElapsedTimer poll_;            // pending children and models are looked for again every 700 ms
    int modelsDrawn_ = 0, lightsDrawn_ = 0, decalsDrawn_ = 0;'''),
])

P = 'qtapp/vfx_preview.cpp'
rw(P, [
('''#include "material_cache.h"
#include "vfx_textures.h"''', '''#include "material_cache.h"
#include "model_import.h"
#include "vfx_textures.h"'''),
('''bool drawnElement(const VfxEmitterSim& em) { return em.element == 0 || em.element == 8 || em.element == 10; }

}  // namespace''', '''// Sprites: billboards, oriented sprites, tails and decals (a decal lies on the ground).
bool drawnElement(const VfxEmitterSim& em) { return em.element == 0 || em.element == 8 || em.element == 10 || em.element == 2; }
bool lightElement(const VfxEmitterSim& em) { return (em.element == 5 || em.element == 6) && em.light.present; }
// Everything the preview shows: the sprites, models and light glows (runners show through their children).
bool shownElement(const VfxEmitterSim& em) { return drawnElement(em) || em.element == 7 || lightElement(em); }

VfxPreview::EffectSource& effectSource() {
    static VfxPreview::EffectSource fn;
    return fn;
}
VfxPreview::ModelSource& modelSource() {
    static VfxPreview::ModelSource fn;
    return fn;
}

// A model's triangles for the preview (Z up, at most about 1,500 of them) and its radius, read once per model.
struct ModelTris {
    std::vector<std::array<float, 9>> tris;
    float radius = 4;
};
struct ModelSlot {
    std::shared_ptr<const ModelTris> tris;
    bool failed = false;
};
std::unordered_map<std::string, ModelSlot>& modelSlots() {
    static std::unordered_map<std::string, ModelSlot> m;
    return m;
}
// The model's triangles, or null (*pending while the cache exports it).
std::shared_ptr<const ModelTris> modelTris(const std::string& name, bool* pending) {
    ModelSlot& s = modelSlots()[name];
    if (s.tris || s.failed) return s.tris;
    if (!modelSource()) {
        s.failed = true;
        return nullptr;
    }
    bool wait = false;
    const std::shared_ptr<const ImportedModel> m = modelSource()(name, &wait);
    if (!m) {
        if (wait) *pending = true;
        else s.failed = true;
        return nullptr;
    }
    auto out = std::make_shared<ModelTris>();
    size_t total = 0;
    for (const auto& part : m->meshes) total += part.idx.size() / 3;
    const size_t step = std::max<size_t>(1, total / 1500);
    size_t n = 0;
    float r2 = 1;
    for (const auto& part : m->meshes)
        for (size_t i = 0; i + 2 < part.idx.size(); i += 3, ++n) {
            if (n % step) continue;
            std::array<float, 9> t{};
            for (int v = 0; v < 3; ++v) {
                const Vec3 p = part.pos[part.idx[i + size_t(v)]];
                // glTF Y up -> Z up (as the prefab pictures turn them).
                t[size_t(v) * 3 + 0] = float(p.x), t[size_t(v) * 3 + 1] = float(-p.z), t[size_t(v) * 3 + 2] = float(p.y);
                r2 = std::max(r2, float(p.x * p.x + p.y * p.y + p.z * p.z));
            }
            out->tris.push_back(t);
        }
    out->radius = std::sqrt(r2);
    s.tris = out;
    return s.tris;
}

// The light glow: a white disc fading out (additive; the quad's colour tints it).
const VfxSprite* glowSprite() {
    static const std::shared_ptr<VfxSprite> sp = [] {
        auto s = std::make_shared<VfxSprite>();
        s->mat.lit = false;
        s->mat.ready = true;
        app::Rgba8Image im;
        im.width = im.height = 64;
        im.pixels.resize(64 * 64 * 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float dx = (x + 0.5f) / 32.0f - 1, dy = (y + 0.5f) / 32.0f - 1;
                const float f = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
                const uint8_t v = uint8_t(std::clamp(f * f * 255.0f, 0.0f, 255.0f));
                uint8_t* px = &im.pixels[(size_t(y) * 64 + size_t(x)) * 4];
                px[0] = px[1] = px[2] = v;
                px[3] = 255;
            }
        s->levels.push_back(std::move(im));
        return s;
    }();
    return sp.get();
}

}  // namespace

void VfxPreview::setEffectSource(EffectSource fn) { effectSource() = std::move(fn); }
void VfxPreview::setModelSource(ModelSource fn) {
    modelSource() = std::move(fn);
    modelSlots().clear();
}

void VfxPreview::buildTree() {
    // The effect, then breadth first the child effects its runners play (3 levels, 24 effects at most); each child's
    // emitters are listed in def_ after the ones before it.
    def_ = root_;
    nodes_.clear();
    childrenPending_ = false;
    nodes_.push_back({std::make_shared<const VfxSimDef>(root_), 0, std::vector<int>(root_.emitters.size(), -1)});
    std::vector<int> depth{0};
    for (size_t n = 0; n < nodes_.size() && nodes_.size() < 24; ++n) {
        if (depth[n] >= 3) continue;
        const std::shared_ptr<const VfxSimDef> parent = nodes_[n].def;
        for (size_t e = 0; e < parent->emitters.size() && nodes_.size() < 24; ++e) {
            const VfxEmitterSim& em = parent->emitters[e];
            if (em.element != 9 || em.runners.empty() || !effectSource()) continue;
            bool pending = false;
            const std::shared_ptr<const VfxSimDef> child = effectSource()(em.runners.front(), parent->name, &pending);
            if (!child) {
                childrenPending_ = childrenPending_ || pending;
                continue;
            }
            nodes_[n].child[e] = int(nodes_.size());
            nodes_.push_back({child, int(def_.emitters.size()), std::vector<int>(child->emitters.size(), -1)});
            depth.push_back(depth[n] + 1);
            def_.emitters.insert(def_.emitters.end(), child->emitters.begin(), child->emitters.end());
        }
    }
    // A child plays while its runner particle lives (from the particle's age), so the effect's own run is the run.
    poll_.restart();
}

std::vector<VfxParticle> VfxPreview::sampleAll(double t) const {
    if (nodes_.size() <= 1) return sampleVfx(def_, t);
    std::vector<VfxParticle> out;
    const std::function<void(int, double, uint32_t, const double*, int)> walk = [&](int n, double tt, uint32_t seed, const double* at, int depth) {
        const Node& node = nodes_[size_t(n)];
        for (VfxParticle p : sampleVfx(*node.def, tt, seed)) {
            if (out.size() >= 3 * kVfxMaxParticles) return;
            for (int k = 0; k < 3; ++k) p.pos[k] += at[k];
            const int c = p.emitter >= 0 && size_t(p.emitter) < node.child.size() ? node.child[size_t(p.emitter)] : -1;
            if (c >= 0) {
                if (depth < 3) walk(c, p.age, seed * 2654435761u + uint32_t(p.rnd * 1e6) + 1u, p.pos, depth + 1);
                continue;
            }
            p.emitter += node.offset;
            out.push_back(p);
        }
    };
    const double origin[3] = {0, 0, 0};
    walk(0, t, 1, origin, 0);
    return out;
}'''),
# playback: pending children / models looked for again
('''        if (!playing_) return;
        if (def_.duration >= 0) {''', '''        if ((childrenPending_ || modelsPending_) && poll_.elapsed() > 700) {
            // An export finished meanwhile: the children are attached and the models read on the next frames.
            if (childrenPending_) {
                buildTree();
                fit();
            }
            modelsPending_ = false;
            poll_.restart();
            dirty_ = true;
        }
        if (!playing_) return update();
        if (def_.duration >= 0) {'''),
('''            if (t_ > 3600) offset_ = warmTime(), clock_.restart(), t_ = offset_;
        }
        last_ = sampleVfx(def_, t_);''', '''            if (t_ > 3600) offset_ = warmTime(), clock_.restart(), t_ = offset_;
        }
        last_ = sampleAll(t_);'''),
('''        for (const VfxParticle& p : sampleVfx(def_, t))
            if (drawnElement(def_.emitters[size_t(p.emitter)])) n += std::clamp(double(p.rgba[3]), 0.0, 1.0) * std::max(0.5, p.size2[0] + p.size2[1]);''',
 '''        for (const VfxParticle& p : sampleAll(t))
            if (shownElement(def_.emitters[size_t(p.emitter)])) n += std::clamp(double(p.rgba[3]), 0.0, 1.0) * std::max(0.5, p.size2[0] + p.size2[1]);'''),
('''void VfxPreview::setEffect(const VfxSimDef& def) {
    def_ = def;
    playing_ = true;
    t_ = offset_ = warmTime();
    fit();
    last_ = sampleVfx(def_, t_);''', '''void VfxPreview::setEffect(const VfxSimDef& def) {
    root_ = def;
    buildTree();
    playing_ = true;
    t_ = offset_ = warmTime();
    fit();
    last_ = sampleAll(t_);'''),
('''    playing_ = false;
    t_ = t;
    last_ = sampleVfx(def_, t_);''', '''    playing_ = false;
    t_ = t;
    last_ = sampleAll(t_);'''),
('''        for (const VfxParticle& p : sampleVfx(def_, t1 * k / 32.0)) {
            const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
            if (!drawnElement(em)) continue;
            double r = 0.5 * std::max(p.size2[0], p.size2[1]);''', '''        for (const VfxParticle& p : sampleAll(t1 * k / 32.0)) {
            const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
            if (!shownElement(em)) continue;
            double r = 0.5 * std::max(p.size2[0], p.size2[1]);
            if (em.element == 7) {
                // A model: its own size (read when the cache has it).
                bool wait = false;
                const auto mt = em.models.empty() ? nullptr : modelTris(em.models[size_t(p.rnd * 1e6) % em.models.size()], &wait);
                r = mt ? double(mt->radius) : 4.0;
            } else if (lightElement(em)) {
                r = std::min(r * 0.5, 48.0);  // a light's reach is not its glow: the fit keeps the effect large
            }'''),
('''        for (const VfxParticle& p : sampleVfx(def_, len * k / 9.0)) {''', '''        for (const VfxParticle& p : sampleAll(len * k / 9.0)) {'''),
# render: count only what is not shown at all; collect models and lights
('''            if (!drawnElement(em) && !(em.element == 3 && em.flare.present)) {
                ++undrawn_;
                continue;
            }''', '''            if (!drawnElement(em) && !(em.element == 3 && em.flare.present)) {
                if (!shownElement(em) && !(em.element == 9 && !em.runners.empty())) ++undrawn_;  // vector fields
                continue;
            }'''),
('''    if (fitSide_ != s || back_.size() != size_t(s) * s * 3) renderBackground();
    buf_ = back_;
    const double k = s / span_;  // pixels per inch
    const auto toScreen = [&](const double v[3], float& x, float& y) {
        x = float(dot3(v, right_) * k), y = float(-dot3(v, up_) * k);
    };
    std::vector<Quad> quads, dist, flares;
    const auto displayColour = [](float c) { return std::pow(std::max(c, 0.0f), 1.0f / 2.2f); };
    for (const VfxParticle& p : last_) {
        const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
        const bool flare = em.element == 3 && em.flare.present;
        if (!drawnElement(em) && !flare) continue;  // models, lights, runners, decals''', '''    if (fitSide_ != s || back_.size() != size_t(s) * s * 3) renderBackground();
    buf_ = back_;
    const double k = s / span_;  // pixels per inch
    const auto toScreen = [&](const double v[3], float& x, float& y) {
        x = float(dot3(v, right_) * k), y = float(-dot3(v, up_) * k);
    };
    std::vector<Quad> quads, dist, flares;
    const auto displayColour = [](float c) { return std::pow(std::max(c, 0.0f), 1.0f / 2.2f); };
    modelsDrawn_ = lightsDrawn_ = decalsDrawn_ = 0;
    {
        // Models: their triangles, flat-shaded (light from the upper left of the view), far to near, under the sprites.
        struct Tri {
            float depth;
            QPointF pts[3];
            QColor colour;
        };
        std::vector<Tri> tris;
        const double light[3] = {-right_[0] * 0.4 + up_[0] * 0.8 - fwd_[0] * 0.45, -right_[1] * 0.4 + up_[1] * 0.8 - fwd_[1] * 0.45,
                                 -right_[2] * 0.4 + up_[2] * 0.8 - fwd_[2] * 0.45};
        int instances = 0;
        for (const VfxParticle& p : last_) {
            const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
            if (em.element != 7 || em.models.empty() || instances >= 64) continue;
            bool wait = false;
            const auto mt = modelTris(em.models[size_t(p.rnd * 1e6) % em.models.size()], &wait);
            modelsPending_ = modelsPending_ || wait;
            if (!mt) continue;
            ++instances;
            // Turned about Z by the particle's angle and tumbled about X a little (debris and shells spin).
            const double ca = std::cos(p.angle), sa = std::sin(p.angle), cb = std::cos(p.angle * 0.7), sb = std::sin(p.angle * 0.7);
            const float alpha = std::clamp(p.rgba[3], 0.0f, 1.0f);
            for (const auto& t : mt->tris) {
                double w[3][3];
                for (int v = 0; v < 3; ++v) {
                    const double x = t[size_t(v) * 3], y = t[size_t(v) * 3 + 1], z = t[size_t(v) * 3 + 2];
                    const double y1 = y * cb - z * sb, z1 = y * sb + z * cb;
                    w[v][0] = x * ca - y1 * sa + p.pos[0], w[v][1] = x * sa + y1 * ca + p.pos[1], w[v][2] = z1 + p.pos[2];
                }
                const double e1[3] = {w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2]};
                const double e2[3] = {w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2]};
                double nrm[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                const double nl = std::sqrt(dot3(nrm, nrm));
                if (nl < 1e-12) continue;
                for (double& c : nrm) c /= nl;
                if (dot3(nrm, fwd_) > 0) for (double& c : nrm) c = -c;  // the side facing the view
                const double lit = 0.3 + 0.7 * std::max(0.0, dot3(nrm, light) / std::sqrt(dot3(light, light)));
                Tri tr;
                tr.depth = float((dot3(w[0], fwd_) + dot3(w[1], fwd_) + dot3(w[2], fwd_)) / 3);
                for (int v = 0; v < 3; ++v) {
                    float x, y;
                    toScreen(w[v], x, y);
                    tr.pts[v] = QPointF(s / 2.0 - cr_ * k + x, s / 2.0 + cu_ * k + y);
                }
                const int g = int(std::clamp(150.0 * lit, 0.0, 255.0));
                tr.colour = QColor(g, int(g * 0.96), int(g * 0.9), int(255 * std::max(0.35f, alpha)));
                tris.push_back(tr);
            }
        }
        modelsDrawn_ = instances;
        if (!tris.empty()) {
            std::sort(tris.begin(), tris.end(), [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
            QImage layer(s, s, QImage::Format_RGB32);
            for (int y = 0; y < s; ++y) {
                auto* line = reinterpret_cast<QRgb*>(layer.scanLine(y));
                const float* d = &buf_[size_t(y) * s * 3];
                for (int x = 0; x < s; ++x, d += 3) line[x] = qRgb(int(sat(d[0]) * 255 + 0.5f), int(sat(d[1]) * 255 + 0.5f), int(sat(d[2]) * 255 + 0.5f));
            }
            QPainter g(&layer);
            g.setRenderHint(QPainter::Antialiasing, true);
            g.setPen(Qt::NoPen);
            for (const Tri& tr : tris) {
                g.setBrush(tr.colour);
                g.drawPolygon(tr.pts, 3);
            }
            g.end();
            for (int y = 0; y < s; ++y) {
                const auto* line = reinterpret_cast<const QRgb*>(layer.constScanLine(y));
                float* d = &buf_[size_t(y) * s * 3];
                for (int x = 0; x < s; ++x, d += 3) d[0] = qRed(line[x]) / 255.0f, d[1] = qGreen(line[x]) / 255.0f, d[2] = qBlue(line[x]) / 255.0f;
            }
        }
    }
    for (const VfxParticle& p : last_) {
        const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
        const bool flare = em.element == 3 && em.flare.present;
        if (lightElement(em)) {
            // A light: a glow in its colour x brightness, added (about its size, which is the light's reach, kept small).
            Quad q;
            q.sprite = glowSprite();
            q.kind = 1;
            float px, py;
            toScreen(p.pos, px, py);
            q.cx = float(s / 2.0 - cr_ * k) + px, q.cy = float(s / 2.0 + cu_ * k) + py;
            const float half = float(std::clamp(0.25 * std::max(p.size2[0], p.size2[1]), 6.0, 96.0) * k);
            q.a0x = half, q.a0y = 0, q.a1x = 0, q.a1y = -half;
            const bool coloured = em.hasAttrColor || em.color.present;
            for (int i = 0; i < 3; ++i) q.rgba[i] = displayColour((coloured ? p.rgba[i] : 1.0f) * float(std::min(4.0, em.light.brightness)) * 0.45f);
            q.rgba[3] = std::max(0.3f, std::clamp(p.rgba[3], 0.0f, 1.0f));
            q.level = 0;
            q.depth = float(dot3(p.pos, fwd_));
            quads.push_back(q);
            ++lightsDrawn_;
            continue;
        }
        if (!drawnElement(em) && !flare) continue;  // models (above), runners (their children), vector fields'''),
('''        const double w = std::max(0.25, p.size2[0] * 0.5), h = std::max(0.25, p.size2[1] * 0.5);
        double ax0[3], ax1[3];
        if (em.element == 10) {''', '''        const double w = std::max(0.25, p.size2[0] * 0.5), h = std::max(0.25, p.size2[1] * 0.5);
        double ax0[3], ax1[3];
        double at[3] = {p.pos[0], p.pos[1], p.pos[2]};
        if (em.element == 2) {
            // A decal: flat on the ground under the particle, turned by its angle.
            const double ca = std::cos(p.angle), sa = std::sin(p.angle);
            ax0[0] = ca * w, ax0[1] = sa * w, ax0[2] = 0;
            ax1[0] = -sa * h, ax1[1] = ca * h, ax1[2] = 0;
            at[2] = 0.05;
            ++decalsDrawn_;
        } else if (em.element == 10) {'''),
('''        float px, py;
        toScreen(p.pos, px, py);
        q.cx = float(s / 2.0 - cr_ * k) + px, q.cy = float(s / 2.0 + cu_ * k) + py;
        toScreen(ax0, q.a0x, q.a0y);''', '''        float px, py;
        toScreen(at, px, py);
        q.cx = float(s / 2.0 - cr_ * k) + px, q.cy = float(s / 2.0 + cu_ * k) + py;
        toScreen(ax0, q.a0x, q.a0y);'''),
('''    if (undrawn_ > 0)
        parts << QString("%1 emitter(s) of models, child effects, decals or lights not shown.").arg(undrawn_);''',
 '''    if (childrenPending_) parts << "Exporting child effects...";
    if (modelsPending_) parts << "Exporting models...";
    if (modelsDrawn_ + lightsDrawn_ + decalsDrawn_ > 0)
        parts << QString("%1 model(s), %2 light(s), %3 decal(s).").arg(modelsDrawn_).arg(lightsDrawn_).arg(decalsDrawn_);
    if (undrawn_ > 0) parts << QString("%1 emitter(s) not drawn (vector fields).").arg(undrawn_);'''),
])
print('ok')
