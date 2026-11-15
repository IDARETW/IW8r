#include "feature_shots.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QTest>
#include <QBuffer>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWidget>

#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>

#include "asset_browser.h"
#include "asset_usage_dialog.h"
#include "atmosphere_dialog.h"
#include "brush_tools.h"
#include "camera_view.h"
#include "command_palette.h"
#include "compile_panel.h"
#include "devmap_cover.h"
#include "find_dialog.h"
#include "fx_editor.h"
#include "material_cache.h"
#include "grid_view.h"
#include "help_panel.h"
#include "ies_browser.h"
#include "link_graph_view.h"
#include "main_window.h"
#include "material_editor.h"
#include "model_cache.h"
#include "outliner_panel.h"
#include "prefab_browser.h"
#include "sun_sky_dialog.h"
#include "vfx_preview.h"
#include "vision_editor_dialog.h"

namespace iw8r::qt {

namespace {

// ------------------------------------------------------------------ keeping every window off the monitors

// The union of all monitors: windows are kept to the left of it.
QRect virtualDesktop() {
    QRect virt;
    for (QScreen* s : QGuiApplication::screens()) virt = virt.united(s->geometry());
    return virt;
}

// Every top-level window (dialogs, floating docks, the cover card) that would show on a monitor is moved off them
// before it is mapped, and is shown without taking the focus. Popups and tooltips are left alone (none are opened).
class OffscreenKeeper : public QObject {
public:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Show && e->type() != QEvent::Resize && e->type() != QEvent::Move) return false;
        auto* wd = qobject_cast<QWidget*>(o);
        if (!wd || !wd->isWindow()) return false;
        const Qt::WindowType type = wd->windowType();
        if (type == Qt::Popup || type == Qt::ToolTip) return false;
        if (e->type() == QEvent::Show) wd->setAttribute(Qt::WA_ShowWithoutActivating);
        const QRect virt = virtualDesktop();
        const QRect g = wd->frameGeometry().isValid() ? wd->frameGeometry() : wd->geometry();
        if (g.right() >= virt.left() - 20 && !moving_) {
            moving_ = true;
            wd->move(virt.left() - std::max(g.width(), wd->width()) - 200, virt.top() + 40);
            moving_ = false;
        }
        return false;
    }

private:
    bool moving_ = false;
};

// ------------------------------------------------------------------ waiting

// Lets the editor work for `ms`: events are processed and the camera renders real frames (update() alone does not
// paint an off-screen window), so textures, models and thumbnails that load in the background arrive.
void settle(MainWindow& w, int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (w.cameraView()) w.cameraView()->renderAndCount();
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        QThread::msleep(15);
    }
}

// Works like settle() until `done` holds or `timeoutMs` passed; true when it held.
bool waitFor(MainWindow& w, const std::function<bool()>& done, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (done()) return true;
        settle(w, 250);
    }
    return done();
}

// Waits until the texture cache has been idle for two seconds (stock textures exported and loaded), at most timeoutMs.
void waitIdle(MainWindow& w, int timeoutMs) {
    QElapsedTimer t, idle;
    t.start();
    idle.start();
    while (t.elapsed() < timeoutMs) {
        settle(w, 250);
        if (w.materialCache() && w.materialCache()->busy()) idle.restart();
        if (idle.elapsed() > 2000) break;
    }
    settle(w, 1500);
}

// Only processes events (inside a modal dialog's own loop, where the camera is not drawn).
void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        QThread::msleep(15);
    }
}

// ------------------------------------------------------------------ pictures

// The whole window: the Qt widgets plus the D3D camera frame, which QWidget::grab() cannot see.
QImage grabWindow(MainWindow& w) {
    QPixmap shot = w.grab();
    if (CameraView* cam = w.cameraView(); cam && cam->isVisible() && cam->window() == &w) {
        QPainter painter(&shot);
        const QImage frame = cam->grabFrame();
        const QPoint at = cam->mapTo(&w, QPoint(0, 0));
        if (!frame.isNull()) painter.drawImage(QRect(at, cam->size()), frame);
    }
    return shot.toImage();
}

// No Windows user folder on a picture: paths under the home folder are shown as %USERPROFILE%.
void scrubPaths(QWidget* root) {
    if (!root) return;
    const QString home = QDir::homePath();
    const QString homeNative = QDir::toNativeSeparators(home);
    const auto fix = [&](const QString& s, bool* changed) {
        QString t = s;
        t.replace(home, "%USERPROFILE%", Qt::CaseInsensitive);
        t.replace(homeNative, "%USERPROFILE%", Qt::CaseInsensitive);
        *changed = t != s;
        return t;
    };
    QList<QWidget*> all = root->findChildren<QWidget*>();
    all.prepend(root);
    for (QWidget* x : all) {
        const QSignalBlocker block(x);
        bool c = false;
        if (auto* l = qobject_cast<QLabel*>(x)) {
            const QString t = fix(l->text(), &c);
            if (c) l->setText(t);
        } else if (auto* e = qobject_cast<QLineEdit*>(x)) {
            const QString t = fix(e->text(), &c);
            if (c) e->setText(t);
        } else if (auto* p = qobject_cast<QPlainTextEdit*>(x)) {
            const QString t = fix(p->toPlainText(), &c);
            if (c) p->setPlainText(t);
        } else if (auto* te = qobject_cast<QTextEdit*>(x)) {
            const QString t = fix(te->toHtml(), &c);
            if (c) te->setHtml(t);
        } else if (auto* lw = qobject_cast<QListWidget*>(x)) {
            for (int i = 0; i < lw->count(); ++i) {
                const QString t = fix(lw->item(i)->text(), &c);
                if (c) lw->item(i)->setText(t);
            }
        } else if (auto* tw = qobject_cast<QTreeWidget*>(x)) {
            for (QTreeWidgetItemIterator it(tw); *it; ++it)
                for (int col = 0; col < tw->columnCount(); ++col) {
                    const QString t = fix((*it)->text(col), &c);
                    if (c) (*it)->setText(col, t);
                }
        } else if (auto* tb = qobject_cast<QTableWidget*>(x)) {
            for (int r = 0; r < tb->rowCount(); ++r)
                for (int col = 0; col < tb->columnCount(); ++col)
                    if (QTableWidgetItem* item = tb->item(r, col)) {
                        const QString t = fix(item->text(), &c);
                        if (c) item->setText(t);
                    }
        } else if (auto* cb = qobject_cast<QComboBox*>(x)) {
            for (int i = 0; i < cb->count(); ++i) {
                const QString t = fix(cb->itemText(i), &c);
                if (c) cb->setItemText(i, t);
            }
        }
    }
    if (auto* mw = qobject_cast<QMainWindow*>(root); mw && mw->statusBar()) {
        bool c = false;
        const QString t = fix(mw->statusBar()->currentMessage(), &c);
        if (c) mw->statusBar()->showMessage(t);
    }
}

