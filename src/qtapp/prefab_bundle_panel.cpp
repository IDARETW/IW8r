#include "prefab_bundle_panel.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "map_text.h"
#include "prefab.h"
#include "prefab_browser.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

// A number or choice field changes with the mouse wheel only while it has the keyboard focus: turning the wheel to
// scroll the Entity dock must not change a parameter the cursor happens to pass over.
class WheelNeedsFocus : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Wheel) return false;
        const auto* w = qobject_cast<QWidget*>(o);
        if (!w || w->hasFocus()) return false;
        e->ignore();
        return true;
    }
};
}  // namespace

PrefabBundlePanel::PrefabBundlePanel(EditorContext* ctx, Hooks hooks, QWidget* parent)
    : QWidget(parent), ctx_(ctx), hooks_(std::move(hooks)) {
    setObjectName("prefabBundlePanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 2);
    lay->setSpacing(4);
    title_ = new QLabel(this);
    title_->setObjectName("prefabBundleTitle");
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    title_->setWordWrap(true);
    sub_ = new QLabel(this);
    sub_->setObjectName("prefabBundleSub");
    sub_->setWordWrap(true);
    sub_->setEnabled(false);  // secondary text
    description_ = new QLabel(this);
    description_->setObjectName("prefabBundleDescription");
    description_->setWordWrap(true);
    badge_ = new QLabel(this);
    badge_->setObjectName("prefabBundleBadge");
    badge_->setWordWrap(true);
    badge_->setStyleSheet("QLabel { background-color: #d78c14; color: white; padding: 3px 6px; border-radius: 3px; }");
    lay->addWidget(title_);
    lay->addWidget(sub_);
    lay->addWidget(description_);
    lay->addWidget(badge_);
    formHost_ = new QWidget(this);
    form_ = new QFormLayout(formHost_);
    form_->setContentsMargins(0, 2, 0, 2);
    lay->addWidget(formHost_);
    auto* buttons = new QHBoxLayout();
    const auto button = [&](const char* name, const QString& text, const QString& tip, const std::function<void()>& fn) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(name);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [fn] {
            if (fn) fn();
        });
        buttons->addWidget(b);
    };
    button("prefabBundleEdit", "Edit Prefab", "Opens the bundle's prefab.map (Edit > Leave Prefab comes back)", hooks_.editPrefab);
    button("prefabBundleFolder", "Open Bundle Folder", "Shows the bundle's folder: prefab.json, prefab.map, its materials and models",
           hooks_.openFolder);
    button("prefabBundleExplode", "Explode", "Replaces this placement by its loose contents, with the names the build gives them",
           hooks_.explode);
    button("prefabBundleUnique", "Make Unique", "Copies the bundle beside the map under a new id and points this placement at the copy",
           hooks_.makeUnique);
    buttons->addStretch(1);
    lay->addLayout(buttons);
    hide();
    connect(ctx_, &EditorContext::documentChanged, this, [this] { refresh(); });
}

QString PrefabBundlePanel::shownValue(const PrefabParameter& p, const Entity& e) {
    if (const auto v = prefabParameterValue(p, e)) return qs(*v);
    return qs(p.defaultValue);
}

void PrefabBundlePanel::refresh() {
    Id id = ctx_->doc.inspectedEntity();
    const Entity* e = ctx_->doc.findEntity(id);
    PrefabBundle bundle;
    // Only when the placement is what is selected (not the worldspawn the inspector falls back to).
    const bool selected = e && ctx_->doc.isSelected({id, kNoId});
    if (!selected || e->classname() != "misc_prefab" || !hooks_.bundleOf || !hooks_.bundleOf(id, bundle)) {
        entity_ = kNoId;
        shownKey_.clear();
        hide();
        return;
    }
    const QString key = qs(bundle.folder) + "\n" + qs(prefabManifestText(bundle.manifest)) + "\n" + qs(bundle.problem);
    if (key != shownKey_) {
        shownKey_ = key;
        rebuild(bundle);
    }
    entity_ = id;
    fill(*e);
    show();
}

