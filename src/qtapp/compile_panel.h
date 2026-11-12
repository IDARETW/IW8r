// IW8 Radiant Suite (Qt shell) - Compile dock: map source -> IW8 extraction (native, in-process) ->
// Replay 1.20 zones (x64-zt buildextraction) -> ACTS validation. Runs asynchronously; the editor stays usable.
#pragma once

#include <optional>
#include <set>
#include "model_import.h"
#include <QString>
#include <QMap>
#include <QElapsedTimer>
#include <QWidget>
#include <deque>
#include <functional>

#include "editor_context.h"
#include "editor_log.h"
#include "prefab_bundle.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QCheckBox;

namespace iw8r::qt {

class CompilePanel : public QWidget {
    Q_OBJECT
public:
    enum class Stage { Extraction, Zones, Validate };
    using ToolPathFn = std::function<QString(const QString&)>;

    CompilePanel(EditorContext* ctx, ToolPathFn toolPath, QWidget* parent = nullptr);
    ~CompilePanel() override;

    // Queues stages and starts them (no-op while busy). Emits finished() when the queue drains or a stage fails.
    void run(const std::vector<Stage>& stages);
    void cancel();
    bool busy() const { return running_; }

    QString mapId() const;      // validated IW8 map id (mp_<name>, lower case)
    QString buildRoot() const;  // <map dir>/iw8build/<map id> (or the app-data builds folder for untitled maps)
    QString extractionDir() const { return buildRoot() + "/extraction"; }
    QString navDebugFile() const { return buildRoot() + "/nav_debug.json"; }  // x64-zt IW8_ZONETOOL_NAV_DEBUG dump (6.23)
    // x64-zt writes <output root>/<map id>/*.ff plus a large render cache in the output root, so every map
    // shares one output root (docs/research/17 §5).
    QString outputRoot() const;
    QString zonesDir() const { return outputRoot() + "/" + mapId(); }
    QString logText() const;
    bool lastOk() const { return lastOk_; }

    static QString sanitizeMapId(const QString& raw);
    // Mapper asset folders for the build: custom materials (*.iw8mat.json) and custom models (*.obj).
    void setAssetRoots(std::function<QStringList()> materialRoots, std::function<QStringList()> modelRoots,
                       std::function<double()> modelScale);
    // Stock textures in builds: material -> cached colour DDS ("" = not available yet; the source may start a fetch).
    void setStockImageSource(std::function<QString(const QString&)> colorDds);
    // Why a stock material cannot get its colour image ("" = not resolved yet, it may still arrive).
    void setStockStatusSource(std::function<QString(const QString&)> status) { stockStatus_ = std::move(status); }
    // Cut-out mask DDS of a stock material (`_sdf`), "" when it has none.
    void setStockMaskSource(std::function<QString(const QString&)> maskDds) { stockMask_ = std::move(maskDds); }
    // True for stock materials whose technique set is blended (decals, glass): built with soft coverage (techset _b0).
    void setStockBlendSource(std::function<bool(const QString&)> blended) { stockBlend_ = std::move(blended); }
    // Packed normal + gloss image of a stock material ("" = none): built as the material's normal and specular maps (4.14).
    void setStockNormalSource(std::function<QString(const QString&)> normalDds) { stockNormal_ = std::move(normalDds); }
    // Called at the start of every build that writes the extraction (research 73: probes were flat grey unless the
    // mapper had run Capture Reflection Probes by hand, so they made no visible difference in game).
    void setBeforeExtraction(std::function<void()> fn) { beforeExtraction_ = std::move(fn); }
    // Replay surfaceFlags of a stock material (-1 = unknown): its real surface type for impacts and footsteps.
    void setStockSurfaceSource(std::function<qint64(const QString&)> flags) { stockSurface_ = std::move(flags); }
    // Stock xmodel mesh in map units (Z-up inches) from the editor's ACTS model cache, or nullopt when it is not cached
    // yet (the source should request it so the next build has it).
    using StockModelFn = std::function<std::optional<ImportedModel>(const QString& name)>;
    void setStockModelSource(StockModelFn fn) { stockModel_ = std::move(fn); }
    // Sky DDS for the worldspawn `skyname` (doc 33): the cached stock sky image, or "" when it is not cached yet (the
    // source should start its export so the next build has it).
    void setSkySource(std::function<QString(const QString& skyname)> fn) { skySource_ = std::move(fn); }
    // Writes <build>/stock_materials: one PNG (max side `maxSide`) + .iw8mat.json per stock editor material whose
    // colour image is available. Returns the folder ("" when none) and counts.
    QString prepareStockMaterials(int maxSide, int* made, int* missing, const std::set<std::string>& extra = {});
    MapFile buildMap_;  // the document with prefabs expanded (runExtraction): what the build actually contains
    QStringList missingStock_;  // materials prepareStockMaterials could not build this time (named in the log)
    QStringList lowResStock_;   // 4.25: built from a cached colour image smaller than the stock one (streamed)

