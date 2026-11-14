import io

# GUI check and docs for plan 1.9: brushes added to a stock map take the map's tetrahedral light grid.

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


T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            const double manual = cv->gameExposure();
            cv->setGameStockMap("mp_shipment");
''', '''            const double manual = cv->gameExposure();
            cv->setGameStockMap("mp_shipment");
            {
                // Plan 1.9: brushes on a stock map take their indirect light from the map's tetrahedral light grid.
                // Two grids of one big tetrahedron around the room, a dark one and a bright one: the preview
                // lightmap's ambient layer follows the grid.
                const auto gridOf = [](uint32_t dcPacked) {
                    std::vector<uint8_t> probes, positions, tets;
                    const float pts[4][3] = {{-3000, -3000, -1000}, {3000, -3000, -1000}, {0, 3000, -1000}, {0, 0, 5000}};
                    for (const auto& p : pts) {
                        std::vector<uint8_t> rec(32, 128);
                        std::memcpy(rec.data(), &dcPacked, 4);
                        probes.insert(probes.end(), rec.begin(), rec.end());
                        for (const float f : p) {
                            uint8_t b[4];
                            std::memcpy(b, &f, 4);
                            positions.insert(positions.end(), b, b + 4);
                        }
                    }
                    for (const uint32_t i : {0u, 1u, 2u, 3u}) {
                        uint8_t b[4];
                        std::memcpy(b, &i, 4);
                        tets.insert(tets.end(), b, b + 4);
                    }
                    auto g = std::make_shared<iw8r::StockGpuGrid>();
                    iw8r::buildStockGpuGrid(probes, positions, tets, *g);
                    return g;
                };
                const auto ambientMean = [&](const std::shared_ptr<iw8r::StockGpuGrid>& grid) {
                    const auto before = cv->gameLightmap();
                    cv->setStockGpuGrid("mp_shipment", grid);
                    QElapsedTimer gt;
                    gt.start();
                    while (gt.elapsed() < 30000) {
                        cv->renderImage(160, 90, true, 0, 0);
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                        const auto now = cv->gameLightmap();
                        if (now && now != before && !cv->gameBaking() && now->polys > 0) break;
                        QThread::msleep(15);
                    }
                    const auto lm = cv->gameLightmap();
                    double sum = 0;
                    size_t n = 0;
                    if (lm && lm != before)
                        for (size_t i = 0; i + 3 < lm->a.size(); i += 4)
                            if (lm->a[i] + lm->a[i + 1] + lm->a[i + 2] > 0 || lm->a[i + 3] > 0) sum += lm->a[i + 1], ++n;
                    return n ? sum / double(n) : -1.0;
                };
                const uint32_t dcDark = 0;
                const uint32_t dcBright = (17u << 6) | ((17u << 6) << 11) | ((17u << 5) << 22);  // 4.0 in each channel
                const double dark = ambientMean(gridOf(dcDark));
                const double bright = ambientMean(gridOf(dcBright));
                r.check(dark >= 0 && dark < 0.05 && bright > 0.5 && cv->stockGpuGrid() && cv->stockGpuGrid()->tets.size() == 1,
                        QString("brushes on a stock map take the map's light grid: ambient %1 under a dark grid, %2 under a bright one")
                            .arg(dark, 0, 'f', 3).arg(bright, 0, 'f', 3));
            }
