#include "qt_gui_test_sidebar.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <vector>

#include "asset_browser.h"
#include "main_window.h"
#include "numeric_drag.h"
#include "panel_chrome.h"
#include "settings.h"
#include "sound_preview.h"
#include "thumb_store.h"
#include "map_check_panel.h"
#include "menu_pack.h"
#include "panels.h"
#include "prefab_browser.h"
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
    section("number fields: drag to change");
    {
        // A field whose changes edit the map (as the panels' fields do): dragged sideways, it follows the mouse; past the
        // zone it keeps scrolling while held; back inside it stops; the whole drag is one undo step.
        EditorContext& ctx = w.context();
        const Id world = ctx.doc.map().entities.empty() ? kNoId : ctx.doc.map().entities.front().id;
        QWidget host;
        host.setAttribute(Qt::WA_DontShowOnScreen);
        auto* form = new QFormLayout(&host);
        auto* spin = new QDoubleSpinBox(&host);
        spin->setRange(-100000, 100000);
        spin->setSingleStep(2);
        form->addRow("Test amount", spin);
        host.resize(260, 40);
        host.show();
        QTest::qWait(30);
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &host, [&ctx, world](double v) {
            ctx.doc.setKeyValue(world, "iw8r_drag_test", QString::number(v).toStdString());
        });
        const size_t undoBefore = ctx.doc.undoPosition();
        QLineEdit* le = spin->findChild<QLineEdit*>();
        const QPoint at = le ? QPoint(le->width() / 2, le->height() / 2) : QPoint();
        const auto send = [&](QEvent::Type t, int dx, Qt::MouseButtons buttons) {
            if (!le) return;
            const QPoint local = at + QPoint(dx, 0);
            QMouseEvent ev(t, QPointF(local), QPointF(le->mapToGlobal(local)), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons,
                           Qt::NoModifier);
            QApplication::sendEvent(le, &ev);
        };
        const int s0 = QApplication::startDragDistance() + 2;  // past the drag distance: the drag starts here
        send(QEvent::MouseButtonPress, 0, Qt::LeftButton);
        send(QEvent::MouseMove, s0, Qt::LeftButton);
        send(QEvent::MouseMove, s0 + 40, Qt::LeftButton);  // 40 px = 10 steps of 2
        const double followed = spin->value();
        const bool started = numeric_drag::dragging();
        send(QEvent::MouseMove, s0 + numeric_drag::kZone + 80, Qt::LeftButton);  // past the zone
        const double atZone = spin->value();
        QTest::qWait(300);
        const double later = spin->value();
        QTest::qWait(200);
        const double later2 = spin->value();
        send(QEvent::MouseMove, s0 + 40, Qt::LeftButton);  // back inside: it follows the mouse again
        const double back = spin->value();
        QTest::qWait(200);
        const bool stopped = spin->value() == back;
        send(QEvent::MouseButtonRelease, s0 + 40, Qt::NoButton);
        QTest::qWait(20);
        const Entity* ws = ctx.doc.findEntity(world);
        const std::string* key = ws ? ws->value("iw8r_drag_test") : nullptr;
        const bool oneStep = ctx.doc.undoPosition() == undoBefore + 1 && key && QString::fromStdString(*key).toDouble() == spin->value();
        const QString stepName = ctx.doc.undoPosition() > 0 ? QString::fromStdString(ctx.doc.history()[ctx.doc.undoPosition() - 1].name) : QString();
        check(le && started && followed == 20 && atZone == 100 && later > atZone + 10 && later2 > later && back > followed && stopped &&
                  !numeric_drag::dragging(),
              QString("a number field follows a sideways drag (40 px: %1), keeps scrolling past %2 px (%3 -> %4 -> %5) and stops back inside (%6)")
                  .arg(followed).arg(numeric_drag::kZone).arg(atZone).arg(later).arg(later2).arg(back));
        check(oneStep && stepName == "Drag Test amount",
              QString("one drag is one undo step named after the field (\"%1\", %2 step(s))").arg(stepName).arg(ctx.doc.undoPosition() - undoBefore));
        while (ctx.doc.undoPosition() > undoBefore) ctx.doc.undo();
        ctx.documentEdited();
        // A click that does not move gives the caret (typing), not a drag.
        send(QEvent::MouseButtonPress, 0, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, 0, Qt::NoButton);
        check(!numeric_drag::dragging() && ctx.doc.undoPosition() == undoBefore, "a click on a number field without moving is a plain click");
    }

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
        for (const char* name : {"Materials", "Models", "Effects", "Sounds", "Images", "Map Check", "IW8 Spawns", "Undo History"}) {
            w.showDock(name);
            QTest::qWait(50);
            QWidget* panel = nullptr;
            if (QString(name) == "Materials") panel = w.materialBrowser();
            else if (QString(name) == "Models") panel = w.modelsBrowser();
            else if (QString(name) == "Effects") panel = w.effectsBrowser();
            else if (QString(name) == "Sounds") panel = w.soundsBrowser();
            else if (QString(name) == "Images") panel = w.imagesBrowser();
            else if (QString(name) == "Map Check") panel = w.findChild<MapCheckPanel*>();
            else if (QString(name) == "IW8 Spawns") panel = w.findChild<SpawnsPanel*>();
            else panel = w.findChild<HistoryPanel*>();
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

    section("side bar: menu packs");
    {
        // Owner 2026-11-16: thumbnails, previews and facts made once, compressed, shipped (menu_pack.h).
        const auto picture = [](int w, int h, QColor c) {
            QImage img(w, h, QImage::Format_RGB32);
            img.fill(c);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; x += 7) img.setPixel(x, y, qRgb((x * 3) & 255, (y * 5) & 255, 90));
            return img;
        };
        const QString path = QDir(outDir).filePath("test.iw8mp");
        QFile::remove(path);
        MenuPackWriter w("test", 80);
        const QByteArray json = QByteArray("{\"name\":\"vfx/test\"}").repeated(40);
        w.add("one", picture(64, 48, Qt::red), "loop", "3 emitters", picture(160, 120, Qt::red), json);
        w.add("two", picture(64, 48, Qt::red), "", "the same picture as one");
        w.add("three", picture(40, 64, Qt::blue), "1.5 s", "");
        w.add("four", {}, "", "no picture");
        MenuPackWriter::Stats st;
        QString err;
        const bool saved = w.save(path, &err, &st);
        MenuPack pack(path);
        const QImage one = pack.thumb("one"), three = pack.thumb("three"), pv = pack.preview("one");
        check(saved && pack.valid() && pack.count() == 4 && pack.thumbCount() == 3 && st.uniqueThumbs == 3 /* two thumbs + one preview */ &&
                  st.headers >= 1 && st.wholeJpegs == 0 && st.savedHeaderBytes > 500 && one.size() == QSize(64, 48) && three.size() == QSize(40, 64) &&
                  pv.size() == QSize(160, 120) && pack.preview("two").isNull() && pack.thumb("four").isNull() && pack.pill("one") == "loop" &&
                  pack.facts("two") == "the same picture as one" && pack.data("one") == json && pack.data("two").isEmpty() &&
                  qRed(one.pixel(3, 3)) > 200 && qBlue(three.pixel(3, 3)) > 200 && st.dataBytes < json.size() / 4,
              QString("a menu pack keeps thumbnails, previews, data and texts, one copy of identical pictures, without their JPEG headers "
                      "(%1 entries, %2 pictures stored, %3 header(s), %4 header bytes saved, data %5 -> %6 bytes) %7")
                  .arg(st.entries).arg(st.uniqueThumbs).arg(st.headers).arg(st.savedHeaderBytes).arg(json.size()).arg(st.dataBytes).arg(err));
        // The shipped packs open, and their pictures read (when the build has them).
        for (const char* kind : {"materials", "models", "images", "effects", "sounds", "prefabs"}) {
            const auto shipped = MenuPack::shared(kind);
            if (!shipped) continue;
            QString first;
            for (const QString& n : shipped->names())
                if (shipped->hasThumb(n)) {
                    first = n;
                    break;
                }
            check(shipped->valid() && shipped->kind() == kind && (first.isEmpty() || !shipped->thumb(first).isNull()),
                  QString("the shipped %1 pack reads (%2 entries, %3 pictures, %4 MB)").arg(kind).arg(shipped->count()).arg(shipped->thumbCount())
                      .arg(shipped->fileBytes() / 1048576.0, 0, 'f', 1));
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
        const QString first = eb->stripTitle();
        check(eb->stripShown() && first.startsWith("vfx/core/expl/spark_death") && caption,
              "selecting an effect puts it in the panel's details strip: " + first + " (" + eb->stripFacts() + ")");
        // Exported (now or before): the particle preview plays in the panel.
        waitFor([&] { return eb->effectPreview()->isVisible() || caption->text().contains("failed") || caption->text().contains("No export"); },
                120000);
        check(eb->effectPreview()->isVisible() && eb->effectPreview()->effect().emitters.size() > 0,
              "the selected effect plays in the side bar (" + caption->text().section('\n', 0, 0) + ")");
        // Owner 2026-11-15: "the preview should be square with the vfx fit in the middle centered", and the particles
        // drawn as their sprites ("a lot of vfx shows as faint orbs").
        if (VfxPreview* pv = eb->effectPreview(); pv && pv->isVisible()) {
            const QRect vr = pv->viewRect();
            // A square, as wide as the panel or as its height allows (2/5 of it: the list keeps its room), centred.
            check(vr.width() == vr.height() && vr.width() >= std::min(pv->width(), pv->maxSide()) - 1 && pv->height() > vr.height() &&
                      std::abs(vr.center().x() - pv->width() / 2) <= 1 && pv->maxSide() <= std::max(200, eb->height() * 2 / 5),
                  QString("the effect preview is a centred square (%1 x %2 in a %3 x %4 px preview; at most %5 in a %6 px high panel)")
                      .arg(vr.width()).arg(vr.height()).arg(pv->width()).arg(pv->height()).arg(pv->maxSide()).arg(eb->height()));
            // The whole strip shows, its Place button included: the panel is not taller than where it is shown (a dock's
            // scroll area grew with an earlier square and the strip scrolled off).
            if (auto* place = eb->findChild<QPushButton*>("effectPlace")) {
                QWidget* area = eb;
                for (QWidget* p = eb->parentWidget(); p; p = p->parentWidget())
                    if (auto* sa = qobject_cast<QAbstractScrollArea*>(p)) {
                        area = sa->viewport();
                        break;
                    }
                QTest::qWait(50);
                const int bottom = place->mapTo(area, QPoint(0, place->height())).y();
                check(place->isVisible() && bottom <= area->height() && eb->minimumSizeHint().height() <= area->height(),
                      QString("the effect strip fits where the panel is shown (Place fx_origin ends at %1 of %2 px; the panel needs %3)")
                          .arg(bottom).arg(area->height()).arg(eb->minimumSizeHint().height()));
            }
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
        {
            // Owner 2026-11-16 ("a lot of the vfx dont show anything"): a runner plays its child effect where its
            // particles are; models, lights and decals have their own looks (vfx_preview.cpp).
            const auto exportOf = [](const char* name, int element, double rate, const std::string& extra) {
                return std::string(R"({"format":"mw19-asset-json","root_type":"ParticleSystemDef","asset":{"fields":{"name":{"string":")") + name +
                       R"("},"emitterDefs":{"values":[{"flags":0,"particleSpawnRate":{"min":)" + std::to_string(rate) + R"(,"max":)" + std::to_string(rate) +
                       R"(},"particleLife":{"min":2,"max":2},"stateDefs":{"values":[{"elementType":{"value":)" + std::to_string(element) +
                       R"(},"moduleGroupDefs":{"values":[{"moduleDefs":{"values":[)" + extra + R"(]}}]}}]}}]}}}})";
            };
            VfxSimDef parent, child;
            const bool built =
                buildVfxSim(exportOf("vfx/test/parent", 9, 3,
                                     R"({"moduleData":{"active_member":"initRunner","value":{"":{"m_flags":0},"m_linkedAssetList":{"assetList":{"values":[{"value":{"asset_reference":true,"type":"ParticleSystemDef","name":"vfx/test/child"}}]}}}}})"),
                            parent) &&
                buildVfxSim(exportOf("vfx/test/child", 0, 20, ""), child);
            const VfxPreview::EffectSource was = VfxPreview::effectSourceFn();
            auto childDef = std::make_shared<const VfxSimDef>(child);
            VfxPreview::setEffectSource([childDef](const std::string& name, const std::string&, bool*) -> std::shared_ptr<const VfxSimDef> {
                return name == "vfx/test/child" ? childDef : nullptr;
            });
            VfxPreview pv;
            pv.setAttribute(Qt::WA_DontShowOnScreen);
            pv.resize(300, 400);
            pv.show();
            pv.setEffect(parent);
            pv.setTime(1.5);
            const int children = pv.childEffects(), particles = pv.particlesNow();
            VfxPreview::setEffectSource(was);
            check(built && children == 1 && particles > 10 && pv.effect().emitters.size() == 1,
                  QString("a runner plays its child effect in the preview (%1 child effect(s), %2 particles at 1.5 s)").arg(children).arg(particles));
        }
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

    section("side bar: the tools in their panels");
    {
        // Owner 2026-11-15: no separate Tools side panel (the Tools menu stays in the menu bar); each panel's dots menu
        // holds the tools that belong to it.
        check(!w.findChild<QWidget*>("toolsPanel") && w.action("MeasureTool") && w.action("CheckMap") && w.action("BuildMenuCache"),
              "there is no Tools side panel, and the Tools menu still has its commands");
        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.effectsBrowser(), w.soundsBrowser(), w.imagesBrowser()}) {
            auto* tb = b ? b->findChild<QToolButton*>("assetTools") : nullptr;
            bool all = tb && tb->menu() && !b->toolActions().isEmpty();
            if (all)
                for (QAction* a : b->toolActions()) all &= tb->menu()->actions().contains(a);
            check(all, QString("an asset panel's dots menu holds its kind's tools (%1)").arg(b ? b->toolActions().size() : 0));
        }
        const auto hasTool = [](const QList<QAction*>& tools, const char* command) {
            for (QAction* a : tools)
                if (a->objectName() == command) return true;
            return false;
        };
        w.showDock("IW8 Spawns");
        QTest::qWait(30);
        auto* sp = w.findChild<SpawnsPanel*>();
        w.showDock("Undo History");
        QTest::qWait(30);
        auto* hp = w.findChild<HistoryPanel*>();
        w.showDock("Map Check");
        QTest::qWait(30);
        auto* mc = w.findChild<MapCheckPanel*>();
        check(sp && hasTool(sp->tools(), "GenerateSpawns") && hasTool(sp->tools(), "GameModes") && hp && hasTool(hp->tools(), "RecordMacro") &&
                  mc && hasTool(mc->tools(), "LeakCheck") && hasTool(mc->tools(), "CheckMap"),
              "IW8 Spawns has Generate Spawns and Game Modes, Undo History the macros, Map Check the map's checks");
    }

    section("side bar: IW8 Spawns");
    if (auto* sp = (w.showDock("IW8 Spawns"), QTest::qWait(30), w.findChild<SpawnsPanel*>())) {
        EditorContext& ctx = w.context();
        const size_t undo0 = ctx.doc.undoPosition();
        const int before = sp->spawnCount();
        ctx.doc.addEntity({{"classname", "mp_tdm_spawn_allies_start"}, {"origin", "100 200 0"}, {"angles", "0 90 0"}}, {}, "test spawn");
        ctx.doc.addEntity({{"classname", "mp_tdm_spawn"}, {"origin", "300 200 0"}}, {}, "test spawn");
        ctx.doc.addEntity({{"classname", "mp_dom_spawn"}, {"origin", "300 -200 0"}}, {}, "test spawn");
        ctx.documentEdited();
        QTest::qWait(30);
        sp->setMode("tdm");
        const int tdm = sp->visibleCount();
        const Vec3 cam = ctx.cam.pos;
        sp->selectRow(0);
        QTest::qWait(30);
        const QString title = sp->stripTitle();
        check(sp->spawnCount() == before + 3 && sp->modes().contains("tdm") && sp->modes().contains("dom") && tdm >= 2 &&
                  title.startsWith("mp_tdm_spawn") && sp->stripFacts().contains("TDM") && sp->countText().contains(" of "),
              QString("IW8 Spawns lists the map's spawns by mode (%1 spawns, modes %2; TDM %3; strip \"%4\": %5)")
                  .arg(sp->spawnCount()).arg(sp->modes().join(" ")).arg(tdm).arg(title, sp->stripFacts()));
        check(ctx.doc.selection().size() == 1 && (cam.x != ctx.cam.pos.x || cam.y != ctx.cam.pos.y || cam.z != ctx.cam.pos.z),
              "a spawn picked in the list is selected and the views go to it");
        const Id added = sp->addHere();
        QTest::qWait(30);
        const Entity* e = ctx.doc.findEntity(added);
        check(e && e->classname() == title.toStdString() && sp->spawnCount() == before + 4,
              "Add one here adds another spawn of the picked kind at the camera");
        sp->setMode("");
        while (ctx.doc.undoPosition() > undo0) ctx.doc.undo();
        ctx.documentEdited();
        QTest::qWait(30);
        check(sp->spawnCount() == before, "the spawns list follows undo");
    }

    section("side bar: Undo History");
    if (auto* hp = (w.showDock("Undo History"), QTest::qWait(30), w.findChild<HistoryPanel*>())) {
        EditorContext& ctx = w.context();
        const size_t undo0 = ctx.doc.undoPosition();
        ctx.doc.addEntity({{"classname", "info_null"}, {"origin", "0 0 64"}}, {}, "Place info_null");
        const Id world = ctx.doc.map().entities.front().id;
        ctx.doc.setKeyValue(world, "iw8r_history_test", "1");
        ctx.documentEdited();
        QTest::qWait(30);
        const QString placed = hp->summaryOf(undo0 + 1), keyed = hp->summaryOf(undo0 + 2);
        hp->setFilter("info_null");
        const int found = hp->visibleSteps();
        hp->setFilter("");
        check(placed.contains("1 entity added") && keyed.contains("key") && found == 2 && !hp->stripTitle().isEmpty() &&
                  hp->countText().contains("step"),
              QString("Undo History says what each step touched (\"%1\", \"%2\"), searches the steps (%3) and shows the current one (\"%4\", %5)")
                  .arg(placed, keyed).arg(found).arg(hp->stripTitle(), hp->countText()));
        while (ctx.doc.undoPosition() > undo0) ctx.doc.undo();
        ctx.documentEdited();
    }

    section("side bar: Map Check");
    if (auto* mc = (w.showDock("Map Check"), QTest::qWait(30), w.findChild<MapCheckPanel*>())) {
        mc->recheck();
        const int total = mc->findingCount();
        int build = 0;
        for (const QString& l : mc->lines()) build += MapCheckPanel::kindOf(l) == "build";
        mc->setChip("build");
        const int shown = mc->visibleCount();
        mc->selectRow(0);
        for (int r = 0; r < total && !mc->stripTitle().startsWith("Build"); ++r) mc->selectRow(r);
        const QString strip = mc->stripTitle();
        mc->setChip("all");
        check(total > 0 && shown == build && strip.startsWith("Build") && !mc->countText().isEmpty() &&
                  MapCheckPanel::severityOf("Lights (warning): x") == "warning" && MapCheckPanel::kindOf("Spawns (warning): x") == "gameplay",
              QString("Map Check lists the map's findings by kind (%1 findings, %2 build notes shown; strip \"%3\"; count %4)")
                  .arg(total).arg(shown).arg(strip, mc->countText()));
    }

    section("side bar: Prefabs");
    if (PrefabBrowser* pb = (w.showDock("Prefabs"), QTest::qWait(50), w.prefabBrowser())) {
        if (pb->count() == 0) pb->refresh();
        pb->setCategory("doors");
        int doors = 0, other = 0;
        for (int r = 0; r < pb->count(); ++r)
            if (pb->shownAt(r)) (pb->categoryAt(r) == "doors" ? doors : other)++;
        int first = -1;
        for (int r = 0; r < pb->count() && first < 0; ++r)
            if (pb->shownAt(r)) first = r;
        pb->selectRow(first);
        QTest::qWait(30);
        const bool strip = pb->stripShown() && pb->stripTitle() == pb->titleAt(first) && pb->stripFacts().startsWith("Doors") &&
                           pb->findChild<QPushButton*>("prefabOpen") && pb->findChild<QPushButton*>("prefabPlace");
        pb->setCategory("");
        check(doors > 20 && other == 0 && strip,
              QString("the Prefabs chips list a category (Doors: %1, %2 others) and the strip shows the pick (\"%3\": %4)")
                  .arg(doors).arg(other).arg(pb->stripTitle(), pb->stripFacts()));
    }

    section("side bar: flush asset panels");
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
            // Owner 2026-11-15: the filter is a dropdown list (a square box across the panel), not a row of buttons.
            auto* bar = b->findChild<ChipBar*>();
            QPushButton* drop = bar ? bar->button() : nullptr;
            const int entries = drop && drop->menu() ? int(drop->menu()->actions().size()) : 0;
            check(drop && drop->menu() && entries >= 2 && !bar->findChild<QToolButton*>() && drop->styleSheet().contains("border-radius: 0px") &&
                      !bar->shownText().isEmpty(),
                  QString("its filter is one dropdown (\"%1\", %2 entries)").arg(bar ? bar->shownText() : QString()).arg(entries));
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
            auto* loops = sb->findChild<QAction*>("chip_loops");
            if (loops) loops->trigger();
            QTest::qWait(20);
            auto* view = sb->findChild<QListView*>();
            int looping = 0, other = 0;
            for (int r = 0; r < sb->visibleCount() && view; ++r) (view->model()->index(r, 0).data(Qt::UserRole + 2).toBool() ? looping : other)++;
            check(loops && looping > 50 && other == 0 && sb->visibleCount() < total,
                  QString("the Sounds Loops chip lists looping aliases only (%1 loops, %2 others of %3)").arg(looping).arg(other).arg(total));
            const QString shown = sb->findChild<ChipBar*>() ? sb->findChild<ChipBar*>()->shownText() : QString();
            check(shown.endsWith("Loops"), "the dropdown names the switches that are on (" + shown + ")");
            if (loops) loops->trigger();
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
        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser()}) {
            if (!b) continue;
            const bool mat = b == w.materialBrowser();
            w.showDock(mat ? "Materials" : "Models");
            QTest::qWait(30);
            const QString chipId = mat ? "walls" : "foliage";
            b->setChip(chipId);
            QTest::qWait(20);
            int in = 0, wrong = 0;
            for (int r = 0; r < std::min(b->visibleCount(), 300); ++r) (b->groupOf(b->visibleName(r)) == chipId ? in : wrong)++;
            b->selectRow(0);
            QTest::qWait(50);
            const bool strip = b->stripShown() && b->stripTitle() == b->visibleName(0) && b->stripFacts().contains("this map") &&
                               b->findChild<QPushButton*>(mat ? "materialApply" : "modelPlace");
            check(in > 10 && wrong == 0 && strip,
                  QString("the %1 chips narrow by type (%2: %3, %4 wrong) and the strip shows the pick with its actions (\"%5\": %6)")
                      .arg(mat ? "Materials" : "Models", chipId).arg(in).arg(wrong).arg(b->stripTitle(), b->stripFacts()));
            b->findChild<QListView*>()->setCurrentIndex({});
            b->setChip("all");
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

    section("side bar: closed panels make their first rows ahead");
    if (AssetBrowser* mb = w.materialBrowser()) {
        // Another panel open, so Materials is closed; every picture forgotten (earlier checks made some): a search there
        // still gets its first rows' pictures (from the pack on the decoder, or made by the source), ready when the
        // panel opens.
        w.showDock("Map Check");
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
