// IW8 Radiant (Qt shell) - number fields change by dragging (owner 2026-11-15: "numeric values should be able to
// be dragged to change the value, with constant scrolling after a certain distance").
//
// Every QSpinBox / QDoubleSpinBox of the application (one event filter on the application, so dialogs and panels made
// later have it too):
//   - press on a field that is not being typed in and drag sideways: the value follows the mouse, one step per 4 px
//     (Shift: one step per 16 px, finer; Ctrl: ten steps, coarser). A press that does not move is the usual click
//     (the caret, then typing). Esc during a drag puts the value back.
//   - further than kZone px from where the drag started, the value keeps scrolling while the button is held (one step
//     every 30 ms at the zone's edge, faster further out); back inside the zone it follows the mouse again.
//   - over a field that is not being typed in, the pointer is the sideways arrow.
//   - one drag is one undo step: the begin / end hooks (MainWindow: Document::beginGroup / endGroup) wrap its edits.
#pragma once

#include <QString>
#include <functional>

class QAbstractSpinBox;

namespace iw8r::qt::numeric_drag {

constexpr int kZone = 120;  // px from the drag's start before the value keeps scrolling on its own

void install();  // once (later calls do nothing)
// What a drag does around its edits; `what` names the field ("Offset X").
void setHooks(std::function<void(const QString& what)> begin, std::function<void()> end);
bool dragging();
QString fieldName(const QAbstractSpinBox* spin);  // the label beside the field, else its tooltip's first line

}  // namespace iw8r::qt::numeric_drag
