import io

# Research 132 G2: stock `unlit_` materials (glows, sign faces, light shafts, effect cards) are drawn unlit in Game
# Lighting: their colour as is (x the emissive constant when they have one), fogged and exposed like the rest, instead
# of being lit by the lightmap, the probes and the sun.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/camera_view.h', '''        std::array<float, 4> emissiveScale{0, 0, 0, 0}, transTint{0, 0, 0, 0}, wrap{0, 0, 0, 0};
    };''', '''        std::array<float, 4> emissiveScale{0, 0, 0, 0}, transTint{0, 0, 0, 0}, wrap{0, 0, 0, 0};
        // Research 132 G2: an `unlit_` technique set (TechsetFamily::UnlitEffect): drawn with its colour as is.
        bool unlit = false;
    };''')
patch('src/qtapp/main_window.cpp', '''        if (const auto* c103 = cst(103)) f.wrap = {(*c103)[0], (*c103)[1], (*c103)[2], 1.0f};
        return f;''', '''        if (const auto* c103 = cst(103)) f.wrap = {(*c103)[0], (*c103)[1], (*c103)[2], 1.0f};
        f.unlit = parseTechset(e->techset.section('/', -1).toStdString()).family == TechsetFamily::UnlitEffect;
        return f;''')
s = io.open(ROOT + 'src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
if '#include "techset_info.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#include "vfx_info.h"', '#include "vfx_info.h"' + nl + '#include "techset_info.h"', 1)
    io.open(ROOT + 'src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
patch('src/qtapp/camera_view.cpp', '''    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0)) return true;''',
      '''    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0 && !f->unlit)) return true;''')
patch('src/qtapp/camera_view.cpp', '''                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, 0, 0, 0, 0};''',
      '''                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, f->unlit ? 1.0f : 0.0f, 0, 0, 0};''')
patch('src/viewport/render.cpp', '''        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));''',
      '''        if (stockRef && matX.x > 0.5) {
            // A stock `unlit_` material (research 132 G2): its colour as is, x the emissive image and constant when it has
            // them (research 107 section 5), fogged and exposed like the rest; no lightmap, probes, sun or lamps.
            float3 cu = albedo;
            if (matE.w > 0.5) cu = srgbToLinear3(emi0.Sample(samp0, uvS).rgb) * matE.rgb;
            cu = fogAny(cu, i.w);
            if (gameAo.w < 0.5) cu = gamePost(cu);
            if (batchFlags.y > 1.5) return float4(cu * outA, 1.0);
            return float4(cu, outA);
        }
        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));''')
print('ok')