void PrefabBundlePanel::rebuild(const PrefabBundle& bundle) {
    const PrefabManifest& m = bundle.manifest;
    title_->setText(qs(bundle.title));
    description_->setText(qs(m.description));
    description_->setVisible(!m.description.empty());
    badgeText_ = PrefabBrowser::bundleBadge(bundle, &badgeReason_);
    badge_->setText(badgeText_.isEmpty() ? QString() : badgeText_ + ": " + badgeReason_);
    badge_->setToolTip(badgeReason_);
    badge_->setVisible(!badgeText_.isEmpty());
    badge_->setStyleSheet(QString("QLabel { background-color: %1; color: white; padding: 3px 6px; border-radius: 3px; }")
                              .arg(badgeText_ == "CANNOT PLACE" ? "#be3232" : "#d78c14"));
    setToolTip(QDir::toNativeSeparators(qs(bundle.folder)));
    // The parameter fields.
    while (form_->rowCount() > 0) form_->removeRow(0);
    rows_.clear();
    for (const PrefabParameter& p : m.parameters) {
        Row row;
        row.param = p;
        const int index = int(rows_.size());
        QString hint;
        if (p.type == "number") {
            auto* spin = new QDoubleSpinBox(formHost_);
            spin->setDecimals(3);
            spin->setRange(p.min.value_or(-1000000.0), p.max.value_or(1000000.0));
            spin->setKeyboardTracking(false);  // a typed number is written when it is finished, not per digit
            const double span = spin->maximum() - spin->minimum();
            spin->setSingleStep(span <= 10 ? 0.1 : span <= 1000 ? 1 : 10);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
                if (!filling_) commit(index, qs(formatNumber(v)));
            });
            row.field = spin;
            if (p.min && p.max) hint = QString("%1 to %2").arg(qs(formatNumber(*p.min)), qs(formatNumber(*p.max)));
            else if (p.min) hint = QString("at least %1").arg(qs(formatNumber(*p.min)));
            else if (p.max) hint = QString("at most %1").arg(qs(formatNumber(*p.max)));
        } else if (p.type == "choice") {
            auto* box = new QComboBox(formHost_);
            for (const std::string& c : p.choices) box->addItem(qs(c));
            connect(box, &QComboBox::currentTextChanged, this, [this, index](const QString& v) {
                if (!filling_) commit(index, v);
            });
            row.field = box;
        } else {
            auto* edit = new QLineEdit(formHost_);
            if (p.type == "material") edit->setPlaceholderText("material name");
            connect(edit, &QLineEdit::editingFinished, this, [this, index, edit] {
                if (!filling_) commit(index, edit->text());
            });
            row.field = edit;
        }
        row.field->setObjectName("prefabParam_" + qs(p.name));
        row.field->setFocusPolicy(Qt::StrongFocus);
        row.field->installEventFilter(new WheelNeedsFocus(row.field));
        if (!p.defaultValue.empty()) hint += (hint.isEmpty() ? QString() : QString(", ")) + "default " + qs(p.defaultValue);
        row.hint = new QLabel(hint, formHost_);
        row.hint->setEnabled(false);
        auto* reset = new QToolButton(formHost_);
        reset->setText("Default");
        reset->setToolTip("Back to the bundle's own value (removes this placement's key)");
        connect(reset, &QToolButton::clicked, this, [this, index] { resetParameter(index); });
        auto* line = new QWidget(formHost_);
        auto* h = new QHBoxLayout(line);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(row.field, 1);
        h->addWidget(row.hint);
        h->addWidget(reset);
        const QString label = qs(p.label.empty() ? p.name : p.label);
        const auto written = prefabParameterKey(p, "<value>");
        line->setToolTip(QString("%1\nWrites the key %2 on this placement.").arg(label, qs(written.first)));
        form_->addRow(label, line);
        rows_.push_back(row);
    }
    formHost_->setVisible(!rows_.empty());
}

