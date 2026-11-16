import io

# Leaving a prefab made easier (owner checklist 2026-11-15, B6: "it should be made easier to exit the prefab"): a
# "Leave Prefab" button in the status bar while a prefab is being edited, and Esc with nothing selected leaves it.

ROOT = 'E:/IW8-Radiant/src/qtapp/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('main_window.cpp', [
('''    addCommand(edit, "UnSelectSelection", "D&eselect", QKeySequence(Qt::Key_Escape), [this] {
        ctx_.doc.deselectAll();
        ctx_.documentEdited();
    });''', '''    addCommand(edit, "UnSelectSelection", "D&eselect", QKeySequence(Qt::Key_Escape), [this] {
        // Inside a prefab with nothing selected, Esc leaves the prefab (Edit > Leave Prefab).
        if (ctx_.doc.selection().empty() && prefabDepth() > 0) {
            leavePrefab();
            return;
        }
        ctx_.doc.deselectAll();
        ctx_.documentEdited();
    });'''),
])

rw('main_window_prefabs.cpp', [
('''    prefabStack_.push_back({qs(ctx_.doc.path()), ctx_.cam});''', '''    prefabStack_.push_back({qs(ctx_.doc.path()), ctx_.cam});
    QTimer::singleShot(0, this, [this] { updatePrefabBanner(); });'''),
('''    const PrefabParent parent = prefabStack_.back();
    prefabStack_.pop_back();''', '''    const PrefabParent parent = prefabStack_.back();
    prefabStack_.pop_back();
    QTimer::singleShot(0, this, [this] { updatePrefabBanner(); });'''),
('''bool MainWindow::enterPrefab() {''', '''// While a prefab is edited: a button in the status bar that leaves it (also Esc with nothing selected, Ctrl+Shift+Q).
void MainWindow::updatePrefabBanner() {
    if (!prefabLeave_) {
        prefabLeave_ = new QPushButton(this);
        prefabLeave_->setObjectName("prefabLeaveButton");
        prefabLeave_->setToolTip("Edit > Leave Prefab (Ctrl+Shift+Q, or Esc with nothing selected): back to the map that places it");
        connect(prefabLeave_, &QPushButton::clicked, this, [this] { leavePrefab(); });
        statusBar()->insertPermanentWidget(0, prefabLeave_);
    }
    prefabLeave_->setText(prefabStack_.empty() ? QString()
                                               : QString("Leave Prefab (back to %1)").arg(QFileInfo(prefabStack_.back().path).fileName()));
    prefabLeave_->setVisible(!prefabStack_.empty());
}

bool MainWindow::enterPrefab() {'''),
])

rw('main_window.h', [
('''    int prefabDepth() const { return int(prefabStack_.size()); }''', '''    int prefabDepth() const { return int(prefabStack_.size()); }
    void updatePrefabBanner();
    QPushButton* prefabLeave_ = nullptr;'''),
])

s = io.open(ROOT + 'main_window_prefabs.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
for inc in ('#include <QPushButton>', '#include <QStatusBar>', '#include <QTimer>', '#include <QFileInfo>'):
    if inc not in s:
        s = s.replace('#include "main_window.h"' + nl, '#include "main_window.h"' + nl + inc + nl, 1)
io.open(ROOT + 'main_window_prefabs.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
