import io

# Volumetric box masks: the key help of iw8r_volume (mask .. mask4), a GUI check that a box's mask reaches the
# renderer, and the class description brought up to date.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


D = 'data/iw8_entity_defs_overrides.json'
patch(D, '''          "description": "Height in units above the box's bottom (along the box's own up axis) where the fog is half as thick: a ground mist. 6.931 or more; empty = the same thickness at every height. Written as height_fade.",
          "required": false,
          "examples": [
            "64",
            "200"
          ]
        }
      }
    },''', '''          "description": "Height in units above the box's bottom (along the box's own up axis) where the fog is half as thick: a ground mist. 6.931 or more; empty = the same thickness at every height. Written as height_fade.",
          "required": false,
          "examples": [
            "64",
            "200"
          ]
        },
        "mask": {
          "type": "string",
          "default": "",
          "description": "A picture that thins the box's fog (white keeps it, black removes it): \\"<picture> [x|y|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]\\". The picture is projected through the box along the axis (default z, the box's up) and repeats every size units; scroll moves it, in units per second. Pictures: a file name without its ending from the map's volume_masks folder (PNG, TGA, JPG, BMP), or the editor's clouds_soft and clouds_hard. Game Lighting shows it. Up to four masks multiply (mask2, mask3, mask4): two cloud layers moving at different speeds look like drifting mist. Not seen in game.",
          "required": false,
          "examples": [
            "clouds_soft 800",
            "clouds_hard z 400 400 60 20"
          ]
        },
        "mask2": {
          "type": "string",
          "default": "",
          "description": "A second mask picture, as mask. All masks of a box multiply.",
          "required": false,
          "examples": [
            "clouds_hard 400 400 -40 15"
          ]
        },
        "mask3": {
          "type": "string",
          "default": "",
          "description": "A third mask picture, as mask.",
          "required": false,
          "examples": []
        },
        "mask4": {
          "type": "string",
          "default": "",
          "description": "A fourth mask picture, as mask.",
          "required": false,
          "examples": []
        }
      }
    },''')
patch(D, '''"description": "Volumetric box (editor class, 5.7): written as a world volumetric (lit fog / light shafts inside the box). Density and falloff units and the in-game look are not verified yet.",''',
      '''"description": "Volumetric box (editor class, 5.7): a box of fog in the game's volumetric air, lit by the sun, the sky light and lamps with iw8r_volumetric. Density is extinction per unit (0.001 is a light haze, stock boxes hold 0.0002 to 0.04); falloff is the soft border in units. Mask pictures (mask .. mask4) break it into drifting patches. Game Lighting shows it. Seen in game only as part of a haze.",''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            for (int k = 0; k < (ws0 ? 3 : 2); ++k) ctx.doc.undo();  // the key, the lamp, the haze''',
      '''            // A volumetric box with a mask: the mask's code (the picture's slice and its axis) and its sizes reach the
            // renderer; a mask whose picture does not exist does nothing.
            const Id fogBox = ctx.doc.addEntity({{"classname", "iw8r_volume"}, {"origin", "0 0 64"}, {"density", "0.01"},
                                                 {"mask", "clouds_soft x 400 200"}, {"mask2", "no_such_picture 100"}}, {}, "t");
            ctx.documentEdited();
            cv->renderImage(320, 180, true, 0, 100);
            const auto& look = cv->gameLookForTest();
            const bool masked = look.volBoxCount == 1 && look.volBoxMasks[0][0] == 0.0f && look.volBoxMasks[0][1] < 0.0f &&
                                std::fabs(look.volBoxMasks[0][4] - 1.0f / 400) < 1e-7f && std::fabs(look.volBoxMasks[0][5] - 1.0f / 200) < 1e-7f;
            r.check(masked, QString("a volumetric box's mask reaches the renderer with its picture, axis and sizes (%1 box(es), code %2)")
                                .arg(look.volBoxCount).arg(double(look.volBoxMasks[0][0])));
            (void)fogBox;
            ctx.doc.undo();  // the box
            for (int k = 0; k < (ws0 ? 3 : 2); ++k) ctx.doc.undo();  // the key, the lamp, the haze''')
print('ok')
