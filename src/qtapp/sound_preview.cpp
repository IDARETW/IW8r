#include "sound_preview.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#include "worker_guard.h"

namespace iw8r::qt {

// ---------------------------------------------------------------------------------------------------------------
// FLAC frames (the FLAC format specification, RFC 9639): frame header, CONSTANT / VERBATIM / FIXED / LPC subframes with
// Rice-coded residuals, stereo decorrelation. 16-bit output.

namespace {

class BitReader {
public:
    BitReader(const uint8_t* d, size_t n) : d_(d), bytes_(n), bits_(n * 8) {}
    bool bad() const { return bad_; }
    size_t pos() const { return pos_; }
    size_t left() const { return bits_ > pos_ ? bits_ - pos_ : 0; }
    uint32_t read(int n) {
        if (n == 0) return 0;
        if (pos_ + size_t(n) > bits_) {
            bad_ = true;
            pos_ = bits_;
            return 0;
        }
        const size_t byte = pos_ >> 3;
        if (byte + 8 <= bytes_) {  // the usual case: one 64-bit load (offset 0-7 plus n <= 32 bits fit in it)
            const uint64_t v = load64(byte) << (pos_ & 7);
            pos_ += size_t(n);
            return uint32_t(v >> (64 - n));
        }
        const int off = int(pos_ & 7);
        const int need = (off + n + 7) >> 3;  // at most 5 bytes for n <= 32
        uint64_t v = 0;
        for (int i = 0; i < need; ++i) v = (v << 8) | d_[byte + size_t(i)];
        v >>= (need * 8 - off - n);
        pos_ += size_t(n);
        return uint32_t(v & ((uint64_t(1) << n) - 1));
    }
    int32_t readSigned(int n) {
        if (n == 0) return 0;
        const uint32_t u = read(n);
        if (n >= 32) return int32_t(u);
        return int32_t(u << (32 - n)) >> (32 - n);
    }
    // The zeros before the next one bit (consumed with it).
    uint32_t unary() {
        uint32_t n = 0;
        for (;;) {
            if (pos_ >= bits_) {
                bad_ = true;
                return n;
            }
            const size_t byte = pos_ >> 3;
            const int off = int(pos_ & 7);
            if (byte + 8 <= bytes_) {
                const uint64_t v = load64(byte) << off;  // the bits shifted in are below every real one
                if (v == 0) {
                    n += uint32_t(64 - off);
                    pos_ += size_t(64 - off);
                    continue;
                }
                const int lz = std::countl_zero(v);
                n += uint32_t(lz);
                pos_ += size_t(lz) + 1;
                return n;
            }
            const uint8_t b = uint8_t(d_[byte] << off);
            if (b == 0) {
                n += uint32_t(8 - off);
                pos_ += size_t(8 - off);
                continue;
            }
            const int lz = std::countl_zero(b);
            n += uint32_t(lz);
            pos_ += size_t(lz) + 1;
            return n;
        }
    }
    void align() { pos_ = (pos_ + 7) & ~size_t(7); }

private:
    uint64_t load64(size_t byte) const {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | d_[byte + size_t(i)];  // big endian (compiled to a load and a swap)
        return v;
    }
    const uint8_t* d_;
    size_t bytes_, bits_, pos_ = 0;
    bool bad_ = false;
};

bool residual(BitReader& br, int order, int block, int32_t* out) {
    const uint32_t method = br.read(2);
    if (method > 1) return false;
    const int paramBits = method == 0 ? 4 : 5;
    const uint32_t escape = method == 0 ? 15 : 31;
    const int p = int(br.read(4));
    const int parts = 1 << p;
    if ((block >> p) < order) return false;
    int idx = order;
    for (int part = 0; part < parts; ++part) {
        const int n = (block >> p) - (part == 0 ? order : 0);
        const uint32_t param = br.read(paramBits);
        if (param == escape) {
            const int bits = int(br.read(5));
            for (int i = 0; i < n; ++i) out[idx++] = br.readSigned(bits);
        } else {
            for (int i = 0; i < n; ++i) {
                const uint64_t q = br.unary();
                const uint64_t u = (q << param) | br.read(int(param));
                out[idx++] = int32_t(uint32_t(u >> 1)) ^ -int32_t(u & 1);
            }
        }
        if (br.bad()) return false;
    }
    return true;
}

bool subframe(BitReader& br, int bps, int block, int32_t* out) {
    if (br.read(1) != 0) return false;
    const uint32_t type = br.read(6);
    int wasted = 0;
    if (br.read(1)) wasted = int(br.unary()) + 1;
    bps -= wasted;
    if (bps <= 0 || bps > 32) return false;
    if (type == 0) {
        const int32_t v = br.readSigned(bps);
        std::fill(out, out + block, v);
    } else if (type == 1) {
        for (int i = 0; i < block; ++i) out[i] = br.readSigned(bps);
    } else if (type >= 8 && type <= 12) {
        const int order = int(type - 8);
        if (order > block) return false;
        for (int i = 0; i < order; ++i) out[i] = br.readSigned(bps);
        if (!residual(br, order, block, out)) return false;
        for (int i = order; i < block; ++i) {
            const int64_t a = i >= 1 ? out[i - 1] : 0, b = i >= 2 ? out[i - 2] : 0, c = i >= 3 ? out[i - 3] : 0, d = i >= 4 ? out[i - 4] : 0;
            switch (order) {
                case 1: out[i] = int32_t(out[i] + a); break;
                case 2: out[i] = int32_t(out[i] + 2 * a - b); break;
                case 3: out[i] = int32_t(out[i] + 3 * a - 3 * b + c); break;
                case 4: out[i] = int32_t(out[i] + 4 * a - 6 * b + 4 * c - d); break;
                default: break;
            }
        }
    } else if (type >= 32) {
        const int order = int(type & 31) + 1;
        if (order > block) return false;
        for (int i = 0; i < order; ++i) out[i] = br.readSigned(bps);
        const int precision = int(br.read(4)) + 1;
        if (precision == 16) return false;
        const int shift = br.readSigned(5);
        if (shift < 0) return false;
        int32_t coef[32];
        for (int i = 0; i < order; ++i) coef[i] = br.readSigned(precision);
        if (!residual(br, order, block, out)) return false;
        for (int i = order; i < block; ++i) {
            int64_t sum = 0;
            for (int j = 0; j < order; ++j) sum += int64_t(coef[j]) * out[i - 1 - j];
            out[i] = int32_t(out[i] + (sum >> shift));
        }
    } else {
        return false;
    }
    if (wasted)
        for (int i = 0; i < block; ++i) out[i] = int32_t(uint32_t(out[i]) << wasted);
    return !br.bad();
}

int16_t to16(int32_t s, int bps) {
    const int32_t v = bps > 16 ? s >> (bps - 16) : bps < 16 ? int32_t(uint32_t(s) << (16 - bps)) : s;
    return int16_t(std::clamp(v, -32768, 32767));
}

}  // namespace

bool decodeFlacFrames(const uint8_t* data, size_t size, PcmSound& out, QString* err, int rate, int64_t maxFrames) {
    out = PcmSound{};
    int streamBps = 16, streamRate = rate, streamChannels = 0;
    size_t start = 0;
    if (size >= 4 && std::memcmp(data, "fLaC", 4) == 0) {
        // A whole file: its metadata blocks first (STREAMINFO gives the defaults).
        size_t at = 4;
        for (bool last = false; !last && at + 4 <= size;) {
            last = data[at] & 0x80;
            const int type = data[at] & 0x7f;
            const size_t len = (size_t(data[at + 1]) << 16) | (size_t(data[at + 2]) << 8) | data[at + 3];
            if (type == 0 && len >= 18 && at + 4 + 18 <= size) {
                BitReader si(data + at + 4 + 10, 8);
                streamRate = int(si.read(20));
                streamChannels = int(si.read(3)) + 1;
                streamBps = int(si.read(5)) + 1;
            }
            at += 4 + len;
        }
        start = at;
    }
    BitReader br(data + start, size - start);
    if (maxFrames > 0) out.samples.reserve(size_t(maxFrames) * 2);  // stereo at most, as the banks hold
    std::vector<int32_t> ch[8];
    int64_t frames = 0;
    QString why;
    while (br.left() >= 16 * 8 && (maxFrames <= 0 || frames < maxFrames)) {
        const size_t frameAt = br.pos();
        if (br.read(14) != 0x3FFE) {
            why = QString("no frame sync at byte %1").arg(frameAt / 8);
            break;
        }
        br.read(1);
        br.read(1);  // blocking strategy (fixed or variable: the header says the size either way)
        const uint32_t bsCode = br.read(4), srCode = br.read(4), chCode = br.read(4), ssCode = br.read(3);
        br.read(1);
        // The frame / sample number, UTF-8 style.
        const uint32_t lead = br.read(8);
        int extra = 0;
        if (lead & 0x80) {
            if ((lead & 0xE0) == 0xC0) extra = 1;
            else if ((lead & 0xF0) == 0xE0) extra = 2;
            else if ((lead & 0xF8) == 0xF0) extra = 3;
            else if ((lead & 0xFC) == 0xF8) extra = 4;
            else if ((lead & 0xFE) == 0xFC) extra = 5;
            else if (lead == 0xFE) extra = 6;
            else {
                why = "bad frame number";
                break;
            }
        }
        for (int i = 0; i < extra; ++i) br.read(8);
        int block = 0;
        if (bsCode == 1) block = 192;
        else if (bsCode >= 2 && bsCode <= 5) block = 576 << (bsCode - 2);
        else if (bsCode == 6) block = int(br.read(8)) + 1;
        else if (bsCode == 7) block = int(br.read(16)) + 1;
        else if (bsCode >= 8) block = 256 << (bsCode - 8);
        static const int rates[12] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
        int frameRate = streamRate;
        if (srCode >= 1 && srCode <= 11) frameRate = rates[srCode];
        else if (srCode == 12) frameRate = int(br.read(8)) * 1000;
        else if (srCode == 13) frameRate = int(br.read(16));
        else if (srCode == 14) frameRate = int(br.read(16)) * 10;
        static const int sizes[8] = {0, 8, 12, 0, 16, 20, 24, 32};
        const int bps = ssCode == 0 ? streamBps : sizes[ssCode];
        br.read(8);  // CRC-8 of the header (not checked: the bank's own checksums cover the data)
        const int channels = chCode <= 7 ? int(chCode) + 1 : chCode <= 10 ? 2 : 0;
        if (block <= 0 || bps <= 0 || channels == 0 || br.bad()) {
            why = "bad frame header";
            break;
        }
        if (out.channels == 0) out.channels = channels, out.rate = frameRate;
        if (channels != out.channels) {
            why = "the channel count changes";
            break;
        }
        bool ok = true;
        for (int c = 0; c < channels && ok; ++c) {
            const bool side = (chCode == 8 && c == 1) || (chCode == 9 && c == 0) || (chCode == 10 && c == 1);
            ch[c].resize(size_t(block));
            ok = subframe(br, bps + (side ? 1 : 0), block, ch[c].data());
        }
        if (!ok) {
            why = QString("bad subframe at byte %1").arg(frameAt / 8);
            break;
        }
        br.align();
        br.read(16);  // CRC-16 of the frame
        for (int i = 0; i < block; ++i) {
            if (chCode == 8) ch[1][size_t(i)] = ch[0][size_t(i)] - ch[1][size_t(i)];  // left / side
            else if (chCode == 9) ch[0][size_t(i)] = ch[0][size_t(i)] + ch[1][size_t(i)];  // side / right
            else if (chCode == 10) {  // mid / side
                const int64_t side = ch[1][size_t(i)];
                const int64_t mid = (int64_t(ch[0][size_t(i)]) * 2) | (side & 1);
                ch[0][size_t(i)] = int32_t((mid + side) >> 1);
                ch[1][size_t(i)] = int32_t((mid - side) >> 1);
            }
        }
        const int64_t take = maxFrames > 0 ? std::min<int64_t>(block, maxFrames - frames) : block;
        const size_t at = out.samples.size();
        out.samples.resize(at + size_t(take) * size_t(channels));
        int16_t* dst = out.samples.data() + at;
        for (int64_t i = 0; i < take; ++i)
            for (int c = 0; c < channels; ++c) *dst++ = to16(ch[c][size_t(i)], bps);
        frames += take;
    }
    if (out.rate <= 0) out.rate = rate > 0 ? rate : 44100;
    if (streamChannels > 0 && out.channels == 0) out.channels = streamChannels;
    if (frames == 0) {
        if (err) *err = why.isEmpty() ? QString("no FLAC frames") : why;
        return false;
    }
    if (err) *err = why;  // where it stopped early, if it did
    return true;
}

QByteArray wavFile(const PcmSound& pcm) {
    const uint32_t dataBytes = uint32_t(pcm.samples.size() * 2);
    QByteArray w;
    w.resize(44 + qsizetype(dataBytes));
    char* p = w.data();
    const auto u32 = [&](int at, uint32_t v) { std::memcpy(p + at, &v, 4); };
    const auto u16 = [&](int at, uint16_t v) { std::memcpy(p + at, &v, 2); };
    std::memcpy(p, "RIFF", 4);
    u32(4, 36 + dataBytes);
    std::memcpy(p + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, uint16_t(pcm.channels));
    u32(24, uint32_t(pcm.rate));
    u32(28, uint32_t(pcm.rate * pcm.channels * 2));
    u16(32, uint16_t(pcm.channels * 2));
    u16(34, 16);
    std::memcpy(p + 36, "data", 4);
    u32(40, dataBytes);
    std::memcpy(p + 44, pcm.samples.data(), dataBytes);
    return w;
}

// ---------------------------------------------------------------------------------------------------------------
// SAB v10 banks

namespace {
constexpr uint32_t kSabMagic = 0x23585532;  // '2UX#'

template <class T>
T le(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

bool validSabHeader(const char* h, uint64_t available, uint32_t* count, uint64_t* fileSize, uint64_t* index) {
    if (le<uint32_t>(h) != kSabMagic || le<uint32_t>(h + 4) != 10 || le<uint32_t>(h + 8) != 44) return false;
    *count = le<uint32_t>(h + 20);
    *fileSize = le<uint64_t>(h + 32);
    *index = le<uint64_t>(h + 40);
    return *count > 0 && *count < 1000000 && *fileSize <= available && *index + uint64_t(*count) * 44 <= *fileSize;
}
}  // namespace

std::vector<SabEntry> readSabIndex(const QString& file, QString* err) {
    std::vector<SabEntry> out;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "cannot open " + file;
        return out;
    }
    const QByteArray h = f.read(48);
    uint32_t count = 0;
    uint64_t fileSize = 0, index = 0;
    if (h.size() < 48 || !validSabHeader(h.constData(), uint64_t(f.size()), &count, &fileSize, &index)) {
        if (err) *err = file + " is not a SAB v10 sound bank";
        return out;
    }
    if (!f.seek(qint64(index))) return out;
    const QByteArray t = f.read(qint64(count) * 44);
    if (t.size() != qint64(count) * 44) {
        if (err) *err = "short index in " + file;
        return out;
    }
    out.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const char* e = t.constData() + size_t(i) * 44;
        SabEntry& s = out[i];
        s.key = le<uint32_t>(e);
        s.size = le<uint32_t>(e + 4);
        s.seekBytes = le<uint32_t>(e + 8);
        s.frames = le<uint32_t>(e + 12);
        s.hybridPcmBytes = le<uint32_t>(e + 16);
        s.offset = le<uint64_t>(e + 20);
        s.rate = le<uint32_t>(e + 28);
        s.channels = uint8_t(e[32]);
        s.looping = uint8_t(e[33]);
        s.format = uint8_t(e[34]);
    }
    return out;
}

