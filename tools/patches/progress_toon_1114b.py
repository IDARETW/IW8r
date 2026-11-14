import io

# PROGRESS handoff note: the toon ink from scene depth and the owner's queue of 2026-11-14 afternoon.

ROOT = 'E:/IW8-Radiant/'
p = 'docs/PROGRESS.md'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '  OWNER IN GAME 2026-11-14 13:00 (they test again):'
assert s.count(a) == 1
note = '''  OWNER QUEUE 2026-11-14 15:00, in this order (their words): "the ends of lines are still visible, they should be
  seamless, this toon shader should look professionally and intricately made"; "when done developing the toon shader,
  make sure it will work with shipment, and build and deploy both the toon ship and cinderrail toon files"; "also
  implement proper volumetric sky/fog, make cinderrail have a brighter scene and sky, make the fog/volumetrics similar
  to how rust is, but don't just copy it, this is to see how customizable the scene in the radiant tool is"; "then get
  shipment working and continue with the radiant development". The deploy of both toon maps is asked for (not a
  standing permission for other game-folder writes). Cinder Rail changes go to out/agent/mp_cinderrail_toon (map id
  mp_cinderrail_t), not to the kept project out/agent/mp_cinderrail.
  TOON INK, DONE in the viewport (uncommitted until the suites ran): lines from the scene's depth
  (data/shaders/toon.iw8shader.hlsl toonInkScene: per pixel 8 ways x 4 pixels, each sample's "rise" off the pixel's
  own plane; behind = fold or outline, in front = inner corner only when the rise holds over three samples; a face
  no wider than a line is part of the line; the edge inputs stay as the fallback, toonInkEdges). New shader inputs
  IW8RSurface.pixel / viewDepth / eye, host function iw8rSceneDepth(pixel) (0 = none, 1e9 = sky) and constant
  iw8rSceneDepthStep (1 / 1024): core custom_shader.h; viewport render.cpp (t31, ps_scenez, keepSceneDepth,
  sceneDepthStale -> camera_view redraws). The game's scene depth is depth / 6 in a 16-bit float (research 104), so
  the viewport stores its own the same way (f16tof32(f32tof16(z / 6)) * 6) and takes off the editor's slope depth
  bias first (rsTris_ 1.5 x slope: faceStep in ps_scenez; without that thin tilted faces read as sky and folds
  between tilted and upright faces showed steps). Patch scripts tools/patches/scene_depth_*.py, toon_scene_ink*.py.
  Bench: out/agent/mapcam_file.ps1 -Name x -Cam '-1500 -120 100 55 8 90' -MapFile <toon map>, MCROOT = the real
  material cache folder under LOCALAPPDATA (read only use). Pictures sent: out/agent/toon_ink_scene_1/2.png.
  Converter side DONE in the worktree, not committed (tools/patches/zt_scene_depth.py): map_surface_realtime.hlsl
  iw8rSceneDepth under IW8R_SCENE_DEPTH (texture t4, models t3, Load at pixel * replayView[7].x >> mip
  replayView[7].z, |v| * 6, >= 392999 sky), IW8R_SURFACE_SCREEN fills pixel / viewDepth (SV_POSITION.w) / eye
  (replayView[4]); iw3_render_assets.cpp g_surfaceSceneDepth + RepointCodeImage(lit, 79, 0) (the lit technique's
  type-1 argument: thermal mask -> code image 0 "Frontend FloatZ"); replay_render.cpp checks the binding. Other code
  images a shader could read: 1 "Frontend EntityID_Velocity", 2 "Frontend Tangent Frame" (game-test r_scene.cpp
  10489 / 10517; formats not decoded, not used).
  Headless deploy added: IW8Radiant.exe --compile <map> --out <folder> --deploy (MainWindow::deployBuiltMapQuiet ->
  CompilePanel::deploy; built maps live in <game>\\mods\\mw120r\\maps\\<id>, old package to <game>\\.proxy\\backups).
  SHIPMENT LINES, PLAN (not started): extend core dxbc_splice (and the converter's copy) so a filter program may
  also read input 1 = SV_Position (mapped to the stock shader's position input, its mask widened to xyzw) and
  texture t0 (mapped to the register of the technique's code image 79 argument, which the editzone re-points to
  code image 0), instructions ld / resinfo / derivatives / flow control copied as they are; no constant buffers and
  no constant arrays. New optional shader function for stock surfaces with (colour, pixel, viewDepth); the ink code
  is the same as toonInkScene, which needs only pixel, view depth, its screen derivatives and the pixel size (stock:
  2 / the depth texture's width, a 90 degree view assumed). Viewport parity: render.cpp IW8R_FILTER branch
  (line ~1210) calls it with i.pos.xy and the view depth. Stock pixel shaders to check first: every lit PS of
  techsets_mp_shipment must declare SV_Position (dxbc_dis.py over out/agent/toon_ship/ps).
  VOLUMETRIC FOG / SKY, research 126 (Opus agent, to be verified while building): a built map has no game fog (iw3
  style: fog baked into the surface shader, sky never fogged, air density 0, veil 0). Its change list: vision field
  names for veilStrength and the volumetric* fields (replay_vision_names.inc -> vision_fields.inc), fogSunDir in
  BuildReplayVision, drop the baked fog when the vision has fogAtmosphericScattering 1, fog for blended layers in
  map_surface_realtime.hlsl, light key iw8r_volumetric, an atmosphere preset key, own maps in camera_view's fog /
  veil preview. Suggested desert haze values are in the note's table.
'''.replace('\n', nl)
s = s.replace(a, note + a)
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
