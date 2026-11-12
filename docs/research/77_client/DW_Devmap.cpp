// DW-only, link-time source integration: cold-start "devmap" for map testing.
//
//   game_dx12_ship_replay_local_dw.exe --devmap <id> [--gametype <name>] [--bots <n>]
//       [--devmap-path lobby|direct] [--devmap-boot init|frame] [--devmap-boot-dvars 0|1]
//
// One request per process, read once from the process command line. The stock
// startup-command parser drops every '+' token except connect_lobby, retaildev
// and devreplay (Com_ParseCommandLine 0x1412B03B0, list 0x1445ACE68), and text
// that is not a '+' token is ignored, so these arguments reach no native code.
//
// What the owner sees: a neutral dark cover from the first UI frame until the
// engine's own map loading screen takes over. The cover is one UI_FillRect at
// the end of the frame (Con_DeveloperFrame, inside ScrPlace_EndFrame, after
// every UI worker and before the developer console). Splash screens, mode
// select, the Multiplayer menu and the lobby still run underneath; nothing in
// the Lua UI is changed.
//
// Start paths:
//   lobby (default)  the game's own private-match start, without the menus:
//                    xblive_privatematch + xstartprivatematch, then ui_mapname /
//                    ui_gametype and PartyHost_RequestStartMatch(party, true)
//                    (skip countdown), as DW_LobbyControls.cpp does for
//                    replay_dw_forceStart.
//   direct           SV_DevMap_f 0x14136BBB0 (server application slot 15),
//                    through the dw_devmap command. No registered native command
//                    reaches that function in this build, so it is unproven.
//
// The lobby path only automates what the menus do, so it works in every DW
// client. The direct path and the dw_devmap command are experiments (a native
// with no caller in this build and a wait that has no timeout), so they need
// the development opt-in the client already has (replay-dw-test.enabled, as
// DW_LocalPlatform.cpp TestFrame); without it a direct request uses the lobby.
//
// UNVERIFIED (docs/research/77_loading_screen_first.md, section 5): this file
// compiles with the DW build flags but has not been linked or run. The
// earliest safe frame for each step, the alignment values of the cover, the
// Com_Init mode switch and the whole direct path need the live tests listed
// there.
#include "com_debugstates.h"
#include "replay_dvar.h"

extern "C"
{
    __declspec(dllimport) void *__stdcall LoadLibraryA(const char *);
    __declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
    __declspec(dllimport) int __stdcall FreeLibrary(void *);
    __declspec(dllimport) uint64_t __stdcall GetTickCount64();
    __declspec(dllimport) int __stdcall CloseHandle(void *);
    __declspec(dllimport) void *__stdcall CreateFileA(
        const char *, unsigned long, unsigned long, void *, unsigned long, unsigned long, void *);
    const char *DW_LocalConfig_TestEnabled(); // DW_LocalConfig.cpp: "replay-dw-test.enabled"

    bool DW_LocalBackend_IsEnabled();
    void DW_LocalBackend_LogLine(const char *); // DW_LocalBackend_Frame.cpp
    const char *DW_MapAvailability_InstalledIds(); // " id1 id2 ... " of custom map packages
    void DW_MapAvailability_EnsureScanned();
    uint8_t Live_IsUserSignedInToLive(int controller, int *reason); // DW_LocalPlatform.cpp
    uintptr_t Cbuf_AddText(int client, const char *text);           // Con_Developer.cpp

    void Com_GameMode_SetDesiredGameMode(unsigned char, bool); // 0x1410C88F0 (as DW_LocalPlatform.cpp)
    void CL_Mgr_ActivateClient(int, int);                      // as DW_LocalPlatform.cpp TestMenuMP_f
    void Dvar_SetString_Internal(void *, const char *);        // as DW_LobbyControls.cpp
    // UI_FillRect 0x141B9B700: (ScreenPlacement, x, y, width, height, horzAlign,
    // vertAlign, colour). It draws nothing while s_uiFillRectMaterial is null.
    void UI_FillRect(const void *, float, float, float, float, int, int, const float *);
    bool replay_rva_01187fa0();              // Lobby_IsInLobby
    void *replay_rva_01187ee0();             // Lobby_GetPartyData
    bool replay_rva_01189120(const void *);  // Party_AreWeHost
    bool replay_rva_0118dd00(const void *);  // Party_IsGameLobby
    void replay_rva_0119cfa0(void *, bool);  // PartyHost_RequestStartMatch(party, skipCountdown)
    // Direct path only.
    void *replay_rva_0132cc50();                    // SvGameModeApplication::GetActiveServerApplication
    void SV_DevMap_f(void *serverApplication);      // 0x14136BBB0, reads SV_Cmd argument 1
    void SV_Cmd_TokenizeString(const char *);       // 0x141298BD0
    void SV_Cmd_EndTokenizedString();               // 0x141298B90
    bool FenceManager_IsFenceTypeActive(unsigned controller, int typeMask); // 0x140F19830
}

