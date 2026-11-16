#include "vfx_preview.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <future>
#include <thread>
#include <unordered_map>

#include "material_cache.h"
#include "vfx_textures.h"

namespace iw8r::qt {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxSide = 520;          // a wide dialog draws a square of at most this side, centred
constexpr double kYaw = 72, kPitch = -16;  // the view: from the side (looking along +Y, turned a little), a little above

double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float sat(float x) { return x < 0 ? 0.0f : x > 1 ? 1.0f : x; }

// The sprites of the effects' materials, shared by every preview: resolved through the material cache (asked for when
// it has no entry), decoded and composed on two worker threads, kept while they fit in a memory budget.
class SpriteStore : public QObject {
public:
    enum State { Unknown, Requested, Decoding, Ready, Missing };
    struct Slot {
        State state = Unknown;
        std::shared_ptr<const VfxSprite> sprite;
        QString why;
        quint64 used = 0;
        size_t bytes = 0;
        qint64 checked = 0;  // Requested: when the cache was last looked at (ms)
    };
    static SpriteStore& get() {
        static QPointer<SpriteStore> s;
        if (!s) s = new SpriteStore(qApp);
        return *s;
    }
    explicit SpriteStore(QObject* parent) : QObject(parent) {
        pool_.setMaxThreadCount(2);
        since_.start();
    }
    ~SpriteStore() override { pool_.waitForDone(); }
    void setCache(MaterialCache* c) {
        if (cache_ == c) return;
        if (cache_) disconnect(cache_, nullptr, this, nullptr);
        cache_ = c;
        slots_.clear();
        if (c)
            connect(c, &MaterialCache::materialReady, this, [this](const QString& m) {
                const auto it = slots_.find(m.toStdString());
                if (it == slots_.end() || it->second.state != Requested) return;
                it->second.state = Unknown;  // resolved on the next look
                notify();
            });
        notify();
    }
    MaterialCache* cache() const { return cache_; }
    // The slots of these materials; the ones the cache has no entry for are asked for in one request.
    std::vector<const Slot*> want(const std::vector<std::string>& names) {
        QStringList ask;
        std::vector<const Slot*> out;
        for (const std::string& n : names) {
            Slot& s = resolve(n, &ask);
            s.used = ++clock_;
            out.push_back(&s);
        }
        if (!ask.isEmpty() && cache_) cache_->request(ask);
        return out;
    }
    // `fn` runs when a sprite arrives or the cache answers (while `owner` lives).
    void listen(QObject* owner, std::function<void()> fn) {
        listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [](const auto& l) { return !l.first; }), listeners_.end());
        listeners_.push_back({QPointer<QObject>(owner), std::move(fn)});
    }

private:
    Slot& resolve(const std::string& name, QStringList* ask) {
        Slot& s = slots_[name];
        // A requested material is looked up again every two seconds as well (its answer may come without a signal).
        if (s.state == Requested && since_.elapsed() - s.checked > 2000 && cache_ && cache_->lookup(QString::fromStdString(name))) s.state = Unknown;
        if (s.state != Unknown) return s;
        if (!cache_) {
            s.state = Missing, s.why = "no material cache";
            return s;
        }
        const QString qn = QString::fromStdString(name);
        const auto e = cache_->lookup(qn);
        if (!e) {
            s.state = Requested;
            s.checked = since_.elapsed();
            ask->push_back(qn);
            return s;
        }
        const VfxSpriteMaterial pm = vfxSpriteMaterial(qn, *e, cache_->cacheRoot());
        if (pm.failed) {
            s.state = Missing, s.why = e->error.isEmpty() ? QString("the material cache could not export it") : e->error;
            return s;
        }
        if (pm.skip) {
            s.state = Missing, s.why = "a distortion set without its image or strength";
            return s;
        }
        s.state = Decoding;
        pool_.start([this, name, pm] {
            std::string why;
            std::shared_ptr<const VfxSprite> sp = loadVfxSprite(pm, &why);
            QMetaObject::invokeMethod(this, [this, name, sp, why] { arrived(name, sp, QString::fromStdString(why)); }, Qt::QueuedConnection);
        });
        return s;
    }
    void arrived(const std::string& name, const std::shared_ptr<const VfxSprite>& sp, const QString& why) {
        const auto it = slots_.find(name);
        if (it == slots_.end() || it->second.state != Decoding) return;  // the cache changed meanwhile
        Slot& s = it->second;
        if (sp) {
            s.state = Ready, s.sprite = sp;
            s.bytes = 0;
            for (const auto& l : sp->levels) s.bytes += l.pixels.size();
        } else {
            s.state = Missing, s.why = why;
        }
        evict();
        notify();
    }
    void evict() {
        // About 200 MB of sprites at most: the least recently drawn go first (read again when wanted).
        size_t total = 0;
        for (const auto& [n, s] : slots_) total += s.state == Ready ? s.bytes : 0;
        while (total > size_t(200) << 20) {
            Slot* oldest = nullptr;
            for (auto& [n, s] : slots_)
                if (s.state == Ready && (!oldest || s.used < oldest->used)) oldest = &s;
            if (!oldest) break;
            total -= oldest->bytes;
            oldest->state = Unknown, oldest->sprite.reset(), oldest->bytes = 0;
        }
    }
    void notify() {
        for (const auto& [owner, fn] : listeners_)
            if (owner) fn();
    }
    QPointer<MaterialCache> cache_;
    std::unordered_map<std::string, Slot> slots_;
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> listeners_;
    quint64 clock_ = 0;
    QElapsedTimer since_;
    QThreadPool pool_;  // last: its jobs are waited for before the rest goes
};

