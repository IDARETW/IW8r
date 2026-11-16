import io

# GUI test of the flush asset panels (sidebar_flush_assets*.py).

ROOT = 'E:/IW8-Radiant/src/'


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


P = 'qtapp/qt_gui_test_sidebar.cpp'
rw(P, [
(r'''#include <QScrollBar>
#include <QTest>''', r'''#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>'''),
(r'''#include "main_window.h"
#include "settings.h"''', r'''#include "main_window.h"
#include "panel_chrome.h"
#include "settings.h"'''),
(r'''        auto* caption = eb->findChild<QLabel*>("effectPreviewCaption");
        const QString first = caption ? caption->text() : QString();
        check(first.startsWith("vfx/core/expl/spark_death"), "selecting an effect puts it in the panel's preview: " + first.section('\n', 1, 1));''',
 r'''        auto* caption = eb->findChild<QLabel*>("effectPreviewCaption");
        const QString first = eb->stripTitle();
        check(eb->stripShown() && first.startsWith("vfx/core/expl/spark_death") && caption,
              "selecting an effect puts it in the panel's details strip: " + first + " (" + eb->stripFacts() + ")");'''),
(r'''              "the selected effect plays in the side bar (" + caption->text().section('\n', 1, 1) + ")");''',
 r'''              "the selected effect plays in the side bar (" + caption->text().section('\n', 0, 0) + ")");'''),
(r'''        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.effectsBrowser(), w.soundsBrowser()}) {
            auto* tb = b ? b->findChild<QPushButton*>("assetTools") : nullptr;
            check(tb && tb->menu() && !tb->menu()->actions().isEmpty(),
                  QString("an asset panel has a Tools button with its kind's commands (%1)").arg(tb && tb->menu() ? tb->menu()->actions().size() : 0));
        }''', r'''        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.effectsBrowser(), w.soundsBrowser()}) {
            auto* tb = b ? b->findChild<QToolButton*>("assetTools") : nullptr;
            bool all = tb && tb->menu() && !b->toolActions().isEmpty();
            if (all)
                for (QAction* a : b->toolActions()) all &= tb->menu()->actions().contains(a);
            check(all, QString("an asset panel's dots menu holds its kind's tools (%1)").arg(b ? b->toolActions().size() : 0));
        }'''),
(r'''    section("side bar: closed panels make their first rows ahead");''', r'''    section("side bar: flush asset panels");
    {
        // Owner 2026-11-15 ("more flush menus and actually useful UIs"): every asset panel has the header line (count,
        // favourites star, dots menu), chips and an edge-to-edge list; effects, sounds and images a details strip.
        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.effectsBrowser(), w.soundsBrowser(), w.imagesBrowser()}) {
            if (!b) continue;
            waitFor([&] { return b->catalogLoaded(); }, 30000);
            b->setListMode(AssetBrowser::ListMode::All);
            b->setFilter("");
            auto* view = b->findChild<QListView*>();
            check(!b->countText().isEmpty() && b->findChild<QToolButton*>("assetFavourites") && b->findChild<QWidget*>("assetChips") && view &&
                      view->frameShape() == QFrame::NoFrame,
                  QString("an asset panel has the flush header (count %1), favourites star, chips and an edge-to-edge list").arg(b->countText()));
        }
        if (AssetBrowser* eb = w.effectsBrowser()) {
            w.showDock("Effects");
            QTest::qWait(30);
            // Kind chips: Fire lists fires only, and the count says how many of all.
            const int total = eb->visibleCount();
            eb->setChip("fire");
            QTest::qWait(20);
            int fires = 0, wrong = 0;
            for (int r = 0; r < eb->visibleCount(); ++r) (eb->groupOf(eb->visibleName(r)) == "fire" ? fires : wrong)++;
            check(eb->chipIds().contains("smoke") && fires > 10 && wrong == 0 && eb->visibleCount() < total && eb->countText().contains(" of "),
                  QString("the Effects chips narrow the list by kind (Fire: %1 of %2, %3 wrong; count \"%4\")").arg(fires).arg(total).arg(wrong).arg(eb->countText()));
            eb->setChip("all");
            // The folder tree: a folder lists the effects under it.
            eb->setFolder("vfx/core/expl");
            QTest::qWait(20);
            int under = 0, outside = 0;
            for (int r = 0; r < eb->visibleCount(); ++r) (eb->visibleName(r).startsWith("vfx/core/expl/") ? under : outside)++;
            auto* tree = eb->findChild<QTreeWidget*>("effectFolders");
            const bool treeFollows = tree && tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole).toString() == "vfx/core/expl";
            check(under > 3 && outside == 0 && treeFollows && tree->topLevelItemCount() == 1,
                  QString("the Effects folder tree lists a folder's effects (%1 under vfx/core/expl, %2 outside; tree %3)")
                      .arg(under).arg(outside).arg(treeFollows ? "on it" : "NOT on it"));
            eb->setFolder("");
            // A known fact shows on the strip.
            eb->setFilter("vfx/core/expl/spark_death");
            QTest::qWait(20);
            eb->selectRow(0);
            const QString fx = eb->visibleName(0);
            const QString was = eb->rowFact(fx);
            eb->setRowFact(fx, "2.5 s");
            check(eb->stripShown() && eb->stripFacts().contains("one-shot 2.5 s") && eb->findChild<QPushButton*>("effectPlace"),
                  "the effect strip says its kind and length, with Place fx_origin (" + eb->stripFacts() + ")");
            eb->setRowFact(fx, was);
            eb->setFilter("");
        }
        if (AssetBrowser* sb = w.soundsBrowser()) {
            w.showDock("Sounds");
            QTest::qWait(30);
            // The Loops chip lists looping aliases only.
            const int total = sb->visibleCount();
            auto* loops = sb->findChild<QToolButton*>("chip_loops");
            if (loops) loops->click();
            QTest::qWait(20);
            auto* view = sb->findChild<QListView*>();
            int looping = 0, other = 0;
            for (int r = 0; r < sb->visibleCount() && view; ++r) (view->model()->index(r, 0).data(Qt::UserRole + 2).toBool() ? looping : other)++;
            check(loops && looping > 50 && other == 0 && sb->visibleCount() < total,
                  QString("the Sounds Loops chip lists looping aliases only (%1 loops, %2 others of %3)").arg(looping).arg(other).arg(total));
            if (loops) loops->click();
            sb->setChip("mus");
            QTest::qWait(20);
            check(sb->visibleCount() > 20 && sb->visibleName(0).startsWith("mus_"), QString("the Music chip lists music (%1)").arg(sb->visibleCount()));
            sb->setChip("all");
            // Facts on the strip; a click on a row's play button asks for that alias (the preview muted).
            sb->setFilter("amb_");
            QTest::qWait(20);
            sb->selectRow(1);
            const QString alias = sb->visibleName(1);
            sb->setRowFacts({{alias, "1.5" + QString(QChar(0x2013)) + "4.0 s" + factSep() + QChar(0x00D7) + "3"}});
            check(sb->stripShown() && sb->stripTitle() == alias && sb->stripFacts().contains("4.0 s") && sb->findChild<QPushButton*>("soundPlace"),
                  "the sound strip shows the alias, its length and variants, with Place speaker (" + sb->stripFacts() + ")");
            if (auto* sp = w.findChild<SoundPreview*>("soundPreview")) sp->setMuted(true);
            QSignalSpy asked(sb, &AssetBrowser::soundPlayRequested);
            if (view) {
                QTest::qWait(30);
                const QRect r = view->visualRect(view->model()->index(0, 0));
                QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, QPoint(r.x() + 13, r.center().y()));
            }
            check(asked.size() == 1 && asked.first().value(0).toString() == sb->visibleName(0),
                  QString("a sound row's play button plays that alias (%1 request(s))").arg(asked.size()));
            emit sb->soundStopRequested();
            if (auto* sp = w.findChild<SoundPreview*>("soundPreview")) sp->stop();
            sb->setFilter("");
        }
        if (AssetBrowser* ib = w.imagesBrowser()) {
            w.showDock("Images");
            QTest::qWait(30);
            ib->setChip("sky");
            QTest::qWait(20);
            int sky = 0, wrong = 0;
            for (int r = 0; r < std::min(ib->visibleCount(), 400); ++r) (ib->groupOf(ib->visibleName(r)) == "sky" ? sky : wrong)++;
            auto* view = ib->findChild<QListView*>();
            check(view && view->viewMode() == QListView::IconMode && sky > 5 && wrong == 0,
                  QString("the Images panel is a thumbnail grid with type chips (Sky: %1, %2 wrong)").arg(sky).arg(wrong));
            ib->setChip("all");
        }
        if (AssetBrowser* eb = w.effectsBrowser()) {
            // The star: favourites only.
            const QString fav = eb->visibleName(0);
            const bool wasFav = eb->isFavourite(fav);
            eb->setFavourite(fav, true);
            auto* star = eb->findChild<QToolButton*>("assetFavourites");
            if (star) star->click();
            QTest::qWait(20);
            const int favs = eb->visibleCount();
            bool listed = false;
            for (int r = 0; r < favs; ++r) listed |= eb->visibleName(r) == fav;
            if (star) star->click();
            check(star && listed && favs < 50 && eb->visibleCount() > favs,
                  QString("the star lists the favourites only (%1 shown, then %2)").arg(favs).arg(eb->visibleCount()));
            eb->setFavourite(fav, wasFav);
        }
    }

    section("side bar: closed panels make their first rows ahead");'''),
])
print('ok')
