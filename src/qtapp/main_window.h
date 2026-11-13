// IW8 Radiant Suite (Qt shell) - main window: menus, rebindable commands, dock layout, file I/O.
#pragma once
#include "command_script.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSet>
#include <QMap>
#include "camera_path.h"
#include <QTimer>

#include <optional>

#include "xmodel_info.h"
#include <map>
#include <memory>
#include <set>

#include "custom_materials.h"
#include "editor_context.h"
#include "model_import.h"
#include "prefab_bundle.h"

class QLabel;
class QLineEdit;
class QMenu;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace ads {
class CDockManager;
class CDockWidget;
}  // namespace ads

#include "stock_loot.h"
#include "stock_placed.h"
#include "stock_smodels.h"
#include "stock_prop_fetch.h"

namespace iw8r {
struct GameModePreset;
}
namespace iw8r::qt {
class FxEditorDialog;
}
namespace iw8r::qt {

class CameraView;
class WorldCache;
class GridView;
class EntityInspector;
class AssetBrowser;
class CompilePanel;
class FindReplaceDialog;
class KeyReplaceDialog;
class LiveLinkPanel;
class WebServer;
class UpnpPortMapper;
class ReplayLogPanel;
class LogPanel;
class ScriptPanel;
class OutlinerPanel;
class ToastHost;
class LayersPanel;
class MaterialCache;
class ModelCache;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent = nullptr);
    ~MainWindow() override;

    EditorContext& context() { return ctx_; }
    ScriptPanel* scriptPanel() const { return scriptPanel_; }
    OutlinerPanel* outliner() const { return outliner_; }
    CameraView* cameraView() const { return camera_; }
    GridView* gridView() const { return grid_; }
    EntityInspector* inspector() const { return inspector_; }
    AssetBrowser* materialBrowser() const { return materials_; }
    AssetBrowser* modelBrowser() const { return models_; }
    CompilePanel* compilePanel() const { return compile_; }
    LogPanel* logPanel() const { return logPanel_; }    // Console dock
    ToastHost* toasts() const { return toasts_; }       // bottom-right toast notifications
    QAction* action(const QString& command) const { return actions_.value(command); }
    int lastShortcutRows() const { return lastShortcutRows_; }  // rows of the last Help > Keyboard Shortcuts sheet
    // Stock sky names for the Sun & Sky dialog (filled once the sky route lands; free text is always allowed).
    QStringList stockSkyList() const;  // stock sky names (stock_sky.h) for the Sun & Sky dialog
    void showDock(const QString& name);  // opens a panel and makes it the current tab of its area

    void openPath(const QString& path);  // asks to save unsaved changes first
    bool saveTo(const QString& path, QString* err = nullptr);  // Save As without a dialog
    // Runs ACTS on a Replay 1.20 zone (map_ents only, cached) and opens its entity string.
    bool openStockZone(const QString& zonePath, QString* err = nullptr);
    // Stock map editing (6.1): spawn records as document entities, and the scratch zone rebuild
    // (tools/m2_rebuild_mapents.ps1 with the entity string and, after editing spawns, the spawnList).
    bool editStockSpawns();
    // 13.5: the loot points of the open Warzone stock map become entities (class iw8r_loot_point); Rebuild Stock Zone
    // writes them back. Returns how many were added; 0 when they already are entities; -1 without a stock map.
    int editLootPoints();
    size_t stockLootPointCount() const { return stockLoot_.points.size(); }
    // 13.5: the placed objects of the open stock map that can be edited (the Warzone buy stations) become
    // `scriptable` entities; Rebuild Stock Zone writes moves and new ones. Returns how many were added; 0 when they
    // already are entities; -1 without a stock map.
    int editPlacedObjects();
    size_t stockPlacedObjectCount() const { return stockPlaced_.size(); }
    // Selection sets (8.7): saved by name, restored from Edit > Selection Sets > Restore.
    void saveSelectionSet(const QString& name);
    bool restoreSelectionSet(const QString& name);
    // Prefab editing (8.5): Enter opens the selected misc_prefab's .map (the parent is saved/asked first and kept on a
    // stack, so prefabs inside prefabs work); Leave saves/asks for the prefab and reopens the parent with the camera
    // where it was, so every instance shows the edit.
    // Shape models (Primitives > Shape Models): writes the shape's OBJ (current material) into the map's models folder
    // once and places a static misc_model at the view centre. The map must be saved (the models folder is beside it).
    bool addShapeModel(int kind, QString* placedName = nullptr);
    LiveLinkPanel* liveLinkPanel() const { return liveLink_; }
    // Browser access (L10): starts/stops the web server from Settings web/* (enabled, port, lan, upnp).
    bool applyWebServerSettings(QString* message = nullptr);
    WebServer* webServer() const { return web_; }
    // Reflection probe capture (5.5): renders every probe position (the world-default probe 0 at the brush bounds
    // centre, then each reflection_probe in map order) into <build>/probe_capture; the next compile uses them.
    int captureReflectionProbes(QString* dir = nullptr);
    // Camera context menu (3.7): built for a widget position (exposed for --gui-test, which inspects it without exec).
    QMenu* buildCameraContextMenu(const QPointF& widgetPos);
    // Material Info / Model Info windows (4.16 / 4.18); the HTML is returned for the GUI test.
    QString showMaterialInfo(const QString& name);
    QString showModelInfo(const QString& name);
    QString showEffectInfo(const QString& name);
    QImage materialImage(const QString& material);  // colour image of a stock or custom material (UV editor, 2.27)
    class UvEditor* openUvEditor();
    class PortWizardDialog* openPortWizard();  // File > Port CoD4 Map (11.9)
    // 11.8: the stock reference triangles inside the selection's bounds become a custom OBJ model (models folder) placed
    // as a misc_model where they were; returns the triangle count (0 with a status message when it cannot).
    size_t exportReferenceRegion();
    // 11.8: stock triangles in the selection bounds as thin prism brushes (a func_group, one undo step).
    size_t exportReferenceBrushes();
    // 5.12: selected worldspawn brushes -> a script_brushmodel glass pane with the built-in iw8r_glass material.
    Id makeGlassPane();
    // A climbable ladder (owner playtest 2026-11-11: the ladder brush is invisible, so the ladder could not be seen):
    // an invisible ladder brush plus a stock ladder model at the same place, 160 units tall or as tall as the selection.
    // Returns the model entity (kNoId on failure). One undo step.
    Id makeLadder();
    // 8.8 / 2.24: Brush > Convert Selection to Static Model (one OBJ under <map>/models, placed as a misc_model).
    size_t bakeSelectionToModel(QString* err = nullptr);
    // 8.8 exposed parameters: File > Prefab Parameters lists the keys of the selected prefab's entities and its
    // materials with this instance's overrides (iw8r_ovr / iw8r_mat keys on the misc_prefab).
    struct PrefabParam {
        bool material = false;    // a material swap row (key = the prefab's material)
        std::string selector;     // targetname or class=<c>
        std::string key, prefabValue, override;
        bool overridden = false;
    };
    std::vector<PrefabParam> prefabParameters(Id prefabEntity, QString* err = nullptr) const;
    bool setPrefabParameter(Id prefabEntity, const PrefabParam& p, const std::string& value, bool clear);
    void showPrefabParameters();
    QString writeYupObj(const std::string& zupObj, const QString& stem, QString* err);  // returns the model name
    // 3.21: the camera view rendered off-screen at w x h and saved as an image (clean = game surfaces only).
    bool exportCameraImage(const QString& path, int w, int h, bool clean, QString* err = nullptr);
    // 3.13: selection tint colour and strength in the camera (saved).
    void setSelectionHighlight(const QColor& c, double amount);
    // 3.22: View > Four-View Layout (camera + top / front / side 2D views).
    void fourViewLayout();
    // 2.28: Edit > Copy / Cut / Paste (system clipboard, map text; core clipboard_ops).
    size_t copySelection(bool cut);
    size_t pasteFromClipboard(QString* err = nullptr);
    // 4.17: File > New / Edit Custom Material (MaterialEditorDialog); `drive` fills and saves it without showing it
    // (tests). Returns the definition written ("" = cancelled / failed).
    QString editCustomMaterial(const QString& name, std::function<void(class MaterialEditorDialog&)> drive = {});
    // 7.8: the player's feet and game angles through the direct link (nullopt + why when there is no game / player);
    // selection along a view and moving the selection to a point (also used by the GUI test without a game).
    std::optional<std::pair<Vec3, Vec3>> playerView(QString* err = nullptr);
    bool selectAlongView(const Vec3& feet, const Vec3& gameAngles);
    bool moveSelectionTo(const Vec3& feet);
    // 7.13: a log line naming a .gsc file and a line jumps to that line of the map's script (Script dock). True when the
    // line was a script reference (handled, even when it points into the generated part).
    bool jumpToScriptLine(const QString& line);
    // 8.12: Primitives > Generate Stairs / Building in the selection's bounds (core structure_generators); the selected
    // worldspawn brushes are replaced; one undo step. direction: 0 +X, 1 -X, 2 +Y, 3 -Y. Returns brushes made.
    int generateStructure(bool building, int direction, int floors, QString* err = nullptr);
    // 6.18: Tools > Generate Spawns (core spawn_generator) as one undo step; returns the number of entities added.
    int generateSpawnSet(int ffaCount, int startsPerSide, bool replace, QString* message = nullptr);
    // 4.22: Replace Everywhere (material on every face / model key on every entity; one undo step) and the custom
    // assets the map does not use ("material  name  (file)" / "model  name  (file)").
    size_t replaceEverywhere(bool material, const QString& from, const QString& to);
    QStringList unusedCustomAssets();
    AssetBrowser* materialsBrowser() const { return materials_; }  // tests
    // 2.21: Patch > Patch Inspector (modal; true when edits were applied).
    bool showPatchInspector();
    // 9.14: autosave history (timestamped copies, rotation, low-disk guard) and its browser (File > Autosave History).
    QString autosaveDir() const;
    QString autosaveNow(QString* message = nullptr);  // path written, or empty (message says why)
    void showAutosaveHistory();
    // 4.26: map source package (zip): the map, its prefabs, custom materials / models and sidecars. Export returns the
    // file count (0 on failure; *err also lists references that were not found). Import extracts into destDir, adds
    // <destDir>/prefabs as a prefab root and returns the package's map (empty on failure).
    int exportMapPackage(const QString& zipPath, QString* err = nullptr);
    QString importMapPackage(const QString& zipPath, const QString& destDir, QString* err = nullptr);
    // 1.17: a clean 16:9 shot of the camera saved as <map>.loadscreen.png (2048 x 1024, stretched back in game); the
    // next build writes it as loadscreen_<id> and the converter makes the map-select preview from it. Returns the path.
    QString setLoadscreenFromCamera(QString* err = nullptr);
    // 8.15: the current camera path rendered frame by frame (fps) into dir/frame_00000.png ...; returns the count.
    int exportCameraPathFrames(const QString& dir, double fps, int w, int h, QString* err = nullptr);
    // 3.21: a turntable of the selection (a model, a prefab, brushes): `frames` views around its bounds, 20 degrees
    // above it, into dir/turntable_00000.png ...; returns the count (0: nothing selected).
    int exportTurntable(const QString& dir, int frames, int w, int h, QString* err = nullptr);
    // Headless builds (--compile): fetch every stock model and stock material the map uses (prefabs expanded, the
    // models' own materials included) into the caches and wait until they settle, so one build is complete.
    // Returns a one-line summary.
    QString prefetchStockAssets(int timeoutMs);
    // 6.10: File > Rebuild Stock Map With These Entities: the open entity text into <zone> (m2_rebuild_mapents.ps1, under
    // out/m2-<zone>-<time>; the game folder is only read). Returns the work folder; start = false only prepares it.
    QString rebuildStockEntities(const QString& zone, QString* err = nullptr, bool start = true);
    // 11.6: File > Relight Stock Map: fog values of a stock zone's vision, rebuilt and validated (tools/m2_rebuild_mapents.ps1).
    void relightStockMapDialog();
    static QString writeVisionEdits(const QString& file, double fogStart, double fogDensity, const QColor& fogColor, double maxOpacity);
    // Aims worldspawn sundirection at the sun of the current sky (core sky_sun), one undo step. Empty on success,
    // else why not (no sky, not exported yet: then it runs when the export arrives).
    QString sunFromSky(bool automatic = false);
    QString currentSkyFile(QString* skyName = nullptr);  // 8.9: from the converter's stock vfx exports (x64zt_output/_stock_cache)
    // Main toolbar (9.2): the commands listed in Settings ui/toolbar (View > Customize Toolbar).
    void rebuildMainToolbar();
    static QStringList defaultToolbarCommands();
    bool enterPrefab();
    bool leavePrefab();
    int prefabDepth() const { return int(prefabStack_.size()); }
    // Writes <work>/edit.ents (+ edit.spawns.ents) and returns the pipeline arguments; false without a stock map.
    bool prepareStockRebuild(const QString& work, QStringList* args, QString* err);
    // The fast path (converter editzone, research 88): writes <work>/edit.ents and <work>/edits.json and gives the
    // converter call. `notes`: what the rebuild will carry or hide, and warnings.
    // 11.4: the brushes added to a stock map are built first as models of a small helper map (the editor's own
    // headless build), then carried into the stock zone. `overlay` (optional) gets that build: the helper map and the
    // folder for its result; without it brushes are left out with a note.
    struct StockOverlayPlan {
        QString map;        // the helper map (empty: no brushes to build)
        QString resultDir;  // --out of the headless build (compile-result.txt names the package folder)
        QStringList models;
    };
    bool prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes = nullptr,
                          StockOverlayPlan* overlay = nullptr);
    void rebuildStockZoneFast();
    void runStockEdit(const QString& work, const QString& program, const QStringList& args);  // the editzone step
    // 11.5: the stock props (static models of the reference) whose origin lies inside the selection bounds are
    // hidden in the rebuilt map (worldspawn iw8r_hide_smodels) and leave the reference display. Returns how many
    // were added; -1 without a stock reference or a selection.
    int hideStockPropsInSelection();
    int showAllStockProps();  // clears the list; returns how many were hidden
    // GUI test seam: the reference props of the open stock map, as the background loader would hand them over.
    void setReferencePropsForTest(std::vector<StockModelInstance> props);
    // Owner actions (File menu): copy the patch files of the last stock rebuild into the game's zone folder (a stock
    // patch of the same name is backed up first), and take them out again. Both ask before they write.
    void installEditedStockMap();
    // Big maps (Quarry, Verdansk): the streamed world tiles around the camera are exported and drawn in the stock
    // reference (View > Load Streamed World Around Camera). Returns how many tiles were asked for; -1 without a big map.
    int loadStreamedWorldAroundCamera();
    // The zone file to read the stock map `zone` (a file of the game's zone folder) from: the file itself, or, while
    // an edit of it is installed (the patch beside it is ours), a copy with the saved stock patch under the app data
    // folder; `*tempDir` then names the folder to delete afterwards. "" (and *err) when the copy cannot be made.
    QString stockZoneForReading(const QString& zone, QString* tempDir, QString* err);
    // Map > Game Modes: presets (game type + dvars) for F5, baked into the map, and the Warzone start entities.
    void showGameModes();
    // Applies a preset to the open map (worldspawn iw8r_mode_dvars / iw8r_br, and the game type joins iw8r_gametypes
    // when that key is set). Returns false without a worldspawn.
    bool bakeGameMode(const GameModePreset& preset);
    // Adds what the br game type needs to start on a small map: spawn points and one prematch insertion point around
    // the 2D view centre. Returns the number of entities added.
    int insertWarzoneStart();
    // 13.6: writes the game type template <map folder>/gametypes/<name>.gsc (kept when it exists) and points worldspawn
    // iw8r_gametype_script at it. Returns the file, or "" with *err.
    QString newGameTypeScript(const QString& name, QString* err = nullptr);
    void restoreStockMap();
    QString stockBackupDir(const QString& zone) const;
    QString stockZone() const { return stockZone_; }
    // Prefabs (also used by --gui-test): a misc_prefab at the 2D view centre, explode, save selection.
    // `file` is a plain .map prefab, or a prefab bundle (its .iw8prefab folder, or a file inside it).
    bool insertPrefab(const QString& file, QString* err = nullptr);
    // The document edit of insertPrefab. `normal`: the outward normal of the surface at `at` (placement hints).
    Id addPrefabPlacement(const QString& file, const Vec3& at, QString* err, const std::optional<Vec3>& normal = std::nullopt);
    // Prefab sharing (placement hints, zip, plain map to bundle). Return "" / false with *err on failure.
    QString convertPlainPrefabToBundle(const QString& mapPath, QString* err = nullptr);  // the bundle folder written
    bool exportPrefabBundleAsZip(const QString& bundleFolder, const QString& zipPath, QString* err = nullptr);
    QStringList importPrefabBundlesFromZip(const QString& zipPath, QString* err = nullptr);  // the bundle folders written
    // Turns the selected misc_prefab placements by their bundle's yaw step (90 for a plain prefab); one undo step.
    int rotateSelectedPrefabsByStep();
    bool explodeSelectedPrefab(QString* err = nullptr);
    bool saveSelectionAsPrefab(const QString& file, QString* err = nullptr);  // the old form: one plain .map file

    // ---- Prefab bundles (docs/PREFAB_BUNDLES.md) ----
    // The bundle a misc_prefab entity places. False for anything else (a plain .map prefab, a missing folder). A
    // bundle that cannot be placed is returned too (out.placeable is false, out.problem says why).
    bool prefabBundleOf(Id prefabEntity, PrefabBundle& out) const;
    // The asset folders of the bundles the open map places. They join the mapper's own folders for the material
    // library and the model lookup of the open map, so a bundle's materials and models draw while editing.
    const PrefabAssetRoots& bundleAssetRoots() const { return bundleRoots_; }
    QStringList lookupMaterialRoots() const;  // customMaterialRoots() plus the bundles' material folders
    QStringList lookupModelRoots() const;     // customModelRoots() plus the bundles' model folders
    // A bundle parameter (manifest `parameters`) of a placement: written with prefabParameterKey, one undo step.
    // An empty optional takes the key away again (the bundle's default applies).
    bool setBundleParameter(Id prefabEntity, const PrefabParameter& p, const std::optional<std::string>& value);
    // Selected placements whose placement id (iw8r_pid) another placement of the map also holds get a new one.
    // Returns how many changed. Clone, Paste and the Arrange duplicates call it inside their undo step.
    size_t renewPlacementIds();
    bool cloneSelection(const Vec3& offset);  // Edit > Clone: the copies of bundle placements get new ids (one undo step)
    // Runs an edit that copies the selection (the copies end up selected); when the selection holds placements with
    // an id, the edit and the new ids are one undo step called `undoName`. Returns what `edit` returned.
    bool duplicateWithNewIds(const std::string& undoName, const std::function<bool()>& edit);
    // Save Selection As Prefab: the selection becomes a bundle folder in prefabSaveFolder().
    struct PrefabBundleSave {
        QString id, title, description, category;
        QStringList tags;
        std::vector<Id> alsoEntities;    // linked entities outside the selection that should travel too
        bool replaceSelection = true;    // the selection becomes one placement of the new bundle
        bool overwrite = false;          // replace a bundle that is already there
    };
    // Where new bundles are written: the first prefab folder of Edit > Preferences, else `prefabs` beside the map
    // ("" with a reason when neither exists: an unsaved map and no prefab folder).
    QString prefabSaveFolder(QString* err = nullptr) const;
    // What would be saved for the selection (nothing is written, nothing changes).
    PrefabSaveResult scanSelectionForBundle(const PrefabBundleSave& s) const;
    // The folder a save with this id and category writes; *exists: a bundle is already there (same id or same folder).
    QString bundleFolderFor(const QString& id, const QString& category, bool* exists = nullptr) const;
    // Writes the bundle (manifest, contents, the mapper's own asset files, thumb.png) and returns its folder; "" on
    // failure. Refuses to replace an existing bundle unless s.overwrite is set.
    QString saveSelectionAsBundle(const PrefabBundleSave& s, QString* err = nullptr);
    void saveSelectionAsBundleDialog();
    // The bundle folder of the selected placement ("" when the selection is not one), and the Explorer window on it.
    QString selectedBundleFolder() const;
    bool openSelectedBundleFolder();
    // Copies the selected placement's prefab (bundle folder or plain .map) into `prefabs` beside the map under a new
    // id and points this placement at the copy (one undo step). The map must be saved.
    bool makeSelectedPrefabUnique(QString* err = nullptr);
    // Drag preview: while a prefab is dragged over the camera, a provisional placement with its real contents follows
    // the cursor (the newest undo step); leaving the view takes it back, dropping keeps it.
    bool previewPrefabAtPoint(const QString& file, const Vec3& at);
    void endPrefabPreview(bool keep);
    bool prefabPreviewActive() const { return prefabPreview_.active; }
    class PrefabBundlePanel* bundlePanel() const { return bundlePanel_; }  // Entity dock: bundle description and parameters
    // Radiant Region (View > Region) and File > Save Selected / Save Region (also used by --gui-test).
    bool setRegionFromSelection(QString* err = nullptr);
    bool setRegionFromXYView(QString* err = nullptr);  // visible grid area in X/Y (full depth), see grid_view.h
    void clearRegion();
    bool saveSelectedAs(const QString& file, QString* err = nullptr);
    bool saveRegionAs(const QString& file, QString* err = nullptr);
    std::shared_ptr<const MapFile> loadPrefab(const std::string& model);  // cached per resolved path
    FindReplaceDialog* findDialog() const { return findDialog_; }
    LayersPanel* layersPanel() const { return layers_; }
    // Mapper assets: <map dir>/materials and <map dir>/models (or app data for an untitled map) + Settings extras.
    QStringList customMaterialRoots() const;
    QStringList customModelRoots() const;
    void reloadCustomAssets();
    bool importTexture(const QString& image, const QString& name, QString* err = nullptr);  // returns the new material
    bool importModel(const QString& obj, QString* placedName = nullptr, QString* err = nullptr);
    const MaterialLibrary* customMaterials() const { return customMaterials_.get(); }
    MaterialCache* materialCache() const { return matCache_; }  // tests

    // Edit > Preferences (9.2): one place for the settings that were spread over menus and Tool Paths.
    struct Preferences {
        double cameraSpeed = 600, cameraFov = 90;
        int autosaveMinutes = 5;  // 0 = off
        bool toasts = true, textureLock = false, fogPreview = true;
        double modelScale = 1.0;
        QStringList materialRoots, modelRoots, prefabRoots;
    };
    Preferences currentPreferences() const;
    void applyPreferences(const Preferences& p);  // applies now and saves
    void showPreferences();
    class PrefabBrowser* prefabBrowser() const { return prefabs_; }
    AssetBrowser* modelsBrowser() const { return models_; }

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    MacroRecorder macro_;
    std::string lastSunSent_;
    std::shared_ptr<const StockWorld> stockWorld_;  // the loaded stock map reference (11.8 region export)
    std::map<std::string, std::vector<Vec3>> tagCache_;  // stock model -> tags (model space), scene.tagsFor  // worldspawn sun keys last seen by the hot sun channel
    bool macroRecording_ = false;
    QAction* addCommand(QMenu* menu, const QString& command, const QString& text, const QKeySequence& def,
                        const std::function<void()>& fn);
    void buildMenus();
    void buildDocks();
    void runNudge(int dh, int dv);
    bool confirmDiscard();
    void loadDocument(const QString& path);
    bool save();
    bool saveAsDialog(bool copyOnly);
    void openDialog();
    void openStockZoneDialog();   // runs ACTS on a Replay zone (map_ents only) and opens the result
    void insertPrefabDialog();
    void saveSubsetDialog(bool regionOnly);  // Save Selected (false) / Save Region (true): dialog, then saveSelectedAs / saveRegionAs
    QString prefabModelFor(const QString& file) const;
    Vec3 placementPoint() const;
    // Camera bookmarks (3.22): slot 1..9 per map file, kept in the settings ("x y z yaw pitch").
    QString bookmarkKey(int slot) const;
