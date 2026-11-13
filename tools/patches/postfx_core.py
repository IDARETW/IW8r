import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/game_look.h'
C = 'src/core/game_look.cpp'

patch(H, '''// The inscattered radiance scale, in scene light units.
double gameFogInscatter(const GameFog& f);''', '''// The inscattered radiance scale, in scene light units.
double gameFogInscatter(const GameFog& f);

// The vision's post effects (research 109): the bloom veil, the vignette and the film grain. Defaults are the game's
// (visionDefFields): a map that leaves a field out gets none of that effect.
struct GamePostFx {
    double veil = 0;  // veilStrength (MMQLTMKTPK): hdr = (1 - veil) hdr + veil bloom
    double vignetteIntensity = 0, vignetteSquareWeight = 0, vignetteSize[2] = {1, 1}, vignetteFalloff = 1, vignetteFalloffStart = 0;
    double vignetteBox[2] = {0, 0}, vignetteOffset[2] = {0, 0};
    bool vignetteImage = false;  // vignetteImage set (not drawn by the editor: the analytic shape stands in)
    double grain = 0;            // tonemapGrainStrength (LNQNRRPNPP) x r_filmGrainAtten 0.25
};
GamePostFx gamePostFxFromVision(const std::string& visionText);
// The vignette's constants (cb7[214..216], research 109 section 4.1) for a view whose half-FOV tangents are tanX, tanY:
// out[0..3] = (scaleX, scaleY, intensity, 0), out[4..7] = (offsetX, offsetY, boxX, boxY),
// out[8..11] = (1 / falloff, -falloffStart / falloff, 0, 0). All zero when the intensity is 0.
void gameVignetteConstants(const GamePostFx& p, double tanX, double tanY, float out[12]);
// The vignette's factor (multiplying linear HDR) at screen position (u, v) in [0, 1]^2 (CPU reference of the shader).
double gameVignetteFactor(const float constants[12], double u, double v);''')
patch(C, '''GameFog gameFogFromVision(const std::string& text) {''', '''namespace {
// `TOKEN "value"` lines of a vision text.
std::map<std::string, std::string> visionValues(const std::string& text) {
    std::map<std::string, std::string> v;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string key = line.substr(0, q1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\\t')) key.pop_back();
        while (!key.empty() && (key.front() == ' ' || key.front() == '\\t')) key.erase(key.begin());
        v[key] = line.substr(q1 + 1, q2 - q1 - 1);
    }
    return v;
}
}  // namespace

GamePostFx gamePostFxFromVision(const std::string& text) {
    const std::map<std::string, std::string> v = visionValues(text);
    GamePostFx p;
    const auto num = [&](const char* tok, double& out) {
        const auto it = v.find(tok);
        if (it == v.end() || it->second.empty()) return;
        char* e = nullptr;
        const double x = std::strtod(it->second.c_str(), &e);
        if (e != it->second.c_str() && std::isfinite(x)) out = x;
    };
    num("MMQLTMKTPK", p.veil);
    num("NKMMLMPOKK", p.vignetteIntensity);
    num("NQNMQNTTRL", p.vignetteSquareWeight);
    num("LPPRTKTTO", p.vignetteSize[0]);
    num("MMMSOLKOMR", p.vignetteSize[1]);
    num("RSPMMQLQN", p.vignetteFalloff);
    num("LNTKQTQOMN", p.vignetteFalloffStart);
    num("OLOOSTRSMR", p.vignetteBox[0]);
    num("MKRMMRSNTO", p.vignetteBox[1]);
    num("NQTKKPORNO", p.vignetteOffset[0]);
    num("LOQRRKQSRL", p.vignetteOffset[1]);
    if (const auto it = v.find("MPKKMPRLRQ"); it != v.end() && !it->second.empty()) p.vignetteImage = true;
    double grain = 0;
    num("LNQNRRPNPP", grain);
    p.grain = grain * 0.25;  // r_filmGrainAtten
    p.veil = std::clamp(p.veil, 0.0, 1.0);
    p.vignetteIntensity = std::clamp(p.vignetteIntensity, 0.0, 1.0);
    return p;
}

void gameVignetteConstants(const GamePostFx& p, double tanX, double tanY, float out[12]) {
    for (int k = 0; k < 12; ++k) out[k] = 0;
    if (p.vignetteIntensity <= 0) return;
    const double ax = std::max(tanX / std::max(tanY, 1e-6), 1.0), ay = std::max(tanY / std::max(tanX, 1e-6), 1.0);
    const double k = 0.70710677;  // the analytic shape (the image variant uses 1)
    const double w = std::clamp(p.vignetteSquareWeight, 0.0, 1.0);
    out[0] = float(2 * (w * ax + (1 - w) * k) / std::max(p.vignetteSize[0], 1e-6));
    out[1] = float(2 * (w * ay + (1 - w) * k) / std::max(p.vignetteSize[1], 1e-6));
    out[2] = float(p.vignetteIntensity);
    out[4] = float(p.vignetteOffset[0]), out[5] = float(p.vignetteOffset[1]);
    out[6] = float(p.vignetteBox[0]), out[7] = float(p.vignetteBox[1]);
    const double f = std::max(p.vignetteFalloff, 0.001);
    out[8] = float(1 / f), out[9] = float(-p.vignetteFalloffStart / f);
}

double gameVignetteFactor(const float c[12], double u, double v) {
    if (c[2] <= 0) return 1.0;
    const double px = (u - 0.5) * c[0] + c[4], py = (v - 0.5) * c[1] + c[5];
    const double qx = px - std::clamp(px, -double(c[6]), double(c[6])), qy = py - std::clamp(py, -double(c[7]), double(c[7]));
    const double r = std::sqrt(qx * qx + qy * qy);
    const double t = std::clamp(r * c[8] + c[9], 0.0, 1.0);
    const double s = t + 0.6 * (t * t * (3 - 2 * t) - t);
    return 1.0 - s * c[2];
}

GameFog gameFogFromVision(const std::string& text) {''')
print('ok')
