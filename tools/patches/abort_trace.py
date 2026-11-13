import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'src/qtapp/crash_handler.cpp'
patch(C, '''void onAbort(int) {
    RaiseException(0xE0000ABu, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}''', '''void onAbort(int) {
    // First a trace that needs no symbols and little memory (abort-<tick>.txt): the C++ exception being handled when
    // std::terminate called abort (an exception that left a thread), and the raw stack as module+offset (resolved offline
    // against the PDB). The symbolised report below can fail when memory is short.
    if (g_reserve) VirtualFree(g_reserve, 0, MEM_RELEASE), g_reserve = nullptr;
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%s\\\\abort-%lu.txt", g_dir, GetTickCount());
    if (FILE* f = _wfopen(path, L"w")) {
        try {
            if (const std::exception_ptr ex = std::current_exception()) std::rethrow_exception(ex);
            std::fprintf(f, "no C++ exception in flight\\n");
        } catch (const std::exception& e) {
            std::fprintf(f, "exception: %s\\n", e.what());
        } catch (...) {
            std::fprintf(f, "exception: not a std::exception\\n");
        }
        void* frames[62];
        const USHORT n = RtlCaptureStackBackTrace(0, 62, frames, nullptr);
        for (USHORT i = 0; i < n; ++i) {
            HMODULE m = nullptr;
            char mod[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(frames[i]), &m))
                GetModuleFileNameA(m, mod, MAX_PATH);
            const char* base = std::strrchr(mod, '\\\\');
            std::fprintf(f, "#%02u %s+0x%llx\\n", unsigned(i), base ? base + 1 : mod,
                         (unsigned long long)(reinterpret_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(m)));
        }
        std::fclose(f);
    }
    RaiseException(0xE0000ABu, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}''')
patch(C, '''#include <csignal>''', '''#include <csignal>
#include <cstring>
#include <exception>''')
print('ok')