bool readSabSound(const QString& file, const SabEntry& e, PcmSound& out, QString* err) {
    if (e.format != 8) {
        if (err) *err = QString("codec %1 is not FLAC (not previewed)").arg(e.format);
        return false;
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(e.offset + e.seekBytes))) {
        if (err) *err = "cannot read " + file;
        return false;
    }
    const QByteArray data = f.read(qint64(e.size));
    if (data.size() != qint64(e.size)) {
        if (err) *err = "short sound data in " + file;
        return false;
    }
    QString why;
    if (!decodeFlacFrames(reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()), out, &why, int(e.rate), e.frames)) {
        if (err) *err = "cannot decode: " + why;
        return false;
    }
    if (e.rate) out.rate = int(e.rate);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// SoundPreview

struct SoundPreview::Zone {
    QString name;
    bool ready = false, failed = false;
    QString error;
    QHash<QString, QVector<uint32_t>> aliases;  // alias -> asset keys of its variants
    struct Bank {
        QString file;
        std::vector<SabEntry> entries;
        QHash<uint32_t, int> byKey;
    };
    std::vector<Bank> banks;  // search order: patch .sabs, .sabs, the loaded bank
};

namespace {

// Alias -> keys from ACTS's structured soundbank JSON (117 MB for common_mp): a scan for the three fields in order
// (each variant's own aliasName comes before its assetFileName and assetId) instead of a JSON tree of the whole file.
QHash<QString, QVector<uint32_t>> scanAliasJson(const QByteArray& json) {
    QHash<QString, QVector<uint32_t>> out;
    const std::string_view j(json.constData(), size_t(json.size()));
    QString alias;
    bool hasFile = false;
    const auto stringAfter = [&](size_t at) -> std::pair<std::string_view, size_t> {
        // {"address":...,"type":...,"string":"VALUE"} or null
        const size_t brace = j.find_first_of("{n", at);
        if (brace == std::string_view::npos || j[brace] == 'n') return {{}, at};
        const size_t s = j.find("\"string\":\"", brace);
        if (s == std::string_view::npos) return {{}, at};
        const size_t b = s + 10, e = j.find('"', b);
        if (e == std::string_view::npos) return {{}, at};
        return {j.substr(b, e - b), e};
    };
    size_t at = 0;
    for (;;) {
        at = j.find("\"a", at);
        if (at == std::string_view::npos) break;
        if (j.compare(at, 12, "\"aliasName\":") == 0) {
            const auto [v, end] = stringAfter(at + 12);
            alias = QString::fromUtf8(v.data(), qsizetype(v.size()));
            hasFile = false;
            at = end;
        } else if (j.compare(at, 16, "\"assetFileName\":") == 0) {
            const auto [v, end] = stringAfter(at + 16);
            hasFile = !v.empty();
            at = end;
        } else if (j.compare(at, 10, "\"assetId\":") == 0) {
            const uint64_t key = std::strtoull(j.data() + at + 10, nullptr, 10);
            if (!alias.isEmpty() && hasFile && key) out[alias].push_back(uint32_t(key));
            hasFile = false;
            at += 10;
        } else {
            at += 2;
        }
    }
    return out;
}

// The loaded bank inside a decompressed zone dump: the largest valid SAB v10 header in it, written to `outFile`.
bool extractLoadedBank(const QString& dump, const QString& outFile, QString* err) {
    QFile f(dump);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "cannot open " + dump;
        return false;
    }
    const qint64 size = f.size();
    const uchar* base = f.map(0, size);
    if (!base) {
        if (err) *err = "cannot map " + dump;
        return false;
    }
    const std::string_view all(reinterpret_cast<const char*>(base), size_t(size));
    size_t best = std::string_view::npos;
    uint64_t bestSize = 0;
    for (size_t at = all.find("2UX#"); at != std::string_view::npos; at = all.find("2UX#", at + 4)) {
        if (size_t(size) - at < 48) break;
        uint32_t count = 0;
        uint64_t fileSize = 0, index = 0;
        if (validSabHeader(all.data() + at, uint64_t(size) - at, &count, &fileSize, &index) && fileSize > bestSize) {
            best = at;
            bestSize = fileSize;
        }
    }
    bool ok = false;
    if (best != std::string_view::npos) {
        QFile o(outFile + ".part");
        if (o.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ok = o.write(all.data() + best, qint64(bestSize)) == qint64(bestSize);
            o.close();
        }
        ok = ok && (QFile::remove(outFile) || !QFileInfo::exists(outFile)) && QFile::rename(outFile + ".part", outFile);
        if (!ok) {
            QFile::remove(outFile + ".part");
            if (err) *err = "cannot write " + outFile;
        }
    } else if (err) {
        *err = "no sound bank in the zone dump";
    }
    f.unmap(const_cast<uchar*>(base));
    return ok;
}

