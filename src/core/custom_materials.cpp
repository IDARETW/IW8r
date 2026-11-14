// Mapper-side custom material library and image conversion (see custom_materials.h and doc 21).
#include "custom_materials.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "iw8_contents.h"
#include "json_lite.h"
#include "map_text.h"

namespace iw8r {

namespace fs = std::filesystem;

namespace detail {
bool stbDecodeRgba(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba, std::string* err);  // stb_image_impl.cpp
}

namespace {

constexpr const char* kDefinitionSuffix = ".iw8mat.json";
constexpr int kMaxOutputSide = 2048;      // editor DDS sides: powers of two up to this (doc 21 section 6)
constexpr int kMaxInputSide = 16384;

std::string lowerText(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWithCi(const std::string& text, const char* suffix) {
    const std::string s = lowerText(suffix);
    return text.size() >= s.size() && lowerText(text.substr(text.size() - s.size())) == s;
}

void addError(std::string* err, const std::string& message) {
    if (!err) return;
    if (!err->empty()) *err += "\n";
    *err += message;
}

// Surface type from its IW3 name (iw8_contents.h table); "" and "default" are 0.
bool surfaceTypeFromName(const std::string& text, int& type) {
    const std::string t = lowerText(text);
    if (t.empty() || t == "default") {
        type = 0;
        return true;
    }
    for (int i = 1; i <= 28; ++i)
        if (t == iw3SurfaceTypeName(i)) {
            type = i;
            return true;
        }
    return false;
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

bool readBinary(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

uint32_t readU32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | (uint32_t(b[at + 1]) << 8) | (uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24);
}

void putU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

// Nearest power of two of n (ties round up), capped at kMaxOutputSide.
int powerOfTwoSide(int n) {
    int p = 1;
    while (p * 2 <= n) p *= 2;
    const int q = p * 2;
    const int chosen = (n - p) < (q - n) ? p : q;
    return std::min(chosen, kMaxOutputSide);
}

// Bilinear resample of an RGBA8 image (centre-aligned). Colour and alpha are interpolated unpremultiplied.
std::vector<uint8_t> resampleBilinear(const std::vector<uint8_t>& src, int w, int h, int tw, int th) {
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th * 4);
    if (w == tw && h == th) return src;
    for (int y = 0; y < th; ++y) {
        const double fy = std::clamp((y + 0.5) * h / th - 0.5, 0.0, double(h - 1));
        const int y0 = int(std::floor(fy)), y1 = std::min(y0 + 1, h - 1);
        const double ty = fy - y0;
        for (int x = 0; x < tw; ++x) {
            const double fx = std::clamp((x + 0.5) * w / tw - 0.5, 0.0, double(w - 1));
            const int x0 = int(std::floor(fx)), x1 = std::min(x0 + 1, w - 1);
            const double tx = fx - x0;
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](int xx, int yy) { return double(src[(size_t(yy) * w + xx) * 4 + c]); };
                const double top = at(x0, y0) * (1 - tx) + at(x1, y0) * tx;
                const double bottom = at(x0, y1) * (1 - tx) + at(x1, y1) * tx;
                const double v = top * (1 - ty) + bottom * ty;
                out[(size_t(y) * tw + x) * 4 + c] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
        }
    }
    return out;
}

// One 2x2 box-filter step (each destination texel averages up to four source texels).
std::vector<uint8_t> halve(const std::vector<uint8_t>& src, int w, int h, int nw, int nh) {
    std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4);
    for (int y = 0; y < nh; ++y) {
        const int ya = std::min(2 * y, h - 1), yb = std::min(2 * y + 1, h - 1);
        for (int x = 0; x < nw; ++x) {
            const int xa = std::min(2 * x, w - 1), xb = std::min(2 * x + 1, w - 1);
            const size_t idx[4] = {(size_t(ya) * w + xa) * 4, (size_t(ya) * w + xb) * 4, (size_t(yb) * w + xa) * 4,
                                   (size_t(yb) * w + xb) * 4};
            for (int c = 0; c < 4; ++c) {
                const int sum = src[idx[0] + c] + src[idx[1] + c] + src[idx[2] + c] + src[idx[3] + c];
                out[(size_t(y) * nw + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

// Reads the top level of an uncompressed DDS (32, 24 or 16 bits per pixel, RGB masks).
bool decodeDds(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba, std::string* err) {
    std::vector<uint8_t> b;
    if (!readBinary(path, b)) {
        addError(err, "cannot read '" + path + "'");
        return false;
    }
    const auto fail = [&](const std::string& why) {
        addError(err, "DDS '" + path + "': " + why);
        return false;
    };
    if (b.size() < 128 || std::memcmp(b.data(), "DDS ", 4) != 0 || readU32(b, 4) != 124) return fail("not a DDS file");
    const uint32_t height = readU32(b, 12), width = readU32(b, 16);
    const uint32_t pfFlags = readU32(b, 80), fourCC = readU32(b, 84), bits = readU32(b, 88);
    const uint32_t masks[4] = {readU32(b, 92), readU32(b, 96), readU32(b, 100), readU32(b, 104)};
    if (fourCC != 0)
        return fail("compressed DDS (" + std::string(reinterpret_cast<const char*>(b.data() + 84), 4) +
                    ") is not read by the editor; export PNG or TGA");
    if (!(pfFlags & 0x40u)) return fail("only RGB uncompressed DDS is read");
    if (bits != 16 && bits != 24 && bits != 32) return fail("unsupported bit depth");
    if (!width || !height || width > kMaxInputSide || height > kMaxInputSide) return fail("unsupported size");
    const size_t stride = bits / 8;
    const size_t pixels = size_t(width) * height;
    if (b.size() < 128 || pixels * stride > b.size() - 128) return fail("truncated pixel data");
    w = int(width);
    h = int(height);
    rgba.assign(pixels * 4, 255);
    for (size_t i = 0; i < pixels; ++i) {
        uint32_t value = 0;
        for (size_t k = 0; k < stride; ++k) value |= uint32_t(b[128 + i * stride + k]) << (8 * k);
        for (int c = 0; c < 4; ++c) {
            const uint32_t m = masks[c];
            if (!m) {
                rgba[i * 4 + c] = c == 3 ? 255 : 0;
                continue;
            }
            int shift = 0;
            while (!((m >> shift) & 1u)) ++shift;
            const uint32_t field = (value & m) >> shift;
            const uint32_t maxValue = m >> shift;
            rgba[i * 4 + c] = static_cast<uint8_t>(maxValue == 255 ? field : (field * 255 + maxValue / 2) / maxValue);
        }
    }
    return true;
}

// Parses one <name>.iw8mat.json. Image paths are resolved against the definition's folder and must exist.
bool parseDefinition(const fs::path& file, const fs::path& root, CustomMaterial& out, std::string& err) {
    std::string text;
    if (!readFile(file.string(), text)) {
        err = "cannot read '" + file.string() + "'";
        return false;
    }
    std::string perr;
    const auto doc = json::parse(text, &perr);
    if (!doc || doc->type != json::Value::Type::Object) {
        err = "'" + file.string() + "': not a JSON object" + (perr.empty() ? "" : " (" + perr + ")");
        return false;
    }
    const auto fail = [&](const std::string& why) {
        err = "'" + file.string() + "': " + why;
        return false;
    };
    const fs::path base = file.parent_path();

    std::string name;
    if (const json::Value* v = doc->get("name")) {
        if (!v->string()) return fail("'name' must be a string");
        name = *v->string();
    } else {
        // Default: the path under the library root, without the suffix.
        name = fs::relative(file, root).generic_string();
        name = name.substr(0, name.size() - std::strlen(kDefinitionSuffix));
    }
    if (!isValidCustomMaterialName(name)) return fail("invalid material name '" + name + "'");

    const auto image = [&](const char* key, bool required, std::string& target) -> bool {
        const json::Value* v = doc->get(key);
        if (!v) return !required || fail(std::string("missing '") + key + "'");
        if (!v->string() || v->string()->empty()) return fail(std::string("'") + key + "' must be a file name");
        fs::path p = fs::path(*v->string());
        if (!p.is_absolute()) p = base / p;
        p = p.lexically_normal();
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return fail("image '" + *v->string() + "' not found");
        target = p.string();
        return true;
    };
    CustomMaterial m;
    m.name = name;
    if (const json::Value* v = doc->get("buildName")) {
        if (!v->string() || !isValidCustomMaterialName(*v->string())) return fail("invalid 'buildName'");
        m.buildName = *v->string();
    }
    if (!image("color", true, m.colorImage)) return false;
    if (!image("normal", false, m.normalImage)) return false;
    if (!image("specular", false, m.specImage)) return false;

    if (const json::Value* v = doc->get("surface")) {
        if (!v->string()) return fail("'surface' must be a string");
        if (!surfaceTypeFromName(*v->string(), m.surfaceType))
            return fail("unknown surface type '" + *v->string() + "' (see iw8_contents.h, e.g. brick, concrete, wood)");
    }
    for (const char* key : {"alphaTest", "twoSided", "blend"}) {
        const json::Value* v = doc->get(key);
        if (!v) continue;
        if (v->type != json::Value::Type::Bool) return fail(std::string("'") + key + "' must be true or false");
        const std::string k = key;
        (k == "alphaTest" ? m.alphaTest : k == "twoSided" ? m.twoSided : m.blend) = v->b;
    }
    if (const json::Value* v = doc->get("blendMode")) {
        if (!v->string()) return fail("'blendMode' must be \"alpha\", \"add\" or \"multiply\"");
        const std::string& b = *v->string();
        if (b == "alpha") m.blend = true;
        else if (b == "add") m.blendMode = 1;
        else if (b == "multiply") m.blendMode = 2;
        else return fail("'blendMode' must be \"alpha\", \"add\" or \"multiply\"");
    }
    if (const json::Value* v = doc->get("unlit")) {
        if (v->type != json::Value::Type::Bool) return fail("'unlit' must be true or false");
        m.unlit = v->b;
    }
    if (const json::Value* v = doc->get("shader")) {
        if (!v->string() || v->string()->empty()) return fail("'shader' must be a file name (*.iw8shader.hlsl)");
        fs::path p = fs::path(*v->string());
        if (!p.is_absolute()) p = base / p;
        auto shader = std::make_shared<CustomShader>();
        std::string serr;
        if (!readCustomShader(p.lexically_normal().string(), *shader, &serr)) return fail("shader: " + serr);
        m.shader = std::move(shader);
    }
    if (const json::Value* v = doc->get("emission")) {
        if (!v->number() || *v->number() < 0 || *v->number() > 64) return fail("'emission' must be a number from 0 to 64");
        m.emission = *v->number();
    }
    if (const json::Value* v = doc->get("tint")) {
        if (v->type != json::Value::Type::Array || v->arr.size() != 3) return fail("'tint' must be [r, g, b]");
        for (size_t i = 0; i < 3; ++i) {
            const auto c = v->at(i);
            if (!c || !c->number() || *c->number() < 0 || *c->number() > 4) return fail("'tint' values must be from 0 to 4");
            m.tint[i] = *c->number();
        }
    }
    if (const json::Value* v = doc->get("sortKey")) {
        if (!v->number() || *v->number() < 0 || *v->number() > 40) return fail("'sortKey' must be a whole number from 0 to 40");
        m.sortKey = int(*v->number());
    }
    if (const json::Value* v = doc->get("polygonOffset")) {
        if (!v->number() || *v->number() < 0 || *v->number() > 2) return fail("'polygonOffset' must be 0, 1 or 2");
        m.polygonOffset = int(*v->number());
    }
    if (const json::Value* v = doc->get("scale")) {
        const auto a = v->at(0), b2 = v->at(1);
        if (v->type != json::Value::Type::Array || v->arr.size() != 2 || !a || !b2 || !a->number() || !b2->number())
            return fail("'scale' must be [width, height]");
        m.width = *a->number();
        m.height = *b2->number();
        if (!(m.width > 0 && m.height > 0 && m.width <= 65536 && m.height <= 65536))
            return fail("'scale' must be positive world units");
    }
    m.defFile = file.lexically_normal().string();
    out = std::move(m);
    return true;
}

}  // namespace

std::string customNameHash(const std::string& text) {
    uint32_t h = 2166136261u;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 16777619u;
    }
    char buf[12];
    std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(h));
    return buf;
}

bool isValidCustomMaterialName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.back() == '/' || name.find("..") != std::string::npos ||
        name.find("//") != std::string::npos || lowerText(name).starts_with("mc/"))
        return false;
    for (const char c : name) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool MaterialLibrary::load(const std::vector<std::string>& roots, std::string* err) {
    byKey_.clear();
    bool ok = true;
    for (const std::string& root : roots) {
        const fs::path base(root);
        std::error_code ec;
        if (!fs::is_directory(base, ec)) {
            addError(err, "material library folder not found: " + root);
            ok = false;
            continue;
        }
        std::vector<fs::path> files;
        fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (it->is_regular_file(fileEc) && endsWithCi(it->path().filename().string(), kDefinitionSuffix))
                files.push_back(it->path());
        }
        if (ec) {
            addError(err, "cannot scan material library " + root + ": " + ec.message());
            ok = false;
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            CustomMaterial m;
            std::string why;
            if (!parseDefinition(file, base, m, why)) {
                addError(err, why);
                ok = false;
                continue;
            }
            const std::string key = lowerText(m.name);
            if (byKey_.count(key)) {
                addError(err, "material '" + m.name + "' is defined twice (second: " + file.string() + ")");
                ok = false;
                continue;
            }
            byKey_.emplace(key, std::move(m));
        }
    }
    return ok;
}

const CustomMaterial* MaterialLibrary::find(const std::string& name) const {
    const auto it = byKey_.find(lowerText(name));
    return it == byKey_.end() ? nullptr : &it->second;
}

std::vector<std::string> MaterialLibrary::names() const {
    std::vector<std::string> out;
    for (const auto& [key, m] : byKey_) out.push_back(m.name);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lowerText(a) < lowerText(b); });
    return out;
}

