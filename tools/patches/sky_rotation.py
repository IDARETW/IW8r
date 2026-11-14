import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/sky_image.h'
patch(H, '''    float exposure = 0.0f;
};''', '''    float exposure = 0.0f;
    // The map's worldspawn "skyrotation" (pitch yaw roll, degrees; key 47372): the game turns the view direction into
    // the sky's frame with it before the lookup (sky VS a6bc8f6e: o1 = cb7[153..155] x direction). Mp_m_speedball:
    // 0 -111 0. Only hemi-octahedral sources (a converter cube passes through unchanged).
    float rotation[3] = {0, 0, 0};
};''')
C = 'src/core/sky_image.cpp'
patch(C, '''                float px, py, rgb[3];
                hemiOctTexel(d[0], d[1], d[2], img.w, img.h, px, py);''', '''                float px, py, rgb[3];
                const float s[3] = {d[0] * rot[0][0] + d[1] * rot[0][1] + d[2] * rot[0][2], d[0] * rot[1][0] + d[1] * rot[1][1] + d[2] * rot[1][2],
                                    d[0] * rot[2][0] + d[1] * rot[2][1] + d[2] * rot[2][2]};
                hemiOctTexel(s[0], s[1], s[2], img.w, img.h, px, py);''')
s = io.open(C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = '    cube = cubeHeader(size);'
assert s.count(anchor) == 1
rot = '''    // The sky's frame (skyrotation): rows = the rotated axes (forward, left, up) of the angles, so the direction in
    // the sky's frame is their dot products with the world direction (the inverse rotation).
    float rot[3][3];
    {
        const double p = opt.rotation[0] * 3.14159265358979 / 180.0, y = opt.rotation[1] * 3.14159265358979 / 180.0,
                     r = opt.rotation[2] * 3.14159265358979 / 180.0;
        const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r), cr = std::cos(r);
        const double fwd[3] = {cp * cy, cp * sy, -sp};
        const double left[3] = {-(sr * sp * cy - cr * sy), -(sr * sp * sy + cr * cy), -(sr * cp)};
        const double upv[3] = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
        for (int k = 0; k < 3; ++k) rot[0][k] = float(fwd[k]), rot[1][k] = float(left[k]), rot[2][k] = float(upv[k]);
    }
'''
s = s.replace(anchor, rot.replace('\n', nl) + anchor)
io.open(C, 'w', encoding='utf-8', newline='').write(s)

CV = 'src/qtapp/camera_view.cpp'
patch(CV, '''            SkyConvertOptions so;
            so.faceSize = 256;
            SkyConvertInfo skyInfo;''', '''            SkyConvertOptions so;
            so.faceSize = 256;
            // The map's sky rotation (worldspawn skyrotation; stock maps: from their entity string).
            if (const Entity* ws = ctx_->doc.map().worldspawn())
                if (const std::string* r = ws->value("skyrotation"))
                    std::sscanf(r->c_str(), "%f %f %f", &so.rotation[0], &so.rotation[1], &so.rotation[2]);
            SkyConvertInfo skyInfo;''')
print('ok')
