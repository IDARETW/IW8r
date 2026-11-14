#include "shader_editor.h"

#include "custom_shader.h"
#include "render.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QVBoxLayout>

namespace iw8r::qt {

namespace {

constexpr const char* kSuffix = ".iw8shader.hlsl";

QString shaderName(const QString& file) {
    QString n = QFileInfo(file).fileName();
    if (n.endsWith(kSuffix)) n.chop(int(qstrlen(kSuffix)));
    return n;
}

QString readText(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

bool validName(const QString& n) {
    static const QRegularExpression re("^[a-z0-9_]{1,48}$");
    return re.match(n).hasMatch();
}

}  // namespace

QString ShaderEditorDialog::checkCode(const QString& name, const QString& code) {
    std::string err;
    if (!checkCustomShaderCode(code.toStdString(), &err)) return QString::fromStdString(err);
    CustomShader s;
    s.name = name.toStdString();
    s.code = code.toStdString();
    const std::string src =
        customShaderViewportSource(app::SceneRenderer::texShaderSource(), "float4 ps_tex(PSIn i) : SV_TARGET {", s);
    if (src.empty()) return "the viewport shader has no entry point to add the function to";
    if (!app::SceneRenderer::compileCheck(src, &err)) {
        // Only the lines about the shader's own file (the #line marks them), else everything.
        QStringList own;
        const QString file = name + kSuffix;
        for (const QString& l : QString::fromStdString(err).split('\n', Qt::SkipEmptyParts))
            if (l.contains(file)) own << l.mid(l.indexOf(file));
        return own.isEmpty() ? QString::fromStdString(err).trimmed() : own.join('\n');
    }
    return {};
}

ShaderEditorDialog::ShaderEditorDialog(const QString& mapShaderDir, const QString& editorShaderDir,
                                       const QString& currentShader, QWidget* parent)
    : QDialog(parent), mapDir_(mapShaderDir), editorDir_(editorShaderDir), current_(currentShader) {
    setWindowTitle("Shader Editor");
    resize(1100, 720);
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(this);
    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(new QLabel("Map shaders (editable) and the editor's templates:", left));
    list_ = new QListWidget(left);
    ll->addWidget(list_, 1);
    auto* newBtn = new QPushButton("New...", left);
    newBtn->setToolTip("A new map shader from a template (Stock lighting is the game's own surface lighting)");
    ll->addWidget(newBtn);
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    title_ = new QLabel(right);
    rl->addWidget(title_);
    code_ = new QPlainTextEdit(right);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    code_->setFont(mono);
    code_->setLineWrapMode(QPlainTextEdit::NoWrap);
    code_->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(' '));
    rl->addWidget(code_, 4);
    messages_ = new QPlainTextEdit(right);
    messages_->setReadOnly(true);
    messages_->setFont(mono);
    messages_->setMaximumHeight(140);
    rl->addWidget(messages_, 1);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);
    auto* buttons = new QHBoxLayout;
    auto* checkBtn = new QPushButton("Check", this);
    save_ = new QPushButton("Save", this);
    use_ = new QPushButton("Use for This Map", this);
    auto* noneBtn = new QPushButton("No Map Shader", this);
    auto* close = new QPushButton("Close", this);
    buttons->addWidget(checkBtn);
    buttons->addWidget(save_);
    buttons->addStretch(1);
    buttons->addWidget(use_);
    buttons->addWidget(noneBtn);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { loadSelected(); });
    connect(newBtn, &QPushButton::clicked, this, [this] { newFromTemplate(); });
    connect(checkBtn, &QPushButton::clicked, this, [this] { check(); });
    connect(save_, &QPushButton::clicked, this, [this] {
        QString err;
        if (!saveCurrent(&err)) messages_->setPlainText(err);
    });
    connect(use_, &QPushButton::clicked, this, [this] { useForMap(); });
    connect(noneBtn, &QPushButton::clicked, this, [this] {
        if (onUse) onUse(QString());
        current_.clear();
        refreshList(shaderName(selectedPath()));
        messages_->setPlainText("The map's surfaces use the game's own lighting again (worldspawn iw8r_surface_shader removed).");
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    refreshList(current_);
    if (mapDir_.isEmpty())
        messages_->setPlainText("Save the map first to make shaders for it (they go into its shaders folder). The "
                                "editor's templates can be read here.");
}

void ShaderEditorDialog::refreshList(const QString& select) {
    loading_ = true;
    list_->clear();
    int pick = -1;
    const auto add = [&](const QString& dir, bool map) {
        if (dir.isEmpty()) return;
        const QStringList files = QDir(dir).entryList({QString("*") + kSuffix}, QDir::Files, QDir::Name);
        for (const QString& f : files) {
            const QString n = shaderName(f);
            auto* it = new QListWidgetItem(n + (map ? QString() : QString("  (editor template)")) +
                                           (n == current_ && map ? QString("  - used by this map") : QString()));
            it->setData(Qt::UserRole, QDir(dir).filePath(f));
            it->setData(Qt::UserRole + 1, map);
            list_->addItem(it);
            if (pick < 0 && n == select) pick = list_->count() - 1;
        }
    };
    add(mapDir_, true);
    add(editorDir_, false);
    loading_ = false;
    if (list_->count()) list_->setCurrentRow(pick >= 0 ? pick : 0);
    loadSelected();
}

QString ShaderEditorDialog::selectedPath() const {
    const QListWidgetItem* it = list_->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QString();
}

bool ShaderEditorDialog::selectedIsMap() const {
    const QListWidgetItem* it = list_->currentItem();
    return it && it->data(Qt::UserRole + 1).toBool();
}

void ShaderEditorDialog::loadSelected() {
    if (loading_) return;
    const QString path = selectedPath();
    if (path == loadedPath_) return;
    loadedPath_ = path;
    code_->setPlainText(path.isEmpty() ? QString() : readText(path));
    const bool map = selectedIsMap();
    code_->setReadOnly(!map);
    save_->setEnabled(map);
    use_->setEnabled(!path.isEmpty() && !mapDir_.isEmpty());
    title_->setText(path.isEmpty() ? QString("No shader")
                                   : (map ? "Map shader: " : "Editor template (read-only; New makes an editable copy): ") +
                                         QDir::toNativeSeparators(path));
}

bool ShaderEditorDialog::selectShader(const QString& name) {
    for (int i = 0; i < list_->count(); ++i)
        if (shaderName(list_->item(i)->data(Qt::UserRole).toString()) == name) {
            list_->setCurrentRow(i);
            loadSelected();
            return true;
        }
    return false;
}

QString ShaderEditorDialog::currentCode() const { return code_->toPlainText(); }
void ShaderEditorDialog::setCode(const QString& code) { code_->setPlainText(code); }
QString ShaderEditorDialog::statusText() const { return messages_->toPlainText(); }

void ShaderEditorDialog::check() {
    const QString err = checkCode(shaderName(selectedPath()), code_->toPlainText());
    messages_->setPlainText(err.isEmpty() ? QString("OK: compiles into the viewport's Game Lighting shader. The build "
                                                    "compiles it into the map's surface shaders (F7).")
                                          : err);
}

bool ShaderEditorDialog::saveCurrent(QString* err) {
    const QString path = selectedPath();
    if (path.isEmpty() || !selectedIsMap()) {
        if (err) *err = "Only map shaders are saved: New makes an editable copy of a template.";
        return false;
    }
    const QString problem = checkCode(shaderName(path), code_->toPlainText());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = "Cannot write " + QDir::toNativeSeparators(path);
        return false;
    }
    f.write(code_->toPlainText().toUtf8());
    f.close();
    messages_->setPlainText(problem.isEmpty() ? "Saved; the viewport shows it in Game Lighting."
                                              : "Saved, but it does not compile (the viewport keeps the game's lighting "
                                                "until it does):\n" + problem);
    if (onSaved) onSaved();
    return true;
}