// One sprite on the screen: a parallelogram (centre, half axes along s and t, pixels), its image cell and colour.
struct Quad {
    float depth = 0;
    const VfxSprite* sprite = nullptr;
    int kind = 0;  // 0 lit (premultiplied over), 1 additive, 2 distortion
    float cx = 0, cy = 0, a0x = 0, a0y = 0, a1x = 0, a1y = 0;
    float u0 = 0, u1 = 1, v0 = 0, v1 = 1;  // the cell: s = -1 .. 1 -> u0 .. u1, t = -1 .. 1 -> v1 .. v0
    int col = 0, row = 0, level = 0;
    float rgba[4] = {1, 1, 1, 1};
    float ds[2] = {0, 0};           // distortion: constant 127 (inches)
    float tx = 0, ty = 0, bx = 0, by = 0;  // distortion: the quad's unit tangent and bitangent on screen, pixels per inch
};

// Bilinear inside the texel range [x0, x1) x [y0, y1) (an atlas cell: its neighbours never bleed in).
inline void sampleCell(const app::Rgba8Image& im, float u, float v, int x0, int x1, int y0, int y1, float out[4]) {
    float X = std::clamp(u * float(im.width) - 0.5f, float(x0), float(x1 - 1));
    float Y = std::clamp(v * float(im.height) - 0.5f, float(y0), float(y1 - 1));
    const int ix = int(X), iy = int(Y);
    const float fx = X - float(ix), fy = Y - float(iy);
    const int ix1 = std::min(ix + 1, x1 - 1), iy1 = std::min(iy + 1, y1 - 1);
    const size_t w = size_t(im.width);
    const uint8_t* a = &im.pixels[(size_t(iy) * w + size_t(ix)) * 4];
    const uint8_t* b = &im.pixels[(size_t(iy) * w + size_t(ix1)) * 4];
    const uint8_t* c = &im.pixels[(size_t(iy1) * w + size_t(ix)) * 4];
    const uint8_t* d = &im.pixels[(size_t(iy1) * w + size_t(ix1)) * 4];
    const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
    for (int k = 0; k < 4; ++k) out[k] = (a[k] * w00 + b[k] * w10 + c[k] * w01 + d[k] * w11) * (1.0f / 255.0f);
}

// The picture behind (RGB floats), bilinear, clamped to the square.
inline void sampleScene(const float* scene, int side, float x, float y, float out[3]) {
    x = std::clamp(x - 0.5f, 0.0f, float(side - 1)), y = std::clamp(y - 0.5f, 0.0f, float(side - 1));
    const int ix = int(x), iy = int(y), ix1 = std::min(ix + 1, side - 1), iy1 = std::min(iy + 1, side - 1);
    const float fx = x - float(ix), fy = y - float(iy);
    const float* a = scene + (size_t(iy) * side + ix) * 3;
    const float* b = scene + (size_t(iy) * side + ix1) * 3;
    const float* c = scene + (size_t(iy1) * side + ix) * 3;
    const float* d = scene + (size_t(iy1) * side + ix1) * 3;
    for (int k = 0; k < 3; ++k) out[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
}

// One quad into the rows [yb0, yb1) of the frame. Each write is clamped to 0..1 (the camera's display-space target).
void drawQuad(const Quad& q, int side, float* buf, const float* scene, int yb0, int yb1) {
    const float ex = std::fabs(q.a0x) + std::fabs(q.a1x), ey = std::fabs(q.a0y) + std::fabs(q.a1y);
    const int xmin = std::max(0, int(std::floor(q.cx - ex))), xmax = std::min(side - 1, int(std::ceil(q.cx + ex)));
    const int ymin = std::max(yb0, int(std::floor(q.cy - ey))), ymax = std::min(yb1 - 1, int(std::ceil(q.cy + ey)));
    if (xmin > xmax || ymin > ymax) return;
    const float det = q.a0x * q.a1y - q.a0y * q.a1x;
    if (std::fabs(det) < 1e-4f) return;
    const float inv = 1.0f / det;
    const float dsdx = q.a1y * inv, dsdy = -q.a1x * inv, dtdx = -q.a0y * inv, dtdy = q.a0x * inv;
    const app::Rgba8Image& im = q.sprite->levels[size_t(q.level)];
    const int cols = std::max(1, q.sprite->mat.cols), rows = std::max(1, q.sprite->mat.rows);
    const bool whole = q.sprite->mat.flare;  // a flare piece reads its whole image
    const int x0 = whole ? 0 : q.col * im.width / cols, x1 = whole ? im.width : std::max(x0 + 1, (q.col + 1) * im.width / cols);
    const int y0 = whole ? 0 : q.row * im.height / rows, y1 = whole ? im.height : std::max(y0 + 1, (q.row + 1) * im.height / rows);
    const float du = 0.5f * (q.u1 - q.u0), dv = 0.5f * (q.v0 - q.v1);
    float tex[4];
    for (int y = ymin; y <= ymax; ++y) {
        const float px = float(xmin) + 0.5f - q.cx, py = float(y) + 0.5f - q.cy;
        float s = px * dsdx + py * dsdy, t = px * dtdx + py * dtdy;
        float* d = buf + (size_t(y) * side + xmin) * 3;
        for (int x = xmin; x <= xmax; ++x, s += dsdx, t += dtdx, d += 3) {
            if (s < -1 || s > 1 || t < -1 || t > 1) continue;
            sampleCell(im, q.u0 + (s + 1) * du, q.v1 + (t + 1) * dv, x0, x1, y0, y1, tex);
            if (q.kind == 0) {
                // Lit (render.cpp ps_part): texel x colour x alpha, premultiplied over what is behind.
                const float a = sat(tex[3] * q.rgba[3]);
                if (a <= 0) continue;
                for (int k = 0; k < 3; ++k) d[k] = std::min(1.0f, tex[k] * q.rgba[k] * a + d[k] * (1 - a));
            } else if (q.kind == 1) {
                // Unlit and flares: added, x saturate(alpha).
                const float a = sat(q.rgba[3]);
                for (int k = 0; k < 3; ++k) d[k] = std::min(1.0f, d[k] + tex[k] * q.rgba[k] * a);
            } else {
                // Distortion (render.cpp ps_dist, research 139 section 2): the picture behind, moved along the quad's
                // tangent frame by the image, over it by saturate(4 m.z a).
                const float va = sat(q.rgba[3]);
                const float k = sat(tex[2] * va - 0.25f) * (4.0f / 3.0f);
                const float dx = q.ds[0] * k * (2 * tex[0] - 1), dy = q.ds[1] * k * (2 * tex[1] - 1);
                float sc[3];
                sampleScene(scene, side, float(x) + 0.5f + dx * q.tx + dy * q.bx, float(y) + 0.5f + dx * q.ty + dy * q.by, sc);
                const float al = sat(4 * tex[2] * va);
                for (int c = 0; c < 3; ++c) d[c] = std::min(1.0f, sc[c] * sat(q.rgba[c]) * al + d[c] * (1 - al));
            }
        }
    }
}

// The quads into the frame, in order, split into bands of rows on threads when there is much to draw.
void drawQuads(const std::vector<Quad>& quads, int side, float* buf, const float* scene) {
    double work = 0;
    for (const Quad& q : quads) work += 4.0 * (std::fabs(q.a0x) + std::fabs(q.a1x)) * (std::fabs(q.a0y) + std::fabs(q.a1y));
    const int threads = work < 120000 ? 1 : std::clamp(int(std::thread::hardware_concurrency()), 1, 8);
    const auto band = [&](int y0, int y1) {
        for (const Quad& q : quads) drawQuad(q, side, buf, scene, y0, y1);
    };
    if (threads == 1) return band(0, side);
    std::vector<std::future<void>> jobs;
    const int rows = (side + threads - 1) / threads;
    for (int b = 1; b < threads; ++b) jobs.push_back(std::async(std::launch::async, band, std::min(side, b * rows), std::min(side, (b + 1) * rows)));
    band(0, std::min(side, rows));
    for (auto& j : jobs) j.get();
}

bool drawnElement(const VfxEmitterSim& em) { return em.element == 0 || em.element == 8 || em.element == 10; }

}  // namespace

