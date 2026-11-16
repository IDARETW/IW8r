// IW8 Radiant (Qt shell) - GUI checks for the side bar's Stock Map panel (stock_map_panel.h): its icon shows
// only while a stock map is open, it fits the narrow side section, and every control edits the stock map keys with
// one undo step per finished change (a slider drag is one step). Called from the GUI test runner (qt_gui_test.cpp)
// after the loot checks, with mp_br_tut open, and from the feature shots (IW8R_FEATURE_SHOTS_ONLY=stock_panel).
#pragma once

#include <QString>

#include <functional>

namespace iw8r::qt {

class MainWindow;

// Leaves the document as it found it (every edit undone). `closeAndReopen`: the check that the icon and the panel go
// when another map is opened (New Map), then the stock map `zonePath` is opened again (its ACTS export is cached).
void runStockPanelChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check,
                         std::function<void(const QString&)> skip, MainWindow& w, const QString& zonePath, bool closeAndReopen);

}  // namespace iw8r::qt