// PNG under 1.5 MB: a picture that is larger is scaled down a little until it fits.
bool savePng(QImage img, const QString& path) {
    for (int i = 0; i < 8; ++i) {
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!img.save(&buf, "PNG")) return false;
        if (bytes.size() <= 1450 * 1024 || i == 7) {
            QFile f(path);
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
        }
        img = img.scaled(int(img.width() * 0.88), int(img.height() * 0.88), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return false;
}

// ------------------------------------------------------------------ modal safety

// A modal window that nobody answers would stop the run: message boxes are answered with their safe button (Discard
// for "save changes?", else No / Cancel) after a moment; other dialogs are rejected after a minute. ACTS progress
// dialogs are left to finish.
class ModalGuard : public QObject {
public:
    explicit ModalGuard(QStringList* report) : report_(report) {
        timer_.setInterval(400);
        connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
        timer_.start();
    }

private:
    void tick() {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) {
            seen_ = nullptr;
            return;
        }
        if (m != seen_) {
            seen_ = m;
            since_.restart();
            return;
        }
        const bool box = qobject_cast<QMessageBox*>(m) != nullptr;
        const int limit = qobject_cast<QProgressDialog*>(m) ? 15 * 60 * 1000 : (box ? 2500 : 60000);
        if (since_.elapsed() < limit) return;
        QString text = m->windowTitle();
        if (auto* mb = qobject_cast<QMessageBox*>(m)) {
            text += ": " + mb->text().left(120);
            QAbstractButton* pick = nullptr;
            for (QAbstractButton* b : mb->buttons())
                if (mb->buttonRole(b) == QMessageBox::DestructiveRole) pick = b;
            if (!pick)
                for (QAbstractButton* b : mb->buttons())
                    if (mb->buttonRole(b) == QMessageBox::NoRole || mb->buttonRole(b) == QMessageBox::RejectRole) pick = b;
            if (pick) pick->click();
            else mb->reject();
        } else if (auto* d = qobject_cast<QDialog*>(m)) {
            d->reject();
        } else {
            m->close();
        }
        *report_ << "-\twatchdog\tclosed an unanswered window: " + text;
        seen_ = nullptr;
    }
    QTimer timer_;
    QPointer<QWidget> seen_;
    QElapsedTimer since_;
    QStringList* report_;
};

// ------------------------------------------------------------------ docks

// ADS takes the widgets of tabs that are not current out of the widget tree, so docks are found through the manager.
ads::CDockWidget* dockNamed(MainWindow& w, const QString& name) {
    if (auto* m = w.findChild<ads::CDockManager*>()) return m->findDockWidget(name);
    return nullptr;
}

// Floats a dock as its own window of `size` (off the monitors); ResetLayout docks it again.
ads::CDockWidget* floatDock(MainWindow& w, const QString& name, const QSize& size) {
    ads::CDockWidget* d = dockNamed(w, name);
    if (!d) return nullptr;
    d->toggleView(true);
    d->setAsCurrentTab();
    QApplication::processEvents();
    d->setFloating();
    QApplication::processEvents();
    if (ads::CFloatingDockContainer* f = d->floatingDockContainer()) f->resize(size);
    QApplication::processEvents();
    return d;
}

// Types into a browser's search box (the first line edit), as the mapper does; setFilter when it has none.
void typeFilter(AssetBrowser* b, const QString& text) {
    if (!b) return;
    if (auto* e = b->findChild<QLineEdit*>()) e->setText(text);
    b->setFilter(text);
    QApplication::processEvents();
}

// ------------------------------------------------------------------ the document

const Entity* entityNamed(MainWindow& w, const std::string& targetname) {
    for (const Entity& e : w.context().doc.map().entities)
        if (const std::string* tn = e.value("targetname"); tn && *tn == targetname) return &e;
    return nullptr;
}

Vec3 originOf(MainWindow& w, const std::string& targetname, Vec3 fallback) {
    if (const Entity* e = entityNamed(w, targetname))
        if (auto o = e->origin()) return *o;
    return fallback;
}

// Selects an entity as a click would: a point entity whole, a brush entity by its brushes.
void selectEntity(MainWindow& w, Id id, bool additive = false) {
    Document& doc = w.context().doc;
    if (!additive) doc.deselectAll();
    const Entity* e = doc.findEntity(id);
    if (!e) return;
    bool prims = false;
    for (const EntityItem& it : e->items)
        if (it.prim()) {
            doc.select({id, it.id}, true);
            prims = true;
        }
    if (!prims) doc.select({id, kNoId}, true);
    w.context().documentEdited();
}

bool selectNamed(MainWindow& w, const std::string& targetname, bool additive = false) {
    const Entity* e = entityNamed(w, targetname);
    if (!e) return false;
    selectEntity(w, e->id, additive);
    return true;
}

// The camera looks at `target` from `dist` units away, along yaw / pitch (degrees, pitch < 0 looks down).
void look(MainWindow& w, const Vec3& target, double yaw, double pitch, double dist) {
    auto& c = w.context().cam;
    c.yaw = yaw;
    c.pitch = pitch;
    c.pos = target - c.forward() * dist;
    w.context().cameraMoved();
}

// The camera at `pos`, looking along yaw / pitch (degrees).
void place(MainWindow& w, const Vec3& pos, double yaw, double pitch) {
    auto& c = w.context().cam;
    c.pos = pos;
    c.yaw = yaw;
    c.pitch = pitch;
    w.context().cameraMoved();
}

// The 2D view centred on (x, y) at `scale` pixels per unit.
void centerGrid(MainWindow& w, double x, double y, double scale) {
    EditorContext& ctx = w.context();
    ctx.gridAxis = 0;
    ctx.gridCenterH = x;
    ctx.gridCenterV = y;
    ctx.gridScale = scale;
    ctx.cameraMoved();
    if (w.gridView()) w.gridView()->update();
}

// Undoes every edit of the run, so the open map is unchanged (and never autosaved or asked about).
void revert(MainWindow& w) {
    Document& doc = w.context().doc;
    for (int guard = 0; guard < 100000 && doc.modified() && doc.undo(); ++guard) {}
    doc.deselectAll();
    w.context().documentEdited();
}

// The showcase map beside the editor (samples/dev/mp_iw8r_show2.map), found from the exe folder upward.
QString repoFile(const QString& rel) {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath(rel))) return d.filePath(rel);
    return {};
}

// ------------------------------------------------------------------ the run

struct Shots {
    MainWindow& w;
    QString dir;
    QStringList report;
    int failed = 0;

    void note(const QString& file, const QString& what, const QString& state) { report << file + "\t" + what + "\t" + state; }
    void skip(const QString& file, const QString& what, const QString& why) {
        note(file, what, "skipped: " + why);
        ++failed;
    }
    void saved(const QImage& img, const QString& file, const QString& what) {
        const bool ok = !img.isNull() && savePng(img, QDir(dir).filePath(file));
        note(file, what, ok ? "ok" : "could not be saved");
        failed += ok ? 0 : 1;
    }

    // A picture of the whole editor window.
    void window(const QString& file, const QString& what, int settleMs = 1500) {
        settle(w, settleMs);
        scrubPaths(&w);
        saved(grabWindow(w), file, what);
    }

    // A picture of one widget (a floating dock, a dialog made without exec()).
    void widget(QWidget* target, const QString& file, const QString& what, int settleMs = 800) {
        if (!target) return skip(file, what, "the widget does not exist");
        settle(w, settleMs);
        scrubPaths(target);
        saved(target->grab().toImage(), file, what);
    }

    // A floating dock (its own window of `size`), filled by `prepare`, waited for with `ready`.
    void dock(const QString& name, const QSize& size, const QString& file, const QString& what, const std::function<void(QWidget*)>& prepare = {},
              const std::function<bool()>& ready = {}, int settleMs = 1200) {
        ads::CDockWidget* d = floatDock(w, name, size);
        if (!d) return skip(file, what, "no dock named " + name);
        settle(w, 400);
        if (prepare) prepare(d->widget());
        if (ready) waitFor(w, ready, 60000);
        widget(d, file, what, settleMs);
        trigger("ResetLayout");
        settle(w, 300);
    }

