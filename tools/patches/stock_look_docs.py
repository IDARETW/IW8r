import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('CHANGELOG.md', [
 ('''  (8 ms moving); the small sample room draws in 0.35 ms. Not compared with the game yet.
''', '''  (8 ms moving); the small sample room draws in 0.35 ms. Not compared with the game yet.
- **Game Lighting on stock maps.** With a stock map shown (View > Stock Map Reference or a stock edit), Game Lighting
  draws the stock world with the map's own baked lightmap (read from the zone once, cached as
  `world_cache/<zone>/lightmap.iw8rlm`), the map's own sun, and its vision's auto exposure, measured from the frame and
  adapting over time as in game. Backlot2's 363k-triangle world draws in about 0.6 ms per frame. Warzone maps
  (streamed lightmaps) are not supported yet and say so.
'''),
])
patch('docs/PROGRESS.md', [
 ('''  absolute calibration (EV 10, +1.5 EV base, light window scale 1) is INFERRED (research 95).''',
  '''  absolute calibration (EV 10, +1.5 EV base, light window scale 1) is INFERRED (research 95).
- Then stock maps in Game Lighting: core stock_lightmap (decompressed-zone root scan, BC4/BC5/R11G11B10F/RGBA8,
  lightmap.iw8rlm cache), stock_world lightmapUv, WorldCache lightmap job (requestLightmap / lightmapReady), reference
  vertices with UVs, render.cpp stock branch (batchFlags2.z/.w, setStockLightmap half floats), core game_look
  stockMapLooks (shipped maps' sun + auto exposure) and the renderer's metering pass (setGameMetering,
  takeMeteredLuminance). Bench reference scene loads the zone's lightmap. Open: Warzone (type 0, BC6H, streamed);
  AB_LIGHTING's extra channels; own brushes inside a stock edit are baked in editor units, not the stock map's;
  stock probes / specular / retro-reflective diffuse are not drawn.'''),
])
patch('docs/research/94_stock_lightmaps.md', [
 ('''| mp_donetsk (Warzone) | REGULAR | 258 lightmaps, 64x64 to 1024x512, 868 transient zones | 26,221 |''',
  '''| mp_donetsk (Warzone) | REGULAR | 258 lightmaps, 64x64 to 1024x512, 868 transient zones | 26,221 |

Correction (2026-11-12, core stock_lightmap on the zone): mp_donetsk's main zone holds one root with one lightmap of
type 0 whose irradiance plane is BC6H (format 42); the others are in the streamed zones. The editor refuses it with
that reason. Shipment and Backlot2 read back with the numbers below (C mean, max, D mean) exactly.'''),
])
print('ok')
