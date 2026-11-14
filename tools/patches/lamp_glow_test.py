import io

# A GUI check for the lamp glow in the viewport's volumetric air: a flagged lamp reaches the renderer when the map has
# a haze, an unflagged one does not.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/viewport/render.h', '''    void setGameLook(const GameLook& g) { game_ = g; }''',
      '''    void setGameLook(const GameLook& g) { game_ = g; }
    const GameLook& gameLook() const { return game_; }''')
patch('src/qtapp/camera_view.h', '''    app::SceneRenderer::PixelLight pixelLightOf(const app::SceneCache::PreviewLight& L, bool game) const;''',
      '''    app::SceneRenderer::PixelLight pixelLightOf(const app::SceneCache::PreviewLight& L, bool game) const;

public:
    // What the last frame gave the renderer for Game Lighting (the GUI test reads it).
    const app::SceneRenderer::GameLook& gameLookForTest() const { return renderer_.gameLook(); }

private:''')
s = io.open(ROOT + 'src/qtapp/qt_gui_test.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''                QString("the game look renders a different, non-flat image (mean %1, variance %2, change %3 per pixel)")'''
i = s.index(a)
j = s.index(';' + nl, i) + 1 + len(nl)
add = '''        {
            // A lamp that lights the volumetric air (iw8r_volumetric) reaches the renderer when the map has a haze, and
            // one without the key does not. (It was lost once: the glow read the frame's light list before it was filled.)
            const Entity* ws0 = ctx.doc.worldspawn();
            const Id wsId = ws0 ? ws0->id : Id{};
            if (ws0) ctx.doc.setKeyValue(wsId, "iw8r_haze", "fog 2");
            const Id lamp = ctx.doc.addEntity({{"classname", "light"}, {"origin", "0 0 96"}, {"radius", "300"}, {"iw8r_volumetric", "1"}}, {}, "t");
            ctx.documentEdited();
            cv->renderImage(320, 180, true, 0, 100);
            const int flagged = cv->gameLookForTest().volLampCount;
            const float radius = cv->gameLookForTest().volLamps[0][3];
            ctx.doc.setKeyValue(lamp, "iw8r_volumetric", "0");
            ctx.documentEdited();
            cv->renderImage(320, 180, true, 0, 100);
            const int plain = cv->gameLookForTest().volLampCount;
            for (int k = 0; k < (ws0 ? 3 : 2); ++k) ctx.doc.undo();  // the key, the lamp, the haze
            ctx.documentEdited();
            r.check(flagged == 1 && radius == 300.0f && plain == 0,
                    QString("a lamp with iw8r_volumetric lights the haze in the viewport, one without does not (%1 lamp(s), radius %2; then %3)")
                        .arg(flagged).arg(double(radius)).arg(plain));
        }
'''.replace('\n', nl)
s = s[:j] + add + s[j:]
io.open(ROOT + 'src/qtapp/qt_gui_test.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
