// IW8 Radiant (Qt shell) - GUI checks of the gizmo (owner 2026-11-15): the Move / Rotate / Scale / All dropdown,
// the All gizmo's parts, drags that follow the gizmo's local axes, and the entity range shapes (light radius and cone,
// sound distance). Called from the GUI test runner (qt_gui_test.cpp).
#pragma once

#include <QString>

#include <functional>

namespace iw8r::qt {

class MainWindow;

// Leaves the document, the camera, the gizmo mode and its saved setting as they were.
void runGizmoChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w);

}  // namespace iw8r::qt