public:
    bool saveCameraBookmark(int slot);
    // Spline tools (2.26): brushes along the selected point entities' origins (in entity order). kind: wall, posts, pipe.
    size_t splineFromSelection(const QString& kind, double a, double b, bool closed);
    // Tools > Run Command Script (9.10): runs `text` on the document; returns the result line for the status bar.
    QString runScriptText(const QString& text, bool oneUndoStep, QStringList* log = nullptr);
    // Macro recording (9.10): while on, moves / clones / deletes and key edits of the selection become command-script
    // lines; stopping puts them in the Run Command Script editor. Returns the recorded text when it stops.
    QString toggleMacroRecording();
    bool macroRecording() const { return macroRecording_; }
    bool goToCameraBookmark(int slot);
private:
    std::shared_ptr<std::vector<StockModelInstance>> refProps_;
    std::shared_ptr<std::vector<app::GpuVertex>> refBaseTris_;
    size_t refWorldTris_ = 0;
    class QTimer* refPropTimer_ = nullptr;
    // Stock reference props (research 84): which models the map places, the fetch chain, and the fixed status text.
    std::map<std::string, size_t> refPropUse_;  // prop model -> placements
    QString refPropZone_;                       // the zone refProps_ belongs to (another one may be loading)
    PropFetchChain refPropChain_;
    QLabel* refPropLabel_ = nullptr;            // permanent status-bar label: props ready / fetching / failed
    QElapsedTimer refPropClock_;                // runs from the first pass that found models missing
    qint64 refPropFetchMs_ = -1;                // how long the fetch took (-1: none, or still going)
    size_t refPropPlaceholders_ = 0;            // marker boxes drawn by the last pass
    bool refPropFailuresLogged_ = false;
    // Prop models converted for drawing, kept between passes while models are still arriving (then dropped).
    struct RefPropMesh {
        std::vector<app::GpuVertex> plain;                                           // untextured surfaces
        std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>> tex;     // material, its triangles
        size_t tris = 0;
    };
    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;
    size_t refPropBoxed_ = 0;  // models drawn as stand-in boxes (past the triangle budget) in the last rebuild
    // The stock map's decal volumes with their materials and masks (appendStockDecalVolumes), kept to project them on
    // the props once those are built (projectPropDecals; done once per map).
    std::shared_ptr<const struct StockDecalData> stockDecals_;
    QString propDecalsZone_;
    void projectPropDecals();
    size_t refPropBuiltTris_ = 0;
    void updateStockPropLabel();
