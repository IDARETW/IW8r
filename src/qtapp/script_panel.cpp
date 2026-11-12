#include "script_panel.h"
#include <QTextCursor>

#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QStringListModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include "editor_context.h"
#include "script_check.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

const QStringList& gscKeywords() {
    static const QStringList k = {"if", "else", "for", "foreach", "in", "while", "do", "switch", "case", "default", "break", "continue",
                                  "return", "wait", "waittill", "waittillmatch", "waittillframeend", "endon", "notify", "thread",
                                  "childthread", "self", "level", "game", "anim", "true", "false", "undefined", "isdefined", "size"};
    return k;
}

// Colours that read on both the dark and the light theme.
class GscHighlighter : public QSyntaxHighlighter {
public:
    GscHighlighter(QTextDocument* doc, QSet<QString> builtins) : QSyntaxHighlighter(doc), builtins_(std::move(builtins)) {
        keyword_.setForeground(QColor(0x56, 0x9c, 0xd6));
        keyword_.setFontWeight(QFont::Bold);
        builtin_.setForeground(QColor(0x4e, 0xc9, 0xb0));
        string_.setForeground(QColor(0xce, 0x91, 0x78));
        comment_.setForeground(QColor(0x6a, 0x99, 0x55));
        number_.setForeground(QColor(0xb5, 0xce, 0xa8));
        for (const QString& k : gscKeywords()) keywords_.insert(k);
    }

protected:
    void highlightBlock(const QString& text) override {
        int i = 0;
        const int n = int(text.size());
        bool inComment = previousBlockState() == 1;
        while (i < n) {
            if (inComment) {
                const int end = int(text.indexOf("*/", i));
                if (end < 0) {
                    setFormat(i, n - i, comment_);
                    i = n;
                } else {
                    setFormat(i, end + 2 - i, comment_);
                    i = end + 2;
                    inComment = false;
                }
                continue;
            }
            const QChar c = text[i];
            if (c == '/' && i + 1 < n && text[i + 1] == '/') {
                setFormat(i, n - i, comment_);
                break;
            }
            if (c == '/' && i + 1 < n && text[i + 1] == '*') {
                inComment = true;
                continue;
            }
            if (c == '"') {
                int j = i + 1;
                while (j < n && text[j] != '"') j += text[j] == '\\' ? 2 : 1;
                setFormat(i, std::min(j + 1, n) - i, string_);
                i = j + 1;
                continue;
            }
            if (c.isDigit() || (c == '.' && i + 1 < n && text[i + 1].isDigit())) {
                int j = i;
                while (j < n && (text[j].isDigit() || text[j] == '.')) ++j;
                setFormat(i, j - i, number_);
                i = j;
                continue;
            }
            if (c.isLetter() || c == '_') {
                int j = i;
                while (j < n && (text[j].isLetterOrNumber() || text[j] == '_')) ++j;
                const QString word = text.mid(i, j - i).toLower();
                if (keywords_.contains(word)) setFormat(i, j - i, keyword_);
                else if (builtins_.contains(word)) setFormat(i, j - i, builtin_);
                i = j;
                continue;
            }
            ++i;
        }
        setCurrentBlockState(inComment ? 1 : 0);
    }

private:
    QSet<QString> builtins_, keywords_;
    QTextCharFormat keyword_, builtin_, string_, comment_, number_;
};

// Plain text edit with Ctrl+Space / as-you-type completion and four-space tabs.
class CodeEdit : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    QCompleter* completer = nullptr;

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (completer && completer->popup()->isVisible() &&
            (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Tab || e->key() == Qt::Key_Escape)) {
            e->ignore();  // the completer takes it
            return;
        }
        if (e->key() == Qt::Key_Tab && !(e->modifiers() & Qt::ControlModifier)) {
            insertPlainText("    ");
            return;
        }
        const bool force = e->key() == Qt::Key_Space && (e->modifiers() & Qt::ControlModifier);
        if (!force) QPlainTextEdit::keyPressEvent(e);
        if (!completer) return;
        QTextCursor tc = textCursor();
        tc.select(QTextCursor::WordUnderCursor);
        const QString prefix = tc.selectedText();
        if (!force && (prefix.size() < 3 || e->text().isEmpty() || !(e->text().at(0).isLetterOrNumber() || e->text().at(0) == '_'))) {
            completer->popup()->hide();
            return;
        }
        completer->setCompletionPrefix(prefix);
        if (completer->completionCount() == 0) return completer->popup()->hide();
        completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
        QRect r = cursorRect();
        r.setWidth(completer->popup()->sizeHintForColumn(0) + completer->popup()->verticalScrollBar()->sizeHint().width() + 12);
        completer->complete(r);
    }
};

