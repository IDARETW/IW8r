// IW8 Radiant Suite - GUI thread stall watch (owner 2026-11-15: "the viewport rendering being on one thread so its
// never frozen, and other workloads on others").
//
// A heartbeat timer on the GUI thread and a watch thread: when the GUI thread has not come back to its event loop for
// longer than the threshold, the watch thread samples the GUI thread's call stack every 25 ms (suspend, unwind, resume;
// nothing is allocated while it is suspended). When the stall ends, the stall is written to the Console (warning, source
// "stall") with its length and the functions most often on top of the samples, and to the file named by
// IW8R_STALL_REPORT when set (one block per stall, for benchmarks). Symbols come from the exe's PDB through dbghelp,
// loaded on the first report.
//
// Off unless IW8R_STALL_WATCH is set (to the threshold in milliseconds, or 1 for 250 ms) or View > Report Freezes is
// on (Settings "debug/stallWatch").
#pragma once

#include <QString>

namespace iw8r::qt {

class StallWatch {
public:
    static StallWatch& instance();
    // Starts the heartbeat (call on the GUI thread, after the QApplication exists) and the watch thread.
    void start(int thresholdMs);
    void stop();
    bool running() const;
    // Stalls seen since start (tests and the bench).
    int stalls() const;
    // The last stall's report text ("" when none yet).
    QString lastReport() const;

private:
    StallWatch() = default;
    ~StallWatch();
    struct Impl;
    Impl* d_ = nullptr;
};

}  // namespace iw8r::qt
