#include "help_panel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace iw8r::qt {

QString HelpSearchDialog::docsDir() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("docs/USER_GUIDE.md"))) return QDir::cleanPath(d.filePath("docs"));
    return {};
}

HelpSearchDialog::HelpSearchDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("helpSearchDialog");
    setWindowTitle("Search Help");
    resize(980, 640);
    // The guides a mapper reads: user guide, tutorial, entity reference, agent guide, test checklist, feature matrix.
    const QString dir = docsDir();
    for (const char* f : {"USER_GUIDE.md", "TUTORIAL.md", "ENTITY_REFERENCE.md", "AGENT_MAP_GUIDE.md", "TEST_CHECKLIST.md", "FEATURE_MATRIX.md"}) {
        QFile file(dir + "/" + f);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const auto s = splitMarkdownSections(f, QString::fromUtf8(file.readAll()).toStdString());
        sections_.insert(sections_.end(), s.begin(), s.end());
    }
    auto* lay = new QVBoxLayout(this);
    query_ = new QLineEdit(this);
    query_->setObjectName("helpQuery");
    query_->setPlaceholderText(QString("Search %1 help sections (e.g. spawn, compile, prefab, sky)").arg(sections_.size()));
    query_->setClearButtonEnabled(true);
    lay->addWidget(query_);
    auto* split = new QSplitter(this);
    results_ = new QListWidget(split);
    view_ = new QTextBrowser(split);
    view_->setOpenExternalLinks(true);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);
    connect(query_, &QLineEdit::textChanged, this, [this](const QString& q) { search(q); });
    connect(results_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || size_t(row) >= hits_.size()) return view_->clear();
        const HelpSection& s = sections_[hits_[size_t(row)].section];
        view_->setMarkdown(QString::fromStdString("## " + (s.title.empty() ? s.document : s.title) + "\n\n*" + s.path + "*\n\n" + s.body));
    });
    if (sections_.empty()) view_->setPlainText("The docs folder was not found next to the editor.");
}

void HelpSearchDialog::search(const QString& query) {
    if (query_->text() != query) query_->setText(query);
    hits_ = searchHelp(sections_, query.toStdString(), 60);
    results_->clear();
    for (const HelpHit& h : hits_) {
        const HelpSection& s = sections_[h.section];
        auto* it = new QListWidgetItem(QString::fromStdString(s.title.empty() ? s.document : s.title) + "  -  " + QString::fromStdString(s.document));
        it->setToolTip(QString::fromStdString(h.snippet));
        results_->addItem(it);
    }
    if (!hits_.empty()) results_->setCurrentRow(0);
    else view_->setPlainText(query.trimmed().isEmpty() ? QString() : QString("Nothing found for \"%1\".").arg(query));
}

int HelpSearchDialog::resultCount() const { return int(hits_.size()); }
QString HelpSearchDialog::shownText() const { return view_->toPlainText(); }

FirstMapGuide::FirstMapGuide(EditorContext* ctx, RunCommand run, BuildState state, QWidget* parent)
    : QWidget(parent), ctx_(ctx), run_(std::move(run)), state_(std::move(state)) {
    setObjectName("firstMapGuide");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    auto* intro = new QLabel("Your first playable map, step by step. Steps tick themselves as the map gets there "
                             "(docs/TUTORIAL.md walks through a finished one).", this);
    intro->setWordWrap(true);
    outer->addWidget(intro);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget(scroll);
    list_ = new QVBoxLayout(inner);
    list_->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (isVisible()) refresh();
    });
    refresh();
}

void FirstMapGuide::refresh() {
    const auto st = state_ ? state_() : std::pair<bool, bool>{false, false};
    steps_ = firstMapSteps(ctx_->doc.map(), st.first, st.second);
    while (QLayoutItem* it = list_->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    int n = 0;
    for (const FirstMapStep& s : steps_) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 2, 0, 2);
        auto* mark = new QLabel(s.done ? QString::fromUtf8("\xE2\x9C\x94") : QString::number(++n), row);  // check mark
        if (s.done) ++n;
        mark->setFixedWidth(22);
        mark->setStyleSheet(s.done ? "color:#5c5" : "color:#aab");
        h->addWidget(mark);
        auto* text = new QLabel(QString("<b>%1</b><br><span style='color:#9aa'>%2</span>")
                                    .arg(QString::fromStdString(s.title).toHtmlEscaped(), QString::fromStdString(s.hint).toHtmlEscaped()),
                                row);
        text->setWordWrap(true);
        h->addWidget(text, 1);
        if (!s.command.empty() && !s.done) {
            auto* b = new QPushButton("Do it", row);
            const QString cmd = QString::fromStdString(s.command);
            connect(b, &QPushButton::clicked, this, [this, cmd] {
                if (run_) run_(cmd);
            });
            h->addWidget(b);
        }
        list_->addWidget(row);
    }
    list_->addStretch(1);
}

int FirstMapGuide::doneCount() const {
    int n = 0;
    for (const auto& s : steps_) n += s.done;
    return n;
}

}  // namespace iw8r::qt
