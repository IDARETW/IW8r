// IW8 Radiant Suite (Qt shell) - the side bar's Stock Map panel (owner 2026-11-15: "there should be a sidebar option
// to edit the values and see them change live in the viewport"). Its side bar icon shows while a stock map is open
// (File > Open IW8 Stock Map). Every control edits the document's stock map keys (worldspawn iw8r_sun_*, iw8r_sky_from,
// iw8r_stock_retexture, iw8r_hide_smodels, the iw8r_remove_world entities), one undo step per finished change (a
// slider drag is one step), and the viewport shows the edit at once (MainWindow::applyStockSunPreview, and the
// world edits a moment later through refreshStockWorldPreview, main_window_stock_edit.cpp). Laid out for the narrow
// side section (300-420 px, no sideways scrolling).
#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor_context.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QStringListModel;
class QTableWidget;
class QToolButton;

namespace ads {
class CDockWidget;
}

namespace iw8r::qt {

class MainWindow;

class StockMapPanel : public QWidget {
    Q_OBJECT
public:
    StockMapPanel(MainWindow* window, EditorContext* ctx, QWidget* parent = nullptr);

    // A sun: linear colour (0-16), intensity (0-1000000), direction "pitch yaw" in degrees (the sundirection
    // convention: toward the sun, pitch -90 to -1 above the horizon).
    struct Sun {
        double color[3] = {1.0, 0.95, 0.9};
        double intensity = 8.6;
        double pitch = -45, yaw = 0;
        bool known = false;  // read from the stock map's own sun light (false = the Relight Sun defaults)
    };
    Sun stockSun() const;  // the map's sun as loaded (its zone's sun light)
    Sun shownSun() const;  // the document's iw8r_sun_* keys over the stock sun: what the viewport shows

    // Edits (the controls call them; also the GUI test). Each is one undo step, unless `mergeTag` is the tag of the
    // previous change and that change was the last step and came less than a second ago (or a slider is held): then
    // the previous step is replaced, so a drag or a run of spin box steps is one undo step.
    void setSunColor(double r, double g, double b, const QString& mergeTag = {});
    void setSunIntensity(double v, const QString& mergeTag = {});
    void setSunDirection(double pitch, double yaw, const QString& mergeTag = {});
    void resetSun();                        // the stock sun again (the iw8r_sun_* keys removed)
    void setSkyFrom(const QString& mapId);  // "" = the map's own sky
    QStringList skyChoices() const;         // the stock map ids the Sky list offers (this map's own left out)
    // Retexture pairs (worldspawn iw8r_stock_retexture "from=to; from=to").
    std::vector<std::pair<QString, QString>> retexturePairs() const;
    bool setRetexturePair(const QString& from, const QString& to);  // adds, or replaces the pair of `from`
    bool removeRetexturePair(const QString& from);
    QStringList worldMaterials() const;  // the stock world's surface materials (the From field's completer)
    QString browserMaterial() const;     // the Materials browser's highlighted material, else the current material
    // World removal boxes (iw8r_remove_world entities), in document order.
    std::vector<Id> removalBoxes() const;
    Id addRemovalBoxAroundSelection();  // a caulk box around the selection's bounds; kNoId without a selection
    Id makeSelectedBrushesBoxes();      // the selected world brushes become one removal entity (one box each)
    bool setRemovalBoxRender(Id entity, bool removeDrawing);
    bool setRemovalBoxCollision(Id entity, bool removeCollision);
    size_t hiddenPropCount() const;
    bool stockMapOpen() const;