    // Runs `open`, which shows a window (modal: exec() runs inside; or not modal), waits `waitMs` (and for `ready`),
    // takes the window that appeared, and closes it again (reject for dialogs: nothing is applied).
    void opened(const QString& file, const QString& what, const std::function<void()>& open, int waitMs,
                const std::function<void(QWidget*)>& prepare = {}, const std::function<bool(QWidget*)>& ready = {}) {
        QSet<QWidget*> before;
        for (QWidget* t : QApplication::topLevelWidgets())
            if (t->isVisible()) before.insert(t);
        auto done = std::make_shared<bool>(false);
        auto finish = [this, done, file, what, prepare, ready](QWidget* win, bool modal) {
            if (*done) return;
            *done = true;
            if (!win) return skip(file, what, "no window appeared");
            if (prepare) prepare(win);
            if (ready) {
                QElapsedTimer t;
                t.start();
                while (!ready(win) && t.elapsed() < 60000) modal ? pump(250) : settle(w, 250);
            }
            modal ? pump(500) : settle(w, 500);
            scrubPaths(win);
            QImage img = win->grab().toImage();
            // A window may ask to be cut to its filled part (property featureShotHeight, in widget pixels).
            if (const int h = win->property("featureShotHeight").toInt(); h > 0 && h < win->height())
                img = img.copy(0, 0, img.width(), int(h * img.devicePixelRatio()));
            saved(img, file, what);
            if (auto* d = qobject_cast<QDialog*>(win)) d->reject();
            else win->close();
        };
        QTimer::singleShot(waitMs, qApp, [done, finish] {
            if (*done) return;
            if (QWidget* m = QApplication::activeModalWidget(); m && !qobject_cast<QProgressDialog*>(m)) finish(m, true);
        });
        open();
        if (*done) {
            settle(w, 300);
            return;
        }
        settle(w, waitMs);
        if (*done) return;
        QWidget* found = nullptr;
        for (QWidget* t : QApplication::topLevelWidgets())
            if (t->isVisible() && t != &w && !before.contains(t) && t->windowType() != Qt::Popup && t->windowType() != Qt::ToolTip &&
                (!found || t->width() * t->height() > found->width() * found->height()))
                found = t;
        finish(found, false);
        settle(w, 300);
    }

    // A dialog this code made (not by exec()): shown off the monitors, pictured, closed and deleted.
    void made(QWidget* dlg, const QString& file, const QString& what, int settleMs = 1000, const std::function<bool()>& ready = {}) {
        if (!dlg) return skip(file, what, "the dialog could not be made");
        dlg->show();
        settle(w, 200);
        if (ready) waitFor(w, ready, 60000);
        widget(dlg, file, what, settleMs);
        dlg->close();
        dlg->deleteLater();
        settle(w, 200);
    }

    bool trigger(const char* command) {
        if (QAction* a = w.action(command)) {
            a->trigger();
            return true;
        }
        return false;
    }
};

// A camera frame in a given mode, labelled, for the camera-mode sheet.
QImage modeFrame(MainWindow& w, const char* command, const QString& label, int fw, int fh) {
    if (QAction* a = w.action(command)) a->trigger();
    settle(w, 1500);
    QImage img = w.cameraView()->renderImage(fw, fh, false, 0, 1500).convertToFormat(QImage::Format_RGB32);
    QPainter p(&img);
    QFont f = p.font();
    f.setPointSize(13);
    f.setBold(true);
    p.setFont(f);
    const QRect box(10, 10, QFontMetrics(f).horizontalAdvance(label) + 20, QFontMetrics(f).height() + 10);
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, label);
    return img;
}

// ------------------------------------------------------------------ stock textures

// Waits until the camera's textures have stopped arriving: the loaded-texture count, the resolved stock reference
// materials and the reference prop materials unchanged for `stableMs`, nothing left for the next frame and the
// material cache idle; at most `timeoutMs`. Returns a line for the report.
QString waitTexturesStable(MainWindow& w, int stableMs, int timeoutMs) {
    CameraView* cam = w.cameraView();
    if (!cam) return "no camera";
    QElapsedTimer t, still;
    t.start();
    still.start();
    auto state = [cam] {
        return std::array<size_t, 3>{cam->loadedTextures(), cam->referenceTexturesResolved(), cam->referenceTexInstancesResolved()};
    };
    std::array<size_t, 3> last = state();
    bool stable = false;
    while (t.elapsed() < timeoutMs) {
        settle(w, 500);
        const std::array<size_t, 3> now = state();
        const bool busy = cam->texturesPending() || (w.materialCache() && w.materialCache()->busy());
        if (now != last || busy) {
            last = now;
            still.restart();
        } else if (still.elapsed() >= stableMs) {
            stable = true;
            break;
        }
    }
    return QString("%1 after %2 s: %3 textures, %4 reference materials and %5 prop materials resolved")
        .arg(stable ? "textures stable" : "textures still arriving (timeout)")
        .arg(t.elapsed() / 1000)
        .arg(last[0])
        .arg(last[1])
        .arg(last[2]);
}

// Share of the picture drawn with the missing-material checker (magenta, also when lit or shaded).
double checkerShare(const QImage& src) {
    if (src.isNull()) return 1.0;
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    size_t hits = 0, all = 0;
    for (int y = 0; y < img.height(); y += 2) {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2, ++all) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            if (r > 60 && b > 60 && r > 2 * g && b > 2 * g && std::abs(r - b) * 3 < std::max(r, b)) ++hits;
        }
    }
    return all ? double(hits) / double(all) : 1.0;
}

// Share of the picture that is sky or empty background (the top rows' colour): a view of the sky is no view.
double emptyShare(const QImage& src) {
    if (src.isNull()) return 1.0;
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    const QRgb bg = img.pixel(img.width() / 2, 1);
    size_t hits = 0, all = 0;
    for (int y = 0; y < img.height(); y += 2) {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2, ++all)
            if (std::abs(qRed(line[x]) - qRed(bg)) + std::abs(qGreen(line[x]) - qGreen(bg)) + std::abs(qBlue(line[x]) - qBlue(bg)) < 24) ++hits;
    }
    return all ? double(hits) / double(all) : 1.0;
}

// How much the picture shows at a large scale: the spread of brightness over a coarse 32 x 20 grid. A field of grass
// or a flat wall is low; buildings, props and shadows are high.
double coarseDetail(const QImage& src) {
    if (src.isNull()) return 0;
    const QImage img = src.scaled(32, 20, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    double sum = 0, sum2 = 0;
    const int n = img.width() * img.height();
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const double l = qGray(img.pixel(x, y));
            sum += l;
            sum2 += l * l;
        }
    const double mean = sum / n;
    return std::sqrt(std::max(0.0, sum2 / n - mean * mean));
}

struct View {
    Vec3 target;
    double yaw = 45, pitch = -30, dist = 1500;
};