public:
    struct StockPropStatus {
        QString zone;             // stock reference zone ("" = none)
        bool loaded = false;      // the world and its prop list are in
        bool fetching = false;    // models are missing and the cache is asked or working
        bool stalled = false;     // models are missing and the cache gave up (stallReason)
        QString stallReason;
        PropFetchCounts counts;
        size_t placeholders = 0;  // marker boxes drawn for models not ready or failed
        size_t worldTriangles = 0;
        qint64 fetchMs = -1;      // fetch time so far or in total (-1: nothing had to be fetched)
        QString line;             // the status-bar text
        std::vector<std::pair<QString, QString>> failures;  // failed model, error
    };
    StockPropStatus stockPropStatus() const;  // also for --stock-props-test
private:
    bool previewActive_ = false;
    SelRef previewHit_{kNoId, kNoId};
    QString previewMaterial_;
    size_t previewUndoPos_ = 0;
    std::optional<Vec3> dropPoint_;  // set while an asset drop is being placed: placementPoint() returns it
public:
    // Places / applies an asset dragged from a browser (4.24). `at`: world point; `hit`: what is under the cursor
    // (materials are applied to it). Returns false when nothing was done.
    bool dropAsset(const QString& kind, const QString& name, bool loop, const Vec3& at, SelRef hit);
    // Material drags (owner request 2026-11-11): while the material hovers over a brush, patch or model in the camera,
    // that object shows it (a provisional edit); moving on or leaving the view takes it back, dropping keeps it.
    // A brush or patch gets the material on every face; a model gets the key iw8r_material.
    bool applyMaterialTo(SelRef hit, const QString& material);
    void previewMaterialAt(const QString& material, SelRef hit);
    void endMaterialPreview(bool keep);
    bool materialPreviewActive() const { return previewActive_; }
