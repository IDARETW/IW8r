import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------------------------------------------------------- core
patch('src/core/game_look.h', [
 ('''// The universal colour LUT's curve for one Rec.709 linear colour (already exposed): the display value 0..1 (encoded).
void gameTonemap(const double in[3], double out[3]);
// The LUT itself: size^3 RGBA8 texels, red fastest, indexed by gameLutCoordinate per channel.
std::vector<uint8_t> buildGameTonemapLut(int size = 64);''',
  '''// A vision's grading CLUT (vision field NONMKTKNNM = "<name>", image "gradingclut<name>": a size^3 3D texture, red
// fastest; mp_m_speedball's is 32^3 R16G16B16A16_UNORM). cs_universal_clut_generate (step 2, colorGradingFlags bit 0;
// disassembly lines 63-144) samples it at PQ(x / 1024) of the Rec.2020 linear colour, scaled to the texel centres
// (x 31/32 + 1/64), and decodes the PQ result back (x 1024): a grade in PQ space before the ACES RRT.
struct GameGradingClut {
    int size = 0;
    std::vector<float> rgb;  // size^3 x 3, PQ codes 0..1
    bool valid() const { return size >= 2 && rgb.size() == size_t(size) * size * size * 3; }
};
// Reads the DDS (DX10 header, 3D, R16G16B16A16_UNORM / R8G8B8A8_UNORM / R16G16B16A16_FLOAT).
bool gameGradingClutFromDds(const std::string& dds, GameGradingClut& out, std::string* err = nullptr);
// The ST.2084 PQ code of a linear value normalised to 10000 nits, and its inverse.
double pqEncode(double y);
double pqDecode(double code);
// The universal colour LUT's curve for one Rec.709 linear colour (already exposed): the display value 0..1 (encoded).
// grade: the map's grading CLUT (weight 1), or none.
void gameTonemap(const double in[3], double out[3], const GameGradingClut* grade = nullptr);
// The LUT itself: size^3 RGBA8 texels, red fastest, indexed by gameLutCoordinate per channel.
std::vector<uint8_t> buildGameTonemapLut(int size = 64, const GameGradingClut* grade = nullptr);'''),
])
patch('src/core/game_look.cpp', [
 ('''void gameTonemap(const double in[3], double out[3]) {
    // 1. Rec.709 -> Rec.2020; 2. no grading; 3. RRT via AP0 (Rec.2020 -> XYZ -> AP0), then AP1 -> XYZ -> Rec.2020.
    double c[3], xyz[3], ap0[3], ap1[3];
    mul(kRec709To2020, in, c);
    for (double& v : c) v = std::max(0.0, v);''', '''double pqEncode(double y) {
    const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double p = std::pow(std::clamp(y, 0.0, 1.0), m1);
    return std::pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

double pqDecode(double code) {
    const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double p = std::pow(std::clamp(code, 0.0, 1.0), 1.0 / m2);
    return std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

namespace {
uint32_t rd32(const std::string& s, size_t o) {
    uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}
float halfToFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(float(m | 1024), int(e) - 25);
    return s ? -v : v;
}
// Trilinear sample of the CLUT at texture coordinates (0..1, texel centres at (i + 0.5) / size), clamped.
void sampleClut(const GameGradingClut& g, const double uvw[3], double out[3]) {
    const int n = g.size;
    int i0[3], i1[3];
    double f[3];
    for (int k = 0; k < 3; ++k) {
        const double p = std::clamp(uvw[k] * n - 0.5, 0.0, double(n - 1));
        i0[k] = int(std::floor(p));
        i1[k] = std::min(i0[k] + 1, n - 1);
        f[k] = p - i0[k];
    }
    for (int c = 0; c < 3; ++c) out[c] = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const int r = corner & 1 ? i1[0] : i0[0], gg = corner & 2 ? i1[1] : i0[1], b = corner & 4 ? i1[2] : i0[2];
        const double w = (corner & 1 ? f[0] : 1 - f[0]) * (corner & 2 ? f[1] : 1 - f[1]) * (corner & 4 ? f[2] : 1 - f[2]);
        const size_t o = ((size_t(b) * n + size_t(gg)) * n + size_t(r)) * 3;
        for (int c = 0; c < 3; ++c) out[c] += w * g.rgb[o + size_t(c)];
    }
}
}  // namespace

bool gameGradingClutFromDds(const std::string& d, GameGradingClut& out, std::string* err) {
    const auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    if (d.size() < 148 || d.compare(0, 4, "DDS ") != 0) return fail("not a DDS");
    const uint32_t h = rd32(d, 12), w = rd32(d, 16), depth = rd32(d, 24);
    if (d.compare(84, 4, "DX10") != 0) return fail("no DX10 header");
    const uint32_t fmt = rd32(d, 128), dim = rd32(d, 132);
    if (dim != 4 || w != h || w != depth || w < 2 || w > 128) return fail("not a cube-sized 3D texture");
    const size_t n = w, count = n * n * n;
    const size_t bpp = fmt == 11 || fmt == 10 ? 8 : fmt == 28 ? 4 : 0;
    if (!bpp) return fail("unsupported pixel format");
    if (d.size() < 148 + count * bpp) return fail("truncated");
    out.size = int(n);
    out.rgb.resize(count * 3);
    for (size_t i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c) {
            const size_t o = 148 + i * bpp;
            float v = 0;
            if (fmt == 11) {
                uint16_t u;
                std::memcpy(&u, d.data() + o + size_t(c) * 2, 2);
                v = float(u) / 65535.0f;
            } else if (fmt == 10) {
                uint16_t u;
                std::memcpy(&u, d.data() + o + size_t(c) * 2, 2);
                v = halfToFloat(u);
            } else {
                v = float(uint8_t(d[o + size_t(c)])) / 255.0f;
            }
            out.rgb[i * 3 + size_t(c)] = v;
        }
    return true;
}

void gameTonemap(const double in[3], double out[3], const GameGradingClut* grade) {
    // 1. Rec.709 -> Rec.2020; 2. the vision's grading CLUT in PQ space (when given); 3. RRT via AP0 (Rec.2020 -> XYZ ->
    // AP0), then AP1 -> XYZ -> Rec.2020.
    double c[3], xyz[3], ap0[3], ap1[3];
    mul(kRec709To2020, in, c);
    for (double& v : c) v = std::max(0.0, v);
    if (grade && grade->valid()) {
        double uvw[3], q[3];
        for (int k = 0; k < 3; ++k) uvw[k] = pqEncode(std::min(c[k] / 1024.0, 1.0)) * 0.96875 + 0.015625;
        sampleClut(*grade, uvw, q);
        for (int k = 0; k < 3; ++k) c[k] = pqDecode(q[k]) * 1024.0;
    }'''),
 ('''std::vector<uint8_t> buildGameTonemapLut(int size) {''', '''std::vector<uint8_t> buildGameTonemapLut(int size, const GameGradingClut* grade) {'''),
 ('''                gameTonemap(in, out);
                for (int k = 0; k < 3; ++k) lut[o++] = uint8_t(std::lround(std::clamp(out[k], 0.0, 1.0) * 255.0));''',
  '''                gameTonemap(in, out, grade);
                for (int k = 0; k < 3; ++k) lut[o++] = uint8_t(std::lround(std::clamp(out[k], 0.0, 1.0) * 255.0));'''),
])
s = io.open('src/core/game_look.cpp', encoding='utf-8', newline='').read()
if '#include <cstring>' not in s:
    s = s.replace('#include <cmath>', '#include <cmath>\n#include <cstring>', 1)
    io.open('src/core/game_look.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('tests/game_look_tests.cpp', [
 ('''TEST(game_look_fog_from_vision) {''', '''TEST(game_look_grading_clut) {
    CHECK(std::fabs(pqDecode(pqEncode(0.01)) - 0.01) < 1e-9 && std::fabs(pqEncode(0.01) - 0.508) < 0.01);  // 100 nits
    // An identity CLUT (PQ code in = out) leaves the curve as it is, within the trilinear error.
    GameGradingClut id;
    id.size = 32;
    id.rgb.resize(32 * 32 * 32 * 3);
    for (int b = 0; b < 32; ++b)
        for (int g = 0; g < 32; ++g)
            for (int r = 0; r < 32; ++r) {
                const size_t o = ((size_t(b) * 32 + size_t(g)) * 32 + size_t(r)) * 3;
                id.rgb[o] = float(r) / 31.0f, id.rgb[o + 1] = float(g) / 31.0f, id.rgb[o + 2] = float(b) / 31.0f;
            }
    for (double x : {0.02, 0.18, 0.5, 2.0, 8.0}) {
        const double in[3] = {x, x * 0.8, x * 0.5};
        double a[3], b[3];
        gameTonemap(in, a);
        gameTonemap(in, b, &id);
        for (int k = 0; k < 3; ++k) CHECK(std::fabs(a[k] - b[k]) < 0.02);
    }
    std::string err;
    GameGradingClut bad;
    CHECK(!gameGradingClutFromDds("DDS nope", bad, &err) && !err.empty());
}

TEST(game_look_fog_from_vision) {'''),
])