bool MaterialLibrary::createFromImage(const std::string& imagePath, const std::string& libraryRoot, const std::string& name,
                                      std::string* err) {
    if (!isValidCustomMaterialName(name)) {
        addError(err, "invalid material name '" + name + "'");
        return false;
    }
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    if (!decodeImageFile(imagePath, w, h, pixels, err)) return false;

    const fs::path json = fs::path(libraryRoot) / (name + kDefinitionSuffix);
    std::error_code ec;
    if (fs::exists(json, ec)) {
        addError(err, "material definition already exists: " + json.string());
        return false;
    }
    fs::create_directories(json.parent_path(), ec);
    if (ec) {
        addError(err, "cannot create " + json.parent_path().string() + ": " + ec.message());
        return false;
    }
    // Image path relative to the definition file (it is not copied).
    const fs::path image = fs::absolute(imagePath, ec);
    fs::path rel = fs::relative(image, fs::absolute(json.parent_path(), ec), ec);
    const std::string colour = (ec || rel.empty()) ? image.string() : rel.generic_string();

    const char* surfaceName = iw3SurfaceTypeName(iw3SurfaceTypeForMaterial(name));
    const std::string text = "{\"name\":\"" + jsonEscape(name) + "\",\"color\":\"" + jsonEscape(colour) +
                             "\",\"surface\":\"" + jsonEscape(surfaceName) +
                             "\",\"alphaTest\":false,\"twoSided\":false,\"scale\":[128,128]}\n";
    std::ofstream f(json, std::ios::binary);
    if (!f || !(f << text)) {
        addError(err, "cannot write " + json.string());
        return false;
    }
    return true;
}

