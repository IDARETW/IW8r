// IW8 Radiant - the asset preview window (owner request 2026-11-14: "a sophisticated navigable preview window
// with textures applied to the model"): a model or a prefab in a real, textured 3D view of its own.
//
// The window is a second CameraView with its own EditorContext, given the same material, image, model and prefab hooks
// as the main camera (CameraView::copyHooksFrom), so the asset is drawn with the textures, normal maps and lighting
// modes of the main view. Navigation: orbit with the left (or right) button, pan with the middle button or Shift+left,
// zoom with the wheel, F frames the asset, R resets the view. Side panel: view mode, floor, collision hulls, the level
// of detail, the asset's size and triangles, and its materials (a click shows the material's colour image).
#pragma once

#include <QImage>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
#include <string>

#include "editor_context.h"
#include "model_import.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace iw8r::qt {

class CameraView;

class AssetPreviewWindow : public QWidget {
    Q_OBJECT
public:
    struct Sources {
        // A model by name and level of detail (0 = the model itself); null when it cannot be loaded.
        std::function<std::shared_ptr<const ImportedModel>(const std::string& model, int lod)> model;
        // How many levels of detail a model has (1 = only the model).
        std::function<int(const std::string& model)> lodCount;
        // A material's colour image, decoded (null = none or not cached yet).
        std::function<QImage(const std::string& material)> materialImage;
    };
    AssetPreviewWindow(ID3D11Device* dev, ID3D11DeviceContext* dc, CameraView* hooksFrom, const app::SceneCache& sceneHooks,
                       Sources sources, QWidget* parent = nullptr);
    ~AssetPreviewWindow() override;

    // Shows a model (a stock or custom model name).
    void showModel(const QString& name);
    // Shows a prefab: a .map file, drawn as it is placed (brushes, patches, models, prefabs inside it).
    bool showPrefab(const QString& path, QString* err = nullptr);
    // Frames the asset: the camera looks at it from its three-quarter view at a distance where it fills the view.
    void frame();

    // Tests.
    CameraView* view() const { return view_; }
    EditorContext& context() { return ctx_; }
    QString infoText() const;
    int materialCount() const;
    QString assetName() const { return name_; }

    // Pictures for the asset browsers (TexturedThumbs): the Lit view without the floor; a square picture of the asset
    // as shown, from its three-quarter view; the images and the models the view still waits for.
    void setPictureStyle();
    QImage picture(int side);
    int texturesWaiting() const;
    QStringList modelNames() const;
    // A material's images arrived in the caches: the view asks for them again.
    void materialsChanged();

private:
    void rebuildDocument();
    void updateInfo();
    void applyMode(int mode);
    AABB assetBounds() const;

    EditorContext ctx_;
    CameraView* view_ = nullptr;
    Sources sources_;
    QComboBox* mode_ = nullptr;
    QComboBox* lod_ = nullptr;
    QCheckBox* floor_ = nullptr;
    QCheckBox* hull_ = nullptr;
    QLabel* info_ = nullptr;
    QListWidget* materials_ = nullptr;
    QLabel* swatch_ = nullptr;
    QString name_;
    bool prefab_ = false;
    std::string prefabText_, prefabPath_;
    AABB assetBox_;  // the asset without the floor
    double frameScale_ = 1.05;  // frame(): the distance as a share of the one that fits the bounding sphere
};

}  // namespace iw8r::qt
