import io

# Research 105 section 1.4, the thin push (static-model VS:344-372): for a light grid box with flag 4
# (LGV_SMODEL_FLAGS_THINPUSH), axis a = flags & 3, d = M[a] . (camera, 1); the box coordinate along a gets
# +4 sat(4 (d - 1)) when d > 1 and -4 sat(-4 d) when d < 0, so a thin wall or sign shows the probes of the side the
# camera is on. The VS runs it when cb7[185].x == 0 (INFERRED 0 on MP, research 105 section 6). The box's flags ride in
# the spare w of its uvScale row; IW8R_NO_LGV_THINPUSH leaves them out (A/B).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/viewport/render.cpp'
patch(C, '''    const float4 p = float4(w, 1.0);
    const float3 c = saturate(float3(dot(lgvBoxes[box * 5u], p), dot(lgvBoxes[box * 5u + 1u], p), dot(lgvBoxes[box * 5u + 2u], p)));''',
      '''    const float4 p = float4(w, 1.0);
    float3 c = float3(dot(lgvBoxes[box * 5u], p), dot(lgvBoxes[box * 5u + 1u], p), dot(lgvBoxes[box * 5u + 2u], p));
    // The thin push (research 105 section 1.4): flag 4, axis flags & 3, toward the camera's side of the box.
    const uint flags = uint(max(lgvBoxes[box * 5u + 3u].w, 0.0) + 0.5);
    const uint axis = flags & 3u;
    if ((flags & 4u) != 0u && axis < 3u) {
        const float d = dot(lgvBoxes[box * 5u + axis], float4(eyeStart.xyz, 1.0));
        const float push = d > 1.0 ? 4.0 * saturate(4.0 * (d - 1.0)) : (d < 0.0 ? -4.0 * saturate(-4.0 * d) : 0.0);
        if (axis == 0u) c.x += push;
        else if (axis == 1u) c.y += push;
        else c.z += push;
    }
    c = saturate(c);''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                for (int k = 0; k < 3; ++k) b[12 + k] = st.atlas.pack[v].uvScale[k], b[16 + k] = st.atlas.pack[v].uvBias[k];
            }''', '''                for (int k = 0; k < 3; ++k) b[12 + k] = st.atlas.pack[v].uvScale[k], b[16 + k] = st.atlas.pack[v].uvBias[k];
                // The box flags (thin push, research 105 section 1.4) in the spare w of the uvScale row.
                static const bool noThinPush = std::getenv("IW8R_NO_LGV_THINPUSH") != nullptr;
                b[15] = noThinPush ? 0.0f : float(st.atlas.xform[v].flags & 7u);
            }''')
print('ok')
