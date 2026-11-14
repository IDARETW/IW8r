import io

# Converter (x64-zt worktree): reflection probes captured by the editor (IW8 Radiant research 123).
# A probe entry with "capture": "editor" in the world JSON is a render of the map itself, not an IW3 capture:
#   - it keeps its colour (the reduction to luminance answers IW3's clipped, over-saturated captures);
#   - it is stored luminance-normalised as stock probes are (stock mean luminance 0.82 .. 0.93, measured), because
#     the stock lit shader and the converter's own iw8-style path multiply the probe by the luminance of the
#     baked light. A built zone's probe measured 0.139 and no colour: reflections about six times too dark.
# The absolute-level table (MAP_PROBE_LEVELS) and the glass environment keep their old values.

ROOT = 'C:/zsw-iw8r/scriptables/'
p = 'src/convert/iw3_iw8/iw3_render_assets.cpp'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b, count=1):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:80])
    s = s.replace(a, b)


rep('''    // Stored linear value x absoluteScale = IW3's radiance (decoded rgb * a).
    float absoluteScale{1};
};''', '''    // Stored linear value x absoluteScale = IW3's radiance (decoded rgb * a).
    float absoluteScale{1};
    // The probe array stores the image times this (an editor capture: its mean luminance brought to the stock
    // probes' level; 1 for every other source).
    float arrayGain{1};
};''')

# The decoder takes the kind of capture.
rep('''Cubemap DecodeDdsCubemap(const std::filesystem::path &path)
{''', '''// The stock probes' mean luminance (IW8 Radiant research 123: mp_shipment 0.82, mp_hideout 0.87, mp_m_speedball
// 0.93 over all their probes): the level an editor capture is stored at.
constexpr float kStockProbeMeanLuminance = 0.9f;

// The gain that brings a cube's mean luminance (linear, weighted by solid angle) to the stock level. A black or
// nearly black capture keeps gain 1; the gain is held to 64.
float StockProbeGain(const Cubemap &cubemap)
{
    double sum = 0, weights = 0;
    for (const auto &face : cubemap.faces)
    {
        if (face.empty())
            continue;
        const Image &image = face.front();
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x)
            {
                const double u = 2.0 * (x + 0.5) / image.width - 1.0, v = 2.0 * (y + 0.5) / image.height - 1.0;
                const double weight = 1.0 / std::pow(1.0 + u * u + v * v, 1.5);
                const std::uint8_t *texel = image.rgba.data() + (std::size_t(y) * image.width + x) * 4;
                sum += weight * (0.2126 * Linear(texel[0] / 255.0f) + 0.7152 * Linear(texel[1] / 255.0f) +
                                 0.0722 * Linear(texel[2] / 255.0f));
                weights += weight;
            }
    }
    const double mean = weights > 0 ? sum / weights : 0;
    if (!(mean > 1.0e-3))
        return 1.0f;
    return static_cast<float>(std::min(double(kStockProbeMeanLuminance) / mean, 64.0));
}

Cubemap DecodeDdsCubemap(const std::filesystem::path &path, const bool editorCapture = false)
{''')
# The half-float path: an editor capture gets its gain too.
rep('''                result.residentPixels.insert(result.residentPixels.end(), result.faces[face][level].rgba.begin(),
                                             result.faces[face][level].rgba.end());
                result.residentPixels.resize((result.residentPixels.size() + 15) & ~std::size_t{15});
            }
        return result;
    }''', '''                result.residentPixels.insert(result.residentPixels.end(), result.faces[face][level].rgba.begin(),
                                             result.faces[face][level].rgba.end());
                result.residentPixels.resize((result.residentPixels.size() + 15) & ~std::size_t{15});
            }
        if (editorCapture)
            result.arrayGain = StockProbeGain(result);
        return result;
    }''')
