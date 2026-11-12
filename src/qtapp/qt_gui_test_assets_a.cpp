// GUI checks for the Image Adjust and Asset Usage dialogs. See qt_gui_test_assets_a.h.
#include "qt_gui_test_assets_a.h"

#include "asset_usage_dialog.h"
#include "image_adjust.h"
#include "image_adjust_dialog.h"

#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>

namespace iw8r::qt {

void runAssetDialogChecksA(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check,
                           const iw8r::MapFile& map, const QString& outDir) {
    QDir().mkpath(outDir);
    const QDir dir(outDir);

    // A 64x64 grey ramp: left pixels dark, right pixels bright.
    const QString rampPath = dir.filePath("asset_checks_ramp.png");
    {
        QImage ramp(64, 64, QImage::Format_RGB32);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const int v = x * 4;
                ramp.setPixel(x, y, qRgb(v, v, v));
            }
        check(ramp.save(rampPath, "PNG"), "test PNG (64x64 grey ramp) written to " + rampPath);
    }

    section("image adjust: headless invert and resize");
    {
        ImageAdjust inv;
        inv.invert = true;
        const QString outPath = dir.filePath("asset_checks_ramp_inverted.png");
        QString err;
        const bool ok = runImageAdjustHeadless(rampPath, inv, 32, 16, false, 1.0, outPath, &err);
        check(ok, "runImageAdjustHeadless returns true (" + (err.isEmpty() ? QString("no error") : err) + ")");
        const QImage out(outPath);
        check(!out.isNull() && out.width() == 32 && out.height() == 16,
              QString("output is 32x16 (got %1x%2)").arg(out.width()).arg(out.height()));
        check(out.width() == 32 && out.height() == 16 && qGray(out.pixel(0, 8)) > qGray(out.pixel(31, 8)),
              "inverted ramp is reversed: left pixel brighter than right");
    }

    section("image adjust: headless normal map from a flat grey");
    {
        QImage flat(64, 64, QImage::Format_RGB32);
        flat.fill(qRgb(100, 100, 100));
        const QString flatPath = dir.filePath("asset_checks_flat.png");
        check(flat.save(flatPath, "PNG"), "flat grey test PNG written");
        const QString normalPath = dir.filePath("asset_checks_normal.png");
        QString err;
        const bool ok = runImageAdjustHeadless(flatPath, ImageAdjust{}, 0, 0, true, 1.0, normalPath, &err);
        check(ok, "normal map from flat grey returns true (" + (err.isEmpty() ? QString("no error") : err) + ")");
        const QImage nm(normalPath);
        bool flatOk = false;
        QColor c;
        if (!nm.isNull() && nm.width() > 32 && nm.height() > 32) {
            c = nm.pixelColor(32, 32);
            flatOk = qAbs(c.red() - 128) <= 2 && qAbs(c.green() - 128) <= 2 && qAbs(c.blue() - 255) <= 2;
        }
        check(flatOk, QString("centre pixel of a flat normal map is about (128, 128, 255) (got %1, %2, %3)")
                          .arg(c.red()).arg(c.green()).arg(c.blue()));
    }

    section("image adjust dialog (non-modal)");
    {
        QDialog* d = createImageAdjustDialog(nullptr, rampPath);
        check(d != nullptr, "createImageAdjustDialog builds the dialog for the test PNG");
        if (d) {
            d->show();
            QTest::qWait(100);
            QSlider* bright = d->findChild<QSlider*>("brightness");
            check(bright != nullptr, "brightness slider found by its objectName");
            if (bright) {
                bright->setValue(50);
                QTest::qWait(100);
                check(bright->value() == 50, "brightness slider moved to 50");
            }
            const QLabel* preview = d->findChild<QLabel*>("previewLabel");
            check(preview && !preview->pixmap().isNull(),
                  "preview label has a pixmap after moving brightness");
            d->close();
            delete d;
        }
    }

    section("asset usage dialog (non-modal)");
    {
        QDialog* d = createAssetUsageDialog(nullptr, map, QStringList(), QStringList(), QStringList(), QStringList());
        check(d != nullptr, "createAssetUsageDialog builds the dialog for the map");
        if (d) {
            d->show();
            QTest::qWait(100);
            const QTabWidget* tabs = d->findChild<QTabWidget*>("assetUsageTabs");
            check(tabs && tabs->count() == 6, QString("the tab widget has 6 tabs (got %1)").arg(tabs ? tabs->count() : -1));
            const QTableWidget* mats = d->findChild<QTableWidget*>("materialsTable");
            check(mats && mats->rowCount() >= 1, QString("the Materials table has rows (got %1)").arg(mats ? mats->rowCount() : -1));
            if (mats && mats->rowCount() >= 1) {
                const QTableWidgetItem* uses = mats->item(0, 1);
                const qlonglong n = uses ? uses->data(Qt::DisplayRole).toLongLong() : 0;
                check(n > 0, QString("the first Materials row has a positive Uses count (got %1)").arg(n));
            }
            QPushButton* copy = d->findChild<QPushButton*>("copyAsTextButton");
            check(copy != nullptr, "the Copy as text button found by its objectName");
            QClipboard* cb = QGuiApplication::clipboard();
            if (cb) cb->clear();
            if (copy) copy->click();
            const QString text = cb ? cb->text() : QString();
            check(!text.isEmpty(), QString("Copy as text puts the table on the clipboard (%1 chars)").arg(text.size()));
            d->close();
            delete d;
        }
    }
}

}  // namespace iw8r::qt
