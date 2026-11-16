#include "play_input_test.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <QTest>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <memory>

#include "camera_view.h"
#include "main_window.h"

namespace iw8r::qt {

namespace {

int qtKeyOf(const QString& name) {
    const QString n = name.toUpper();
    if (n == "SPACE") return Qt::Key_Space;
    if (n == "CTRL") return Qt::Key_Control;
    if (n == "SHIFT") return Qt::Key_Shift;
    if (n == "ESC") return Qt::Key_Escape;
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return Qt::Key_A + (n[0].unicode() - 'A');
    return 0;
}

struct Runner {
    MainWindow& w;
    QStringList lines;
    int pc = 0;
    QElapsedTimer clock;
    qint64 waitUntil = -1, nextPrint = 0, tapUpAt = -1;
    int tapKey = 0;
    int every = 100;
    Qt::KeyboardModifiers mods;

    CameraView* cv() const { return w.cameraView(); }

    void print(const char* label) {
        const PlayerState& s = cv()->player();
        const auto& c = w.context().cam;
        std::fprintf(stderr,
                     "play-input %7.2f %-14s pos %8.1f %8.1f %7.1f  hs %6.1f vz %7.1f  %s%s%s%s%s%s%s%s%s eye %5.1f yaw %.0f\n",
                     clock.elapsed() / 1000.0, label, s.origin.x, s.origin.y, s.origin.z, std::hypot(s.velocity.x, s.velocity.y),
                     s.velocity.z, cv()->playing() ? "" : "NOT-PLAYING ", s.onGround ? "ground " : s.standing ? "stood " : "air ", s.crouched ? "crouch " : "",
                     s.prone ? "prone " : "", s.sprinting ? "sprint " : "", s.tacticalSprinting ? "tactical " : "",
                     s.sliding ? "slide " : "", s.mantling ? "mantle " : "", s.onLadder ? "ladder " : "", c.pos.z - s.origin.z, c.yaw);
        std::fflush(stderr);
    }

    void key(int k, bool down) {
        Qt::KeyboardModifier m = k == Qt::Key_Shift ? Qt::ShiftModifier : k == Qt::Key_Control ? Qt::ControlModifier : Qt::NoModifier;
        if (down) {
            if (m != Qt::NoModifier) mods |= m;
            QTest::keyPress(cv(), Qt::Key(k), mods);
        } else {
            QTest::keyRelease(cv(), Qt::Key(k), mods);
            if (m != Qt::NoModifier) mods &= ~Qt::KeyboardModifiers(m);
        }
    }

