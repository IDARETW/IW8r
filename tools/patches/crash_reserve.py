import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'src/qtapp/crash_handler.cpp'
M = 'src/qtapp/main.cpp'

patch(C, '''#ifdef _WIN32
wchar_t g_dir[MAX_PATH * 2] = {};

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;''', '''#ifdef _WIN32
wchar_t g_dir[MAX_PATH * 2] = {};
// Memory kept back for the report: an out-of-memory crash otherwise leaves an empty dump and no stack (Verdansk,
// 2026-11-13). Freed first thing in onCrash.
void* g_reserve = nullptr;

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    if (g_reserve) VirtualFree(g_reserve, 0, MEM_RELEASE), g_reserve = nullptr;''')
# the stack text first (small), then the dump (a normal one if the full one fails)
patch(C, '''    HANDLE proc = GetCurrentProcess();
    HANDLE dmp = CreateFileW(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dmp != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(proc, GetCurrentProcessId(), dmp, MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                          &mei, nullptr, nullptr);
        CloseHandle(dmp);
    }
''', '''    HANDLE proc = GetCurrentProcess();
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
''')
patch(C, '''    SymCleanup(proc);
    std::fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}''', '''    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc)))
        std::fprintf(f, "memory: commit %llu MB, peak commit %llu MB, working set %llu MB\\n", (unsigned long long)(pmc.PagefileUsage >> 20),
                     (unsigned long long)(pmc.PeakPagefileUsage >> 20), (unsigned long long)(pmc.WorkingSetSize >> 20));
    SymCleanup(proc);
    std::fclose(f);
    writeDump();  // after the text: the text is what matters when memory is short
    return EXCEPTION_CONTINUE_SEARCH;
}''')
patch(C, '''    FILE* f = _wfopen(txtPath.c_str(), L"w");
    if (!f) return EXCEPTION_CONTINUE_SEARCH;''', '''    FILE* f = _wfopen(txtPath.c_str(), L"w");
    if (!f) {
        writeDump();
        return EXCEPTION_CONTINUE_SEARCH;
    }''')
patch(C, '''    SetUnhandledExceptionFilter(onCrash);
    std::signal(SIGABRT, onAbort);''', '''    SetUnhandledExceptionFilter(onCrash);
    std::signal(SIGABRT, onAbort);
    if (!g_reserve) g_reserve = VirtualAlloc(nullptr, 64u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);''')
patch(C, '''#include <dbghelp.h>''', '''#include <dbghelp.h>
#include <psapi.h>''')
# the bench: a memory sample every 2 s into <out>/memory.log (left behind by a crash)
patch(M, '''int runViewportBench(iw8r::qt::MainWindow& w, const QString& mapFile, const QString& outDir, const QString& referenceDir) {''',
      '''int runViewportBench(iw8r::qt::MainWindow& w, const QString& mapFile, const QString& outDir, const QString& referenceDir) {
    // A memory sample every 2 s from its own thread (the UI thread can be busy for long), appended as it is taken so a
    // crash leaves the trace: seconds, commit MB, working set MB.
    std::thread([outDir] {
        QElapsedTimer clock;
        clock.start();
        const std::string path = QDir(outDir).filePath("memory.log").toStdString();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS pmc{};
            if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) continue;
            if (FILE* f = std::fopen(path.c_str(), "a")) {
                std::fprintf(f, "%lld %llu %llu\\n", (long long)(clock.elapsed() / 1000), (unsigned long long)(pmc.PagefileUsage >> 20),
                             (unsigned long long)(pmc.WorkingSetSize >> 20));
                std::fclose(f);
            }
#endif
        }
    }).detach();''')
print('ok')