QStringList loadBuiltins() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/generated/iw8_gsc_builtins.txt")); ++up) d.cdUp();
    QFile f(d.filePath("data/generated/iw8_gsc_builtins.txt"));
    QStringList names;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return names;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith('#')) continue;
        const QString name = line.section('\t', 1, 1).trimmed();
        if (!name.isEmpty()) names << name;
    }
    names.removeDuplicates();
    names.sort();
    return names;
}

}  // namespace

ScriptPanel::ScriptPanel(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout;
    header_ = new QLabel(this);
    header_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(header_, 1);
    auto* create = new QPushButton("New Script", this);
    create->setObjectName("scriptCreate");
    create->setToolTip("Create <map>.gsc next to the map and set worldspawn iw8r_script to it");
    auto* saveBtn = new QPushButton("Save", this);
    saveBtn->setObjectName("scriptSave");
    auto* reloadBtn = new QPushButton("Reload", this);
    auto* checkBtn = new QPushButton("Check", this);
    checkBtn->setObjectName("scriptCheck");
    checkBtn->setToolTip("Check brackets, strings, comments and function names (the build compiles it with gsc-tool)");
    auto* insert = new QToolButton(this);
    insert->setObjectName("scriptInsert");
    insert->setText("Insert Behaviour");
    insert->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(insert);
    for (const ScriptSnippet& s : scriptSnippets()) {
        const QString id = s.id;
        menu->addAction(s.title, this, [this, id] {
            if (!insertSnippet(id)) ctx_->setStatus("That behaviour is already in the script");
        });
    }
    insert->setMenu(menu);
    for (QWidget* w : {static_cast<QWidget*>(create), static_cast<QWidget*>(saveBtn), static_cast<QWidget*>(reloadBtn),
                       static_cast<QWidget*>(checkBtn), static_cast<QWidget*>(insert)})
        top->addWidget(w);
    lay->addLayout(top);

    auto* code = new CodeEdit(this);
    edit_ = code;
    edit_->setObjectName("scriptEdit");
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    edit_->setFont(mono);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(' '));
    lay->addWidget(edit_, 1);

    const QStringList builtins = loadBuiltins();
    builtinCount_ = int(builtins.size());
    new GscHighlighter(edit_->document(), QSet<QString>(builtins.begin(), builtins.end()));
    QStringList words = builtins + gscKeywords();
    words.sort();
    completer_ = new QCompleter(words, this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setWidget(edit_);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    code->completer = completer_;
    connect(completer_, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& word) {
        QTextCursor tc = edit_->textCursor();
        tc.select(QTextCursor::WordUnderCursor);
        tc.insertText(word);
        edit_->setTextCursor(tc);
    });

    issues_ = new QListWidget(this);
    issues_->setObjectName("scriptIssues");
    issues_->setMaximumHeight(96);
    lay->addWidget(issues_);
    connect(issues_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        const int line = it->data(Qt::UserRole).toInt();
        if (line <= 0) return;
        QTextCursor tc(edit_->document()->findBlockByNumber(line - 1));
        edit_->setTextCursor(tc);
        edit_->setFocus();
    });

    connect(create, &QPushButton::clicked, this, [this] {
        QString why;
        if (!createScript(&why)) ctx_->setStatus(why);
    });
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        QString why;
        ctx_->setStatus(save(&why) ? "Saved " + QFileInfo(scriptPath()).fileName() : why);
    });
    connect(reloadBtn, &QPushButton::clicked, this, [this] { reload(); });
    connect(checkBtn, &QPushButton::clicked, this, [this] {
        const int errors = check();
        ctx_->setStatus(errors ? QString("Script: %1 problem(s)").arg(errors) : QString("Script: no structure problems found"));
    });
    connect(edit_->document(), &QTextDocument::modificationChanged, this, [this] { refreshHeader(); });
    // The script follows the map: another map, or iw8r_script edited in the inspector.
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (scriptPath() != loadedPath_ && !modified()) reload();
        else refreshHeader();
    });
    reload();
}

QString ScriptPanel::scriptPath() const {
    const Entity* ws = ctx_->doc.worldspawn();
    const std::string* v = ws ? ws->value("iw8r_script") : nullptr;
    if (!v || v->empty()) return {};
    const QFileInfo fi(qs(*v));
    if (fi.isAbsolute()) return fi.absoluteFilePath();
    if (ctx_->doc.path().empty()) return {};
    return QDir(QFileInfo(qs(ctx_->doc.path())).absolutePath()).absoluteFilePath(qs(*v));
}

