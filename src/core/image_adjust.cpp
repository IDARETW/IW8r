#include "image_adjust.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace iw8r {

namespace {

double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

uint8_t toByte(double v) {
    return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0));
}

// Rotates the hue of an RGB colour (all in 0..1) by degrees. Saturation and value are kept.
void rotateHue(double& r, double& g, double& b, double deg) {
    double maxc = std::max(r, std::max(g, b));
    double minc = std::min(r, std::min(g, b));
    double delta = maxc - minc;
    if (delta <= 1e-12 || maxc <= 0.0) return;  // grey or black: no hue

    double h;
    if (maxc == r) {
        h = std::fmod((g - b) / delta, 6.0);
    } else if (maxc == g) {
        h = (b - r) / delta + 2.0;
    } else {
        h = (r - g) / delta + 4.0;
    }
    h *= 60.0;
    if (h < 0.0) h += 360.0;

    double s = delta / maxc;
    double v = maxc;
    h = std::fmod(h + deg, 360.0);
    if (h < 0.0) h += 360.0;

    double c = v * s;
    double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c;
    double rr = 0, gg = 0, bb = 0;
    int sector = static_cast<int>(h / 60.0) % 6;
    switch (sector) {
        case 0: rr = c; gg = x; bb = 0; break;
        case 1: rr = x; gg = c; bb = 0; break;
        case 2: rr = 0; gg = c; bb = x; break;
        case 3: rr = 0; gg = x; bb = c; break;
        case 4: rr = x; gg = 0; bb = c; break;
        default: rr = c; gg = 0; bb = x; break;
    }
    r = rr + m;
    g = gg + m;
    b = bb + m;
}

// One box or bilinear tap list per destination index along one axis.
struct Tap {
    int index;
    double weight;
};

std::vector<std::vector<Tap>> buildTaps(int srcLen, int dstLen) {
    std::vector<std::vector<Tap>> taps(static_cast<size_t>(dstLen));
    const double scale = static_cast<double>(srcLen) / static_cast<double>(dstLen);
    for (int d = 0; d < dstLen; ++d) {
        auto& list = taps[static_cast<size_t>(d)];
        if (dstLen < srcLen) {
            // Box filter: average every source pixel covered by this destination pixel.
            int start = static_cast<int>(std::floor(d * scale));
            int end = static_cast<int>(std::floor((d + 1) * scale));
            if (end <= start) end = start + 1;
            if (end > srcLen) end = srcLen;
            if (start >= end) start = end - 1;
            double w = 1.0 / static_cast<double>(end - start);
            for (int s = start; s < end; ++s) list.push_back({s, w});
        } else {
            // Bilinear: sample at pixel centres.
            double sc = (d + 0.5) * scale - 0.5;
            sc = std::clamp(sc, 0.0, static_cast<double>(srcLen - 1));
            int i0 = static_cast<int>(std::floor(sc));
            int i1 = std::min(i0 + 1, srcLen - 1);
            double f = sc - i0;
            if (i1 == i0 || f <= 0.0) {
                list.push_back({i0, 1.0});
            } else {
                list.push_back({i0, 1.0 - f});
                list.push_back({i1, f});
            }
        }
    }
    return taps;
}

}  // namespace

bool isIdentity(const ImageAdjust& a) {
    if (a.brightness != 0.0 || a.contrast != 1.0 || a.saturation != 1.0) return false;
    if (std::fmod(a.hueShiftDeg, 360.0) != 0.0) return false;
    if (a.gamma != 1.0 || a.invert) return false;
    for (double t : a.tint) {
        if (t != 1.0) return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (a.channelMap[i] != i) return false;
    }
    return true;
}

