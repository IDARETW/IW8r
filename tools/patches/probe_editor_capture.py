import io

# Editor: reflection probes for a built map (research 123). The capture is 128 texels a face (was 64: the game's
# 256-texel octahedral image needs faces of about 104), each texel the mean of four frame samples, and the world
# JSON says the picture is the editor's own render ("capture": "editor"), so the converter keeps its colour and
# stores it at the stock probes' level.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('src/core/compile_iw8.h', '''    std::string probeCaptureDir;''', '''    std::string probeCaptureDir;  // probe<N>.rgba (6 faces of kIw8ProbeCaptureSize^2 RGBA) + probe<N>.pos''')
s = read('src/core/compile_iw8.h')
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'struct CompileOptions {'
assert s.count(anchor) == 1
s = s.replace(anchor, ('// The face size of a reflection probe capture and of the cube the build writes (research 123: the game\'s probe\n'
                       '// image is a 256-texel octahedron, which a cube of about 104-texel faces fills).\n'
                       'constexpr int kIw8ProbeCaptureSize = 128;\n\n').replace('\n', nl) + anchor)
write('src/core/compile_iw8.h', s)
patch('src/core/compile_iw8.cpp', '''constexpr int kProbeSize = 64;                    // doc 15 §7: 64x64 cube, 7 mips
constexpr int kProbeMips = 7;''', '''constexpr int kProbeSize = kIw8ProbeCaptureSize;  // the probe cube's face (research 123), every level down to 1
constexpr int kProbeMips = 8;''')
patch('src/core/compile_iw8.cpp', '''// 64x64 cube map, B8G8R8A8, 7 mips: neutral grey (128) with opaque alpha, or the six captured RGBA faces (each
// 64*64*4 bytes, mips by 2x2 box filter). 128 + 6 * 21844 bytes.''', '''// A cube map of kProbeSize faces, B8G8R8A8, every level: neutral grey (128) with opaque alpha, or the six captured
// RGBA faces (each kProbeSize^2 * 4 bytes, mips by 2x2 box filter).''')
patch('src/core/compile_iw8.cpp', '''        probeJson += std::string(pi ? "," : "") + "{\\"image\\":" + quote(image) + ",\\"origin\\":" + vec3(probes[pi]) + volumeJson + "}";''',
      '''        // "capture": "editor": the picture is the editor's own render of the map (or its neutral grey), not an IW3
        // capture: the converter keeps its colour and stores it at the stock probes' level (research 123).
        probeJson += std::string(pi ? "," : "") + "{\\"image\\":" + quote(image) + ",\\"origin\\":" + vec3(probes[pi]) + volumeJson +
                     ",\\"capture\\":\\"editor\\"}";''')
patch('src/qtapp/main_window.cpp', '''        const std::vector<uint8_t> faces = camera_->captureCube(positions[i], 64);''',
      '''        const std::vector<uint8_t> faces = camera_->captureCube(positions[i], kIw8ProbeCaptureSize);''')
patch('src/qtapp/camera_view.cpp', '''                const double u = 2.0 * (x + 0.5) / size - 1.0, v = 2.0 * (y + 0.5) / size - 1.0;
                // Inverse of the converter's per-face (u, v) for a direction on that face.
                Vec3 d;
                switch (f) {
                    case 0: d = {1, -v, -u}; break;
                    case 1: d = {-1, -v, u}; break;
                    case 2: d = {u, 1, v}; break;
                    case 3: d = {u, -1, -v}; break;
                    case 4: d = {u, -v, 1}; break;
                    default: d = {-u, -v, -1}; break;
                }
                const Vec3 p = pos + normalize(d) * 1024.0;
                // Row-vector times row-major matrix.
                const double cx = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
                const double cy = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
                const double cw = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
                uint8_t* o = &out[((size_t(f) * size_t(size) + size_t(y)) * size_t(size) + size_t(x)) * 4];
                o[3] = 255;
                if (cw <= 1e-6) continue;
                const int px = std::clamp(int((cx / cw * 0.5 + 0.5) * fw), 0, fw - 1);
                const int py = std::clamp(int((0.5 - cy / cw * 0.5) * fh), 0, fh - 1);
                const uint8_t* s = &frames[size_t(f)][(size_t(py) * size_t(fw) + size_t(px)) * 4];
                o[0] = s[0];
                o[1] = s[1];
                o[2] = s[2];
            }''', '''                uint8_t* o = &out[((size_t(f) * size_t(size) + size_t(y)) * size_t(size) + size_t(x)) * 4];
                o[3] = 255;
                // The mean of four samples inside the texel (the frame has several pixels per texel: one sample
                // made sparkling reflections).
                unsigned sum[3] = {0, 0, 0}, taken = 0;
                for (int sub = 0; sub < 4; ++sub) {
                    const double u = 2.0 * (x + (sub & 1 ? 0.75 : 0.25)) / size - 1.0, v = 2.0 * (y + (sub & 2 ? 0.75 : 0.25)) / size - 1.0;
                    // Inverse of the converter's per-face (u, v) for a direction on that face.
                    Vec3 d;
                    switch (f) {
                        case 0: d = {1, -v, -u}; break;
                        case 1: d = {-1, -v, u}; break;
                        case 2: d = {u, 1, v}; break;
                        case 3: d = {u, -1, -v}; break;
                        case 4: d = {u, -v, 1}; break;
                        default: d = {-u, -v, -1}; break;
                    }
                    const Vec3 p = pos + normalize(d) * 1024.0;
                    // Row-vector times row-major matrix.
                    const double cx = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
                    const double cy = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
                    const double cw = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
                    if (cw <= 1e-6) continue;
                    const int px = std::clamp(int((cx / cw * 0.5 + 0.5) * fw), 0, fw - 1);
                    const int py = std::clamp(int((0.5 - cy / cw * 0.5) * fh), 0, fh - 1);
                    const uint8_t* s = &frames[size_t(f)][(size_t(py) * size_t(fw) + size_t(px)) * 4];
                    sum[0] += s[0], sum[1] += s[1], sum[2] += s[2];
                    ++taken;
                }
                if (!taken) continue;
                o[0] = uint8_t((sum[0] + taken / 2) / taken);
                o[1] = uint8_t((sum[1] + taken / 2) / taken);
                o[2] = uint8_t((sum[2] + taken / 2) / taken);
            }''')
patch('src/qtapp/qt_gui_test.cpp', '''QFileInfo(dir + "/probe0.rgba").size() == 6 * 64 * 64 * 4''',
      '''QFileInfo(dir + "/probe0.rgba").size() == qint64(6) * iw8r::kIw8ProbeCaptureSize * iw8r::kIw8ProbeCaptureSize * 4''')
print('ok')