namespace
{
    // Dvar tokens (con_dvar_names.h).
    constexpr char kUiMapname[] = "NSQLTTMRMP";       // ui_mapname
    constexpr char kUiGametype[] = "MOLPOSLOMO";      // ui_gametype
    constexpr char kViewedSplash[] = "MLNMPQOON";     // cg_viewedSplashScreen
    constexpr char kAutoSkipSplash[] = "NPLRKNKKOP";  // cg_autoSkipSplashScreen
    constexpr char kSkipBootFlow[] = "LKSQOLNKLP";    // lui_skip_boot_flow
    constexpr char kBotCount[] = "replay_dw_botCount"; // DW_LobbyControls.cpp
    // SV_DevMap_f waits on this fence type (index 2, NETWORKING) with no timeout.
    constexpr int kNetworkingFence = 4;
    constexpr int kMultiplayer = 2;

    // Waits in milliseconds. The settle values are guesses to be tuned by test.
    constexpr uint64_t kSignInLimit = 90000;
    constexpr uint64_t kModeLimit = 120000;
    constexpr uint64_t kFrontEndLimit = 180000;
    constexpr uint64_t kFrontEndSettle = 1000;
    constexpr uint64_t kLobbyLimit = 30000;
    constexpr uint64_t kLobbyRetry = 10000;
    constexpr uint64_t kLobbySettle = 500;
    constexpr uint64_t kStartLimit = 30000;

    enum class Phase : uint8_t
    {
        None,     // no request
        SignIn,   // waiting for the local platform sign-in
        Mode,     // waiting for Multiplayer to be the active game mode
        FrontEnd, // waiting for the front end scene
        Hosting,  // xstartprivatematch queued, waiting for our game lobby
        Started,  // start requested, waiting for the front end to shut down
        Done,
        Failed,
    };

    struct Request
    {
        char id[32];
        char gametype[16];
        unsigned bots;
        bool direct;    // --devmap-path direct
        bool bootInit;  // --devmap-boot init (default)
        bool bootDvars; // --devmap-boot-dvars 1
        bool valid;
        bool rejected;
    };

    Request s_request{};
    bool s_parsed = false;
    bool s_modeSetInInit = false;
    bool s_modeRequested = false;
    bool s_clientActivated = false;
    bool s_hostRetried = false;
    bool s_registered = false;
    bool s_directRan = false;
    Phase s_phase = Phase::None;
    uint64_t s_phaseSince = 0, s_stableSince = 0;
    cmd_function_s s_devmapCommand{};

    bool Equal(const char *a, const char *b)
    {
        while (*a && *a == *b)
            ++a, ++b;
        return *a == *b;
    }

    // Package ids and game types: lower-case letters, digits and underscore.
    bool SafeName(const char *text, unsigned minimum, unsigned maximum)
    {
        unsigned length = 0;
        for (; text[length]; ++length)
            if (!((text[length] >= 'a' && text[length] <= 'z') ||
                  (text[length] >= '0' && text[length] <= '9') || text[length] == '_'))
                return false;
        return length >= minimum && length <= maximum;
    }

    void Copy(char *target, unsigned capacity, const char *value)
    {
        unsigned i = 0;
        for (; i + 1 < capacity && value[i]; ++i)
            target[i] = value[i];
        target[i] = 0;
    }

    void Log(const char *text)
    {
        char line[224]{};
        Com_sprintf(line, sizeof(line), "DW_DEVMAP %s", text);
        DW_LocalBackend_LogLine(line);
    }

    void Log(const char *format, const char *a, const char *b)
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

    // One ASCII argument from the UTF-16 command line. Any other character,
    // or an argument that does not fit, invalidates the argument.
    const wchar_t *NextArgument(const wchar_t *at, char *out, unsigned capacity, bool &valid)
    {
        while (*at == L' ' || *at == L'\t')
            ++at;
        unsigned length = 0;
        valid = *at != 0;
        const bool quoted = *at == L'"';
        if (quoted)
            ++at;
        for (; *at && (quoted ? *at != L'"' : (*at != L' ' && *at != L'\t')); ++at)
        {
            if (*at < 33 || *at > 126 || length + 1 >= capacity)
                valid = false;
            else
                out[length++] = static_cast<char>(*at);
        }
        if (quoted && *at == L'"')
            ++at;
        out[length] = 0;
        return at;
    }

