import io

# Atmosphere dialog: the game's fog and volumetric air (worldspawn iw8r_haze): a preset list with a short text for
# each and an amount. The old baked fog group says that the haze replaces it.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/atmosphere_dialog.h'
patch(H, '''class QCheckBox;
''', '''class QCheckBox;
class QComboBox;
class QLabel;
''')
patch(H, '''private:
    QGroupBox* fogBox_ = nullptr;''', '''private:
    // The game's own fog and volumetric air (iw8r_haze): the preset ("" = none) and its amount.
    QComboBox* haze_ = nullptr;
    QDoubleSpinBox* hazeAmount_ = nullptr;
    QLabel* hazeAbout_ = nullptr;
    QGroupBox* fogBox_ = nullptr;''')
C = 'src/qtapp/atmosphere_dialog.cpp'
patch(C, '''#include <QCheckBox>
''', '''#include <QCheckBox>
#include <QComboBox>
''')
patch(C, '''    const ExpFog fog = a.fog.value_or(ExpFog{});
    fogBox_ = new QGroupBox("Exponential fog (setExpFog)", this);''', '''    // The game's own fog, its blend into the sky and the volumetric air (research 126).
    auto* hazeBox = new QGroupBox("Haze: the game's fog, sky blend and volumetric air", this);
    hazeBox->setObjectName("atmoHaze");
    auto* hf = new QFormLayout(hazeBox);
    haze_ = new QComboBox(hazeBox);
    haze_->setObjectName("atmoHazePreset");
    haze_->addItem("None", QString());
    for (const HazePreset& p : hazePresets()) haze_->addItem(p.label, QString::fromLatin1(p.id));
    hazeAmount_ = spin(hazeBox, 0.1, 4, 0.1, a.haze ? a.haze->amount : 1.0);
    hazeAmount_->setObjectName("atmoHazeAmount");
    hazeAmount_->setToolTip("How thick: 1 is the preset as designed, 2 twice the fog and air, 0.5 half");
    hazeAbout_ = new QLabel(hazeBox);
    hazeAbout_->setWordWrap(true);
    hf->addRow("Preset", haze_);
    hf->addRow("Amount", hazeAmount_);
    hf->addRow(hazeAbout_);
    top->addWidget(hazeBox);

    const ExpFog fog = a.fog.value_or(ExpFog{});
    fogBox_ = new QGroupBox("Exponential fog (setExpFog)", this);''')
patch(C, '''    top->addWidget(fogBox_);
''', '''    top->addWidget(fogBox_);
    auto hazeChanged = [this] {
        const QString id = haze_->currentData().toString();
        QString about = "No haze: no volumetric air; the exponential fog below, when on, is baked into the map's surfaces "
                        "(the sky is not fogged).";
        for (const HazePreset& p : hazePresets())
            if (id == QLatin1String(p.id))
                about = QString::fromLatin1(p.about) + ". The game fogs the map and the sky itself, the sun lights the air, and a lamp "
                        "with iw8r_volumetric 1 glows in it. Any single value can be changed in Vision overrides or Map > Vision "
                        "Fields (fog..., volumetric..., veilStrength). It replaces the exponential fog below.";
        hazeAbout_->setText(about);
        hazeAmount_->setEnabled(!id.isEmpty());
        fogBox_->setEnabled(id.isEmpty());
    };
    connect(haze_, &QComboBox::currentIndexChanged, this, hazeChanged);
    haze_->setCurrentIndex(a.haze ? std::max(0, haze_->findData(QString::fromStdString(a.haze->preset))) : 0);
    hazeChanged();
''')
patch(C, '''Atmosphere AtmosphereDialog::value() const {
    Atmosphere a;
''', '''Atmosphere AtmosphereDialog::value() const {
    Atmosphere a;
    if (const QString id = haze_->currentData().toString(); !id.isEmpty()) a.haze = Haze{id.toStdString(), hazeAmount_->value()};
''')
patch(C, '''    const std::string want[4] = {a.fog ? formatFog(*a.fog) : std::string(), a.film ? formatFilm(*a.film) : std::string(), a.lut,
                                a.vision};
    const char* keys[4] = {kFogKey, kFilmKey, kLutKey, kVisionKey};
    bool changed = false;
    for (int i = 0; i < 4; ++i) {''', '''    const std::string want[5] = {a.fog ? formatFog(*a.fog) : std::string(), a.film ? formatFilm(*a.film) : std::string(), a.lut,
                                a.vision, a.haze ? formatHaze(*a.haze) : std::string()};
    const char* keys[5] = {kFogKey, kFilmKey, kLutKey, kVisionKey, kHazeKey};
    bool changed = false;
    for (int i = 0; i < 5; ++i) {''')
patch(C, '''    ctx.doc.beginGroup("Atmosphere");
    for (int i = 0; i < 4; ++i) {''', '''    ctx.doc.beginGroup("Atmosphere");
    for (int i = 0; i < 5; ++i) {''')
patch(C, '''    setWindowTitle("Atmosphere (fog and film grade)");''', '''    setWindowTitle("Atmosphere (haze, fog and film grade)");''')
print('ok')
