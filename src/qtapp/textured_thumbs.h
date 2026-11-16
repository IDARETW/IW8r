// IW8 Radiant Suite - textured pictures of models and prefabs for the asset browsers (owner request 2026-11-14: the
// Models and Prefabs tabs show textured thumbnails and a textured side preview, pre-generated and kept in a cache, not
// the grey drawings they had).
//
// A picture is made once, by a hidden asset preview window in its Textured mode (the 3D view of Preview in 3D: the main
// view's materials and images, no lighting), and kept in a pack (thumb_store.h):
//   <AppLocalData>/thumbs/models.textured.pack   by model name
//   <AppLocalData>/thumbs/prefabs.textured.pack  by prefab .map file, with the file's signature (an edit makes a new one)
// The browsers only read the packs. An asset with no picture yet is queued; the queue makes one picture at a time on
// the event loop (a few frames each, so the editor stays responsive) and keeps rendering until the view has every
// texture it asked for (or 45 seconds passed: a material that cannot be exported), then stores it and signals.
// `IW8Radiant.exe --thumb-pack models|prefabs` fills the packs ahead of time (MainWindow::runTexturedThumbPack).
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <deque>
#include <functional>
#include <memory>

#include "thumb_store.h"

class QTimer;

namespace iw8r::qt {

class AssetPreviewWindow;

class TexturedThumbs : public QObject {
    Q_OBJECT
public:
    enum Kind { Model = 0, Prefab = 1 };
    static constexpr int kSide = 256;  // pixels; the list thumbnails are this picture scaled down

    // makeWindow: a new hidden asset preview window. modelWaiting(name): true while the model is not in the model
    // cache yet (the hook starts its export), so a prefab's picture waits for its models.
    TexturedThumbs(const QString& thumbDir, std::function<AssetPreviewWindow*()> makeWindow,
                   std::function<bool(const QString&)> modelWaiting, QObject* parent = nullptr);
    ~TexturedThumbs() override;

    // The stored picture (null when there is none yet). A prefab is keyed by its .map file and that file's signature.
    QImage picture(Kind kind, const QString& name);
    // Queues a picture (front: the asset the side preview shows). Nothing happens when it is stored or queued already.
    void request(Kind kind, const QString& name, bool front = false);
    // A material's images arrived: the picture being made asks for them again.
    void materialsChanged();

    bool busy() const { return active_ || !queue_.empty(); }
    int queued() const { return int(queue_.size()) + (active_ ? 1 : 0); }
    int made() const { return made_; }
    ThumbStore& store(Kind kind) { return kind == Model ? *models_ : *prefabs_; }
    // The pack itself, for reads on worker threads (ThumbDecoder): it stays valid however long the reader holds it.
    std::shared_ptr<ThumbStore> storePtr(Kind kind) const { return kind == Model ? models_ : prefabs_; }

signals:
    void pictureReady(int kind, const QString& name);

private:
    struct Job {
        Kind kind;
        QString name;
    };
    void step();
    void finish(const QImage& img);
    AssetPreviewWindow* window();
    static QString keyOf(Kind kind, const QString& name) { return QString::number(int(kind)) + '|' + name; }

    std::shared_ptr<ThumbStore> models_, prefabs_;
    std::function<AssetPreviewWindow*()> makeWindow_;
    std::function<bool(const QString&)> modelWaiting_;
    AssetPreviewWindow* window_ = nullptr;
    int windowUses_ = 0;  // the window is made again after a few hundred pictures (its textures accumulate)
    std::deque<Job> queue_;
    QHash<QString, bool> queuedKeys_;
    bool active_ = false;
    Job job_{};
    QElapsedTimer jobClock_;
    int frames_ = 0;
    bool stepping_ = false;
    int made_ = 0;
    QTimer* timer_ = nullptr;
};

}  // namespace iw8r::qt
