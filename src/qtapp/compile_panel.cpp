#include "material_tints.h"
#include "scriptable_catalog.h"
#include "log_links.h"
#include "build_report.h"
#include "build_preflight.h"
#include "normal_pack.h"
#include "compile_panel.h"

#include "map_gametypes.h"

#include <algorithm>
#include "map_doctor.h"
#include "autosave_history.h"
#include "prefab_browser.h"
#include "prefab.h"
#include "settings.h"
#include "atmosphere_dialog.h"
#include "editor_log.h"

#include <QProcessEnvironment>
#include <QTextBlock>
#include <QMouseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <QCryptographicHash>
#include <QFile>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "build_cache.h"
#include "compile_iw8.h"
#include "streamed_images.h"
#include "model_import.h"
#include "iw8_contents.h"
#include "texture_decode.h"
#include <QCheckBox>
#include <QImage>
#include <QDirIterator>
#include <map>
#include <set>
#include "custom_materials.h"
#include "gameplay_checks.h"
#include "leak_check.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#include <iterator>

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

void CompilePanel::setStockImageSource(std::function<QString(const QString&)> colorDds) { stockColor_ = std::move(colorDds); }

QString CompilePanel::prepareStockMaterials(int maxSide, int* made, int* missing, const std::set<std::string>& extra) {
    *made = *missing = 0;
    missingStock_.clear();
    lowResStock_.clear();
    // 4.25: the streamed-image catalog, to notice a cached colour image smaller than the stock one.
    static StreamedImageCatalog streamed;
    static bool streamedLoaded = false;
    if (!streamedLoaded) {
        streamedLoaded = true;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_streamed_images.tsv")); ++up) d.cdUp();
        streamed.load(d.filePath("data/asset_catalog/iw8_streamed_images.tsv").toStdString());
    }
    // Editor materials of drawn faces and patches (prefab contents included).
    std::set<std::string> used;
    for (const auto& e : buildMap_.entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                if (p->kind == PrimKind::Patch) used.insert(p->patchMaterial);
                for (const auto& f : p->faces) used.insert(f.material());
            }
    used.insert(extra.begin(), extra.end());  // stock model surface materials
    // Materials the user's own libraries define (<map dir>/materials, Preferences) are custom, not stock.
    {
        std::vector<std::string> userRoots;
        for (const QString& r : buildMaterialRoots())
            if (QFileInfo(r).isDir()) userRoots.push_back(r.toStdString());
        if (!userRoots.empty()) {
            MaterialLibrary userLib;
            std::string ignored;
            userLib.load(userRoots, &ignored);
            std::erase_if(used, [&](const std::string& m) { return userLib.find(m) != nullptr; });
        }
    }
    const QString dir = buildRoot() + "/stock_materials";
    QDir().mkpath(dir);
    for (const std::string& m : used) {
        if (m.empty() || isIw8ToolMaterial(m) || !isValidCustomMaterialName(m) || qs(m).compare("water", Qt::CaseInsensitive) == 0 ||
            qs(m).compare("iw8r_glass", Qt::CaseInsensitive) == 0)
            continue;  // water and iw8r_glass: built in
        const QString dds = stockColor_(qs(m));
        if (dds.isEmpty() || !QFileInfo(dds).isFile()) {
            ++*missing;
            missingStock_ << qs(m);
            continue;
        }
        if (const StreamedImage* si = streamed.findByCacheStem(QFileInfo(dds).completeBaseName().toStdString()); si && si->streamed) {
            int w = 0, h = 0;
            if (readDdsSize(dds.toStdString(), w, h) && std::max(w, h) < std::max(si->fullW, si->fullH))
                lowResStock_ << QString("%1 (%2x%3 of %4x%5)").arg(qs(m)).arg(w).arg(h).arg(si->fullW).arg(si->fullH);
        }
        QString stem = qs(m);
        stem.replace('/', "__");
        const QString png = dir + "/" + stem + "_c.png";
        // Cut-out materials (chainlink, foliage) carry a signed-distance-field mask; the colour image's alpha is
        // specular (`_c&_s` packing), so the PNG alpha is opaque unless the mask says otherwise.
        const QString mask = stockMask_ ? stockMask_(qs(m)) : QString();
        const bool cutout = !mask.isEmpty() && QFileInfo(mask).isFile();
        const bool blended = stockBlend_ && stockBlend_(qs(m));  // decals, glass: soft coverage, not a cut-out
        // Colour tint of the stock material (core material_tints): baked into the build's colour image. The marker file
        // names the tint the PNG was made with, so a new or changed tint remakes it.
        const auto tint = MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(m);
        const QString tintMark = dir + "/" + stem + ".tint";
        QString tintNow, tintWas;
        if (tint) tintNow = QString("%1%2%3").arg((*tint)[0], 2, 16, QChar('0')).arg((*tint)[1], 2, 16, QChar('0')).arg((*tint)[2], 2, 16, QChar('0'));
        if (QFile tf(tintMark); tf.open(QIODevice::ReadOnly)) tintWas = QString::fromLatin1(tf.readAll()).trimmed();
        const bool stale = !QFileInfo(png).isFile() || QFileInfo(png).lastModified() < QFileInfo(dds).lastModified() ||
                           (cutout && QFileInfo(png).lastModified() < QFileInfo(mask).lastModified()) || tintNow != tintWas;
        if (stale || cutout != QFileInfo(dir + "/" + stem + ".cutout").exists() || blended != QFileInfo(dir + "/" + stem + ".blend").exists()) {
            std::string bytes;
            app::Rgba8Image img;
            if (!readFile(dds.toStdString(), bytes) || !app::decodeDds(bytes, img)) {
                ++*missing;
                missingStock_ << qs(m);
                continue;
            }
            const app::Rgba8Image scaled = app::downscale(img, maxSide);
            QImage q = QImage(scaled.pixels.data(), scaled.width, scaled.height, scaled.width * 4, QImage::Format_RGBA8888).copy();
            QImage maskImg;
            if (cutout) {
                std::string mb;
                app::Rgba8Image mi;
                if (readFile(mask.toStdString(), mb) && app::decodeDds(mb, mi) && mi.width > 0 && mi.height > 0 &&
                    mi.pixels.size() >= size_t(mi.width) * size_t(mi.height) * 4)
                    // Smooth scaling may change the format (premultiplied ARGB): force RGBA8888 at exactly q's size.
                    maskImg = QImage(mi.pixels.data(), mi.width, mi.height, mi.width * 4, QImage::Format_RGBA8888)
                                  .scaled(q.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                  .convertToFormat(QImage::Format_RGBA8888)
                                  .copy();  // deep copy: scaled()/convert can share mi.pixels, freed at the end of this block
                if (maskImg.size() != q.size()) maskImg = QImage();
            }
            if (q.format() != QImage::Format_RGBA8888) q = q.convertToFormat(QImage::Format_RGBA8888);
            // Research 66: the opacity image is one channel (red) or a colour image with the opacity in alpha; read
            // alpha when it varies. Alpha-tested materials cut at half; blended ones keep the soft value.
            int maskChannel = 0;
            if (!maskImg.isNull())
                for (int y = 0; y < maskImg.height() && maskChannel == 0; ++y) {
                    const uchar* mrow = maskImg.constScanLine(y);
                    for (int x = 0; x < maskImg.width(); ++x)
                        if (mrow[x * 4 + 3] < 250) {
                            maskChannel = 3;
                            break;
                        }
                }
            for (int y = 0; y < q.height(); ++y) {
                uchar* row = q.scanLine(y);
                const uchar* mrow = maskImg.isNull() ? nullptr : maskImg.constScanLine(y);
                for (int x = 0; x < q.width(); ++x) {
                    const uchar v = mrow ? mrow[x * 4 + maskChannel] : 255;
                    row[x * 4 + 3] = blended ? v : (v >= 128 ? 255 : 0);
                }
            }
            if (tint) {
                if (q.format() != QImage::Format_RGBA8888) q = q.convertToFormat(QImage::Format_RGBA8888);
                for (int y = 0; y < q.height(); ++y) applyMaterialTint(q.scanLine(y), size_t(q.width()), *tint);
            }
            if (!q.save(png, "PNG")) {
                ++*missing;
                missingStock_ << qs(m);
                continue;
            }
            if (tintNow.isEmpty()) QFile::remove(tintMark);
            else if (QFile tf(tintMark); tf.open(QIODevice::WriteOnly | QIODevice::Truncate)) tf.write(tintNow.toLatin1());
            if (cutout) QFile(dir + "/" + stem + ".cutout").open(QIODevice::WriteOnly);
            else QFile::remove(dir + "/" + stem + ".cutout");
            if (blended) QFile(dir + "/" + stem + ".blend").open(QIODevice::WriteOnly);
            else QFile::remove(dir + "/" + stem + ".blend");
        }
        QJsonObject def;
        if (blended) {
            def["blend"] = true;
            def["twoSided"] = true;
        } else if (cutout) {
            def["alphaTest"] = true;
            def["twoSided"] = true;
        }
        def["name"] = qs(m);
        def["buildName"] = "iw8r_stock/" + qs(m);
        def["color"] = QFileInfo(png).fileName();
        // Normal + gloss (4.14): the stock packed image becomes the converter's slope map, and a specular map carrying
        // the colour image's specular alpha with the gloss in its alpha. Half the colour size keeps zones small.
        const QString nrm = stockNormal_ && Settings::instance().value("compile/stockNormalMaps", true).toBool() ? stockNormal_(qs(m)) : QString();
        if (!nrm.isEmpty() && QFileInfo(nrm).isFile()) {
            const QString npng = dir + "/" + stem + "_n.png", spng = dir + "/" + stem + "_s.png";
            bool ok = QFileInfo(npng).isFile() && QFileInfo(spng).isFile() && QFileInfo(npng).lastModified() >= QFileInfo(nrm).lastModified();
            if (!ok) {
                std::string nb, cb;
                app::Rgba8Image ni, ci;
                if (readFile(nrm.toStdString(), nb) && app::decodeDds(nb, ni) && ni.width > 0 && ni.height > 0) {
                    const app::Rgba8Image ns = app::downscale(ni, std::max(64, maxSide / 2));
                    const size_t texels = size_t(ns.width) * size_t(ns.height);
                    QImage slope(ns.width, ns.height, QImage::Format_RGBA8888), spec(ns.width, ns.height, QImage::Format_RGBA8888);
                    std::vector<uint8_t> sl(texels * 4), sp(texels * 4), colour;
                    replayNormalToSlopeMap(ns.pixels.data(), texels, sl.data());
                    if (readFile(dds.toStdString(), cb) && app::decodeDds(cb, ci) && ci.width > 0 && ci.height > 0) {
                        // The colour image at the normal map's size (nearest texel), for its specular alpha.
                        colour.resize(texels * 4);
                        for (int y = 0; y < ns.height; ++y)
                            for (int x = 0; x < ns.width; ++x) {
                                const size_t src = (size_t(int64_t(y) * ci.height / ns.height) * size_t(ci.width) + size_t(int64_t(x) * ci.width / ns.width)) * 4;
                                std::copy_n(&ci.pixels[src], 4, &colour[(size_t(y) * size_t(ns.width) + size_t(x)) * 4]);
                            }
                    }
                    replaySpecularMap(colour.empty() ? nullptr : colour.data(), ns.pixels.data(), texels, sp.data());
                    for (int y = 0; y < ns.height; ++y) {
                        std::copy_n(&sl[size_t(y) * size_t(ns.width) * 4], size_t(ns.width) * 4, slope.scanLine(y));
                        std::copy_n(&sp[size_t(y) * size_t(ns.width) * 4], size_t(ns.width) * 4, spec.scanLine(y));
                    }
                    ok = slope.save(npng, "PNG") && spec.save(spng, "PNG");
                }
            }
            if (ok) {
                def["normal"] = QFileInfo(npng).fileName();
                def["specular"] = QFileInfo(spng).fileName();
            }
        }
        // The stock material's own surface type when the cache has it (owner playtest 2026-11-11: impacts looked the same
        // everywhere); the name keywords otherwise.
        int st = stockSurface_ ? iw3SurfaceTypeForStockSurfaceFlags(stockSurface_(qs(m))) : 0;
        if (st <= 0) st = iw3SurfaceTypeForMaterial(m);
        if (st > 0) def["surface"] = QString(iw3SurfaceTypeName(st));
        QFile f(dir + "/" + stem + ".iw8mat.json");
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(def).toJson());
        ++*made;
    }
    return *made ? dir : QString();
}