void VfxPreview::setMaterialCache(MaterialCache* cache) { SpriteStore::get().setCache(cache); }

VfxPreview::VfxPreview(QWidget* parent) : QWidget(parent) {
    setObjectName("vfxPreview");
    setMinimumWidth(0);
    {
        QSizePolicy p(QSizePolicy::Ignored, QSizePolicy::Fixed);
        p.setHeightForWidth(true);
        setSizePolicy(p);
    }
    discs_ = qEnvironmentVariableIsSet("IW8R_VFX_PREVIEW_DISCS");  // A/B: the soft discs of before
    if (discs_) setMinimumHeight(150), setMaximumHeight(240);
    // The view's axes: looking along fwd (yaw from +X, pitch down), screen right and up.
    const double y = kYaw * kPi / 180, p = kPitch * kPi / 180;
    fwd_[0] = std::cos(p) * std::cos(y), fwd_[1] = std::cos(p) * std::sin(y), fwd_[2] = std::sin(p);
    const double rl = std::hypot(fwd_[1], fwd_[0]);
    right_[0] = fwd_[1] / rl, right_[1] = -fwd_[0] / rl, right_[2] = 0;  // fwd x Z
    up_[0] = right_[1] * fwd_[2] - right_[2] * fwd_[1], up_[1] = right_[2] * fwd_[0] - right_[0] * fwd_[2], up_[2] = right_[0] * fwd_[1] - right_[1] * fwd_[0];
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!playing_) return;
        if (def_.duration >= 0) {
            t_ = std::fmod(offset_ + clock_.elapsed() / 1000.0, loopLength());
        } else {
            // A looping effect plays on (no restart: every particle would vanish at once), from when it has filled up.
            t_ = offset_ + clock_.elapsed() / 1000.0;
            if (t_ > 3600) offset_ = warmTime(), clock_.restart(), t_ = offset_;
        }
        last_ = sampleVfx(def_, t_);
        dirty_ = true;
        update();
    });
    SpriteStore::get().listen(this, [this] {
        dirty_ = true;
        update();
    });
    followWidth();
}

double VfxPreview::loopLength() const {
    // One-shots replay after their duration plus a short pause; looping effects: the 4 seconds after they filled up.
    return def_.duration >= 0 ? def_.duration + 0.5 : warmTime() + 4.0;
}

double VfxPreview::warmTime() const {
    if (def_.duration >= 0) return 0;
    double warm = 0;
    for (const VfxEmitterSim& e : def_.emitters)
        if (!e.burst && e.spawnRate > 0) warm = std::max(warm, e.startDelay + e.lifeMax);
    return std::min(warm, 8.0);
}