bool ScriptPanel::createScript(QString* why) {
    auto fail = [why](const QString& m) {
        if (why) *why = m;
        return false;
    };
    if (ctx_->doc.path().empty()) return fail("Save the map first: the script is created next to it");
    Entity* ws = ctx_->doc.worldspawn();
    if (!ws) return fail("The map has no worldspawn");
    const QFileInfo map(qs(ctx_->doc.path()));
    const QString name = map.completeBaseName() + ".gsc";
    const QString path = map.absoluteDir().absoluteFilePath(name);
    if (!QFileInfo::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Cannot write " + path);
        f.write(QByteArray::fromStdString(userScriptTemplate(map.completeBaseName().toStdString())));
    }
    if (!ctx_->doc.setKeyValue(ws->id, "iw8r_script", name.toStdString())) return fail("Cannot set worldspawn iw8r_script");
    ctx_->documentEdited("Map script " + name + " (worldspawn iw8r_script)");
    reload();
    return true;
}

bool ScriptPanel::save(QString* why) {
    const QString path = scriptPath();
    if (path.isEmpty()) {
        if (why) *why = "No map script yet: press New Script";
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (why) *why = "Cannot write " + path;
        return false;
    }
    QString t = edit_->toPlainText();
    if (!t.endsWith('\n')) t += '\n';
    f.write(t.toUtf8());
    edit_->document()->setModified(false);
    loadedPath_ = path;
    refreshHeader();
    return true;
}

void ScriptPanel::reload() {
    loadedPath_ = scriptPath();
    QFile f(loadedPath_);
    QString t;
    if (!loadedPath_.isEmpty() && f.open(QIODevice::ReadOnly)) t = QString::fromUtf8(f.readAll());
    edit_->setPlainText(t);
    edit_->document()->setModified(false);
    edit_->setReadOnly(loadedPath_.isEmpty());
    issues_->clear();
    refreshHeader();
}

bool ScriptPanel::modified() const { return edit_->document()->isModified(); }
QString ScriptPanel::text() const { return edit_->toPlainText(); }
void ScriptPanel::setText(const QString& t) {
    edit_->setPlainText(t);
    edit_->document()->setModified(true);
}

bool ScriptPanel::insertSnippet(const QString& id) {
    if (edit_->isReadOnly()) {
        ctx_->setStatus("No map script yet: press New Script");
        return false;
    }
    for (const ScriptSnippet& s : scriptSnippets()) {
        if (id != s.id) continue;
        const std::vector<std::string> have = scriptFunctionNames(edit_->toPlainText().toStdString());
        for (const std::string& fn : scriptFunctionNames(s.text))
            if (std::find(have.begin(), have.end(), fn) != have.end()) return false;
        QTextCursor tc(edit_->document());
        tc.movePosition(QTextCursor::End);
        tc.insertText((edit_->toPlainText().endsWith('\n') ? "\n" : "\n\n") + QString::fromUtf8(s.text));
        edit_->setTextCursor(tc);
        ctx_->setStatus(QString("Inserted \"%1\": add its call (shown in the comment) to user_main()").arg(s.title));
        return true;
    }
    return false;
}

int ScriptPanel::check() {
    issues_->clear();
    int errors = 0;
    for (const ScriptIssue& i : checkUserScript(edit_->toPlainText().toStdString())) {
        auto* item = new QListWidgetItem((i.line > 0 ? QString("line %1: ").arg(i.line) : QString()) + qs(i.message), issues_);
        item->setData(Qt::UserRole, i.line);
        item->setForeground(i.error ? QColor(0xe0, 0x60, 0x60) : QColor(0xd0, 0xb0, 0x50));
        errors += i.error;
    }
    return errors;
}

int ScriptPanel::issueCount() const { return issues_->count(); }

void ScriptPanel::refreshHeader() {
    const QString path = scriptPath();
    if (path.isEmpty())
        header_->setText("No map script. <b>New Script</b> creates one next to the map (worldspawn iw8r_script); the build starts its user_main().");
    else
        header_->setText(QFileInfo(path).fileName() + (modified() ? " <b>(modified)</b>" : "") +
                         (QFileInfo::exists(path) ? QString() : QString(" <span style='color:#e06060'>(file missing)</span>")));
}

bool ScriptPanel::goToLine(int line) {
    QTextBlock b = edit_->document()->findBlockByNumber(line - 1);
    if (line < 1 || !b.isValid()) return false;
    QTextCursor c(b);
    c.select(QTextCursor::LineUnderCursor);
    edit_->setTextCursor(c);
    edit_->centerCursor();
    edit_->setFocus();
    return true;
}

int ScriptPanel::currentLine() const { return edit_->textCursor().blockNumber() + 1; }

}  // namespace iw8r::qt