void CompilePanel::setAssetRoots(std::function<QStringList()> materialRoots, std::function<QStringList()> modelRoots,
                                 std::function<double()> modelScale) {
    materialRoots_ = std::move(materialRoots);
    modelRoots_ = std::move(modelRoots);
    modelScale_ = std::move(modelScale);
}

namespace {
QStringList withBundleRoots(QStringList roots, const std::vector<std::string>& bundle) {
    for (const std::string& r : bundle) {
        const QString q = QDir::cleanPath(QString::fromStdString(r));
        bool have = false;
        for (const QString& x : roots) have = have || QDir::cleanPath(x).compare(q, Qt::CaseInsensitive) == 0;
        if (!have) roots << q;
    }
    return roots;
}
}  // namespace

QStringList CompilePanel::buildMaterialRoots() const {
    return withBundleRoots(materialRoots_ ? materialRoots_() : QStringList(), bundleRoots_.materials);
}

QStringList CompilePanel::buildModelRoots() const {
    return withBundleRoots(modelRoots_ ? modelRoots_() : QStringList(), bundleRoots_.models);
}

QString CompilePanel::sanitizeMapId(const QString& raw) {
    // IW8 map ids are lower-case mp_<name>; zone and asset names derive from it (docs/research/15).
    QString id = raw.toLower();
    id.replace(QRegularExpression("[^a-z0-9_]"), "_");
    if (!id.startsWith("mp_")) id = "mp_" + id;
    if (id.size() > 15) id.truncate(15);
    while (id.endsWith('_') && id.size() > 3) id.chop(1);
    return id == "mp_" ? QString("mp_iw8r_map") : id;
}

CompilePanel::CompilePanel(EditorContext* ctx, ToolPathFn toolPath, QWidget* parent)
    : QWidget(parent), ctx_(ctx), toolPath_(std::move(toolPath)) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("Map id", this));
    mapId_ = new QLineEdit(this);
    mapId_->setObjectName("compileMapId");
    mapId_->setMaximumWidth(160);
    mapId_->setToolTip("IW8 map id: lower-case mp_<name>, at most 15 characters");
    top->addWidget(mapId_);
    auto* all = new QPushButton("Compile + Build + Validate", this);
    all->setObjectName("compileAll");
    auto* extract = new QPushButton("Extraction only", this);
    stockTextures_ = new QCheckBox("Stock textures", this);
    stockTextures_->setObjectName("compileStockTextures");
    stockTextures_->setToolTip("Build stock IW8 materials with their colour textures (from the ACTS material cache) instead of grey");
    stockTextures_->setChecked(true);
    top->addWidget(stockTextures_);
    auto* info = new QPushButton("Map Info...", this);
    info->setObjectName("compileMapInfo");
    info->setToolTip("Title, description, compass rotation, minimap scale and exposure written to map.json");
    connect(info, &QPushButton::clicked, this, &CompilePanel::editMapInfo);
    top->addWidget(info);
    auto* atmo = new QPushButton("Atmosphere...", this);
    atmo->setObjectName("compileAtmosphere");
    atmo->setToolTip("Exponential fog and film grade (worldspawn iw8r_fog / iw8r_film), written as the setExpFog gsc and the vision file");
    connect(atmo, &QPushButton::clicked, this, [this] { editAtmosphere(*ctx_, this); });
    top->addWidget(atmo);
    auto* open = new QPushButton("Open Output", this);
    cancel_ = new QPushButton("Cancel", this);
    cancel_->setEnabled(false);
    top->addWidget(all);
    top->addWidget(extract);
    top->addWidget(open);
    top->addWidget(cancel_);
    top->addStretch(1);
    lay->addLayout(top);
    status_ = new QLabel("Idle. F7 compiles the open map to Replay 1.20 zones.", this);
    status_->setWordWrap(true);
    lay->addWidget(status_);
    log_ = new QPlainTextEdit(this);
    log_->setToolTip("Double-click a line that names an entity, model, material, effect or sound to select it in the map");
    log_->viewport()->installEventFilter(this);
    log_->setObjectName("compileLog");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(20000);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    log_->setFont(mono);
    lay->addWidget(log_, 1);

    connect(all, &QPushButton::clicked, this, [this] { run({Stage::Extraction, Stage::Zones, Stage::Validate}); });
    connect(extract, &QPushButton::clicked, this, [this] { run({Stage::Extraction}); });
    connect(open, &QPushButton::clicked, this, [this] {
        QDir().mkpath(buildRoot());
        QDesktopServices::openUrl(QUrl::fromLocalFile(buildRoot()));
    });
    connect(cancel_, &QPushButton::clicked, this, &CompilePanel::cancel);
    connect(mapId_, &QLineEdit::textEdited, this, [this] { userEditedId_ = true; });
    connect(ctx_, &EditorContext::documentChanged, this, &CompilePanel::refreshMapId);
    refreshMapId();
}