QString findFile(const QString& root, const QString& name) {
    for (QDirIterator it(root, {name}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) return it.next();
    return {};
}

}  // namespace

SoundPreview::SoundPreview(ToolPath toolPath, QObject* parent) : QObject(parent), toolPath_(std::move(toolPath)) {
    endTimer_ = new QTimer(this);
    endTimer_->setSingleShot(true);
    connect(endTimer_, &QTimer::timeout, this, [this] {
        playing_ = false;
        setStatus(status_.startsWith("Playing") ? "Played " + status_.mid(8) : status_);
    });
}

SoundPreview::~SoundPreview() {
#ifdef _WIN32
    if (playing_) PlaySoundW(nullptr, nullptr, 0);
#endif
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(3000);
    }
}

QString SoundPreview::cacheDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/sound_preview";
}

void SoundPreview::setStatus(const QString& text) {
    status_ = text;
    emit statusChanged(text, playing_);
}

SoundPreview::Zone* SoundPreview::zone(const QString& name) {
    auto& z = zones_[name];
    if (!z) {
        z = std::make_shared<Zone>();
        z->name = name;
    }
    return z.get();
}

void SoundPreview::stop() {
#ifdef _WIN32
    PlaySoundW(nullptr, nullptr, 0);
#endif
    ++decodeGen_;
    endTimer_->stop();
    const bool was = playing_;
    playing_ = false;
    wantAlias_.clear();
    if (was) setStatus("Stopped");
}

