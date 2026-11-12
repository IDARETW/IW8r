#include "setup_detect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace iw8r {

namespace fs = std::filesystem;

namespace {

std::string generic(const fs::path& p) { return p.generic_string(); }

std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Sorted names of the regular files in `dir` whose lower-case name passes `match`.
std::vector<std::string> filesWhere(const std::string& dir, bool (*match)(const std::string& lowerName)) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(dir), ec)) {
        std::error_code fe;
        if (!e.is_regular_file(fe)) continue;
        if (match(lowerAscii(e.path().filename().string()))) out.push_back(generic(e.path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool isGameExeName(const std::string& lower) {
    return startsWith(lower, kReplayExeStem) && endsWith(lower, ".exe");
}

bool isOodleName(const std::string& lower) {
    return startsWith(lower, "oo2core_") && endsWith(lower, "_win64.dll");
}

// ---------------------------------------------------------------- MD5 (RFC 1321)

class Md5 {
public:
    void update(const unsigned char* data, std::size_t n) {
        total_ += n;
        while (n > 0) {
            const std::size_t take = std::min<std::size_t>(n, 64 - buffered_);
            std::memcpy(buf_ + buffered_, data, take);
            buffered_ += take;
            data += take;
            n -= take;
            if (buffered_ == 64) {
                transform(buf_);
                buffered_ = 0;
            }
        }
    }

    std::string hexDigest() {
        const std::uint64_t bits = total_ * 8;
        const unsigned char one = 0x80, zero = 0;
        update(&one, 1);
        while (buffered_ != 56) update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (8 * i));
        update(len, 8);
        static const char* hex = "0123456789abcdef";
        std::string out;
        for (std::uint32_t word : {a_, b_, c_, d_})
            for (int i = 0; i < 4; ++i) {
                const unsigned byte = (word >> (8 * i)) & 0xFF;
                out += hex[byte >> 4];
                out += hex[byte & 0xF];
            }
        return out;
    }

private:
    static const std::uint32_t* sineTable() {
        // K[i] = floor(2^32 * |sin(i + 1)|), the table from RFC 1321; computed once.
        static const auto table = [] {
            std::uint32_t k[64];
            for (int i = 0; i < 64; ++i) k[i] = static_cast<std::uint32_t>(std::floor(std::fabs(std::sin(i + 1.0)) * 4294967296.0));
            return std::vector<std::uint32_t>(k, k + 64);
        }();
        return table.data();
    }
    static std::uint32_t rotl(std::uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }

    void transform(const unsigned char* p) {
        static const int shifts[4][4] = {{7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}};
        std::uint32_t m[16];
        for (int j = 0; j < 16; ++j)
            m[j] = std::uint32_t(p[4 * j]) | (std::uint32_t(p[4 * j + 1]) << 8) | (std::uint32_t(p[4 * j + 2]) << 16) |
                   (std::uint32_t(p[4 * j + 3]) << 24);
        std::uint32_t a = a_, b = b_, c = c_, d = d_;
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + sineTable()[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + rotl(f, shifts[i / 16][i % 4]);
        }
        a_ += a;
        b_ += b;
        c_ += c;
        d_ += d;
    }

    std::uint32_t a_ = 0x67452301, b_ = 0xefcdab89, c_ = 0x98badcfe, d_ = 0x10325476;
    unsigned char buf_[64] = {};
    std::size_t buffered_ = 0;
    std::uint64_t total_ = 0;
};

}  // namespace

const char* exeKindName(ExeKind kind) {
    switch (kind) {
        case ExeKind::Missing: return "missing";
        case ExeKind::NotPe: return "not a PE file";
        case ExeKind::Stock: return "stock";
        case ExeKind::Recompiled: return "recompiled";
    }
    return "missing";
}

ExeKind classifyExe(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(path), ec)) return ExeKind::Missing;
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) return ExeKind::Missing;
    char head[2] = {};
    f.read(head, 2);
    if (f.gcount() != 2 || head[0] != 'M' || head[1] != 'Z') return ExeKind::NotPe;
    f.clear();
    f.seekg(static_cast<std::streamoff>(kRecompiledMarkerOffset), std::ios::beg);
    char mark[8] = {};
    f.read(mark, 8);
    if (f.gcount() == 8 && std::memcmp(mark, kRecompiledMarker, 8) == 0) return ExeKind::Recompiled;
    return ExeKind::Stock;
}

std::string md5Hex(const std::string& bytes) {
    Md5 md;
    md.update(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    return md.hexDigest();
}

std::string md5File(const std::string& path) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) return {};
    Md5 md;
    std::vector<unsigned char> chunk(1 << 20);
    while (f) {
        f.read(reinterpret_cast<char*>(chunk.data()), std::streamsize(chunk.size()));
        const std::streamsize got = f.gcount();
        if (got > 0) md.update(chunk.data(), std::size_t(got));
    }
    if (f.bad()) return {};
    return md.hexDigest();
}

bool isExistingFile(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec);
}

bool isExistingDir(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec);
}

std::vector<std::string> windowsDriveRoots() {
    std::vector<std::string> out;
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        const std::string root = std::string(1, letter) + ":/";
        if (isExistingDir(root)) out.push_back(root);
    }
    return out;
}

std::vector<std::string> candidateGameDirs(const std::vector<std::string>& driveRoots) {
    std::vector<std::string> out;
    auto addBuilds = [&out](const fs::path& builds) {
        std::error_code ec;
        if (!fs::is_directory(builds, ec)) return;
        for (const auto& e : fs::directory_iterator(builds, ec)) {
            std::error_code de;
            if (!e.is_directory(de)) continue;
            if (!gameExesIn(generic(e.path())).empty()) out.push_back(generic(e.path()));
        }
    };
    for (const std::string& root : driveRoots) {
        const fs::path r(root);
        addBuilds(r / "IW8" / "Builds");  // <root>/IW8/Builds/<name>
        std::error_code ec;
        if (!fs::is_directory(r, ec)) continue;
        for (const auto& e : fs::directory_iterator(r, ec)) {  // <root>/<any>/IW8/Builds/<name>
            std::error_code de;
            if (e.is_directory(de)) addBuilds(e.path() / "IW8" / "Builds");
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> gameExesIn(const std::string& dir) {
    if (dir.empty()) return {};
    return filesWhere(dir, isGameExeName);
}

std::vector<std::string> oodleIn(const std::string& dir) {
    if (dir.empty()) return {};
    return filesWhere(dir, isOodleName);
}

}  // namespace iw8r