void ShaderEditorDialog::newFromTemplate() {
    if (mapDir_.isEmpty()) {
        messages_->setPlainText("Save the map first: its shaders go into its shaders folder.");
        return;
    }
    QStringList templates;
    for (const QString& f : QDir(editorDir_).entryList({QString("*") + kSuffix}, QDir::Files, QDir::Name)) templates << shaderName(f);
    if (templates.isEmpty()) {
        messages_->setPlainText("No templates in " + QDir::toNativeSeparators(editorDir_));
        return;
    }
    int at = templates.indexOf("stock");
    if (!selectedIsMap() && !selectedPath().isEmpty()) at = templates.indexOf(shaderName(selectedPath()));
    bool ok = false;
    const QString from = QInputDialog::getItem(this, "New Shader", "Start from (stock = the game's own surface lighting):",
                                               templates, std::max(0, at), false, &ok);
    if (!ok) return;
    QString name = QInputDialog::getText(this, "New Shader", "Name (a-z 0-9 _):", QLineEdit::Normal, "my_" + from, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!validName(name)) {
        messages_->setPlainText("Names are lower-case letters, digits and _ (up to 48).");
        return;
    }
    const QString path = QDir(mapDir_).filePath(name + kSuffix);
    if (QFileInfo::exists(path)) {
        messages_->setPlainText(name + " already exists in the map's shaders folder.");
        return;
    }
    QDir().mkpath(mapDir_);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        messages_->setPlainText("Cannot write " + QDir::toNativeSeparators(path));
        return;
    }
    f.write(readText(QDir(editorDir_).filePath(from + kSuffix)).toUtf8());
    f.close();
    loadedPath_.clear();
    refreshList(name);
    messages_->setPlainText("New map shader " + name + " from " + from + ". Edit it, Check, Save, then Use for This Map.");
}

void ShaderEditorDialog::useForMap() {
    const QString path = selectedPath();
    if (path.isEmpty() || mapDir_.isEmpty()) return;
    const QString n = shaderName(path);
    const QString problem = checkCode(n, readText(path));
    if (!problem.isEmpty()) {
        messages_->setPlainText("Not used: the saved file does not compile:\n" + problem);
        return;
    }
    if (onUse) onUse(n);
    current_ = n;
    loadedPath_.clear();
    refreshList(n);
    messages_->setPlainText("The map's own surfaces now use " + n +
                            " (worldspawn iw8r_surface_shader) in Game Lighting and in the build. Undo removes it.");
}

}  // namespace iw8r::qt
