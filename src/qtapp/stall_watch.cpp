#include "stall_watch.h"

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <QCoreApplication>
#include <QFile>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "editor_log.h"

namespace iw8r::qt {

namespace {
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The GUI thread's call stack while it is suspended: RtlVirtualUnwind over the unwind tables, into a fixed array (no
// allocation: the suspended thread may hold the heap lock).
int sampleStack(HANDLE thread, DWORD64* out, int max) {
    if (SuspendThread(thread) == DWORD(-1)) return 0;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_FULL;
    int n = 0;
    if (GetThreadContext(thread, &c)) {
        while (n < max && c.Rip) {
            out[n++] = c.Rip;
            DWORD64 base = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
            if (!fn) {  // a leaf function: the return address is on top of the stack
                c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
                c.Rsp += 8;
                continue;
            }
            void* handlerData = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &handlerData, &frame, nullptr);
        }
    }
    ResumeThread(thread);
    return n;
}
}  // namespace

struct StallWatch::Impl {
    std::atomic<long long> beat{0};
    std::atomic<bool> quit{false};
    std::atomic<int> stalls{0};
    int threshold = 250;
    HANDLE gui = nullptr;
    std::thread watcher;
    QTimer* heart = nullptr;
    mutable std::mutex m;
    QString last;
    bool symbols = false;
    QString reportFile;

    QString name(DWORD64 a) {
        HANDLE p = GetCurrentProcess();
        if (!symbols) {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            SymInitialize(p, nullptr, TRUE);
            symbols = true;
        }
        // The wide API explicitly (with UNICODE defined the narrow names were read as one character).
        alignas(SYMBOL_INFOW) char buf[sizeof(SYMBOL_INFOW) + 512 * sizeof(WCHAR)];
        auto* s = reinterpret_cast<SYMBOL_INFOW*>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFOW);
        s->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (SymFromAddrW(p, a, &disp, s)) {
            QString r = QString::fromWCharArray(s->Name, int(s->NameLen));
            IMAGEHLP_LINEW64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD ld = 0;
            if (SymGetLineFromAddrW64(p, a, &ld, &line) && line.FileName)
                r += QString(" (%1:%2)").arg(QString::fromWCharArray(line.FileName).section('\\', -1)).arg(line.LineNumber);
            return r;
        }
        // No symbols (a PDB that does not match): the offset in the exe, for llvm-symbolizer with the right PDB.
        const DWORD64 base = DWORD64(GetModuleHandleW(nullptr));
        return QString("exe+0x%1").arg(a - base, 0, 16);
    }

    void run() {
        constexpr int kMax = 64;
        std::vector<std::array<DWORD64, kMax>> samples;
        std::vector<int> depth;
        samples.reserve(400);
        depth.reserve(400);
        long long stallFrom = 0;
        while (!quit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const long long t = nowMs(), b = beat.load();
            if (b == 0) continue;
            if (t - b > threshold) {
                if (!stallFrom) stallFrom = b;
                if (samples.size() < 400) {  // 10 s of samples at most per stall
                    samples.emplace_back();
                    depth.push_back(sampleStack(gui, samples.back().data(), kMax));
                }
            } else if (stallFrom) {
                report(b - stallFrom, samples, depth);
                samples.clear();
                depth.clear();
                stallFrom = 0;
            }
        }
    }

    void report(long long ms, const std::vector<std::array<DWORD64, 64>>& samples, const std::vector<int>& depth) {
        ++stalls;
        // The editor's own frames count (Qt and system frames are skipped when naming the stall): per function, the
        // samples it was on the stack in, and the innermost own function of each sample.
        std::map<QString, int> inner, onStack;
        const HMODULE self = GetModuleHandleW(nullptr);
        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof(mi));
        const DWORD64 lo = DWORD64(mi.lpBaseOfDll), hi = lo + mi.SizeOfImage;
        std::map<DWORD64, QString> names;
        const auto nameOf = [&](DWORD64 a) {
            auto it = names.find(a);
            if (it == names.end()) it = names.emplace(a, name(a).section(" (", 0, 0)).first;
            return it->second;
        };
        for (size_t i = 0; i < samples.size(); ++i) {
            bool first = true;
            std::vector<QString> seen;
            for (int k = 0; k < depth[i]; ++k) {
                const DWORD64 a = samples[i][size_t(k)];
                if (a < lo || a >= hi) continue;
                const QString n = nameOf(a);
                if (n.startsWith("std::") || n.startsWith("Concurrency::") || n.startsWith("`")) continue;
                if (first) ++inner[n], first = false;
                if (std::find(seen.begin(), seen.end(), n) == seen.end()) seen.push_back(n), ++onStack[n];
            }
        }
        std::vector<std::pair<int, QString>> top, tops;
        for (const auto& [n, c] : inner) top.push_back({c, n});
        for (const auto& [n, c] : onStack) tops.push_back({c, n});
        std::sort(top.rbegin(), top.rend());
        std::sort(tops.rbegin(), tops.rend());
        QString text = QString("GUI thread did not answer for %1 ms (%2 samples). Busy in:").arg(ms).arg(samples.size());
        for (size_t i = 0; i < top.size() && i < 4; ++i) text += QString(" %1 (%2%)").arg(top[i].second).arg(100 * top[i].first / std::max<size_t>(1, samples.size()));
        QString detail = text + "\n  on the stack:";
        for (size_t i = 0; i < tops.size() && i < 12; ++i)
            detail += QString("\n    %1%  %2").arg(100 * tops[i].first / std::max<size_t>(1, samples.size()), 3).arg(tops[i].second);
        {
            std::lock_guard lock(m);
            last = detail;
        }
        if (!reportFile.isEmpty()) {
            QFile f(reportFile);
            if (f.open(QIODevice::Append | QIODevice::Text)) f.write((detail + "\n").toUtf8());
        }
        QMetaObject::invokeMethod(qApp, [text] { EditorLog::instance().add(LogLevel::Warning, "stall", text); }, Qt::QueuedConnection);
    }
};

StallWatch& StallWatch::instance() {
    static StallWatch w;
    return w;
}

StallWatch::~StallWatch() { stop(); }

void StallWatch::start(int thresholdMs) {
    if (d_) return;
    d_ = new Impl;
    d_->threshold = std::max(50, thresholdMs);
    d_->reportFile = qEnvironmentVariable("IW8R_STALL_REPORT");
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &d_->gui, THREAD_ALL_ACCESS, FALSE, 0);
    d_->heart = new QTimer;
    QObject::connect(d_->heart, &QTimer::timeout, [impl = d_] { impl->beat.store(nowMs()); });
    d_->beat.store(nowMs());
    d_->heart->start(20);
    d_->watcher = std::thread([impl = d_] { impl->run(); });
}

void StallWatch::stop() {
    if (!d_) return;
    d_->quit.store(true);
    if (d_->watcher.joinable()) d_->watcher.join();
    delete d_->heart;
    if (d_->gui) CloseHandle(d_->gui);
    delete d_;
    d_ = nullptr;
}

bool StallWatch::running() const { return d_ != nullptr; }

int StallWatch::stalls() const { return d_ ? d_->stalls.load() : 0; }

QString StallWatch::lastReport() const {
    if (!d_) return {};
    std::lock_guard lock(d_->m);
    return d_->last;
}

}  // namespace iw8r::qt