void SoundPreview::play(const QString& alias, const QString& zoneName) {
    stop();
    wantAlias_ = alias;
    wantZone_ = zoneName;
    Zone* z = zone(zoneName);
    if (z->failed) return setStatus("No sound preview for " + zoneName + ": " + z->error);
    if (!z->ready) {
        if (!preparing_) prepare(*z);
        return;
    }
    playResolved(alias);
}

void SoundPreview::runActs(const QStringList& extra, const QString& outDir, bool structured, std::function<void(bool, const QString&)> done) {
    const QString acts = toolPath_("acts"), replay = toolPath_("replay"), oodle = toolPath_("oodle");
    if (!QFileInfo::exists(acts) || !QFileInfo::exists(replay)) {
        done(false, "ACTS or the Replay exe was not found (Tools > Tool Paths)");
        return;
    }
    QDir().mkpath(outDir);
    auto* p = proc_ = new QProcess(this);
    p->setProgram(acts);
    p->setArguments(QStringList{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle} + extra);
    p->setWorkingDirectory(outDir);
    p->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (structured) env.insert("ACTS_REPLAY_STRUCTURED", "1");
    p->setProcessEnvironment(env);
    auto* guard = new QTimer(p);  // a run that hangs is ended (the common_mp runs take 10 s)
    guard->setSingleShot(true);
    connect(guard, &QTimer::timeout, p, [p] { p->kill(); });
    guard->start(10 * 60 * 1000);
    connect(p, &QProcess::finished, this, [this, p, done](int code, QProcess::ExitStatus st) {
        const QString log = QString::fromLocal8Bit(p->readAll());
        if (proc_ == p) proc_ = nullptr;
        p->deleteLater();
        done(st == QProcess::NormalExit && code == 0, log);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        if (proc_ == p) proc_ = nullptr;
        p->deleteLater();
        done(false, "ACTS did not start");
    });
    p->start();
}