double VfxPreview::busiestTime() const {
    if (def_.duration < 0) return warmTime() + 2.0;
    // The most drawn particle (opacity x size) of the run: not an instant where everything has just spawned unseen.
    double best = 0, most = -1;
    for (int k = 0; k <= 40; ++k) {
        const double t = def_.duration * k / 40.0;
        double n = 0;
        for (const VfxParticle& p : sampleVfx(def_, t))
            if (drawnElement(def_.emitters[size_t(p.emitter)])) n += std::clamp(double(p.rgba[3]), 0.0, 1.0) * std::max(0.5, p.size2[0] + p.size2[1]);
        if (n > most) most = n, best = t;
    }
    return best;
}

void VfxPreview::setEffect(const VfxSimDef& def) {
    def_ = def;
    playing_ = true;
    t_ = offset_ = warmTime();
    fit();
    last_ = sampleVfx(def_, t_);
    clock_.restart();
    timer_->start();
    dirty_ = true;
    update();
}

void VfxPreview::setTime(double t) {
    playing_ = false;
    t_ = t;
    last_ = sampleVfx(def_, t_);
    dirty_ = true;
    update();
}

void VfxPreview::setDiscStyle(bool on) {
    if (discs_ == on) return;
    discs_ = on;
    if (discs_) {
        setMinimumHeight(150);
        setMaximumHeight(240);
        fitDiscs();
    } else {
        followWidth();
    }
    dirty_ = true;
    update();
}

int VfxPreview::side() const { return std::max(1, std::min(width(), kMaxSide)); }

int VfxPreview::stripHeight() const {
    QFont f = font();
    f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.9));
    return QFontMetrics(f).lineSpacing() * 2 + 8;
}

int VfxPreview::heightForWidth(int w) const { return std::max(1, std::min(w, kMaxSide)) + stripHeight(); }

QSize VfxPreview::sizeHint() const { return {300, heightForWidth(300)}; }

QSize VfxPreview::minimumSizeHint() const { return {100, discs_ ? 150 : heightForWidth(100)}; }

QRect VfxPreview::viewRect() const {
    if (discs_) return rect();
    const int s = side();
    return {(width() - s) / 2, 0, s, s};
}

void VfxPreview::followWidth() {
    // The square follows the width (the side bar's column is 300 to 420 px; a wide dialog keeps it at kMaxSide).
    if (discs_) return;
    const int want = heightForWidth(width());
    if (minimumHeight() != want || maximumHeight() != want) setFixedHeight(want);
}

void VfxPreview::fit() {
    if (discs_) return fitDiscs();
    // The space the drawn particles cover over the run (each centre +- half its larger size, a tail's length), seen
    // along the view: the square is centred on it with a margin.
    // Weighted quantiles (3 % and 97 %), each particle by its opacity x size: a few stray particles far out (sparks
    // flying off, smoke drifting for minutes) do not shrink the effect to a dot.
    struct Edge {
        double v, w;
    };
    std::vector<Edge> edges[4];  // low R, high R, low U, high U
    std::vector<std::array<double, 5>> per(def_.emitters.size(), {1e30, -1e30, 1e30, -1e30, 0});
    const double t1 = loopLength();
    for (int k = 0; k <= 32; ++k)
        for (const VfxParticle& p : sampleVfx(def_, t1 * k / 32.0)) {
            const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
            if (!drawnElement(em)) continue;
            double r = 0.5 * std::max(p.size2[0], p.size2[1]);
            if (em.element == 10 && em.tailScaleWithVelocity) r = std::max(r, std::sqrt(dot3(p.vel, p.vel)) * 0.033 * 0.5);
            const double R = dot3(p.pos, right_), U = dot3(p.pos, up_);
            const double w = std::clamp(double(p.rgba[3]), 0.05, 1.0) * std::max(0.5, r);
            edges[0].push_back({R - r, w}), edges[1].push_back({R + r, w}), edges[2].push_back({U - r, w}), edges[3].push_back({U + r, w});
            auto& e = per[size_t(p.emitter)];
            e[0] = std::min(e[0], R - r), e[1] = std::max(e[1], R + r), e[2] = std::min(e[2], U - r), e[3] = std::max(e[3], U + r), e[4] = std::max(e[4], r);
        }
    fitInfo_.clear();
    for (size_t i = 0; i < per.size(); ++i)
        if (per[i][0] < per[i][1])
            fitInfo_ << QString("  fit e%1 (element %2): R %3 .. %4, U %5 .. %6, largest half size %7")
                            .arg(i).arg(def_.emitters[i].element).arg(per[i][0], 0, 'f', 1).arg(per[i][1], 0, 'f', 1)
                            .arg(per[i][2], 0, 'f', 1).arg(per[i][3], 0, 'f', 1).arg(per[i][4], 0, 'f', 1);
    const auto quantile = [](std::vector<Edge>& e, double q) {
        std::sort(e.begin(), e.end(), [](const Edge& a, const Edge& b) { return a.v < b.v; });
        double total = 0;
        for (const Edge& x : e) total += x.w;
        double acc = 0;
        for (const Edge& x : e)
            if ((acc += x.w) >= q * total) return x.v;
        return e.back().v;
    };
    double lo[2] = {-32, -32}, hi[2] = {32, 32};
    if (!edges[0].empty()) lo[0] = quantile(edges[0], 0.03), hi[0] = quantile(edges[1], 0.97), lo[1] = quantile(edges[2], 0.03), hi[1] = quantile(edges[3], 0.97);
    cr_ = (lo[0] + hi[0]) / 2, cu_ = (lo[1] + hi[1]) / 2;
    span_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 2.0}) * 1.15;  // inches across the square
    // The grid: a power of two of inches, about six cells across.
    groundStep_ = std::pow(2.0, std::round(std::log2(std::max(0.125, span_ / 6.0))));
    fitInfo_ << QString("  fit: R %1 .. %2, U %3 .. %4").arg(lo[0], 0, 'f', 1).arg(hi[0], 0, 'f', 1).arg(lo[1], 0, 'f', 1).arg(hi[1], 0, 'f', 1);
    fitSide_ = 0;  // the background is made again
    dirty_ = true;
}

