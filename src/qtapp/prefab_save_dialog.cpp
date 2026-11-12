#include "prefab_save_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "map_text.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

QString zonesText(const std::vector<std::string>& zones) {
    if (zones.empty()) return {};
    QStringList z;
    for (size_t i = 0; i < zones.size() && i < 4; ++i) z << qs(zones[i]);
    return QString("  (in %1%2)").arg(z.join(", "), zones.size() > 4 ? QString(", ...") : QString());
}
}  // namespace

PrefabSaveDialog::PrefabSaveDialog(const PrefabSaveResult& scan, FolderFn folderFor, QWidget* parent)
    : QDialog(parent), folderFor_(std::move(folderFor)) {
    setObjectName("prefabSaveDialog");
    setWindowTitle("Save Selection As Prefab");
    resize(640, 620);
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel("The selection is saved as a prefab bundle: one folder that holds its brushes and entities, their "
                             "behaviour keys, and your own materials, models, sounds and effects that it uses. Stock assets are "
                             "named, not copied. The bundle then appears in the Prefabs dock and can be dragged into any map.",
                             this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto* form = new QFormLayout();
    title_ = new QLineEdit(qs(scan.manifest.title), this);
    title_->setObjectName("prefabSaveTitle");
    id_ = new QLineEdit(qs(scan.manifest.id), this);
    id_->setObjectName("prefabSaveId");
    id_->setToolTip("1 to 40 characters of a-z 0-9 _. It names the bundle's folder and is part of its script function names.\n"
                    "Saving again under the same id replaces the bundle (the editor asks first).");
    description_ = new QPlainTextEdit(qs(scan.manifest.description), this);
    description_->setObjectName("prefabSaveDescription");
    description_->setPlaceholderText("One or two sentences: what it is and how it is used");
    description_->setFixedHeight(54);
    description_->setTabChangesFocus(true);
    category_ = new QLineEdit(qs(scan.manifest.category), this);
    category_->setObjectName("prefabSaveCategory");
    category_->setPlaceholderText("doors, props, gameplay ... (a subfolder; may stay empty)");
    QStringList tags;
    for (const std::string& t : scan.manifest.tags) tags << qs(t);
    tags_ = new QLineEdit(tags.join(", "), this);
    tags_->setObjectName("prefabSaveTags");
    tags_->setPlaceholderText("search words, separated by commas");
    form->addRow("Title", title_);
    form->addRow("Id", id_);
    form->addRow("Description", description_);
    form->addRow("Category", category_);
    form->addRow("Tags", tags_);
    lay->addLayout(form);

    lay->addWidget(new QLabel("What travels with it:", this));
    list_ = new QTreeWidget(this);
    list_->setObjectName("prefabSaveList");
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(true);
    const auto section = [&](const QString& name) {
        auto* s = new QTreeWidgetItem(list_, QStringList{name});
        QFont f = s->font(0);
        f.setBold(true);
        s->setFont(0, f);
        s->setExpanded(true);
        return s;
    };
    const auto line = [](QTreeWidgetItem* under, const QString& text, const QString& tip = {}) {
        auto* it = new QTreeWidgetItem(under, QStringList{text});
        if (!tip.isEmpty()) it->setToolTip(0, tip);
        return it;
    };
    const PrefabManifest& m = scan.manifest;
    {
        // Contents: what the prefab's own map holds.
        size_t prims = 0, patches = 0, brushEntities = 0, pointEntities = 0;
        QMap<QString, int> classes;
        Id next = 1;
        if (ParseResult pr = parseMap(scan.mapText, next); pr.map)
            for (const Entity& e : pr.map->entities) {
                size_t own = 0;
                for (const auto& it : e.items)
                    if (const Primitive* p = it.prim()) {
                        ++own;
                        (p->kind == PrimKind::Patch ? patches : prims)++;
                    }
                if (e.isWorldspawn()) continue;
                (own ? brushEntities : pointEntities)++;
                ++classes[qs(e.classname())];
            }
        QTreeWidgetItem* s = section("Contents");
        line(s, QString("%1 brush(es) and %2 patch(es)").arg(prims).arg(patches));
        QStringList cls;
        for (auto it = classes.begin(); it != classes.end(); ++it) cls << QString("%1 x %2").arg(it.value()).arg(it.key());
        line(s, QString("%1 entit%2 (%3 with brushes)%4").arg(brushEntities + pointEntities).arg(brushEntities + pointEntities == 1 ? "y" : "ies")
                    .arg(brushEntities).arg(cls.isEmpty() ? QString() : ": " + cls.join(", ")),
             "Entities keep every key, so behaviour set with keys (doors, jump pads, lifts, effects, sounds) travels with the prefab.");
        if (!m.pivot.empty()) line(s, "Origin of the prefab: " + qs(m.pivot));
    }
    {
        QTreeWidgetItem* s = section("Your own files (copied into the bundle)");
        for (const auto& a : m.materials) line(s, "material  " + qs(a.name) + "  ->  " + qs(a.file));
        for (const auto& a : m.models) line(s, "model  " + qs(a.name) + "  ->  " + qs(a.file));
        for (const auto& a : m.sounds) line(s, "sound  " + qs(a.alias) + "  ->  " + qs(a.file));
        for (const auto& a : m.effects) line(s, "effect  " + qs(a.name) + "  ->  " + qs(a.file));
        // Images and side files that follow a material or a model.
        for (const PrefabSaveFile& f : scan.files) {
            bool listed = false;
            for (const auto& a : m.materials) listed |= a.file == f.dest;
            for (const auto& a : m.models) listed |= a.file == f.dest;
            for (const auto& a : m.sounds) listed |= a.file == f.dest;
            for (const auto& a : m.effects) listed |= a.file == f.dest;
            if (!listed) line(s, qs(f.kind) + " file  " + qs(f.dest), QDir::toNativeSeparators(qs(f.source)));
        }
        if (s->childCount() == 0) line(s, "(none: the selection uses no material, model, sound or effect of your own)");
    }
    {
        QTreeWidgetItem* s = section("Stock assets it needs (named only; they come from the game)");
        for (const auto& r : m.stockModels) line(s, "model  " + qs(r.name) + zonesText(r.zones));
        for (const auto& r : m.stockMaterials) line(s, "material  " + qs(r.name) + zonesText(r.zones));
        for (const std::string& n : m.stockSounds) line(s, "sound  " + qs(n));
        for (const std::string& n : m.stockEffects) line(s, "effect  " + qs(n));
        for (const auto& r : m.scriptableDefs) line(s, "scriptable definition  " + qs(r.name) + zonesText(r.zones));
        for (const std::string& n : m.buildFeatures) line(s, "build feature  " + qs(n));
        if (s->childCount() == 0) line(s, "(none)");
    }
    if (!m.namesLocal.empty() || !m.namesGlobal.empty()) {
        QTreeWidgetItem* s = section("Names");
        QStringList local, global;
        for (const std::string& n : m.namesLocal) local << qs(n);
        for (const std::string& n : m.namesGlobal) global << qs(n);
        if (!local.isEmpty())
            line(s, "kept apart per placement: " + local.join(", "),
                 "Each placement gets its own copy of these names (p1_door, p2_door), so two placements never open each other's door.");
        if (!global.isEmpty())
            line(s, "pointing outside the prefab (left as they are): " + global.join(", "));
    }
    if (!scan.linkedOutside.empty()) {
        QTreeWidgetItem* s = section("Linked objects that are not selected (tick to add them)");
        for (const PrefabLinkedEntity& l : scan.linkedOutside) {
            QTreeWidgetItem* it = line(s, qs(l.text));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, Qt::Unchecked);
            it->setData(0, Qt::UserRole, qulonglong(l.entity));
        }
    }
    if (!scan.notes.empty()) {
        QTreeWidgetItem* s = section("Notes");
        for (const std::string& n : scan.notes) line(s, qs(n));
    }
    lay->addWidget(list_, 1);

    replace_ = new QCheckBox("Replace the selection with one placement of the new prefab", this);
    replace_->setObjectName("prefabSaveReplace");
    replace_->setChecked(true);
    lay->addWidget(replace_);
    folder_ = new QLabel(this);
    folder_->setObjectName("prefabSaveFolder");
    folder_->setWordWrap(true);
    folder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(folder_);

    auto* box = new QDialogButtonBox(this);
    save_ = box->addButton("Save Prefab", QDialogButtonBox::AcceptRole);
    save_->setObjectName("prefabSaveOk");
    save_->setDefault(true);
    box->addButton(QDialogButtonBox::Cancel);
    auto* plain = box->addButton("Save As Plain .map File...", QDialogButtonBox::ActionRole);
    plain->setObjectName("prefabSavePlain");
    plain->setToolTip("The old form: one .map file with the brushes and entities only. No description, no assets, no names per placement.");
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(plain, &QPushButton::clicked, this, [this] { done(kPlainMap); });
    connect(title_, &QLineEdit::textChanged, this, [this](const QString& t) {
        // The id follows the title until the mapper types an id of their own.
        if (!idEdited_) id_->setText(qs(prefabIdFromName(t.toStdString())));
        refreshFolder();
    });
    connect(id_, &QLineEdit::textEdited, this, [this] {
        idEdited_ = true;
        refreshFolder();
    });
    connect(id_, &QLineEdit::textChanged, this, [this] { refreshFolder(); });
    connect(category_, &QLineEdit::textChanged, this, [this] { refreshFolder(); });
    refreshFolder();
    title_->selectAll();
    title_->setFocus();
}

