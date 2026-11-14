// IW8 Radiant Suite - Tools > Shader Editor (plan M6, owner 2026-11-13: "edit stock shaders and make custom shaders
// for maps").
//
// Lists the map's shaders (<map folder>/shaders/*.iw8shader.hlsl, editable) and the editor's (data/shaders, read-only
// templates: "stock" is the game's own surface lighting, "toon" the anime cel example). New makes a map shader from a
// template; Check compiles it into the viewport's Game Lighting shader and reports errors with the file's own line
// numbers; Save writes it (the viewport picks it up at once); Use for This Map sets worldspawn iw8r_surface_shader.
// Editing an editor shader saves a copy in the map's folder (the editor's files are never changed).
#pragma once

#include <QDialog>
#include <QString>
#include <functional>

class QListWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;

namespace iw8r::qt {

class ShaderEditorDialog : public QDialog {
public:
    // mapShaderDir: <map folder>/shaders ("" when the map is not saved: map shaders cannot be made then).
    // editorShaderDir: the editor's data/shaders. currentShader: worldspawn iw8r_surface_shader ("" none).
    ShaderEditorDialog(const QString& mapShaderDir, const QString& editorShaderDir, const QString& currentShader,
                       QWidget* parent = nullptr);

    std::function<void(const QString& name)> onUse;  // set the map's surface shader ("" = none)
    std::function<void()> onSaved;                   // a shader file changed: refresh the viewport

    // Compiles shader code as the viewport would (core checks + the Game Lighting shader); "" when it is fine, else
    // the errors (file-relative line numbers). Static so tests can call it.
    static QString checkCode(const QString& name, const QString& code);

    // Selects and loads a shader by name (tests); false when not listed.
    bool selectShader(const QString& name);
    QString currentCode() const;
    void setCode(const QString& code);
    bool saveCurrent(QString* err = nullptr);
    QString statusText() const;

private:
    void refreshList(const QString& select = {});
    void loadSelected();
    void check();
    void newFromTemplate();
    void useForMap();
    QString selectedPath() const;
    bool selectedIsMap() const;

    QString mapDir_, editorDir_, current_;
    QString loadedPath_;
    QListWidget* list_ = nullptr;
    QPlainTextEdit* code_ = nullptr;
    QPlainTextEdit* messages_ = nullptr;
    QLabel* title_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* use_ = nullptr;
    bool loading_ = false;
};

}  // namespace iw8r::qt