void SoundPreview::prepare(Zone& z) {
    preparing_ = true;
    const QString dir = cacheDir(), zoneName = z.name;
    QDir().mkpath(dir);
    const QString zoneDir = QFileInfo(toolPath_("replay")).absolutePath() + "/zone";
    const QString ff = zoneDir + "/" + zoneName + ".ff";
    const QString aliasFile = dir + "/" + zoneName + ".aliases.tsv", loaded = dir + "/" + zoneName + ".all.sabl";
    const QString tmp = dir + "/tmp_" + zoneName;
    QPointer<SoundPreview> self(this);
    const auto fail = [self, zoneName](const QString& why) {
        if (!self) return;
        self->preparing_ = false;
        Zone* zz = self->zone(zoneName);
        zz->failed = true;
        zz->error = why;
        self->setStatus("No sound preview for " + zoneName + ": " + why);
    };
    // The last step, on a worker thread: the alias index and the banks' indexes read into memory.
    const auto load = [self, zoneName, zoneDir, aliasFile, loaded, fail] {
        if (!self) return;
        self->setStatus("Reading the sound banks of " + zoneName + "...");
        std::thread(guardedWorker(__FILE__, __LINE__, [self, zoneName, zoneDir, aliasFile, loaded, fail] {
            auto z = std::make_shared<Zone>();
            z->name = zoneName;
            QFile f(aliasFile);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QStringList c = in.readLine().split('\t');
                    if (c.size() < 2) continue;
                    QVector<uint32_t> keys;
                    for (const QString& k : c[1].split(',', Qt::SkipEmptyParts)) keys << uint32_t(k.toULongLong(nullptr, 16));
                    z->aliases.insert(c[0], keys);
                }
            }
            for (const QString& b : {zoneDir + "/patch_" + zoneName + ".sabs", zoneDir + "/" + zoneName + ".sabs", loaded}) {
                if (!QFileInfo::exists(b)) continue;
                Zone::Bank bank;
                bank.file = b;
                bank.entries = readSabIndex(b);
                for (int i = 0; i < int(bank.entries.size()); ++i) bank.byKey.insert(bank.entries[size_t(i)].key, i);
                if (!bank.entries.empty()) z->banks.push_back(std::move(bank));
            }
            QMetaObject::invokeMethod(qApp, [self, z, fail] {
                if (!self) return;
                if (z->aliases.isEmpty() || z->banks.empty()) return fail("the alias index or the sound banks could not be read");
                z->ready = true;
                self->zones_[z->name] = z;
                self->preparing_ = false;
                self->setStatus(QString("Sound previews ready: %1 aliases in %2 bank(s)").arg(z->aliases.size()).arg(z->banks.size()));
                if (!self->wantAlias_.isEmpty() && self->wantZone_ == z->name) self->playResolved(self->wantAlias_);
            }, Qt::QueuedConnection);
        })).detach();
    };
    // Step 2: the loaded bank, copied out of the decompressed zone.
    const auto bankStep = [self, ff, tmp, loaded, zoneName, load, fail] {
        if (!self) return;
        if (QFileInfo::exists(loaded)) return load();
        self->setStatus("Preparing sound previews for " + zoneName + " (one time, about half a minute): copying the loaded sound bank out "
                        "of the zone (ACTS)...");
        QDir(tmp).removeRecursively();
        self->runActs({"-d", "--noAssetDump", "-o", tmp, ff}, tmp, false, [self, tmp, loaded, zoneName, load, fail](bool, const QString& log) {
            if (!self) return;
            const QString dump = findFile(tmp, zoneName + ".ff.dec");
            if (dump.isEmpty()) {
                QDir(tmp).removeRecursively();
                return fail("ACTS wrote no zone dump (" + log.right(200).simplified() + ")");
            }
            std::thread(guardedWorker(__FILE__, __LINE__, [self, dump, tmp, loaded, load, fail] {
                QString err;
                const bool ok = extractLoadedBank(dump, loaded, &err);
                QDir(tmp).removeRecursively();  // 1-2 GB of dump
                QMetaObject::invokeMethod(qApp, [ok, err, load, fail] { ok ? load() : fail(err); }, Qt::QueuedConnection);
            })).detach();
        });
    };
    // Step 1: alias -> keys from the structured soundbank export.
    if (QFileInfo::exists(aliasFile)) return bankStep();
    if (!QFileInfo::exists(ff)) return fail(ff + " was not found");
    setStatus("Preparing sound previews for " + zoneName + " (one time, about half a minute): reading the aliases (ACTS)...");
    QDir(tmp + "_aliases").removeRecursively();
    runActs({"-a", "soundbank", "-o", tmp + "_aliases", ff}, tmp + "_aliases", true, [self, tmp, aliasFile, zoneName, bankStep, fail](bool, const QString& log) {
        if (!self) return;
        const QString json = findFile(tmp + "_aliases", zoneName + ".all.*.asset.json");
        if (json.isEmpty()) {
            QDir(tmp + "_aliases").removeRecursively();
            return fail("ACTS wrote no soundbank export (" + log.right(200).simplified() + ")");
        }
        std::thread(guardedWorker(__FILE__, __LINE__, [self, json, tmp, aliasFile, bankStep, fail] {
            QHash<QString, QVector<uint32_t>> aliases;
            if (QFile f(json); f.open(QIODevice::ReadOnly)) aliases = scanAliasJson(f.readAll());
            bool ok = !aliases.isEmpty();
            if (ok) {
                QFile o(aliasFile + ".part");
                ok = o.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
                if (ok) {
                    QTextStream out(&o);
                    QStringList names = aliases.keys();
                    names.sort();
                    for (const QString& n : names) {
                        QStringList keys;
                        for (const uint32_t k : aliases.value(n)) keys << QString::number(k, 16);
                        out << n << '\t' << keys.join(',') << '\n';
                    }
                    out.flush();
                    o.close();
                    ok = (QFile::remove(aliasFile) || !QFileInfo::exists(aliasFile)) && QFile::rename(aliasFile + ".part", aliasFile);
                }
            }
            QDir(tmp + "_aliases").removeRecursively();
            QMetaObject::invokeMethod(qApp, [ok, bankStep, fail] { ok ? bankStep() : fail("the soundbank export had no aliases"); },
                                      Qt::QueuedConnection);
        })).detach();
    });
}