void PrefabBundlePanel::fill(const Entity& e) {
    filling_ = true;
    const std::string* pid = e.value(kPrefabPlacementIdKey);
    const std::string* model = e.value("model");
    sub_->setText(QString("Prefab bundle  %1   placement id %2").arg(model ? qs(*model) : QString(), pid && !pid->empty() ? qs(*pid) : QString("(none: the build gives it one)")));
    for (Row& row : rows_) {
        const QString v = shownValue(row.param, e);
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(row.field)) spin->setValue(v.toDouble());
        else if (auto* box = qobject_cast<QComboBox*>(row.field)) {
            if (box->findText(v) < 0 && !v.isEmpty()) box->addItem(v);  // a value written by hand
            box->setCurrentText(v);
        } else if (auto* edit = qobject_cast<QLineEdit*>(row.field)) {
            if (!edit->hasFocus() || edit->text().isEmpty()) edit->setText(v);
        }
        // A value this placement holds is shown in bold; the bundle's default in the normal weight.
        QFont f = row.field->font();
        f.setBold(prefabParameterValue(row.param, e).has_value());
        row.field->setFont(f);
    }
    filling_ = false;
}

bool PrefabBundlePanel::commit(int i, const QString& value) {
    if (i < 0 || i >= int(rows_.size()) || entity_ == kNoId || !hooks_.setParameter) return false;
    const Entity* e = ctx_->doc.findEntity(entity_);
    if (!e) return false;
    const PrefabParameter& p = rows_[size_t(i)].param;
    std::string v = value.toStdString();
    if (p.type == "number") {
        // Kept inside the bundle's range, whatever was typed.
        double d = value.toDouble();
        if (p.min) d = std::max(d, *p.min);
        if (p.max) d = std::min(d, *p.max);
        v = formatNumber(d);
    }
    // Nothing to write when the placement already shows this value.
    if (shownValue(p, *e).toStdString() == v && (prefabParameterValue(p, *e).has_value() || v == p.defaultValue)) return false;
    return hooks_.setParameter(entity_, p, v);
}

bool PrefabBundlePanel::setParameter(int i, const QString& value) {
    if (i < 0 || i >= int(rows_.size())) return false;
    QWidget* field = rows_[size_t(i)].field;
    {
        // Put the text in the field without committing, then commit once, as leaving the field does.
        const QSignalBlocker block(field);
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(field)) spin->setValue(value.toDouble());
        else if (auto* box = qobject_cast<QComboBox*>(field)) box->setCurrentText(value);
        else if (auto* edit = qobject_cast<QLineEdit*>(field)) edit->setText(value);
    }
    return commit(i, parameterValue(i));
}

bool PrefabBundlePanel::resetParameter(int i) {
    if (i < 0 || i >= int(rows_.size()) || entity_ == kNoId || !hooks_.setParameter) return false;
    return hooks_.setParameter(entity_, rows_[size_t(i)].param, std::nullopt);
}

QString PrefabBundlePanel::titleText() const { return title_->text(); }
QString PrefabBundlePanel::descriptionText() const { return description_->text(); }
QString PrefabBundlePanel::badgeText() const { return badgeText_; }
QString PrefabBundlePanel::badgeReason() const { return badgeReason_; }
QString PrefabBundlePanel::placementIdText() const { return sub_->text(); }

QString PrefabBundlePanel::parameterLabel(int i) const {
    if (i < 0 || i >= int(rows_.size())) return {};
    const PrefabParameter& p = rows_[size_t(i)].param;
    return qs(p.label.empty() ? p.name : p.label);
}

QString PrefabBundlePanel::parameterValue(int i) const {
    if (i < 0 || i >= int(rows_.size())) return {};
    QWidget* field = rows_[size_t(i)].field;
    if (auto* spin = qobject_cast<QDoubleSpinBox*>(field)) return qs(formatNumber(spin->value()));
    if (auto* box = qobject_cast<QComboBox*>(field)) return box->currentText();
    if (auto* edit = qobject_cast<QLineEdit*>(field)) return edit->text();
    return {};
}

QString PrefabBundlePanel::parameterHint(int i) const {
    return i >= 0 && i < int(rows_.size()) ? rows_[size_t(i)].hint->text() : QString();
}

QWidget* PrefabBundlePanel::parameterField(int i) const { return i >= 0 && i < int(rows_.size()) ? rows_[size_t(i)].field : nullptr; }

}  // namespace iw8r::qt
