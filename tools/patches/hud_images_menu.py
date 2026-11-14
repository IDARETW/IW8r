import io

# ROADMAP 4.27: Compile > HUD Images Folder opens (and makes) "<map name>.hud" beside the map; and the docs.

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


M = 'src/qtapp/main_window.cpp'
patch(M, '''    addCommand(compile, "SetLoadscreen", "Set Loadscreen From Camera", QKeySequence(), [this] {''',
      '''    addCommand(compile, "HudImagesFolder", "HUD Images Folder (pictures for the map script)...", QKeySequence(), [this] {
        // 4.27: every picture in "<map name>.hud" becomes a HUD image the map script can draw with setshader.
        const QString folder = qs(hudImageFolderFor(ctx_.doc.path()));
        if (folder.isEmpty()) return ctx_.setStatus("Save the map first: its HUD images live in a folder beside it");
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        const size_t have = listHudImages(folder.toStdString()).size();
        ctx_.setStatus(QString("HUD images: %1 picture(s) in %2. A file flag.png is drawn in the map script with "
                               "setshader(\\"hud_flag\\", width, height); the next build carries them.")
                           .arg(have).arg(QDir::toNativeSeparators(folder)));
    });
    addCommand(compile, "SetLoadscreen", "Set Loadscreen From Camera", QKeySequence(), [this] {''')
s = read(M)
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "hud_images.h"' not in s:
    a = '#include "convert_ops.h"' + nl
    assert s.count(a) == 1
    s = s.replace(a, a + '#include "hud_images.h"' + nl, 1)
    write(M, s)
patch('src/qtapp/qt_gui_test.cpp', '''                for (const char* id : {"CloneStockMaterial", "AdjustImage", "TechsetInfo", "AssetUsage", "SoundSettings", "ReplaceMaterialInMap",
                                       "ReplaceModelMaterial", "VisionEditor", "MakeLadder", "BehaviourPreview", "CompareWithBackup"})''',
      '''                for (const char* id : {"CloneStockMaterial", "AdjustImage", "TechsetInfo", "AssetUsage", "SoundSettings", "ReplaceMaterialInMap",
                                       "ReplaceModelMaterial", "VisionEditor", "MakeLadder", "BehaviourPreview", "CompareWithBackup", "HudImagesFolder"})''')

# ---- docs
patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Your own HUD pictures in a map script.** Until now a map script could only draw the game's own icons. Put
  pictures (PNG, TGA, JPG, BMP or DDS) into the folder `<map name>.hud` beside the map (Compile > HUD Images Folder
  makes and opens it); the build carries each one, and the script draws `flag.png` with
  `setshader("hud_flag", width, height)`. A picture is stored as a square (it gets its shape back from the width and
  height you draw it with), with its transparency. The build lists the names, and tells you when the script draws a
  `hud_` name that is not one of your pictures. This needed a small converter change (x64-zt `1c70f6ed`, deployed).
  Proven offline: `tools/hud_image_build_test.py` builds a map with a picture and a script, the game's loaders accept
  all five zones, and the picture and its material are read back from them. Whether the game draws it has not been
  seen.

''')
patch('docs/PROGRESS.md', '''  Review fixes (tools/patches/review_fixes_1114.py)''', '''  ROADMAP 4.27 HUD images: core hud_images.{h,cpp} (hudImageName, listHudImages, encodeHudImageDds = square DXT5
  through minimap encodeMinimapDds, hudImageFolderFor, hudNamesInScript), CompileOptions::hudImageDir /
  CompileReport::hudImages, command HudImagesFolder. Converter 1c70f6ed (worktree, deployed; old exe
  out/zt_backup/zonetool_24d1aec7.exe): CopyHudImages + a 2D material per hud_* image in addMapAssets (the material
  lands in techsets_<map>.ff, the image in <map>.ff). MainWindow::toolPath reads IW8R_TOOL_<KEY> first (a converter
  under test). Proof tools/hud_image_build_test.py (in run_offline_proofs.py: 21 proofs).
  Review fixes (tools/patches/review_fixes_1114.py)''')
P = 'docs/PLAN_REMAINING.md'
patch(P, '''  - fonts and LUI (4.27): HUD from the map script works with stock icons; own fonts and menus are not built
    (deferred, as the plan allowed).''', '''  - fonts and LUI (4.27): HUD from the map script works with stock icons and, since 2026-11-14, with the map's own
    pictures (`<map name>.hud`, proven offline; game). Own fonts and menus are not built (deferred, as the plan
    allowed: no map-zone writer for them, and nothing shows the game would use one).''')
R = 'docs/ROADMAP.md'
s = read(R)
nl = '\r\n' if '\r\n' in s else '\n'
a = 'custom TTF fonts and LUI have no map writer in x64-zt (deferred); custom HUD images need the converter to carry an unused material (open); in-game look NOT VERIFIED'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, 'custom HUD images (2026-11-14): pictures in `<map name>.hud` beside the map are carried as `hud_<name>` with a 2D material of that name (core `hud_images`, x64-zt `1c70f6ed`, proof `tools/hud_image_build_test.py`: ACTS 0 failed on all zones, image and material read back); custom TTF fonts and LUI have no map writer in x64-zt (deferred); in-game look NOT VERIFIED')
write(R, s)
patch('docs/USER_GUIDE.md', '''- **Painting a blend on a brush surface.**''', '''- **Your own pictures on the HUD.** Compile > HUD Images Folder opens the folder `<map name>.hud` beside your map.
  Put pictures in it (PNG with transparency works best). A file `flag.png` becomes `hud_flag`; draw it from the
  map's script: `icon = newhudelem(); icon.x = 20; icon.y = 120; icon setshader("hud_flag", 64, 64);`. Use names the
  game does not already have (a name such as `hud_icon_c4` is a stock icon). Up to 64 pictures; each is stored
  square, up to 1024 pixels a side.
- **Painting a blend on a brush surface.**''')
patch('docs/features/FEATURES.md', '''### Keys that make things move or act''', '''### HUD pictures

- Your own pictures for script HUD elements: `Compile > HUD Images Folder` opens `<map name>.hud` beside the map; each picture in it is built into the map as `hud_<file name>` and drawn from the map script with `setshader("hud_<file name>", width, height)`. The build lists them and points out a `hud_` name in the script that is not one of them. (not yet seen in game)

### Keys that make things move or act''')
patch('docs/TEST_CHECKLIST.md', '''## In game: your map's model light (2026-11-13), not seen in game''', '''## In game (optional, 2026-11-14): your own HUD picture
- [ ] Put a PNG into `<map name>.hud` (Compile > HUD Images Folder), add a map script (worldspawn `iw8r_script`) with
  `user_main() { icon = newhudelem(); icon.x = 20; icon.y = 120; icon setshader("hud_<file name>", 64, 64); }`, build
  and play: the picture is on the screen at the left. If a grey or checkered square shows, the material loaded but
  the image did not; if nothing shows, say so.

## In game: your map's model light (2026-11-13), not seen in game''')
patch('docs/HANDOVER.md', '''`py -3 -I tools\\run_offline_proofs.py <work dir>`: 20 end-to-end proofs; all 20 passed on the last build of 2026-11-14 (commit `ecb71fd`).''',
      '''`py -3 -I tools\\run_offline_proofs.py <work dir>`: 21 end-to-end proofs; all passed on the last build of 2026-11-14.''')
print('ok')