// Of the candidate views, the one that shows the most (coarseDetail) among those with at most `maxChecker` of the
// missing-material checker (stock materials the cache cannot resolve stay checkered; they should not fill the
// picture); when none is that clean, the one with the least checker. Views mostly of sky are passed over. `why`
// gets a line for the report.
View leastChecker(MainWindow& w, const std::vector<View>& views, QString* why, double maxChecker = 0.03, const QString& debugName = {}) {
    struct Scored {
        View v;
        double checker, detail;
        bool sky;
    };
    std::vector<Scored> all;
    // Rendered at the camera's own shape, so the score is about what the picture will show. IW8R_FEATURE_SHOTS_DEBUG
    // saves every candidate with its scores (beside the pictures, in candidates/).
    CameraView* cam = w.cameraView();
    const double aspect = cam->height() > 0 ? double(cam->width()) / cam->height() : 1.6;
    const int rw = 560, rh = std::max(200, int(rw / aspect));
    const QString debugDir = qEnvironmentVariableIsSet("IW8R_FEATURE_SHOTS_DEBUG") && !debugName.isEmpty() ? debugName : QString();
    for (const View& v : views) {
        look(w, v.target, v.yaw, v.pitch, v.dist);
        settle(w, 250);
        const QImage img = cam->renderImage(rw, rh, false, 0, 400);
        all.push_back({v, checkerShare(img), coarseDetail(img), emptyShare(img) > 0.45});
        if (!debugDir.isEmpty()) {
            QDir().mkpath(debugDir);
            const Scored& sc = all.back();
            img.save(QDir(debugDir).filePath(QString("%1_c%2_d%3_y%4_p%5_r%6.png")
                                                 .arg(all.size() - 1, 3, 10, QChar('0'))
                                                 .arg(int(sc.checker * 1000))
                                                 .arg(int(sc.detail))
                                                 .arg(int(v.yaw))
                                                 .arg(int(-v.pitch))
                                                 .arg(int(v.dist))));
        }
    }
    const Scored* best = nullptr;
    for (const Scored& s : all)
        if (!s.sky && s.checker <= maxChecker && (!best || s.detail > best->detail)) best = &s;
    const bool clean = best != nullptr;
    if (!best)
        for (const Scored& s : all)
            if (!best || (s.sky ? 1.0 : 0.0) + s.checker < (best->sky ? 1.0 : 0.0) + best->checker) best = &s;
    if (why)
        *why = QString("view chosen of %1 (%2): checker %3 % of the camera (first candidate %4 %), detail %5, yaw %6, pitch %7, distance %8")
                   .arg(all.size())
                   .arg(clean ? "most detail under the checker limit" : "least checker")
                   .arg(best->checker * 100, 0, 'f', 1)
                   .arg(all.front().checker * 100, 0, 'f', 1)
                   .arg(best->detail, 0, 'f', 1)
                   .arg(best->v.yaw)
                   .arg(best->v.pitch)
                   .arg(best->v.dist, 0, 'f', 0);
    const View chosen = best->v;
    look(w, chosen.target, chosen.yaw, chosen.pitch, chosen.dist);
    return chosen;
}

// The same target seen from eight sides, at each of `pitches` and `dists`, for leastChecker.
std::vector<View> around(const Vec3& target, std::initializer_list<double> pitches, std::initializer_list<double> dists, double firstYaw = 45) {
    std::vector<View> v;
    for (double d : dists)
        for (double p : pitches)
            for (int k = 0; k < 8; ++k) v.push_back({target, std::fmod(firstYaw + 45.0 * k, 360.0), p, d});
    return v;
}

// A view picked by eye for the pictures (on the owner's cache: few checkered surfaces, the feature in the middle);
// when it shows more than `limit` checker on another cache, the search over `search` decides instead.
View chosenView(MainWindow& w, const View& fixed, const std::vector<View>& search, double limit, double maxChecker, const QString& debugName,
                QString* why) {
    look(w, fixed.target, fixed.yaw, fixed.pitch, fixed.dist);
    settle(w, 250);
    CameraView* cam = w.cameraView();
    const double aspect = cam->height() > 0 ? double(cam->width()) / cam->height() : 1.6;
    const double checker = checkerShare(cam->renderImage(560, std::max(200, int(560 / aspect)), false, 0, 400));
    if (checker <= limit) {
        if (qEnvironmentVariableIsSet("IW8R_FEATURE_SHOTS_DEBUG")) {  // the search's candidates are dumped all the same
            leastChecker(w, search, nullptr, maxChecker, debugName);
            look(w, fixed.target, fixed.yaw, fixed.pitch, fixed.dist);
        }
        if (why) *why = QString("the fixed view: checker %1 % of the camera").arg(checker * 100, 0, 'f', 1);
        look(w, fixed.target, fixed.yaw, fixed.pitch, fixed.dist);
        return fixed;
    }
    const View v = leastChecker(w, search, why, maxChecker, debugName);
    if (why) *why = QString("the fixed view had %1 % checker; ").arg(checker * 100, 0, 'f', 1) + *why;
    return v;
}

}  // namespace

