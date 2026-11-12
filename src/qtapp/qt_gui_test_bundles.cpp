#include "qt_gui_test_bundles.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <DockWidget.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTimer>

#include "brush_ops.h"
#include "compile_panel.h"
#include "custom_materials.h"
#include "layout_tools.h"
#include "main_window.h"
#include "panels.h"
#include "map_text.h"
#include "prefab.h"
#include "prefab_browser.h"
#include "prefab_bundle.h"
#include "prefab_bundle_panel.h"
#include "prefab_save_dialog.h"
#include "settings.h"

#ifdef _MSC_VER
// One long scripted function, as in qt_gui_test.cpp: optimizing it buys nothing.
#pragma optimize("", off)
#endif

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

bool putFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(bytes);
    return true;
}

QByteArray slurp(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::string boxText(const AABB& b, const char* material) {
    const auto prim = makeBrushPrimitive(makeBox(b, std::string(" ") + material + " 128 128 0 0 0 0"), "\n");
    return prim ? prim->text() : std::string();
}

// A cube model (OBJ, Y-up) of side `s` standing on the ground, with one material.
QByteArray cubeObj(double s, const char* material) {
    const double h = s / 2;
    QByteArray o = "# test cube\n";
    const double v[8][3] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}, {-h, s, -h}, {h, s, -h}, {h, s, h}, {-h, s, h}};
    for (const auto& p : v) o += QString("v %1 %2 %3\n").arg(p[0]).arg(p[1]).arg(p[2]).toUtf8();
    o += "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";
    o += QByteArray("usemtl ") + material + "\n";
    const int f[6][4] = {{1, 2, 3, 4}, {8, 7, 6, 5}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 8, 4}, {4, 8, 5, 1}};
    for (const auto& q : f) o += QString("f %1/1 %2/2 %3/3 %4/4\n").arg(q[0]).arg(q[1]).arg(q[2]).arg(q[3]).toUtf8();
    return o;
}

bool solidPng(const QString& path, const QColor& c, int side = 64) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage img(side, side, QImage::Format_RGBA8888);
    img.fill(c);
    return img.save(path);
}

// Runs `steps` one after the other, each on the modal window that is up when its turn comes (a dialog, then the
// question it asks, then the dialog again). A window still up five seconds after the last step is closed, so a
// failed step cannot hang the run.
// A later call ends the earlier one, so the steps of one dialog never land on the next.
int g_modalRun = 0;

void driveModals(std::vector<std::function<void(QWidget*)>> steps) {
    const int run = ++g_modalRun;
    auto list = std::make_shared<std::vector<std::function<void(QWidget*)>>>(std::move(steps));
    auto index = std::make_shared<size_t>(0);
    auto tries = std::make_shared<int>(0);
    // The function that polls holds itself only through the pending timer, so it is freed when the steps are done.
    struct Poll {
        std::function<void()> fn;
    };
    auto poll = std::make_shared<Poll>();
    std::weak_ptr<Poll> weak = poll;
    poll->fn = [list, index, tries, weak, run] {
        const auto self = weak.lock();
        if (!self || run != g_modalRun || *index >= list->size()) return;
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal) {
            if (++*tries < 100) QTimer::singleShot(50, qApp, [self] { self->fn(); });
            return;
        }
        *tries = 0;
        const size_t at = (*index)++;
        (*list)[at](modal);
        if (*index < list->size()) QTimer::singleShot(200, qApp, [self] { self->fn(); });
        else
            QTimer::singleShot(5000, qApp, [run] {
                if (run != g_modalRun) return;
                for (int i = 0; i < 4; ++i)
                    if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
            });
    };
    QTimer::singleShot(250, qApp, [poll] { poll->fn(); });
}

struct Placement {
    Id id = kNoId;
    std::string model, pid, origin;
};

std::vector<Placement> placementsOf(const Document& doc) {
    std::vector<Placement> out;
    for (const auto& e : doc.map().entities) {
        if (e.classname() != "misc_prefab") continue;
        Placement p;
        p.id = e.id;
        if (const std::string* v = e.value("model")) p.model = *v;
        if (const std::string* v = e.value(kPrefabPlacementIdKey)) p.pid = *v;
        if (const std::string* v = e.value("origin")) p.origin = *v;
        out.push_back(p);
    }
    return out;
}

bool distinctIds(const std::vector<Placement>& ps) {
    std::set<std::string> seen;
    for (const Placement& p : ps)
        if (!p.pid.empty() && !seen.insert(p.pid).second) return false;
    return true;
}

}  // namespace

void runPrefabBundleChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                           const QString& outDir) {
    EditorContext& ctx = w.context();
    PrefabBrowser* pb = w.prefabBrowser();
    PrefabBundlePanel* panel = w.bundlePanel();
    const QString scratch = QDir(outDir).absoluteFilePath("bundles");
    QDir(scratch).removeRecursively();
    const QString lib = scratch + "/library";
    const QString mapDir = scratch + "/map";
    QDir().mkpath(lib);
    QDir().mkpath(mapDir);

    // The prefab folder setting belongs to the owner: it points at the scratch library for these checks only.
    struct RestoreRoots {
        QVariant before = Settings::instance().value("prefabRoots");
        ~RestoreRoots() { Settings::instance().setValue("prefabRoots", before); }
    } restoreRoots;
    Settings::instance().setValue("prefabRoots", QStringList{lib});

    const auto select = [&](const std::vector<SelRef>& refs) {
        ctx.doc.deselectAll();
        for (const SelRef& r : refs) ctx.doc.select(r, true);
        ctx.documentEdited();
    };
    const auto undoTo = [&](size_t pos) {
        for (int i = 0; i < 64 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
    };
    const auto drawn = [&](Id entity) -> const app::PrimGeom* {
        ctx.scene.sync(ctx.doc);
        for (const app::PrimGeom* g : ctx.scene.items())
            if (g->ref.entity == entity && g->ref.item == kNoId) return g;
        return nullptr;
    };

    // ---- fixtures: a bundle with its own material, model, picture and parameters; one that builds its fallback; one
    // of a format this editor does not read; and a plain .map prefab.
    const QString lamp = lib + "/props/lamp.iw8prefab";
    putFile(lamp + "/prefab.json",
            "{\n"
            "  \"format\": \"iw8r-prefab-1\",\n"
            "  \"id\": \"zq_lamp_post\",\n"
            "  \"title\": \"Street lamp\",\n"
            "  \"description\": \"A lamp post with its own metal material and shade model.\",\n"
            "  \"tags\": [\"light\", \"street\", \"zqlamp\"],\n"
            "  \"parameters\": [\n"
            "    {\"name\": \"radius\", \"label\": \"Reach of the light\", \"type\": \"number\", \"default\": 200, \"min\": 64, \"max\": 512,\n"
            "     \"apply\": {\"selector\": \"lamp_light\", \"key\": \"radius\", \"template\": \"{radius}\"}},\n"
            "    {\"name\": \"mode\", \"label\": \"Light mode\", \"type\": \"choice\", \"default\": \"steady\", \"choices\": [\"steady\", \"flicker\"],\n"
            "     \"apply\": {\"selector\": \"lamp_light\", \"key\": \"script_noteworthy\", \"template\": \"lamp_{mode}\"}},\n"
            "    {\"name\": \"metal\", \"label\": \"Post material\", \"type\": \"material\", \"default\": \"custom/lampmetal\",\n"
            "     \"apply\": {\"material\": \"custom/lampmetal\"}}\n"
            "  ],\n"
            "  \"assets\": {\n"
            "    \"materials\": [{\"name\": \"custom/lampmetal\", \"file\": \"materials/lampmetal.iw8mat.json\"}],\n"
            "    \"models\": [{\"name\": \"lampshade\", \"file\": \"models/lampshade.obj\"}]\n"
            "  }\n"
            "}\n");
    {
        std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxText({{-8, -8, 0}, {8, 8, 128}}, "custom/lampmetal") + "}\n";
        t += "{\n\"classname\" \"misc_model\"\n\"model\" \"lampshade\"\n\"origin\" \"0 0 128\"\n}\n";
        t += "{\n\"classname\" \"light\"\n\"targetname\" \"lamp_light\"\n\"origin\" \"0 0 120\"\n\"radius\" \"200\"\n}\n";
        t += "{\n\"classname\" \"trigger_multiple\"\n\"target\" \"lamp_light\"\n" + boxText({{-48, -48, 0}, {48, 48, 64}}, "trigger") + "}\n";
        putFile(lamp + "/prefab.map", QByteArray::fromStdString(t));
    }
    putFile(lamp + "/materials/lampmetal.iw8mat.json", "{\n \"name\": \"custom/lampmetal\",\n \"color\": \"lampmetal.png\"\n}\n");
    solidPng(lamp + "/materials/lampmetal.png", QColor(90, 100, 120));
    putFile(lamp + "/models/lampshade.obj", cubeObj(24, "custom/lampmetal"));
    solidPng(lamp + "/thumb.png", QColor(255, 0, 0));

    const QString fbDoor = lib + "/doors/fb_door.iw8prefab";
    putFile(fbDoor + "/prefab.json",
            "{\n  \"format\": \"iw8r-prefab-1\",\n  \"id\": \"fb_door\",\n  \"title\": \"Fallback door\",\n"
            "  \"tags\": [\"door\"],\n"
            "  \"requires\": {\"build\": [\"scriptable_instances\"]},\n"
            "  \"fallback\": {\"when_missing\": [\"scriptable_instances\"], \"map\": \"fallback.map\", \"note\": \"built as a plain box\"}\n}\n");
    putFile(fbDoor + "/prefab.map", QByteArray::fromStdString("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" +
                                                               boxText({{0, 0, 0}, {16, 16, 16}}, "caulk") + "}\n"));
    putFile(fbDoor + "/fallback.map", QByteArray::fromStdString("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" +
                                                                 boxText({{0, 0, 0}, {64, 8, 96}}, "caulk") + "}\n"));
    const QString broken = lib + "/props/broken.iw8prefab";
    putFile(broken + "/prefab.json", "{\n  \"format\": \"iw8r-prefab-9\",\n  \"id\": \"broken_one\",\n  \"title\": \"From the future\"\n}\n");
    putFile(broken + "/prefab.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n");
    const QString plain = lib + "/props/plain_crate.map";
    putFile(plain, QByteArray::fromStdString("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxText({{0, 0, 0}, {32, 32, 32}}, "caulk") + "}\n"));

    // The map the checks work in: its own material and model, a few objects to save as a prefab, and one trigger
    // outside the future selection that targets it.
    putFile(mapDir + "/materials/gui_brick.iw8mat.json", "{\n \"name\": \"custom/gui_brick\",\n \"color\": \"gui_brick.png\"\n}\n");
    solidPng(mapDir + "/materials/gui_brick.png", QColor(170, 80, 60));
    putFile(mapDir + "/models/gui_crate.obj", cubeObj(32, "custom/gui_brick"));
    const QString hostPath = mapDir + "/mp_bundle_gui.map";
    {
        std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxText({{-512, -512, -16}, {512, 512, 0}}, "custom/gui_brick") +
                        boxText({{300, 300, 0}, {332, 332, 96}}, "custom/gui_brick") + "}\n";
        t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-300 0 16\"\n\"angles\" \"0 0 0\"\n}\n";
        t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-300 100 16\"\n\"angles\" \"0 180 0\"\n}\n";
        t += "{\n\"classname\" \"misc_model\"\n\"model\" \"gui_crate\"\n\"origin\" \"316 360 0\"\n}\n";
        t += "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"gate\"\n" + boxText({{300, 400, 0}, {332, 416, 96}}, "custom/gui_brick") + "}\n";
        t += "{\n\"classname\" \"trigger_multiple\"\n\"target\" \"gate\"\n" + boxText({{280, 380, 0}, {352, 436, 64}}, "trigger") + "}\n";
        t += "{\n\"classname\" \"sound_emitter\"\n\"soundalias\" \"emt_light_fluorescent_hum1_int\"\n\"loop\" \"1\"\n\"origin\" \"316 316 100\"\n}\n";
        t += "{\n\"classname\" \"trigger_use\"\n\"targetname\" \"outside_trig\"\n\"target\" \"gate\"\n\"origin\" \"100 100 32\"\n}\n";
        putFile(hostPath, QByteArray::fromStdString(t));
    }
    w.openPath(hostPath);
    const bool opened = QFileInfo(qs(ctx.doc.path())).absoluteFilePath().compare(QFileInfo(hostPath).absoluteFilePath(), Qt::CaseInsensitive) == 0;

    // ------------------------------------------------------------------ the Prefabs dock
    section("prefab bundles: the Prefabs dock");
    check(opened && pb && panel, "the scratch map is open and the dock and the bundle panel exist");
    if (!opened || !pb || !panel) return;
    pb->refresh();
    const int lampRow = pb->rowOfFile(lamp), fbRow = pb->rowOfFile(fbDoor), brokenRow = pb->rowOfFile(broken), plainRow = pb->rowOfFile(plain);
    check(lampRow >= 0 && pb->isBundleAt(lampRow) && pb->titleAt(lampRow) == "Street lamp",
          QString("a bundle folder is listed as one entry with its manifest's title (%1)").arg(pb->titleAt(lampRow)));
    check(pb->toolTipAt(lampRow).contains("A lamp post with its own metal material") && pb->toolTipAt(lampRow).contains("light, street") &&
              pb->toolTipAt(lampRow).contains("Reach of the light"),
          "its tooltip shows the description, the tags and the parameters");
    {
        bool inner = false;
        for (int i = 0; i < pb->count(); ++i) inner = inner || pb->fileAt(i).contains(".iw8prefab/", Qt::CaseInsensitive);
        check(!inner && pb->rowOfFile(lamp + "/prefab.map") < 0 && pb->rowOfFile(fbDoor + "/fallback.map") < 0,
              "the .map files inside bundle folders are not listed on their own");
    }
    check(plainRow >= 0 && !pb->isBundleAt(plainRow) && pb->titleAt(plainRow) == "plain_crate" && pb->badgeAt(plainRow).isEmpty(),
          "a plain .map prefab is still listed as before");
    check(lampRow < plainRow, "bundles are listed before the plain prefabs");
    {
        pb->setFilter("zqlamp");
        const bool byTag = pb->shownAt(lampRow) && pb->visibleCount() == 1;
        pb->setFilter("street LAMP");
        const bool byTitle = pb->shownAt(lampRow) && !pb->shownAt(fbRow) && !pb->shownAt(plainRow);
        pb->setFilter("zq_lamp_post");
        const bool byId = pb->shownAt(lampRow) && pb->visibleCount() == 1;
        pb->setFilter("zzz_no_such_prefab");
        const int none = pb->visibleCount();
        pb->setFilter("");
        check(byTag && byTitle && byId && none == 0 && pb->visibleCount() == pb->count(),
              QString("the filter finds a bundle by a tag, by its title and by its id (%1 %2 %3; %4 shown for no match)")
                  .arg(byTag).arg(byTitle).arg(byId).arg(none));
    }
    {
        const QImage pic = pb->pictureAt(lampRow);
        const QColor mid = pic.isNull() ? QColor() : pic.pixelColor(pic.width() / 2, pic.height() / 2);
        check(pb->pictureFromFileAt(lampRow) && mid.red() > 200 && mid.green() < 60 && mid.blue() < 60,
              QString("a bundle with a thumb.png shows that picture (centre %1 %2 %3)").arg(mid.red()).arg(mid.green()).arg(mid.blue()));
        pic.save(QDir(outDir).filePath("bundle_dock_thumb_file.png"));
    }
    {
        const QImage pic = pb->pictureAt(fbRow);
        // The badge is a filled amber strip along the bottom of the picture.
        int amber = 0;
        for (int x = 0; !pic.isNull() && x < pic.width(); ++x) {
            const QColor c = pic.pixelColor(x, pic.height() - 2);
            amber += c.red() > 180 && c.green() > 100 && c.green() < 190 && c.blue() < 90;
        }
        check(fbRow >= 0 && pb->badgeAt(fbRow) == "FALLBACK" && pb->badgeReasonAt(fbRow).contains("built as a plain box") &&
                  pb->badgeReasonAt(fbRow).contains("scriptable_instances"),
              "a bundle that builds its fallback contents has the badge FALLBACK and says why: " + pb->badgeReasonAt(fbRow));
        check(pb->toolTipAt(fbRow).contains("FALLBACK") && pb->toolTipAt(fbRow).contains("built as a plain box"), "the reason is in its tooltip");
        check(!pb->pictureFromFileAt(fbRow) && !pic.isNull() && amber > pic.width() / 2,
              QString("without a thumb.png the picture is drawn from its contents, with the badge on it (%1 amber pixels)").arg(amber));
        pic.save(QDir(outDir).filePath("bundle_dock_badge.png"));
        check(pb->badgeAt(lampRow).isEmpty() && pb->badgeReasonAt(lampRow).isEmpty(), "a bundle that builds its main contents has no badge");
    }
    {
        QString err;
        const size_t pos = ctx.doc.undoPosition();
        check(brokenRow >= 0 && pb->badgeAt(brokenRow) == "CANNOT PLACE" && !pb->placeableAt(brokenRow) && pb->titleAt(brokenRow) == "From the future",
              "a bundle of another format is listed with the badge CANNOT PLACE");
        const bool refused = !pb->stamp(brokenRow, &err);
        check(refused && err.contains("iw8r-prefab-9") && ctx.doc.undoPosition() == pos, "placing it is refused with the reason: " + err);
    }
    QString shippedPad, shippedDoor;
    {
        // The bundles shipped with the editor (data/prefabs/bundles) are listed whatever the prefab folders are.
        for (const QString& root : PrefabBrowser::searchRoots()) {
            if (QFileInfo(root + "/bundles/gameplay/jump_pad.iw8prefab").isDir()) shippedPad = root + "/bundles/gameplay/jump_pad.iw8prefab";
            if (QFileInfo(root + "/bundles/doors/stock_door_wood_single.iw8prefab").isDir())
                shippedDoor = root + "/bundles/doors/stock_door_wood_single.iw8prefab";
        }
        const int padRow = pb->rowOfFile(shippedPad), doorRow = pb->rowOfFile(shippedDoor);
        check(!shippedPad.isEmpty() && padRow >= 0 && pb->titleAt(padRow) == "Jump pad with effect and sound" && pb->badgeAt(padRow).isEmpty(),
              "the shipped jump pad bundle is listed, without a badge " + pb->badgeReasonAt(padRow));
        check(!shippedDoor.isEmpty() && doorRow >= 0 && pb->titleAt(doorRow) == "Stock door, painted wood, single" &&
                  pb->badgeAt(doorRow) == "FALLBACK" && pb->badgeReasonAt(doorRow).contains("stock door built as a scripted door"),
              "the shipped stock door is listed with the badge FALLBACK: " + pb->badgeReasonAt(doorRow));
        {
            // With real stock scripted objects on (the default for mappers) the door builds its real contents: no badge.
            const QByteArray saved = qgetenv("IW8R_REAL_SCRIPTABLES");
            qputenv("IW8R_REAL_SCRIPTABLES", "1");
            pb->refresh();
            const int onRow = pb->rowOfFile(shippedDoor);
            const QString onBadge = onRow >= 0 ? pb->badgeAt(onRow) : QString("missing");
            qputenv("IW8R_REAL_SCRIPTABLES", saved.isEmpty() ? QByteArray("0") : saved);
            pb->refresh();
            check(onRow >= 0 && onBadge != "FALLBACK" && pb->badgeAt(pb->rowOfFile(shippedDoor)) == "FALLBACK",
                  "with real stock scripted objects on the stock door has no FALLBACK badge (badge '" + onBadge + "')");
        }
        pb->setFilter("jump");
        check(pb->visibleCount() >= 1 && pb->visibleCount() < 200, QString("the tag 'jump' finds the jump pad (%1 shown)").arg(pb->visibleCount()));
        pb->setFilter("");
        check(PrefabBrowser::stockCatalog() != nullptr && !PrefabBrowser::stockCatalog()->models.empty(),
              "the stock catalogue is read for the checks of required stock names");
    }

    // ------------------------------------------------------------------ place, drop, copy
    section("prefab bundles: place, drop and copy");
    const size_t posStart = ctx.doc.undoPosition();
    const std::string textStart = ctx.doc.text();
    Id lampA = kNoId;
    {
        const size_t pos = ctx.doc.undoPosition();
        check(w.dropAsset("prefabs", lamp, false, Vec3{64, 64, 0}, {kNoId, kNoId}), "a bundle dragged into a view is placed");
        const auto ps = placementsOf(ctx.doc);
        check(ps.size() == 1 && ps[0].model == "props/lamp.iw8prefab" && ps[0].pid == "p1" && ps[0].origin == "64 64 0" &&
                  ctx.doc.undoPosition() == pos + 1,
              QString("it is one misc_prefab that names the bundle folder and carries iw8r_pid p1 (model %1, id %2; one undo step)")
                  .arg(ps.empty() ? QString() : qs(ps[0].model), ps.empty() ? QString() : qs(ps[0].pid)));
        lampA = ps.empty() ? kNoId : ps[0].id;
        const app::PrimGeom* g = drawn(lampA);
        check(g && g->prefab && g->bounds.valid() && g->bounds.maxs.z > 120 && g->bounds.mins.x <= 20 && g->bounds.maxs.x >= 108 && g->polys.size() >= 12,
              QString("the placement is drawn with the bundle's contents at the drop point (%1 polygons, top at z %2)")
                  .arg(g ? g->polys.size() : 0).arg(g && g->bounds.valid() ? g->bounds.maxs.z : 0));
    }
    {
        // Double-click in the dock.
        const size_t pos = ctx.doc.undoPosition();
        QString err;
        check(pb->stamp(lampRow, &err), "a double-click in the dock places the bundle " + err);
        const auto ps = placementsOf(ctx.doc);
        check(ps.size() == 2 && ps[1].pid == "p2" && ctx.doc.undoPosition() == pos + 1, "the second placement gets the id p2 (one undo step)");
    }
    {
        // The fallback bundle draws what a build makes of it: the 64 unit wide fallback box, not the 16 unit main box.
        const size_t pos = ctx.doc.undoPosition();
        check(w.dropAsset("prefabs", fbDoor + "/prefab.json", false, Vec3{-200, 200, 0}, {kNoId, kNoId}), "a bundle can be placed by its prefab.json");
        const auto ps = placementsOf(ctx.doc);
        const Id id = ps.empty() ? kNoId : ps.back().id;
        const app::PrimGeom* g = drawn(id);
        check(ps.size() == 3 && ps.back().model == "doors/fb_door.iw8prefab" && ps.back().pid == "p3" && g && g->bounds.valid() &&
                  std::fabs((g->bounds.maxs.x - g->bounds.mins.x) - 64) < 0.5 && std::fabs(g->bounds.maxs.z - 96) < 0.5,
              QString("a bundle that builds its fallback is drawn with the fallback contents (width %1)")
                  .arg(g && g->bounds.valid() ? g->bounds.maxs.x - g->bounds.mins.x : -1));
        undoTo(pos);
    }
    {
        // Edit > Clone: the copy is a new placement with a new id; one undo step.
        select({{lampA, kNoId}});
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        w.action("CloneSelection")->trigger();
        auto ps = placementsOf(ctx.doc);
        check(ps.size() == 3 && distinctIds(ps) && ps[2].pid == "p3" && ps[0].pid == "p1" && ctx.doc.undoPosition() == pos + 1 &&
                  ctx.doc.isSelected({ps[2].id, kNoId}),
              QString("Clone gives the copy a new placement id in one undo step (%1 %2 %3)")
                  .arg(ps.size() > 0 ? qs(ps[0].pid) : QString(), ps.size() > 1 ? qs(ps[1].pid) : QString(), ps.size() > 2 ? qs(ps[2].pid) : QString()));
        w.action("Undo")->trigger();
        check(ctx.doc.text() == before && ctx.doc.undoPosition() == pos, "one Undo takes the clone and its id back");
    }
    {
        // Edit > Copy and Paste.
        select({{lampA, kNoId}});
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        QString err;
        const bool pasted = w.copySelection(false) == 1 && w.pasteFromClipboard(&err) == 1;
        auto ps = placementsOf(ctx.doc);
        check(pasted && ps.size() == 3 && distinctIds(ps) && ps[2].pid == "p3" && ctx.doc.undoPosition() == pos + 1,
              "Copy and Paste give the pasted placement a new id in one undo step " + err);
        w.action("Undo")->trigger();
        check(ctx.doc.text() == before, "one Undo takes the paste back");
    }
    {
        // Arrange > Array Duplicate: every copy its own id.
        select({{lampA, kNoId}});
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        const bool ok = w.duplicateWithNewIds("Array Duplicate", [&] { return arrayDuplicate(ctx.doc, Vec3{128, 0, 0}, 3) > 0; });
        auto ps = placementsOf(ctx.doc);
        check(ok && ps.size() == 5 && distinctIds(ps) && ctx.doc.undoPosition() == pos + 1,
              QString("an array of three copies has three new ids, in one undo step (%1 placements)").arg(ps.size()));
        w.action("Undo")->trigger();
        check(ctx.doc.text() == before, "one Undo takes the array back");
    }
    {
        // The drag preview: a provisional placement with the real contents follows the cursor.
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        const size_t count0 = placementsOf(ctx.doc).size();
        const bool shown = w.previewPrefabAtPoint(lamp, Vec3{200, -200, 0});
        auto ps = placementsOf(ctx.doc);
        const app::PrimGeom* g = ps.empty() ? nullptr : drawn(ps.back().id);
        check(shown && w.prefabPreviewActive() && ps.size() == count0 + 1 && ps.back().origin == "200 -200 0" && g && g->prefab &&
                  g->bounds.maxs.z > 120 && ctx.doc.undoPosition() == pos + 1,
              "while a bundle is dragged over the camera its real contents are shown at the cursor");
        w.previewPrefabAtPoint(lamp, Vec3{264, -200, 0});
        ps = placementsOf(ctx.doc);
        check(ps.size() == count0 + 1 && ps.back().origin == "264 -200 0" && ctx.doc.undoPosition() == pos + 1,
              "moving the cursor moves that one preview (no second placement, no extra undo step)");
        w.endPrefabPreview(false);
        check(!w.prefabPreviewActive() && ctx.doc.text() == before && ctx.doc.undoPosition() == pos, "leaving the view takes the preview back");
        w.previewPrefabAtPoint(lamp, Vec3{264, -200, 0});
        const bool dropped = w.dropAsset("prefabs", lamp, false, Vec3{264, -200, 0}, {kNoId, kNoId});
        ps = placementsOf(ctx.doc);
        check(dropped && !w.prefabPreviewActive() && ps.size() == count0 + 1 && ps.back().origin == "264 -200 0" && distinctIds(ps) &&
                  ctx.doc.undoPosition() == pos + 1,
              "dropping keeps the previewed placement: one placement, one undo step");
        undoTo(pos);
        check(ctx.doc.text() == before, "Undo removes the dropped placement");
    }

    // ------------------------------------------------------------------ the bundle's own assets while editing
    section("prefab bundles: their materials and models while editing");
    {
        const QString lampMaterials = QDir::cleanPath(lamp + "/materials"), lampModels = QDir::cleanPath(lamp + "/models");
        const auto has = [](const QStringList& roots, const QString& dir) {
            for (const QString& r : roots)
                if (QDir::cleanPath(r).compare(dir, Qt::CaseInsensitive) == 0) return true;
            return false;
        };
        check(has(w.lookupMaterialRoots(), lampMaterials) && has(w.lookupModelRoots(), lampModels),
              "the placed bundle's materials and models folders are among the map's asset folders");
        check(!has(w.customMaterialRoots(), lampMaterials) && has(QStringList{w.customMaterialRoots().front()}, QDir::cleanPath(mapDir + "/materials")),
              "the mapper's own folders stay as they were (new materials still go beside the map)");
        check(w.customMaterials() && w.customMaterials()->find("custom/lampmetal") && w.customMaterials()->find("custom/gui_brick"),
              "the material library holds the bundle's material next to the map's own");
        // A model that exists only in the bundle's folder is found for drawing.
        const size_t pos = ctx.doc.undoPosition();
        const Id shade = ctx.doc.addEntity({{"classname", "misc_model"}, {"model", "lampshade"}, {"origin", "0 -300 0"}}, {}, "shade");
        ctx.documentEdited();
        const app::PrimGeom* g = drawn(shade);
        check(g && g->mesh && g->polys.size() == 12, QString("a model from the bundle's models folder is drawn as its mesh (%1 triangles)").arg(g ? g->polys.size() : 0));
        undoTo(pos);
        // Without a placement of the bundle its folders are not used.
        undoTo(posStart);
        check(placementsOf(ctx.doc).empty() && !has(w.lookupMaterialRoots(), lampMaterials) && w.customMaterials() &&
                  !w.customMaterials()->find("custom/lampmetal") && w.customMaterials()->find("custom/gui_brick"),
              "when the last placement is gone the bundle's folders leave the map's asset folders again");
        check(ctx.doc.text() == textStart, "the map is as it was opened");
        // Back to two lamps for the next checks.
        w.dropAsset("prefabs", lamp, false, Vec3{64, 64, 0}, {kNoId, kNoId});
        w.dropAsset("prefabs", lamp, false, Vec3{-128, 64, 0}, {kNoId, kNoId});
        const auto ps = placementsOf(ctx.doc);
        lampA = ps.empty() ? kNoId : ps[0].id;
        check(ps.size() == 2 && ps[0].pid == "p1" && ps[1].pid == "p2" && w.customMaterials()->find("custom/lampmetal"),
              "placing the bundle again brings its material back");
    }

    // ------------------------------------------------------------------ the Entity dock: description and parameters
    section("prefab bundles: description and parameters in the Entity dock");
    {
        select({{lampA, kNoId}});
        check(panel->active() && panel->entity() == lampA && panel->titleText() == "Street lamp" &&
                  panel->descriptionText() == "A lamp post with its own metal material and shade model." && panel->placementIdText().contains("p1"),
              "a selected bundle placement shows the bundle's title, description and its placement id");
        check(panel->badgeText().isEmpty(), "no badge for a bundle that builds its main contents");
        check(panel->parameterCount() == 3 && panel->parameterLabel(0) == "Reach of the light" && panel->parameterLabel(1) == "Light mode" &&
                  panel->parameterLabel(2) == "Post material",
              QString("the bundle's three parameters are fields with their titles (%1)").arg(panel->parameterCount()));
        auto* spin = qobject_cast<QDoubleSpinBox*>(panel->parameterField(0));
        auto* combo = qobject_cast<QComboBox*>(panel->parameterField(1));
        auto* edit = qobject_cast<QLineEdit*>(panel->parameterField(2));
        {
            // The panel sits in the Entity dock, above the key list of the inspector.
            for (QWidget* pw = panel; pw; pw = pw->parentWidget())
                if (auto* dw = qobject_cast<ads::CDockWidget*>(pw)) {
                    dw->toggleView(true);
                    dw->setAsCurrentTab();
                }
            QTest::qWait(300);
            QWidget* insp = w.inspector();
            QWidget* host = panel->parentWidget();
            check(host && insp && insp->parentWidget() == host && panel->isVisible() && insp->isVisible() && spin && spin->isVisible() &&
                      spin->width() > 40 && panel->geometry().bottom() <= insp->geometry().top() && panel->height() > 60,
                  QString("the panel is shown in the Entity dock above the entity inspector (panel %1 x %2, inspector below at y %3)")
                      .arg(panel->width()).arg(panel->height()).arg(insp ? insp->geometry().top() : -1));
        }
        check(spin && spin->minimum() == 64 && spin->maximum() == 512 && spin->value() == 200 && panel->parameterHint(0) == "64 to 512, default 200",
              "a number shows its default and is limited to its range: " + panel->parameterHint(0));
        check(combo && combo->count() == 2 && combo->currentText() == "steady" && edit && edit->text() == "custom/lampmetal",
              "a choice lists its values and a material shows its default");
        const Entity* e = ctx.doc.findEntity(lampA);
        PrefabBundle bundle;
        const bool isBundle = w.prefabBundleOf(lampA, bundle) && bundle.manifest.parameters.size() == 3;
        check(isBundle, "the placement resolves to its bundle");
        if (isBundle && spin && combo && edit && e) {
            const auto key0 = prefabParameterKey(bundle.manifest.parameters[0], "320");
            const auto key1 = prefabParameterKey(bundle.manifest.parameters[1], "flicker");
            const auto key2 = prefabParameterKey(bundle.manifest.parameters[2], "custom/gui_brick");
            const size_t pos = ctx.doc.undoPosition();
            const std::string before = ctx.doc.text();
            const auto valueOf = [&](const std::string& key) {
                const Entity* x = ctx.doc.findEntity(lampA);
                const std::string* v = x ? x->value(key) : nullptr;
                return v ? *v : std::string("(none)");
            };
            check(panel->setParameter(0, "320") && valueOf(key0.first) == "320" && key0.first == "iw8r_ovr:lamp_light:radius" &&
                      ctx.doc.undoPosition() == pos + 1 && panel->parameterValue(0) == "320",
                  "typing a number writes the key prefabParameterKey gives (iw8r_ovr:lamp_light:radius 320), one undo step");
            const bool typedHigh = panel->setParameter(0, "9999");
            check(typedHigh && valueOf(key0.first) == "512" && panel->parameterValue(0) == "512",
                  "a number outside the range is held at the limit (" + qs(valueOf(key0.first)) + ")");
            check(panel->setParameter(1, "flicker") && valueOf(key1.first) == "lamp_flicker" && key1.second == "lamp_flicker",
                  "a choice writes its template with the chosen value (script_noteworthy lamp_flicker)");
            check(panel->setParameter(2, "custom/gui_brick") && valueOf(key2.first) == "custom/gui_brick" && key2.first == "iw8r_mat:custom/lampmetal",
                  "a material parameter writes the material swap key (iw8r_mat:custom/lampmetal)");
            check(!panel->setParameter(2, "custom/gui_brick"), "typing the same value again changes nothing");
            // The other placement keeps the bundle's values.
            const auto ps = placementsOf(ctx.doc);
            const Entity* other = ps.size() > 1 ? ctx.doc.findEntity(ps[1].id) : nullptr;
            check(other && !other->value(key0.first) && !other->value(key2.first), "the other placement of the same bundle is not changed");
            check(panel->resetParameter(0) && valueOf(key0.first) == "(none)" && panel->parameterValue(0) == "200",
                  "Default takes the key away again and the field shows the bundle's default");
            w.action("Undo")->trigger();
            check(valueOf(key0.first) == "512" && panel->parameterValue(0) == "512", "Undo brings the value back, and the field follows");
            panel->grab().save(QDir(outDir).filePath("bundle_panel.png"));
            undoTo(pos);
            check(ctx.doc.text() == before && panel->parameterValue(0) == "200" && panel->parameterValue(1) == "steady",
                  "undoing every step removes the keys; the fields show the defaults again");
        }
        // Other selections hide the panel; the fallback bundle shows its badge.
        Id spawn = kNoId;
        for (const auto& x : ctx.doc.map().entities)
            if (x.classname() == "mp_tdm_spawn") spawn = x.id;
        select({{spawn, kNoId}});
        check(!panel->active() && panel->isHidden(), "for an entity that is not a bundle placement the panel is hidden");
        const size_t pos = ctx.doc.undoPosition();
        w.dropAsset("prefabs", fbDoor, false, Vec3{-200, 200, 0}, {kNoId, kNoId});
        check(panel->active() && panel->titleText() == "Fallback door" && panel->badgeText() == "FALLBACK" &&
                  panel->badgeReason().contains("built as a plain box") && panel->parameterCount() == 0,
              "a placement of a bundle that builds its fallback says so in the panel: " + panel->badgeReason());
        w.dropAsset("prefabs", plain, false, Vec3{-200, -200, 0}, {kNoId, kNoId});
        check(!panel->active(), "a placement of a plain .map prefab has no bundle panel");
        undoTo(pos);
        // The shipped jump pad, as the mapper uses it: drag it in, set its launch speed.
        if (!shippedPad.isEmpty()) {
            const bool placed = w.dropAsset("prefabs", shippedPad, false, Vec3{0, -300, 0}, {kNoId, kNoId});
            const auto ps = placementsOf(ctx.doc);
            const Id pad = ps.empty() ? kNoId : ps.back().id;
            const app::PrimGeom* g = drawn(pad);
            check(placed && !ps.empty() && ps.back().model == "bundles/gameplay/jump_pad.iw8prefab" && !ps.back().pid.empty() && distinctIds(ps) &&
                      g && g->prefab && g->bounds.valid() && g->bounds.maxs.z >= 24,
                  "the shipped jump pad is placed as one object with its own id and drawn with its contents");
            auto* speed = qobject_cast<QDoubleSpinBox*>(panel->parameterField(0));
            check(panel->active() && panel->titleText() == "Jump pad with effect and sound" && panel->parameterCount() == 1 &&
                      panel->parameterLabel(0).startsWith("Launch speed") && speed && speed->minimum() == 100 && speed->maximum() == 3000 &&
                      speed->value() == 700,
                  "its panel shows the description and the launch speed (100 to 3000, default 700)");
            const bool set = panel->setParameter(0, "900");
            const Entity* pe = ctx.doc.findEntity(pad);
            const std::string* written = pe ? pe->value("iw8r_ovr:jump_pad_trigger:iw8r_jump") : nullptr;
            check(set && written && *written == "0 0 900", "a launch speed of 900 is written as iw8r_jump 0 0 900 for this pad's trigger");
            undoTo(pos);
        }
    }

    // ------------------------------------------------------------------ Save Selection As Prefab
    section("prefab bundles: Save Selection As Prefab");
    const QString saved = QDir::cleanPath(lib + "/gui/gui_gate_set.iw8prefab");
    const auto selectParts = [&] {
        std::vector<SelRef> refs;
        for (const auto& e : ctx.doc.map().entities) {
            if (e.isWorldspawn()) {
                for (const auto& it : e.items)
                    if (const Primitive* p = it.prim(); p && p->bounds().valid() && p->bounds().mins.z >= 0) refs.push_back({e.id, it.id});
                continue;
            }
            const std::string cls = e.classname();
            const std::string* tn = e.value("targetname");
            if (cls == "script_brushmodel" || cls == "trigger_multiple") {
                // A brush entity is selected by its brushes, as a click in a view selects it.
                for (const auto& it : e.items)
                    if (it.prim()) refs.push_back({e.id, it.id});
            } else if (cls == "sound_emitter" || (cls == "misc_model" && !tn)) {
                refs.push_back({e.id, kNoId});
            }
        }
        select(refs);
        return refs.size();
    };
    {
        check(w.action("SaveSelectionAsPrefab") && w.action("SaveSelectionAsMapPrefab") && w.action("OpenBundleFolder") && w.action("MakePrefabUnique"),
              "File has Save Selection As Prefab, the old plain .map save, Open Bundle Folder and Make Unique");
        check(QDir::cleanPath(w.prefabSaveFolder()).compare(QDir::cleanPath(lib), Qt::CaseInsensitive) == 0,
              "new prefabs go into the first prefab folder of the Preferences: " + w.prefabSaveFolder());
        const size_t parts = selectParts();
        const AABB selBounds = ctx.doc.selectionBounds();
        check(parts == 5, QString("the pillar, the crate model, the gate, its trigger and the sound are selected (%1)").arg(parts));
        const size_t pos = ctx.doc.undoPosition();
        QStringList lines;
        QString folderLine;
        bool dialogSeen = false;
        driveModals({[&](QWidget* modal) {
            auto* dlg = dynamic_cast<PrefabSaveDialog*>(modal);
            dialogSeen = dlg != nullptr;
            if (!dlg) {
                if (auto* d = qobject_cast<QDialog*>(modal)) d->reject();
                return;
            }
            dlg->findChild<QLineEdit*>("prefabSaveTitle")->setText("GUI gate set");
            dlg->findChild<QPlainTextEdit*>("prefabSaveDescription")->setPlainText("A gate with its trigger, a crate and a hum.");
            dlg->findChild<QLineEdit*>("prefabSaveCategory")->setText("gui");
            dlg->findChild<QLineEdit*>("prefabSaveTags")->setText("gate, test");
            lines = dlg->travelLines();
            folderLine = dlg->folderText();
            dlg->grab().save(QDir(outDir).filePath("bundle_save_dialog.png"));
            dlg->accept();
        }});
        w.action("SaveSelectionAsPrefab")->trigger();
        QApplication::setActiveWindow(&w);
        w.activateWindow();
        QApplication::processEvents();
        const QString all = lines.join("\n");
        check(dialogSeen && lines.size() >= 8, QString("the dialog lists what travels with the prefab (%1 lines)").arg(lines.size()));
        check(all.contains("3 brush(es)") && all.contains("4 entit") && all.contains("script_brushmodel") && all.contains("trigger_multiple"),
              "it lists the brushes and the entities by class");
        check(all.contains("material  custom/gui_brick") && all.contains("model  gui_crate") && all.contains("gui_brick.png"),
              "it lists the mapper's own material, its image and the model that are copied into the bundle");
        check(all.contains("sound  emt_light_fluorescent_hum1_int") && all.contains("Stock assets"),
              "it lists the stock sound the prefab needs by name");
        check(all.contains("kept apart per placement: gate") && all.contains("outside_trig"),
              "it lists the names kept apart per placement and the linked trigger that is not selected");
        check(folderLine.contains("gui_gate_set.iw8prefab") && folderLine.contains("gui"), "it says where the bundle is saved: " + folderLine);
        PrefabBundle b;
        std::string berr;
        const bool loaded = loadPrefabBundle(saved.toStdString(), b, &berr);
        check(loaded && b.manifest.id == "gui_gate_set" && b.title == "GUI gate set" && b.manifest.description == "A gate with its trigger, a crate and a hum." &&
                  b.category == "gui" && b.manifest.tags == std::vector<std::string>{"gate", "test"},
              "the bundle folder is written with id, title, description, category and tags " + qs(berr));
        check(loaded && b.manifest.materials.size() == 1 && b.manifest.materials[0].name == "custom/gui_brick" && b.manifest.models.size() == 1 &&
                  b.manifest.models[0].name == "gui_crate" && QFileInfo(saved + "/" + qs(b.manifest.models[0].file)).isFile() &&
                  QFileInfo(saved + "/" + qs(b.manifest.materials[0].file)).isFile() && b.warnings.empty(),
              "the mapper's material and model files are inside the bundle");
        check(loaded && b.manifest.stockSounds == std::vector<std::string>{"emt_light_fluorescent_hum1_int"} &&
                  b.manifest.namesLocal == std::vector<std::string>{"gate"},
              "the stock sound is named in requires and 'gate' is a name of the placement");
        const QImage thumb(saved + "/thumb.png");
        check(!thumb.isNull() && thumb.width() == 256 && !b.thumbFile.empty(), "a rendered thumb.png is in the bundle");
        thumb.save(QDir(outDir).filePath("bundle_saved_thumb.png"));
        // The selection became one placement of the new bundle.
        const auto ps = placementsOf(ctx.doc);
        const Placement* mine = nullptr;
        for (const Placement& p : ps)
            if (p.model == "gui/gui_gate_set.iw8prefab") mine = &p;
        check(mine && mine->pid == "p3" && ctx.doc.undoPosition() == pos + 1 && distinctIds(ps),
              "the selection is replaced by one placement of the bundle with its own id (one undo step)");
        if (mine) {
            const app::PrimGeom* g = drawn(mine->id);
            check(g && g->prefab && g->bounds.valid() && selBounds.valid() && std::fabs(g->bounds.mins.x - selBounds.mins.x) < 9 &&
                      std::fabs(g->bounds.maxs.y - selBounds.maxs.y) < 9 && std::fabs(g->bounds.maxs.z - selBounds.maxs.z) < 9,
                  QString("the placement is drawn where the saved objects were (drawn %1 %2 %3 .. %4 %5 %6, selection %7 %8 %9 .. %10 %11 %12)")
                      .arg(g ? g->bounds.mins.x : 0).arg(g ? g->bounds.mins.y : 0).arg(g ? g->bounds.mins.z : 0)
                      .arg(g ? g->bounds.maxs.x : 0).arg(g ? g->bounds.maxs.y : 0).arg(g ? g->bounds.maxs.z : 0)
                      .arg(selBounds.mins.x).arg(selBounds.mins.y).arg(selBounds.mins.z)
                      .arg(selBounds.maxs.x).arg(selBounds.maxs.y).arg(selBounds.maxs.z));
        }
        bool looseLeft = false;
        for (const auto& e : ctx.doc.map().entities) looseLeft = looseLeft || e.classname() == "script_brushmodel" || e.classname() == "sound_emitter";
        check(!looseLeft, "the saved objects are no longer loose in the map");
        const int row = pb->rowOfFile(saved);
        check(row >= 0 && pb->titleAt(row) == "GUI gate set" && pb->pictureAt(row).width() > 0 && pb->pictureFromFileAt(row),
              "the Prefabs dock lists the new bundle with its picture");
        pb->setFilter("gate test");
        check(pb->visibleCount() == 1, QString("its tags find it (%1 shown)").arg(pb->visibleCount()));
        pb->setFilter("");
    }
    {
        // Saving again under the same id asks before replacing. First answer: No (nothing changes).
        const size_t pos = ctx.doc.undoPosition();
        w.action("Undo")->trigger();  // the loose objects are back
        selectParts();
        const QByteArray manifestBefore = slurp(saved + "/prefab.json");
        // What only a hand can write (a parameter) must survive a replace.
        {
            PrefabBundle b;
            loadPrefabBundle(saved.toStdString(), b, nullptr);
            PrefabParameter p;
            p.name = "height";
            p.label = "Gate height";
            p.type = "number";
            p.defaultValue = "96";
            p.selector = "gate";
            p.key = "iw8r_note";
            p.valueTemplate = "{height}";
            b.manifest.parameters.push_back(p);
            putFile(saved + "/prefab.json", QByteArray::fromStdString(prefabManifestText(b.manifest)));
        }
        const QByteArray manifestHand = slurp(saved + "/prefab.json");
        QString asked;
        bool warned = false;
        driveModals({[&](QWidget* modal) {
                         if (auto* dlg = dynamic_cast<PrefabSaveDialog*>(modal)) {
                             dlg->findChild<QLineEdit*>("prefabSaveTitle")->setText("GUI gate set");
                             warned = dlg->folderText().contains("already there");
                             dlg->accept();
                         } else if (auto* d = qobject_cast<QDialog*>(modal)) d->reject();
                     },
                     [&](QWidget* modal) {
                         if (auto* box = qobject_cast<QMessageBox*>(modal)) {
                             asked = box->text();
                             box->button(QMessageBox::No)->click();
                         } else if (auto* d = qobject_cast<QDialog*>(modal)) d->reject();
                     },
                     [&](QWidget* modal) {
                         if (auto* d = qobject_cast<QDialog*>(modal)) d->reject();  // back in the dialog: cancel
                     }});
        w.action("SaveSelectionAsPrefab")->trigger();
        QApplication::setActiveWindow(&w);
        w.activateWindow();
        QApplication::processEvents();
        check(warned && asked.contains("gui_gate_set") && asked.contains("Replace it?"),
              "saving under an id that is taken asks before replacing: " + asked.section('\n', 0, 0));
        check(slurp(saved + "/prefab.json") == manifestHand && manifestHand != manifestBefore, "answering No leaves the saved bundle as it was");
        QString err;
        MainWindow::PrefabBundleSave again;
        again.id = "gui_gate_set";
        again.title = "GUI gate set";
        const bool refusedAgain = w.saveSelectionAsBundle(again, &err).isEmpty();
        check(refusedAgain && err.contains("already") && slurp(saved + "/prefab.json") == manifestHand,
              "without the mapper's yes the bundle is never replaced: " + err);
        // Second time: Yes.
        selectParts();
        driveModals({[&](QWidget* modal) {
                         if (auto* dlg = dynamic_cast<PrefabSaveDialog*>(modal)) {
                             dlg->findChild<QLineEdit*>("prefabSaveTitle")->setText("GUI gate set");
                             dlg->findChild<QPlainTextEdit*>("prefabSaveDescription")->setPlainText("Second version.");
                             dlg->findChild<QLineEdit*>("prefabSaveCategory")->setText("somewhere_else");
                             dlg->accept();
                         } else if (auto* d = qobject_cast<QDialog*>(modal)) d->reject();
                     },
                     [&](QWidget* modal) {
                         if (auto* box = qobject_cast<QMessageBox*>(modal)) box->button(QMessageBox::Yes)->click();
                         else if (auto* d = qobject_cast<QDialog*>(modal)) d->reject();
                     }});
        w.action("SaveSelectionAsPrefab")->trigger();
        QApplication::setActiveWindow(&w);
        w.activateWindow();
        QApplication::processEvents();
        PrefabBundle b;
        const bool loaded = loadPrefabBundle(saved.toStdString(), b, nullptr);
        check(loaded && b.manifest.description == "Second version." && !QFileInfo(lib + "/somewhere_else").exists(),
              "answering Yes replaces the bundle in its folder (no second copy under another category)");
        check(loaded && b.manifest.parameters.size() == 1 && b.manifest.parameters[0].label == "Gate height",
              "the parameter written into prefab.json by hand is kept by the replace");
        const auto ps = placementsOf(ctx.doc);
        check(!ps.empty() && ps.back().model == "gui/gui_gate_set.iw8prefab" && distinctIds(ps), "the selection is one placement of it again");
        (void)pos;
    }

    // ------------------------------------------------------------------ explode, edit, bundle folder, make unique
    section("prefab bundles: explode, edit, bundle folder, make unique");
    {
        Id gate = kNoId;
        std::string gatePid;
        for (const Placement& p : placementsOf(ctx.doc))
            if (p.model == "gui/gui_gate_set.iw8prefab") gate = p.id, gatePid = p.pid;
        select({{gate, kNoId}});
        check(QDir::cleanPath(w.selectedBundleFolder()).compare(saved, Qt::CaseInsensitive) == 0,
              "Open Bundle Folder knows the folder of the selected placement: " + w.selectedBundleFolder());
        check(panel->active() && panel->parameterCount() == 1 && panel->parameterLabel(0) == "Gate height", "its hand-written parameter is a field in the Entity dock");
        // Explode: loose objects with the names the build gives them.
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        QString err;
        const bool exploded = w.explodeSelectedPrefab(&err);
        bool namedDoor = false, trigger = false, stillPlaced = false;
        const std::string scopedName = gatePid + "_gate";
        for (const auto& e : ctx.doc.map().entities) {
            if (e.classname() == "script_brushmodel" && e.value("targetname") && *e.value("targetname") == scopedName) namedDoor = true;
            if (e.classname() == "trigger_multiple" && e.value("target") && *e.value("target") == scopedName) trigger = true;
            if (e.id == gate) stillPlaced = true;
        }
        check(exploded && namedDoor && trigger && !stillPlaced,
              QString("Explode leaves loose objects whose names carry the placement id (%1 and the trigger that targets it) %2").arg(qs(scopedName), err));
        undoTo(pos);
        check(ctx.doc.text() == before, "Undo brings the placement back");
        // Edit Prefab opens the bundle's prefab.map; Leave returns.
        check(w.saveTo(hostPath, &err), "the map is saved " + err);
        for (const Placement& p : placementsOf(ctx.doc))
            if (p.model == "gui/gui_gate_set.iw8prefab") gate = p.id;
        select({{gate, kNoId}});
        const bool entered = w.enterPrefab();
        check(entered && w.prefabDepth() == 1 &&
                  QFileInfo(qs(ctx.doc.path())).absoluteFilePath().compare(QFileInfo(saved + "/prefab.map").absoluteFilePath(), Qt::CaseInsensitive) == 0,
              "Edit Prefab opens the bundle's prefab.map: " + qs(ctx.doc.path()));
        if (entered) check(w.leavePrefab() && w.prefabDepth() == 0 && qs(ctx.doc.path()).endsWith("mp_bundle_gui.map"), "Leave Prefab returns to the map");
        // A bundle that builds its fallback is still edited in its main contents.
        w.dropAsset("prefabs", fbDoor, false, Vec3{-200, 200, 0}, {kNoId, kNoId});
        check(w.saveTo(hostPath, &err), "the map is saved again " + err);
        const bool enteredFb = w.enterPrefab();
        check(enteredFb && QDir::fromNativeSeparators(qs(ctx.doc.path())).endsWith("fb_door.iw8prefab/prefab.map"), "a bundle that builds its fallback is edited in its prefab.map: " + qs(ctx.doc.path()));
        if (enteredFb) w.leavePrefab();
        // Make Unique: a copy beside the map with a new id; this placement uses it.
        for (const Placement& p : placementsOf(ctx.doc))
            if (p.model == "gui/gui_gate_set.iw8prefab") gate = p.id;
        select({{gate, kNoId}});
        const size_t posU = ctx.doc.undoPosition();
        const bool unique = w.makeSelectedPrefabUnique(&err);
        const QString copy = mapDir + "/prefabs/gui_gate_set_u2.iw8prefab";
        PrefabBundle cb;
        const bool copyLoads = loadPrefabBundle(copy.toStdString(), cb, nullptr);
        const Entity* ge = ctx.doc.findEntity(gate);
        check(unique && copyLoads && cb.manifest.id == "gui_gate_set_u2" && cb.manifest.materials.size() == 1 && QFileInfo(copy + "/prefab.map").isFile() &&
                  ge && ge->value("model") && *ge->value("model") == "prefabs/gui_gate_set_u2.iw8prefab" && ctx.doc.undoPosition() == posU + 1,
              "Make Unique copies the bundle beside the map under a new id and points the placement at it (one undo step) " + err);
        PrefabBundle orig;
        check(loadPrefabBundle(saved.toStdString(), orig, nullptr) && orig.manifest.id == "gui_gate_set", "the original bundle is untouched");
        const app::PrimGeom* g = drawn(gate);
        check(g && g->prefab && g->bounds.valid() && g->bounds.maxs.z > 90, "the placement draws the copy's contents");
    }

    // ------------------------------------------------------------------ the build
    section("prefab bundles: the build");
    {
        QString err;
        check(w.saveTo(hostPath, &err), "the map is saved before another one is opened " + err);
        // A small map of its own: a floor and two placements of the lamp bundle (its material and model live only in
        // the bundle folder).
        const QString buildMap = scratch + "/build/mp_bundle_bld.map";
        {
            std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxText({{-512, -512, -16}, {512, 512, 0}}, "custom/lampmetal") + "}\n";
            t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-300 0 16\"\n\"angles\" \"0 0 0\"\n}\n";
            t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"300 0 16\"\n\"angles\" \"0 180 0\"\n}\n";
            t += "{\n\"classname\" \"misc_prefab\"\n\"model\" \"props/lamp.iw8prefab\"\n\"origin\" \"-100 0 0\"\n\"iw8r_pid\" \"p1\"\n}\n";
            t += "{\n\"classname\" \"misc_prefab\"\n\"model\" \"props/lamp.iw8prefab\"\n\"origin\" \"100 0 0\"\n\"iw8r_pid\" \"p2\"\n"
                 "\"iw8r_ovr:lamp_light:radius\" \"320\"\n}\n";
            putFile(buildMap, QByteArray::fromStdString(t));
        }
        w.openPath(buildMap);
        CompilePanel* cp = w.compilePanel();
        const bool open = cp && qs(ctx.doc.path()).endsWith("mp_bundle_bld.map");
        check(open && w.customMaterials() && w.customMaterials()->find("custom/lampmetal"),
              "a map that places a bundle has the bundle's material from the moment it is opened");
        if (open) {
            cp->run({CompilePanel::Stage::Extraction});
            QElapsedTimer t;
            t.start();
            while (cp->busy() && t.elapsed() < 180000) QTest::qWait(50);
            const QString log = cp->logText();
            QString bundleLine;
            for (const QString& line : log.split('\n'))
                if (line.contains("prefab bundle 'zq_lamp_post'") && line.contains("placement(s)")) bundleLine = line.trimmed();
            check(!cp->busy() && cp->lastOk(), "the extraction of the map with two bundle placements succeeds");
            check(bundleLine.contains("prefab bundle 'zq_lamp_post' (lamp.iw8prefab): 2 placement(s), main contents") &&
                      bundleLine.contains("its own asset folders are used: materials, models") && !bundleLine.startsWith("warning"),
                  "the build log has the bundle's line: " + bundleLine);
            check(log.contains("prefab bundles: 1 material, 1 model"), "the log says the bundle's asset folders joined the build");
            check(!log.contains("prefab bundle materials are not in the material library") && !log.contains("build grey") && !log.contains("iw8r_pid"),
                  "no warning about bundle materials or placement ids");
            // The bundle's material and model reached the extraction.
            bool material = false, model = false, lightTwice = false;
            QDirIterator it(cp->extractionDir(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                material = material || f.contains("lampmetal", Qt::CaseInsensitive);
                model = model || f.contains("lampshade", Qt::CaseInsensitive);
                if (f.endsWith(".ents")) {
                    const QByteArray ents = slurp(f);
                    lightTwice = ents.contains("p1_lamp_light") && ents.contains("p2_lamp_light");
                }
            }
            check(material && model, "the extraction holds the bundle's own material and model");
            check(lightTwice, "each placement has its own names in the built entities (p1_lamp_light, p2_lamp_light)");
        }
    }
    // Leave the window on a saved map.
    QApplication::processEvents();
}

}  // namespace iw8r::qt
