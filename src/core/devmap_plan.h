// IW8 Radiant - one-key "devmap" for Replay 1.20 MP (docs/research/65_devmap_one_key.md).
//
// Replay 1.20 is a ship build: in multiplayer it has no `map` / `devmap` console command (those are registered only
// by ComGameModeAppSP::InitializeGameMode, the SP table at 0x145A05960; the MP server table at 0x1445C00D0 holds
// hostmigration_start(_backout), sv_recordEventFF and sv_systemlinkStartMatch). A custom map is started the way the
// lobby starts any map: the host sets ui_mapname / ui_gametype and asks the party to start
// (PartyHost_StartMatch_Internal reads ui_mapname through Party_GetMapName). This file is the pure part: game-state
// classification from a memory snapshot, the command texts, and the next step of the F5 flow. No I/O.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r::devmap {

// Native Replay 1.20 RVAs (stock game_dx12_ship_replay.exe; recompiled exes map them through .rplmap).
// Dvar pointers: the dvar_t* global; the current value is at +0x28 (bool byte / char* for strings).
inline constexpr uint32_t kRvaDvarUiMapname = 0xD386740;          // NSQLTTMRMP ui_mapname
inline constexpr uint32_t kRvaDvarUiGametype = 0xD386738;         // MOLPOSLOMO ui_gametype
inline constexpr uint32_t kRvaDvarSystemlink = 0xD386788;         // LPSPMQSNPQ systemlink
inline constexpr uint32_t kRvaDvarSystemlinkHost = 0xD386790;     // LLPNKKORPT systemlink_host
inline constexpr uint32_t kRvaDvarPrivateMatch = 0xD386820;       // LSTLQTSSRM xblive_privatematch
inline constexpr uint32_t kRvaDvarServerRunning = 0xD385FB0;      // Com_IsAnyLocalServerRunning's dvar
inline constexpr uint32_t kRvaActiveGameMode = 0xC5244E0;         // byte: 0 none, 1 SP, 2 MP, 3 CP
inline constexpr uint32_t kRvaFrontEndState = 0xC4EBEB0;          // int: >= 5 means the front end scene is active
inline constexpr uint32_t kRvaPartyInParty = 0xC7E23CC;           // int g_partyData_inParty
inline constexpr uint32_t kRvaPartyAreWeHost = 0xC7E23C8;         // int g_partyData_areWeHost
inline constexpr uint32_t kDvarValueOffset = 0x28;

// What the editor read from the running game (DirectLink). `valid` false = nothing could be read.
struct GameSnapshot {
    bool valid = false;
    int gameMode = -1;          // Com_GameMode_GetActiveGameMode
    int frontEndState = 0;      // Com_FrontEndScene_IsActive is frontEndState >= 5
    bool inParty = false, partyHost = false;
    bool serverRunning = false;
    bool inMatch = false;       // the local player entity exists (g_entities -> client)
    bool systemlink = false, systemlinkHost = false, privateMatch = false;
    bool commandBufferIdle = false;  // s_cbufMainBuffer.size == 0 (DirectLink::sendCommand accepts text)
    std::string uiMapname, uiGametype;

    bool frontEnd() const { return frontEndState >= 5; }
};

enum class State {
    Unknown,           // snapshot not valid (wrong build, no access)
    Booting,           // not yet in a mode / front end
    NotMultiplayer,    // a mode other than MP is active (SP, CP)
    FrontEndNoParty,   // MP front end, no party: the editor hosts a private match first
    LobbyHost,         // MP lobby and we host it: set the map, start
    LobbyGuest,        // in someone else's lobby: cannot start
    InMatchHost,       // a match runs on this machine's server
    InMatchGuest,      // a match on someone else's server
};
State classify(const GameSnapshot& s);
const char* stateName(State s);

// --- Command texts (sent through the native command buffer; one command per line). Dvars are given by their Replay
// token: the command buffer path does not translate readable names (docs/research/47 §1.1).
std::string setMapCommand(const std::string& mapId);           // set NSQLTTMRMP <id>
std::string setGametypeCommand(const std::string& gametype);   // set MOLPOSLOMO <gametype>
// Host a private match lobby from the MP front end: the native xstartprivatematch (CL_MainMP_StartPrivateMatchHost_f)
// after xblive_privatematch is set (xpartygo refuses outside private match / system link lobbies).
std::vector<std::string> hostLobbyCommands();
// Start the match in the lobby we host: map + mode, then the native start request (PartyHost_RequestStartMatch)
// with no lobby countdown (research 76 section 2.3: the countdown is 5 + 3 s). In a private match lobby that is
// "xpartygo 1" (the argument is the skip flag). sv_systemlinkStartMatch (a Local Play lobby we host) has no skip
// flag, so its two timer dvars are set to 0 first (pt_privateGameStartTimerLength NMQNNPQPRS,
// pt_privatePregameStartTimerLength LTLOQSNSOT).
std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s);
// The same with the dvars of a game mode preset set first ("set <name> <value>", game_modes.h): a private match does
// not apply playlist dvars, so a Warzone variant has to bring its own (research 87, 4.4). Pairs that are not a plain
// name and value are left out.
std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s,
                                            const std::vector<std::pair<std::string, std::string>>& modeDvars);