void VfxPreview::fitDiscs() {
    // The look before: bounds of a few samples, x across and z up.
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    const double len = def_.duration >= 0 ? def_.duration + 0.5 : 4.0;
    for (int k = 1; k <= 8; ++k)
        for (const VfxParticle& p : sampleVfx(def_, len * k / 9.0)) {
            const double r = p.size / 2;
            lo[0] = std::min(lo[0], p.pos[0] - r), hi[0] = std::max(hi[0], p.pos[0] + r);
            lo[1] = std::min(lo[1], p.pos[2] - r), hi[1] = std::max(hi[1], p.pos[2] + r);
        }
    if (lo[0] > hi[0]) lo[0] = lo[1] = -32, hi[0] = hi[1] = 32;
    cx_ = (lo[0] + hi[0]) / 2, cz_ = (lo[1] + hi[1]) / 2;
    discSpan_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 16.0});
}

void VfxPreview::renderBackground() {
    const int s = side();
    QImage bg(s, s, QImage::Format_RGB32);
    QPainter g(&bg);
    QLinearGradient grad(0, 0, 0, s);
    grad.setColorAt(0, QColor(52, 56, 64));
    grad.setColorAt(1, QColor(26, 28, 32));
    g.fillRect(bg.rect(), grad);
    if (backdrop_) {
        // Behind a distortion set: a fine checker, so the picture it moves shows (the camera moves the scene).
        constexpr int kCell = 10;
        for (int y = 0; y < s; y += kCell)
            for (int x = (y / kCell) % 2 * kCell; x < s; x += 2 * kCell) g.fillRect(x, y, kCell, kCell, QColor(255, 255, 255, 16));
    }
    g.setRenderHint(QPainter::Antialiasing);
    const double k = s / span_;
    const auto at = [&](double x, double y, double z) {
        const double p[3] = {x, y, z};
        return QPointF(s / 2.0 + (dot3(p, right_) - cr_) * k, s / 2.0 - (dot3(p, up_) - cu_) * k);
    };
    // The ground (z = 0) around the origin, a grid fading out.
    const int n = std::clamp(int(std::ceil(span_ / groundStep_)) + 1, 2, 40);
    const double ext = n * groundStep_;
    for (int i = -n; i <= n; ++i) {
        const double v = i * groundStep_;
        const int alpha = i == 0 ? 50 : 20;
        g.setPen(QPen(QColor(255, 255, 255, alpha), 1));
        g.drawLine(at(v, -ext, 0), at(v, ext, 0));
        g.drawLine(at(-ext, v, 0), at(ext, v, 0));
    }
    // The origin: a small cross and the effect's up axis.
    g.setPen(QPen(QColor(255, 210, 60, 200), 1.2));
    const QPointF o = at(0, 0, 0);
    g.drawLine(o - QPointF(5, 0), o + QPointF(5, 0));
    g.drawLine(o - QPointF(0, 5), o + QPointF(0, 5));
    g.end();
    back_.resize(size_t(s) * s * 3);
    for (int y = 0; y < s; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(bg.constScanLine(y));
        float* d = &back_[size_t(y) * s * 3];
        for (int x = 0; x < s; ++x, d += 3) d[0] = qRed(line[x]) / 255.0f, d[1] = qGreen(line[x]) / 255.0f, d[2] = qBlue(line[x]) / 255.0f;
    }
    fitSide_ = s;
}

