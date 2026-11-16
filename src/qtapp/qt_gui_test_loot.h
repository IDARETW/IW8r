// IW8 Radiant (Qt shell) - GUI checks for the loot points of a Warzone stock map (roadmap 13.5, research 89):
// the points of mp_br_tut become entities, are edited, and reach the edits file of the stock rebuild; the entity text
// never holds them. Called from the GUI test runner (qt_gui_test.cpp). Skipped without the game's mp_br_tut zone.
#pragma once

#include <QString>

#include <functional>

namespace iw8r::qt {

class MainWindow;

// Files are written under <outDir>/loot_points. The game folder is only read (ACTS export of map_ents).
void runLootPointChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check,
                        std::function<void(const QString&)> skip, MainWindow& w, const QString& outDir);

}  // namespace iw8r::qt