CompilePanel::~CompilePanel() {
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        proc_->kill();
        proc_->waitForFinished(3000);
    }
}

void CompilePanel::refreshMapId() {
    if (userEditedId_ && !mapId_->text().isEmpty()) return;
    const QString base = QFileInfo(qs(ctx_->doc.path())).completeBaseName();
    mapId_->setText(sanitizeMapId(base.isEmpty() ? QString("iw8r_map") : base));
}

QString CompilePanel::mapId() const { return sanitizeMapId(mapId_->text()); }

QString CompilePanel::buildRoot() const {
    const QString path = qs(ctx_->doc.path());
    QString dir = path.isEmpty() ? QString() : QFileInfo(path).absolutePath();
    if (dir.isEmpty() || path.endsWith(".ents") || path.endsWith(".json"))
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/builds";
    return QDir::cleanPath(dir + "/iw8build/" + mapId());
}

QString CompilePanel::outputRoot() const {
    // One shared x64-zt output root (its render cache is several hundred MB): Tools > Tool Paths "x64zt_output",
    // default in the app-data folder rather than beside every map.
    const QString def = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/x64zt_output";
    const QString set = toolPath_("x64zt_output");
    return QDir::cleanPath(set.isEmpty() ? def : set);
}

QString CompilePanel::logText() const { return log_->toPlainText(); }

void CompilePanel::append(const QString& line, bool strong) {
    if (strong) log_->appendHtml("<b>" + line.toHtmlEscaped() + "</b>");
    else log_->appendPlainText(line);
    EditorLog::instance().add(lineLevel(line), "compile", line);
}

LogLevel CompilePanel::lineLevel(const QString& line) {
    // Headers are "strong" too, so the level comes from the text: "warning..." lines, and error / fail words. A count of
    // zero ("0 failed", "0 error(s)", "0 failures", "failed: 0") and "no errors" are summaries, not errors.
    if (line.startsWith("warning", Qt::CaseInsensitive)) return LogLevel::Warning;
    static const QRegularExpression zero("\\b(0|no)\\s+(error|fail)\\w*(\\(s\\))?|\\b(error|fail)\\w*(\\(s\\))?\\s*[:=]\\s*0\\b",
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression failWord("\\b(error|fail\\w*)\\b", QRegularExpression::CaseInsensitiveOption);
    QString rest = line;
    rest.remove(zero);
    return failWord.match(rest).hasMatch() ? LogLevel::Error : LogLevel::Info;
}

void CompilePanel::run(const std::vector<Stage>& stages) {
    if (running_) return;
    if (beforeExtraction_ && std::find(stages.begin(), stages.end(), Stage::Extraction) != stages.end()) beforeExtraction_();
    queue_.assign(stages.begin(), stages.end());
    running_ = true;
    lastOk_ = false;
    cancel_->setEnabled(true);
    log_->clear();
    stageMs_.clear();
    stageOrder_.clear();
    stageLabel_.clear();
    totalClock_.start();
    pendingKey_.clear();
    reusedZones_ = false;
    append(QString("== %1  map %2  ->  %3").arg(QDateTime::currentDateTime().toString(Qt::ISODate), mapId(), buildRoot()), true);
    // 9.12 Map Doctor: findings with an automatic fix are reported before every build (applied only by the mapper).
    if (const auto fixes = diagnoseMap(ctx_->doc.map()); !fixes.empty()) {
        append(QString("warning: map doctor: %1 finding(s) with an automatic fix (Tools > Check Map > Fix All), e.g. %2")
                   .arg(fixes.size()).arg(qs(fixes.front().description)), true);
    }
    // 9.14 low-disk guard: a build writes several GB of zones, caches and render data; warn before it runs out.
    for (const QString& dir : {buildRoot(), outputRoot()}) {
        QString probe = dir;
        while (!probe.isEmpty() && !QFileInfo::exists(probe)) probe = QFileInfo(probe).absolutePath() == probe ? QString() : QFileInfo(probe).absolutePath();
        uintmax_t freeBytes = 0;
        if (!probe.isEmpty() && !hasFreeSpace(probe.toStdString(), uintmax_t(4) << 30, &freeBytes))
            append(QString("warning: only %1 MB free on the drive of %2; a build can need several GB").arg(freeBytes >> 20).arg(dir), true);
    }
    next();
}

void CompilePanel::cancel() {
    if (!running_) return;
    queue_.clear();
    pendingValidate_.clear();
    if (proc_ && proc_->state() != QProcess::NotRunning) proc_->kill();
    else fail("cancelled");
}

void CompilePanel::fail(const QString& why) {
    queue_.clear();
    pendingValidate_.clear();
    running_ = false;
    lastOk_ = false;
    cancel_->setEnabled(false);
    markStage(QString());
    QFile::remove(zonesKeyFile());  // the zones on disk may be incomplete: never reuse them
    append("FAILED: " + why, true);
    append(buildTimeLine(), true);
    status_->setText("<span style='color:#e66'>Failed:</span> " + why.toHtmlEscaped());
    ctx_->setStatus("Compile failed: " + why);
    emit finished(false);
}

QString CompilePanel::zonesKeyFile() const { return buildRoot() + "/zones.key"; }

QString CompilePanel::zonesKey() const {
    const QFileInfo zt(toolPath_("zonetool"));
    return QString("%1 %2 %3 %4")
        .arg(QString::number(qulonglong(hashDirectory(extractionDir().toStdString())), 16), mapId())
        .arg(zt.size())
        .arg(zt.lastModified().toMSecsSinceEpoch());
}

void CompilePanel::forceFullRebuild() {
    QFile::remove(zonesKeyFile());
    run({Stage::Extraction, Stage::Zones, Stage::Validate});
}

void CompilePanel::markStage(const QString& label) {
    if (label == stageLabel_) return;
    if (!stageLabel_.isEmpty()) stageMs_[stageLabel_] += stageClock_.elapsed();
    stageClock_.restart();
    stageLabel_ = label;
    if (!label.isEmpty() && !stageOrder_.contains(label)) stageOrder_ << label;
}

QString CompilePanel::buildTimeLine() const {
    QStringList parts;
    for (const QString& s : stageOrder_) parts << QString("%1 %2 s").arg(s).arg(stageMs_.value(s) / 1000.0, 0, 'f', 1);
    return QString("build time: %1%2total %3 s")
        .arg(parts.join(", "))
        .arg(parts.isEmpty() ? QString() : QString(", "))
        .arg(totalClock_.isValid() ? totalClock_.elapsed() / 1000.0 : 0.0, 0, 'f', 1);
}

void CompilePanel::next() {
    if (!pendingValidate_.isEmpty()) {
        markStage("validate");
        startValidation();
        return;
    }
    if (queue_.empty()) {
        running_ = false;
        lastOk_ = true;
        cancel_->setEnabled(false);
        markStage(QString());
        // The package's map.json names the game types the map has spawns and objects for (the client lists a custom
        // map with Team Deathmatch only otherwise). Written on every finished run, also when the zones were reused.
        writePackageGameTypes();
        if (!pendingKey_.isEmpty()) {
            // Zones built and validated in this run: later runs with the same extraction can reuse them.
            QFile kf(zonesKeyFile());
            if (kf.open(QIODevice::WriteOnly | QIODevice::Truncate)) kf.write(pendingKey_.toUtf8());
        }
        lastBuildTimes_ = buildTimeLine();
        append(lastBuildTimes_, true);
        append("== done", true);
        status_->setText("<span style='color:#6c6'>Done.</span> Output in " + buildRoot().toHtmlEscaped() +
                         ". In-game loading is not verified by this step.");
        ctx_->setStatus("Compile finished: " + buildRoot());
        emit finished(true);
        return;
    }
    const Stage s = queue_.front();
    queue_.pop_front();
    markStage(s == Stage::Extraction ? "extraction" : s == Stage::Zones ? "zones" : "validate");
    switch (s) {
    case Stage::Extraction:
        if (runExtraction()) QMetaObject::invokeMethod(this, &CompilePanel::next, Qt::QueuedConnection);
        return;
    case Stage::Zones: {
        const QString zt = toolPath_("zonetool");
        const QString replay = toolPath_("replay");
        for (const QString& f : {zt, replay})
            if (!QFileInfo::exists(f)) return fail("missing " + f + " (Tools > Tool Paths)");
        if (!QFileInfo::exists(extractionDir() + "/complete")) return fail("no extraction; run the Extraction stage first");
        {
            // 7.10: an extraction identical to the one of the last validated build (and the same converter) gives the
            // same zones: reuse them instead of rebuilding and validating again.
            const QString key = zonesKey();
            QFile kf(zonesKeyFile());
            const QString zone = outputRoot() + "/" + mapId() + "/" + mapId() + ".ff";
            const bool reuse = Settings::instance().value("compile/reuseZones", true).toBool() && kf.open(QIODevice::ReadOnly) &&
                               QString::fromUtf8(kf.readAll()) == key && QFileInfo::exists(zone);
            kf.close();
            if (reuse) {
                std::erase(queue_, Stage::Validate);
                reusedZones_ = true;
                append("zones: the extraction is unchanged since the last validated build; its zones are reused (" + zone +
                           "). Compile > Force Full Rebuild rebuilds them.",
                       true);
                QMetaObject::invokeMethod(this, &CompilePanel::next, Qt::QueuedConnection);
                return;
            }
            pendingKey_ = key;
        }
        QDir(zonesDir()).removeRecursively();
        QDir().mkpath(outputRoot());
        // x64-zt route (docs/research/17 §6): reads the extraction, writes the Replay 1.20 zones under --output
        // (its parser drops single-dash flags, so -o would not work).
        // -path points at the game for stock asset lookups only; output never goes into the game folder.
        QStringList args = {"-game", "iw8", "-path", QFileInfo(replay).absolutePath(), "buildextraction", extractionDir(),
                            "--map", mapId(), "--replay", replay, "--output", outputRoot()};
        {
            const QString meta = buildRoot() + "/metadata.json";
            QFile mf(meta);
            if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate) || mf.write(metadataJson()) < 0) return fail("cannot write " + meta);
            mf.close();
            args << "--metadata" << meta;
        }
        const QString acts = toolPath_("acts");
        if (QFileInfo::exists(acts)) args << "--acts" << acts;
        // gsc-tool compiles the converted map script (triggers, effects, ...); without it x64-zt ships the
        // minimap-only startup script.
        const QString gsc = toolPath_("gsc_tool");
        if (!gsc.isEmpty() && QFileInfo::exists(gsc)) args << "--gsc_tool" << gsc;
        // Editor builds carry no IW3 gametype assets: without this the converter plants the IW3 prop_flag_neutral
        // at every Domination flag and fails ("IW3 entity model assets are absent"); Replay draws its own markers.
        args << "--iw3_mode_assets" << "off";
        QFile::remove(navDebugFile());  // a fresh navmesh dump comes with this build
        startProcess("x64-zt buildextraction", zt, args, buildRoot());
        return;
    }
    case Stage::Validate: {
        QDir zones(zonesDir());
        QStringList ffs;
        for (const QFileInfo& fi : zones.entryInfoList({"*.ff"}, QDir::Files, QDir::Name)) ffs << fi.absoluteFilePath();
        // x64-zt may nest zones one level down (zone/<lang>/...).
        for (const QFileInfo& sub : zones.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QFileInfo& fi : QDir(sub.absoluteFilePath()).entryInfoList({"*.ff"}, QDir::Files, QDir::Name))
                ffs << fi.absoluteFilePath();
        if (ffs.isEmpty()) return fail("no .ff files in " + zonesDir());
        pendingValidate_ = ffs;
        startValidation();
        return;
    }
    }
}