private:
    void toolPathsDialog();       // acts.exe / Replay exe / Oodle DLL / x64-zt zonetool locations
public:
    // Help > First-Run Setup (onlyIfReplayMissing = false), or the startup check (true: only when the Replay exe is missing).
    void showFirstRunSetup(bool onlyIfReplayMissing);
private:
    QString toolPath(const QString& key) const;
    void updateTitle();
    void updateStatus();
    void rebuildRecentMenu();

    EditorContext ctx_;
    ads::CDockManager* dock_ = nullptr;
    CameraView* camera_ = nullptr;
    GridView* grid_ = nullptr;
    class LinkGraphView* linkView_ = nullptr;  // Links dock (6.25)
    class BudgetPanel* budget_ = nullptr;      // Build Budget dock (1.16)
    size_t referencePropMeshes_ = 0;            // stock reference props drawn as their meshes (3.19)
public:
    size_t referencePropMeshes() const { return referencePropMeshes_; }  // tests
    AABB referenceWorldBounds() const { return stockWorld_ ? stockWorld_->bounds : AABB{}; }  // tests
    Vec3 referencePropSample() const { return referencePropSample_; }  // tests: origin of one prop drawn as its mesh
    Vec3 referencePropSample_;
private:
    QSet<QString> vfxExporting_;                // effects being exported for Effect Info (8.9)
    std::map<std::string, std::shared_ptr<const struct VfxSimDef>> fxSims_;  // camera effect preview (8.1)
