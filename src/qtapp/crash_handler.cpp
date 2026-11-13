#include "crash_handler.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <csignal>
#include <cstring>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#endif

namespace iw8r::qt {

namespace {

#ifdef _WIN32
wchar_t g_dir[MAX_PATH * 2] = {};
// Memory kept back for the report: an out-of-memory crash otherwise leaves an empty dump and no stack (Verdansk,
// 2026-11-13). Freed first thing in onCrash.
void* g_reserve = nullptr;

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    if (g_reserve) VirtualFree(g_reserve, 0, MEM_RELEASE), g_reserve = nullptr;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stem[MAX_PATH * 2];
    swprintf(stem, MAX_PATH * 2, L"%s\\crash-%04d%02d%02d-%02d%02d%02d", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    std::wstring txtPath = std::wstring(stem) + L".txt", dmpPath = std::wstring(stem) + L".dmp";

    HANDLE proc = GetCurrentProcess();
    const auto writeDump = [&] {
        HANDLE dmp = CreateFileW(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dmp == INVALID_HANDLE_VALUE) return;
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        if (!MiniDumpWriteDump(proc, GetCurrentProcessId(), dmp, MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                               &mei, nullptr, nullptr)) {
            SetFilePointer(dmp, 0, nullptr, FILE_BEGIN);
            SetEndOfFile(dmp);
            MiniDumpWriteDump(proc, GetCurrentProcessId(), dmp, MiniDumpNormal, &mei, nullptr, nullptr);
        }
        CloseHandle(dmp);
    };

    FILE* f = _wfopen(txtPath.c_str(), L"w");
    if (!f) {
        writeDump();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    std::fprintf(f, "IW8 Radiant Suite crash\nexception 0x%08lX at %p\n", ep->ExceptionRecord->ExceptionCode,
                 ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        std::fprintf(f, "access violation %s address 0x%llx\n", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                     (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512];
    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            !sf.AddrPC.Offset)
            break;
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        const bool named = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym);
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const bool lined = SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line);
        char mod[MAX_PATH] = "?";
        if (HMODULE hm = nullptr; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                     reinterpret_cast<LPCSTR>(sf.AddrPC.Offset), &hm))
            GetModuleFileNameA(hm, mod, MAX_PATH);
        const char* base = std::strrchr(mod, '\\');
        std::fprintf(f, "#%02d %s!%s+0x%llx", i, base ? base + 1 : mod, named ? sym->Name : "?", (unsigned long long)disp);
        if (lined) std::fprintf(f, "  %s:%lu", line.FileName, line.LineNumber);
        std::fprintf(f, "\n");
    }
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc)))
        std::fprintf(f, "memory: commit %llu MB, peak commit %llu MB, working set %llu MB\n", (unsigned long long)(pmc.PagefileUsage >> 20),
                     (unsigned long long)(pmc.PeakPagefileUsage >> 20), (unsigned long long)(pmc.WorkingSetSize >> 20));
    SymCleanup(proc);
    std::fclose(f);
    writeDump();  // after the text: the text is what matters when memory is short
    return EXCEPTION_CONTINUE_SEARCH;
}

// abort() (std::terminate after an uncaught C++ exception, an allocation failure in a worker thread, ...) ends the
// process with a fail-fast that skips the filter above. The SIGABRT handler raises a structured exception instead, so
// onCrash writes the dump and the stack of the aborting thread (the throw site is still on it).
void onAbort(int) {
    // First a trace that needs no symbols and little memory (abort-<tick>.txt): the C++ exception being handled when
    // std::terminate called abort (an exception that left a thread), and the raw stack as module+offset (resolved offline
    // against the PDB). The symbolised report below can fail when memory is short.
    if (g_reserve) VirtualFree(g_reserve, 0, MEM_RELEASE), g_reserve = nullptr;
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%s\\abort-%lu.txt", g_dir, GetTickCount());
    if (FILE* f = _wfopen(path, L"w")) {
        try {
            if (const std::exception_ptr ex = std::current_exception()) std::rethrow_exception(ex);
            std::fprintf(f, "no C++ exception in flight\n");
        } catch (const std::exception& e) {
            std::fprintf(f, "exception: %s\n", e.what());
        } catch (...) {
            std::fprintf(f, "exception: not a std::exception\n");
        }
        void* frames[62];
        const USHORT n = RtlCaptureStackBackTrace(0, 62, frames, nullptr);
        for (USHORT i = 0; i < n; ++i) {
            HMODULE m = nullptr;
            char mod[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(frames[i]), &m))
                GetModuleFileNameA(m, mod, MAX_PATH);
            const char* base = std::strrchr(mod, '\\');
            std::fprintf(f, "#%02u %s+0x%llx\n", unsigned(i), base ? base + 1 : mod,
                         (unsigned long long)(reinterpret_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(m)));
        }
        std::fclose(f);
    }
    RaiseException(0xE0000ABu, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}
#endif

}  // namespace

void installCrashHandler(const QString& dir) {
    QDir().mkpath(dir);
#ifdef _WIN32
    const std::wstring w = QDir::toNativeSeparators(dir).toStdWString();
    wcsncpy_s(g_dir, w.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(onCrash);
    std::signal(SIGABRT, onAbort);
    if (!g_reserve) g_reserve = VirtualAlloc(nullptr, 64u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

size_t processCommitMb() {
    PROCESS_MEMORY_COUNTERS pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return size_t(pmc.PagefileUsage >> 20);
}

QString newestCrashReport(const QString& dir, qint64 since) {
    const QFileInfoList l = QDir(dir).entryInfoList({"crash-*.txt"}, QDir::Files, QDir::Time);
    if (l.isEmpty() || l.first().lastModified().toMSecsSinceEpoch() < since) return {};
    return l.first().absoluteFilePath();
}

}  // namespace iw8r::qt
