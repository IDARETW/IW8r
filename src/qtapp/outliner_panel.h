#pragma once
// Outliner dock (roadmap 2.29): the map's entities grouped by class, with search, click-to-select, double-click to
// frame, and map statistics (brushes, patches, entities, materials and models used).
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QLabel;

namespace iw8r::qt {

class EditorContext;

class OutlinerPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutlinerPanel(EditorContext* ctx, QWidget* parent = nullptr);
    void setFilter(const QString& text);  // also used by the GUI test
    int classCount() const;                // top-level rows shown
    int entityCount() const;               // entity rows shown
    QString statsText() const;
    // Selects the shown entity rows whose label contains `text` (GUI test); returns how many.
    int selectMatching(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void rebuild();
    void syncSelectionFromDoc();
    EditorContext* ctx_;
    QLineEdit* filter_;
    QTreeWidget* tree_;
    QLabel* stats_;
    bool syncing_ = false;
    bool dirty_ = true;
    quint64 signature_ = 0;  // entity ids + item revisions at the last rebuild: selection-only changes skip it
    quint64 docSignature() const;
};

}  // namespace iw8r::qt