bool CompilePanel::runExtraction() {
    append("-- IW8 extraction (native compiler)", true);
    {
        const LeakResult leak = checkLeak(ctx_->doc.map());
        if (!leak.error.empty()) append("warning: leak check did not run: " + qs(leak.error));
        else if (!leak.sealed)
            append(QString("warning: LEAK from %1 (entity %2); Tools > Leak Check draws the path. The build continues (one DPVS cell).")
                       .arg(qs(leak.classname))
                       .arg(leak.leakingEntity));
        for (const auto& g : gameplayIssues(ctx_->doc.map())) append("warning: gameplay: " + qs(g.message));
    }
    CompileOptions opt;
    opt.mapId = mapId().toStdString();
    if (!ctx_->doc.path().empty()) opt.mapDir = QFileInfo(qs(ctx_->doc.path())).absolutePath().toStdString();
    for (const QString& r : PrefabBrowser::searchRoots()) opt.prefabRoots.push_back(r.toStdString());
    // Prefab bundles: what this build can provide (features, and the stock catalogue their required names are checked
    // against), and the bundles' own material and model folders, which join the mapper's folders for this build.
    opt.prefabSupport = PrefabBrowser::buildSupport();
    bundleRoots_ = prefabBundleAssetRoots(ctx_->doc.map(), ctx_->doc.path(), opt.prefabRoots);
    // What the build contains: the document with its prefabs expanded (stock models and textures inside prefabs too).
    {
        std::vector<std::string> pw;
        std::string perr;
        PrefabFlattenInfo info;
        if (!flattenPrefabBundles(ctx_->doc.map(), ctx_->doc.path(), opt.prefabRoots, opt.prefabSupport, buildMap_, info, &pw, &perr))
            buildMap_ = ctx_->doc.map();
    }
    // Stock xmodels on script_model / misc_model: built as copies from the ACTS model cache, textured
    // like brushes (their xmodel materials join the stock-texture set below).
    std::map<std::string, ImportedModel> stockModels;
    if (stockModel_) {
        std::set<std::string> names;
        for (const auto& e : buildMap_.entities) {
            const std::string cls = e.classname();
            // dynamic_prop too: a stock model used only by physics props was never fetched (showcase 2, 2026-11-11).
            if (cls != "script_model" && cls != "misc_model" && cls != "dynamic_prop") continue;
            if (const std::string* m = e.value("model"); m && !m->empty() && (*m)[0] != '*') names.insert(*m);
        }
        int missing = 0;
        // The mapper's own OBJ models (model folders, and the models folders of placed prefab bundles) are custom
        // models, never stock.
        const QStringList roots = buildModelRoots();
        const auto isCustom = [&](const std::string& n) {
            for (const QString& r : roots) {
                QDirIterator it(r, {QString::fromStdString(n) + ".obj", QString::fromStdString(n) + ".glb", QString::fromStdString(n) + ".gltf"},
                                QDir::Files, QDirIterator::Subdirectories);
                if (it.hasNext()) return true;
            }
            return false;
        };
        for (const std::string& n : names) {
            if (isCustom(n)) continue;
            if (auto im = stockModel_(qs(n))) stockModels.emplace(n, std::move(*im));
            else ++missing;
        }
        if (!stockModels.empty() || missing)
            append(QString("stock models: %1 built from the ACTS model cache%2").arg(stockModels.size()).arg(
                missing ? QString(", %1 not cached yet (left out this time; build again in a moment)").arg(missing) : QString()));
        if (!stockModels.empty()) opt.stockModels = &stockModels;
    }
    // Sky (doc 33): the worldspawn skyname's stock image from the ACTS image cache; the compiler makes the cube map.
    QString skyName;
    for (const auto& e : ctx_->doc.map().entities) {
        if (e.classname() != "worldspawn") continue;
        for (const char* key : {"skyname", "sky"})
            if (const std::string* v = e.value(key); v && !v->empty()) {
                skyName = qs(*v);
                break;
            }
        break;
    }
    if (!skyName.isEmpty() && skySource_) opt.skyDdsPath = skySource_(skyName).toStdString();
    // Custom sky (5.17): Compile > Sky From Panorama wrote a cube DDS and named it in worldspawn iw8r_sky_file.
    if (const Entity* ws = ctx_->doc.map().entities.empty() ? nullptr : &ctx_->doc.map().entities.front(); ws && ws->isWorldspawn())
        if (const std::string* f = ws->value("iw8r_sky_file"); f && !f->empty()) {
            const QString path = QFileInfo(qs(*f)).isAbsolute() ? qs(*f)
                                                                 : QFileInfo(qs(ctx_->doc.path())).absoluteDir().absoluteFilePath(qs(*f));
            if (QFileInfo::exists(path)) opt.skyDdsPath = path.toStdString();
            else append("warning: iw8r_sky_file " + qs(*f) + " not found; the stock sky route is used");
        }
    // Reflection probes captured in the editor (Compile > Capture Reflection Probes).
    if (QFileInfo(buildRoot() + "/probe_capture").isDir()) opt.probeCaptureDir = (buildRoot() + "/probe_capture").toStdString();
    // 1.17: Compile > Set Loadscreen From Camera saves <map>.loadscreen.png beside the map.
    if (const QString ls = loadscreenImageFile(); !ls.isEmpty() && QFileInfo::exists(ls)) opt.loadscreenImagePath = ls.toStdString();
    MaterialLibrary library;
    {
        std::vector<std::string> roots;
        for (const QString& r : buildMaterialRoots())
            if (QFileInfo(r).isDir()) roots.push_back(r.toStdString());  // <map dir>/materials is optional
        if (stockTextures_->isChecked() && stockColor_) {
            int made = 0, missing = 0;
            std::set<std::string> modelMats;
            for (const auto& [n, im] : stockModels)
                for (const auto& mesh : im.meshes)
                    if (!mesh.material.empty()) modelMats.insert(mesh.material);
            // Materials put on models (iw8r_material): built like a brush face's.
            for (const auto& e : buildMap_.entities)
                if (const std::string* om = e.value("iw8r_material"); om && !om->empty()) modelMats.insert(*om);
            for (const auto& e : buildMap_.entities)
                for (const auto& it : e.items)
                    if (const KeyValueLine* kv = it.kv(); kv && kv->name().rfind("iw8r_mat:", 0) == 0 && !kv->value.empty() &&
                                                          e.classname() != "misc_prefab")
                        modelMats.insert(kv->value);
            // 11.8: custom models (OBJ / glTF in the model folders) whose groups name stock materials, such as a
            // Stock Reference to Model copy: their stock materials are built like a brush face's.
            if (const QStringList modelFolders = buildModelRoots(); !modelFolders.isEmpty()) {
                std::vector<std::string> mroots;
                for (const QString& r : modelFolders) mroots.push_back(r.toStdString());
                std::set<std::string> seen;
                for (const auto& e : buildMap_.entities) {
                    const std::string cls = e.classname();
                    if (cls != "misc_model" && cls != "script_model" && cls != "dynamic_prop") continue;
                    const std::string* m = e.value("model");
                    if (!m || m->empty() || !seen.insert(*m).second) continue;
                    const std::string file = findCustomModelFile(mroots, *m);
                    if (file.empty()) continue;
                    ImportedModel im;
                    if (importModelFile(file, im, nullptr))
                        for (const auto& mesh : im.meshes)
                            if (!mesh.material.empty()) modelMats.insert(mesh.material);
                }
            }
            const QString stockRoot = prepareStockMaterials(1024, &made, &missing, modelMats);
            if (!stockRoot.isEmpty()) roots.push_back(stockRoot.toStdString());
            // Missing ones are either still being fetched (build again later) or not available in Replay at all
            // (the cache tried every zone that lists the material or its images): those always build grey.
            QStringList pendingNames, unavailable;
            for (const QString& m : missingStock_) {
                const QString why = stockStatus_ ? stockStatus_(m) : QString();
                if (why.isEmpty()) pendingNames << m;
                else unavailable << m + " (" + why.left(160) + ")";
            }
            append(QString("stock textures: %1 material(s) built with their colour image%2").arg(made).arg(
                pendingNames.isEmpty() ? QString()
                                       : QString(", %1 not cached yet (grey this time; the cache is filling, build again later): %2")
                                             .arg(pendingNames.size()).arg(pendingNames.join(", "))));
            if (!unavailable.isEmpty())
                append(QString("warning: %1 stock material(s) are not available in Replay and build grey (pick another): %2")
                           .arg(unavailable.size()).arg(unavailable.join("; ")));
            if (!lowResStock_.isEmpty())
                append(QString("warning: %1 stock material(s) built from a cached colour image smaller than the stock one (streamed "
                               "image, only part of its mips cached; research 52): %2")
                           .arg(lowResStock_.size()).arg(lowResStock_.mid(0, 12).join(", ")));
        }
        // Built-in "water" (the editor's water tool texture): a tinted rippled image so water volumes read as water
        // in game instead of the white placeholder. Skipped when a library of the user's already defines "water".
        {
            MaterialLibrary userLib;
            std::string ignored;
            if (!roots.empty()) userLib.load(roots, &ignored);
            if (!userLib.find("water")) {
                const QString dir = buildRoot() + "/builtin_materials";
                QDir().mkpath(dir);
                const QString png = dir + "/iw8r_builtin_water_c.png";
                if (!QFileInfo(png).isFile()) {
                    QImage img(128, 128, QImage::Format_RGBA8888);
                    for (int y = 0; y < 128; ++y)
                        for (int x = 0; x < 128; ++x) {
                            const double r = 0.5 + 0.25 * std::sin(x * 0.19635) * std::cos(y * 0.0981748) +
                                             0.25 * std::sin((x + y) * 0.0490874);  // tiles every 128 texels
                            img.setPixel(x, y, qRgba(int(28 + 30 * r), int(78 + 45 * r), int(110 + 50 * r), 255));
                        }
                    img.save(png);
                }
                QFile def(dir + "/water.iw8mat.json");
                if (def.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    def.write("{\n \"name\": \"water\",\n \"color\": \"iw8r_builtin_water_c.png\",\n \"surface\": \"water\"\n}\n");
                def.close();
                roots.push_back(dir.toStdString());
            }
            if (!userLib.find("iw8r_glass")) {
                // 5.12: built-in clear glass (Brush > Make Glass Pane): blended, so a script_brushmodel pane with it becomes
                // Replay's native breakable glass (the converter: "replaced N IW3 scripted glass entities with native glass").
                const QString dir = buildRoot() + "/builtin_materials";
                QDir().mkpath(dir);
                const QString png = dir + "/iw8r_builtin_glass_c.png";
                if (!QFileInfo(png).isFile()) {
                    QImage img(128, 128, QImage::Format_RGBA8888);
                    for (int y = 0; y < 128; ++y)
                        for (int x = 0; x < 128; ++x) {
                            const double streak = 0.5 + 0.5 * std::sin((x + y * 0.35) * 0.049);  // soft diagonal sheen
                            img.setPixel(x, y, qRgba(int(185 + 30 * streak), int(205 + 25 * streak), int(220 + 20 * streak), int(48 + 30 * streak)));
                        }
                    img.save(png);
                }
                QFile def(dir + "/iw8r_glass.iw8mat.json");
                if (def.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    def.write("{\n \"name\": \"iw8r_glass\",\n \"color\": \"iw8r_builtin_glass_c.png\",\n \"surface\": \"glass\",\n"
                              " \"blend\": true,\n \"twoSided\": true\n}\n");
                def.close();
                if (std::find(roots.begin(), roots.end(), dir.toStdString()) == roots.end()) roots.push_back(dir.toStdString());
            }
        }
        std::string lerr;
        // A bad definition is reported but never drops the rest of the library (that made every surface grey).
        if (!roots.empty()) {
            library.load(roots, &lerr);
            if (!library.names().empty()) opt.library = &library;
        }
        if (!lerr.empty()) append("warning: custom materials: " + qs(lerr));
        if (opt.library) append(QString("custom materials: %1 in the library").arg(library.names().size()));
        // Stock scriptable definitions (6.14): the catalogue beside the editor.
        opt.scriptables = &ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
        opt.carryScriptables = PrefabBrowser::realScriptables();
        // The folders the library came from (the placed bundles' material folders are among them): the build can
        // then load one library that also holds the materials of a bundle it finds inside another prefab.
        opt.materialRoots = roots;
        for (const QString& r : buildModelRoots()) opt.modelRoots.push_back(r.toStdString());
        if (modelScale_) opt.modelScale = modelScale_();
        if (!bundleRoots_.materials.empty() || !bundleRoots_.models.empty() || !bundleRoots_.sounds.empty() || !bundleRoots_.effects.empty())
            append(QString("prefab bundles: %1 material, %2 model, %3 sound and %4 effect folder(s) of placed bundles join this build")
                       .arg(bundleRoots_.materials.size()).arg(bundleRoots_.models.size()).arg(bundleRoots_.sounds.size())
                       .arg(bundleRoots_.effects.size()));
    }
    {
        // 8.2 authored effects: <map dir>/fx/<name>.iw8fx.json (Tools > Effect Editor), the default soft sprite from data/fx.
        if (!ctx_->doc.path().empty()) {
            opt.fxRoots.push_back((QFileInfo(qs(ctx_->doc.path())).absolutePath() + "/fx").toStdString());
            // 6.22: the map's own sounds (<map dir>/sounds/<name>.wav|.mp3).
            opt.soundRoots.push_back((QFileInfo(qs(ctx_->doc.path())).absolutePath() + "/sounds").toStdString());
        }
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/fx/iw8r_soft_particle.png")); ++up) d.cdUp();
        if (QFileInfo::exists(d.filePath("data/fx/iw8r_soft_particle.png")))
            opt.fxDefaultSprite = d.filePath("data/fx/iw8r_soft_particle.png").toStdString();
    }
    CompileReport rep;
    std::string err;
    QDir().mkpath(buildRoot());
    const bool ok = compileIw8Extraction(ctx_->doc.map(), extractionDir().toStdString(), opt, rep, &err);
    append(QString("surfaces %1, triangles %2, collision brushes %3, entities %4, spawns %5")
               .arg(rep.surfaces).arg(rep.triangles).arg(rep.collisionBrushes).arg(rep.entities).arg(rep.spawns));
    if (rep.bounds.valid())
        append(QString("bounds %1 %2 %3 .. %4 %5 %6")
                   .arg(rep.bounds.mins.x).arg(rep.bounds.mins.y).arg(rep.bounds.mins.z)
                   .arg(rep.bounds.maxs.x).arg(rep.bounds.maxs.y).arg(rep.bounds.maxs.z));
    if (!rep.sky.empty())
        append(QString("sky: %1 built (%2)").arg(qs(rep.sky), qs(rep.skyNote)));
    else if (!skyName.isEmpty() && skySource_ && opt.skyDdsPath.empty())
        append(QString("sky: %1 not cached yet (dark placeholder this time; the export runs in the background, build again later)")
                   .arg(skyName));
    for (const auto& w : rep.warnings) {
        // The one line per prefab bundle kind that says what was built is information, not a warning; a bundle built
        // from its fallback contents stays a warning, so an imitation is never mistaken for the real object.
        const bool bundleSummary = w.rfind("prefab bundle '", 0) == 0 && w.find(" placement(s), main contents") != std::string::npos;
        append((bundleSummary ? QString() : QString("warning: ")) + qs(w));
    }
    if (!ok) {
        fail("extraction: " + qs(err));
        return false;
    }
    append("wrote " + extractionDir());
    // Preflight (build_preflight.h): what would fail or error in game, checked before x64-zt spends minutes on it.
    // Errors stop the build unless Preferences compile/preflightBlocks is off; warnings are listed.
    {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/generated/iw8_gsc_builtins.txt")); ++up) d.cdUp();
        PreflightCatalogs cat;
        std::string cerr;
        if (!loadPreflightCatalogs(d.filePath("data").toStdString(), cat, &cerr)) {
            append("preflight skipped: " + qs(cerr));
        } else {
            const auto issues = preflightExtraction((extractionDir() + "/extracted").toStdString(), mapId().toStdString(), cat);
            int errors = 0;
            for (const PreflightIssue& i : issues) {
                append(QString("preflight %1 %2: %3").arg(i.error ? "error" : "warning", qs(i.category), qs(i.message)), i.error);
                errors += i.error;
            }
            append(QString("preflight: %1 error(s), %2 warning(s)").arg(errors).arg(int(issues.size()) - errors));
            // Build report and budget (1.14 / 1.16): what this build carries, what changed since the last one.
            const BuildReport report = makeBuildReport((extractionDir() + "/extracted").toStdString(), mapId().toStdString());
            for (const QString& line : qs(formatBuildReport(report)).split('\n', Qt::SkipEmptyParts)) append(line);
            const std::string last = (buildRoot() + "/last_build_report.json").toStdString();
            BuildReport previous;
            if (loadBuildReport(last, previous)) {
                const QString diff = qs(diffBuildReports(previous, report)).trimmed();
                append(diff.isEmpty() ? QString("since the last build: no changes") : QString("since the last build:"));
                for (const QString& line : diff.split('\n', Qt::SkipEmptyParts)) append("  " + line);
            }
            saveBuildReport(report, last);
            for (const std::string& w : budgetWarnings(report)) append("budget: " + qs(w), true);
            if (errors && Settings::instance().value("compile/preflightBlocks", true).toBool()) {
                fail(QString("preflight found %1 error(s) that would break the map in game (listed above)").arg(errors));
                return false;
            }
        }
    }
    return true;
}

void CompilePanel::startProcess(const QString& title, const QString& exe, const QStringList& args, const QString& workDir) {
    append("-- " + title, true);
    append("> \"" + exe + "\" " + args.join(' '));
    status_->setText("Running " + title + "...");
    if (!proc_) {
        proc_ = new QProcess(this);
        proc_->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc_, &QProcess::readyRead, this, [this] {
            const QString out = QString::fromLocal8Bit(proc_->readAll());
            for (const QString& l : out.split('\n', Qt::SkipEmptyParts)) append(l.trimmed());
        });
        connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
            if (!running_) return;
            if (st != QProcess::NormalExit) return fail(procTitle_ + " crashed or was cancelled");
            if (code != 0) return fail(QString("%1 exited with %2").arg(procTitle_).arg(code));
            append(QString("%1: exit 0").arg(procTitle_));
            next();
        });
        connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart && running_) fail(procTitle_ + " failed to start");
        });
    }
    procTitle_ = title;
    proc_->setWorkingDirectory(workDir);
    // Navigation overlay (6.23, research 45): x64-zt writes its navmesh debug dump to the path in this variable.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QDir().mkpath(buildRoot());
    env.insert("IW8_ZONETOOL_NAV_DEBUG", QDir::toNativeSeparators(navDebugFile()));
    proc_->setProcessEnvironment(env);
    proc_->start(exe, args);
}

