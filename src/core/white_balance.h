// The game's white balance (research 120): the 3x3 matrix its tone map pass applies to the linear scene colour,
// after the veil and the vignette and before the exposure and the grade (cs_tonemap CB0[0..2]). Read in the game-test
// source (gfx_d3d/r_whitebalance.cpp, R_WhiteBalance_SetMatrix); the constant tables are from the Replay 1.20 exe.
// The vision sets it per map: Hideout, Verdansk, Rust and others use method 4 (Von Kries) with a D illuminant of
// 5,400 to 5,700 K, which makes the picture warmer than the unbalanced render.
#pragma once

#include <string>

namespace iw8r {

struct GameWhiteBalance {
    // 0 scale in sRGB (white point only), 1 CAT02, 2 Sharp, 3 Bradford, 4 Von Kries, 5 XYZ scaling, 6 none.
    int method = 0;
    // 0 the vision's toneWhitePoint, 1 a D illuminant at colorTempK, 2 a black body at colorTempK, 3 .. 21 the CIE
    // illuminants A, B, C, D50, D55, D65, D75, F1 .. F12.
    int illuminant = 0;
    double colorTempK = 5000;
    double tint = 0;                     // green / magenta shift, in 1/3000 of a CIE 1960 uv unit across the locus
    double whitePoint[3] = {1, 1, 1};    // toneWhitePoint (linear sRGB), used by illuminant 0
};

// The white balance fields of a vision text (`TOKEN "value"` lines): whiteBalanceMethod MNMNSSLLOR,
// whiteBalanceIlluminant NLSRLSTPTN, whiteBalanceColorTempK TRQLLNNLS, whiteBalanceGreenMagentaShift OPSMSQNKQ,
// toneWhitePoint LRROOOSOMM. Fields that are missing keep the defaults above (which give the identity).
GameWhiteBalance gameWhiteBalanceFromVision(const std::string& visionText);

// The same from a map's worldspawn iw8r_vision overrides ("name=value;name=value"): the four white balance fields
// by their tokens (they have no names in the converter's table) and toneWhitePoint by name or token. Text that does
// not parse gives the defaults.
GameWhiteBalance gameWhiteBalanceFromOverrides(const std::string& overridesText);

// The matrix, row-major: out_r = m[0] r + m[1] g + m[2] b, and so on. Values out of range (an unknown method or
// illuminant, a white point with a zero) give the identity.
void gameWhiteBalanceMatrix(const GameWhiteBalance& wb, double m[9]);

// True when the matrix differs from the identity by more than 1e-6 in any entry.
bool gameWhiteBalanceActive(const GameWhiteBalance& wb);

// The CIE xy chromaticity of the D illuminant at a colour temperature (the game's formula; valid 4,000 to 25,000 K).
void dIlluminantChromaticity(double colorTempK, double xy[2]);
// The game's black body locus approximation.
void blackBodyChromaticity(double colorTempK, double xy[2]);
// Moves a chromaticity across the locus by `tint` (the game's IlluminantChromaticityGreenMagentaTint).
void tintChromaticity(double xy[2], double colorTempK, double tint, bool dIlluminant);

}  // namespace iw8r
