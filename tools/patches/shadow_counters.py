import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''        size_t shadowDrawCalls = 0, shadowPasses = 0;''', '''        size_t shadowDrawCalls = 0, shadowPasses = 0;
        size_t nearShadowPasses = 0, lampShadowMaps = 0;  // the near sun cascade and local light maps drawn this frame''')
R = 'src/viewport/render.cpp'
patch(R, '''        if (nearShadowDue) nearDirty_ = false;
        inShadowPass_ = false;''', '''        if (nearShadowDue) nearDirty_ = false, ++stats_.nearShadowPasses;
        inShadowPass_ = false;''')
patch(R, '''                drawCasters(lightShadowDsv_[size_t(j)], sl.vp, true);
                sl.drawn = true;''', '''                drawCasters(lightShadowDsv_[size_t(j)], sl.vp, true);
                sl.drawn = true;
                ++stats_.lampShadowMaps;''')
M = 'src/qtapp/main.cpp'
patch(M, '''    size_t relayouts = 0, inPlace = 0, shadowPasses = 0, uploadBytes = 0;  // sums over the scenario's frames''',
      '''    size_t relayouts = 0, inPlace = 0, shadowPasses = 0, uploadBytes = 0;  // sums over the scenario's frames
    size_t nearShadowPasses = 0, lampShadowMaps = 0;''')
patch(M, '''            s->shadowPasses += cam->frameStats().shadowPasses;''', '''            s->shadowPasses += cam->frameStats().shadowPasses;
            s->nearShadowPasses += cam->frameStats().nearShadowPasses;
            s->lampShadowMaps += cam->frameStats().lampShadowMaps;''')
patch(M, '''                      .arg(s.relayouts).arg(s.inPlace).arg(s.shadowPasses).arg(double(s.uploadBytes) / 1024.0 / double(std::max<size_t>(1, s.cpu.size())), 0, 'f', 1);
    };''', '''                      .arg(s.relayouts).arg(s.inPlace).arg(s.shadowPasses).arg(double(s.uploadBytes) / 1024.0 / double(std::max<size_t>(1, s.cpu.size())), 0, 'f', 1);
        if (s.nearShadowPasses || s.lampShadowMaps)
            report << QString("  shadows_%1 near_cascade=%2 lamp_maps=%3 (over %4 frames)").arg(name.trimmed()).arg(s.nearShadowPasses)
                          .arg(s.lampShadowMaps).arg(s.cpu.size());
    };''')
print('ok')