void SoundPreview::playResolved(const QString& alias) {
    const auto it = zones_.find(wantZone_);
    if (it == zones_.end() || !it.value()->ready) return;
    const std::shared_ptr<Zone> z = it.value();
    const QVector<uint32_t> keys = z->aliases.value(alias);
    if (keys.isEmpty()) return setStatus(alias + ": no sound data (the alias names no sound file, or it is not in " + z->name + ")");
    // The variants that have samples; one picked at random, as the game does.
    struct Hit {
        int bank;
        SabEntry e;
    };
    std::vector<Hit> hits;
    for (const uint32_t k : keys)
        for (int b = 0; b < int(z->banks.size()); ++b)
            if (const auto e = z->banks[size_t(b)].byKey.constFind(k); e != z->banks[size_t(b)].byKey.constEnd()) {
                hits.push_back({b, z->banks[size_t(b)].entries[size_t(*e)]});
                break;
            }
    if (hits.empty()) return setStatus(alias + ": its sound is not in the " + z->name + " banks");
    const int pick = int(QRandomGenerator::global()->bounded(int(hits.size())));
    const Hit h = hits[size_t(pick)];
    const QString file = z->banks[size_t(h.bank)].file;
    const QString what = QString("%1%2, %3 s%4")
                             .arg(alias)
                             .arg(hits.size() > 1 ? QString(" (variant %1 of %2)").arg(pick + 1).arg(hits.size()) : QString())
                             .arg(h.e.rate ? double(h.e.frames) / h.e.rate : 0.0, 0, 'f', h.e.rate && h.e.frames < h.e.rate ? 2 : 1)
                             .arg(h.e.looping ? ", loop" : "");
    setStatus("Decoding " + alias + "...");
    const int gen = ++decodeGen_;
    QPointer<SoundPreview> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, gen, file, h, what] {
        PcmSound pcm;
        QString err;
        const bool ok = readSabSound(file, h.e, pcm, &err);
        QMetaObject::invokeMethod(qApp, [self, gen, ok, pcm = std::move(pcm), err, what, loop = h.e.looping != 0]() mutable {
            if (!self || gen != self->decodeGen_) return;  // stopped or another sound asked for meanwhile
            if (!ok) return self->setStatus(what + ": " + err);
            self->startPlayback(std::move(pcm), what, loop);
        }, Qt::QueuedConnection);
    })).detach();
}