    void Parse()
    {
        if (s_parsed)
            return;
        s_parsed = true;
        // GetCommandLineW is resolved through the existing loader imports, as
        // DW_LocalConfig.cpp does for MoveFileExA.
        auto *kernel = LoadLibraryA("kernel32.dll");
        if (!kernel)
            return;
        const auto commandLine =
            reinterpret_cast<const wchar_t *(__stdcall *)()>(GetProcAddress(kernel, "GetCommandLineW"));
        const wchar_t *at = commandLine ? commandLine() : nullptr;
        Request request{};
        Copy(request.gametype, sizeof(request.gametype), "war");
        request.bootInit = true;
        bool present = false, good = true;
        char name[40]{}, value[40]{};
        bool valid = false;
        if (at)
            at = NextArgument(at, name, sizeof(name), valid); // the executable path
        while (at && *at)
        {
            at = NextArgument(at, name, sizeof(name), valid);
            if (!valid)
                continue;
            const bool id = Equal(name, "--devmap"), gametype = Equal(name, "--gametype"),
                       bots = Equal(name, "--bots"), path = Equal(name, "--devmap-path"),
                       boot = Equal(name, "--devmap-boot"), dvars = Equal(name, "--devmap-boot-dvars");
            if (!id && !gametype && !bots && !path && !boot && !dvars)
                continue;
            at = NextArgument(at, value, sizeof(value), valid);
            if (!valid)
            {
                good = false;
                continue;
            }
            if (id)
            {
                present = true;
                good = good && SafeName(value, 4, sizeof(request.id) - 1) && value[0] == 'm' &&
                       value[1] == 'p' && value[2] == '_';
                Copy(request.id, sizeof(request.id), value);
            }
            else if (gametype)
            {
                good = good && SafeName(value, 1, sizeof(request.gametype) - 1);
                Copy(request.gametype, sizeof(request.gametype), value);
            }
            else if (bots)
            {
                unsigned count = 0, digits = 0;
                for (; value[digits] >= '0' && value[digits] <= '9' && digits < 4; ++digits)
                    count = count * 10 + unsigned(value[digits] - '0');
                good = good && digits && !value[digits] && count <= 199;
                request.bots = count;
            }
            else if (path)
            {
                request.direct = Equal(value, "direct");
                good = good && (request.direct || Equal(value, "lobby"));
            }
            else if (boot)
            {
                request.bootInit = Equal(value, "init");
                good = good && (request.bootInit || Equal(value, "frame"));
            }
            else
            {
                request.bootDvars = Equal(value, "1");
                good = good && (request.bootDvars || Equal(value, "0"));
            }
        }
        FreeLibrary(kernel);
        if (!present)
            return;
        request.valid = good;
        request.rejected = !good;
        s_request = request;
    }

    dvar_t *SetBool(const char *name, bool enabled)
    {
        auto *dvar = Dvar_FindVarByName(name);
        if (dvar && dvar->type == 0 && dvar->current.enabled != enabled)
        {
            DvarValue value{};
            value.enabled = enabled;
            Dvar_SetVariant(dvar, &value, 0);
        }
        return dvar;
    }

    bool SetString(const char *name, const char *text)
    {
        auto *dvar = Dvar_FindVarByName(name);
        if (!dvar)
            return false;
        if (!dvar->current.string || !Equal(dvar->current.string, text))
            Dvar_SetString_Internal(dvar, text);
        return true;
    }

    bool Installed(const char *id)
    {
        // The list is " id1 id2 ... " with a space on both sides of every id.
        const char *list = DW_MapAvailability_InstalledIds();
        for (; list && *list; ++list)
        {
            if (*list != ' ')
                continue;
            unsigned i = 0;
            while (id[i] && list[1 + i] == id[i])
                ++i;
            if (!id[i] && list[1 + i] == ' ')
                return true;
        }
        return false;
    }

    void Enter(Phase phase, uint64_t now)
    {
        s_phase = phase;
        s_phaseSince = now;
        s_stableSince = 0;
    }

    void Fail(const char *reason, uint64_t now)
    {
        Log("failed: %s (%s); the cover is removed", reason, s_request.id);
        Enter(Phase::Failed, now);
    }

    // True once `condition` has held for `settle` milliseconds without a break.
    bool Stable(bool condition, uint64_t settle, uint64_t now)
    {
        if (!condition)
        {
            s_stableSince = 0;
            return false;
        }
        if (!s_stableSince)
            s_stableSince = now;
        return now - s_stableSince >= settle;
    }