void VfxPreview::render() {
    QElapsedTimer clock;
    clock.start();
    const int s = side();
    textured_ = pending_ = undrawn_ = 0;
    missingNames_.clear();
    // The effect's materials: their sprites (asked for, read on the workers) and what the note says of them.
    SpriteStore& store = SpriteStore::get();
    std::vector<std::vector<const SpriteStore::Slot*>> matSlots(def_.emitters.size());
    {
        std::vector<std::string> names;
        for (const VfxEmitterSim& em : def_.emitters) {
            if (!drawnElement(em) && !(em.element == 3 && em.flare.present)) {
                ++undrawn_;
                continue;
            }
            for (const std::string& m : em.materials.empty() ? std::vector<std::string>{em.material} : em.materials)
                if (!m.empty()) names.push_back(m);
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        const std::vector<const SpriteStore::Slot*> got = store.want(names);
        std::unordered_map<std::string, const SpriteStore::Slot*> by;
        for (size_t i = 0; i < names.size(); ++i) {
            by[names[i]] = got[i];
            if (got[i]->state == SpriteStore::Missing) missingNames_ << QString::fromStdString(names[i]) + " (" + got[i]->why + ")";
            else if (got[i]->state != SpriteStore::Ready) ++pending_;
        }
        for (size_t e = 0; e < def_.emitters.size(); ++e) {
            const VfxEmitterSim& em = def_.emitters[e];
            const std::vector<std::string> ms = em.materials.empty() ? std::vector<std::string>{em.material} : em.materials;
            for (const std::string& m : ms) matSlots[e].push_back(m.empty() || !by.count(m) ? nullptr : by[m]);
        }
        bool distortion = false;
        for (const SpriteStore::Slot* sl : got) distortion = distortion || (sl->state == SpriteStore::Ready && sl->sprite && sl->sprite->mat.distortion);
        if (distortion != backdrop_) backdrop_ = distortion, fitSide_ = 0;
    }
    if (fitSide_ != s || back_.size() != size_t(s) * s * 3) renderBackground();
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
        if (!drawnElement(em) && !flare) continue;  // models, lights, runners, decals
        const auto& es = matSlots[size_t(p.emitter)];
        const SpriteStore::Slot* slot = es.empty() ? nullptr : es[std::min(es.size() - 1, size_t(std::max(0, p.material)))];
        if (!slot || slot->state != SpriteStore::Ready || !slot->sprite || slot->sprite->levels.empty()) continue;  // not read yet: nothing
        const VfxSprite& sp = *slot->sprite;
        const VfxSpriteMaterial& pm = sp.mat;
        Quad q;
        q.sprite = &sp;
        const bool unlit = !pm.lit && !pm.distortion;
        // The colour the vertex carries (camera_view.cpp addColour): an unlit set's emissive graph x the colour.
        float c[3];
        for (int i = 0; i < 3; ++i) c[i] = unlit && p.hasEmissive ? p.rgba[i] * p.emissive[i] : p.rgba[i];
        if (flare) {
            // A flare piece (research 139 section 1.3): its source in the upper left of the square, the piece on the line
            // from there through the centre, sized in square heights. Dirty lens pieces (grime fixed to the screen) are left out.
            if (pm.screenMask || !pm.flare) continue;
            const double src[2] = {-0.45, 0.45};
            const VfxFlareSprite fs = vfxFlareSprite(em, p.size2, src, 1.0, 1000.0);
            const double ca = std::cos(fs.angle), sa = std::sin(fs.angle);
            q.kind = 1;
            q.cx = float((fs.centre[0] * 0.5 + 0.5) * s), q.cy = float((0.5 - fs.centre[1] * 0.5) * s);
            q.a0x = float(fs.half[0] * ca * s * 0.5), q.a0y = float(-fs.half[0] * sa * s * 0.5);
            q.a1x = float(-fs.half[1] * sa * s * 0.5), q.a1y = float(-fs.half[1] * ca * s * 0.5);
            q.u0 = 0, q.u1 = 1, q.v0 = 0, q.v1 = 1;
            for (int i = 0; i < 3; ++i) q.rgba[i] = displayColour(c[i]);
            q.rgba[3] = p.rgba[3];
            q.level = 0;
            flares.push_back(q);
            continue;
        }
        const double w = std::max(0.25, p.size2[0] * 0.5), h = std::max(0.25, p.size2[1] * 0.5);
        double ax0[3], ax1[3];
        if (em.element == 10) {
            // A tail: long along the velocity seen from the view.
            const double fv = dot3(p.vel, fwd_);
            double dir[3] = {p.vel[0] - fwd_[0] * fv, p.vel[1] - fwd_[1] * fv, p.vel[2] - fwd_[2] * fv};
            double dl = std::sqrt(dot3(dir, dir));
            if (dl < 1e-6) std::copy(up_, up_ + 3, dir), dl = 1;
            for (double& v : dir) v /= dl;
            double side3[3] = {dir[1] * fwd_[2] - dir[2] * fwd_[1], dir[2] * fwd_[0] - dir[0] * fwd_[2], dir[0] * fwd_[1] - dir[1] * fwd_[0]};
            const double sl = std::max(1e-9, std::sqrt(dot3(side3, side3)));
            const double speed = std::sqrt(dot3(p.vel, p.vel));
            const double len = std::max(h, em.tailScaleWithVelocity ? speed * 0.033 * 0.5 : h);
            for (int i = 0; i < 3; ++i) ax0[i] = side3[i] / sl * w, ax1[i] = dir[i] * len;
        } else if (em.element == 8) {
            // An oriented sprite: the quaternion's y and z axes (effect axes = the preview's world).
            const double* qt = em.orientQuat;
            const auto rot = [&](const double v[3], double o[3]) {
                const double qx = qt[0], qy = qt[1], qz = qt[2], qw = qt[3];
                const double tx = 2 * (qy * v[2] - qz * v[1]), ty = 2 * (qz * v[0] - qx * v[2]), tz = 2 * (qx * v[1] - qy * v[0]);
                o[0] = v[0] + qw * tx + (qy * tz - qz * ty), o[1] = v[1] + qw * ty + (qz * tx - qx * tz), o[2] = v[2] + qw * tz + (qx * ty - qy * tx);
            };
            const double ly[3] = {0, 1, 0}, lz[3] = {0, 0, 1};
            double a0[3], a1[3];
            rot(ly, a0), rot(lz, a1);
            const double ca = std::cos(p.angle), sa = std::sin(p.angle);
            for (int i = 0; i < 3; ++i) ax0[i] = (a0[i] * ca + a1[i] * sa) * w, ax1[i] = (a1[i] * ca - a0[i] * sa) * h;
        } else {
            // A billboard facing the view, turned by the particle's angle.
            const double ca = std::cos(p.angle), sa = std::sin(p.angle);
            for (int i = 0; i < 3; ++i) ax0[i] = (right_[i] * ca + up_[i] * sa) * w, ax1[i] = (up_[i] * ca - right_[i] * sa) * h;
        }
        float px, py;
        toScreen(p.pos, px, py);
        q.cx = float(s / 2.0 - cr_ * k) + px, q.cy = float(s / 2.0 + cu_ * k) + py;
        toScreen(ax0, q.a0x, q.a0y);
        toScreen(ax1, q.a1x, q.a1y);
        const auto atLeast = [](float& x, float& y, float m, float dx, float dy) {
            const float l = std::hypot(x, y);
            if (l >= m) return;
            if (l < 1e-6f) x = dx * m, y = dy * m;
            else x *= m / l, y *= m / l;
        };
        atLeast(q.a0x, q.a0y, 0.75f, 1, 0);
        atLeast(q.a1x, q.a1y, 0.75f, 0, -1);
        q.depth = float(dot3(p.pos, fwd_));
        // The atlas cell of its frame, mirrored as the particle says.
        const int cols = std::max(1, pm.cols), rows = std::max(1, pm.rows);
        const int f = vfxAtlasFrame(em, p, cols * rows);
        q.col = f % cols, q.row = f / cols;
        q.u0 = float(q.col) / cols, q.u1 = float(q.col + 1) / cols;
        q.v0 = float(q.row) / rows, q.v1 = float(q.row + 1) / rows;
        if (p.flipU) std::swap(q.u0, q.u1);
        if (p.flipV) std::swap(q.v0, q.v1);
        // The mip level: about one texel per pixel across the cell.
        const double cw = double(sp.levels[0].width) / cols, ch = double(sp.levels[0].height) / rows;
        const double l0 = std::max(1e-3, 2.0 * std::hypot(q.a0x, q.a0y)), l1 = std::max(1e-3, 2.0 * std::hypot(q.a1x, q.a1y));
        const double ratio = std::max(cw / l0, ch / l1);
        q.level = std::clamp(int(std::floor(std::log2(std::max(1.0, ratio)))), 0, int(sp.levels.size()) - 1);
        if (pm.distortion) {
            q.kind = 2;
            for (int i = 0; i < 3; ++i) q.rgba[i] = p.rgba[i];
            q.rgba[3] = p.rgba[3];
            q.ds[0] = pm.distScale[0], q.ds[1] = pm.distScale[1];
            const double l0w = std::max(1e-9, std::sqrt(dot3(ax0, ax0))), l1w = std::max(1e-9, std::sqrt(dot3(ax1, ax1)));
            const double T[3] = {ax0[0] / l0w, ax0[1] / l0w, ax0[2] / l0w}, B[3] = {ax1[0] / l1w, ax1[1] / l1w, ax1[2] / l1w};
            toScreen(T, q.tx, q.ty);
            toScreen(B, q.bx, q.by);
            dist.push_back(q);
        } else {
            q.kind = pm.lit ? 0 : 1;
            for (int i = 0; i < 3; ++i) q.rgba[i] = displayColour(c[i]);
            q.rgba[3] = p.rgba[3];
            quads.push_back(q);
        }
    }
    // Far to near; the distortion after the sprites (from the picture so far); flares over everything.
    const auto farFirst = [](const Quad& a, const Quad& b) { return a.depth > b.depth; };
    std::stable_sort(quads.begin(), quads.end(), farFirst);
    std::stable_sort(dist.begin(), dist.end(), farFirst);
    textured_ = int(quads.size() + dist.size() + flares.size());
    debug_.clear();
    debug_ << QString("view: span %1 in, centre r %2 u %3, %4 px/in, grid %5 in; %6 sprites, %7 distortion, %8 flare pieces")
                  .arg(span_, 0, 'f', 1).arg(cr_, 0, 'f', 1).arg(cu_, 0, 'f', 1).arg(k, 0, 'f', 3).arg(groundStep_)
                  .arg(quads.size()).arg(dist.size()).arg(flares.size());
    for (const std::vector<Quad>* list : {&quads, &dist, &flares})
        for (size_t i = 0; i < list->size() && i < 4; ++i) {
            const Quad& q = (*list)[i];
            debug_ << QString("  kind %1 at (%2 %3) axes (%4 %5) (%6 %7) cell %8,%9 of %10x%11 level %12/%13 (%14x%15) rgba %16 %17 %18 %19 ds %20 %21")
                          .arg(q.kind).arg(q.cx, 0, 'f', 1).arg(q.cy, 0, 'f', 1).arg(q.a0x, 0, 'f', 1).arg(q.a0y, 0, 'f', 1)
                          .arg(q.a1x, 0, 'f', 1).arg(q.a1y, 0, 'f', 1).arg(q.col).arg(q.row).arg(q.sprite->mat.cols).arg(q.sprite->mat.rows)
                          .arg(q.level).arg(q.sprite->levels.size()).arg(q.sprite->levels[0].width).arg(q.sprite->levels[0].height)
                          .arg(q.rgba[0], 0, 'f', 2).arg(q.rgba[1], 0, 'f', 2).arg(q.rgba[2], 0, 'f', 2).arg(q.rgba[3], 0, 'f', 2)
                          .arg(q.ds[0], 0, 'f', 2).arg(q.ds[1], 0, 'f', 2);
        }
    drawQuads(quads, s, buf_.data(), nullptr);
    if (!dist.empty()) {
        scene_ = buf_;
        drawQuads(dist, s, buf_.data(), scene_.data());
    }
    drawQuads(flares, s, buf_.data(), nullptr);
    if (frame_.width() != s || frame_.height() != s) frame_ = QImage(s, s, QImage::Format_RGB32);
    for (int y = 0; y < s; ++y) {
        auto* line = reinterpret_cast<QRgb*>(frame_.scanLine(y));
        const float* d = &buf_[size_t(y) * s * 3];
        for (int x = 0; x < s; ++x, d += 3)
            line[x] = qRgb(int(sat(d[0]) * 255 + 0.5f), int(sat(d[1]) * 255 + 0.5f), int(sat(d[2]) * 255 + 0.5f));
    }
    dirty_ = false;
    renderMs_ = clock.nsecsElapsed() / 1e6;
}

