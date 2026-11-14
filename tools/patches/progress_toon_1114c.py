import io

# PROGRESS handoff note, 2026-11-14 16:00: state of the owner's queue (toon ink, haze / volumetrics, Shipment lines).

ROOT = 'E:/IW8-Radiant/'
p = 'docs/PROGRESS.md'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '  OWNER QUEUE 2026-11-14 15:00, in this order (their words):'
assert s.count(a) == 1
note = '''  STATE 2026-11-14 16:00 (read this first; everything below it in this block is older detail).
  Owner said since: "make it a little less thick ... not the ink, the fog/volumetrics" (the haze on Cinder Rail Toon
  is now "desert 0.7"; ink widths stay kLinePixels 5 / kLineMaxUnits 2.2) and "the unimplemented volumetric
  capabilities need to be implemented" (nothing volumetric is to stay behind an option or as a note).
  THE OWNER HAS out/agent/mp_cinderrail_toon/mp_cinderrail_toon.map OPEN IN THEIR EDITOR (bin/release, started 15:29)
  and saves it (they turned on iw8r_bake_lightmaps and iw8r_umbra_bake). The editor does not watch the file: a script
  edit of the worldspawn is lost at their next save. Check the keys before a build: skyname sky_mp_aniyah_tac,
  sundirection -46 235 0, suncolor 1 0.97 0.9, iw8r_haze "desert 0.7", iw8r_sky_brightness 3,
  iw8r_exposure_adjust 1 (backup before these: mp_cinderrail_toon.map.before_haze). Never close their editor.
  DONE IN SOURCE, editor built 15:45 (later edits NOT built yet: see the list), nothing committed:
  - Haze: core atmosphere (Haze, hazePresets, parseHaze / formatHaze, hazeVisionText with the EV shift
    log2(11.7 / sun luminance), mergeVisionText; worldspawn iw8r_haze), compile_iw8 (haze -> vision overrides under
    iw8r_vision, iw8r_fog dropped, ground z = median spawn height), light key iw8r_volumetric (LightKind::Flag),
    Atmosphere dialog group, entity defs, vision field names (gen_vision_fields.py groups volumetric / veil under
    fog), viewport (camera_view ownFog_ / ownFogRefdef_ / ownPostFx_ through core game_look
    hazeVisionFromOverrides; the volumetric march is on for own maps: GameLook::volOwn).
  - Sky brightness: core sky_image (SkyConvertOptions::brightness, parseSkyBrightness, kSkyBrightnessKey), compile,
    viewport, Sun & Sky dialog, entity defs. Viewport honours iw8r_exposure_adjust on an own map.
  - Volumetric boxes (research 127 A): compile defaults density 0.001 / falloff 32, falloff cap, height_half,
    warnings; menu default; entity defs; viewport march draws up to 8 boxes (render.cpp volBoxSigma, cbuffer Volume
    208 floats, GameLook::volBoxes; camera_view ownVolumes_). NOT BUILT YET.
  - Stock ink lines: core dxbc_splice screen mode (DxbcSpliceOptions::sceneDepthRegister), custom_shader
    (customShaderHasScreenFilter, customShaderScreenFilterProgramSource, IW8R_FILTER_SCREEN in the viewport
    variant), toon shader iw8rFilterScreen (toonInkScene now takes pixel / viewDepth / pixelSize), render.cpp filter
    branch + sceneDepthWanted(), stock rebuild writes lit_filter_screen.cso and "screen_program"
    (stockLitFilterJson), --shader-filter-program writes <out>.screen.cso. NOT BUILT YET.
  - Tests written, NOT RUN YET: tests/haze_tests.cpp, dxbc_splice_screen_filter (tests/dxbc_splice_tests.cpp, blobs
    from tools/gen_dxbc_test_blobs.py), compile_iw8_haze_and_volumes (tests/compile_iw8_tests.cpp).
  - Docs patch written, NOT APPLIED: tools/patches/docs_haze_ink_1114.py <converter commit>.
  CONVERTER WORKTREE (C:\\zsw-iw8r\\scriptables, nothing committed; live E:\\mw124\\x64-zt still 1d3ff9c0, so the
  owner's own F7 of the toon map FAILS until the deploy: its shader calls iw8rSceneDepth):
  - applied and built 15:20: zt_scene_depth.py, zt_game_fog.py (vision names, fogSunDir, no baked fog when the
    overrides turn fogAtmosphericScattering on). The 25 proofs ran against this exe from 15:41
    (out/agent/proofs_1114c.log).
  - written, NOT APPLIED: zt_filter_screen.py (editzone lit_filter "screen_program", code image 79 -> 0 per
    technique, re-copies dxbc_splice.hpp), zt_translucent_fog.py (MAP_GAME_FOG: GameFogTranslucent + the
    volumetric images on blended / additive / multiply surfaces; defined when the overrides turn the fog or the air
    on or the map has volumetric boxes).
  NEXT, in order: apply the two converter patches, MSBuild, rebuild the editor, run core + GUI suites, render the
  toon map and a box; compile Cinder Rail Toon (--compile ... --deploy; proof tools/scene_depth_build_test.py on the
  package); rebuild Shipment with the screen filter (work folder out/agent/toon_ship; edits2.json is the last recipe)
  and check the report's shaders_with_screen and a spliced shader's disassembly; commit the worktree, back up the live
  exe to out/zt_backup, ff-merge, build live; install Shipment with --install-stock (game closed); docs patch;
  PROGRESS; commit + push (no trailer). Still open after that: lamp glow in the viewport's volumetric preview
  (flagged lamps), the viewport march against the game's numbers (research 109 section 2.7), viewmodel toon (ask
  first: shared zones).
'''.replace('\n', nl)
s = s.replace(a, note + a)
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