void CompilePanel::startValidation() {
    const QString ff = pendingValidate_.takeFirst();
    const QString acts = toolPath_("acts"), replay = toolPath_("replay"), oodle = toolPath_("oodle");
    for (const QString& f : {acts, replay, oodle})
        if (!QFileInfo::exists(f)) return fail("missing " + f + " (Tools > Tool Paths)");
    const QString out = buildRoot() + "/validate/" + QFileInfo(ff).completeBaseName();
    QDir().mkpath(out);
    startProcess("ACTS --test " + QFileInfo(ff).fileName(), acts,
                 {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "--test", "-o", out, ff}, buildRoot());
}

// ------------------------------------------------------------------ map info (map.json)

namespace {
const char* kInfoKeys[] = {"iw8r_title", "iw8r_description", "iw8r_compass_rotation", "iw8r_minimap_scale",
                           "iw8r_ladder_rung_distance", "iw8r_exposure_adjust", "iw8r_gametypes"};
}  // namespace

QString CompilePanel::writePackageGameTypes(bool log) {
    const QString path = zonesDir() + "/map.json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const std::string before = f.readAll().toStdString();
    f.close();
    const MapGameTypes types = mapGameTypes(ctx_->doc.map());
    const std::string after = withGameTypes(before, types.list);
    if (after != before) {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(after.data(), qint64(after.size())) < 0) {
            append("game types: cannot write " + path, true);
            return {};
        }
    }
    if (log)
        for (const std::string& n : types.notes) append("game types: " + QString::fromStdString(n));
    return QString::fromStdString(types.list);
}

