#pragma once
#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <future>
#include "preview_shader.h"

class QPlainTextEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QTabWidget;
class QLineEdit;
class QTableWidget;
class QUndoStack;
class QCloseEvent;
struct ID3D11Device;

namespace iw8r::qt {
class CameraView;
class AssetPreviewWindow;
class ShaderGraphWidget;
class TechsetWorkspace : public QWidget {
    Q_OBJECT
  public:
    struct Hooks {
        ID3D11Device *device = nullptr;
        QPointer<CameraView> mainView;
        std::function<AssetPreviewWindow *()> makePreview;
        std::function<void(AssetPreviewWindow *)> refreshPreview;
        std::function<bool(AssetPreviewWindow *, QString *)> previewSelection;
        std::function<QString()> mapFolder;
        std::function<app::PreviewOverrideSet(const std::string &)> selectedTargets;
        std::function<void(const QString &, QObject *, std::function<void(const QString &, const QString &)>)>
            importMaterial;
        std::function<void(const QString &, QObject *, std::function<void(const QString &, const QString &)>)>
            requestImage;
    };
    explicit TechsetWorkspace(Hooks hooks, QWidget *parent = nullptr);
    ~TechsetWorkspace() override;
    bool openFile(const QString &file, QString *error = nullptr);
    bool saveFile(const QString &file, QString *error = nullptr);
    bool canClose();
    void newDocument();
    void openMaterial(const QString &name);
    void previewAsset(const QString &name, bool prefab);
    void previewEffect(const QString &name);
    bool previewSelection(QString *error = nullptr);
    void openImage(const QString &name);
    void compileNow();
    bool compiling() const;
    bool lastCompileSucceeded() const { return lastResult_.ok; }
    QString compileStatus() const;
    QString selectedTechnique() const;
    void selectTechnique(const QString &id);
    const TechsetDocument &document() const { return document_; }
    bool setDocument(const TechsetDocument &document, QString *error = nullptr);
    void setStageSource(ShaderStage stage, const QString &source);
    AssetPreviewWindow *previewWindow() const { return preview_; }
    void clearOverrides();
    void shutdown();
    std::function<void()> onSaved;
    std::function<void()> onExpand;
    std::function<void()> onOpen;

  private:
    void populate();
    void loadStage();
    void changed(bool compile = true);
    void pollCompile();
    void applyPreview();
    void updateParameters();
    void save();
    void recovery();
    void restoreRecovery();
    void applyLayout();
    void editBindings();
    void addTechnique(bool duplicate = false);
    TechniqueDefinition *technique();
    ShaderStageDocument *stage();
    Hooks hooks_;
    TechsetDocument document_;
    QString path_;
    bool dirty_ = false, loading_ = false;
    uint64_t revision_ = 0;
    uint64_t importGeneration_ = 0;
    QTimer debounce_, poll_, recoveryTimer_;
    struct Work;
    std::future<std::shared_ptr<Work>> work_;
    bool queued_ = false, closing_ = false, publicationAllowed_ = false;
    app::ShaderCompileResult lastResult_;
    std::shared_ptr<app::PreviewGpuProgram> program_;
    QString lastDirectory_;
    AssetPreviewWindow *preview_ = nullptr;
    QComboBox *techniques_ = nullptr;
    QComboBox *stages_ = nullptr;
    QComboBox *mode_ = nullptr;
    QLineEdit *name_ = nullptr;
    QLineEdit *material_ = nullptr;
    QCheckBox *automatic_ = nullptr;
    QComboBox *scope_ = nullptr;
    QLabel *status_ = nullptr;
    QPlainTextEdit *code_ = nullptr;
    QPlainTextEdit *layout_ = nullptr;
    QPlainTextEdit *reflection_ = nullptr;
    QListWidget *messages_ = nullptr;
    QTableWidget *parameters_ = nullptr;
    ShaderGraphWidget *graph_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    std::string previewNode_;
};

class TechsetPanel : public QWidget {
    Q_OBJECT
  public:
    TechsetPanel(TechsetWorkspace *workspace, std::function<QString()> folder, QWidget *parent = nullptr);
    void refresh();

  private:
    TechsetWorkspace *workspace_;
    std::function<QString()> folder_;
    QListWidget *list_;
    QLineEdit *search_;
};
} // namespace iw8r::qt
