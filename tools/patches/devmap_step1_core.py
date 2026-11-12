import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/core/devmap_plan.h', [
(r'''// Start the match in the lobby we host: map + mode, then the native start request (PartyHost_RequestStartMatch):
// sv_systemlinkStartMatch in a system link (Local Play) lobby we host, xpartygo otherwise.''',
 r'''// Start the match in the lobby we host: map + mode, then the native start request (PartyHost_RequestStartMatch)
// with no lobby countdown (research 76 section 2.3: the countdown is 5 + 3 s). In a private match lobby that is
// "xpartygo 1" (the argument is the skip flag). sv_systemlinkStartMatch (a Local Play lobby we host) has no skip
// flag, so its two timer dvars are set to 0 first (pt_privateGameStartTimerLength NMQNNPQPRS,
// pt_privatePregameStartTimerLength LTLOQSNSOT).'''),
(r'''// Startup arguments for a launch: "+mpMode 1" boots straight into multiplayer (Com_Init_GameMode_FromCommandLine
// reads startup commands "coreMode"/"mpMode"/"cpMode"/"spMode"/"noMode" <0|1>; Com_ParseCommandLine splits at '+').
inline constexpr const char* kDefaultLaunchArgs = "+mpMode 1";
std::vector<std::string> splitLaunchArgs(const std::string& text);  // whitespace split, double quotes group''',
 r'''// Startup arguments for a launch. "+mpMode 1" does NOT work in this build (research 76 section 2.1:
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
bool isLoopbackHost(const std::string& host);                                  // 127.x.x.x, localhost, ::1'''),
(r'''    int selectMpSent = 0;            // how many times selectMultiplayerCommand was sent this run
};
// The mode-select screen must stay this long before the DW Multiplayer selection is sent (at most 3 times).
inline constexpr int64_t kModeSelectSettleMs = 15000;''',
 r'''    int selectMpSent = 0;            // how many times selectMultiplayerCommand was sent this run
    bool signedIn = false;           // the DW client log shows the local sign-in (LaunchLog::signedIn)
    int hostLobbySent = 0;           // how many times hostLobbyCommands was sent this run
    int clientRequest = 0;           // LaunchLog::client: 1 = the client starts the map itself, the editor only watches
    std::string clientMessage;       // LaunchLog::clientLine
};
// Mode select on the DW client: Multiplayer is selected (at most 3 times) once the client has signed in and the
// screen stayed kModeSelectSignedInMs, then again every kModeSelectRetryMs. Without the sign-in line (an older client,
// a rotated log) the old fixed wait applies: kModeSelectSettleMs per try.
inline constexpr int64_t kModeSelectSettleMs = 15000, kModeSelectSignedInMs = 2000, kModeSelectRetryMs = 8000;'''),
(r'''// The MP front end must stay put this long before the editor hosts a lobby (the DW client signs in after the menu
// appears), and the lobby this long before the start request.
inline constexpr int64_t kFrontEndSettleMs = 5000, kLobbySettleMs = 1500;''',
 r'''// The MP front end must stay put this long before the editor hosts a lobby, and the lobby this long before the start
// request. A DW client that has not shown its sign-in line yet gets the longer kFrontEndUnsignedSettleMs. When no
// lobby appears kHostLobbyRetryMs after the host commands, they are sent once more.
inline constexpr int64_t kFrontEndSettleMs = 1500, kFrontEndUnsignedSettleMs = 5000, kLobbySettleMs = 1500,
                         kHostLobbyRetryMs = 10000;'''),
])