// Leave the running match back to the lobby (host: the server tells every client to back out; party stays).
std::string backToLobbyCommand();  // xpartybackout
// DW build only (DW_MapRefresh.cpp): reload the running custom map's level zones from disk (a same-map restart keeps
// them loaded). Used after a rebuild of the map that is running.
std::string reloadCommand();       // map_reload
// DW build only (DW_LocalPlatform.cpp TestMenuMP_f, active when replay-dw-test.enabled existed at launch): from the
// mode-select front end (game mode 0), select Multiplayer through the native mode/client transition.
std::string selectMultiplayerCommand();  // dw_localTestMenuMP

// Joins commands into one command-buffer text (newline separated). Refuses ';', quotes and control characters,
// so one F5 never runs anything but the listed lines.
bool joinCommands(const std::vector<std::string>& commands, std::string& out, std::string* err = nullptr);

// Map ids the DW client says it can load: the last "DW_CUSTOM_MAPS installed: ..." line of replay-dw-client.log.
// `found` is false when the text has no such line (not a DW build, or the log was rotated).
std::vector<std::string> parseDwInstalledMaps(const std::string& logText, bool* found = nullptr);
// DW reload outcome lines after a map_reload: 1 = "level zones reloaded from disk", -1 = refused / failed, 0 = none yet.
int parseDwReloadResult(const std::string& logTail, const std::string& mapId);

// Startup arguments for a launch. "+mpMode 1" does NOT work in this build (research 76 section 2.1:
// Com_ParseCommandLine blanks every '+' token except connect_lobby, retaildev and devreplay, so
// Com_Init_GameMode_FromCommandLine never sees it). It stays the default because it is harmless and becomes real if
// the client is made to accept it. The request the client reads by itself is startRequestArgs().
inline constexpr const char* kDefaultLaunchArgs = "+mpMode 1";
std::vector<std::string> splitLaunchArgs(const std::string& text);  // whitespace split, double quotes group
// The start request for a client that boots straight into a map (research 76 step 2, research 77): "--devmap <id>
// --gametype <name> [--bots <n>]". The client reads it from the process command line; the engine drops it. Empty when
// the id or the game type is not a plain lower-case name.
std::vector<std::string> startRequestArgs(const std::string& mapId, const std::string& gametype, int bots = 0);
// The log prefix of a client with that feature. An exe that does not contain this text gets no start request.
inline constexpr const char* kClientRequestMarker = "DW_DEVMAP ";

// What replay-dw-client.log says since this launch (the text after the log size recorded at launch).
struct LaunchLog {
    bool signedIn = false;     // DW_LOCAL_PLATFORM_SIGNED_IN or DW_LOCAL_IDENTITY_CLAIMS_RESTORED
    bool probeFailed = false;  // DW_LOCAL_PROBE_FAILED: the online-services host did not answer
    int client = 0;            // DW_DEVMAP lines: 0 none, 1 the client runs the start itself, -1 it reported a failure
    std::string clientLine;    // the last DW_DEVMAP line, without the prefix
};
LaunchLog parseDwLaunchLog(const std::string& textSinceLaunch);

// Which exe F5 starts when Tool Paths "launch" is empty: game_dx12_ship_replay_local_dw.exe, then
// game_dx12_ship_replay_dw.exe, then any other recompiled exe, then a stock exe (which cannot load custom maps: there
// is no proxy DLL any more). -1 when the list is empty.
struct ExeCandidate {
    std::string path;
    bool recompiled = false;
};
int pickLaunchExe(const std::vector<ExeCandidate>& exes);

// The local online-services host (E:\DW\IW8 Host-Local-Server.ps1; health at http://127.0.0.1:8085/health).
bool healthIsDwHost(const std::string& healthBody);                             // "server" is "iw8-dw"
std::string iniValue(const std::string& iniText, const std::string& key);      // "key=value" lines; empty when absent
bool isLoopbackHost(const std::string& host);                                  // 127.x.x.x, localhost, ::1