    void ApplyMatchSettings()
    {
        // Asserted every frame until the front end shuts down: the lobby menu
        // may write its own default map when it opens.
        SetString(kUiMapname, s_request.id);
        SetString(kUiGametype, s_request.gametype);
        if (auto *bots = Dvar_FindVarByName(kBotCount))
        {
            if (unsigned(bots->current.integer) != s_request.bots)
            {
                DvarValue value{};
                value.integer = int(s_request.bots);
                Dvar_SetVariant(bots, &value, 0);
            }
        }
    }

    // dw_devmap [id]: the direct path. With no argument it serves the process
    // request; with one it lets the owner try the native devmap by hand from
    // the Multiplayer menu. Runs from the command buffer, never mid-frame.
    void Devmap_f()
    {
        if (!Sys_IsMainThread() || !DW_LocalBackend_IsEnabled() || !TestOptIn())
            return;
        const char *id = Cmd_Argc() == 2 ? Cmd_Argv(1) : s_request.valid ? s_request.id : "";
        if (!SafeName(id, 4, 31) || id[0] != 'm' || id[1] != 'p' || id[2] != '_')
        {
            Com_Printf(25, "Usage: dw_devmap <mp_map id> (Multiplayer menu, signed in)\n");
            return;
        }
        if (Com_GameMode_GetActiveGameMode() != kMultiplayer || !Live_IsUserSignedInToLive(0, nullptr))
        {
            Log("direct refused: %s needs Multiplayer active and a signed-in user%s", id, "");
            return;
        }
        // SV_DevMap_f loops on this fence with no timeout; never enter the loop
        // unless the fence already passes.
        const auto controller = static_cast<unsigned>(CL_Mgr_GetControllerFromClient(0));
        if (!FenceManager_IsFenceTypeActive(controller, kNetworkingFence))
        {
            Log("direct refused: %s, the NETWORKING fence does not pass%s", id, "");
            return;
        }
        void *application = replay_rva_0132cc50();
        if (!application)
        {
            Log("direct refused: %s, no active server application%s", id, "");
            return;
        }
        if (s_request.valid && Equal(id, s_request.id))
            SetString(kUiGametype, s_request.gametype);
        char command[48]{};
        Com_sprintf(command, sizeof(command), "devmap %s", id);
        Log("direct: calling SV_DevMap_f for %s%s", id, "");
        s_directRan = true;
        SV_Cmd_TokenizeString(command);
        SV_DevMap_f(application);
        SV_Cmd_EndTokenizedString();
        Log("direct: SV_DevMap_f returned for %s%s", id, "");
    }

    void Cover()
    {
        // 640x480 virtual space. Alignment 4 is taken to be "full screen"
        // (guess, from the classic ScreenPlacement enum); the second, oversized
        // rectangle with alignment 0 covers the sides if that guess is wrong.
        static const float colour[4] = {0.02f, 0.02f, 0.02f, 1.0f};
        UI_FillRect(scrPlaceFull, 0.0f, 0.0f, 640.0f, 480.0f, 4, 4, colour);
        UI_FillRect(scrPlaceFull, -640.0f, -480.0f, 1920.0f, 1440.0f, 0, 0, colour);
    }

