#include "qt_gui_test_techsets.h"
#include "main_window.h"
#include "techset_workspace.h"
#include "shader_graph_widget.h"
#include "asset_preview_window.h"
#include "camera_view.h"
#include "asset_browser.h"
#include "techset_model_selector.h"
#include <QListView>
#include <QPushButton>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QTableWidget>
#include <QTest>
#include <QToolBar>
#include <DockManager.h>

namespace iw8r::qt {
void runTechsetChecks(std::function<void(const QString &)> section, std::function<void(bool, const QString &)> check,
                      MainWindow &w, const QString &out) {
    section("live techset workspace");
    auto *editor = w.techsetWorkspace();
    check(editor != nullptr, "workspace exists");
    if (!editor)
        return;
    auto *dock = w.findChild<ads::CDockManager *>();
    const auto savedLayout = dock ? dock->saveState() : QByteArray();
    auto *bar = w.findChild<QToolBar *>("sideBar");
    int materials = -1, techsets = -1;
    if (bar) {
        const auto actions = bar->actions();
        for (int i = 0; i < actions.size(); ++i) {
            if (actions[i]->objectName() == "side_Materials")
                materials = i;
            if (actions[i]->objectName() == "side_Techsets")
                techsets = i;
        }
    }
    check(materials >= 0 && techsets == materials + 1, "Techsets icon follows Materials in the upper sidebar");
    w.showDock("Techsets");
    if (editor->onOpen)
        editor->onOpen();
    QTest::qWait(30);
    check(editor->window() == &w, "Techset workspace stays docked in the main window");
    check(editor->width() > w.width() * 0.8 && editor->height() > w.height() * 0.65,
          "Techset workspace occupies the large main content area");
    check(editor->width() <= w.width(), "workspace controls fit inside the main window");
    const auto previousSize = w.size();
    w.resize(960, 540);
    QTest::qWait(30);
    auto *materialSlots = editor->findChild<QComboBox *>("techsetPreviewMaterials");
    auto *scope = editor->findChild<QComboBox *>("techsetPreviewScope");
    const auto bounds = [editor](QWidget *widget) {
        return QRect(widget->mapTo(editor, QPoint()), widget->size());
    };
    check(materialSlots && scope && editor->rect().contains(bounds(materialSlots)) &&
              bounds(materialSlots).bottom() < bounds(scope).top(),
          "preview material controls remain above map controls at 960 by 540");
    w.resize(previousSize);
    QTest::qWait(30);
    editor->previewWindow()->view()->renderImage(320, 240, false, 0, 100);
    check(editor->previewWindow()->view()->texturedTriangles() > 0,
          "first opening uses initialized texture callbacks before any compile");
    const auto original = w.context().doc.text();
    editor->browseModels();
    auto *selector = editor->findChild<TechsetModelSelector *>("techsetModelSelector");
    check(selector && selector->isVisible(), "Browse models opens the model selector");
    if (selector) {
        auto *browser = selector->findChild<AssetBrowser *>("techsetModelBrowser");
        QElapsedTimer loading;
        loading.start();
        while (browser && !browser->catalogLoaded() && loading.elapsed() < 15000)
            QTest::qWait(20);
        check(browser && browser->catalogLoaded() && browser->assetCount() > 0,
              "model selector uses the real model catalog");
        if (browser) {
            const int all = browser->visibleCount();
            browser->setFilter("barrel");
            QTest::qWait(150);
            check(browser->visibleCount() > 0 && browser->visibleCount() < all,
                  "model selector search narrows results");
            auto *list = browser->findChild<QListView *>("techsetModelList");
            check(list && list->height() > selector->height() * 0.55,
                  "model preview grid takes most of the selector height");
            check(browser->findChild<QComboBox *>("assetKit") && browser->iconSide() >= 128,
                  "model selector provides kit filters and large thumbnails");
            browser->selectRow(0);
            const auto chosen = browser->selectedName();
            selector->grab().save(QDir(out).filePath("techset-model-selector.png"));
            if (auto *use = selector->findChild<QPushButton *>("techsetUseModel"))
                use->click();
            check(!chosen.isEmpty() && editor->previewWindow()->assetName() == chosen,
                  "Use model changes the shader preview target");
            check(w.context().doc.text() == original, "choosing a preview model does not place it in the map");
        }
        selector->reject();
    }
    const auto wait = [&] {
        QElapsedTimer timer;
        timer.start();
        while (editor->compiling() && timer.elapsed() < 60000)
            QTest::qWait(20);
        check(!editor->compiling(), "background compile finishes");
    };
    auto doc = newTechsetDocument();
    doc.name = "gui_preview";
    doc.layout["exact_test_flag"] = UINT64_MAX;
    doc.techniques[0].layout["previewState"]["cull"] = "none";
    doc.techniques[0].findStage(ShaderStage::Pixel)->source =
        "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return float4(1,0,0,1); }";
    QString error;
    check(editor->setDocument(doc, &error), "load authoring document: " + error);
    if (auto *mat = editor->findChild<QLineEdit *>("techsetMaterialSlot"))
        mat->clear();
    auto *preview = editor->previewWindow();
    check(preview != nullptr, "GPU asset preview exists");
    if (!preview)
        return;
    preview->showShape(0);
    check(preview->context().scene.gameLookActive(), "asset preview uses Game Lighting");
    editor->compileNow();
    wait();
    check(editor->lastCompileSucceeded(), "HLSL compiles and creates GPU programs: " + editor->compileStatus());
    if (!editor->lastCompileSucceeded())
        return;
    const auto red = preview->view()->renderImage(320, 240, false, 0, 100);
    red.save(QDir(out).filePath("techset-red.png"));
    size_t redPixels = 0;
    for (int y = 0; y < red.height(); ++y)
        for (int x = 0; x < red.width(); ++x) {
            const auto c = red.pixelColor(x, y);
            if (c.red() > c.green() + 40 && c.red() > c.blue() + 40)
                ++redPixels;
        }
    check(redPixels > 1000, QString("compiled shader draws red on the sphere (%1 pixels)").arg(redPixels));
    const auto goodRevision = preview->view()->previewRevision();
    editor->setStageSource(ShaderStage::Pixel,
                           "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return undefined_name; }");
    editor->compileNow();
    wait();
    check(!editor->lastCompileSucceeded() && preview->view()->previewRevision() == goodRevision,
          "failed compile retains the successful GPU revision");
    const auto afterFailure = preview->view()->renderImage(320, 240, false, 0, 30);
    check(afterFailure == red, "failed compile retains the visible picture");
    // A result from the earlier revision must not replace the new document.
    editor->setStageSource(ShaderStage::Pixel,
                           "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return float4(0,0,1,1); }");
    editor->compileNow();
    editor->setStageSource(ShaderStage::Pixel,
                           "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return float4(0,1,0,1); }");
    editor->compileNow();
    wait();
    check(editor->lastCompileSucceeded(), "newest queued revision succeeds");
    const auto green = preview->view()->renderImage(320, 240, false, 0, 50);
    green.save(QDir(out).filePath("techset-green.png"));
    size_t greenPixels = 0;
    for (int y = 0; y < green.height(); ++y)
        for (int x = 0; x < green.width(); ++x) {
            const auto c = green.pixelColor(x, y);
            if (c.green() > c.red() + 30 && c.green() > c.blue() + 30)
                ++greenPixels;
        }
    check(greenPixels > 1000 && green != red, QString("latest revision is visibly green (%1 pixels)").arg(greenPixels));
    check(w.cameraView()->previewRevision() == preview->view()->previewRevision(),
          "both views publish the same compatible revision");
    const auto selectedBefore = w.context().doc.selection();
    w.context().scene.sync(w.context().doc);
    for (const auto *item : w.context().scene.items())
        if (!item->volume && !item->polys.empty() && item->kind == app::PrimGeom::Brush) {
            w.context().doc.select(item->ref, false);
            break;
        }
    check(editor->previewSelection(&error), "selected map geometry opens directly in the dedicated preview: " + error);
    const auto selectionImage = preview->view()->renderImage(320, 240, false, 0, 100);
    selectionImage.save(QDir(out).filePath("techset-selection.png"));
    size_t selectionGreen = 0;
    for (int y = 0; y < selectionImage.height(); ++y)
        for (int x = 0; x < selectionImage.width(); ++x) {
            const auto c = selectionImage.pixelColor(x, y);
            if (c.green() > c.red() + 30 && c.green() > c.blue() + 30)
                ++selectionGreen;
        }
    check(selectionGreen > 20, "compiled shader visibly changes selected geometry in the dedicated preview");
    w.context().doc.deselectAll();
    for (const auto &item : selectedBefore)
        w.context().doc.select(item, true);
    preview->showShape(0);
    const auto file = QDir(out).filePath("gui-preview.iw8techset.json");
    check(editor->saveFile(file, &error), "atomic document save: " + error);
    QFile saved(file);
    saved.open(QIODevice::ReadOnly);
    TechsetDocument loaded;
    std::string e;
    check(readTechsetDocument(saved.readAll().toStdString(), loaded, e) &&
              loaded.layout["exact_test_flag"].get<uint64_t>() == UINT64_MAX,
          "saved layout preserves 64-bit values");
    saved.close();
    auto graph = newShaderGraph(ShaderStage::Pixel);
    graph.nodes.push_back({"test_color", "constant", "Tint", 0, 0, {{"value", {0.0, 0.2, 1.0}}}});
    graph.links = {{"test_color", "out", "output_1", "color"}};
    graph.nodes[0].properties["unlit"] = true;
    auto next = loaded;
    auto *pixel = next.techniques[0].findStage(ShaderStage::Pixel);
    pixel->mode = "graph";
    pixel->graph = graph;
    check(editor->setDocument(next, &error), "load graph document");
    editor->compileNow();
    wait();
    check(editor->lastCompileSucceeded(), "node graph compiles through the same preview pipeline");
    if (auto *canvas = editor->findChild<ShaderGraphWidget *>("techsetGraph")) {
        const auto before = canvas->graph().nodes.size();
        canvas->addNode("time");
        check(canvas->graph().nodes.size() == before + 1, "node insertion");
        canvas->undo();
        check(canvas->graph().nodes.size() == before, "graph undo");
        canvas->redo();
        check(canvas->graph().nodes.size() == before + 1, "graph redo");
    }
    editor->clearOverrides();
    check(preview->view()->previewRevision() == 0 && w.cameraView()->previewRevision() == 0,
          "clear restores both views");
    check(w.context().doc.text() == original, "preview editing leaves the map byte-identical");
    check(editor->saveFile(file, &error), "save test document before shutdown: " + error);
    editor->grab().save(QDir(out).filePath("techset-workspace.png"));
    if (editor->onReturnToMap)
        editor->onReturnToMap();
    check(!editor->isVisible(), "Back to map restores the prior dock layout");
    if (dock && !savedLayout.isEmpty())
        dock->restoreState(savedLayout);
}
} // namespace iw8r::qt