// --- The F5 flow. The runner (src/qtapp/devmap_launch.*) polls the game and asks decide() what to do next.
enum class Phase {
    Start,             // compile finished: look at the game
    WaitBoot,          // the editor launched the game: wait for the MP front end
    WaitLobby,         // hostLobbyCommands sent: wait for our lobby
    WaitFrontEnd,      // backToLobbyCommand sent: wait for the lobby after the match ends
    WaitMatch,         // start commands sent: wait for the match on the map
    WaitReload,        // reloadCommand sent: wait for the DW reload report
    Done,
    Failed,
};

enum class Action {
    None,              // keep waiting (poll again)
    Launch,            // deploy (game not running), then start the game with the launch arguments
    Deploy,            // install the package now (no game holds it)
    DeployThenReload,  // install, then reloadCommand (the map runs; DW reload)
    DeployThenStart,   // install, then startMatchCommands
    StartMatch,        // send startMatchCommands (already installed)
    SelectMultiplayer, // send selectMultiplayerCommand (DW, mode-select screen)
    HostLobby,         // send hostLobbyCommands
    BackToLobby,       // send backToLobbyCommand
    Succeed,           // in game on the map
    Fail,              // stop with `message`
};

struct Context {
    std::string mapId, gametype = "war";
    int processCount = 0;            // exes running from the game folder
    bool dwBuild = false;            // replay-dw-client.log names the installed packages (DW client)
    std::vector<std::string> dwInstalled;
    bool reloadAllowed = true;       // setting: use map_reload for a rebuild of the running map
    int64_t elapsedMs = 0;           // time in the current phase
    int64_t stableMs = 0;            // time the classified state has not changed
    int dwReload = 0;                // parseDwReloadResult since the reload was sent
    int selectMpSent = 0;            // how many times selectMultiplayerCommand was sent this run
    bool signedIn = false;           // the DW client log shows the local sign-in (LaunchLog::signedIn)
    int hostLobbySent = 0;           // how many times hostLobbyCommands was sent this run
    int clientRequest = 0;           // LaunchLog::client: 1 = the client starts the map itself, the editor only watches
    std::string clientMessage;       // LaunchLog::clientLine
};
// Mode select on the DW client: Multiplayer is selected (at most 3 times) once the client has signed in and the
// screen stayed kModeSelectSignedInMs, then again every kModeSelectRetryMs. Without the sign-in line (an older client,
// a rotated log) the old fixed wait applies: kModeSelectSettleMs per try.
inline constexpr int64_t kModeSelectSettleMs = 15000, kModeSelectSignedInMs = 2000, kModeSelectRetryMs = 8000;

struct Decision {
    Action action = Action::None;
    Phase next = Phase::Start;       // the phase after the action
    std::string message;             // status text (what happens, or why it stopped)
};

// Timeouts per phase (ms).
inline constexpr int64_t kBootTimeoutMs = 300000, kLobbyTimeoutMs = 30000, kFrontEndTimeoutMs = 60000,
                         kMatchTimeoutMs = 300000, kReloadTimeoutMs = 180000;
// The MP front end must stay put this long before the editor hosts a lobby, and the lobby this long before the start
// request. A DW client that has not shown its sign-in line yet gets the longer kFrontEndUnsignedSettleMs. When no
// lobby appears kHostLobbyRetryMs after the host commands, they are sent once more.
inline constexpr int64_t kFrontEndSettleMs = 1500, kFrontEndUnsignedSettleMs = 5000, kLobbySettleMs = 1500,
                         kHostLobbyRetryMs = 10000;

Decision decide(Phase phase, const GameSnapshot& s, const Context& c);

// The cover card of a cold start (src/qtapp/devmap_cover.*): shown over the game window from the launch until the map
// begins to load. `sinceStartRequestMs` is the time since the start request went out (the editor's start commands, or
// the client's own "DW_DEVMAP start ..." line), -1 before it. After the request the card stays only while the front
// end is still on screen, and never longer than kCoverMaxAfterStartMs.
inline constexpr int64_t kCoverMaxAfterStartMs = 4000;
// The card is also removed when nothing has been reported for this long (no state change, no log line): the mapper
// then sees the game and can act. Boot zone loading alone was 52 s without an event in the one timed run.
inline constexpr int64_t kCoverStallMs = 150000;
bool coverWanted(bool launchedByEditor, int64_t sinceStartRequestMs, const GameSnapshot& s);
// True for a client line that says the map start went out ("start requested ...", "loading ...").
bool clientLineIsStart(const std::string& clientLine);

}  // namespace iw8r::devmap