# ---------------------------------------------------------------- camera: the stock map's grade
patch('src/qtapp/camera_view.h', [
 ('''    // The stock sky material's intensity (its first pixel constant): the sky image times this is the drawn sky.''',
  '''    // The stock map's grading CLUT (its vision's): baked into the colour LUT.
    void setGameStockGrading(std::shared_ptr<const GameGradingClut> grade) {
        stockGrade_ = std::move(grade);
        gameLutReady_ = false;
        update();
    }
    // The stock sky material's intensity (its first pixel constant): the sky image times this is the drawn sky.'''),
 ('''    double stockSkyIntensity_ = 0;''', '''    double stockSkyIntensity_ = 0;
    std::shared_ptr<const GameGradingClut> stockGrade_;'''),
 ('''stockSunKnown_ = false, stockFog_ = GameFog{}, stockSkyIntensity_ = 0;''',
  '''stockSunKnown_ = false, stockFog_ = GameFog{}, stockSkyIntensity_ = 0, stockGrade_.reset(), gameLutReady_ = false;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''            const std::vector<uint8_t> lut = buildGameTonemapLut(64);''',
  '''            const std::vector<uint8_t> lut = buildGameTonemapLut(64, !gameStockMap_.empty() && stockGrade_ ? stockGrade_.get() : nullptr);'''),
])

