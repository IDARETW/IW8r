import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/game_look.h', [
('''#include <cstdint>
#include <vector>''', '''#include <cstdint>
#include <string>
#include <vector>'''),
('''// sRGB texture value to linear (the game samples colour maps as sRGB).''', '''// A map's exposure (the vision's tonemap fields; research 95 section 3). Manual: 180 x 2^(2.84 - ev + exposureAdjust).
// Auto: metered from the frame's log-average luminance between the dark / mid / light EVs and their adjustments.
struct GameExposure {
    bool autoExposure = false;
    double ev = 10.0, exposureAdjust = 0.0, maxEv = -10.0;
    double darkEv = 6.0, midEv = 11.0, lightEv = 15.0;
    double darkAdjust = -2.0, midAdjust = 0.0, lightAdjust = 1.5;
    double minAdjust = -10.0, maxAdjust = 10.0;  // the key's clamp (defaults INFERRED)
    double adaptSpeed = 1.0;
};
// The exposure for a frame whose log-average luminance (Rec.709) is avgLuminance: the manual value, or the auto
// exposure's target (key / avg). Without the +1.5 EV base.
double gameExposureFor(const GameExposure& e, double avgLuminance);
// Moves the shown exposure toward the target over dt seconds at the vision's adapt speed (in log space; the rate is
// INFERRED).
double gameAdaptExposure(double current, double target, double dt, double adaptSpeed);

// A shipped map's lighting (ACTS dumps of each zone's ComWorld sun and <map>.vision tonemap fields 63-76, the x64-zt
// converter's kIw8LightingReferences, iw3_vision.cpp): the sun intensity and colour and the auto exposure.
struct StockMapLook {
    const char* map;
    double sunIntensity;
    double sunColor[3];
    GameExposure exposure;
};
// The known shipped maps; null for others.
const StockMapLook* stockMapLook(const std::string& map);
const std::vector<StockMapLook>& stockMapLooks();

// sRGB texture value to linear (the game samples colour maps as sRGB).'''),
])
patch('src/core/game_look.cpp', [
('''double srgbToLinear(double c) {''', '''double gameExposureFor(const GameExposure& e, double avgLuminance) {
    const double manualCap = 180.0 * std::exp2(2.84 - e.maxEv);
    if (!e.autoExposure) return std::min(gameManualExposure(e.ev, e.exposureAdjust), manualCap);
    const double avg = std::max(avgLuminance, 1e-6);
    const auto L = [](double ev) { return 0.001 * std::exp2(ev - 2.84); };
    const auto k = [](double adjust) { return std::pow(0.18 * std::exp2(adjust), 1.0 / 2.2); };
    const double Ld = L(e.darkEv), Ll = L(std::max(e.lightEv, e.darkEv + 0.01)), Lm = L(e.midEv);
    const double kd = k(e.darkAdjust), kl = k(e.lightAdjust), km = k(e.midAdjust);
    const double span = std::log(Ll / Ld);
    const double t = std::max(std::log(avg / Ld) / span, 0.0);
    const double tm = std::clamp(std::log(Lm / Ld) / span, 1e-4, 1.0 - 1e-4);
    const double ratio = std::clamp((km - kd) / (kl - kd), 1e-4, 1.0 - 1e-4);
    const double p = std::log(ratio) / std::log(tm);
    double key = std::pow(std::pow(t, p) * (kl - kd) + kd, 2.2);
    key = std::clamp(key, 0.18 * std::exp2(e.minAdjust), 0.18 * std::exp2(e.maxAdjust));
    return std::min(key / avg * std::exp2(e.exposureAdjust), manualCap);
}

double gameAdaptExposure(double current, double target, double dt, double adaptSpeed) {
    if (!(current > 0) || !(target > 0)) return target;
    const double a = 1.0 - std::exp(-std::max(dt, 0.0) * std::max(adaptSpeed, 0.01) * 4.0);
    return std::exp(std::log(current) + (std::log(target) - std::log(current)) * a);
}

namespace {
GameExposure autoExposure(double ev, double adjust, double adapt, double dark, double mid, double light, double dAdj, double mAdj,
                          double lAdj) {
    GameExposure e;
    e.autoExposure = true;
    e.ev = ev, e.exposureAdjust = adjust, e.adaptSpeed = adapt;
    e.darkEv = dark, e.midEv = mid, e.lightEv = light;
    e.darkAdjust = dAdj, e.midAdjust = mAdj, e.lightAdjust = lAdj;
    return e;
}
}  // namespace

const std::vector<StockMapLook>& stockMapLooks() {
    // Fields 63-76 in order: mode 3, auto 1, curve 1, exposure, auto adjust 0, max EV -10, exposure adjust, adapt speed,
    // dark / mid / light EV, dark / mid / light adjust (the converter's table, same order).
    static const std::vector<StockMapLook> looks = {
        {"mp_m_king", 1.55685, {1.2781, 0.952621, 0.650445}, autoExposure(6, 0, 0.075, 4.56, 6.605912, 8.57, -1.6, -0.279, 1.28)},
        {"mp_emporium", 3.39796, {1.4395, 0.9178, 0.520111}, autoExposure(8.5, 0, 0.075, 5.6, 8.229471, 11.9, -2, -0.6758758, 1.5)},
        {"mp_shipment", 8.63193, {1.04877, 0.993345, 0.922306}, autoExposure(10.5, 0.6, 0.75, 6.1, 12.14071, 15, -2, -0.3869346, 1.5)},
        {"mp_village2", 11.0536, {0.983046, 1.00121, 1.0379}, autoExposure(10, 0, 0.075, 8, 11, 14, -1.5, 0, 1.5)},
        {"mp_br_quarry", 11.512, {1.14193, 0.976265, 0.817178}, autoExposure(12, 0, 0.15, 9, 11.75, 16, -1, 0.15, 2)},
        {"mp_rust", 11.6969, {1.11411, 0.989194, 0.771041}, autoExposure(10, 0, 0.06, 7.53, 10.3, 14, -3, -1, 1.3)},
        {"mp_backlot2", 20.3368, {0.98634, 1.00452, 0.995408}, autoExposure(12, -0.1, 0.075, 6.2, 11.23978, 16, -5, -0.9157199, 3.2)},
        {"mp_m_fork", 23.7478, {1.02338, 0.996126, 0.969523}, autoExposure(11.5, 0, 0.075, 5.6, 10.31878, 14.5, -2.38, -1.024797, 1.5)},
        {"mp_m_speed", 23.9449, {0.97691, 1.00372, 1.03118}, autoExposure(14, 0, 0.075, 7, 11.34573, 15.6, -1, 0.05577891, 1.6)},
        {"mp_aniyah_tac", 24.6499, {0.994142, 1.00325, 0.985086}, autoExposure(12, 0, 0.075, 8.6, 12.49304, 16.2, -3.2, -0.7722935, 2.2)},
        {"mp_t_gun_course", 25.8181, {1.13566, 0.979416, 0.804427}, autoExposure(14, 0, 0.1, 11.5, 13.25, 15, -0.25, 0.4, 0.75)},
        {"mp_hideout", 26.9233, {0.984644, 1.00247, 1.02072}, autoExposure(12, 0, 0.075, 9, 10.7015, 15, -3, -0.9118453, 2)},
        {"mp_m_speedball", 30.6723, {1.00331, 1.00331, 0.95747}, autoExposure(13, 0.65, 0.075, 10.8, 13.21207, 14.6, -0.5, 0.04669083, 1.5)},
    };
    return looks;
}

const StockMapLook* stockMapLook(const std::string& map) {
    for (const StockMapLook& l : stockMapLooks())
        if (map == l.map) return &l;
    return nullptr;
}

double srgbToLinear(double c) {'''),
])
patch('tests/game_look_tests.cpp', [
('''TEST(game_look_local_lights) {''', '''TEST(game_look_auto_exposure) {
    const StockMapLook* ship = stockMapLook("mp_shipment");
    CHECK(ship && ship->exposure.autoExposure && std::fabs(ship->sunIntensity - 8.63193) < 1e-9 && !stockMapLook("mp_nowhere"));
    if (!ship) return;
    // At the mid EV's luminance the key is the mid key (0.18 x 2^midAdjust), times 2^exposureAdjust.
    const GameExposure& e = ship->exposure;
    const double Lmid = 0.001 * std::exp2(e.midEv - 2.84);
    const double expected = 0.18 * std::exp2(e.midAdjust) / Lmid * std::exp2(e.exposureAdjust);
    CHECK(std::fabs(gameExposureFor(e, Lmid) / expected - 1.0) < 1e-6);
    // Brighter scenes get less exposure; the product (the displayed key) still rises.
    const double dim = gameExposureFor(e, Lmid * 0.25), bright = gameExposureFor(e, Lmid * 4);
    CHECK(dim > bright && bright * Lmid * 4 > dim * Lmid * 0.25);
    // Manual exposure ignores the luminance; adaptation moves toward the target and never past it.
    GameExposure m;
    CHECK(std::fabs(gameExposureFor(m, 5.0) - gameManualExposure(10.0)) < 1e-12);
    const double a = gameAdaptExposure(1.0, 4.0, 0.1, 1.0);
    CHECK(a > 1.0 && a < 4.0 && std::fabs(gameAdaptExposure(1.0, 4.0, 100.0, 1.0) - 4.0) < 1e-6);
}

TEST(game_look_local_lights) {'''),
])
print('ok')