int runFeatureShots(MainWindow& w, const QString& outDir) {
    QDir().mkpath(outDir);
    OffscreenKeeper keeper;
    qApp->installEventFilter(&keeper);
    Shots s{w, outDir, {}, 0};
    ModalGuard guard(&s.report);
    w.resize(1600, 960);
    EditorContext& ctx = w.context();

    const QString map = repoFile("samples/dev/mp_iw8r_show2.map");
    // IW8R_FEATURE_SHOTS_ONLY=stock: only the stock map pictures (to check them without the whole run).
    const bool stockOnly = qEnvironmentVariable("IW8R_FEATURE_SHOTS_ONLY") == "stock";
    if (stockOnly) {
        s.report << "-\tinfo\tIW8R_FEATURE_SHOTS_ONLY=stock: the showcase pictures are not taken";
    } else if (map.isEmpty()) {
        s.skip("-", "the showcase map", "samples/dev/mp_iw8r_show2.map was not found");
    } else {
        w.openPath(map);
        s.trigger("CameraModeTextured");
        settle(w, 3000);
        waitIdle(w, 120000);

        // ---- the window, the toolbar
        look(w, {1060, 930, 0}, 90, -50, 520);
        selectNamed(w, "s46_fan");
        centerGrid(w, 1150, 1200, 0.22);
        s.window("overview.png", "The editor with the showcase map: textured camera, top 2D view, Entity inspector with the selected fan's keys", 9000);
        // IW8R_FEATURE_SHOTS_ONLY=window: the window and one side bar panel open, then stop (layout work).
        if (qEnvironmentVariable("IW8R_FEATURE_SHOTS_ONLY") == "window") {
            w.showDock("Materials");
            settle(w, 1500);
            s.window("side_panel.png", "The side bar with the Materials panel open beside the camera", 4000);
            // Another browser replaces it; Compile in front under the camera; the area folded away (Ctrl+J).
            if (QAction* a = w.findChild<QAction*>("side_Models")) a->trigger();
            s.window("side_models.png", "The Models browser in the side section (the Materials panel closed by itself)", 3000);
            if (QAction* a = w.findChild<QAction*>("side_Models")) a->trigger();  // folds the side section away
            if (QAction* a = w.findChild<QAction*>("side_Compile")) a->trigger();
            s.window("bottom_compile.png", "The Compile tab in front in the area under the camera, the side section folded", 2000);
            s.trigger("ToggleBottomPanel");
            s.window("bottom_folded.png", "The area under the camera folded away (Ctrl+J): the camera has the whole column", 2000);
            s.trigger("ToggleBottomPanel");
            s.opened("preferences.png", "Edit > Preferences in sections", [&] { s.trigger("Preferences"); }, 1500);
            // The File menu with File > Stock Map open beside it (second review: the stock commands in one submenu).
            {
                QMenu* fileMenu = nullptr;
                for (QAction* a : w.menuBar()->actions())
                    if (a->menu() && a->text().remove('&') == "File") fileMenu = a->menu();
                QMenu* stock = fileMenu ? fileMenu->findChild<QMenu*>("stockMapMenu") : nullptr;
                if (fileMenu && stock) {
                    fileMenu->popup(w.mapToGlobal(QPoint(8, 24)));
                    settle(w, 400);
                    stock->popup(fileMenu->mapToGlobal(QPoint(fileMenu->width(), 0)));
                    settle(w, 400);
                    const QImage a = fileMenu->grab().toImage(), b = stock->grab().toImage();
                    QImage sheet(a.width() + b.width() + 8, std::max(a.height(), b.height()), QImage::Format_ARGB32);
                    sheet.fill(QColor(30, 30, 30));
                    QPainter p(&sheet);
                    p.drawImage(0, 0, a);
                    p.drawImage(a.width() + 8, 0, b);
                    p.end();
                    s.saved(sheet, "file_menu.png", "The File menu and its Stock Map submenu");
                    stock->hide();
                    fileMenu->hide();
                } else {
                    s.skip("file_menu.png", "The File menu and its Stock Map submenu", "no File > Stock Map submenu");
                }
            }
            // Tools > Tool Paths: found / not found beside every path.
            for (QAction* m : w.menuBar()->actions())
                if (m->menu() && m->text().remove('&') == "Tools")
                    for (QAction* a : m->menu()->actions())
                        if (a->text().startsWith("Tool Paths"))
                            s.opened("tool_paths.png", "Tools > Tool Paths with found / not found beside each path", [a] { a->trigger(); }, 1500);
            // The gizmo's scale handle mid-drag: the outline of the scaled bounds before anything changes (Esc cancels).
            if (CameraView* cv = w.cameraView(); cv && !w.context().doc.selection().empty()) {
                EditorContext& ctx = w.context();
                const auto mouse = [cv](QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons held) {
                    QMouseEvent e(t, p, cv->mapToGlobal(p), b, held, Qt::NoModifier);
                    QApplication::sendEvent(cv, &e);
                };
                // The selected fan (s46_fan) in front of the camera, so its outline is in the picture.
                look(w, ctx.doc.selectionBounds().center(), 45, -20, 220);
                settle(w, 1500);
                s.trigger("GizmoScale");
                cv->renderNow();
                const Vec3 c = ctx.doc.selectionBounds().center();
                const double L = std::max(8.0, length(c - ctx.cam.pos) * 0.18);
                std::optional<QPointF> grabAt;
                for (double f : {1.0, 0.95, 0.9, 0.8, 0.7})
                    if (const std::optional<QPointF> p = cv->widgetPointOf(c + Vec3{0, 0, L * f})) {
                        mouse(QEvent::MouseMove, *p, Qt::NoButton, Qt::NoButton);
                        if (cv->gizmoHoverAxis() == 2) {
                            grabAt = p;
                            break;
                        }
                    }
                const std::optional<QPointF> further = cv->widgetPointOf(c + Vec3{0, 0, L * 1.8});
                if (grabAt && further) {
                    mouse(QEvent::MouseButtonPress, *grabAt, Qt::LeftButton, Qt::LeftButton);
                    mouse(QEvent::MouseMove, *further, Qt::NoButton, Qt::LeftButton);
                    s.window("scale_preview.png", "Dragging the gizmo's Z scale handle: the yellow outline is the selection's size after release", 1200);
                    QTest::keyClick(cv, Qt::Key_Escape);
                    mouse(QEvent::MouseButtonRelease, *further, Qt::LeftButton, Qt::NoButton);
                } else {
                    s.skip("scale_preview.png", "The gizmo's scale handle mid-drag", "the Z handle was not found on screen");
                }
                s.trigger("GizmoScale");
            }
            qApp->removeEventFilter(&keeper);
            QFile f(QDir(outDir).filePath("feature-shots.txt"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(s.report.join("\n").toUtf8() + "\n");
            return s.failed == 0 ? 0 : 2;
        }
        if (QToolBar* tb = w.findChild<QToolBar*>("mainToolbar")) {
            QRect used;
            for (QAction* a : tb->actions())
                if (QWidget* aw = tb->widgetForAction(a); aw && aw->isVisible()) used = used.united(aw->geometry());
            QImage img = tb->grab().toImage();
            if (used.isValid()) img = img.copy(QRect(0, 0, std::min(img.width(), used.right() + 8), img.height()));
            s.saved(img, "toolbar.png", "The main toolbar");
        } else {
            s.skip("toolbar.png", "The main toolbar", "no toolbar named mainToolbar");
        }
        ctx.doc.deselectAll();
        ctx.documentEdited();

        // ---- views
        place(w, {230, 352, 140}, 0, -16);  // down the lights row
        centerGrid(w, 1150, 400, 0.35);
        if (s.trigger("ToggleLitPreview")) {
            s.window("lit_preview.png", "Lit preview (Ctrl+L) on the lights row: light entities and the sun with shadows", 6000);
            s.trigger("ToggleLitPreview");
        }
        {
            place(w, {-60, 640, 150}, 0, -18);  // down the brushes row
            const int fw = 800, fh = 450;
            QImage sheet(fw * 2, fh * 2, QImage::Format_RGB32);
            sheet.fill(Qt::black);
            QPainter p(&sheet);
            p.drawImage(0, 0, modeFrame(w, "CameraModeContents", "Contents", fw, fh));
            p.drawImage(fw, 0, modeFrame(w, "CameraModeDensity", "Texel Density", fw, fh));
            p.drawImage(0, fh, modeFrame(w, "CameraModeWireframe", "Wireframe", fw, fh));
            p.drawImage(fw, fh, modeFrame(w, "CameraModeNormals", "Normals", fw, fh));
            p.end();
            s.trigger("CameraModeTextured");
            s.saved(sheet, "camera_modes.png", "Four camera modes on the brushes row: Contents, Texel Density, Wireframe and Normals");
        }
        if (s.trigger("HeatMapSpawns")) {
            centerGrid(w, 1150, 1390, 0.17);
            s.widget(w.gridView(), "heatmap_spawns.png", "Top view with Heat Map > Spawn Density", 1500);
            s.trigger("HeatMapNone");
        }

        // ---- materials
        s.dock("Materials", {1250, 720}, "material_browser.png", "Materials dock with thumbnails, filtered to tm2co/me_",
               [&](QWidget*) { typeFilter(w.materialBrowser(), "tm2co/me_"); },
               [&] {
                   w.materialBrowser()->refreshThumbnails();
                   return w.materialBrowser()->thumbnailsLoaded() >= std::min(30, w.materialBrowser()->visibleCount());
               },
               2500);
        s.opened("material_info.png", "Material Info for the stock material tm2co/me_curb_stone_01",
                 [&] { w.showMaterialInfo("tm2co/me_curb_stone_01"); }, 3000);
        {
            const CustomMaterial* cm = w.customMaterials() ? w.customMaterials()->find("show2/emissive") : nullptr;
            auto* med = new MaterialEditorDialog(QFileInfo(map).absolutePath() + "/materials", cm, &w);
            s.made(med, "material_editor.png", "Material Editor on the showcase's custom material show2/emissive, with the live preview", 2500);
        }
        {
            // A 128-unit wooden cube for the UV editor: its mesh layout unfolds it as a cross.
            const std::string oldMaterial = ctx.currentMaterial;
            ctx.currentMaterial = "tm2co/construction_wood_form_beams_worn_oak_single";
            AABB box;
            box.add({2600, 400, 0});
            box.add({2728, 528, 128});
            createBoxBrush(ctx, box);
            ctx.currentMaterial = oldMaterial;
            ctx.documentEdited();
            s.opened("uv_editor.png", "UV editor on a selected wooden cube: the six faces unfolded as a net with their texture and vertex handles",
                     [&] { w.openUvEditor(); }, 2500);
            revert(w);
        }

        // ---- models and prefabs
        s.dock("Models", {1250, 680}, "model_browser.png", "Models dock: search for cafe furniture and the shaded preview of the selected model",
               [&](QWidget*) {
                   typeFilter(w.modelBrowser(), "furniture_cafe");
                   settle(w, 500);
                   w.modelBrowser()->selectRow(0);
                   w.previewModel(w.modelBrowser()->visibleName(0));
               },
               [&] {
                   if (!w.modelBrowser()->previewHasImage()) w.previewModel(w.modelBrowser()->visibleName(0));
                   return w.modelBrowser()->previewHasImage() && w.modelBrowser()->thumbnailsLoaded() >= std::min(8, w.modelBrowser()->visibleCount());
               },
               2000);
        s.opened("model_info.png", "Model Info for tm/furniture_cafe_chair_01", [&] {
            w.showModelInfo("tm/furniture_cafe_chair_01");
            settle(w, 4000);
            w.showModelInfo("tm/furniture_cafe_chair_01");
        }, 1500);
        {
            place(w, {-60, 1216, 130}, 0, -14);  // down the models row
            settle(w, 5000);
            s.saved(w.cameraView()->renderImage(1280, 720, false, 0, 4000), "models_camera.png",
                    "Textured camera on the models row: stock models, a shape model and custom crates");
        }
        s.dock("Prefabs", {1250, 720}, "prefab_browser.png", "Prefabs dock: the shipped bundles first, then the prefab library, each with its picture",
               {}, [&] { return w.prefabBrowser()->thumbnailsLoaded() >= 12; }, 2500);
        {
            const QString pad = repoFile("data/prefabs/bundles/gameplay/jump_pad.iw8prefab");
            QString err;
            if (!pad.isEmpty() && w.insertPrefab(pad, &err)) {
                s.dock("Entity", {560, 820}, "bundle_panel.png", "Entity dock for a placed jump pad bundle: title, description and its launch speed parameter");
            } else {
                s.skip("bundle_panel.png", "Entity dock for a placed bundle", "the jump pad bundle could not be placed: " + err);
            }
            revert(w);
        }
        {
            // The models and movers of the behaviour row, saved as a prefab: the dialog lists what travels with it.
            ctx.doc.deselectAll();
            for (const char* n : {"s49_spin_roll", "s50_bob_barrel", "s52_lift_plate", "s57_jump_pad"}) selectNamed(w, n, true);
            s.opened("prefab_save_dialog.png", "Save Selection As Prefab: id, title, tags and the list of what travels with the prefab",
                     [&] { s.trigger("SaveSelectionAsPrefab"); }, 2000, [](QWidget* win) {
                         const auto set = [win](const char* name, const QString& text) {
                             if (auto* e = win->findChild<QLineEdit*>(name)) e->setText(text);
                         };
                         set("prefabSaveTitle", "Moving crates and a jump pad");
                         set("prefabSaveCategory", "gameplay");
                         set("prefabSaveTags", "spin, bob, lift, jump");
                         if (auto* d = win->findChild<QPlainTextEdit*>("prefabSaveDescription"))
                             d->setPlainText("A spinning crate, a bobbing barrel, a lift plate and a jump pad, with their behaviour keys.");
                     });
            revert(w);
        }

        // ---- entities
        s.dock("Outliner", {700, 760}, "outliner.png", "Outliner dock: the showcase's entities by class with counts and map statistics");
        s.dock("Links", {1060, 700}, "links.png", "Links dock: target / targetname chains of the showcase as a node view",
               [&](QWidget*) {
                   LinkGraphView* lv = w.linkGraphView();
                   if (!lv) return;
                   lv->rebuild();
                   settle(w, 300);
                   const QPointF at(0, 0);
                   QWheelEvent wheel(at, lv->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                   QApplication::sendEvent(lv, &wheel);
                   const QPointF a(600, 500), b(700, 520);
                   QMouseEvent press(QEvent::MouseButtonPress, a, lv->mapToGlobal(a), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
                   QApplication::sendEvent(lv, &press);
                   QMouseEvent move(QEvent::MouseMove, b, lv->mapToGlobal(b), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
                   QApplication::sendEvent(lv, &move);
                   QMouseEvent release(QEvent::MouseButtonRelease, b, lv->mapToGlobal(b), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
                   QApplication::sendEvent(lv, &release);
               });
        s.made(new CheckMapDialog(&ctx, &w), "check_map.png", "Check Map / Map Doctor findings for the showcase map", 2500);

        // ---- lights, sky, atmosphere
        s.made(new SunSkyDialog(currentSunSky(ctx), w.stockSkyList(), &w), "sun_sky.png", "Compile > Sun & Sky with the showcase's sun and sky");
        s.made(new AtmosphereDialog(currentAtmosphere(ctx), &w), "atmosphere.png", "Compile > Atmosphere: fog and film grade of the showcase");
        {
            const Entity* ws = ctx.doc.worldspawn();
            const std::string* v = ws ? ws->value("iw8r_vision") : nullptr;
            QDialog* vis = createVisionEditorDialog(&w, "Vision Editor: the map", v ? QString::fromStdString(*v) : QString());
            if (vis) {
                vis->resize(1100, 640);
                if (auto* f = vis->findChild<QLineEdit*>("visionFilter")) f->setText("fog");
                if (auto* t = vis->findChild<QTableWidget*>("visionTable")) {
                    settle(w, 300);
                    t->resizeColumnsToContents();
                }
            }
            s.made(vis, "vision_editor.png", "Compile > Vision Editor filtered to the fog fields: each vision field by name and type, with the value to set", 1500);
        }
        {
            auto* ies = new IesBrowserDialog(&ctx, &w);
            ies->setFolder(IesBrowserDialog::defaultFolder());
            ies->selectProfile("iw8r_spot_narrow.ies");
            s.made(ies, "ies_browser.png", "IES Profile Browser: the starter profiles and the polar plot of a narrow spot");
        }

        // ---- effects and sound
        s.dock("Effects", {1000, 440}, "effects_browser.png", "Effects dock filtered to fire effects",
               [&](QWidget*) { typeFilter(w.effectsBrowser(), "fire"); });
        {
            auto* fx = new FxEditorDialog(QFileInfo(map).absolutePath() + "/fx", nullptr, &w);
            fx->openEffect("show2/sparkle");
            s.made(fx, "effect_editor.png", "Effect Editor with the showcase's own sparkle effect: elements, settings and the animated preview", 2500);
        }
        s.opened("effect_info.png", "Effect Info for the stock effect vfx/core/mp/core/vfx_hp_chev_grey: particle preview, emitters, materials and bounds",
                 [&] { w.showEffectInfo("vfx/core/mp/core/vfx_hp_chev_grey"); }, 2000, {},
                 [](QWidget* win) {
                     auto* pv = win->findChild<VfxPreview*>();
                     if (pv && pv->isVisible()) {
                         pv->setTime(0.5);
                         return true;
                     }
                     return false;
                 });
        s.dock("Sounds", {1000, 560}, "sounds_browser.png", "Sounds dock searched for amb_: the ambient aliases, loops in blue",
               [&](QWidget*) { typeFilter(w.soundsBrowser(), "amb_"); });
        // View > Effect Preview in Camera is not pictured: it draws a stock effect only once its ACTS export is cached
        // (MainWindow's effect source), and the showcase's stock fire, smoke and sparks were not exported on the
        // owner's cache, so the camera showed only their markers.

        // ---- script, build, live link
        s.dock("Script", {1250, 760}, "script_dock.png", "Script dock with the showcase's own map script");
        s.opened("map_info.png", "Compile dock > Map Info for the showcase: the fields the build writes into map.json", [&] {
            for (QPushButton* b : w.compilePanel()->findChildren<QPushButton*>())
                if (b->text() == "Map Info...") {
                    b->click();
                    break;
                }
        }, 1500, [](QWidget* win) { win->resize(620, win->height()); });
        s.dock("Build Budget", {900, 250}, "build_budget.png", "Build Budget dock: the showcase's last build against the limits");
        s.dock("Live Link", {1000, 165}, "live_link.png", "Live Link dock (no game running): camera sync, console line, load map, quick match, screenshot and the debug overlay row");
        {
            auto* cover = new DevmapCover();
            cover->setMap("mp_iw8r_show2", "war");
            const QStringList steps{"+0.0 s: build started",         "+6.2 s: build done, deploying",       "+6.9 s: deployed to mods\\mw120r\\maps",
                                    "+7.1 s: starting the game",     "+21.4 s: Multiplayer selected",       "+24.0 s: private match hosted",
                                    "+25.3 s: map and mode set",     "+26.0 s: match starting"};
            for (const QString& l : steps) cover->addLine(l);
            cover->setElapsed(26.0);
            const QRect virt = virtualDesktop();
            cover->coverRect(QRect(virt.left() - 1100, virt.top() + 60, 960, 540));
            s.made(cover, "devmap_cover.png", "The F5 cover card: map name, progress bar and the timeline of the start", 800);
        }
        s.opened("test_checklist.png", "Help > Test Checklist: each in-game check with OK / Wrong / Not tested and a note",
                 [&] { s.trigger("TestChecklist"); }, 2000, [](QWidget* win) {
                     win->resize(1100, 700);
                     pump(300);
                     if (auto* t = win->findChild<QTableWidget*>()) t->resizeRowsToContents();
                 });

        // ---- game modes, asset tools
        s.opened("game_modes.png", "Tools > Game Modes: the presets, with the Warzone variant selected", [&] { s.trigger("GameModes"); }, 1500,
                 [](QWidget* win) {
                     if (auto* list = win->findChild<QListWidget*>("gameModesList"))
                         for (int i = 0; i < list->count(); ++i)
                             if (list->item(i)->text().contains("Warzone", Qt::CaseInsensitive)) {
                                 list->setCurrentRow(i);
                                 break;
                             }
                 });
        s.dock("Images", {1250, 680}, "images_browser.png", "Images dock: catalogue images with the preview of the selected one",
               [&](QWidget*) {
                   typeFilter(w.imagesBrowser(), "me_curb_stone");
                   settle(w, 500);
                   w.imagesBrowser()->selectRow(0);
                   w.previewImage(w.imagesBrowser()->visibleName(0));
               },
               [&] {
                   if (!w.imagesBrowser()->previewHasImage()) w.previewImage(w.imagesBrowser()->visibleName(0));
                   return w.imagesBrowser()->previewHasImage();
               });
        {
            const QString base = QFileInfo(map).absolutePath();
            QDialog* use = createAssetUsageDialog(&w, ctx.doc.map(), {base + "/materials"}, {base + "/models"}, {base + "/sounds"}, {base + "/fx"});
            if (use) {
                use->resize(900, 600);
                for (QTableWidget* t : use->findChildren<QTableWidget*>()) t->resizeColumnsToContents();
            }
            s.made(use, "asset_usage.png", "Tools > Asset Usage for the showcase: the materials it uses with their counts (tabs for models, prefabs, effects, sounds and own files)", 1500);
        }
        s.opened("asset_audit.png", "Tools > Asset Availability Audit: what it checks, and the recorded result for materials", [&] { s.trigger("AssetAudit"); }, 1500,
                 [](QWidget* win) {
                     if (auto* l = win->findChild<QLabel*>("assetAuditSummary"))
                         win->setProperty("featureShotHeight", l->mapTo(win, QPoint(0, l->height())).y() + 12);
                 });

        // ---- automation, help, settings
        {
            auto* pal = new CommandPalette(w.menuBar(), &w);
            pal->setQuery("stock");
            s.made(pal, "command_palette.png", "Command Palette (Ctrl+Shift+P) with the query \"stock\"");
        }
        {
            auto* help = new HelpSearchDialog(&w);
            help->resize(1100, 700);
            help->search("lit preview");
            s.made(help, "search_help.png", "Help > Search Help for \"lit preview\": matching sections and the text of the first");
        }
        s.opened("preferences.png", "Edit > Preferences", [&] { s.trigger("Preferences"); }, 1500);

        // ---- four views, light theme
        {
            // The front and side views take the 2D view's pan and zoom when they are made: x / y at the map's middle, z near
            // the floor.
            selectNamed(w, "s53_door");
            const Vec3 door = originOf(w, "s53_door_slide", {864, 1504, 0});
            ctx.gridCenterH = 1150;
            ctx.gridCenterV = 150;
            ctx.gridScale = 0.28;
            w.fourViewLayout();
            look(w, {door.x, door.y, 40}, 60, -38, 420);
            centerGrid(w, door.x, door.y, 0.6);
            s.window("four_view.png", "View > Four-View Layout: camera, top (XY), front (XZ) and side (YZ) with the sliding door selected", 4000);
            for (const char* n : {"Front (XZ)", "Side (YZ)"})
                if (ads::CDockWidget* d = dockNamed(w, n)) d->toggleView(false);
            s.trigger("ResetLayout");
        }
        revert(w);

        // ---- brush generators on a new map
        s.trigger("NewMap");
        settle(w, 500);
        {
            AABB house;
            house.add({0, 0, 0});
            house.add({384, 256, 256});
            createBoxBrush(ctx, house);
            QString err;
            const int made = w.generateStructure(true, 0, 2, &err);
            AABB stairs;
            stairs.add({480, 0, 0});
            stairs.add({608, 256, 128});
            createBoxBrush(ctx, stairs);
            w.generateStructure(false, 2, 1, &err);
            ctx.doc.deselectAll();
            ctx.documentEdited();
            look(w, {300, 140, 100}, 50, -20, 600);
            centerGrid(w, 300, 128, 0.9);
            if (made > 0) s.window("generate_building.png", "Primitives > Generate Building (two storeys) and Generate Stairs on an empty map", 4000);
            else s.skip("generate_building.png", "Generate Building", err);
        }
        revert(w);
    }

    // ---- stock map: mp_br_tut (the small Warzone tutorial map), read only
    {
        const QString zone = "E:/IW8/Builds/1.20-replay/zone/mp_br_tut.ff";
        QString err;
        if (!QFileInfo::exists(zone) || !w.openStockZone(zone, &err)) {
            s.skip("stock_map.png", "a stock map", "mp_br_tut could not be opened: " + err);
        } else {
            waitFor(w, [&] {
                const auto st = w.stockPropStatus();
                return st.loaded && !st.fetching;
            }, 300000);
            // The middle of the map's own entities (spawns, loot, objects): the reference world is much larger.
            std::vector<double> xs, ys, zs;
            for (const Entity& e : ctx.doc.map().entities)
                if (auto o = e.origin()) xs.push_back(o->x), ys.push_back(o->y), zs.push_back(o->z);
            const auto median = [](std::vector<double> v) {
                if (v.empty()) return 0.0;
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                return v[v.size() / 2];
            };
            const Vec3 c{median(xs), median(ys), median(zs)};
            // Stock textures arrive over minutes on a cold cache: wait until the camera's counts stop changing.
            s.report << "-\tinfo\t" + waitTexturesStable(w, 8000, 420000);
            // Stock blend materials the cache cannot resolve (failed entries, e.g. the tw4/twc4 terrain blends) stay
            // checkered however long one waits: the pictures are taken where none of them is in view (at most 0.5 %
            // of the camera). The fixed views were picked by eye from the search's candidates on the owner's cache;
            // on another cache the search decides.
            constexpr double kCleanLimit = 0.005;
            {
                std::vector<double> sx = xs, sy = ys;
                std::sort(sx.begin(), sx.end());
                std::sort(sy.begin(), sy.end());
                const auto pct = [](const std::vector<double>& v, double f) { return v.empty() ? 0.0 : v[size_t(f * double(v.size() - 1))]; };
                const auto gridTarget = [&](int i, int j) {
                    return Vec3{pct(sx, 0.1) + (pct(sx, 0.9) - pct(sx, 0.1)) * i / 4.0, pct(sy, 0.1) + (pct(sy, 0.9) - pct(sy, 0.1)) * j / 4.0, c.z + 40};
                };
                std::vector<View> views;
                for (int i = 0; i < 5; ++i)
                    for (int j = 0; j < 5; ++j)
                        for (const View& v : around(gridTarget(i, j), {-20}, {700, 1400})) views.push_back(v);
                // A stock tree, a pylon, a container and rocks, the field behind (0.2 % checker on the owner's cache).
                QString why;
                const View v = chosenView(w, View{gridTarget(2, 0), 225, -20, 1400}, views, kCleanLimit, kCleanLimit, QDir(s.dir).filePath("candidates/stock_map"), &why);
                s.report << "-\tinfo\tstock_map.png " + why;
                (void)v;
                centerGrid(w, c.x, c.y, 0.12);  // the 2D view on the map's own entities
            }
            s.window("stock_map.png", "File > Open IW8 Stock Map on mp_br_tut: the stock world and props drawn as reference, its entities as the document", 6000);

            const int loot = w.editLootPoints();
            if (loot > 0) {
                // The loot points close up, all selected: candidates around each point, low enough that the checkered
                // ground decals of the town stay out of view; the 2D view centred on the camera's target.
                std::vector<Vec3> pts;
                ctx.doc.deselectAll();
                QStringList at;
                for (const Entity& e : ctx.doc.map().entities)
                    if (e.classname() == "iw8r_loot_point")
                        if (auto o = e.origin()) {
                            pts.push_back(*o);
                            selectEntity(w, e.id, true);
                            at << QString("%1 %2 %3").arg(o->x, 0, 'f', 0).arg(o->y, 0, 'f', 0).arg(o->z, 0, 'f', 0);
                        }
                s.report << "-\tinfo\tloot points at " + at.join(", ");
                if (!pts.empty()) {
                    std::vector<View> views;
                    for (const Vec3& p : pts)
                        for (const View& v : around(p + Vec3{0, 0, 16}, {-5, -12, -25}, {200, 320})) views.push_back(v);
                    QString why;
                    // Three loot points by a barrier and a vehicle, from the loot point at (276, -6): low, so the checkered
                    // ground decals stay out of view (0.3 % checker on the owner's cache).
                    Vec3 hub = pts.front();
                    for (const Vec3& p : pts)
                        if (length(p - Vec3{276, -6, 33}) < length(hub - Vec3{276, -6, 33})) hub = p;
                    const View v = chosenView(w, View{hub + Vec3{0, 0, 16}, 180, -5, 200}, views, kCleanLimit, kCleanLimit, QDir(s.dir).filePath("candidates/loot_points"), &why);
                    s.report << "-\tinfo\tloot_points.png " + why;
                    centerGrid(w, v.target.x, v.target.y, 0.5);
                }
                s.window("loot_points.png", QString("File > Stock Map > Edit Loot Points on mp_br_tut: its %1 loot points are entities (class iw8r_loot_point), selected, close up in the camera and in the 2D view").arg(loot), 5000);
            } else {
                s.skip("loot_points.png", "Edit Loot Points", "no loot points were added");
            }
            const int placed = w.editPlacedObjects();
            if (placed > 0) {
                // Every buy station is a candidate: the clean view of one of them, that one selected.
                std::vector<std::pair<Id, Vec3>> stations;
                const auto& ents = ctx.doc.map().entities;
                for (auto it = ents.rbegin(); it != ents.rend() && int(stations.size()) < placed; ++it)
                    if (it->classname() == "scriptable" && it->origin()) stations.push_back({it->id, *it->origin()});
                if (!stations.empty()) {
                    std::vector<View> views;
                    for (const auto& st : stations)
                        for (const View& v : around(st.second + Vec3{0, 0, 20}, {-10, -20, -35, -50}, {170, 260})) views.push_back(v);
                    QString why;
                    // The plunder station from above, close enough that it fills the camera: no view of a station on
                    // mp_br_tut is wholly clear of checker (its platform has blend decals; 1.4 % on the owner's cache).
                    const View fixed{stations[std::min<size_t>(3, stations.size() - 1)].second + Vec3{0, 0, 20}, 135, -55, 130};
                    const View v = chosenView(w, fixed, views, 0.02, kCleanLimit, QDir(s.dir).filePath("candidates/buy_stations"), &why);
                    const auto* pick = &stations.front();
                    for (const auto& st : stations)
                        if (length(st.second - v.target) < length(pick->second - v.target)) pick = &st;
                    selectEntity(w, pick->first);
                    look(w, v.target, v.yaw, v.pitch, v.dist);
                    s.report << "-\tinfo\tbuy_stations.png " + why;
                    centerGrid(w, pick->second.x, pick->second.y, 0.5);
                }
                s.window("buy_stations.png", QString("File > Stock Map > Edit Placed Objects on mp_br_tut: its %1 buy stations as scriptable entities, one selected (yellow)").arg(placed), 6000);
            } else {
                s.skip("buy_stations.png", "Edit Placed Objects", "no buy stations were added");
            }
            // The Console at the end of the run: the status lines of everything above, newest at the bottom.
            s.dock("Console", {1250, 400}, "console.png", "Console dock at the end of the run: the editor's status and stock map lines, with level and text filters",
                   [&](QWidget* d) {
                       if (auto* e = d->findChild<QLineEdit*>("consoleFilter")) e->clear();
                       if (auto* t = d->findChild<QTableWidget*>("consoleTable")) {
                           t->resizeColumnToContents(0);
                           t->resizeColumnToContents(1);
                           t->resizeColumnToContents(2);
                           t->scrollToBottom();
                       }
                   });
            revert(w);
        }
    }

    qApp->removeEventFilter(&keeper);
    QFile f(QDir(outDir).filePath("feature-shots.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(s.report.join("\n").toUtf8() + "\n");
    return s.failed == 0 ? 0 : 2;
}

}  // namespace iw8r::qt