    void Step(uint64_t now)
    {
        const bool signedIn = Live_IsUserSignedInToLive(0, nullptr) != 0;
        const int mode = Com_GameMode_GetActiveGameMode();
        const uint64_t elapsed = now - s_phaseSince;
        if (s_request.bootDvars && s_phase < Phase::Started)
        {
            SetBool(kViewedSplash, true);
            SetBool(kAutoSkipSplash, true);
            SetBool(kSkipBootFlow, true);
        }
        switch (s_phase)
        {
        case Phase::SignIn:
            if (signedIn)
            {
                Log("signed in%s%s", "", "");
                Enter(Phase::Mode, now);
            }
            else if (elapsed > kSignInLimit)
                Fail("no sign-in; is the local host running on 127.0.0.1:8085", now);
            break;
        case Phase::Mode:
            if (mode == kMultiplayer)
            {
                // Same native client transition as TestMenuMP_f, once.
                if (!s_clientActivated)
                {
                    CL_Mgr_ActivateClient(0, 0);
                    s_clientActivated = true;
                }
                Log("multiplayer active%s%s", s_modeSetInInit ? " (set in Com_Init)" : "", "");
                Enter(Phase::FrontEnd, now);
            }
            else if (mode != 0)
                Fail("another game mode is active", now);
            else if (!s_modeRequested)
            {
                Com_GameMode_SetDesiredGameMode(kMultiplayer, false);
                CL_Mgr_ActivateClient(0, 0);
                s_modeRequested = s_clientActivated = true;
                Log("selecting multiplayer%s%s", "", "");
            }
            else if (elapsed > kModeLimit)
                Fail("multiplayer did not become active", now);
            break;
        case Phase::FrontEnd:
            if (Stable(mode == kMultiplayer && signedIn && Com_FrontEndScene_IsActive(), kFrontEndSettle, now))
            {
                DW_MapAvailability_EnsureScanned();
                Log(Installed(s_request.id) ? "front end ready; %s is an installed package%s"
                                             : "front end ready; %s is not an installed package, trying it as a stock map%s",
                    s_request.id, "");
                if (s_request.direct)
                {
                    Cbuf_AddText(0, "dw_devmap\n");
                    Log("direct: queued for %s%s", s_request.id, "");
                    Enter(Phase::Started, now);
                }
                else
                {
                    Cbuf_AddText(0, "set LSTLQTSSRM 1\nxstartprivatematch\n");
                    Log("hosting a private match lobby%s%s", "", "");
                    Enter(Phase::Hosting, now);
                }
            }
            else if (elapsed > kFrontEndLimit)
                Fail("the front end did not become ready", now);
            break;
        case Phase::Hosting:
        {
            auto *party = replay_rva_01187fa0() ? replay_rva_01187ee0() : nullptr;
            const bool host = party && replay_rva_0118dd00(party) && replay_rva_01189120(party);
            if (host)
                ApplyMatchSettings();
            if (Stable(host, kLobbySettle, now))
            {
                replay_rva_0119cfa0(party, true);
                Log("start requested %s %s", s_request.id, s_request.gametype);
                Enter(Phase::Started, now);
            }
            else if (!host && !s_hostRetried && elapsed > kLobbyRetry)
            {
                s_hostRetried = true;
                Cbuf_AddText(0, "set LSTLQTSSRM 1\nxstartprivatematch\n");
                Log("no lobby yet; hosting again%s%s", "", "");
            }
            else if (elapsed > kLobbyLimit)
                Fail("no private match lobby that we host", now);
            break;
        }
        case Phase::Started:
            if (!Com_FrontEndScene_IsActive())
            {
                Log("loading %s; the engine loading screen takes over%s", s_request.id, "");
                Enter(Phase::Done, now);
            }
            else if (s_request.direct && !s_directRan && elapsed <= kStartLimit)
                break;
            else if (elapsed > kStartLimit)
                Fail(s_request.direct ? "the direct start did not leave the front end"
                                      : "the lobby did not start the match",
                     now);
            else if (!s_request.direct)
                ApplyMatchSettings();
            break;
        default:
            break;
        }
    }
} // namespace

// Source replacement of Com_Init_GameMode_FromCommandLine 0x1412AF5D0. Com_Init
// calls it once and commits the desired game mode on the next line. The native
// body only recognises "+mpMode 1" style tokens, which Com_ParseCommandLine
// has already dropped, so nothing native is lost.
extern "C" void Com_Init_GameMode_FromCommandLine()
{
    Parse();
    if (!s_request.valid || !s_request.bootInit || !DW_LocalBackend_IsEnabled())
        return;
    Com_GameMode_SetDesiredGameMode(kMultiplayer, true);
    // s_modeRequested stays false: if the mode is still 0 on the first frames,
    // DW_Devmap_Frame makes the same request again after sign-in.
    s_modeSetInInit = true;
}

// Called from Con_DeveloperFrame before the console is drawn; main thread.
extern "C" void DW_Devmap_Frame()
{
    if (!Sys_IsMainThread() || !DW_LocalBackend_IsEnabled())
        return;
    if (!s_registered)
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
        }
        if (s_request.rejected)
            Log("failed: the --devmap arguments are not valid%s%s", "", "");
        else if (s_request.valid)
        {
            char text[160]{};
            Com_sprintf(text, sizeof(text), "request id=%s gametype=%s bots=%u path=%s boot=%s", s_request.id,
                        s_request.gametype, s_request.bots, s_request.direct ? "direct" : "lobby",
                        s_request.bootInit ? "init" : "frame");
            Log(text);
            Enter(Phase::SignIn, now);
        }
    }
    if (s_phase == Phase::None || s_phase == Phase::Done || s_phase == Phase::Failed)
        return;
    Step(GetTickCount64());
    if (s_phase != Phase::Done && s_phase != Phase::Failed)
        Cover();
}
