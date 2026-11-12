#include "sound_settings_dialog.h"

#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include "custom_sounds.h"
#include "map_text.h"
#include "sound_settings.h"

namespace iw8r::qt {

namespace fs = std::filesystem;

namespace {

// One setting: a "set" checkbox, its control and a grey hint with the build's default (shown while unset).
struct Row {
    QCheckBox* set = nullptr;
    QWidget* control = nullptr;
    QLabel* hint = nullptr;
};

// A control is usable only while its "set" box is ticked; the hint shows while it is not.
void syncRow(const Row& r) {
    const bool on = r.set->isChecked();
    r.control->setEnabled(on);
    r.hint->setVisible(!on);
}

Row makeRow(QWidget* parent, QGridLayout* grid, int line, const QString& title, QWidget* control, const QString& defaultText) {
    Row r;
    r.set = new QCheckBox("set", parent);
    r.control = control;
    r.hint = new QLabel("Build default: " + defaultText, parent);
    r.hint->setStyleSheet("color: #888888;");
    grid->addWidget(new QLabel(title, parent), line, 0);
    grid->addWidget(r.set, line, 1);
    grid->addWidget(control, line, 2);
    grid->addWidget(r.hint, line + 1, 2);
    return r;
}

const char* const kChangedProp = "iw8rSidecarChanged";

}  // namespace

QDialog* createSoundSettingsDialog(QWidget* parent, const QString& wavPath) {
    const std::string wav = wavPath.toStdString();
    const std::string sidecar = soundSettingsPathFor(wav);
    const CustomSoundAlias defaults;  // the build's defaults for a map sound

    // Existing sidecar, if any. An invalid one is shown as an error and the dialog starts with everything unset.
    SoundSettings cur;
    std::string loadErr;
    const bool loaded = loadSoundSettingsFor(wav, cur, &loadErr);

    auto* dlg = new QDialog(parent);
    dlg->setObjectName("soundSettingsDialog");
    dlg->setWindowTitle("Sound Settings: " + QFileInfo(wavPath).fileName());
    auto* top = new QVBoxLayout(dlg);

    auto* errLabel = new QLabel(dlg);
    errLabel->setObjectName("soundError");
    errLabel->setStyleSheet("color: #c00000;");
    errLabel->setWordWrap(true);
    const auto showError = [errLabel](const QString& text) {
        errLabel->setText(text);
        errLabel->setVisible(!text.isEmpty());
    };
    showError(loaded ? QString() : "Sidecar is invalid: " + QString::fromStdString(loadErr));
    top->addWidget(errLabel);

    auto* grid = new QGridLayout();
    top->addLayout(grid);

    // Volume: 0..100 % in the dialog, stored as 0..1.
    auto* volBox = new QWidget(dlg);
    auto* volH = new QHBoxLayout(volBox);
    volH->setContentsMargins(0, 0, 0, 0);
    auto* volSlider = new QSlider(Qt::Horizontal, volBox);
    volSlider->setObjectName("soundVolume");
    volSlider->setRange(0, 100);
    auto* volText = new QLabel(volBox);
    volText->setObjectName("soundVolumeText");
    volText->setMinimumWidth(48);
    volH->addWidget(volSlider, 1);
    volH->addWidget(volText);
    const int volDefaultPct = static_cast<int>(std::lround(defaults.volume * 100.0));
    volSlider->setValue(cur.volume ? static_cast<int>(std::lround(*cur.volume * 100.0)) : volDefaultPct);
    volText->setText(QString::number(volSlider->value()) + " %");
    QObject::connect(volSlider, &QSlider::valueChanged, volText,
                     [volText](int v) { volText->setText(QString::number(v) + " %"); });
    const Row rVol = makeRow(dlg, grid, 0, "Volume", volBox, QString::number(volDefaultPct) + " %");
    rVol.set->setObjectName("soundVolumeSet");

    // Distances in map units.
    auto* minSpin = new QSpinBox(dlg);
    minSpin->setObjectName("soundMin");
    minSpin->setRange(0, 100000);
    minSpin->setValue(static_cast<int>(std::lround(cur.distMin.value_or(defaults.distMin))));
    const Row rMin = makeRow(dlg, grid, 2, "Min distance", minSpin, QString::number(std::lround(defaults.distMin)));
    rMin.set->setObjectName("soundMinSet");

    auto* maxSpin = new QSpinBox(dlg);
    maxSpin->setObjectName("soundMax");
    maxSpin->setRange(0, 100000);
    maxSpin->setValue(static_cast<int>(std::lround(cur.distMax.value_or(defaults.distMax))));
    const Row rMax = makeRow(dlg, grid, 4, "Max distance", maxSpin, QString::number(std::lround(defaults.distMax)));
    rMax.set->setObjectName("soundMaxSet");

    auto* loopCheck = new QCheckBox("Loop", dlg);
    loopCheck->setObjectName("soundLoop");
    loopCheck->setChecked(cur.loop.value_or(defaults.looping));
    const Row rLoop = makeRow(dlg, grid, 6, "Loop", loopCheck, defaults.looping ? "on" : "off");
    rLoop.set->setObjectName("soundLoopSet");

    auto* twoDCheck = new QCheckBox("2D (heard everywhere)", dlg);
    twoDCheck->setObjectName("soundTwoD");
    twoDCheck->setChecked(cur.twoD.value_or(defaults.ambient2d));
    const Row rTwoD = makeRow(dlg, grid, 8, "2D", twoDCheck, defaults.ambient2d ? "on" : "off");
    rTwoD.set->setObjectName("soundTwoDSet");

    // A setting is set when the sidecar holds it. Unset settings start greyed out.
    const Row rows[] = {rVol, rMin, rMax, rLoop, rTwoD};
    const bool isSet[] = {cur.volume.has_value(), cur.distMin.has_value(), cur.distMax.has_value(),
                          cur.loop.has_value(), cur.twoD.has_value()};
    for (size_t i = 0; i < std::size(rows); ++i) {
        rows[i].set->setChecked(isSet[i]);
        syncRow(rows[i]);
        QObject::connect(rows[i].set, &QCheckBox::toggled, dlg, [r = rows[i]](bool) { syncRow(r); });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dlg);
    buttons->setObjectName("soundButtons");
    buttons->button(QDialogButtonBox::Save)->setObjectName("soundSave");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("soundCancel");
    top->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, [=]() {
        SoundSettings out;
        if (rVol.set->isChecked()) out.volume = volSlider->value() / 100.0;
        if (rMin.set->isChecked()) out.distMin = minSpin->value();
        if (rMax.set->isChecked()) out.distMax = maxSpin->value();
        if (rLoop.set->isChecked()) out.loop = loopCheck->isChecked();
        if (rTwoD.set->isChecked()) out.twoD = twoDCheck->isChecked();

        if (out.distMin && out.distMax && *out.distMax < *out.distMin) {
            showError("Max distance must not be below min distance.");
            return;
        }

        bool changed = false;
        std::error_code ec;
        const bool nothingSet = !out.volume && !out.distMin && !out.distMax && !out.loop && !out.twoD;
        if (nothingSet) {
            // Nothing left to override: the sidecar goes, so the build uses its defaults.
            if (fs::exists(sidecar, ec)) {
                if (!fs::remove(sidecar, ec) || ec) {
                    showError("Could not delete " + QString::fromStdString(sidecar));
                    return;
                }
                changed = true;
            }
        } else {
            std::string werr;
            if (!writeFileAtomic(sidecar, soundSettingsJson(out), &werr)) {
                showError("Could not write " + QString::fromStdString(sidecar) + "\n" + QString::fromStdString(werr));
                return;
            }
            changed = true;
        }
        dlg->setProperty(kChangedProp, changed);
        dlg->accept();
    });

    return dlg;
}

bool showSoundSettingsDialog(QWidget* parent, const QString& wavPath) {
    QDialog* dlg = createSoundSettingsDialog(parent, wavPath);
    dlg->exec();
    const bool changed = dlg->property(kChangedProp).toBool();
    delete dlg;
    return changed;
}

}  // namespace iw8r::qt
