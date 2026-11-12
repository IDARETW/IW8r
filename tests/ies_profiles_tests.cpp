// IES profiles (roadmap 5.13): LM-63 parsing, candela lookup with symmetry, and the polar preview curve.
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ies_profiles.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool close(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

// Hand-written LM-63 table, type C, TILT=NONE. 5 vertical angles, 3 C planes (0, 45, 90: quadrant symmetric),
// multiplier 2. Candela per plane, vertical 0/30/60/90/180:
//   plane 0:  100  80  50  20   0
//   plane 45:  60  40  30  10   0
//   plane 90:  40  30  20   5   0
const std::string kSample =
    "IESNA:LM-63-2002\n"
    "[TEST] hand-written test table\n"
    "[MANUFAC] IW8 Radiant tests\n"
    "TILT=NONE\n"
    "1 1000 2 5 3 1 1 0.1 0.1 0.1 1 1 20\n"
    "0 30 60 90 180\n"
    "0 45 90\n"
    "100 80 50 20 0\n"
    "60 40 30 10 0\n"
    "40 30 20 5 0\n";

// Replaces the first occurrence; a missing `from` leaves the text unchanged, so the rejection test then fails.
std::string replaced(std::string s, const std::string& from, const std::string& to) {
    const auto at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

// True when the text is refused, the error says why, and the target profile is left as it was.
bool rejects(const std::string& text) {
    IesProfile target;
    target.multiplier = 42.0;
    target.candela = {1.0};
    std::string err;
    const bool ok = parseIesFile(text, target, &err);
    return !ok && !err.empty() && target.multiplier == 42.0 && target.candela.size() == 1;
}

// Hand-built profile for the symmetry and wrap cases.
IesProfile manual(std::vector<double> v, std::vector<double> h, std::vector<double> candela) {
    IesProfile p;
    p.verticalAngles = std::move(v);
    p.horizontalAngles = std::move(h);
    p.candela = std::move(candela);
    return p;
}

}  // namespace

TEST(ies_parse_lm63_table) {
    IesProfile p;
    std::string err;
    CHECK(parseIesFile(kSample, p, &err));
    CHECK(err.empty());
    CHECK(p.verticalAngles.size() == 5);
    CHECK(p.horizontalAngles.size() == 3);
    CHECK(p.candela.size() == 15);
    CHECK(close(p.multiplier, 2.0));
    CHECK(close(p.verticalAngles[4], 180.0));
    CHECK(close(p.horizontalAngles[2], 90.0));
    CHECK(close(p.candela[0], 100.0));  // plane 0, vertical 0
    CHECK(close(p.candela[5], 60.0));   // plane 45 starts after the five vertical values of plane 0
    CHECK(close(p.candela[14], 0.0));   // plane 90, vertical 180
}

TEST(ies_parse_crlf_and_trailing_numbers) {
    std::string crlf;
    for (char c : kSample) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    IesProfile p;
    CHECK(parseIesFile(crlf, p, nullptr));
    CHECK(p.candela.size() == 15);
    IesProfile q;
    CHECK(parseIesFile(kSample + " 7 7\n", q, nullptr));  // numbers past the table are ignored
    CHECK(q.candela.size() == 15);
}

TEST(ies_intensity_table_angles) {
    IesProfile p;
    CHECK(parseIesFile(kSample, p, nullptr));
    // Multiplier 2 is applied: every value is twice the table.
    CHECK(close(iesIntensity(p, 0, 0), 200.0));
    CHECK(close(iesIntensity(p, 30, 0), 160.0));
    CHECK(close(iesIntensity(p, 60, 0), 100.0));
    CHECK(close(iesIntensity(p, 90, 0), 40.0));
    CHECK(close(iesIntensity(p, 180, 0), 0.0));
    CHECK(close(iesIntensity(p, 0, 45), 120.0));
    CHECK(close(iesIntensity(p, 90, 45), 20.0));
    CHECK(close(iesIntensity(p, 0, 90), 80.0));
    CHECK(close(iesIntensity(p, 180, 90), 0.0));
}

TEST(ies_intensity_interpolated) {
    IesProfile p;
    CHECK(parseIesFile(kSample, p, nullptr));
    CHECK(close(iesIntensity(p, 45, 0), 130.0));    // plane 0, halfway between 30 and 60: 65 x 2
    CHECK(close(iesIntensity(p, 0, 22.5), 160.0));  // halfway between plane 0 (100) and plane 45 (60): 80 x 2
    CHECK(close(iesIntensity(p, 45, 22.5), 100.0)); // both: (65 + 35) / 2 x 2
}

TEST(ies_intensity_clamps_vertical) {
    IesProfile p;
    CHECK(parseIesFile(kSample, p, nullptr));
    CHECK(close(iesIntensity(p, -10, 0), 200.0));   // below the first vertical angle: its value
    CHECK(close(iesIntensity(p, 200, 0), 0.0));     // above the last: its value
    CHECK(iesIntensity(p, std::nan(""), 0) == 0.0); // non-finite input gives nothing, not NaN
}

TEST(ies_symmetry_quadrant) {
    // The table ends at 90: planes 0..90 are mirrored at 0 and at 90.
    IesProfile p;
    CHECK(parseIesFile(kSample, p, nullptr));
    CHECK(close(iesIntensity(p, 30, 180), iesIntensity(p, 30, 0)));
    CHECK(close(iesIntensity(p, 30, 270), iesIntensity(p, 30, 90)));
    CHECK(close(iesIntensity(p, 30, 135), iesIntensity(p, 30, 45)));
    CHECK(close(iesIntensity(p, 30, 315), iesIntensity(p, 30, 45)));
    CHECK(close(iesIntensity(p, 0, -45), 120.0));  // negative angles fold too
    CHECK(close(iesIntensity(p, 0, 270), 80.0));   // plane 90 at vertical 0: 40 x 2
}

TEST(ies_symmetry_half) {
    // The table ends at 180: mirrored at 0 only, so 270 folds to 90 and 200 to 160.
    const IesProfile p = manual({0, 90}, {0, 90, 180}, {10, 0, 20, 0, 30, 0});
    CHECK(close(iesIntensity(p, 0, 270), 20.0));
    CHECK(close(iesIntensity(p, 0, 90), 20.0));
    CHECK(close(iesIntensity(p, 0, 200), 20.0 + 10.0 * (70.0 / 90.0)));
}

TEST(ies_full_turn_wraps) {
    // The table ends at 240, so the gap from 240 to 360 wraps to plane 0.
    const IesProfile p = manual({0, 90}, {0, 120, 240}, {10, 0, 20, 0, 30, 0});
    CHECK(close(iesIntensity(p, 0, 0), 10.0));
    CHECK(close(iesIntensity(p, 0, 60), 15.0));   // between 0 and 120
    CHECK(close(iesIntensity(p, 0, 180), 25.0));  // between 120 and 240
    CHECK(close(iesIntensity(p, 0, 300), 20.0));  // between 240 and 360 (= plane 0)
}

TEST(ies_single_plane_and_empty) {
    const IesProfile one = manual({0, 90}, {0}, {5, 1});
    CHECK(close(iesIntensity(one, 45, 123), 3.0));  // one plane: the same at every horizontal angle
    CHECK(close(iesIntensity(one, 45, 0), 3.0));
    CHECK(iesIntensity(IesProfile{}, 0, 0) == 0.0);
}

TEST(ies_polar_curve) {
    IesProfile p;
    CHECK(parseIesFile(kSample, p, nullptr));
    const auto curve = iesPolarCurve(p, 5);  // vertical 0, 45, 90, 135, 180 on plane 0
    CHECK(curve.size() == 5);
    if (curve.size() == 5) {
        CHECK(std::fabs(curve[0] - 1.0f) < 1e-5f);  // peak is 1
        CHECK(std::fabs(curve[1] - 0.65f) < 1e-5f);
        CHECK(std::fabs(curve[2] - 0.20f) < 1e-5f);
        CHECK(std::fabs(curve[3] - 0.10f) < 1e-5f);
        CHECK(std::fabs(curve[4]) < 1e-5f);
    }
    CHECK(iesPolarCurve(p, 37).size() == 37);
    CHECK(iesPolarCurve(p, 1).empty());
    CHECK(iesPolarCurve(p, 0).empty());
    CHECK(iesPolarCurve(IesProfile{}, 16).empty());
    const auto dark = iesPolarCurve(manual({0, 90}, {0, 90}, {0, 0, 0, 0}), 4);
    CHECK(dark.size() == 4);
    for (float c : dark) CHECK(c == 0.0f);  // a dark plane stays dark, no division by zero
}

TEST(ies_reject_bad_input) {
    CHECK(rejects(""));
    CHECK(rejects("TILT=NONE\n1 1000 2 5 3 1 1 0.1 0.1 0.1 1 1 20\n"));                         // no IESNA header
    CHECK(rejects(replaced(kSample, "IESNA:LM-63-2002", "LM-63-2002")));                         // not IESNA
    CHECK(rejects("IESNA:LM-63-2002\n[TEST] no tilt line\n1 1000 2 5 3 1 1 0.1 0.1 0.1 1 1 20\n")); // no TILT=
    CHECK(rejects(replaced(kSample, "TILT=NONE", "TILT=INCLUDE")));                              // tilt needs a second source
    CHECK(rejects(replaced(kSample, "TILT=NONE", "TILT=mytilt.txt")));
    CHECK(rejects(replaced(kSample, "1 1000 2 5 3 1 1", "1 1000 2 5 3 2 1")));                    // type B
    CHECK(rejects(replaced(kSample, "1 1000 2 5 3 1 1", "1 1000 2 5 3 1 3")));                    // bad units
    CHECK(rejects(replaced(kSample, "1 1000 2 5 3", "1 1000 0 5 3")));                            // multiplier 0
    CHECK(rejects(replaced(kSample, "1 1000 2 5 3", "1 1000 2 5 2.5")));                          // fractional count
    CHECK(rejects(replaced(kSample, "0 30 60 90 180", "0 60 30 90 180")));                        // not increasing
    CHECK(rejects(replaced(kSample, "0 30 60 90 180", "0 30 60 90 200")));                        // beyond 180
    CHECK(rejects(replaced(kSample, "0 45 90", "0 45 400")));                                    // beyond 360
    CHECK(rejects(replaced(kSample, "40 30 20 5 0", "40 30 20 5 -1")));                          // negative candela
    CHECK(rejects(replaced(kSample, "100 80 50 20 0", "100 80 abc 20 0")));                      // not a number
    CHECK(rejects(replaced(kSample, "40 30 20 5 0\n", "40 30 20 5\n")));                          // truncated table
    CHECK(rejects("IESNA:LM-63-2002\nTILT=NONE\n"));                                              // header only
}

TEST(ies_replay_texels_follow_the_cone) {
    // A downlight: full intensity on the axis, half at 30 degrees, nothing past 60.
    const std::string text =
        "IESNA:LM-63-2002\nTILT=NONE\n1 1000 1 4 1 1 2 0.1 0.1 0.1\n1 1 100\n0 30 60 90\n0\n1000 500 0 0\n";
    IesProfile p;
    std::string err;
    CHECK(parseIesFile(text, p, &err));
    const auto t = iesReplayTexels(p);
    CHECK(t.size() == 128);
    if (t.size() != 128) return;
    CHECK(t[0] == 255);
    // texel j is at asin(sqrt(2j/255)): 30 degrees is j = 255 * sin^2(30) / 2 = 31.9.
    CHECK(std::abs(t[32] - 128) <= 6);
    // 60 degrees: j = 255 * 0.75 / 2 = 95.6; beyond it the light is off.
    CHECK(t[100] == 0 && t[127] == 0);
    for (int j = 1; j < 128; ++j) CHECK(t[size_t(j)] <= t[size_t(j - 1)]);
}