public:
    QString findVfxExport(const QString& name) const;
private:

public:
    class LinkGraphView* linkGraphView() const { return linkView_; }  // tests
    class BudgetPanel* budgetPanel() const { return budget_; }        // tests

private:
    EntityInspector* inspector_ = nullptr;
    AssetBrowser* materials_ = nullptr;
    AssetBrowser* models_ = nullptr;
    AssetBrowser* effects_ = nullptr;
    AssetBrowser* images_ = nullptr;
    AssetBrowser* sounds_ = nullptr;
    class StockSkyCache* imageCache_ = nullptr;
    class StockSkyCache* skyCache_ = nullptr;  // worldspawn skies (camera preview, builds, Sun From Sky)
    QString lastSky_;
    QString lastSunDirSeen_;  // worldspawn sundirection at the last document change (auto Sun From Sky)        // the worldspawn sky last seen (auto Sun From Sky runs when it changes)
    QString pendingSunSky_;  // a changed sky still exporting: aim the sun when it arrives  // Images browser: any catalog image, exported on demand
    QString previewedImage_;
public:
    AssetBrowser* imagesBrowser() const { return images_; }
    AssetBrowser* soundsBrowser() const { return sounds_; }
    void previewImage(const QString& name);  // Images browser preview (also used by the GUI test)
