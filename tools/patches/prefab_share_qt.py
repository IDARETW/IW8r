import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/main_window.h', [
 ('''    Id addPrefabPlacement(const QString& file, const Vec3& at, QString* err);  // the document edit of insertPrefab''',
  '''    // The document edit of insertPrefab. `normal`: the outward normal of the surface at `at` (placement hints).
    Id addPrefabPlacement(const QString& file, const Vec3& at, QString* err, const std::optional<Vec3>& normal = std::nullopt);
    // The outward normal of the brush face of `hit` nearest to `point` (nullopt for a patch or no hit).
    std::optional<Vec3> surfaceNormalAt(const SelRef& hit, const Vec3& point) const;'''),
 ('''    bool insertPrefab(const QString& file, QString* err = nullptr);''', '''    bool insertPrefab(const QString& file, QString* err = nullptr);
    // Prefab sharing (placement hints, zip, plain map to bundle). Return "" / false with *err on failure.
    QString convertPlainPrefabToBundle(const QString& mapPath, QString* err = nullptr);  // the bundle folder written
    bool exportPrefabBundleAsZip(const QString& bundleFolder, const QString& zipPath, QString* err = nullptr);
    QStringList importPrefabBundlesFromZip(const QString& zipPath, QString* err = nullptr);  // the bundle folders written
    // Turns the selected misc_prefab placements by their bundle's yaw step (90 for a plain prefab); one undo step.
    int rotateSelectedPrefabsByStep();'''),
])

patch('src/qtapp/main_window_prefabs.cpp', [
 ('''Id MainWindow::addPrefabPlacement(const QString& file, const Vec3& point, QString* err) {''',
  '''std::optional<Vec3> MainWindow::surfaceNormalAt(const SelRef& hit, const Vec3& point) const {
    const EntityItem* it = hit.item ? ctx_.doc.findItem(hit.entity, hit.item) : nullptr;
    const Primitive* p = it ? it->prim() : nullptr;
    if (!p || p->kind != PrimKind::Brush) return std::nullopt;
    std::optional<Vec3> best;
    double bestDist = 1e30;
    for (size_t i = 0; i < p->faces.size(); ++i)
        if (const auto pl = facePlane(*p, i)) {
            const double d = std::fabs(dot(pl->n, point) - pl->d);
            if (d < bestDist) bestDist = d, best = pl->n;
        }
    if (bestDist > 2.0) return std::nullopt;  // the point is not on this brush's surface
    return best;
}

Id MainWindow::addPrefabPlacement(const QString& file, const Vec3& point, QString* err, const std::optional<Vec3>& normal) {'''),
 ('''        // placement.snap of the manifest: the grid step this prefab wants on the floor plane.
        if (bundle.manifest.snap > 0) {
            at.x = snapToGrid(at.x, bundle.manifest.snap);
            at.y = snapToGrid(at.y, bundle.manifest.snap);
        }
    }''', '''        // The manifest's placement hints: surface (a wall prefab faces out of the wall), yaw step and snap.
        const PrefabPlacement place = prefabPlacementFor(bundle.manifest, at, normal);
        at = place.origin;
        if (place.yaw != 0) keys.push_back({"angles", "0 " + formatNumber(place.yaw) + " 0"});
        if (!place.note.empty()) ctx_.setStatus("Prefab: " + qs(place.note));
    }'''),
 ('''    Vec3 at = placementPoint();
    if (!dropPoint_ && camera_ && camera_->isVisible()) {
        if (const auto hit = camera_->surfacePoint(QPointF(camera_->width() / 2.0, camera_->height() / 2.0), nullptr)) {
            at = *hit;
            if (ctx_.snap)
                for (int a = 0; a < 3; ++a) at[a] = snapToGrid(at[a], ctx_.doc.grid());
        }
    }
    const Id id = addPrefabPlacement(file, at, err);''', '''    Vec3 at = placementPoint();
    std::optional<Vec3> normal;
    if (!dropPoint_ && camera_ && camera_->isVisible()) {
        SelRef surface;
        if (const auto hit = camera_->surfacePoint(QPointF(camera_->width() / 2.0, camera_->height() / 2.0), &surface)) {
            at = *hit;
            normal = surfaceNormalAt(surface, at);
            if (ctx_.snap)
                for (int a = 0; a < 3; ++a) at[a] = snapToGrid(at[a], ctx_.doc.grid());
        }
    }
    const Id id = addPrefabPlacement(file, at, err, normal);'''),
 ('''void MainWindow::insertPrefabDialog() {''', '''QString MainWindow::convertPlainPrefabToBundle(const QString& mapPath, QString* err) {
    const auto fail = [&](const QString& why) {
        if (err) *err = why;
        return QString();
    };
    PrefabSaveOptions opt;
    opt.grid = 0;
    for (const QString& r : lookupMaterialRoots()) opt.materialRoots.push_back(r.toStdString());
    for (const QString& r : lookupModelRoots()) opt.modelRoots.push_back(r.toStdString());
    const QString dir = QFileInfo(mapPath).absolutePath();
    opt.soundRoots.push_back((dir + "/sounds").toStdString());
    opt.fxRoots.push_back((dir + "/fx").toStdString());
    opt.stock = PrefabBrowser::stockCatalog();
    const PrefabSaveResult result = prefabBundleFromPlainMap(mapPath.toStdString(), opt);
    if (!result.ok) return fail(qs(result.error));
    // Beside the .map file, named after it.
    const QString folder = dir + "/" + QFileInfo(mapPath).completeBaseName() + kPrefabBundleSuffix;
    if (QFileInfo::exists(folder + "/" + kPrefabManifestFile)) return fail("A bundle '" + folder + "' already exists; it is not replaced.");
    std::string werr;
    if (!writePrefabBundleFolder(folder.toStdString(), result, &werr)) return fail(qs(werr));
    return folder;
}

bool MainWindow::exportPrefabBundleAsZip(const QString& bundleFolder, const QString& zipPath, QString* err) {
    std::string e;
    if (exportPrefabBundleZip(QDir::cleanPath(bundleFolder).toStdString(), zipPath.toStdString(), &e)) return true;
    if (err) *err = qs(e);
    return false;
}

QStringList MainWindow::importPrefabBundlesFromZip(const QString& zipPath, QString* err) {
    QString ferr;
    const QString dest = prefabSaveFolder(&ferr);
    if (dest.isEmpty()) {
        if (err) *err = ferr;
        return {};
    }
    std::vector<std::string> written;
    std::string e;
    if (!importPrefabBundleZip(zipPath.toStdString(), dest.toStdString(), &written, &e)) {
        if (err) *err = qs(e);
        return {};
    }
    QStringList out;
    for (const std::string& w : written) out << qs(w);
    return out;
}

int MainWindow::rotateSelectedPrefabsByStep() {
    std::vector<std::pair<Id, std::string>> edits;
    for (const auto& r : ctx_.doc.selection()) {
        if (r.item) continue;
        const Entity* e = ctx_.doc.findEntity(r.entity);
        if (!e || e->classname() != "misc_prefab") continue;
        double step = 90;
        PrefabBundle bundle;
        if (prefabBundleOf(e->id, bundle) && bundle.manifest.yawStep > 0) step = bundle.manifest.yawStep;
        double a[3] = {0, 0, 0};
        if (const std::string* av = e->value("angles")) std::sscanf(av->c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]);
        const double yaw = std::fmod(a[1] + step + 360.0, 360.0);
        edits.push_back({e->id, formatNumber(a[0]) + " " + formatNumber(std::fabs(yaw) < 1e-9 ? 0.0 : yaw) + " " + formatNumber(a[2])});
    }
    if (edits.empty()) return 0;
    ctx_.doc.beginGroup("Rotate Prefab");
    for (const auto& [id, angles] : edits) ctx_.doc.setKeyValue(id, "angles", angles);
    ctx_.doc.endGroup();
    ctx_.documentEdited(QString("Rotated %1 prefab placement(s) by their yaw step (one undo step)").arg(edits.size()));
    return int(edits.size());
}

void MainWindow::insertPrefabDialog() {'''),
])

