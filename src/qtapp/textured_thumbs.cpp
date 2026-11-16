#include "textured_thumbs.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include "asset_preview_window.h"

namespace iw8r::qt {

namespace {
constexpr int kFrameMs = 40;          // one render per tick while a picture is being made
constexpr int kMinFrames = 3;         // frames before a picture may be taken (geometry, first texture uploads)
constexpr qint64 kGiveUpMs = 45000;   // a material that cannot be exported: the picture is kept as it is by then
constexpr int kWindowUses = 300;      // pictures per hidden window before it is made again
}  // namespace

TexturedThumbs::TexturedThumbs(const QString& thumbDir, std::function<AssetPreviewWindow*()> makeWindow,
                               std::function<bool(const QString&)> modelWaiting, QObject* parent)
    : QObject(parent), makeWindow_(std::move(makeWindow)), modelWaiting_(std::move(modelWaiting)) {
    QDir().mkpath(thumbDir);
    models_ = std::make_shared<ThumbStore>(QDir(thumbDir).filePath("models.textured.pack"));
    prefabs_ = std::make_shared<ThumbStore>(QDir(thumbDir).filePath("prefabs.textured.pack"));
    timer_ = new QTimer(this);
    timer_->setInterval(kFrameMs);
    connect(timer_, &QTimer::timeout, this, [this] { step(); });
}

TexturedThumbs::~TexturedThumbs() { delete window_; }

QImage TexturedThumbs::picture(Kind kind, const QString& name) {
    if (kind == Model) return models_->getAny(name);
    return prefabs_->get(name, ThumbStore::signatureOf(name));
}

void TexturedThumbs::request(Kind kind, const QString& name, bool front) {
    if (name.isEmpty()) return;
    const QString key = keyOf(kind, name);
    if (active_ && job_.kind == kind && job_.name == name) return;
    if (queuedKeys_.contains(key)) {
        if (front)  // the side preview's asset goes first
            for (auto it = queue_.begin(); it != queue_.end(); ++it)
                if (it->kind == kind && it->name == name) {
                    const Job j = *it;
                    queue_.erase(it);
                    queue_.push_front(j);
                    break;
                }
        return;
    }
    if (!picture(kind, name).isNull()) return;
    queuedKeys_.insert(key, true);
    if (front) queue_.push_front({kind, name});
    else queue_.push_back({kind, name});
    if (!timer_->isActive()) timer_->start();
}

void TexturedThumbs::materialsChanged() {
    if (active_ && window_) window_->materialsChanged();
}

AssetPreviewWindow* TexturedThumbs::window() {
    if (window_ && windowUses_ >= kWindowUses) {
        delete window_;
        window_ = nullptr;
    }
    if (!window_ && makeWindow_) {
        window_ = makeWindow_();
        windowUses_ = 0;
        if (window_) window_->setPictureStyle();
    }
    return window_;
}

void TexturedThumbs::step() {
    if (stepping_) return;  // a render that runs the event loop must not start the next step inside this one
    stepping_ = true;
    if (!active_) {
        if (queue_.empty()) {
            timer_->stop();
            stepping_ = false;
            return;
        }
        job_ = queue_.front();
        queue_.pop_front();
        queuedKeys_.remove(keyOf(job_.kind, job_.name));
        AssetPreviewWindow* w = window();
        if (!w) {
            stepping_ = false;
            return;
        }
        ++windowUses_;
        bool shown = true;
        if (job_.kind == Model) w->showModel(job_.name);
        else shown = w->showPrefab(job_.name);
        if (!shown) {  // a prefab that does not parse: no picture
            stepping_ = false;
            return;
        }
        active_ = true;
        frames_ = 0;
        jobClock_.start();
    }
    AssetPreviewWindow* w = window_;
    // The asset waits for its models' exports (a model: itself; a prefab: the models it places); once they are in, it
    // is shown again with them.
    bool modelsWaiting = false;
    if (modelWaiting_) {
        if (job_.kind == Model) modelsWaiting = modelWaiting_(job_.name);
        else
            for (const QString& m : w->modelNames())
                if (modelWaiting_(m)) modelsWaiting = true;
    }
    if (frames_ > 0 && !modelsWaiting && w->property("iw8rModelsWaited").toBool()) {
        w->setProperty("iw8rModelsWaited", false);
        if (job_.kind == Model) w->showModel(job_.name);
        else w->showPrefab(job_.name);
        frames_ = 0;
    }
    if (modelsWaiting) w->setProperty("iw8rModelsWaited", true);
    const QImage img = w->picture(kSide);
    ++frames_;
    const bool waiting = modelsWaiting || w->texturesWaiting() > 0;
    if ((frames_ >= kMinFrames && !waiting) || jobClock_.elapsed() > kGiveUpMs) finish(img);
    stepping_ = false;
}

void TexturedThumbs::finish(const QImage& img) {
    active_ = false;
    if (img.isNull()) return;
    if (job_.kind == Model) models_->put(job_.name, 1, img);
    else prefabs_->put(job_.name, ThumbStore::signatureOf(job_.name), img);
    ++made_;
    emit pictureReady(int(job_.kind), job_.name);
}

}  // namespace iw8r::qt