private:
public:
    AssetBrowser* effectsBrowser() const { return effects_; }
private:
    CompilePanel* compile_ = nullptr;
    ads::CDockWidget* compileDock_ = nullptr;
    QByteArray defaultDockState_;
    bool pendingDeploy_ = false;  // Compile and Deploy: deploy when the running compile succeeds
    bool pendingLoad_ = false;  // F5: load the map in game after the deploy
    FindReplaceDialog* findDialog_ = nullptr;
    ReplayLogPanel* replayLog_ = nullptr;
    LogPanel* logPanel_ = nullptr;
    ScriptPanel* scriptPanel_ = nullptr;
    std::vector<app::GpuVertex> referenceProps_;  // stock map reference: prop boxes (11.3)
    OutlinerPanel* outliner_ = nullptr;
    ToastHost* toasts_ = nullptr;
    LayersPanel* layers_ = nullptr;
    class PrefabBrowser* prefabs_ = nullptr;
    QString previewedModel_;
public:
    void previewModel(const QString& name);  // Models browser preview (also used by the GUI test)
private:
    MaterialCache* matCache_ = nullptr;
    ModelCache* modelCacheStock_ = nullptr;
    WorldCache* worldCache_ = nullptr;
    QString referenceZone_;  // stock zone requested for the reference layer ("" = none)
