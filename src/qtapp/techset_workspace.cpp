#include "techset_workspace.h"
#include "shader_graph_widget.h"
#include "asset_preview_window.h"
#include "camera_view.h"
#include "preview_gpu.h"
#include "panel_chrome.h"
#include "settings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QPainter>
#include <chrono>
#include <cmath>

namespace iw8r::qt {
namespace {
QString qs(const std::string &s) { return QString::fromStdString(s); }
void rememberTechset(const QString &path) {
    auto recent = Settings::instance().value("techsets/recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 20)
        recent.removeLast();
    Settings::instance().setValue("techsets/recent", recent);
}
QString read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}
QString recoveryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           "/techset_recovery/draft.iw8techset.json";
}
class HlslHighlight : public QSyntaxHighlighter {
  public:
    explicit HlslHighlight(QTextDocument *doc) : QSyntaxHighlighter(doc) {}
    void highlightBlock(const QString &text) override {
        const auto paint = [&](const QString &pattern, const QColor &color) {
            QRegularExpression re(pattern);
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), color);
            }
        };
        paint("\\b(float[1-4]?(x[1-4])?|int[1-4]?|uint[1-4]?|bool|void|struct|return|if|else|for|while|inout|const|"
              "static)\\b",
              QColor("#78aaf2"));
        paint("\\b(iw8r\\w*|IW8R\\w*)\\b", QColor("#71c5ad"));
        paint("\\b[0-9]+(\\.[0-9]+)?f?\\b", QColor("#d9ae73"));
        paint("//.*$", QColor("#7c8d78"));
        int start = previousBlockState() == 1 ? 0 : text.indexOf("/*");
        setCurrentBlockState(0);
        while (start >= 0) {
            int end = text.indexOf("*/", start + 2);
            if (end < 0) {
                setFormat(start, text.size() - start, QColor("#7c8d78"));
                setCurrentBlockState(1);
                break;
            }
            setFormat(start, end + 2 - start, QColor("#7c8d78"));
            start = text.indexOf("/*", end + 2);
        }
    }
};
class CodeEdit : public QPlainTextEdit {
  public:
    explicit CodeEdit(QWidget *parent) : QPlainTextEdit(parent) {
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setLineWrapMode(NoWrap);
        setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
        gutter_ = new QWidget(this);
        gutter_->installEventFilter(this);
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { margin(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
            if (dy)
                gutter_->scroll(0, dy);
            else
                gutter_->update(0, r.y(), gutter_->width(), r.height());
        });
        completer_ = new QCompleter(QStringList{"IW8RPreviewSurface",
                                                "IW8RPreviewVertex",
                                                "iw8rPreviewPixel",
                                                "iw8rPreviewVertex",
                                                "iw8rParameters",
                                                "iw8rTime",
                                                "iw8rPreviewSceneDepth",
                                                "float2",
                                                "float3",
                                                "float4",
                                                "normalize",
                                                "saturate",
                                                "lerp",
                                                "dot",
                                                "cross",
                                                "sin",
                                                "cos",
                                                "pow",
                                                "frac",
                                                "ddx",
                                                "ddy",
                                                "clip"},
                                    this);
        completer_->setWidget(this);
        completer_->setCaseSensitivity(Qt::CaseInsensitive);
        connect(completer_, QOverload<const QString &>::of(&QCompleter::activated), this, [this](const QString &word) {
            auto c = textCursor();
            c.select(QTextCursor::WordUnderCursor);
            c.insertText(word);
            setTextCursor(c);
        });
        new HlslHighlight(document());
        margin();
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (completer_->popup()->isVisible() && (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return ||
                                                 e->key() == Qt::Key_Tab || e->key() == Qt::Key_Escape)) {
            e->ignore();
            return;
        }
        if (e->key() == Qt::Key_Space && e->modifiers().testFlag(Qt::ControlModifier)) {
            auto c = textCursor();
            c.select(QTextCursor::WordUnderCursor);
            completer_->setCompletionPrefix(c.selectedText());
            completer_->complete(cursorRect());
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }
    void resizeEvent(QResizeEvent *e) override {
        QPlainTextEdit::resizeEvent(e);
        gutter_->setGeometry(0, 0, marginWidth_, height());
    }
    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == gutter_ && e->type() == QEvent::Paint) {
            QPainter p(gutter_);
            p.fillRect(gutter_->rect(), palette().alternateBase());
            p.setPen(palette().color(QPalette::PlaceholderText));
            auto block = firstVisibleBlock();
            int y = int(blockBoundingGeometry(block).translated(contentOffset()).top());
            while (block.isValid() && y < height()) {
                const int h = int(blockBoundingRect(block).height());
                if (block.isVisible())
                    p.drawText(0, y, marginWidth_ - 6, h, Qt::AlignRight | Qt::AlignVCenter,
                               QString::number(block.blockNumber() + 1));
                y += h;
                block = block.next();
            }
            return true;
        }
        return QPlainTextEdit::eventFilter(o, e);
    }

  private:
    void margin() {
        marginWidth_ = fontMetrics().horizontalAdvance(QString::number(std::max(1, blockCount()))) + 18;
        setViewportMargins(marginWidth_, 0, 0, 0);
        gutter_->setGeometry(0, 0, marginWidth_, height());
    }
    QWidget *gutter_;
    QCompleter *completer_;
    int marginWidth_ = 40;
};
} // namespace
struct TechsetWorkspace::Work {
    app::ShaderCompileResult result;
    std::shared_ptr<app::PreviewGpuProgram> gpu;
    QString error;
};
TechsetWorkspace::TechsetWorkspace(Hooks hooks, QWidget *parent) : QWidget(parent), hooks_(std::move(hooks)) {
    setObjectName("techsetWorkspace");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto *top = new QHBoxLayout;
    name_ = new QLineEdit(this);
    name_->setPlaceholderText("Techset name");
    top->addWidget(name_, 1);
    const auto button = [&](QHBoxLayout *row, const QString &text, auto fn) {
        auto *b = new QPushButton(text, this);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    };
    button(top, "New", [this] { newDocument(); });
    button(top, "Open...", [this] {
        const auto f = QFileDialog::getOpenFileName(this, "Open techset", lastDirectory_,
                                                    "Techsets (*.iw8techset.json *.asset.json)");
        if (!f.isEmpty()) {
            QString e;
            if (!openFile(f, &e))
                QMessageBox::warning(this, "Techset", e);
        }
    });
    button(top, "Save", [this] { save(); });
    button(top, "Float / expand", [this] {
        if (onExpand)
            onExpand();
    });
    root->addLayout(top);
    auto *select = new QHBoxLayout;
    techniques_ = new QComboBox(this);
    techniques_->setObjectName("techsetTechnique");
    select->addWidget(techniques_, 1);
    button(select, "Add technique", [this] { addTechnique(); });
    button(select, "Duplicate", [this] { addTechnique(true); });
    button(select, "Remove", [this] {
        const int i = techniques_->currentIndex();
        if (i < 0)
            return;
        document_.techniques.erase(document_.techniques.begin() + i);
        rebuildTechsetMask(document_);
        populate();
        changed();
    });
    stages_ = new QComboBox(this);
    stages_->setObjectName("techsetStage");
    stages_->addItems({"Vertex", "Pixel", "Hull (inspect)", "Domain (inspect)"});
    stages_->setCurrentIndex(1);
    select->addWidget(stages_);
    mode_ = new QComboBox(this);
    mode_->addItems({"HLSL", "Graph", "Imported bytecode"});
    mode_->setObjectName("techsetMode");
    select->addWidget(mode_);
    button(select, "Replace stage", [this] {
        auto *t = technique();
        if (!t)
            return;
        const auto value = ShaderStage(stages_->currentIndex());
        if (value == ShaderStage::Hull || value == ShaderStage::Domain)
            return;
        auto *s = t->findStage(value);
        if (!s) {
            t->stages.push_back({});
            s = &t->stages.back();
            s->stage = value;
        }
        s->mode = "hlsl";
        s->source = defaultPreviewSource(value);
        s->entry = value == ShaderStage::Vertex ? "iw8rPreviewVertex" : "iw8rPreviewPixel";
        loadStage();
        changed();
    });
    root->addLayout(select);
    auto *splitter = new QSplitter(this);
    tabs_ = new QTabWidget(splitter);
    graph_ = new ShaderGraphWidget(tabs_);
    tabs_->addTab(graph_, "Nodes");
    auto *codePage = new QWidget(tabs_);
    auto *codeLayout = new QVBoxLayout(codePage);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    code_ = new CodeEdit(codePage);
    code_->setObjectName("techsetHlsl");
    codeLayout->addWidget(code_);
    auto *detach = new QPushButton("Copy generated source into HLSL mode", codePage);
    codeLayout->addWidget(detach);
    connect(detach, &QPushButton::clicked, this, [this] {
        auto *s = stage();
        if (!s || s->mode != "graph")
            return;
        s->source = code_->toPlainText().toStdString();
        s->mode = "hlsl";
        loadStage();
        changed();
    });
    tabs_->addTab(codePage, "HLSL");
    auto *techniquePage = new QWidget(tabs_);
    auto *techniqueLayout = new QVBoxLayout(techniquePage);
    techniqueLayout->addWidget(new QLabel("Complete imported technique layout. Edit previewState for preview "
                                          "blend/depth/culling; native fields remain exact.",
                                          techniquePage));
    layout_ = new QPlainTextEdit(techniquePage);
    layout_->setObjectName("techsetLayout");
    layout_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    techniqueLayout->addWidget(layout_);
    auto *layoutButtons = new QHBoxLayout;
    button(layoutButtons, "Apply layout", [this] { applyLayout(); });
    button(layoutButtons, "Resources / parameters...", [this] { editBindings(); });
    techniqueLayout->addLayout(layoutButtons);
    parameters_ = new QTableWidget(techniquePage);
    parameters_->setObjectName("techsetParameters");
    parameters_->setColumnCount(5);
    parameters_->setHorizontalHeaderLabels({"Parameter", "X", "Y", "Z", "W"});
    parameters_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    parameters_->setMaximumHeight(180);
    techniqueLayout->addWidget(parameters_);
    tabs_->addTab(techniquePage, "Technique");
    auto *diagPage = new QWidget(tabs_);
    auto *diagLayout = new QVBoxLayout(diagPage);
    messages_ = new QListWidget(diagPage);
    messages_->setObjectName("techsetDiagnostics");
    diagLayout->addWidget(messages_);
    reflection_ = new QPlainTextEdit(diagPage);
    reflection_->setReadOnly(true);
    reflection_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diagLayout->addWidget(reflection_);
    tabs_->addTab(diagPage, "Diagnostics");
    preview_ = hooks_.makePreview ? hooks_.makePreview() : nullptr;
    if (preview_) {
        preview_->setParent(splitter);
        preview_->setWindowFlags(Qt::Widget);
        splitter->addWidget(preview_);
        preview_->useGameLighting();
        preview_->show();
    }
    splitter->insertWidget(0, tabs_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);
    auto *previewBar = new QHBoxLayout;
    button(previewBar, "Selection", [this] {
        QString error;
        if (!previewSelection(&error))
            status_->setText(error);
    });
    auto *shape = new QComboBox(this);
    shape->addItems({"Sphere", "Plane", "Cube"});
    previewBar->addWidget(shape);
    connect(shape, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (preview_)
            preview_->showShape(i);
        applyPreview();
    });
    button(previewBar, "Model...", [this] {
        bool ok = false;
        auto n = QInputDialog::getText(this, "Preview model", "Stock or custom model name", QLineEdit::Normal, {}, &ok);
        if (ok && !n.isEmpty())
            previewAsset(n, false);
    });
    button(previewBar, "Prefab...", [this] {
        auto n = QFileDialog::getOpenFileName(this, "Preview prefab", {}, "Prefabs (*.map)");
        if (!n.isEmpty())
            previewAsset(n, true);
    });
    material_ = new QLineEdit(this);
    material_->setPlaceholderText("Material slot (empty = all slots of target)");
    material_->setObjectName("techsetMaterialSlot");
    previewBar->addWidget(material_, 1);
    scope_ = new QComboBox(this);
    scope_->setObjectName("techsetPreviewScope");
    scope_->addItems({"Asset preview only", "Selected map targets + preview", "Material everywhere + preview"});
    scope_->setCurrentIndex(1);
    previewBar->addWidget(scope_);
    button(previewBar, "Apply to current selection", [this] { applyPreview(); });
    button(previewBar, "Clear overrides", [this] { clearOverrides(); });
    root->addLayout(previewBar);
    auto *bottom = new QHBoxLayout;
    automatic_ = new QCheckBox("Auto compile", this);
    automatic_->setChecked(true);
    bottom->addWidget(automatic_);
    button(bottom, "Compile", [this] { compileNow(); });
    status_ = new QLabel("Ready", this);
    status_->setObjectName("techsetCompileStatus");
    bottom->addWidget(status_, 1);
    button(bottom, "Recover draft", [this] { restoreRecovery(); });
    root->addLayout(bottom);
    debounce_.setSingleShot(true);
    debounce_.setInterval(350);
    connect(automatic_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled)
            debounce_.stop();
        else if (dirty_)
            debounce_.start();
    });
    connect(&debounce_, &QTimer::timeout, this, &TechsetWorkspace::compileNow);
    poll_.setInterval(20);
    connect(&poll_, &QTimer::timeout, this, &TechsetWorkspace::pollCompile);
    recoveryTimer_.setSingleShot(true);
    recoveryTimer_.setInterval(1500);
    connect(&recoveryTimer_, &QTimer::timeout, this, &TechsetWorkspace::recovery);
    connect(name_, &QLineEdit::textEdited, this, [this](const QString &n) {
        document_.name = n.toStdString();
        changed(false);
    });
    connect(techniques_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) {
            previewNode_.clear();
            loadStage();
            changed();
        }
    });
    connect(stages_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) {
            previewNode_.clear();
            loadStage();
        }
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (loading_)
            return;
        auto *s = stage();
        if (!s)
            return;
        if (i == 2) {
            loadStage();
            return;
        }
        s->mode = i == 1 ? "graph" : "hlsl";
        if (s->mode == "graph" && s->graph.nodes.empty())
            s->graph = newShaderGraph(s->stage);
        if (s->source.empty())
            s->source = defaultPreviewSource(s->stage);
        loadStage();
        changed();
    });
    connect(code_, &QPlainTextEdit::textChanged, this, [this] {
        if (!loading_) {
            if (auto *s = stage(); s && s->mode == "hlsl") {
                s->source = code_->toPlainText().toStdString();
                changed();
            }
        }
    });
    graph_->onChanged = [this](const ShaderGraph &g) {
        if (loading_)
            return;
        if (auto *s = stage()) {
            s->graph = g;
            s->mode = "graph";
            const auto r = compileShaderGraph(g, s->stage, document_.parameters, document_.resources);
            {
                QSignalBlocker b(code_);
                code_->setPlainText(qs(r.source));
            }
            graph_->setDiagnostics(r.diagnostics);
            changed();
        }
    };
    graph_->onPreview = [this](const std::string &node) {
        previewNode_ = node;
        ++revision_;
        compileNow();
    };
    connect(parameters_, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (loading_ || col < 1 || row < 0 || size_t(row) >= document_.parameters.size())
            return;
        bool ok = false;
        float value = parameters_->item(row, col)->text().toFloat(&ok);
        if (!ok || !std::isfinite(value))
            return;
        document_.parameters[size_t(row)].value[size_t(col - 1)] = value;
        changed(false);
        updateParameters();
    });
    connect(scope_, &QComboBox::currentIndexChanged, this, [this] { applyPreview(); });
    connect(messages_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const auto index = item->data(Qt::UserRole).toUInt();
        if (index >= lastResult_.diagnostics.size())
            return;
        const auto &d = lastResult_.diagnostics[index];
        if (!d.node.empty()) {
            tabs_->setCurrentIndex(0);
            graph_->focusNode(qs(d.node));
        } else {
            tabs_->setCurrentIndex(1);
            auto cursor = code_->textCursor();
            cursor.setPosition(code_->document()->findBlockByNumber(std::max(0, int(d.line) - 1)).position());
            code_->setTextCursor(cursor);
            code_->centerCursor();
        }
    });
    if (preview_) {
        preview_->onMaterialSelected = [this](const QString &m) { material_->setText(m); };
        preview_->showShape(0);
    }
    document_ = newTechsetDocument();
    populate();
}
TechsetWorkspace::~TechsetWorkspace() { shutdown(); }
void TechsetWorkspace::shutdown() {
    if (closing_)
        return;
    closing_ = true;
    debounce_.stop();
    poll_.stop();
    recoveryTimer_.stop();
    clearOverrides();
    if (dirty_)
        recovery();
    if (work_.valid())
        work_.wait();
    hooks_.mainView.clear();
    hooks_.refreshPreview = {};
}
TechniqueDefinition *TechsetWorkspace::technique() {
    const int i = techniques_->currentIndex();
    return i >= 0 && size_t(i) < document_.techniques.size() ? &document_.techniques[size_t(i)] : nullptr;
}
ShaderStageDocument *TechsetWorkspace::stage() {
    auto *t = technique();
    return t ? t->findStage(ShaderStage(stages_->currentIndex())) : nullptr;
}
void TechsetWorkspace::populate() {
    loading_ = true;
    name_->setText(qs(document_.name));
    techniques_->clear();
    for (const auto &t : document_.techniques)
        techniques_->addItem(qs(std::to_string(t.slot) + "  " + t.name));
    parameters_->setRowCount(int(document_.parameters.size()));
    for (size_t i = 0; i < document_.parameters.size(); ++i) {
        const auto &p = document_.parameters[i];
        auto *n = new QTableWidgetItem(qs(p.name));
        n->setFlags(n->flags() & ~Qt::ItemIsEditable);
        parameters_->setItem(int(i), 0, n);
        for (int k = 0; k < 4; ++k)
            parameters_->setItem(int(i), k + 1, new QTableWidgetItem(QString::number(p.value[size_t(k)], 'g', 8)));
    }
    loading_ = false;
    loadStage();
}
void TechsetWorkspace::loadStage() {
    loading_ = true;
    auto *t = technique();
    auto *s = stage();
    layout_->setPlainText(t ? qs(t->layout.dump(2)) : QString());
    code_->clear();
    reflection_->clear();
    mode_->setEnabled(s && s->stage != ShaderStage::Hull && s->stage != ShaderStage::Domain);
    if (s) {
        mode_->setCurrentIndex(s->mode == "graph" ? 1 : s->mode == "imported" ? 2 : 0);
        graph_->setGraph(s->graph, s->stage);
        if (s->mode == "graph")
            code_->setPlainText(
                qs(compileShaderGraph(s->graph, s->stage, document_.parameters, document_.resources).source));
        else
            code_->setPlainText(qs(s->source));
        code_->setReadOnly(s->mode != "hlsl" || s->stage == ShaderStage::Hull || s->stage == ShaderStage::Domain);
        graph_->setEnabled(s->mode == "graph");
        if (s->mode == "imported") {
            QString p = qs(s->bytecodePath);
            if (QFileInfo(p).isRelative())
                p = QFileInfo(path_).dir().filePath(p);
            QFile f(p);
            std::vector<uint8_t> bytes;
            if (!s->bytecodePath.empty() && f.open(QIODevice::ReadOnly)) {
                const auto b = f.readAll();
                bytes.assign(b.begin(), b.end());
            }
            reflection_->setPlainText("Imported stage: " + qs(s->reference) + "\n\n" +
                                      qs(app::disassemblePreviewShader(bytes)));
            tabs_->setCurrentIndex(3);
        }
    } else {
        graph_->setEnabled(false);
        code_->setReadOnly(true);
    }
    if (t && !t->previewLimitation().empty())
        status_->setText(qs(t->previewLimitation()));
    loading_ = false;
}
void TechsetWorkspace::changed(bool compile) {
    if (loading_)
        return;
    dirty_ = true;
    if (compile) {
        ++revision_;
        status_->setText("Stale — last successful preview retained");
        if (automatic_->isChecked())
            debounce_.start();
    }
    recoveryTimer_.start();
}
bool TechsetWorkspace::canClose() {
    if (!dirty_)
        return true;
    const auto answer = QMessageBox::question(this, "Unsaved techset", "Save changes to this techset?",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        save();
        return !dirty_;
    }
    dirty_ = false;
    recoveryTimer_.stop();
    QFile::remove(recoveryPath());
    return true;
}
void TechsetWorkspace::newDocument() {
    if (!canClose())
        return;
    clearOverrides();
    ++revision_;
    queued_ = false;
    document_ = newTechsetDocument();
    path_.clear();
    const auto folder = hooks_.mapFolder ? hooks_.mapFolder() : QString();
    lastDirectory_ = folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                      : QDir(folder).filePath("techsets");
    ++importGeneration_;
    dirty_ = false;
    populate();
    status_->setText("New techset");
    if (onOpen)
        onOpen();
}
bool TechsetWorkspace::setDocument(const TechsetDocument &d, QString *error) {
    std::string e;
    if (!validateTechsetDocument(d, e)) {
        if (error)
            *error = qs(e);
        return false;
    }
    document_ = d;
    ++revision_;
    populate();
    dirty_ = true;
    return true;
}
bool TechsetWorkspace::openFile(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const auto text = f.readAll().toStdString();
    TechsetDocument d;
    std::string e;
    const bool imported = path.endsWith(".asset.json", Qt::CaseInsensitive);
    if (!(imported ? importTechsetDocument(text, d, e) : readTechsetDocument(text, d, e))) {
        if (error)
            *error = qs(e);
        return false;
    }
    if (!canClose())
        return false;
    clearOverrides();
    ++revision_;
    queued_ = false;
    document_ = std::move(d);
    path_ = imported ? QString() : path;
    lastDirectory_ = QFileInfo(path).absolutePath();
    dirty_ = imported;
    if (imported) {
        const auto manifest = read(path + ".programs.json");
        try {
            const auto programs = TechsetJson::parse(manifest.toStdString());
            for (auto &t : document_.techniques)
                for (auto &s : t.stages)
                    if (programs.contains(s.reference))
                        s.bytecodePath = programs.at(s.reference).get<std::string>();
        } catch (...) {
        }
    }
    populate();
    rememberTechset(path);
    if (onSaved)
        onSaved();
    status_->setText(imported ? "Imported layout; replace stages to author preview HLSL"
                              : "Opened " + QFileInfo(path).fileName());
    if (onOpen)
        onOpen();
    return true;
}
bool TechsetWorkspace::saveFile(const QString &path, QString *error) {
    std::string text, e;
    auto savedDocument = document_;
    const QDir oldBase(path_.isEmpty() ? lastDirectory_ : QFileInfo(path_).absolutePath());
    const QDir newBase(QFileInfo(path).absolutePath());
    const auto rebase = [&](std::string &value) {
        if (value.empty())
            return;
        const auto name = qs(value);
        const auto absolute = QFileInfo(name).isAbsolute() ? name : oldBase.absoluteFilePath(name);
        value = newBase.relativeFilePath(absolute).toStdString();
    };
    for (auto &resource : savedDocument.resources)
        rebase(resource.path);
    for (auto &t : savedDocument.techniques)
        for (auto &s : t.stages)
            rebase(s.bytecodePath);
    if (!writeTechsetDocument(savedDocument, text, e)) {
        if (error)
            *error = qs(e);
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromStdString(text)) != qint64(text.size()) ||
        !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    document_ = std::move(savedDocument);
    path_ = path;
    lastDirectory_ = QFileInfo(path).absolutePath();
    dirty_ = false;
    QFile::remove(recoveryPath());
    rememberTechset(path);
    if (onSaved)
        onSaved();
    return true;
}
void TechsetWorkspace::save() {
    QString path = path_;
    if (path.isEmpty()) {
        QString folder = hooks_.mapFolder ? hooks_.mapFolder() : lastDirectory_;
        if (!folder.isEmpty())
            folder = QDir(folder).filePath("techsets");
        path = QFileDialog::getSaveFileName(this, "Save preview techset",
                                            QDir(folder).filePath(qs(document_.name) + ".iw8techset.json"),
                                            "Techsets (*.iw8techset.json)");
    }
    if (path.isEmpty())
        return;
    if (!path.endsWith(".iw8techset.json", Qt::CaseInsensitive))
        path += ".iw8techset.json";
    QString e;
    if (!saveFile(path, &e))
        QMessageBox::warning(this, "Save techset", e);
}
void TechsetWorkspace::recovery() {
    if (!dirty_)
        return;
    std::string text, error;
    auto draft = document_;
    const QDir base(path_.isEmpty() ? lastDirectory_ : QFileInfo(path_).absolutePath());
    for (auto &resource : draft.resources)
        if (!resource.path.empty())
            resource.path = base.absoluteFilePath(qs(resource.path)).toStdString();
    for (auto &t : draft.techniques)
        for (auto &s : t.stages)
            if (!s.bytecodePath.empty())
                s.bytecodePath = base.absoluteFilePath(qs(s.bytecodePath)).toStdString();
    if (!writeTechsetDocument(draft, text, error))
        return;
    const auto path = recoveryPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(QByteArray::fromStdString(text)) == qint64(text.size()))
        f.commit();
}
void TechsetWorkspace::restoreRecovery() {
    const auto saved = recoveryPath();
    if (!QFileInfo::exists(saved)) {
        status_->setText("No recovery draft");
        return;
    }
    QString e;
    if (openFile(saved, &e)) {
        path_.clear();
        dirty_ = true;
        status_->setText("Recovered unsaved draft");
    } else if (!e.isEmpty())
        status_->setText(e);
}
void TechsetWorkspace::setStageSource(ShaderStage value, const QString &source) {
    auto *t = technique();
    if (!t)
        return;
    auto *s = t->findStage(value);
    if (!s)
        return;
    s->mode = "hlsl";
    s->source = source.toStdString();
    loadStage();
    changed();
}
bool TechsetWorkspace::compiling() const { return work_.valid(); }
QString TechsetWorkspace::compileStatus() const { return status_->text(); }
QString TechsetWorkspace::selectedTechnique() const {
    const int i = techniques_->currentIndex();
    return i >= 0 && size_t(i) < document_.techniques.size() ? qs(document_.techniques[size_t(i)].id) : QString();
}
void TechsetWorkspace::selectTechnique(const QString &id) {
    for (size_t i = 0; i < document_.techniques.size(); ++i)
        if (qs(document_.techniques[i].id) == id) {
            techniques_->setCurrentIndex(int(i));
            break;
        }
}
void TechsetWorkspace::compileNow() {
    debounce_.stop();
    if (closing_)
        return;
    publicationAllowed_ = true;
    if (work_.valid()) {
        queued_ = true;
        return;
    }
    auto *t = technique();
    if (!t) {
        status_->setText("Choose a technique");
        return;
    }
    if (preview_ && hooks_.refreshPreview)
        hooks_.refreshPreview(preview_);
    app::ShaderCompileRequest req;
    req.revision = revision_;
    req.document = document_;
    req.techniqueId = t->id;
    req.graphPreviewNode = previewNode_;
    req.graphPreviewStage = ShaderStage(stages_->currentIndex());
    const std::string directory = (path_.isEmpty() ? lastDirectory_ : QFileInfo(path_).absolutePath()).toStdString();
    Microsoft::WRL::ComPtr<ID3D11Device> device = hooks_.device;
    const std::string geometry = app::SceneRenderer::texShaderSource(),
                      particles = app::SceneRenderer::particlePreviewSource();
    status_->setText("Compiling in background...");
    work_ = std::async(std::launch::async, [req = std::move(req), directory, device, geometry, particles] {
        auto w = std::make_shared<Work>();
        w->result = app::compilePreviewShaders(req, geometry, particles);
        if (w->result.ok) {
            w->gpu = std::make_shared<app::PreviewGpuProgram>();
            std::string error;
            if (!w->gpu->create(device.Get(), w->result, req.document, req.techniqueId, directory, error)) {
                w->gpu.reset();
                w->result.ok = false;
                w->result.diagnostics.push_back({error});
            }
        }
        return w;
    });
    poll_.start();
}
void TechsetWorkspace::pollCompile() {
    if (!work_.valid() || work_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    std::shared_ptr<Work> completed;
    try {
        completed = work_.get();
    } catch (const std::exception &e) {
        status_->setText("Compile worker failed: " + QString::fromUtf8(e.what()));
    }
    poll_.stop();
    if (completed && completed->result.revision == revision_ && !closing_ && publicationAllowed_) {
        lastResult_ = std::move(completed->result);
        messages_->clear();
        for (size_t i = 0; i < lastResult_.diagnostics.size(); ++i) {
            const auto &d = lastResult_.diagnostics[i];
            auto *row = new QListWidgetItem(qs((d.warning ? "Warning: " : "Error: ") + d.file +
                                               (d.line ? ":" + std::to_string(d.line) : "") + " " + d.message),
                                            messages_);
            row->setData(Qt::UserRole, uint(i));
        }
        graph_->setDiagnostics(lastResult_.diagnostics);
        if (lastResult_.ok) {
            program_ = std::move(completed->gpu);
            updateParameters();
            applyPreview();
            status_->setText(QString("Current — compiled in %1 ms").arg(lastResult_.milliseconds, 0, 'f', 0));
            QString details;
            for (const auto &s : lastResult_.stages) {
                details += qs(s.adapter + " / " + s.entry + " / " + s.target) + "\n";
                for (const auto &b : s.bindings)
                    details += qs("  " + b.name + " " + b.kind + " slot " + std::to_string(b.slot)) + "\n";
            }
            reflection_->setPlainText(details);
        } else
            status_->setText("Compile failed — last successful preview retained");
    }
    if (publicationAllowed_ &&
        (queued_ || (completed && completed->result.revision != revision_ && automatic_->isChecked()))) {
        queued_ = false;
        compileNow();
    }
}
void TechsetWorkspace::applyPreview() {
    if (!program_)
        return;
    app::PreviewOverrideSet targets;
    targets.enabled = true;
    targets.all = true;
    targets.material = material_->text().trimmed().toStdString();
    if (preview_)
        preview_->view()->setPreviewProgram(program_, targets);
    if (hooks_.mainView) {
        if (scope_->currentIndex() == 0)
            hooks_.mainView->setPreviewProgram({}, {});
        else {
            targets = hooks_.selectedTargets
                          ? hooks_.selectedTargets(material_->text().trimmed().toStdString())
                          : hooks_.mainView->selectedPreviewTargets(material_->text().trimmed().toStdString());
            if (scope_->currentIndex() == 2) {
                if (targets.material.empty()) {
                    status_->setText("Choose a material before applying a material-wide override");
                    return;
                }
                targets.materialWide = true;
            }
            hooks_.mainView->setPreviewProgram(program_, targets);
        }
    }
}
void TechsetWorkspace::updateParameters() {
    std::lock_guard lock(gpuMutex());
    if (program_)
        program_->parameters(document_.parameters);
    if (preview_)
        preview_->view()->update();
    if (hooks_.mainView)
        hooks_.mainView->update();
}
void TechsetWorkspace::clearOverrides() {
    debounce_.stop();
    queued_ = false;
    publicationAllowed_ = false;
    ++revision_;
    if (preview_)
        preview_->view()->setPreviewProgram({}, {});
    if (hooks_.mainView)
        hooks_.mainView->setPreviewProgram({}, {});
    program_.reset();
}
void TechsetWorkspace::previewAsset(const QString &name, bool prefab) {
    if (preview_) {
        if (prefab) {
            QString e;
            if (!preview_->showPrefab(name, &e))
                status_->setText(e);
        } else
            preview_->showModel(name);
        applyPreview();
    }
    if (onOpen)
        onOpen();
}
void TechsetWorkspace::previewEffect(const QString &name) {
    if (preview_) {
        preview_->showEffect(name);
        applyPreview();
    }
    if (onOpen)
        onOpen();
}
bool TechsetWorkspace::previewSelection(QString *error) {
    if (!preview_ || !hooks_.previewSelection || !hooks_.previewSelection(preview_, error))
        return false;
    applyPreview();
    if (onOpen)
        onOpen();
    return true;
}
void TechsetWorkspace::openMaterial(const QString &name) {
    material_->setText(name);
    if (onOpen)
        onOpen();
    if (!hooks_.importMaterial)
        return;
    status_->setText("Reading stock technique set...");
    const auto generation = ++importGeneration_;
    hooks_.importMaterial(name, this, [this, generation](const QString &file, const QString &error) {
        if (generation != importGeneration_)
            return;
        if (!error.isEmpty()) {
            status_->setText(error);
            return;
        }
        QString e;
        if (!openFile(file, &e))
            status_->setText(e);
    });
}
void TechsetWorkspace::openImage(const QString &name) {
    if (onOpen)
        onOpen();
    if (!hooks_.requestImage)
        return;
    const auto generation = ++importGeneration_;
    status_->setText("Reading image...");
    hooks_.requestImage(name, this, [this, name, generation](const QString &file, const QString &error) {
        if (generation != importGeneration_)
            return;
        if (!error.isEmpty() || file.isEmpty()) {
            status_->setText(error);
            return;
        }
        if (!canClose())
            return;
        clearOverrides();
        document_ = newTechsetDocument();
        document_.name = "image_preview";
        path_.clear();
        lastDirectory_ = QFileInfo(file).absolutePath();
        document_.resources = {{"Texture", "texture2d", file.toStdString(), 0, true, "linear", "wrap"}};
        auto *pixel = document_.techniques[0].findStage(ShaderStage::Pixel);
        pixel->mode = "hlsl";
        pixel->source =
            "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return iw8rTexture0.Sample(iw8rSampler0,s.uv); }\n";
        populate();
        if (preview_)
            preview_->showShape(1);
        material_->clear();
        dirty_ = true;
        ++revision_;
        compileNow();
    });
}
void TechsetWorkspace::applyLayout() {
    auto *t = technique();
    if (!t)
        return;
    try {
        const auto j = TechsetJson::parse(layout_->toPlainText().toStdString());
        if (!j.is_object())
            throw std::runtime_error("Layout must be an object.");
        t->layout = j;
        changed();
    } catch (const std::exception &e) {
        status_->setText(QString::fromUtf8(e.what()));
    }
}
void TechsetWorkspace::editBindings() {
    std::string text, error;
    if (!writeTechsetDocument(document_, text, error))
        return;
    auto j = TechsetJson::parse(text);
    TechsetJson bindings = {{"resources", j["resources"]}, {"parameters", j["parameters"]}};
    QDialog dialog(this);
    dialog.setWindowTitle("Preview bindings");
    dialog.resize(760, 550);
    auto *root = new QVBoxLayout(&dialog);
    auto *help = new QLabel("Resources: name, kind (texture2d/texturecube), path, slot 0–7, srgb, filter, address. "
                            "Parameters: name, slot 0–63, value [x,y,z,w]. Cube images use a six-face strip.",
                            &dialog);
    help->setWordWrap(true);
    root->addWidget(help);
    auto *edit = new CodeEdit(&dialog);
    edit->setPlainText(qs(bindings.dump(2)));
    root->addWidget(edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    try {
        auto b = TechsetJson::parse(edit->toPlainText().toStdString());
        j["resources"] = b.at("resources");
        j["parameters"] = b.at("parameters");
        TechsetDocument d;
        if (!readTechsetDocument(j.dump(), d, error))
            throw std::runtime_error(error);
        document_ = std::move(d);
        populate();
        changed();
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Bindings", QString::fromUtf8(e.what()));
    }
}
void TechsetWorkspace::addTechnique(bool duplicate) {
    bool ok = false;
    QStringList choices = {"34 Lit surface", "30 Emissive / unlit", "0 Depth prepass", "27 Shadow depth",
                           "38 Wireframe"};
    auto choice = QInputDialog::getItem(this, "Add technique", "Preview technique", choices, 0, false, &ok);
    if (!ok)
        return;
    const auto slot = choice.section(' ', 0, 0).toUInt();
    for (const auto &t : document_.techniques)
        if (t.slot == slot) {
            status_->setText("That technique slot already exists");
            return;
        }
    auto t = duplicate && technique() ? *technique() : newTechsetDocument().techniques.front();
    t.id = "technique_" + std::to_string(slot);
    t.slot = slot;
    t.name = choice.section(' ', 1).toStdString();
    t.layout["type"]["value"] = slot;
    t.layout["type"]["name"] = slot == 0    ? "TECHNIQUE_DEPTH_PREPASS"
                               : slot == 27 ? "TECHNIQUE_BUILD_SHADOWMAP_DEPTH"
                               : slot == 30 ? "TECHNIQUE_EMISSIVE"
                               : slot == 38 ? "TECHNIQUE_WIREFRAME_SOLID"
                                            : "TECHNIQUE_LIT_FORWARDPLUS_BITMASK";
    document_.techniques.push_back(t);
    rebuildTechsetMask(document_);
    populate();
    techniques_->setCurrentIndex(int(document_.techniques.size() - 1));
    changed();
}

TechsetPanel::TechsetPanel(TechsetWorkspace *workspace, std::function<QString()> folder, QWidget *parent)
    : QWidget(parent), workspace_(workspace), folder_(std::move(folder)) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *header = new PanelHeader("Techsets", this);
    root->addWidget(header);
    auto *add = header->addButton(glyph::Add, "New techset");
    connect(add, &QToolButton::clicked, this, [this] { workspace_->newDocument(); });
    auto *open = header->addButton(glyph::Folder, "Open techset");
    connect(open, &QToolButton::clicked, this, [this] {
        const auto f = QFileDialog::getOpenFileName(this, "Open techset", folder_ ? folder_() : QString(),
                                                    "Techsets (*.iw8techset.json *.asset.json)");
        if (!f.isEmpty()) {
            QString e;
            if (!workspace_->openFile(f, &e))
                QMessageBox::warning(this, "Techset", e);
        }
    });
    auto *reload = header->addButton(glyph::Refresh, "Refresh project techsets");
    connect(reload, &QToolButton::clicked, this, &TechsetPanel::refresh);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search techsets...");
    root->addWidget(search_);
    list_ = new QListWidget(this);
    list_->setObjectName("techsetProjectList");
    root->addWidget(list_, 1);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < list_->count(); ++i)
            list_->item(i)->setHidden(!list_->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *row) {
        QString e;
        if (!workspace_->openFile(row->data(Qt::UserRole).toString(), &e))
            QMessageBox::warning(this, "Techset", e);
    });
    auto *technique = new QComboBox(this);
    technique->setObjectName("techsetSidebarTechnique");
    root->addWidget(technique);
    auto *status = new QLabel(this);
    status->setWordWrap(true);
    root->addWidget(status);
    connect(technique, &QComboBox::activated, this,
            [this, technique](int i) { workspace_->selectTechnique(technique->itemData(i).toString()); });
    auto *poll = new QTimer(this);
    poll->setInterval(250);
    connect(poll, &QTimer::timeout, this, [this, technique, status] {
        if (!isVisible())
            return;
        status->setText(workspace_->compileStatus());
        const auto &definitions = workspace_->document().techniques;
        bool differs = technique->count() != int(definitions.size());
        for (int i = 0; !differs && i < technique->count(); ++i)
            differs = technique->itemData(i).toString() != qs(definitions[size_t(i)].id) ||
                      technique->itemText(i) != qs(definitions[size_t(i)].name);
        const QSignalBlocker blocked(technique);
        if (differs) {
            technique->clear();
            for (const auto &t : definitions)
                technique->addItem(qs(t.name), qs(t.id));
        }
        technique->setCurrentIndex(technique->findData(workspace_->selectedTechnique()));
    });
    poll->start();
    auto *edit = new QPushButton("Open shader workspace", this);
    root->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, [this] {
        if (workspace_->onOpen)
            workspace_->onOpen();
    });
    auto *info =
        new QLabel("Live vertex/pixel authoring for editor previews. Saved techsets do not change game builds.", this);
    info->setWordWrap(true);
    root->addWidget(info);
    workspace_->onSaved = [this] { refresh(); };
    refresh();
}
void TechsetPanel::refresh() {
    list_->clear();
    const QString folder = folder_ ? folder_() : QString();
    QStringList files;
    if (!folder.isEmpty()) {
        QDir dir(QDir(folder).filePath("techsets"));
        for (const auto &file : dir.entryList({"*.iw8techset.json"}, QDir::Files, QDir::Name))
            files << dir.absoluteFilePath(file);
    }
    files << Settings::instance().value("techsets/recent").toStringList();
    files.removeDuplicates();
    for (const auto &file : files) {
        if (!QFileInfo::exists(file))
            continue;
        auto *row = new QListWidgetItem(QFileInfo(file).fileName(), list_);
        row->setData(Qt::UserRole, file);
        row->setToolTip(file);
        row->setHidden(!row->text().contains(search_->text(), Qt::CaseInsensitive));
    }
}
} // namespace iw8r::qt