void PrefabSaveDialog::refreshFolder() {
    const QString theId = id();
    const bool valid = isValidPrefabId(theId.toStdString());
    save_->setEnabled(valid && !title().isEmpty());
    if (!valid) {
        folder_->setText("The id needs 1 to 40 characters of a-z, 0-9 and _.");
        return;
    }
    bool exists = false;
    const QString folder = folderFor_ ? folderFor_(theId, category(), &exists) : QString();
    folder_->setText(QString("Saved to: %1%2").arg(QDir::toNativeSeparators(folder),
                                                   exists ? QString("\nA prefab with this id is already there. Saving asks before it replaces it.")
                                                          : QString()));
}

QString PrefabSaveDialog::id() const { return id_->text().trimmed(); }
QString PrefabSaveDialog::title() const { return title_->text().trimmed(); }
QString PrefabSaveDialog::description() const { return description_->toPlainText().simplified(); }
QString PrefabSaveDialog::category() const { return category_->text().trimmed(); }
bool PrefabSaveDialog::replaceSelection() const { return replace_->isChecked(); }
QString PrefabSaveDialog::folderText() const { return folder_->text(); }

QStringList PrefabSaveDialog::tags() const {
    QStringList out;
    for (const QString& t : tags_->text().split(',', Qt::SkipEmptyParts))
        if (!t.trimmed().isEmpty()) out << t.trimmed();
    return out;
}

std::vector<Id> PrefabSaveDialog::tickedLinkedEntities() const {
    std::vector<Id> out;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* s = list_->topLevelItem(i);
        for (int k = 0; k < s->childCount(); ++k)
            if ((s->child(k)->flags() & Qt::ItemIsUserCheckable) && s->child(k)->checkState(0) == Qt::Checked)
                out.push_back(Id(s->child(k)->data(0, Qt::UserRole).toULongLong()));
    }
    return out;
}

QStringList PrefabSaveDialog::travelLines() const {
    QStringList out;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* s = list_->topLevelItem(i);
        for (int k = 0; k < s->childCount(); ++k) out << s->text(0) + ": " + s->child(k)->text(0);
    }
    return out;
}

}  // namespace iw8r::qt
