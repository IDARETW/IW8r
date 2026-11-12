#include "qt_gui_test_assets_b.h"

#include <cmath>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>

#include "sound_settings.h"
#include "sound_settings_dialog.h"
#include "techset_dialog.h"
#include "vision_editor_dialog.h"
#include "vision_fields.h"

namespace iw8r::qt {

namespace {

constexpr int kValueColumn = 3;

void putU32(QByteArray& b, quint32 v) {
    for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff));
}

void putU16(QByteArray& b, quint16 v) {
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
}

// A valid 16-bit mono 44100 Hz WAV: a 44-byte header and 100 zero samples.
QByteArray tinyWav() {
    const quint32 dataBytes = 100 * 2;
    QByteArray b;
    b.append("RIFF", 4);
    putU32(b, 36 + dataBytes);
    b.append("WAVE", 4);
    b.append("fmt ", 4);
    putU32(b, 16);
    putU16(b, 1);       // PCM
    putU16(b, 1);       // mono
    putU32(b, 44100);   // sample rate
    putU32(b, 44100 * 2);  // byte rate
    putU16(b, 2);       // block align
    putU16(b, 16);      // bits per sample
    b.append("data", 4);
    putU32(b, dataBytes);
    b.append(QByteArray(int(dataBytes), '\0'));
    return b;
}

bool writeBytes(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

// Walks up from the executable folder (at most 5 levels) to find the technique set sample export.
QString findTechsetSample() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int level = 0; level <= 5; ++level) {
        const QString candidate = dir.filePath("tests/data/techset_sample.asset.json");
        if (QFile::exists(candidate)) return QDir::cleanPath(candidate);
        if (!dir.cdUp()) break;
    }
    return QString();
}

// Row of the vision table whose Field column is `name`, or -1.
int visionRowOf(QTableWidget* table, const QString& name) {
    if (!table) return -1;
    for (int r = 0; r < table->rowCount(); ++r) {
        auto* it = table->item(r, 0);
        if (it && it->text() == name) return r;
    }
    return -1;
}

int visibleRows(QTableWidget* table) {
    int n = 0;
    for (int r = 0; r < table->rowCount(); ++r) n += table->isRowHidden(r) ? 0 : 1;
    return n;
}

void checkSound(const std::function<void(const QString&)>& section, const std::function<void(bool, const QString&)>& check,
                const QString& outDir) {
    section("sound settings: Save writes the sidecar, unset everything removes it");
    const QString sndDir = QDir(outDir).filePath("snd_test");
    QDir().mkpath(sndDir);
    const QString wav = QDir(sndDir).filePath("beep.wav");
    const QString sidecar = QDir(sndDir).filePath("beep.iw8snd.json");
    QFile::remove(sidecar);  // stale file from an earlier run
    check(writeBytes(wav, tinyWav()), "wrote a 44-byte-header WAV with 100 samples: " + wav);

    {
        QDialog* dlg = createSoundSettingsDialog(nullptr, wav);
        dlg->show();
        auto* volSet = dlg->findChild<QCheckBox*>("soundVolumeSet");
        auto* vol = dlg->findChild<QSlider*>("soundVolume");
        auto* loopSet = dlg->findChild<QCheckBox*>("soundLoopSet");
        auto* loop = dlg->findChild<QCheckBox*>("soundLoop");
        auto* save = dlg->findChild<QPushButton*>("soundSave");
        check(volSet && vol && loopSet && loop && save, "sound dialog has its volume, loop and Save controls");
        if (volSet) volSet->setChecked(true);
        if (vol) vol->setValue(50);
        if (loopSet) loopSet->setChecked(true);
        if (loop) loop->setChecked(true);
        if (save) save->click();
        check(QFile::exists(sidecar), "Save wrote the sidecar " + sidecar);
        SoundSettings s;
        std::string err;
        const bool ok = loadSoundSettingsFor(wav.toStdString(), s, &err);
        check(ok && s.volume.has_value() && std::abs(*s.volume - 0.5) < 0.01,
              QString("sidecar volume is 0.5 (got %1)").arg(s.volume ? *s.volume : -1.0));
        check(ok && s.loop.has_value() && *s.loop, "sidecar loop is true");
        delete dlg;
    }

    {
        QDialog* dlg = createSoundSettingsDialog(nullptr, wav);
        dlg->show();
        auto* volSet = dlg->findChild<QCheckBox*>("soundVolumeSet");
        check(volSet && volSet->isChecked(), "reopening shows the saved volume as set");
        for (const char* name : {"soundVolumeSet", "soundMinSet", "soundMaxSet", "soundLoopSet", "soundTwoDSet"}) {
            if (auto* box = dlg->findChild<QCheckBox*>(name)) box->setChecked(false);
        }
        auto* save = dlg->findChild<QPushButton*>("soundSave");
        if (save) save->click();
        check(!QFile::exists(sidecar), "Save with nothing set removed the sidecar");
        delete dlg;
    }
}

