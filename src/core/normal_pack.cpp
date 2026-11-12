#include "normal_pack.h"

#include <algorithm>
#include <cmath>

namespace iw8r {

namespace {
uint8_t toByte(double v) { return uint8_t(std::lround(std::clamp(v, 0.0, 255.0))); }
}  // namespace

void replayNormalSlopes(uint8_t g, uint8_t a, double& slopeX, double& slopeY) {
    const double s = g / 127.5 - 1.0;  // ox + oy
    const double d = a / 127.5 - 1.0;  // ox - oy
    const double ox = (s + d) * 0.5, oy = (s - d) * 0.5;
    // 1 + |sx| + |sy| = 1 / (1 - |ox| - |oy|); kept away from the edge of the octahedron (a horizontal normal).
    const double k = std::max(0.02, 1.0 - std::fabs(ox) - std::fabs(oy));
    slopeX = ox / k;
    slopeY = oy / k;
}

void replayNormalPack(double slopeX, double slopeY, uint8_t& g, uint8_t& a) {
    const double den = 1.0 + std::fabs(slopeX) + std::fabs(slopeY);
    const double ox = slopeX / den, oy = slopeY / den;
    g = toByte((1.0 + ox + oy) * 127.5);
    a = toByte((1.0 + ox - oy) * 127.5);
}

void slopeMapBytes(double slopeX, double slopeY, uint8_t& r, uint8_t& g) {
    r = toByte((slopeX + 2.07999992) / 4.07999992 * 255.0);
    g = toByte((slopeY + 2.06451607) / 4.06451607 * 255.0);
}

void replayNormalToSlopeMap(const uint8_t* packedRgba, size_t texels, uint8_t* slopeRgba) {
    for (size_t i = 0; i < texels; ++i) {
        double sx, sy;
        replayNormalSlopes(packedRgba[i * 4 + 1], packedRgba[i * 4 + 3], sx, sy);
        slopeMapBytes(sx, sy, slopeRgba[i * 4], slopeRgba[i * 4 + 1]);
        slopeRgba[i * 4 + 2] = 255;
        slopeRgba[i * 4 + 3] = 255;
    }
}

void replaySpecularMap(const uint8_t* colorRgba, const uint8_t* packedRgba, size_t texels, uint8_t* specRgba) {
    for (size_t i = 0; i < texels; ++i) {
        const double dielectric = colorRgba ? colorRgba[i * 4 + 3] / 255.0 : 1.0;
        for (int c = 0; c < 3; ++c) {
            const double albedo = colorRgba ? colorRgba[i * 4 + c] : 56.0;
            specRgba[i * 4 + c] = toByte(albedo + (56.0 - albedo) * dielectric);
        }
        specRgba[i * 4 + 3] = packedRgba[i * 4];
    }
}

}  // namespace iw8r