QByteArray CompilePanel::metadataJson() const {
    QJsonObject o;
    o["id"] = mapId();
    const Entity* w = nullptr;
    for (const auto& e : ctx_->doc.map().entities)
        if (e.isWorldspawn()) {
            w = &e;
            break;
        }
    auto value = [&](const char* key) -> QString {
        const std::string* v = w ? w->value(key) : nullptr;
        return v ? QString::fromStdString(*v).trimmed() : QString();
    };
    const QString title = value("iw8r_title");
    o["title"] = title.isEmpty() ? mapId() : title;
    if (const QString d = value("iw8r_description"); !d.isEmpty()) o["description"] = d;
    bool ok = false;
    if (const int r = value("iw8r_compass_rotation").toInt(&ok); ok && (r == 0 || r == 90 || r == 180 || r == 270)) o["compassRotation"] = r;
    if (const double s = value("iw8r_minimap_scale").toDouble(&ok); ok && s >= 0.5 && s <= 1.5) o["minimapScale"] = s;
    if (const double l = value("iw8r_ladder_rung_distance").toDouble(&ok); ok && l >= 8 && l <= 32) o["ladderRungDistance"] = l;
    if (const double x = value("iw8r_exposure_adjust").toDouble(&ok); ok && std::isfinite(x) && std::fabs(x) <= 8) o["exposureAdjust"] = x;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

void CompilePanel::editMapInfo() {
    Entity* w = ctx_->doc.worldspawn();
    if (!w) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Map Info (map.json)");
    auto* form = new QFormLayout(&dlg);
    auto value = [&](const char* key) {
        const std::string* v = w->value(key);
        return v ? QString::fromStdString(*v) : QString();
    };
    auto* title = new QLineEdit(value("iw8r_title"), &dlg);
    title->setPlaceholderText(mapId());
    auto* desc = new QLineEdit(value("iw8r_description"), &dlg);
    auto* compass = new QComboBox(&dlg);
    compass->addItems({"auto (from the map)", "0", "90", "180", "270"});
    if (const int i = compass->findText(value("iw8r_compass_rotation")); i > 0) compass->setCurrentIndex(i);
    // Special value text at the minimum means "not set" (x64-zt default).
    auto spin = [&](double lo, double hi, double step, const char* key) {
        auto* s = new QDoubleSpinBox(&dlg);
        s->setDecimals(2);
        s->setRange(lo - step, hi);
        s->setSingleStep(step);
        s->setSpecialValueText("default");
        bool ok = false;
        const double v = value(key).toDouble(&ok);
        s->setValue(ok && v >= lo && v <= hi ? v : lo - step);
        return s;
    };
    auto* minimap = spin(0.5, 1.5, 0.05, "iw8r_minimap_scale");
    auto* ladder = spin(8, 32, 1, "iw8r_ladder_rung_distance");
    auto* exposure = spin(-8, 8, 0.25, "iw8r_exposure_adjust");
    form->addRow("Title (map.json)", title);
    form->addRow("Description", desc);
    form->addRow("Compass rotation", compass);
    form->addRow("Minimap scale", minimap);
    form->addRow("Ladder rung distance", ladder);
    form->addRow("Exposure adjust (EV)", exposure);
    // Game types: blank = what the map has spawns and objects for (shown as the placeholder).
    auto* gametypes = new QLineEdit(value("iw8r_gametypes"), &dlg);
    gametypes->setObjectName("mapInfoGameTypes");
    gametypes->setPlaceholderText("detected: " + QString::fromStdString(detectedGameTypes(ctx_->doc.map())));
    gametypes->setToolTip("The game modes the game offers for this map (ui_gametype names with spaces: war dm dom sd ctf ...).\n"
                          "Leave blank to list the modes the map has spawns and objects for. Needs a client that reads map.json gameTypes.");
    form->addRow("Game modes", gametypes);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    auto num = [](QDoubleSpinBox* s) { return s->value() <= s->minimum() ? QString() : QString::number(s->value(), 'g', 6); };
    const QString values[] = {title->text().trimmed(), desc->text().trimmed(), compass->currentIndex() > 0 ? compass->currentText() : QString(),
                              num(minimap), num(ladder), num(exposure), gametypes->text().trimmed()};
    const Id wid = w->id;
    ctx_->doc.beginGroup("Map Info");
    for (int i = 0; i < 7; ++i) {
        const std::string* cur = ctx_->doc.findEntity(wid)->value(kInfoKeys[i]);
        if (values[i].isEmpty()) {
            if (cur) ctx_->doc.removeKey(wid, kInfoKeys[i]);
        } else if (!cur || QString::fromStdString(*cur) != values[i]) {
            ctx_->doc.setKeyValue(wid, kInfoKeys[i], values[i].toStdString());
        }
    }
    ctx_->doc.endGroup();
    ctx_->documentEdited("Map info updated (written to map.json on the next build)");
}

// ------------------------------------------------------------------ deploy

QString CompilePanel::loadscreenImageFile() const {
    const QString path = qs(ctx_->doc.path());
    if (path.isEmpty()) return {};
    const QFileInfo fi(path);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".loadscreen.png";
}

QStringList CompilePanel::packageFiles() const {
    const QString id = mapId();
    const QStringList names = {id + ".ff", "srv_" + id + ".ff", "eng_" + id + ".ff", "ww_" + id + ".ff", "techsets_" + id + ".ff", "map.json"};
    QStringList out;
    for (const QString& n : names) {
        const QString f = zonesDir() + "/" + n;
        if (!QFileInfo(f).isFile()) return {};
        out << f;
    }
    // Optional: the map-select preview the converter makes from the loadscreen image (1.17).
    if (const QString p = zonesDir() + "/preview.rgba"; QFileInfo(p).isFile()) out << p;
    return out;
}

QList<quint32> CompilePanel::gamePidsFrom(const QString& gameRoot) {
    QList<quint32> out;
#ifdef _WIN32
    // Any exe running from the game folder counts (the owner may run a renamed Replay exe).
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(gameRoot)).toLower() + "/";
    DWORD pids[4096];
    DWORD bytes = 0;
    if (!EnumProcesses(pids, sizeof(pids), &bytes)) return out;
    for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        wchar_t buf[MAX_PATH * 2];
        DWORD n = DWORD(std::size(buf));
        const bool got = QueryFullProcessImageNameW(h, 0, buf, &n) != 0;
        CloseHandle(h);
        if (!got) continue;
        const QString path = QDir::fromNativeSeparators(QString::fromWCharArray(buf, int(n))).toLower();
        if (path.startsWith(root) && path.endsWith(".exe")) out << quint32(pids[i]);
    }
