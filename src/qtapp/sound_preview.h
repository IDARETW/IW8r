// IW8 Radiant (Qt shell) - plays a stock sound alias in the Sounds panel (owner 2026-11-15: "audio ... should be
// previewable in the side bar").
//
// How a Replay 1.20 alias reaches its samples (checked on the game's own files, 2026-11-15):
//   alias -> SndAlias variants (ACTS structured soundbank export: assetFileName, assetId) -> assetId = the key of an
//   entry in a SAB v10 sound bank ('2UX#', 44-byte index entries: key, size, seekBytes, frames, hybridPcmBytes,
//   offset, rate, channels, looping, format). Every entry of common_mp is format 8: FLAC frames without the fLaC header,
//   starting at offset + seekBytes.
// Banks, searched in this order (a "primed" sound has only its start in the loaded bank):
//   <game>/zone/patch_<zone>.sabs, <game>/zone/<zone>.sabs (streamed, read in place; the game folder is only read),
//   then the loaded bank <zone>.all, which lives inside <zone>.ff. ACTS cannot export it for common_mp (a 128 MiB cap;
//   the bank is 702 MB), so the decompressed zone is dumped once (acts fastfile -d --noAssetDump) and the bank copied
//   out of it into the cache.
// One-time preparation per zone (about half a minute for common_mp, shown in the panel), kept in
// <AppLocalData>/sound_preview/: <zone>.aliases.tsv (alias -> keys) and <zone>.all.sabl. A sound is then read, decoded
// (flac frames -> 16-bit PCM, decodeFlacFrames) and played from memory with the Windows sound API (no Qt Multimedia in
// the Qt used).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QProcess;
class QTimer;

namespace iw8r::qt {

struct PcmSound {
    int rate = 0, channels = 0;
    std::vector<int16_t> samples;  // interleaved
    int64_t frames() const { return channels > 0 ? int64_t(samples.size()) / channels : 0; }
};

// Raw FLAC frames (or a whole .flac file) to 16-bit PCM. `rate` / `channels` are used when a frame header leaves them
// to the stream info. maxFrames > 0 stops after that many samples per channel.
bool decodeFlacFrames(const uint8_t* data, size_t size, PcmSound& out, QString* err = nullptr, int rate = 0, int64_t maxFrames = 0);
// A RIFF WAVE file of the PCM (16-bit).
QByteArray wavFile(const PcmSound& pcm);

// One entry of a SAB v10 bank.
struct SabEntry {
    uint32_t key = 0, size = 0, seekBytes = 0, frames = 0, hybridPcmBytes = 0;
    uint64_t offset = 0;
    uint32_t rate = 0;
    uint8_t channels = 0, looping = 0, format = 0;
};
// The index of a bank file (empty with *err when it is not a SAB v10 bank). Any thread.
std::vector<SabEntry> readSabIndex(const QString& file, QString* err = nullptr);
// The sound of one entry, decoded. Any thread.
bool readSabSound(const QString& file, const SabEntry& e, PcmSound& out, QString* err = nullptr);

class SoundPreview : public QObject {
    Q_OBJECT
public:
    using ToolPath = std::function<QString(const QString& key)>;
    SoundPreview(ToolPath toolPath, QObject* parent = nullptr);
    ~SoundPreview() override;

    // Plays the alias (weighted by SndAlias.probability, or a custom sound file from sounds/); searches custom sounds
    // and candidate stock zones when `zone` is empty, preparing banks on first use as needed.
    void play(const QString& alias, const QString& zone = QString());
    void stop();
    bool playing() const { return playing_; }
    bool preparing() const { return preparing_; }
    QString cacheDir() const;
    QString lastStatus() const { return status_; }
    // Additional stock zones to search ahead of the defaults (e.g. the opened map's own zone).
    void setExtraZones(const QStringList& zones);
    // Folders searched for custom map sound files (<alias>.wav / .flac / .mp3).
    void setCustomSoundRoots(std::function<QStringList()> rootsFn) { customSoundRoots_ = std::move(rootsFn); }
    // The last sound decoded (tests): rate, channels and frames, 0 before any.
    const PcmSound& lastSound() const { return last_; }
    // Tests: decode only, nothing is played.
    void setMuted(bool m) { muted_ = m; }
    // The index of a zone prepared before (or all prepared candidate zones when `zone` is empty), read without ACTS
    // on a worker thread; indexReady when in memory. Nothing for a zone never prepared (its first Play prepares it).
    void loadIndex(const QString& zone = QString());
    // Every alias of a read zone (or merged across all read zones when `zone` is empty): how many variants have
    // samples, the shortest and longest, whether one loops.
    struct AliasFacts {
        int variants = 0;
        double shortest = 0, longest = 0;
        bool loop = false;
    };
    QHash<QString, AliasFacts> aliasFacts(const QString& zone = QString()) const;

signals:
    void statusChanged(const QString& text, bool playing);
    void indexReady(const QString& zone);
    // A sound started: its waveform (the peak of each of 480 slices, 0-1), its length, whether it loops.
    void waveReady(const QVector<float>& peaks, double seconds, bool loop);

private:
    struct Zone;
    Zone* zone(const QString& name);
    QStringList candidateZones() const;
    bool zoneHasAliasHit(const Zone& z, const QString& alias) const;
    QString findCustomSoundFile(const QString& alias) const;
    bool playCustomFile(const QString& alias, const QString& path);
    void advancePlayQueue();
    void advanceIndexQueue();
    void prepare(Zone& z);
    void runActs(const QStringList& args, const QString& outDir, bool structured, std::function<void(bool ok, const QString& log)> done);
    void playResolved(const QString& alias);
    void loadPrepared(const QString& zone, bool quiet);  // the cached index read (quiet: no status lines)
    void setStatus(const QString& text);
    void startPlayback(PcmSound pcm, const QString& what, bool loop);

    ToolPath toolPath_;
    std::function<QStringList()> customSoundRoots_;
    QStringList extraZones_;
    QHash<QString, std::shared_ptr<Zone>> zones_;
    QString wantAlias_, wantZone_;
    QStringList wantZones_, indexQueue_;
    QByteArray wav_;  // what the sound API plays from (kept until stopped)
    PcmSound last_;
    bool playing_ = false, preparing_ = false, muted_ = false, indexing_ = false, mciPlaying_ = false;
    QString status_;
    QTimer* endTimer_ = nullptr;
    QProcess* proc_ = nullptr;
    int decodeGen_ = 0;
};

// IW8R_FEATURE_SHOTS_ONLY=sound: decodes sounds of the game's streamed bank and checks them (frames, not silent);
// with <dir> holding reference .flac / .wav pairs, compares the decoder with them sample by sample. Returns 0 when ok.
int runSoundSelfTest(const QString& replayExe, const QString& referenceDir, const QString& outDir);

}  // namespace iw8r::qt
