#include "qt_gui_test_sidebar.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <vector>

#include "asset_browser.h"
#include "main_window.h"
#include "settings.h"
#include "sound_preview.h"
#include "thumb_store.h"
#include "tools_panel.h"
#include "vfx_preview.h"

namespace iw8r::qt {

namespace {

// Bits, most significant first (the FLAC bit order).
struct BitWriter {
    QByteArray bytes;
    int used = 8;
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if (used == 8) {
                bytes.append('\0');
                used = 0;
            }
            if ((v >> i) & 1) bytes[bytes.size() - 1] = char(uint8_t(bytes[bytes.size() - 1]) | (0x80 >> used));
            ++used;
        }
    }
    void align() { used = 8; }
};

// One FLAC frame of 192 mono 16-bit samples at 44.1 kHz: a FIXED order-1 subframe with a Rice-coded residual
// (parameter 4) when `rice`, else a VERBATIM one.
QByteArray flacFrame(const std::vector<int16_t>& s, bool rice) {
    BitWriter w;
    w.put(0x3FFE, 14);
    w.put(0, 1);
    w.put(0, 1);   // fixed block size
    w.put(1, 4);   // 192
    w.put(9, 4);   // 44.1 kHz
    w.put(0, 4);   // mono
    w.put(4, 3);   // 16 bits
    w.put(0, 1);
    w.put(0, 8);   // frame 0
    w.put(0, 8);   // CRC-8 (not checked)
    if (!rice) {
        w.put(0, 1);
        w.put(1, 6);  // VERBATIM
        w.put(0, 1);
        for (const int16_t v : s) w.put(uint16_t(v), 16);
    } else {
        w.put(0, 1);
        w.put(9, 6);  // FIXED, order 1
        w.put(0, 1);
        w.put(uint16_t(s[0]), 16);
        w.put(0, 2);  // Rice, 4-bit parameters
        w.put(0, 4);  // one partition
        const int param = 4;
        w.put(param, 4);
        for (size_t i = 1; i < s.size(); ++i) {
            const int32_t res = int32_t(s[i]) - s[i - 1];
            const uint32_t u = res >= 0 ? uint32_t(res) << 1 : (uint32_t(-res) << 1) - 1;
            for (uint32_t q = u >> param; q > 0; --q) w.put(0, 1);
            w.put(1, 1);
            w.put(u & ((1u << param) - 1), param);
        }
    }
    w.align();
    w.put(0, 16);  // CRC-16 (not checked)
    return w.bytes;
}

void waitFor(const std::function<bool()>& done, int ms) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QTest::qWait(20);
}

}  // namespace

void runSidebarChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                      const QString& outDir) {
    section("side bar: model tiles");
    if (AssetBrowser* mb = w.modelsBrowser()) {
        w.showDock("Models");
        waitFor([&] { return mb->catalogLoaded(); }, 30000);
        auto* view = mb->findChild<QListView*>();
        auto* size = mb->findChild<QComboBox*>("assetTileSize");
        check(view && view->viewMode() == QListView::IconMode && size && size->count() == 3,
              "the Models panel shows tiles, with a Small / Medium / Large size choice");
        const AssetBrowser::TileSize was = mb->tileSize();
        mb->setTileSize(AssetBrowser::TileSize::Small);
        QTest::qWait(50);
        const int small = mb->tileWidth(), smallSide = mb->iconSide();
        mb->setTileSize(AssetBrowser::TileSize::Large);
        QTest::qWait(50);
        const int large = mb->tileWidth(), largeSide = mb->iconSide();
        check(small > 0 && large > small && largeSide > smallSide && size->currentIndex() == 2 &&
                  Settings::instance().value("assets/models/tileSize").toInt() == 2,
              QString("the size choice changes the tiles (%1 px -> %2 px wide, pictures %3 -> %4 px) and is kept in the settings")
                  .arg(small).arg(large).arg(smallSide).arg(largeSide));
        mb->setTileSize(was);
        // The whole list (earlier checks may leave a list mode, a kit or a search behind).
        mb->setListMode(AssetBrowser::ListMode::All);
        mb->setKit("");
        mb->setFilter("accessory_money_pile_large");
        QTest::qWait(50);
        // Preview only on selection: a selected model shows it; cleared, it goes; a preview that arrives for a model no
        // longer selected (its picture finished) does not bring it back.
        mb->selectRow(0);
        waitFor([&] { return mb->previewShown(); }, 5000);
        const bool shownSelected = mb->previewShown();
        view->setCurrentIndex({});
        QTest::qWait(20);
        const bool hiddenCleared = !mb->previewShown();
        w.previewModel("accessory_money_pile_large");
        QTest::qWait(50);
        const bool hiddenLate = !mb->previewShown();
        mb->selectRow(0);
        waitFor([&] { return mb->previewShown(); }, 5000);
        check(shownSelected && hiddenCleared && hiddenLate && mb->previewShown() && mb->findChild<QPushButton*>("modelPreview3d"),
              QString("the compact model preview shows only while a model is selected, with its Preview in 3D button "
                      "(selected %1, cleared %2, late preview %3)")
                  .arg(shownSelected ? "shown" : "HIDDEN", hiddenCleared ? "hidden" : "SHOWN", hiddenLate ? "hidden" : "SHOWN"));
        // A search that leaves the selected model out takes the preview away too.
        mb->setFilter("zzzz_no_such_model");
        QTest::qWait(50);
        const bool hiddenFiltered = !mb->previewShown();
        mb->setFilter("accessory_money_pile_large");
        QTest::qWait(50);
        check(hiddenFiltered, "the model preview goes when the search leaves the selected model out");
        // The tile names: elided in the tile, the whole name in the tooltip.
        const QModelIndex idx = view->model()->index(0, 0);
        check(idx.isValid() && idx.data(Qt::ToolTipRole).toString().startsWith("accessory_money_pile_large"),
              "a model tile's tooltip starts with its whole name");
        mb->setFilter("");
    }

    section("side bar: narrow column");
    {
        // Every panel fits a 300 px column without a sideways scroll bar.
        for (const char* name : {"Materials", "Models", "Effects", "Sounds", "Images", "Tools"}) {
            w.showDock(name);
            QTest::qWait(50);
            QWidget* panel = nullptr;
            if (QString(name) == "Materials") panel = w.materialBrowser();
            else if (QString(name) == "Models") panel = w.modelsBrowser();
            else if (QString(name) == "Effects") panel = w.effectsBrowser();
            else if (QString(name) == "Sounds") panel = w.soundsBrowser();
            else if (QString(name) == "Images") panel = w.imagesBrowser();
            else panel = w.findChild<ToolsPanel*>();
            if (!panel) {
                check(false, QString("the %1 panel exists").arg(name));
                continue;
            }
            const int minW = panel->minimumSizeHint().width();
            // The widest part, named when the panel is too wide.
            QString widest;
            int widestW = 0;
            for (QWidget* c : panel->findChildren<QWidget*>()) {
                const int cw = std::max(c->minimumSizeHint().width(), c->minimumWidth());
                if (c->isVisibleTo(panel) && cw > widestW) widestW = cw, widest = QString("%1 %2").arg(c->metaObject()->className(), c->objectName());
            }
            if (QString(name) == "Models")
                if (QWidget* box = panel->findChild<QWidget*>("modelPreviewBox"))
                    for (QWidget* c : box->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                        widest += QString("; %1 %2 min %3 hint %4 policy %5").arg(c->metaObject()->className(), c->objectName())
                                      .arg(c->minimumSizeHint().width()).arg(c->sizeHint().width()).arg(int(c->sizePolicy().horizontalPolicy()));
            // A scrolled content (the Tools panel) must fit too: its rows wrap, they never scroll sideways.
            if (auto* sa = panel->findChild<QScrollArea*>(); sa && sa->widget())
                check(sa->widget()->minimumSizeHint().width() <= 280,
                      QString("the %1 panel's scrolled rows fit a 300 px column (need %2 px)").arg(name).arg(sa->widget()->minimumSizeHint().width()));
            check(minW <= 300, QString("the %1 panel fits a 300 px column (needs %2 px; widest part %3, %4 px)").arg(name).arg(minW).arg(widest).arg(widestW));
        }
    }

    section("side bar: stored thumbnails off the GUI thread");
    {
        // A pack with three pictures; the decoder brings them back fitted to the tile, and says which name it lacks.
        const QString pack = QDir(outDir).filePath("sidebar_decoder.pack");
        QFile::remove(pack);
        auto store = std::make_shared<ThumbStore>(pack);
        for (int i = 0; i < 3; ++i) {
            QImage img(256, 128, QImage::Format_RGB32);
            img.fill(QColor(40 * i + 60, 90, 200));
            store->put(QString("pic%1").arg(i), 1, img);
        }
        std::vector<ThumbDecoder::Result> got;
        QObject ctx;
        {
            ThumbDecoder dec([store](const QString& n) { return store->getAny(n); },
                             [&got](std::vector<ThumbDecoder::Result> r) { got.insert(got.end(), r.begin(), r.end()); }, &ctx, 2);
            dec.want({"pic0", "pic1", "missing", "pic2"}, 64);
            // Writes while the workers read (the GUI thread appends records).
            for (int i = 3; i < 12; ++i) {
                QImage img(96, 96, QImage::Format_RGB32);
                img.fill(Qt::green);
                store->put(QString("pic%1").arg(i), 1, img);
            }
            waitFor([&] { return got.size() >= 4; }, 10000);
        }
        int fitted = 0, missing = 0;
        for (const auto& r : got) {
            fitted += !r.image.isNull() && r.image.width() == 64 && r.image.height() == 32;
            missing += r.name == "missing" && r.image.isNull();
        }
        check(got.size() == 4 && fitted == 3 && missing == 1,
              QString("the thumbnail decoder reads stored pictures on its threads and fits them to the tile (%1 results, %2 fitted, %3 missing)")
                  .arg(got.size()).arg(fitted).arg(missing));
        QFile::remove(pack);
    }

    section("side bar: effect preview");
    if (AssetBrowser* eb = w.effectsBrowser()) {
        w.showDock("Effects");
        waitFor([&] { return eb->catalogLoaded(); }, 30000);
        eb->setListMode(AssetBrowser::ListMode::All);
        eb->setFilter("vfx/core/expl/spark_death");
        QTest::qWait(50);
        eb->selectRow(0);
        auto* caption = eb->findChild<QLabel*>("effectPreviewCaption");
        const QString first = caption ? caption->text() : QString();
        check(first.startsWith("vfx/core/expl/spark_death"), "selecting an effect puts it in the panel's preview: " + first.section('\n', 1, 1));
        // Exported (now or before): the particle preview plays in the panel.
        waitFor([&] { return eb->effectPreview()->isVisible() || caption->text().contains("failed") || caption->text().contains("No export"); },
                120000);
        check(eb->effectPreview()->isVisible() && eb->effectPreview()->effect().emitters.size() > 0,
              "the selected effect plays in the side bar (" + caption->text().section('\n', 1, 1) + ")");
        // Owner 2026-11-15: "the preview should be square with the vfx fit in the middle centered", and the particles
        // drawn as their sprites ("a lot of vfx shows as faint orbs").
        if (VfxPreview* pv = eb->effectPreview(); pv && pv->isVisible()) {
            const QRect vr = pv->viewRect();
            check(vr.width() == vr.height() && vr.width() >= std::min(pv->width(), 520) - 1 && pv->height() > vr.height(),
                  QString("the effect preview is a square across the panel (%1 x %2 in a %3 x %4 px preview)")
                      .arg(vr.width()).arg(vr.height()).arg(pv->width()).arg(pv->height()));
            for (const int width : {300, 420}) {
                VfxPreview sq;
                sq.setAttribute(Qt::WA_DontShowOnScreen);
                sq.resize(width, 200);
                sq.show();
                sq.setEffect(pv->effect());
                QTest::qWait(20);
                const QRect r = sq.viewRect();
                check(r.width() == width && r.height() == width && sq.height() > width,
                      QString("at %1 px wide the preview's square follows the width (%2 x %3, the preview %4 px high)")
                          .arg(width).arg(r.width()).arg(r.height()).arg(sq.height()));
            }
            waitFor([&] {
                pv->frame();
                return pv->materialsPending() == 0;
            }, 120000);
            // The run's busiest moment against an empty moment (after the one-shot ended): the sprites' own pixels.
            pv->setTime(pv->effect().duration >= 0 ? pv->effect().duration + 0.3 : 0.0);
            const QImage empty = pv->frame().convertToFormat(QImage::Format_RGB32);
            pv->setTime(pv->busiestTime());
            const QImage drawn = pv->frame().convertToFormat(QImage::Format_RGB32);
            int changed = 0, coloured = 0;
            if (empty.size() == drawn.size())
                for (int y = 0; y < drawn.height(); ++y) {
                    const auto* a = reinterpret_cast<const QRgb*>(empty.constScanLine(y));
                    const auto* b = reinterpret_cast<const QRgb*>(drawn.constScanLine(y));
                    for (int x = 0; x < drawn.width(); ++x) {
                        const int d = std::max({std::abs(qRed(a[x]) - qRed(b[x])), std::abs(qGreen(a[x]) - qGreen(b[x])), std::abs(qBlue(a[x]) - qBlue(b[x]))});
                        if (d < 24) continue;
                        ++changed;
                        coloured += std::max({qRed(b[x]), qGreen(b[x]), qBlue(b[x])}) - std::min({qRed(b[x]), qGreen(b[x]), qBlue(b[x])}) > 40;
                    }
                }
            check(pv->texturedParticles() > 0 && changed > 100 && coloured > 30,
                  QString("the preview draws the particles as their sprites (%1 drawn with images, %2 pixels changed, %3 coloured; "
                          "%4 material(s) still loading, missing: %5)")
                      .arg(pv->texturedParticles()).arg(changed).arg(coloured).arg(pv->materialsPending())
                      .arg(pv->missingMaterials().isEmpty() ? QString("none") : pv->missingMaterials().join("; ")));
        }
        eb->grab().save(QDir(outDir).filePath("effects_panel_preview.png"));
        eb->setFilter("");
    }

    section("side bar: sound preview");
    {
        // The FLAC decoder on frames written here (VERBATIM, and FIXED with a Rice-coded residual).
        std::vector<int16_t> s(192);
        for (size_t i = 0; i < s.size(); ++i) s[i] = int16_t(std::lround(9000 * std::sin(double(i) * 0.21)));
        for (const bool rice : {false, true}) {
            const QByteArray frame = flacFrame(s, rice);
            PcmSound pcm;
            QString err;
            const bool ok = decodeFlacFrames(reinterpret_cast<const uint8_t*>(frame.constData()), size_t(frame.size()), pcm, &err);
            check(ok && pcm.rate == 44100 && pcm.channels == 1 && pcm.samples == s,
                  QString("a %1 FLAC frame decodes to its samples (%2)").arg(rice ? "Rice-coded" : "verbatim", ok ? QString("ok") : err));
        }
        // The game's streamed bank (read in place, when the game is on this machine): its sounds decode whole.
        const QString sabs = "E:/IW8/Builds/1.20-replay/zone/common_mp.sabs";
        if (QFileInfo::exists(sabs)) {
            const std::vector<SabEntry> idx = readSabIndex(sabs);
            int good = 0, tried = 0;
            for (size_t i = 0; i < idx.size() && tried < 6; i += std::max<size_t>(1, idx.size() / 6), ++tried) {
                PcmSound pcm;
                if (readSabSound(sabs, idx[i], pcm) && pcm.frames() == int64_t(idx[i].frames) && pcm.rate == int(idx[i].rate)) ++good;
            }
            check(idx.size() > 100 && good == tried, QString("sounds of the game's streamed bank decode to their full length (%1 of %2; %3 entries)")
                                                         .arg(good).arg(tried).arg(idx.size()));
        }
        if (AssetBrowser* sb = w.soundsBrowser()) {
            w.showDock("Sounds");
            waitFor([&] { return sb->catalogLoaded(); }, 30000);
            sb->setListMode(AssetBrowser::ListMode::All);
            sb->setFilter("");
            QTest::qWait(20);
            auto* play = sb->findChild<QPushButton*>("soundPlay");
            sb->findChild<QListView*>()->setCurrentIndex({});
            QTest::qWait(20);
            const bool offFirst = play && !play->isEnabled();
            sb->selectRow(0);
            QTest::qWait(20);
            const bool onSelected = play && play->isEnabled() && play->text() == "Play";
            sb->setSoundStatus("Playing test", true);
            const bool stopShown = play && play->text() == "Stop" && sb->soundStatus() == "Playing test";
            sb->setSoundStatus("Stopped", false);
            check(offFirst && onSelected && stopShown, "the Sounds panel's Play button needs a selected alias and turns into Stop while one plays");
        }
    }

    section("side bar: Tools panel");
    w.showDock("Tools");  // a closed panel's widget is out of the window's tree
    QTest::qWait(50);
    if (auto* tp = w.findChild<ToolsPanel*>()) {
        check(tp->toolCount() >= 30 && tp->groups().size() >= 6,
              QString("the Tools panel lists the Tools menu's commands in groups (%1 in %2: %3)")
                  .arg(tp->toolCount()).arg(tp->groups().size()).arg(tp->groups().join(", ")));
        tp->setFilter("leak");
        const int leak = tp->visibleToolCount();
        tp->setFilter("");
        check(leak >= 1 && leak <= 3, QString("its search narrows the list (\"leak\": %1)").arg(leak));
        QAction* measure = w.action("MeasureTool");
        auto* btn = tp->findChild<QToolButton*>("tool_MeasureTool");
        if (measure && btn) {
            const bool was = measure->isChecked();
            btn->click();
            const bool on = measure->isChecked() != was && btn->isChecked() == measure->isChecked();
            btn->click();
            check(on && measure->isChecked() == was, "a Tools panel button runs the menu's command and shows its on / off state");
        } else {
            check(false, "the Tools panel has the Measure Tool");
        }
        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.effectsBrowser(), w.soundsBrowser()}) {
            auto* tb = b ? b->findChild<QToolButton*>("assetTools") : nullptr;
            check(tb && tb->menu() && !tb->menu()->actions().isEmpty(),
                  QString("an asset panel has a Tools button with its kind's commands (%1)").arg(tb && tb->menu() ? tb->menu()->actions().size() : 0));
        }
    } else {
        check(false, "the side bar has a Tools panel");
    }

    section("side bar: closed panels make their first rows ahead");
    if (AssetBrowser* mb = w.materialBrowser()) {
        // Another panel open, so Materials is closed; every picture forgotten (earlier checks made some): a search there
        // still gets its first rows' pictures (from the pack on the decoder, or made by the source), ready when the
        // panel opens.
        w.showDock("Tools");
        QTest::qWait(50);
        mb->setListMode(AssetBrowser::ListMode::All);
        mb->setKit("");
        mb->setFilter("me_curb_stone");
        QTest::qWait(20);
        mb->forgetThumbnails();
        const int want = std::min(4, mb->visibleCount());
        const int before = mb->picturedRows(want);
        waitFor([&] { return mb->picturedRows(want) == want; }, 60000);
        check(!mb->isVisible() && want > 0 && before == 0 && mb->picturedRows(want) == want,
              QString("a closed Materials panel makes the pictures of its first rows (%1 of %2 after forgetting them, %3 before)")
                  .arg(mb->picturedRows(want)).arg(want).arg(before));
        mb->setFilter("");
    }
}

}  // namespace iw8r::qt