bool decodeImageFile(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba, std::string* err) {
    std::string ext = lowerText(fs::path(path).extension().string());
    bool ok = ext == ".dds" ? decodeDds(path, w, h, rgba, err) : detail::stbDecodeRgba(path, w, h, rgba, err);
    if (!ok) return false;
    if (w <= 0 || h <= 0 || w > kMaxInputSide || h > kMaxInputSide || rgba.size() != size_t(w) * h * 4) {
        addError(err, "unsupported image size in '" + path + "'");
        return false;
    }
    return true;
}

bool encodeDdsFromImage(const std::string& imagePath, bool normalMap, bool keepAlpha, std::vector<uint8_t>& dds,
                        std::string* err) {
    int w = 0, h = 0;
    std::vector<uint8_t> src;
    if (!decodeImageFile(imagePath, w, h, src, err)) return false;

    const int tw = powerOfTwoSide(w), th = powerOfTwoSide(h);
    std::vector<uint8_t> level = resampleBilinear(src, w, h, tw, th);
    const bool opaque = normalMap || !keepAlpha;
    if (opaque)
        for (size_t i = 3; i < level.size(); i += 4) level[i] = 255;

    // Mip chain down to 1x1: floor(log2(max side)) + 1 levels.
    std::vector<std::vector<uint8_t>> chain{level};
    std::vector<std::pair<int, int>> sizes{{tw, th}};
    int cw = tw, ch = th;
    while (cw > 1 || ch > 1) {
        const int nw = std::max(1, cw / 2), nh = std::max(1, ch / 2);
        chain.push_back(halve(chain.back(), cw, ch, nw, nh));
        sizes.push_back({nw, nh});
        cw = nw;
        ch = nh;
    }

    size_t total = 128;
    for (const auto& [lw, lh] : sizes) total += size_t(lw) * lh * 4;
    dds.assign(total, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    putU32(dds, 4, 124);
    putU32(dds, 8, 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u | 0x20000u);  // caps, height, width, pitch, pixelformat, mips
    putU32(dds, 12, uint32_t(th));
    putU32(dds, 16, uint32_t(tw));
    putU32(dds, 20, uint32_t(tw) * 4);                                 // pitch of the top level
    putU32(dds, 28, uint32_t(sizes.size()));
    putU32(dds, 76, 32);                                               // pixel format size
    putU32(dds, 80, 0x1u | 0x40u);                                     // alpha pixels, RGB
    putU32(dds, 88, 32);
    putU32(dds, 92, 0x00FF0000u);                                      // R
    putU32(dds, 96, 0x0000FF00u);                                      // G
    putU32(dds, 100, 0x000000FFu);                                     // B
    putU32(dds, 104, 0xFF000000u);                                     // A
    putU32(dds, 108, 0x1000u | 0x400000u | 0x8u);                      // texture, mipmap, complex

    size_t at = 128;
    for (const auto& data : chain) {
        // Pixel bytes are B, G, R, A in memory (little-endian 0xAARRGGBB, as the masks above say).
        for (size_t i = 0; i < data.size(); i += 4) {
            dds[at++] = data[i + 2];
            dds[at++] = data[i + 1];
            dds[at++] = data[i];
            dds[at++] = data[i + 3];
        }
    }
    return true;
}

bool writeDdsFromImage(const std::string& imagePath, const std::string& ddsPath, bool normalMap, std::string* err,
                       bool keepAlpha) {
    std::vector<uint8_t> dds;
    if (!encodeDdsFromImage(imagePath, normalMap, keepAlpha, dds, err)) return false;
    std::error_code ec;
    fs::create_directories(fs::path(ddsPath).parent_path(), ec);
    std::ofstream f(ddsPath, std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()))) {
        addError(err, "cannot write '" + ddsPath + "'");
        return false;
    }
    return true;
}

}  // namespace iw8r