    // Widgets, for the GUI test and the feature shots.
    QSlider* intensitySlider() const { return intensitySlider_; }
    QDoubleSpinBox* intensitySpin() const { return intensitySpin_; }
    QDoubleSpinBox* pitchSpin() const { return pitchSpin_; }
    QDoubleSpinBox* yawSpin() const { return yawSpin_; }
    QComboBox* skyCombo() const { return skyCombo_; }
    QTableWidget* retextureTable() const { return retexTable_; }
    QListWidget* removalList() const { return boxList_; }
    QLineEdit* fromEdit() const { return fromEdit_; }
    QLineEdit* toEdit() const { return toEdit_; }
    QScrollArea* scrollArea() const { return scroll_; }
    QLabel* sunInfo() const { return sunInfo_; }
    QLabel* propsInfo() const { return propsInfo_; }
    QLabel* pickedPropInfo() const { return pickedPropInfo_; }

public slots:
    // A reference surface's material picked in the camera (for the main window to call once surface picking exists):
    // the retexture's From field takes it (and To the pair's material when the map has a pair for it).
    void setPickedMaterial(const QString& material);
    // A stock prop clicked in the camera (MainWindow::stockPropPicked; `row` is its smodel row, as iw8r_hide_smodels
    // lists it): shown in Stock Props with a Hide (or Show) button for just that prop.
    void setPickedProp(const QString& model, uint32_t row);
    bool togglePickedPropHidden();  // one undo step; false without a picked prop
    void refresh();  // reads the document again (on documentChanged, deferred; on show; twice a second while shown)

protected:
    void showEvent(QShowEvent* e) override;
    void changeEvent(QEvent* e) override;  // font / style changes: the row labels are sized again

private:
    struct KeyEdit {
        std::string key;
        std::optional<std::string> value;  // nullopt = remove the key
    };
    // Key edits of one entity as one undo step named `step` (see the merge rule above). Returns whether the document
    // changed. The status goes to the status bar (a merged change only there, not to the Console).
    bool editKeys(Id entity, const QString& step, const std::vector<KeyEdit>& edits, const QString& mergeTag, const QString& status);
    bool editWorld(const QString& step, const std::vector<KeyEdit>& edits, const QString& mergeTag, const QString& status);
    bool canMerge(const QString& step, const QString& mergeTag) const;
    void noteMerge(const QString& mergeTag);
    void endMerge() { mergeTag_.clear(); }
    void scheduleRefresh();
    void updateStockVisibility();  // the side bar icon only while a stock map is open; the dock closes with the map
    void bindGameLighting();       // the View > Game Lighting command and the Sun's check box follow each other
    void refreshSun();
    void refreshSky();
    void refreshRetexture();
    void refreshBoxes();
    void refreshProps();
    void updateSelectionButtons();
    void writeRetexture(const std::vector<std::pair<QString, QString>>& pairs, const QStringList& extras, const QString& step,
                        const QString& status);
    void applyDirection(const QString& mergeTag);
    void pickColor();
    void selectBox(Id entity, bool frame);
    Id selectedBox() const;

    MainWindow* w_;
    EditorContext* ctx_;
    QStackedWidget* pages_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QLabel* header_ = nullptr;
    // Sun
    QToolButton* colorButton_ = nullptr;
    QLineEdit* colorEdit_ = nullptr;
    QSlider* intensitySlider_ = nullptr;
    QDoubleSpinBox* intensitySpin_ = nullptr;
    QSlider* pitchSlider_ = nullptr;
    QDoubleSpinBox* pitchSpin_ = nullptr;
    QSlider* yawSlider_ = nullptr;
    QDoubleSpinBox* yawSpin_ = nullptr;
    QLabel* sunInfo_ = nullptr;
    std::vector<QLabel*> rowLabels_;  // Colour / Intensity / Pitch / Yaw: one width, from the current font
    void fitRowLabels();
    QCheckBox* gameLighting_ = nullptr;
    QPushButton* resetSun_ = nullptr;
    // Sky
    QComboBox* skyCombo_ = nullptr;
    QLabel* skyInfo_ = nullptr;
    // Retexture
    QTableWidget* retexTable_ = nullptr;
    QLineEdit* fromEdit_ = nullptr;
    QLineEdit* toEdit_ = nullptr;
    QStringListModel* fromModel_ = nullptr;
    QLabel* retexInfo_ = nullptr;
    QPushButton* retexSet_ = nullptr;
    QPushButton* retexRemove_ = nullptr;
    // Removal boxes
    QListWidget* boxList_ = nullptr;
    QPushButton* boxAdd_ = nullptr;
    QPushButton* boxMake_ = nullptr;
    QPushButton* boxSelect_ = nullptr;
    QPushButton* boxDelete_ = nullptr;
    QPushButton* boxCollision_ = nullptr;
    QLabel* boxInfo_ = nullptr;
    // Hidden props
    QLabel* propsInfo_ = nullptr;
    QPushButton* propsHide_ = nullptr;
    QPushButton* propsShow_ = nullptr;
    QLabel* pickedPropInfo_ = nullptr;
    QPushButton* pickedPropHide_ = nullptr;
    QString pickedModel_;
    std::optional<uint32_t> pickedRow_;
    QString pickedZone_;  // the stock map the prop was picked on

    QPointer<QAction> sideAction_;     // the side bar icon (main_window.cpp names it side_Stock_Map)
    QPointer<QAction> gameAction_;     // View > Game Lighting (ToggleGameLighting)
    QPointer<ads::CDockWidget> dock_;  // the dock that holds this panel
    QTimer poll_;                      // while shown: the stock sun and world arrive without a document change
    QString mergeTag_;
    size_t mergePos_ = 0;
    QElapsedTimer mergeClock_;
    bool sliderHeld_ = false;   // a slider is being dragged (or the colour picker is open): changes merge
    bool refreshQueued_ = false;
    QString shownSig_;          // what the controls were filled from (document revision, zone, stock sun, world)
    QString skyZone_;           // the zone the Sky list was filled for
    const void* materialsWorld_ = nullptr;  // the stock world the From completer was filled from
};

}  // namespace iw8r::qt
