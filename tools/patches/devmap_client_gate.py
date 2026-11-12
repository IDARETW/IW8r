import io
p = 'docs/research/77_client/DW_Devmap.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
pairs = [
(r'''// UNVERIFIED (docs/research/77_loading_screen_first.md, section 5): nothing in
// this file has been compiled or run. The earliest safe frame for each step,
// the alignment values of the cover, the Com_Init mode switch and the whole
// direct path need the live tests listed there.''',
 r'''// The lobby path only automates what the menus do, so it works in every DW
// client. The direct path and the dw_devmap command are experiments (a native
// with no caller in this build and a wait that has no timeout), so they need
// the development opt-in the client already has (replay-dw-test.enabled, as
// DW_LocalPlatform.cpp TestFrame); without it a direct request uses the lobby.
//
// UNVERIFIED (docs/research/77_loading_screen_first.md, section 5): this file
// compiles with the DW build flags but has not been linked or run. The
// earliest safe frame for each step, the alignment values of the cover, the
// Com_Init mode switch and the whole direct path need the live tests listed
// there.'''),
(r'''    __declspec(dllimport) uint64_t __stdcall GetTickCount64();
''',
 r'''    __declspec(dllimport) uint64_t __stdcall GetTickCount64();
    __declspec(dllimport) int __stdcall CloseHandle(void *);
    __declspec(dllimport) void *__stdcall CreateFileA(
        const char *, unsigned long, unsigned long, void *, unsigned long, unsigned long, void *);
    const char *DW_LocalConfig_TestEnabled(); // DW_LocalConfig.cpp: "replay-dw-test.enabled"
'''),
(r'''    void Log(const char *format, const char *a, const char *b)
    {
        char text[192]{};
        Com_sprintf(text, sizeof(text), format, a, b);
        Log(text);
    }
''',
 r'''    void Log(const char *format, const char *a, const char *b)
    {
        char text[192]{};
        Com_sprintf(text, sizeof(text), format, a, b);
        Log(text);
    }

    // The development opt-in, read once: the file exists beside the exe.
    bool TestOptIn()
    {
        static unsigned mode = 0;
        if (!mode)
        {
            auto file = CreateFileA(DW_LocalConfig_TestEnabled(), 0x80000000, 3, nullptr, 3, 0x80, nullptr);
            mode = file == reinterpret_cast<void *>(~uintptr_t{0}) ? 1U : 2U;
            if (mode == 2)
                CloseHandle(file);
        }
        return mode == 2;
    }
'''),
(r'''        if (!Sys_IsMainThread() || !DW_LocalBackend_IsEnabled())
            return;
        const char *id = Cmd_Argc() == 2 ? Cmd_Argv(1) : s_request.valid ? s_request.id : "";''',
 r'''        if (!Sys_IsMainThread() || !DW_LocalBackend_IsEnabled() || !TestOptIn())
            return;
        const char *id = Cmd_Argc() == 2 ? Cmd_Argv(1) : s_request.valid ? s_request.id : "";'''),
(r'''    if (!s_registered)
    {
        Cmd_AddCommandInternal("dw_devmap", Devmap_f, &s_devmapCommand);
        s_registered = true;
        Parse();
        const auto now = GetTickCount64();''',
 r'''    if (!s_registered)
    {
        if (TestOptIn())
            Cmd_AddCommandInternal("dw_devmap", Devmap_f, &s_devmapCommand);
        s_registered = true;
        Parse();
        const auto now = GetTickCount64();
        if (s_request.valid && s_request.direct && !TestOptIn())
        {
            s_request.direct = false;
            Log("the direct path needs the development opt-in; using the lobby path%s%s", "", "");
        }'''),
]
for a, b in pairs:
    assert s.count(a) == 1, (s.count(a), a[:60])
    s = s.replace(a, b)
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
print('ok')