    // One timer tick: finish a tap, print while waiting, else run lines until the next wait.
    void tick() {
        const qint64 now = clock.elapsed();
        if (tapUpAt >= 0 && now >= tapUpAt) {
            key(tapKey, false);
            tapUpAt = -1;
        }
        if (waitUntil >= 0) {
            if (every > 0 && now >= nextPrint) {
                print("");
                nextPrint = now + every;
            }
            if (now < waitUntil) return;
            waitUntil = -1;
        }
        while (pc < lines.size()) {
            const QString line = lines[pc++].trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            const QStringList a = line.split(' ', Qt::SkipEmptyParts);
            const QString op = a[0].toLower();
            if (op == "wait") {
                waitUntil = now + (a.size() > 1 ? a[1].toInt() : 0);
                nextPrint = now;
                return;
            } else if (op == "every" && a.size() > 1) {
                every = a[1].toInt();
            } else if (op == "log") {
                print(a.size() > 1 ? a.mid(1).join(' ').toUtf8().constData() : "");
            } else if ((op == "down" || op == "up" || op == "tap") && a.size() > 1) {
                const int k = qtKeyOf(a[1]);
                if (!k) {
                    std::fprintf(stderr, "play-input: unknown key %s\n", a[1].toUtf8().constData());
                    continue;
                }
                key(k, op != "up");
                if (op == "tap") {
                    tapKey = k;
                    tapUpAt = now + 100;
                    waitUntil = now + 120;
                    nextPrint = now + 1000000;
                    return;
                }
            } else if (op == "waitspawns") {  // a stock map: wait (up to <ms>) until its spawns are known
                const qint64 limit = a.size() > 1 ? a[1].toInt() : 600000;
                if (w.context().scene.spawns.empty() && now < limit) {
                    --pc;
                    waitUntil = now + 500;
                    nextPrint = now + 1000000;
                    return;
                }
                std::fprintf(stderr, "play-input: %zu spawns\n", w.context().scene.spawns.size());
            } else if (op == "spawn" && a.size() > 1) {  // start at a stock spawn: spawn <index> [yaw offset]
                const auto& sp = w.context().scene.spawns;
                if (sp.empty()) continue;
                const auto& s = sp[size_t(a[1].toInt()) % sp.size()];
                const double yaw = s.angles.y + (a.size() > 2 ? a[2].toDouble() : 0.0);
                lines.insert(pc, QString("start %1 %2 %3 %4").arg(s.origin.x).arg(s.origin.y).arg(s.origin.z + 60).arg(yaw));
            } else if (op == "probe") {  // what the standing capsule overlaps / meets at the player's position
                const CollisionWorld& world = cv()->playWorld();
                const PlayerState& s = cv()->player();
                const auto show = [&](const char* what, const CollisionWorld::Trace& tr) {
                    std::fprintf(stderr, "play-input   probe %-10s frac %.4f start %d all %d solid %d n %.2f %.2f %.2f end z %.2f", what,
                                 tr.fraction, int(tr.startSolid), int(tr.allSolid), tr.solid, tr.plane.n.x, tr.plane.n.y, tr.plane.n.z,
                                 tr.end.z);
                    if (tr.solid >= 0) {
                        const auto& so = world.solids()[size_t(tr.solid)];
                        std::fprintf(stderr, "  [planes %zu patch %d bounds %.1f %.1f %.1f .. %.1f %.1f %.1f]", so.planes.size(),
                                     int(so.patch), so.bounds.mins.x, so.bounds.mins.y, so.bounds.mins.z, so.bounds.maxs.x,
                                     so.bounds.maxs.y, so.bounds.maxs.z);
                    }
                    std::fprintf(stderr, "\n");
                };
                const MoveParams mp;
                show("static", world.traceCapsule(s.origin, s.origin, mp.radius, mp.standHeight));
                show("ground", world.traceCapsule(s.origin, s.origin - Vec3{0, 0, 0.25}, mp.radius, mp.standHeight));
                show("up8", world.traceCapsule(s.origin + Vec3{0, 0, 8}, s.origin + Vec3{0, 0, 8}, mp.radius, mp.standHeight));
                show("drop", world.traceCapsule(s.origin + Vec3{0, 0, 60}, s.origin - Vec3{0, 0, 200}, mp.radius, mp.standHeight));
                show("ray", world.traceRay(s.origin + Vec3{0, 0, 60}, s.origin - Vec3{0, 0, 200}));
                // The two ground passes (collision type 1, research 140 section 8): the prism, the centre ray.
                show("prism", world.traceCylinder(s.origin + Vec3{0, 0, 23}, s.origin - Vec3{0, 0, 16}, mp.radius, 32));
                show("ray70", world.traceRay(s.origin + Vec3{0, 0, mp.standHeight}, s.origin - Vec3{0, 0, 16}));
                // Every solid the standing capsule overlaps right here (one-solid static tests through tiny worlds).
                int listed = 0;
                for (size_t i = 0; i < world.solids().size() && listed < 8; ++i) {
                    const auto& so = world.solids()[i];
                    if (so.bounds.maxs.x < s.origin.x - 16 || so.bounds.mins.x > s.origin.x + 16 || so.bounds.maxs.y < s.origin.y - 16 ||
                        so.bounds.mins.y > s.origin.y + 16 || so.bounds.maxs.z < s.origin.z - 1 || so.bounds.mins.z > s.origin.z + 71)
                        continue;
                    CollisionWorld one;
                    one.addConvex(so.planes, so.contents);
                    const auto tr = one.traceCapsule(s.origin, s.origin, mp.radius, mp.standHeight);
                    if (!tr.startSolid) continue;
                    ++listed;
                    std::fprintf(stderr, "play-input   inside solid %zu patch %d planes %zu bounds %.1f %.1f %.1f .. %.1f %.1f %.1f first n %.2f %.2f %.2f\n",
                                 i, int(so.patch), so.planes.size(), so.bounds.mins.x, so.bounds.mins.y, so.bounds.mins.z, so.bounds.maxs.x,
                                 so.bounds.maxs.y, so.bounds.maxs.z, so.planes[0].n.x, so.planes[0].n.y, so.planes[0].n.z);
                }
            } else if (op == "prop" && a.size() > 2) {  // start `dist` (default 80) before the Nth stock prop, facing it
                const std::string part = a[1].toStdString();
                const int n = a[2].toInt();
                const double dist = a.size() > 3 ? a[3].toDouble() : 80.0, yaw = a.size() > 4 ? a[4].toDouble() : 0.0;
                int seen = 0;
                bool found = false;
                if (const auto props = w.referencePropPlacements())
                    for (const auto& pl : *props) {
                        if (pl.model.find(part) == std::string::npos || seen++ < n) continue;
                        const double r = yaw * 3.14159265358979 / 180.0;
                        std::fprintf(stderr, "play-input: prop %s at %.1f %.1f %.1f\n", pl.model.c_str(), pl.origin.x, pl.origin.y, pl.origin.z);
                        lines.insert(pc, QString("start %1 %2 %3 %4")
                                             .arg(pl.origin.x - std::cos(r) * dist)
                                             .arg(pl.origin.y - std::sin(r) * dist)
                                             .arg(pl.origin.z + 60)
                                             .arg(yaw));
                        found = true;
                        break;
                    }
                if (!found) std::fprintf(stderr, "play-input: no prop %s #%d\n", part.c_str(), n);
            } else if (op == "look" && a.size() > 2) {
                w.context().cam.yaw = a[1].toDouble();
                w.context().cam.pitch = a[2].toDouble();
            } else if (op == "start" && a.size() > 4) {
                QAction* play = w.action("PlayPreview");
                for (int k : {Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_Shift, Qt::Key_Control, Qt::Key_Space, Qt::Key_C})
                    QTest::keyRelease(cv(), Qt::Key(k));
                mods = {};
                if (cv()->playing()) QTest::keyClick(cv(), Qt::Key_Escape);  // play mode disables the commands: Esc ends it
                auto& cam = w.context().cam;
                cam.pos = Vec3{a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
                cam.yaw = a[4].toDouble();
                cam.pitch = 0;
                if (play) play->trigger();
                std::fprintf(stderr, "play-input: %s\n", line.toUtf8().constData());
                print("start");
            } else if (op == "quit") {
                break;
            }
        }
        finish();
    }

    // Ends play (Esc), stops the script and quits the way IW8R_QUIT_AFTER does.
    void finish() {
        if (done) return;
        done = true;
        if (cv()->playing()) QTest::keyClick(cv(), Qt::Key_Escape);
        if (timer) timer->stop();
        std::fprintf(stderr, "play-input: done\n");
        std::fflush(stderr);
        QTimer::singleShot(300, qApp, &QCoreApplication::quit);
    }
    QTimer* timer = nullptr;
    bool done = false;
};

}  // namespace

void startPlayInputTest(MainWindow& w, const QString& scriptPath) {
    QFile f(scriptPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "play-input: cannot read %s\n", scriptPath.toUtf8().constData());
        return;
    }
    auto r = std::make_shared<Runner>(Runner{w, QString::fromUtf8(f.readAll()).split('\n')});
    r->clock.start();
    auto* t = new QTimer(&w);
    r->timer = t;
    QObject::connect(t, &QTimer::timeout, &w, [r] {
        if (!r->done) r->tick();
    });
    t->start(8);
}

}  // namespace iw8r::qt