QImage VfxPreview::frame() {
    if (discs_) return grab(viewRect()).toImage();
    if (dirty_ || frame_.width() != side()) render();
    return frame_;
}

QString VfxPreview::statusText() const {
    return QString("%1 s, %2 particle(s), %3 emitter(s)%4 (click: %5)")
        .arg(t_, 0, 'f', 2).arg(last_.size()).arg(def_.emitters.size())
        .arg(def_.duration < 0 ? QString(", looping") : QString(", one-shot %1 s").arg(def_.duration, 0, 'f', 1))
        .arg(playing_ ? "pause" : "play");
}

QString VfxPreview::noteText() const {
    if (!SpriteStore::get().cache()) return "No material cache: the particles' images cannot be read";
    QStringList parts;
    if (pending_ > 0) parts << QString("Loading %1 material image(s)...").arg(pending_);
    if (!missingNames_.isEmpty()) parts << "Not found: " + missingNames_.join(", ");
    if (parts.isEmpty()) parts << QString("%1 sprite(s) drawn with their images.").arg(textured_);
    if (undrawn_ > 0)
        parts << QString("%1 emitter(s) of models, child effects, decals or lights not shown.").arg(undrawn_);
    return parts.join(" ");
}

void VfxPreview::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.fillRect(rect(), QColor(18, 20, 24));
    if (discs_) return paintDiscs(g);
    if (dirty_ || frame_.width() != side()) render();
    const QRect vr = viewRect();
    g.drawImage(vr.topLeft(), frame_);
    // The text under the square: the time and counts, then what the materials are doing (one line each, elided).
    QFont f = font();
    f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.9));
    g.setFont(f);
    const QFontMetrics fm(f);
    const QRect strip(vr.left() + 4, vr.bottom() + 4, vr.width() - 8, height() - vr.bottom() - 4);
    g.setPen(QColor(200, 200, 200));
    g.drawText(QRect(strip.left(), strip.top(), strip.width(), fm.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(statusText(), Qt::ElideRight, strip.width()));
    const QString note = noteText();
    g.setPen(missingNames_.isEmpty() ? QColor(150, 155, 165) : QColor(230, 170, 90));
    g.drawText(QRect(strip.left(), strip.top() + fm.lineSpacing(), strip.width(), fm.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(note, Qt::ElideRight, strip.width()));
    setToolTip(note.size() > 40 ? note : QString());
}

void VfxPreview::paintDiscs(QPainter& g) {
    // The look before (A/B): every particle an untextured soft disc in a side view (x across, z up).
    g.setRenderHint(QPainter::Antialiasing);
    const double scale = 0.8 * std::min(std::max(width(), 200), std::max(height(), 200)) / discSpan_;
    const QPointF c(width() / 2.0, height() / 2.0);
    auto at = [&](double x, double z) { return c + QPointF((x - cx_) * scale, -(z - cz_) * scale); };
    g.setPen(QPen(QColor(255, 255, 255, 40), 1));
    g.drawLine(QPointF(0, at(0, 0).y()), QPointF(width(), at(0, 0).y()));
    g.setPen(QPen(QColor(255, 210, 60), 1));
    const QPointF o = at(0, 0);
    g.drawLine(o - QPointF(6, 0), o + QPointF(6, 0));
    g.drawLine(o - QPointF(0, 6), o + QPointF(0, 6));
    g.setPen(Qt::NoPen);
    g.setCompositionMode(QPainter::CompositionMode_Plus);
    for (const VfxParticle& p : last_) {
        const double r = std::max(1.5, p.size * scale / 2);
        QColor col = QColor::fromRgbF(std::clamp(p.rgba[0], 0.f, 1.f), std::clamp(p.rgba[1], 0.f, 1.f), std::clamp(p.rgba[2], 0.f, 1.f),
                                      std::clamp(p.rgba[3], 0.f, 1.f) * 0.6f);
        QRadialGradient grad(at(p.pos[0], p.pos[2]), r);
        grad.setColorAt(0, col);
        col.setAlphaF(0);
        grad.setColorAt(1, col);
        g.setBrush(grad);
        g.drawEllipse(at(p.pos[0], p.pos[2]), r, r);
    }
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    g.setPen(QColor(200, 200, 200));
    g.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap,
               QString("t = %1 s, %2 particle(s), %3 emitter(s)%4. Side view, simplified (click: %5)")
                   .arg(t_, 0, 'f', 2).arg(last_.size()).arg(def_.emitters.size())
                   .arg(def_.duration < 0 ? QString(", looping") : QString(", one-shot %1 s").arg(def_.duration, 0, 'f', 1))
                   .arg(playing_ ? "pause" : "play"));
}

void VfxPreview::showEvent(QShowEvent*) {
    followWidth();
    if (playing_ && !def_.emitters.empty() && !timer_->isActive()) {
        offset_ = t_;
        clock_.restart();
        timer_->start();
    }
}

void VfxPreview::hideEvent(QHideEvent*) { timer_->stop(); }

void VfxPreview::resizeEvent(QResizeEvent*) {
    followWidth();  // the square follows the width (a side panel is laid out after the effect is set)
    dirty_ = true;
}

void VfxPreview::mousePressEvent(QMouseEvent*) {
    playing_ = !playing_;
    if (playing_) {
        offset_ = t_;
        clock_.restart();
    }
    update();
}

}  // namespace iw8r::qt
