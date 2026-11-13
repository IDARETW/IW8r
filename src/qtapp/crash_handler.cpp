#include "crash_handler.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#endif

namespace iw8r::qt {

namespace {

#ifdef _WIN32
wchar_t g_dir[MAX_PATH * 2] = {};

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stem[MAX_PATH * 2];
    swprintf(stem, MAX_PATH * 2, L"%s\\crash-%04d%02d%02d-%02d%02d%02d", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    std::wstring txtPath = std::wstring(stem) + L".txt", dmpPath = std::wstring(stem) + L".dmp";

    HANDLE proc = GetCurrentProcess();
    HANDLE dmp = CreateFileW(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dmp != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(proc, GetCurrentProcessId(), dmp, MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                          &mei, nullptr, nullptr);
        CloseHandle(dmp);
    }

    FILE* f = _wfopen(txtPath.c_str(), L"w");
    if (!f) return EXCEPTION_CONTINUE_SEARCH;
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
    SymCleanup(proc);
    std::fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}

// abort() (std::terminate after an uncaught C++ exception, an allocation failure in a worker thread, ...) ends the
// process with a fail-fast that skips the filter above. The SIGABRT handler raises a structured exception instead, so
// onCrash writes the dump and the stack of the aborting thread (the throw site is still on it).
void onAbort(int) {
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
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

QString newestCrashReport(const QString& dir, qint64 since) {
    const QFileInfoList l = QDir(dir).entryInfoList({"crash-*.txt"}, QDir::Files, QDir::Time);
    if (l.isEmpty() || l.first().lastModified().toMSecsSinceEpoch() < since) return {};
    return l.first().absoluteFilePath();
}

}  // namespace iw8r::qt