#else
    Q_UNUSED(gameRoot);
#endif
    return out;
}

bool CompilePanel::gameRunningFrom(const QString& gameRoot) { return !gamePidsFrom(gameRoot).isEmpty(); }

namespace {
QByteArray sha256(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result();
}
}  // namespace

bool CompilePanel::deploy(const QString& gameRoot, QString* err) {
    auto bad = [&](const QString& why) {
        if (err) *err = why;
        append("deploy: " + why, true);
        return false;
    };
    const QString id = mapId();
    const QStringList files = packageFiles();
    if (files.isEmpty()) return bad("the package in " + zonesDir() + " is incomplete (five zones + map.json); run F7 first");
    {
        QFile mj(zonesDir() + "/map.json");
        if (!mj.open(QIODevice::ReadOnly) || !mj.readAll().contains(("\"" + id + "\"").toUtf8()))
            return bad("map.json does not name " + id);
    }
    if (!QFileInfo(gameRoot).isDir()) return bad("no game folder " + gameRoot);
    // Deploying while Replay runs is allowed. Only a package whose zones the game has open (the map is loaded) cannot
    // be replaced; the renames below then fail and the installed package stays as it was. The recompiled client scans
    // the packages when it starts (research 76 Q4), so a map id it has never seen needs one game restart.
    const bool running = gameRunningFrom(gameRoot);
    if (running) append("deploy: Replay is running; installing next to it (a map id that is new to the running game needs one game restart)");
    const QString maps = gameRoot + "/mods/mw120r/maps";
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString stage = maps + "/." + id + ".stage-" + stamp;
    const QString target = maps + "/" + id;
    if (!QDir().mkpath(stage)) return bad("cannot create " + stage);
    for (const QString& f : files) {
        const QString to = stage + "/" + QFileInfo(f).fileName();
        if (!QFile::copy(f, to) || sha256(f) != sha256(to)) {
            QDir(stage).removeRecursively();
            return bad("copy failed or hash mismatch for " + QFileInfo(f).fileName());
        }
    }
    QString backup;
    if (QFileInfo::exists(target)) {
        backup = gameRoot + "/.proxy/backups/mw120r-map-" + stamp + "/" + id;
        QDir().mkpath(QFileInfo(backup).absolutePath());
        if (!QDir().rename(target, backup)) {
            // The folder cannot move while any process has a handle inside it (Replay / the proxy may keep map.json or a
            // zone open after a scan). Swap the files one by one instead: each old file is renamed aside, the staged
            // file moves in; a file that cannot be renamed is really locked, and every swap so far is rolled back.
            QDir().mkpath(backup);
            QStringList swapped;  // file names already replaced
            QString locked;
            for (const QString& f : files) {
                const QString name = QFileInfo(f).fileName();
                const QString dst = target + "/" + name, old = dst + ".old-" + stamp;
                const bool had = QFileInfo::exists(dst);
                if (had && !QFile::rename(dst, old)) {
                    locked = name;
                    break;
                }
                if (!QFile::rename(stage + "/" + name, dst)) {
                    if (had) QFile::rename(old, dst);
                    locked = name;
                    break;
                }
                swapped << name;
            }
            if (!locked.isEmpty()) {
                for (const QString& name : swapped) {  // roll back
                    const QString dst = target + "/" + name, old = dst + ".old-" + stamp;
                    QFile::rename(dst, stage + "/" + name);
                    if (QFileInfo::exists(old)) QFile::rename(old, dst);
                }
                QDir(stage).removeRecursively();
                QDir(backup).removeRecursively();
                return bad(QString("%1 in %2 is open in another process%3; the installed package is unchanged")
                               .arg(locked, target,
                                    running ? QString(" (Replay has the map loaded: load another map, then deploy again)") : QString()));
            }
            // Old files go to the backup folder (a file still open stays as .old-* and is removed next time).
            for (const QString& name : swapped) {
                const QString old = target + "/" + name + ".old-" + stamp;
                if (QFileInfo::exists(old) && !QFile::rename(old, backup + "/" + name)) QFile::remove(old);
            }
            for (const QFileInfo& stale : QDir(target).entryInfoList({"*.old-*"}, QDir::Files)) QFile::remove(stale.absoluteFilePath());
            QDir(stage).removeRecursively();
            lastInstalled_.clear();  // per-file swap: Undo Deploy is not offered (the backup folder holds the old files)
            lastBackup_.clear();
            append(QString("deploy: installed %1 by replacing its %2 files in place (the folder was held open); previous files in %3")
                       .arg(target).arg(files.size()).arg(backup), true);
            append("To play: press F5 (it starts " + id + " for you), or start it from a private match lobby you host.");
            ctx_->setStatus("Deployed " + id + " to " + target);
            return true;
        }
    }
    if (!QDir().rename(stage, target)) {
        if (!backup.isEmpty()) QDir().rename(backup, target);  // roll back
        QDir(stage).removeRecursively();
        return bad("cannot move the staged package into place; previous package restored");
    }
    lastInstalled_ = target;
    lastBackup_ = backup;
    append(QString("deploy: installed %1 (%2 files)%3").arg(target).arg(files.size()).arg(backup.isEmpty() ? QString() : ", previous package kept in " + backup), true);
    append("To play: press F5 (it starts " + id + " for you), or start it from a private match lobby you host. In-game loading is not verified by the editor.");
    ctx_->setStatus("Deployed " + id + " to " + target);
    return true;
}