''')
s = read(T)
if '#include "stock_lightgrid.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "stock_lightgrid.h"\n' + s[i:]
    write(T, s)

patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Brushes added to a stock map are lit like the game lights models there.** The game builds such brushes as a
  model and (by what the game's data shows) lights models from the map's own light grid: tens of thousands of
  measured light probes joined into tetrahedra. View > Game Lighting now reads that grid (Shipment: 223,432 probes,
  1.33 million tetrahedra) and gives the added brushes its light, so a box in a shaded alley is as dark as the
  containers beside it instead of being lit by an open sky. Where the grid does not reach, the sky bake stays. Read
  from the map's exported data and checked by render (`out/agent/gg_cmp.png`); not seen in game.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- 2026-11-14, plan 1.9 (brushes on a stock map): core stock_lightgrid StockGpuGrid (decodeStockGpuProbe,
  buildStockGpuGrid with a uniform cell index, parseStockGpuGrid from the gfx_map_trzone export's gpuLightGrid,
  sampleStockGpuGrid = barycentric over the holding tetrahedron); BakeScene::ambientAt (a texel's ambient from
  outside, no sky rays there). CameraView: setStockGridSource (MainWindow: <world cache>/<zone>/assets/
  gfx_map_trzone/<zone>.0.asset.json), the grid read in a worker at the first stock-map bake, setStockGpuGrid,
  ambientAt = evalStockLgv x bakedLightScale. Bench: IW8R_BENCH_BOX / IW8R_BENCH_BOX_MATERIAL (a box added before the
  shots), IW8R_NO_STOCK_GRID, report line game_grid. Tests stock_gpugrid_* (the real export through
  IW8R_TEST_TRZONE: Shipment 2,013 of 2,013 sampled probes found and equal to their own light, mean E(up) 1.98),
  lightmap_bake_ambient_from_outside, GUI check. Shipment box A/B: out/agent/gg_cmp.png (box region mean 25.6 with the
  grid, 44.3 with the sky bake). INFERRED: that the game lights script models from this grid (research 105).
''')
patch('docs/PLAN_REMAINING.md', '''multi-bounce occlusion). Open: specular-map materials, brushes added to a stock map, own-map models against the built
light grid.
''', '''multi-bounce occlusion). Brushes added to a stock map DONE 2026-11-14 (the map's tetrahedral light grid, research
117 section 5). Open: specular-map materials, own-map models against the built light grid.
''')
p = 'docs/research/117_own_map_world_shading.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '## 4. Open'
assert s.count(a) == 1
add = '''## 5. Brushes added to a stock map (2026-11-14)

Brushes added to a stock map are not world surfaces: the rebuild carries them as a model (`iw8r_ovl_...`, a script
model). Research 105 section 2.1 found the map's tetrahedral light grid (`gpuLightGrid.gpuLightGrid` of the
`gfx_map_trzone` export), which the static-model shader does not read and which is INFERRED to light dynamic models.

- **Decode (VERIFIED on Shipment's export).** 223,432 probes (32-byte records, positions as three floats) and
  1,330,096 tetrahedra (four probe indices, bit 31 a flag). A probe: the DC as R11G11B10F, then eight coefficients per
  channel as bytes, `n = (byte - 127.5) / 127`, coefficient = `n x DC x sqrt3` (i < 4) or `sqrt5`, read literally. Of
  2,013 probes sampled at their own position, all were found in a tetrahedron and gave their own light back; the mean
  light on an upward face is 1.98 (research 105 measured 0.7 under cover and 3.4 in the open on another map).
- **Sampling.** The tetrahedron holding the point (a uniform cell index over the tetrahedra's boxes), the four probes
  blended by barycentric weights, then the static-model shader's irradiance formula (`evalStockLgv`) for the face's
  normal, times the map's bakedLightScale.
- **Viewport.** The preview bake of such brushes takes this as each texel's ambient light (`BakeScene::ambientAt`);
  the sun and its shadow stay live. Where no tetrahedron holds the texel, the sky bake stays.
- **Check.** A box added in a shaded alley of Shipment: its faces average 25.6 of 255 with the grid and 44.3 with
  the sky bake, beside containers of the same darkness (`out/agent/gg_cmp.png`).
- **Open.** Whether the game lights a script model per pixel or once per model from this grid (INFERRED per point
  here); the probes' visibility byte and the tetrahedron visibility records are not used.

'''.replace('\n', nl)
if '## 5. Brushes added to a stock map' not in s:
    i = s.index(a)
    # Section 5 goes after the "Open" section: append at the end, and point to it from the open list.
    s = s.rstrip() + nl + nl + add.rstrip() + nl
    s = s.replace('- Brushes added to a stock map keep their preview bake in the stock map\'s units (they are built as models; plan 1.9' + nl +
                  '  has that item open).', '- Brushes added to a stock map: section 5.')
    write(p, s)
print('ok')