# ---------------------------------------------------------------- main window: export the grade with the vision
patch('src/qtapp/main_window.cpp', [
 ('''        // The sky material's first pixel constant scales its image''', '''        // The vision's grading CLUT (field NONMKTKNNM, image "gradingclut<name>", a 3D texture in the map zone).
        std::shared_ptr<GameGradingClut> grade;
        {
            std::string clutName;
            for (size_t pos = visionText.find("NONMKTKNNM"); pos != std::string::npos && clutName.empty();
                 pos = visionText.find("NONMKTKNNM", pos + 1)) {
                const size_t q1 = visionText.find('"', pos), q2 = q1 == std::string::npos ? q1 : visionText.find('"', q1 + 1);
                const size_t eol = visionText.find('\\n', pos);
                if (q2 != std::string::npos && (eol == std::string::npos || q2 < eol)) clutName = visionText.substr(q1 + 1, q2 - q1 - 1);
            }
            if (!clutName.empty()) {
                const QString image = "gradingclut" + QString::fromStdString(clutName);
                const QString clutDir = QFileInfo(dir).absolutePath() + "/grading";
                const auto findDds = [&] {
                    for (QDirIterator it(clutDir, {"*.dds"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                        const QString f = it.next();
                        if (QFileInfo(f).fileName().startsWith(image + ".")) return f;
                    }
                    return QString();
                };
                QString dds = findDds();
                const QString techsets = QFileInfo(ff).absolutePath() + "/techsets_" + zone + ".ff";
                if (dds.isEmpty() && QFileInfo::exists(acts))
                    for (const QString& z : {ff, techsets}) {
                        if (!QFileInfo::exists(z)) continue;
                        QProcess p;
                        p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "image", "-n",
                                       image, "-l", "-o", clutDir, z});
                        p.waitForFinished(120000);
                        dds = findDds();
                        if (!dds.isEmpty()) break;
                    }
                std::string bytes;
                auto g = std::make_shared<GameGradingClut>();
                if (!dds.isEmpty() && readFile(dds.toStdString(), bytes) && gameGradingClutFromDds(bytes, *g)) grade = g;
            }
        }
        // The sky material's first pixel constant scales its image'''),
 ('''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity] {
            if (!self || self->referenceZone_ != zone) return;''', '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade] {
            if (!self || self->referenceZone_ != zone) return;
            if (grade) self->camera_->setGameStockGrading(grade);'''),
])
print('ok')