edit('src/core/devmap_plan.cpp', [
(r'''    // SV_SystemLinkStartMatch_f requires systemlink_host; CL_MainMP_PartyGo_f requires xblive_privatematch.
    out.push_back(s.systemlink && s.systemlinkHost && !s.privateMatch ? "sv_systemlinkStartMatch" : "xpartygo");
    return out;''',
 r'''    // SV_SystemLinkStartMatch_f requires systemlink_host; CL_MainMP_PartyGo_f requires xblive_privatematch.
    if (s.systemlink && s.systemlinkHost && !s.privateMatch) {
        out.push_back("set NMQNNPQPRS 0");
        out.push_back("set LTLOQSNSOT 0");
        out.push_back("sv_systemlinkStartMatch");
    } else {
        out.push_back("xpartygo 1");
    }
    return out;'''),
(r'''Decision decide(Phase phase, const GameSnapshot& s, const Context& c) {
    const State st = classify(s);''',
 r'''namespace {

bool plainName(const std::string& s, size_t minLen, size_t maxLen) {
    if (s.size() < minLen || s.size() > maxLen) return false;
    for (char ch : s)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
    return true;
}

std::string lowerFileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return n;
}

bool isSpace(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

}  // namespace

std::vector<std::string> startRequestArgs(const std::string& mapId, const std::string& gametype, int bots) {
    const std::string g = gametype.empty() ? "war" : gametype;
    if (!plainName(mapId, 4, 15) || mapId.compare(0, 3, "mp_") != 0 || !plainName(g, 1, 24)) return {};
    std::vector<std::string> out = {"--devmap", mapId, "--gametype", g};
    if (bots > 0) {
        out.push_back("--bots");
        out.push_back(std::to_string(bots > 199 ? 199 : bots));
    }
    return out;
}

LaunchLog parseDwLaunchLog(const std::string& text) {
    LaunchLog out;
    out.signedIn = text.find("DW_LOCAL_PLATFORM_SIGNED_IN") != std::string::npos ||
                   text.find("DW_LOCAL_IDENTITY_CLAIMS_RESTORED") != std::string::npos;
    out.probeFailed = text.find("DW_LOCAL_PROBE_FAILED") != std::string::npos &&
                      text.find("DW_LOCAL_PROBE_OK") == std::string::npos;
    static const std::string key = kClientRequestMarker;
    size_t p = 0;
    while ((p = text.find(key, p)) != std::string::npos) {
        p += key.size();
        const size_t end = text.find_first_of("\r\n", p);
        out.clientLine = text.substr(p, end == std::string::npos ? std::string::npos : end - p);
        // A request is "the client's" from its first line on; a line that starts with "failed" hands the start back.
        out.client = out.clientLine.compare(0, 6, "failed") == 0 ? -1 : 1;
    }
    return out;
}

int pickLaunchExe(const std::vector<ExeCandidate>& exes) {
    int best = -1, bestRank = 99;
    for (size_t i = 0; i < exes.size(); ++i) {
        const std::string n = lowerFileName(exes[i].path);
        int rank = 4;  // a stock exe
        if (exes[i].recompiled) rank = n == "game_dx12_ship_replay_local_dw.exe" ? 0 : n == "game_dx12_ship_replay_dw.exe" ? 1 : 2;
        if (rank < bestRank) best = int(i), bestRank = rank;
    }
    return best;
}

bool healthIsDwHost(const std::string& body) {
    size_t p = body.find("\"server\"");
    if (p == std::string::npos) return false;
    p += 8;
    while (p < body.size() && isSpace(body[p])) ++p;
    if (p >= body.size() || body[p] != ':') return false;
    ++p;
    while (p < body.size() && isSpace(body[p])) ++p;
    return body.compare(p, 8, "\"iw8-dw\"") == 0;
}

std::string iniValue(const std::string& iniText, const std::string& key) {
    size_t p = 0;
    while (p < iniText.size()) {
        size_t end = iniText.find('\n', p);
        if (end == std::string::npos) end = iniText.size();
        std::string line = iniText.substr(p, end - p);
        p = end + 1;
        while (!line.empty() && isSpace(line.back())) line.pop_back();
        size_t b = 0;
        while (b < line.size() && isSpace(line[b])) ++b;
        const size_t eq = line.find('=', b);
        if (eq == std::string::npos) continue;
        std::string k = line.substr(b, eq - b);
        while (!k.empty() && isSpace(k.back())) k.pop_back();
        if (k != key) continue;
        size_t v = eq + 1;
        while (v < line.size() && isSpace(line[v])) ++v;
        return line.substr(v);
    }
    return {};
}

bool isLoopbackHost(const std::string& host) {
    std::string h = host;
    for (char& ch : h) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return h == "localhost" || h == "::1" || h == "[::1]" || h.compare(0, 4, "127.") == 0;
}

Decision decide(Phase phase, const GameSnapshot& s, const Context& c) {
    const State st = classify(s);'''),
(r'''        switch (st) {
            case State::FrontEndNoParty:
                if (c.stableMs < kFrontEndSettleMs || !s.commandBufferIdle) return wait(phase);
                return make(Action::HostLobby, Phase::WaitLobby, "Hosting a private match lobby (xstartprivatematch).");''',
 r'''        if (c.clientRequest > 0) {
            // The client read the start request and runs the start in process (DW_DEVMAP lines): send nothing, watch.
            if (st == State::InMatchHost && onMap(s, c.mapId)) return make(Action::Succeed, Phase::Done, "In game on " + c.mapId + ".");
            if (st == State::LobbyGuest || st == State::InMatchGuest)
                return fail("The game joined someone else's lobby; only the host can start a map.");
            return wait(phase);
        }
        switch (st) {
            case State::FrontEndNoParty:
                if (c.stableMs < (c.dwBuild && !c.signedIn ? kFrontEndUnsignedSettleMs : kFrontEndSettleMs) || !s.commandBufferIdle)
                    return wait(phase);
                return make(Action::HostLobby, Phase::WaitLobby, "Hosting a private match lobby (xstartprivatematch).");'''),
(r'''            case State::Booting:
                // Mode select (mode 0) on the DW client: pick Multiplayer for the owner (a launch with +mpMode 1
                // normally skips this screen; a launcher script cannot pass that argument).
                if (s.gameMode == 0 && c.dwBuild && c.stableMs >= kModeSelectSettleMs * (c.selectMpSent + 1) &&
                    c.selectMpSent < 3 && s.commandBufferIdle)
                    return make(Action::SelectMultiplayer, Phase::WaitBoot, "Selecting Multiplayer (dw_localTestMenuMP).");
                return wait(phase);''',
 r'''            case State::Booting: {
                // Mode select (mode 0) on the DW client: pick Multiplayer for the owner (the command line cannot:
                // this build drops "+mpMode 1"). The sign-in line of the client log is the signal; without it the
                // old fixed wait applies.
                const int64_t need = c.signedIn ? kModeSelectSignedInMs + kModeSelectRetryMs * c.selectMpSent
                                                : kModeSelectSettleMs * (c.selectMpSent + 1);
                if (s.gameMode == 0 && c.dwBuild && c.stableMs >= need && c.selectMpSent < 3 && s.commandBufferIdle)
                    return make(Action::SelectMultiplayer, Phase::WaitBoot, "Selecting Multiplayer (dw_localTestMenuMP).");
                return wait(phase);
            }'''),
(r'''        if (st == State::InMatchHost && onMap(s, c.mapId)) return make(Action::Succeed, Phase::Done, "In game on " + c.mapId + ".");
        if (c.elapsedMs > kLobbyTimeoutMs)''',
 r'''        if (st == State::InMatchHost && onMap(s, c.mapId)) return make(Action::Succeed, Phase::Done, "In game on " + c.mapId + ".");
        if (st == State::FrontEndNoParty && c.hostLobbySent < 2 && c.elapsedMs > kHostLobbyRetryMs && s.commandBufferIdle)
            return make(Action::HostLobby, Phase::WaitLobby, "No lobby after 10 s: asking once more (xstartprivatematch).");
        if (c.elapsedMs > kLobbyTimeoutMs)'''),
])
print('ok')
