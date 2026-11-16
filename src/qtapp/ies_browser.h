// IW8 Radiant (Qt shell) - IES Profile Browser (roadmap 5.13): LM-63 files of a folder (data/ies by default) with a
// polar plot of the C0 plane (core ies_profiles) and the 128-texel strip Replay's spot light samples; "Use for Selected
// Lights" writes iw8r_ies on the selected light / light_spot entities (one undo step). The two profiles Replay ships
// (data/asset_catalog/iw8_ies_profiles.tsv) are listed for reference.
#pragma once

#include <QDialog>
#include <QWidget>

#include "editor_context.h"
#include "ies_profiles.h"

class QLabel;
class QListWidget;

namespace iw8r::qt {

class IesPolarWidget : public QWidget {
    Q_OBJECT
public:
    explicit IesPolarWidget(QWidget* parent = nullptr);
    void setProfile(const IesProfile* p);  // null = empty plot

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<float> curve_;    // C0 plane, peak 1, over the vertical range
    double vmin_ = 0, vmax_ = 180;
    std::vector<int> texels_;     // Replay segment
};

class IesBrowserDialog : public QDialog {
    Q_OBJECT
public:
    IesBrowserDialog(EditorContext* ctx, QWidget* parent = nullptr);
    void setFolder(const QString& dir);  // also used by the GUI test
    int profileCount() const;
    bool selectProfile(const QString& fileName);
    QString selectedPath() const;
    int applyToSelection();  // lights changed (0 = none selected / nothing chosen)
    static QString defaultFolder();

private:
    EditorContext* ctx_;
    QString dir_;
    QListWidget* list_;
    IesPolarWidget* plot_;
    QLabel* info_;
    IesProfile profile_;
    QString selected_;
};

}  // namespace iw8r::qt