patch('src/qtapp/main_window.cpp', [
 ('''    addCommand(file, "OpenBundleFolder", "Open Bundle Folder of Selected Prefab", QKeySequence(), [this] { openSelectedBundleFolder(); });''',
  '''    addCommand(file, "OpenBundleFolder", "Open Bundle Folder of Selected Prefab", QKeySequence(), [this] { openSelectedBundleFolder(); });
    addCommand(file, "RotatePrefabStep", "Rotate Selected Prefab (its yaw step)", QKeySequence(), [this] {
        if (!rotateSelectedPrefabsByStep()) ctx_.setStatus("Rotate Prefab: select misc_prefab placements first");
    });
    addCommand(file, "ConvertPrefabMap", "Convert Plain .map Prefab To Bundle...", QKeySequence(), [this] {
        const QString f = QFileDialog::getOpenFileName(this, "Convert Prefab To Bundle", prefabSaveFolder(), "Prefab maps (*.map)");
        if (f.isEmpty()) return;
        QString err;
        const QString folder = convertPlainPrefabToBundle(f, &err);
        if (folder.isEmpty()) return void(QMessageBox::warning(this, "Convert Prefab To Bundle", err));
        ctx_.setStatus("Bundle written: " + QDir::toNativeSeparators(folder) + " (the .map file is kept)");
    });
    addCommand(file, "ExportPrefabZip", "Export Prefab Bundle As Zip...", QKeySequence(), [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, "Prefab bundle folder (.iw8prefab)", prefabSaveFolder());
        if (folder.isEmpty()) return;
        const QString zip = QFileDialog::getSaveFileName(this, "Export Prefab Bundle", folder + ".zip", "Zip (*.zip)");
        if (zip.isEmpty()) return;
        QString err;
        if (!exportPrefabBundleAsZip(folder, zip, &err)) return void(QMessageBox::warning(this, "Export Prefab Bundle", err));
        ctx_.setStatus("Prefab bundle exported: " + QDir::toNativeSeparators(zip));
    });
    addCommand(file, "ImportPrefabZip", "Import Prefab Bundles From Zip...", QKeySequence(), [this] {
        const QString zip = QFileDialog::getOpenFileName(this, "Import Prefab Bundles", QString(), "Zip (*.zip)");
        if (zip.isEmpty()) return;
        QString err;
        const QStringList bundles = importPrefabBundlesFromZip(zip, &err);
        if (bundles.isEmpty()) return void(QMessageBox::warning(this, "Import Prefab Bundles", err));
        if (prefabs_) prefabs_->refresh();
        ctx_.setStatus(QString("Imported %1 prefab bundle(s) into %2").arg(bundles.size()).arg(QDir::toNativeSeparators(prefabSaveFolder())));
    });'''),
])
print('ok')