void SoundPreview::startPlayback(PcmSound pcm, const QString& what, bool loop) {
#ifdef _WIN32
    PlaySoundW(nullptr, nullptr, 0);  // the buffer below is replaced: nothing may play from it
#endif
    last_ = std::move(pcm);
    wav_ = wavFile(last_);
    bool started = muted_;
#ifdef _WIN32
    if (!muted_)
        started = PlaySoundW(reinterpret_cast<LPCWSTR>(wav_.constData()), nullptr,
                             SND_MEMORY | SND_ASYNC | SND_NODEFAULT | (loop ? SND_LOOP : 0)) != FALSE;
#endif
    if (!started) return setStatus(what + ": the sound device did not play it");
    playing_ = true;
    if (loop) endTimer_->stop();
    else endTimer_->start(int(std::min<int64_t>(last_.frames() * 1000 / std::max(1, last_.rate) + 150, 600000)));
    setStatus("Playing " + what);
}

// ---------------------------------------------------------------------------------------------------------------

int runSoundSelfTest(const QString& replayExe, const QString& referenceDir, const QString& outDir) {
    QStringList report;
    int failures = 0;
    // Reference pairs (<name>.flac decoded by libsndfile into <name>.wav): sample-exact.
    if (!referenceDir.isEmpty())
        for (const QString& flac : QDir(referenceDir).entryList({"*.flac"}, QDir::Files)) {
            QFile a(QDir(referenceDir).filePath(flac)), b(QDir(referenceDir).filePath(QFileInfo(flac).completeBaseName() + ".wav"));
            if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) continue;
            const QByteArray fl = a.readAll(), wav = b.readAll();
            PcmSound pcm;
            QString err;
            QElapsedTimer t;
            t.start();
            const bool ok = decodeFlacFrames(reinterpret_cast<const uint8_t*>(fl.constData()), size_t(fl.size()), pcm, &err);
            const qint64 ms = t.elapsed();
            const QByteArray mine = wavFile(pcm);
            // The data chunk of the reference (libsndfile may add chunks before it).
            const qsizetype d = wav.indexOf("data");
            const QByteArray ref = d >= 0 ? wav.mid(d + 8) : QByteArray();
            const bool same = ok && mine.mid(44) == ref.left(mine.size() - 44) && ref.size() == mine.size() - 44;
            failures += !same;
            report << QString("%1: %2 (%3 Hz, %4 ch, %5 frames, %6 ms)%7")
                          .arg(flac, same ? "identical to the reference" : "DIFFERS from the reference")
                          .arg(pcm.rate).arg(pcm.channels).arg(pcm.frames()).arg(ms)
                          .arg(err.isEmpty() ? QString() : "; " + err);
        }
    // Real data: the first sounds of the game's streamed bank, read in place.
    const QString sabs = QFileInfo(replayExe).absolutePath() + "/zone/common_mp.sabs";
    if (QFileInfo::exists(sabs)) {
        QString err;
        const std::vector<SabEntry> idx = readSabIndex(sabs, &err);
        report << QString("%1: %2 entries %3").arg(sabs).arg(idx.size()).arg(err);
        int checked = 0;
        QElapsedTimer t;
        t.start();
        for (size_t i = 0; i < idx.size() && checked < 40; i += std::max<size_t>(1, idx.size() / 40), ++checked) {
            PcmSound pcm;
            const bool ok = readSabSound(sabs, idx[i], pcm, &err);
            long loud = 0;
            for (const int16_t s : pcm.samples) loud += std::abs(int(s)) > 64;
            const bool good = ok && pcm.frames() == int64_t(idx[i].frames) && loud > 0;
            failures += !good;
            if (!good) report << QString("  entry %1 key %2: %3 (%4 of %5 frames, %6 loud samples)").arg(i).arg(idx[i].key, 8, 16, QChar('0'))
                                     .arg(ok ? "decoded" : err).arg(pcm.frames()).arg(idx[i].frames).arg(loud);
        }
        report << QString("  %1 streamed sounds decoded in %2 ms").arg(checked).arg(t.elapsed());
    } else {
        report << sabs + " not found: the streamed bank check is skipped";
    }
    // IW8R_SOUND_E2E=1: the panel's whole path, muted: the one-time preparation (ACTS) and three aliases.
    if (qEnvironmentVariableIntValue("IW8R_SOUND_E2E") > 0) {
        const QString base = QFileInfo(replayExe).absolutePath();
        SoundPreview sp([&](const QString& k) -> QString {
            if (k == "replay") return replayExe;
            if (k == "oodle") return base + "/oo2core_7_win64.dll";
            if (k == "acts") return qEnvironmentVariable("IW8R_TOOL_ACTS", "E:/mw124/atian-cod-tools/build/bin/Release/acts.exe");
            return {};
        });
        sp.setMuted(true);
        struct Want {
            const char* alias;
            int64_t frames;  // 0: no sound expected
        };
        for (const Want& w : {Want{"2pop", 1322}, Want{"emt_smokestack_lp", 992808}, Want{"beam", 0}}) {
            QElapsedTimer t;
            t.start();
            sp.play(w.alias);
            const auto busy = [&] {
                const QString st = sp.lastStatus();
                return sp.preparing() || st.startsWith("Decoding") || st.startsWith("Preparing") || st.startsWith("Reading") ||
                       st.startsWith("Sound previews ready");
            };
            while (busy() && t.elapsed() < 10 * 60 * 1000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const bool played = sp.lastStatus().startsWith("Playing");
            const bool good = w.frames ? played && sp.lastSound().frames() == w.frames : !played;
            failures += !good;
            report << QString("play %1: %2 (%3 ms, %4 frames)%5")
                          .arg(w.alias, sp.lastStatus())
                          .arg(t.elapsed())
                          .arg(played ? sp.lastSound().frames() : 0)
                          .arg(good ? QString() : QString(" FAILED"));
            sp.stop();
        }
    }
    report << (failures ? QString("FAILED: %1").arg(failures) : QString("ok"));
    const QString text = report.join('\n') + '\n';
    std::fprintf(stdout, "%s", qPrintable(text));
    std::fflush(stdout);
    QDir().mkpath(outDir);
    if (QFile f(QDir(outDir).filePath("sound-selftest.txt")); f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(text.toUtf8());
    return failures ? 1 : 0;
}

}  // namespace iw8r::qt