bool CompilePanel::undoDeploy(QString* err) {
    if (lastInstalled_.isEmpty()) {
        if (err) *err = "nothing deployed in this session";
        return false;
    }
    // Rename first (fails as a whole if Replay has the zones open), then restore, then delete: a locked file
    // can never leave a half-removed package behind.
    const QString trash = QFileInfo(lastInstalled_).absolutePath() + "/." + QFileInfo(lastInstalled_).fileName() + ".undo-" +
                          QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    if (!QDir().rename(lastInstalled_, trash)) {
        if (err) *err = "the deployed package is in use by Replay (the map is loaded); load another map, then undo again";
        return false;
    }
    if (!lastBackup_.isEmpty() && !QDir().rename(lastBackup_, lastInstalled_)) {
        QDir().rename(trash, lastInstalled_);  // put the deployed package back
        if (err) *err = "could not restore " + lastBackup_ + "; the deployed package is still installed";
        return false;
    }
    QDir(trash).removeRecursively();
    append("deploy: undone (" + lastInstalled_ + (lastBackup_.isEmpty() ? QString(" removed)") : QString(" restored from backup)")), true);
    lastInstalled_.clear();
    lastBackup_.clear();
    return true;
}

}  // namespace iw8r::qt

namespace iw8r::qt {

bool CompilePanel::eventFilter(QObject* o, QEvent* e) {
    // Error round trip (7.13, core log_links): the double-clicked log line's references are resolved in the open map.
    if (log_ && o == log_->viewport() && e->type() == QEvent::MouseButtonDblClick) {
        const QTextCursor c = log_->cursorForPosition(static_cast<QMouseEvent*>(e)->position().toPoint());
        if (!scriptLineJump || !scriptLineJump(c.block().text())) selectFromLogLine(c.block().text());
        return true;
    }
    return QWidget::eventFilter(o, e);
}

int CompilePanel::selectFromLogLine(const QString& line) {
    std::set<Id> ids;
    QString what;
    for (const LogRef& r : findLogRefs(line.toStdString())) {
        for (const Id id : resolveLogRef(ctx_->doc.map(), r)) ids.insert(id);
        if (what.isEmpty()) what = QString::fromStdString(r.value);
    }
    if (ids.empty()) {
        ctx_->setStatus(what.isEmpty() ? QString("Nothing in that line names a map entity") : "Nothing in the open map matches " + what);
        return 0;
    }
    ctx_->doc.deselectAll();
    for (const Id id : ids) {
        const Entity* e = ctx_->doc.findEntity(id);
        if (!e) continue;
        bool prim = false;
        for (const auto& it : e->items)
            if (it.isPrimitive()) {
                ctx_->doc.select({id, it.id}, true);
                prim = true;
            }
        if (!prim) ctx_->doc.select({id, kNoId}, true);
    }
    ctx_->documentEdited(QString("Selected %1 entit%2 named by the log line (%3)").arg(ids.size()).arg(ids.size() == 1 ? "y" : "ies").arg(what));
    ctx_->frameSelection();
    return int(ids.size());
}

}  // namespace iw8r::qt