void checkVision(const std::function<void(const QString&)>& section, const std::function<void(bool, const QString&)>& check) {
    section("vision editor: values, result, filter and invalid input");
    QDialog* dlg = createVisionEditorDialog(nullptr, "Vision editor test", QString());
    dlg->show();
    auto* table = dlg->findChild<QTableWidget*>("visionTable");
    auto* filter = dlg->findChild<QLineEdit*>("visionFilter");
    auto* ok = dlg->findChild<QPushButton*>("visionOk");
    const int rows = table ? table->rowCount() : 0;
    check(rows > 150, QString("vision table has %1 rows (more than 150)").arg(rows));
    check(table && filter && ok, "vision editor has its table, filter and OK button");
    if (!table || !filter || !ok) {
        delete dlg;
        return;
    }

    // First field with a non-empty default.
    const std::vector<VisionField>& fields = visionFields();
    const VisionField* withDefault = nullptr;
    const VisionField* firstScalar = nullptr;
    for (const VisionField& f : fields) {
        if (!withDefault && f.defaultText[0] != '\0') withDefault = &f;
        if (!firstScalar && std::string(f.type) == "scalar") firstScalar = &f;
    }
    check(withDefault != nullptr, "a vision field with a stock default exists");
    if (withDefault) {
        const QString name = QString::fromUtf8(withDefault->name);
        const QString value = QString::fromUtf8(withDefault->defaultText);
        const int row = visionRowOf(table, name);
        check(row >= 0, "row for " + name + " found");
        if (row >= 0) table->item(row, kValueColumn)->setText(value);
        check(visionEditorResult(dlg) == name + "=" + value,
              "result is \"" + name + "=" + value + "\" (got \"" + visionEditorResult(dlg) + "\")");
        check(ok->isEnabled(), "OK is enabled for a valid value");
    }

    const int visibleBefore = visibleRows(table);
    QTest::keyClicks(filter, "fog");
    const int visibleAfter = visibleRows(table);
    check(visibleAfter > 0 && visibleAfter < visibleBefore,
          QString("typing fog shows fewer rows (%1 before, %2 after)").arg(visibleBefore).arg(visibleAfter));

    if (firstScalar) {
        const int row = visionRowOf(table, QString::fromUtf8(firstScalar->name));
        if (row >= 0) {
            table->item(row, kValueColumn)->setText("abc");
            check(!ok->isEnabled(), "invalid scalar 'abc' disables OK");
            check(visionEditorResult(dlg).isEmpty(), "invalid value gives no result text (OK is refused)");
            table->item(row, kValueColumn)->setText(QString());
            check(ok->isEnabled(), "clearing the invalid value enables OK again");
        } else {
            check(false, "row for the first scalar field found");
        }
    }
    dlg->close();
    delete dlg;
}

void checkTechset(const std::function<void(const QString&)>& section, const std::function<void(bool, const QString&)>& check) {
    section("technique set dialog: tree from the export, not-exported line without one");
    const QString sample = findTechsetSample();
    check(!sample.isEmpty(), "found tests/data/techset_sample.asset.json: " + sample);
    if (!sample.isEmpty()) {
        QDialog* dlg = createTechsetDialog(nullptr, "test_material", "mo/lit_3_test", sample);
        auto* tree = dlg->findChild<QTreeWidget*>("techsetTree");
        const int techniques = tree ? tree->topLevelItemCount() : 0;
        check(techniques >= 10, QString("technique tree lists %1 techniques (at least 10)").arg(techniques));
        check(dlg->findChild<QLabel*>("techsetNotExported") == nullptr, "an exported body shows no not-exported line");
        dlg->close();
        delete dlg;
    }

    QDialog* empty = createTechsetDialog(nullptr, "test_material", "mo/lit_3_test", QString());
    check(empty->findChild<QLabel*>("techsetNotExported") != nullptr, "an empty path shows the not-exported line");
    empty->close();
    delete empty;
}

}  // namespace

void runAssetDialogChecksB(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, const QString& outDir) {
    QDir().mkpath(outDir);
    checkSound(section, check, outDir);
    checkVision(section, check);
    checkTechset(section, check);
}

}  // namespace iw8r::qt
