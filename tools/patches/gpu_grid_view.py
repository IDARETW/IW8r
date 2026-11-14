import io

# Plan 1.9: brushes added to a stock map are lit by that map's tetrahedral light grid in View > Game Lighting (the
# grid the game is inferred to light script models with; such brushes are built as a script model). The camera reads
# the grid from the map's gfx_map_trzone export in the world cache, in a worker, the first time a bake wants it.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


H = 'src/qtapp/camera_view.h'
patch(H, '''    void rebakeGameLighting() { gameBakeRev_ = ~0ull; gameSeenAt_.invalidate(); update(); }
''', '''    void rebakeGameLighting() { gameBakeRev_ = ~0ull; gameSeenAt_.invalidate(); update(); }
public:
    // Where a stock map's gfx_map_trzone export lies (the world cache; "" = not exported). With it, brushes added to
    // that map take their indirect light from the map's tetrahedral light grid (core stock_lightgrid, plan 1.9).
    void setStockGridSource(std::function<QString(const QString& zone)> source) { stockGridSource_ = std::move(source); }
    // The grid in use (null: none read yet, or the map has none), and the map it belongs to.
    std::shared_ptr<const StockGpuGrid> stockGpuGrid() const { return stockGrid_; }
    bool stockGpuGridLoading() const { return stockGridLoading_; }
    void setStockGpuGrid(const std::string& zone, std::shared_ptr<const StockGpuGrid> grid);
public slots:
''')
patch(H, '''    bool gameUnbakedTold_ = false;  // the status line said once that the map has no lightmap
''', '''    bool gameUnbakedTold_ = false;  // the status line said once that the map has no lightmap
    std::function<QString(const QString&)> stockGridSource_;
    std::shared_ptr<const StockGpuGrid> stockGrid_;
    std::string stockGridZone_;      // the map stockGrid_ (or the read in progress, or the failed read) belongs to
    bool stockGridLoading_ = false;
''')
s = read(H)
if '#include "stock_lightgrid.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "stock_lightgrid.h"\n' + s[i:]
    write(H, s)

V = 'src/qtapp/camera_view.cpp'
patch(V, '''    if (const StockMapLook* stock = gameStockLook()) {
        in.scene.sunColor = stockSunKnown_ ? stockSunColor_ : Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]};
        in.scene.sunIntensity = stockSunKnown_ ? stockSunIntensity_ : stock->sunIntensity;
        in.scene.skyIntensity = in.scene.sunIntensity;
        in.scene.sunDir = gameSunToward();
        in.key ^= 0x5157u;
    }''', '''    if (const StockMapLook* stock = gameStockLook()) {
        in.scene.sunColor = stockSunKnown_ ? stockSunColor_ : Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]};
        in.scene.sunIntensity = stockSunKnown_ ? stockSunIntensity_ : stock->sunIntensity;
        in.scene.skyIntensity = in.scene.sunIntensity;
        in.scene.sunDir = gameSunToward();
        in.key ^= 0x5157u;
        // The game builds these brushes as a script model and lights models from the map's tetrahedral light grid
        // (research 105 section 2.1): the same grid gives the indirect light here, where it covers the point. The
        // grid is read once per map, in the background; until it is there the sky bake above stands in.
        if (stockGridZone_ != gameStockMap_ && !stockGridLoading_) {
            stockGrid_.reset();
            stockGridZone_ = gameStockMap_;
            const QString file = stockGridSource_ ? stockGridSource_(QString::fromStdString(gameStockMap_)) : QString();
            if (!file.isEmpty()) {
                stockGridLoading_ = true;
                QPointer<CameraView> self(this);
                const std::string zone = gameStockMap_;
                std::thread(guardedWorker(__FILE__, __LINE__, [self, file, zone]() {
                    auto grid = std::make_shared<StockGpuGrid>();
                    QFile f(file);
                    std::string err;
                    bool ok = false;
                    if (f.open(QIODevice::ReadOnly)) {
                        const QByteArray text = f.readAll();
                        ok = parseStockGpuGrid(std::string(text.constData(), size_t(text.size())), *grid, &err);
                    }
                    QMetaObject::invokeMethod(qApp, [self, zone, grid, ok]() {
                        if (self) self->setStockGpuGrid(zone, ok ? grid : nullptr);
                    }, Qt::QueuedConnection);
                })).detach();
            }
        }
        if (stockGrid_ && stockGridZone_ == gameStockMap_) {
            const std::shared_ptr<const StockGpuGrid> grid = stockGrid_;
            const double scale = stockBakedScale_;
            in.scene.ambientAt = [grid, scale](const Vec3& pos, const Vec3& normal, Vec3& ambient) {
                // Half a unit off the face, as the bake's own rays start.
                const double p[3] = {pos.x + normal.x * 0.5, pos.y + normal.y * 0.5, pos.z + normal.z * 0.5};
                StockLgvSh sh;
                if (!sampleStockGpuGrid(*grid, p, sh)) return false;
                const double n[3] = {normal.x, normal.y, normal.z};
                double rgb[3];
                evalStockLgv(sh, n, rgb);
                ambient = Vec3{rgb[0] * scale, rgb[1] * scale, rgb[2] * scale};
                return true;
            };
            in.key ^= 0x67726964u ^ uint64_t(grid->tets.size());
        }
    }''')
patch(V, '''bool CameraView::gameLookUnbaked() const {''', '''void CameraView::setStockGpuGrid(const std::string& zone, std::shared_ptr<const StockGpuGrid> grid) {
    stockGridLoading_ = false;
    if (zone != gameStockMap_) return;  // another map was opened meanwhile: read again at the next bake
    stockGridZone_ = zone;
    stockGrid_ = std::move(grid);
    if (stockGrid_ && !stockGrid_->empty()) {
        emit ctx_->statusMessage(QString("Game lighting: brushes added to %1 take their light from the map's light grid (%2 probes)")
                                     .arg(QString::fromStdString(zone)).arg(stockGrid_->pos.size()));
        rebakeGameLighting();
    } else {
        stockGrid_.reset();
    }
}

bool CameraView::gameLookUnbaked() const {''')
# The bake's "nothing changed" key must see the grid arriving: mix it into the input hash.
patch(V, '''        mixD(double(in.key));
''', '''        mixD(double(in.key));
        mixD(in.scene.ambientAt ? 1.0 : 0.0);
''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''    camera_->setReferenceFilterLookup([this]() -> std::shared_ptr<const CustomShader> {''', '''    // The stock map's tetrahedral light grid lies in its gfx_map_trzone export in the world cache.
    camera_->setStockGridSource([this](const QString& zone) -> QString {
        if (!worldCache_ || zone.isEmpty()) return {};
        const QString file = worldCache_->cacheRoot() + "/" + zone + "/assets/gfx_map_trzone/" + zone + ".0.asset.json";
        return QFileInfo::exists(file) ? file : QString();
    });
    camera_->setReferenceFilterLookup([this]() -> std::shared_ptr<const CustomShader> {''')

# Bench: wait for the grid before the bake line on a stock map, and say whether it lights the brushes.
B = 'src/qtapp/main.cpp'
patch(B, '''            if ((cam->gameLightmap() || cam->gameLookUnbaked()) && !cam->gameBaking()) break;
        }''', '''            if ((cam->gameLightmap() || cam->gameLookUnbaked()) && !cam->gameBaking() && !cam->stockGpuGridLoading()) break;
        }
        if (const auto grid = cam->stockGpuGrid())
            report << QString("game_grid probes=%1 tetrahedra=%2 (brushes on the stock map take the grid's light)").arg(grid->pos.size()).arg(grid->tets.size());''')
print('ok')