public:
    void loadStockReference(const QString& zone);
    // The reference zone's own lightmap (world cache lightmap.iw8rlm) into the camera, read off the UI thread.
    void loadStockLightmap(const QString& zone);
    // The reference zone's reflection probes (world cache probes.iw8rprb) into the camera, read off the UI thread.
    void loadStockProbes(const QString& zone);
    // The stock map's sun (its ComWorld sun light, through ACTS once; cached beside the world export) into the camera.
    // skyMaterial: the world's own sky material (tw/sky_mp_speed_midday on mp_m_speed); "" = tw/sky_<zone>.
    void loadStockSun(const QString& zone, const QString& skyMaterial = QString());
    void rebuildReferenceProps();  // the stock reference's props from the model cache (again as models arrive)  // also used by --smoke-test (IW8R_SMOKE_REFERENCE)
private:

    std::unique_ptr<MaterialLibrary> customMaterials_;
    std::map<std::string, std::shared_ptr<const ImportedModel>> modelCache_;
    // 3.18 LOD preview: the LOD a stock model placement draws (0 = normal; -1 past the last LOD; the collision LOD in
    // the Collision Only camera mode). Details are read once per model.
    int stockLodFor(const Entity& e, const std::string& model);
    void refreshLodPreview();
    bool lodPreview_ = false;
    std::map<std::string, std::optional<XModelInfo>> lodDetail_;
    std::vector<int> lodSig_;
    QTimer* lodTimer_ = nullptr;

