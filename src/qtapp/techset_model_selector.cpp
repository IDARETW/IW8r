#include "techset_model_selector.h"
#include "asset_browser.h"
#include "settings.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QListView>
#include <QItemSelectionModel>

namespace iw8r::qt {
TechsetModelSelector::TechsetModelSelector(AssetBrowser *browser, QWidget *parent)
    : QDialog(parent), browser_(browser) {
    setObjectName("techsetModelSelector");
    setWindowTitle("Choose a preview model");
    setWindowModality(Qt::WindowModal);
    setMinimumSize(660, 480);
    resize(1120, 780);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);
    auto *title = new QLabel("Choose a preview model", this);
    auto font = title->font();
    font.setPointSize(font.pointSize() + 4);
    font.setBold(true);
    title->setFont(font);
    root->addWidget(title);
    auto *hint = new QLabel(
        "Search the model library, narrow by kit or favourites, then choose a model for the shader preview.", this);
    hint->setWordWrap(true);
    root->addWidget(hint);
    browser_->setParent(this);
    root->addWidget(browser_, 1);
    auto *footer = new QHBoxLayout;
    auto *selected = new QLabel("No model selected", this);
    selected->setObjectName("techsetModelSelection");
    selected->setMinimumWidth(0);
    selected->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer->addWidget(selected, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *use = buttons->addButton("Use model", QDialogButtonBox::AcceptRole);
    use->setObjectName("techsetUseModel");
    use->setDefault(true);
    use->setEnabled(false);
    use->setMinimumHeight(32);
    footer->addWidget(buttons);
    root->addLayout(footer);
    connect(browser_, &AssetBrowser::modelHighlighted, this, [selected, use](const QString &name) {
        selected->setText(name);
        selected->setToolTip(name);
        use->setEnabled(!name.isEmpty());
    });
    if (auto *list = browser_->findChild<QListView *>("techsetModelList"))
        connect(list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, selected, use] {
            const auto name = selectedModel();
            selected->setText(name.isEmpty() ? "No model selected" : name);
            selected->setToolTip(name);
            use->setEnabled(!name.isEmpty());
        });
    const auto choose = [this] {
        const auto name = selectedModel();
        if (name.isEmpty())
            return;
        browser_->noteRecent(name);
        emit modelSelected(name);
        accept();
    };
    connect(buttons, &QDialogButtonBox::accepted, this, choose);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browser_, &AssetBrowser::modelChosen, this, [choose](const QString &) { choose(); });
    connect(this, &QDialog::finished, this,
            [this] { Settings::instance().setValue("techsets/modelSelectorGeometry", saveGeometry()); });
    const auto geometry = Settings::instance().value("techsets/modelSelectorGeometry").toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    if (auto *search = browser_->findChild<QLineEdit *>("modelSearch"))
        search->setFocus();
}
QString TechsetModelSelector::selectedModel() const { return browser_->selectedName(); }
} // namespace iw8r::qt