void adjustImage(std::vector<uint8_t>& rgba, int width, int height, const ImageAdjust& a) {
    if (width <= 0 || height <= 0) return;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (rgba.size() != count * 4) return;
    if (isIdentity(a)) return;

    const double gammaInv = 1.0 / std::max(a.gamma, 0.01);
    const bool doTint = a.tint[0] != 1.0 || a.tint[1] != 1.0 || a.tint[2] != 1.0;
    const bool doBright = a.brightness != 0.0;
    const bool doContrast = a.contrast != 1.0;
    const bool doSat = a.saturation != 1.0;
    const bool doHue = std::fmod(a.hueShiftDeg, 360.0) != 0.0;
    const bool doGamma = a.gamma != 1.0;

    for (size_t p = 0; p < count; ++p) {
        uint8_t* px = &rgba[p * 4];
        const uint8_t in[4] = {px[0], px[1], px[2], px[3]};

        // Channel map first. Alpha changes only when the map sends something else to it.
        uint8_t out[4];
        for (int i = 0; i < 4; ++i) {
            int m = a.channelMap[i];
            if (m == 4) {
                out[i] = 0;
            } else if (m == 5) {
                out[i] = 255;
            } else if (m >= 0 && m < 4) {
                out[i] = in[m];
            } else {
                out[i] = in[i];
            }
        }

        double c[3] = {out[0] / 255.0, out[1] / 255.0, out[2] / 255.0};
        if (doTint) {
            for (int k = 0; k < 3; ++k) c[k] *= a.tint[k];
        }
        if (doBright) {
            for (int k = 0; k < 3; ++k) c[k] += a.brightness;
        }
        if (doContrast) {
            for (int k = 0; k < 3; ++k) c[k] = (c[k] - 0.5) * a.contrast + 0.5;
        }
        for (int k = 0; k < 3; ++k) c[k] = clamp01(c[k]);
        if (doSat) {
            double lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
            for (int k = 0; k < 3; ++k) c[k] = clamp01(lum + (c[k] - lum) * a.saturation);
        }
        if (doHue) {
            rotateHue(c[0], c[1], c[2], a.hueShiftDeg);
        }
        if (doGamma) {
            for (int k = 0; k < 3; ++k) c[k] = std::pow(clamp01(c[k]), gammaInv);
        }
        if (a.invert) {
            for (int k = 0; k < 3; ++k) c[k] = 1.0 - c[k];
        }

        px[0] = toByte(c[0]);
        px[1] = toByte(c[1]);
        px[2] = toByte(c[2]);
        px[3] = out[3];
    }
}

bool resizeImage(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh, std::vector<uint8_t>& dst) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return false;
    const size_t srcBytes = static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4;
    if (src.size() != srcBytes) return false;

    const auto xTaps = buildTaps(sw, dw);
    const auto yTaps = buildTaps(sh, dh);

    dst.assign(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4, 0);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (const Tap& ty : yTaps[static_cast<size_t>(y)]) {
                for (const Tap& tx : xTaps[static_cast<size_t>(x)]) {
                    const double w = ty.weight * tx.weight;
                    const size_t si = (static_cast<size_t>(ty.index) * static_cast<size_t>(sw) +
                                       static_cast<size_t>(tx.index)) * 4;
                    for (int k = 0; k < 4; ++k) acc[k] += w * src[si + k];
                }
            }
            const size_t di = (static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 4;
            for (int k = 0; k < 4; ++k) {
                dst[di + k] = static_cast<uint8_t>(std::clamp(std::lround(acc[k]), 0L, 255L));
            }
        }
    }
    return true;
}

bool normalMapFromHeight(const std::vector<uint8_t>& src, int w, int h, double strength, bool wrap,
                         std::vector<uint8_t>& dst) {
    if (w <= 0 || h <= 0) return false;
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (src.size() != count * 4) return false;

    // Height in 0..1 from luminance.
    std::vector<double> height(count);
    for (size_t p = 0; p < count; ++p) {
        const uint8_t* px = &src[p * 4];
        height[p] = (0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]) / 255.0;
    }
    auto sample = [&](int x, int y) {
        if (wrap) {
            x = ((x % w) + w) % w;
            y = ((y % h) + h) % h;
        } else {
            x = std::clamp(x, 0, w - 1);
            y = std::clamp(y, 0, h - 1);
        }
        return height[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    };

    dst.assign(count * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double tl = sample(x - 1, y - 1), t = sample(x, y - 1), tr = sample(x + 1, y - 1);
            const double l = sample(x - 1, y), r = sample(x + 1, y);
            const double bl = sample(x - 1, y + 1), b = sample(x, y + 1), br = sample(x + 1, y + 1);
            // Sobel gradients per pixel, in height units (divided by the kernel weight of 8).
            const double gx = ((tr + 2.0 * r + br) - (tl + 2.0 * l + bl)) / 8.0;  // d/dx (right)
            const double gy = ((bl + 2.0 * b + br) - (tl + 2.0 * t + tr)) / 8.0;  // d/drow (down)

            // Normal = (-dh/dx, -dh/dup, 1) with up = -row, so the green term is +dh/drow.
            double nx = -gx * strength;
            double ny = gy * strength;
            double nz = 1.0;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;

            const size_t di = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            const double comps[3] = {nx, ny, nz};
            for (int k = 0; k < 3; ++k) {
                double v = (comps[k] * 0.5 + 0.5) * 255.0;
                dst[di + k] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
            dst[di + 3] = 255;
        }
    }
    return true;
}

}  // namespace iw8r