public:
    bool lodPreviewOn() const { return lodPreview_; }
    // 4.12: Tools > Asset Availability Audit: runs `--asset-audit <kind>` as a child process into the repo's
    // data/asset_catalog/iw8_<kind>_status.tsv (resumes: names already there are skipped) and reloads the browsers.
    void showAssetAudit();
    // 8.2: Tools > Effect Editor on <map dir>/fx (the map must be saved; else the app data custom_assets/fx folder).
    FxEditorDialog* openEffectEditor();
    QString assetAuditStatusFile(const QString& kind) const;
    ModelCache* stockModelCache() const { return modelCacheStock_; }
    void setLodPreview(bool on);
    int stockLodForTest(const Entity& e, const std::string& model) { return stockLodFor(e, model); }

private:
    std::map<std::string, std::shared_ptr<const MapFile>> prefabCache_;
    // Prefab bundles: the asset folders of the bundles the map places, and what they were worked out from (the
    // placements' models and the prefab folders), so they are found again only when a placement comes or goes.
    PrefabAssetRoots bundleRoots_;
    QString bundleRootsKey_;
    QString bundleRootsKey() const;
    void updateBundleRoots();              // works bundleRoots_ out again
    void bundleRootsMayHaveChanged();      // after a document change: reloads the custom assets when they did change
    // The outward normal of the brush face of `hit` nearest to `point` (nullopt for a patch or no hit).
    std::optional<Vec3> surfaceNormalAt(const SelRef& hit, const Vec3& point) const;
    void saveSelectionAsMapPrefabDialog();  // the old "save as one .map file" dialog
    QWidget* makeEntityDockHost();          // the bundle panel above the entity inspector
    void prefabDragHover(const QString& file, const QPointF& pos);  // a prefab dragged over the camera
    bool prefabDropKeepsPreview(const QString& file);               // the drop of a previewed prefab
    class PrefabBundlePanel* bundlePanel_ = nullptr;
    struct PrefabPreview {
        bool active = false;
        QString file;
        Vec3 at;
        size_t undoPos = 0;
        Id entity = kNoId;
    };
    PrefabPreview prefabPreview_;
    QMap<QString, QAction*> actions_;
    int lastShortcutRows_ = 0;
    QMenu* recentMenu_ = nullptr;
    QLabel* gridLabel_ = nullptr;
    QLabel* selLabel_ = nullptr;
    QLabel* entLabel_ = nullptr;
    QLabel* posLabel_ = nullptr;
    QLineEdit* materialEdit_ = nullptr;
    QTimer tick_;
    QTimer autosave_;
    // Camera fly-through (8.15): the path being recorded / played, saved as <map>.campaths.json.
    CameraPath camPath_;
    QTimer camPathTimer_;
    QElapsedTimer camPathClock_;
    QString camPathFile() const;
public:
    CameraPath& cameraPath() { return camPath_; }
    bool cameraPathPlaying() const { return camPathTimer_.isActive(); }
    void stepCameraPath(double t);  // puts the camera at time t of the path (playback, GUI test)
private:
    QElapsedTimer clock_;
    QString displayName_ = "untitled.map";
    std::map<QString, std::set<SelRef>> selectionSets_;
    QMenu* selectionSetsMenu_ = nullptr;
    KeyReplaceDialog* keyDialog_ = nullptr;
    LiveLinkPanel* liveLink_ = nullptr;
    WebServer* web_ = nullptr;
    QToolBar* mainToolbar_ = nullptr;
    UpnpPortMapper* upnp_ = nullptr;
    struct PrefabParent {
        QString path;
        iw8r::app::Camera cam;
    };
    std::vector<PrefabParent> prefabStack_;
    QString stockZone_;            // zone name of an opened stock map ("" otherwise)
    QString lastStockRebuild_;     // work folder of the last stock rebuild that passed its check ("" = none)
    QString lastStockRebuildZone_;
    bool stockSpawnsEditable_ = false;  // spawns were turned into document entities (Edit Compiled Spawns)
    StockLoot stockLoot_;               // the loot block of the opened stock map (empty: none)
    std::set<std::string> stockModelNames_;  // models the opened stock map's own entities name (never carried)
    std::vector<StockPlacedObject> stockPlaced_;  // every scriptable instance of the opened stock map
    bool stockLootEditable_ = false;    // loot points were turned into document entities (Edit Loot Points)
    ID3D11Device* dev_;
    ID3D11DeviceContext* dc_;
};

}  // namespace iw8r::qt