# The 8-bit path: an editor capture keeps its colour and is not rescaled.
rep('''    float scale = 1;
    if (texels && intensitySum > 0 && intensityMax > 0)
        scale = static_cast<float>(std::min(colourSum / intensitySum, 1.0 / intensityMax));
    result.absoluteScale = 1.0f / scale;
    for (auto &face : result.faces)
        for (auto &image : face)
            for (std::size_t pixel = 0; pixel < image.rgba.size(); pixel += 4)
            {
                const float luminance = std::min(pixelLuminance(&image.rgba[pixel], true) * scale, 1.0f);
                const std::uint8_t grey = SrgbByte(luminance);
                image.rgba[pixel] = image.rgba[pixel + 1] = image.rgba[pixel + 2] = grey;
            }''', '''    float scale = 1;
    if (texels && intensitySum > 0 && intensityMax > 0)
        scale = static_cast<float>(std::min(colourSum / intensitySum, 1.0 / intensityMax));
    if (editorCapture)
    {
        // The editor's own render of the map (IW8 Radiant research 123): colour as it is, no IW3 intensity in the
        // alpha; stored at the stock probes' level.
        result.absoluteScale = 1.0f;
        result.arrayGain = StockProbeGain(result);
    }
    else
    {
        result.absoluteScale = 1.0f / scale;
        for (auto &face : result.faces)
            for (auto &image : face)
                for (std::size_t pixel = 0; pixel < image.rgba.size(); pixel += 4)
                {
                    const float luminance = std::min(pixelLuminance(&image.rgba[pixel], true) * scale, 1.0f);
                    const std::uint8_t grey = SrgbByte(luminance);
                    image.rgba[pixel] = image.rgba[pixel + 1] = image.rgba[pixel + 2] = grey;
                }
    }''')
# The array stores the image times the gain.
rep('''                        for (unsigned channel = 0; channel < sample.size(); ++channel)
                            image[pixel + channel] = sample[channel];
                        image[pixel + 3] = 1.0f;''', '''                        for (unsigned channel = 0; channel < sample.size(); ++channel)
                            image[pixel + channel] = sample[channel] * cubemap.arrayGain;
                        image[pixel + 3] = 1.0f;''')
# The plan: the capture kind, the level table, the SH of the stored image, the glass environment as before.
rep('''        Cubemap cubemap = DecodeDdsCubemap(sourcePath);
        probeLevels.push_back(cubemap.absoluteScale);
        probe.sh = ProjectReflectionSh(cubemap);''', '''        const bool editorCapture = source.value("capture", std::string()) == "editor";
        Cubemap cubemap = DecodeDdsCubemap(sourcePath, editorCapture);
        // Stored value x level = the capture's own radiance, whatever the array's gain.
        probeLevels.push_back(cubemap.absoluteScale / cubemap.arrayGain);
        probeGains.push_back(cubemap.arrayGain);
        probe.sh = ProjectReflectionSh(cubemap);
        // The zone's SH describes the stored image.
        for (auto &channel : probe.sh)
            for (float &coefficient : channel)
                coefficient *= cubemap.arrayGain;
        if (editorCapture)
            zt::info("iw3: reflection probe %zu is an editor capture: colour kept, stored at %.2fx (mean luminance %.3f)",
                     index, cubemap.arrayGain, kStockProbeMeanLuminance / cubemap.arrayGain);''')
rep('''    std::vector<float> probeLevels;
    probeCubemaps.reserve(sourceProbes.size());''', '''    std::vector<float> probeLevels;
    std::vector<float> probeGains;
    probeCubemaps.reserve(sourceProbes.size());''')
rep('''        std::array<std::array<double, 3>, 9> average{};
        for (const auto &probe : plan.reflectionProbes)
            for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
                for (unsigned channel = 0; channel < 3; ++channel)
                    average[coefficient][channel] +=
                        probe.sh[channel][coefficient] / plan.reflectionProbes.size();''', '''        std::array<std::array<double, 3>, 9> average{};
        for (std::size_t slot = 0; slot < plan.reflectionProbes.size(); ++slot)
        {
            // The glass environment keeps the capture's own level (the array gain taken out again).
            const auto &probe = plan.reflectionProbes[slot];
            const double gain = slot < probeGains.size() && probeGains[slot] > 0 ? probeGains[slot] : 1.0;
            for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
                for (unsigned channel = 0; channel < 3; ++channel)
                    average[coefficient][channel] +=
                        probe.sh[channel][coefficient] / gain / plan.reflectionProbes.size();
        }''')
# The encoded array's cache key holds the gain.
rep('''    for (const Cubemap &cubemap : probeCubemaps)
    {
        appendKey(cubemap.width);
        appendKey(cubemap.height);
        appendKey(cubemap.mipCount);''', '''    for (const Cubemap &cubemap : probeCubemaps)
    {
        appendKey(cubemap.width);
        appendKey(cubemap.height);
        appendKey(cubemap.mipCount);
        if (cubemap.arrayGain != 1.0f)
        {
            std::uint32_t gainBits;
            std::memcpy(&gainBits, &cubemap.arrayGain, sizeof(gainBits));
            appendKey(0x6761696eu); // "gain"
            appendKey(gainBits);
        }''')
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