    // Deploy the built package (five zones + map.json) into <gameRoot>\mods\mw120r\maps\<id>\, the folder the
    // mw120rproxy custom-map loader scans (docs/research/03 §5). Staged copy with SHA-256 checks; a previous
    // package is moved to <gameRoot>\.proxy\backups\mw120r-map-<stamp>\<id> and restored on failure.
    // Refuses while a Replay exe from that root is running. undoDeploy() restores the last backup.
    bool deploy(const QString& gameRoot, QString* err);
    bool undoDeploy(QString* err);
    // 7.13: tried first on a double-clicked log line (script line jumps); returns true when it handled the line.
    std::function<bool(const QString& line)> scriptLineJump;
    QStringList packageFiles() const;  // the five zones + map.json (+ preview.rgba), or empty when the package is incomplete
    // <map folder>/<map name>.loadscreen.png (Compile > Set Loadscreen From Camera; used by the next build when present).
    QString loadscreenImageFile() const;
    static bool gameRunningFrom(const QString& gameRoot);
    // PIDs of every exe running from the game folder (any name: a renamed Replay exe counts).
    static QList<quint32> gamePidsFrom(const QString& gameRoot);
    // A line in the compile log from outside the panel (F5 devmap progress, devmap_launch.*).
    void appendLog(const QString& line, bool strong = false) { append(line, strong); }

signals:
    void finished(bool ok);

private:
    void next();
    void fail(const QString& why);
    void append(const QString& line, bool strong = false);
public:
    // Selects what a compile log line names (7.13); returns how many entities. Also used by the GUI test.
    int selectFromLogLine(const QString& line);
    // Log level of a compile log line (Console, toasts): errors and failures, not "0 failed" summaries.
    static LogLevel lineLevel(const QString& line);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    bool runExtraction();
    void startProcess(const QString& title, const QString& exe, const QStringList& args, const QString& workDir);
    void startValidation();
public:
    // Map info for the package's map.json (x64-zt --metadata, convert.cpp:175-230), stored as worldspawn keys
    // iw8r_title / iw8r_description / iw8r_compass_rotation / iw8r_minimap_scale / iw8r_ladder_rung_distance /
    // iw8r_exposure_adjust so it saves with the map and is undoable.
    void editMapInfo();
    QByteArray metadataJson() const;  // what --metadata receives (id always set)
    // Writes the map's game types into the built package's map.json ("gameTypes": "war dm"; core map_gametypes). The
    // converter writes map.json from fixed fields, so the editor adds this line after the zones stage. Returns the
    // list, or empty when there is no package yet.
    QString writePackageGameTypes(bool log = true);
private:
    void refreshMapId();

    EditorContext* ctx_;
    ToolPathFn toolPath_;
    QLineEdit* mapId_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QProcess* proc_ = nullptr;
    QString procTitle_;
    QStringList pendingValidate_;  // zones left to validate in this run
    std::deque<Stage> queue_;
    // 7.10 build time report: milliseconds per stage of the current run ("extraction", "zones", "validate"), in order.
    void markStage(const QString& label);
    QString buildTimeLine() const;
    QElapsedTimer stageClock_, totalClock_;
    QString stageLabel_;
    QStringList stageOrder_;
    QMap<QString, qint64> stageMs_;

public:
    QString lastBuildTimes() const { return lastBuildTimes_; }  // tests and the build report

    // 7.10: the next run rebuilds the zones even when the extraction is unchanged (Compile > Force Full Rebuild).
    void forceFullRebuild();
    bool lastRunReusedZones() const { return reusedZones_; }

private:
    QString lastBuildTimes_;
    QString zonesKey() const;     // extraction hash + converter exe + map id
    QString zonesKeyFile() const;  // <build root>/zones.key, written after a validated build
    QString pendingKey_;
    bool reusedZones_ = false;
    bool running_ = false;
    bool lastOk_ = false;
    bool userEditedId_ = false;
    std::function<QStringList()> materialRoots_, modelRoots_;
    // Prefab bundles (docs/PREFAB_BUNDLES.md): the asset folders of the bundles the open map places, found at the
    // start of every extraction. buildMaterialRoots / buildModelRoots are the mapper's folders plus these.
    PrefabAssetRoots bundleRoots_;
    QStringList buildMaterialRoots() const;
    QStringList buildModelRoots() const;
    std::function<double()> modelScale_;
    std::function<QString(const QString&)> stockColor_;
    std::function<QString(const QString&)> stockStatus_;
    StockModelFn stockModel_;
    std::function<QString(const QString&)> skySource_;
    std::function<QString(const QString&)> stockMask_;
    std::function<bool(const QString&)> stockBlend_;
    std::function<QString(const QString&)> stockNormal_;
    std::function<void()> beforeExtraction_;
    std::function<qint64(const QString&)> stockSurface_;
    class QCheckBox* stockTextures_ = nullptr;
    QString lastInstalled_, lastBackup_;  // for undoDeploy
};

}  // namespace iw8r::qt
