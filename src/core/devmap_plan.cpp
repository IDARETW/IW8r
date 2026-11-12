#include "devmap_plan.h"
#include "game_modes.h"

#include <algorithm>
#include <cctype>

namespace iw8r::devmap {

namespace {

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

Decision make(Action a, Phase next, std::string message) {
    Decision d;
    d.action = a;
    d.next = next;
    d.message = std::move(message);
    return d;
}

Decision wait(Phase phase) { return make(Action::None, phase, {}); }
Decision fail(std::string message) { return make(Action::Fail, Phase::Failed, std::move(message)); }

bool onMap(const GameSnapshot& s, const std::string& id) {
    std::string a = s.uiMapname, b = id;
    for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : b) c = char(std::tolower(static_cast<unsigned char>(c)));
    return !b.empty() && a == b;
}

// A note added to start messages when the DW client did not list the package at its last scan.
std::string newPackageNote(const Context& c) {
    if (!c.dwBuild || contains(c.dwInstalled, c.mapId)) return {};
    return " (" + c.mapId + " was not in the DW client's package list at its last scan: if the game reports a missing "
           "map, restart the game once so it scans the new package)";
}

Decision startFromLobby(const Context& c, bool deploy) {
    return make(deploy ? Action::DeployThenStart : Action::StartMatch, Phase::WaitMatch,
                "Starting " + c.mapId + " (" + c.gametype + ") from the lobby" + newPackageNote(c));
}

}  // namespace

State classify(const GameSnapshot& s) {
    if (!s.valid) return State::Unknown;
    if (s.inMatch) return s.serverRunning ? State::InMatchHost : State::InMatchGuest;
    if (s.gameMode < 0 || s.gameMode > 3) return State::Unknown;
    if (s.gameMode == 0 || !s.frontEnd()) return State::Booting;
    if (s.gameMode != 2) return State::NotMultiplayer;
    if (!s.inParty) return State::FrontEndNoParty;
    return s.partyHost ? State::LobbyHost : State::LobbyGuest;
}

const char* stateName(State s) {
    switch (s) {
        case State::Unknown: return "unknown";
        case State::Booting: return "starting up";
        case State::NotMultiplayer: return "not in Multiplayer";
        case State::FrontEndNoParty: return "Multiplayer menu";
        case State::LobbyHost: return "lobby (host)";
        case State::LobbyGuest: return "lobby (guest)";
        case State::InMatchHost: return "in a match (host)";
        case State::InMatchGuest: return "in a match (guest)";
    }
    return "unknown";
}

std::string setMapCommand(const std::string& mapId) { return "set NSQLTTMRMP " + mapId; }
std::string setGametypeCommand(const std::string& gametype) { return "set MOLPOSLOMO " + gametype; }

std::vector<std::string> hostLobbyCommands() { return {"set LSTLQTSSRM 1", "xstartprivatematch"}; }

std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s) {
    std::vector<std::string> out = {setMapCommand(mapId), setGametypeCommand(gametype.empty() ? "war" : gametype)};
    // SV_SystemLinkStartMatch_f requires systemlink_host; CL_MainMP_PartyGo_f requires xblive_privatematch.
    if (s.systemlink && s.systemlinkHost && !s.privateMatch) {
        out.push_back("set NMQNNPQPRS 0");
        out.push_back("set LTLOQSNSOT 0");
        out.push_back("sv_systemlinkStartMatch");
    } else {
        out.push_back("xpartygo 1");
    }
    return out;
}

std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s,
                                            const std::vector<std::pair<std::string, std::string>>& modeDvars) {
    std::vector<std::string> out;
    for (const auto& [name, value] : modeDvars)
        if (validModeDvarName(name) && validModeDvarValue(value)) out.push_back("set " + name + " " + value);
    for (std::string& line : startMatchCommands(mapId, gametype, s)) out.push_back(std::move(line));
    return out;
}

std::string backToLobbyCommand() { return "xpartybackout"; }
std::string reloadCommand() { return "map_reload"; }
std::string selectMultiplayerCommand() { return "dw_localTestMenuMP"; }

bool joinCommands(const std::vector<std::string>& commands, std::string& out, std::string* err) {
    out.clear();
    for (const std::string& c : commands) {
        if (c.empty()) continue;
        for (char ch : c)
            if (ch == ';' || ch == '"' || static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126) {
                if (err) *err = "refused command text: " + c;
                out.clear();
                return false;
            }
        if (!out.empty()) out += '\n';
        out += c;
    }
    if (out.empty()) {
        if (err) *err = "no command";
        return false;
    }
    return true;
}

std::vector<std::string> parseDwInstalledMaps(const std::string& logText, bool* found) {
    static const std::string key = "DW_CUSTOM_MAPS installed:";
    const size_t at = logText.rfind(key);
    if (found) *found = at != std::string::npos;
    std::vector<std::string> out;
    if (at == std::string::npos) return out;
    size_t p = at + key.size();
    const size_t end = logText.find_first_of("\r\n", p);
    const std::string line = logText.substr(p, end == std::string::npos ? std::string::npos : end - p);
    std::string word;
    for (char ch : line + " ") {
        if (ch == ' ' || ch == '\t') {
            if (!word.empty()) out.push_back(word);
            word.clear();
        } else {
            word += ch;
        }
    }
    return out;
}

int parseDwReloadResult(const std::string& logTail, const std::string& mapId) {
    int result = 0;
    size_t p = 0;
    while ((p = logTail.find("map_reload: ", p)) != std::string::npos) {
        const size_t end = logTail.find_first_of("\r\n", p);
        const std::string line = logTail.substr(p, end == std::string::npos ? std::string::npos : end - p);
        p += 12;
        if (line.find("level zones reloaded from disk") != std::string::npos && line.find(mapId) != std::string::npos)
            result = 1;
        else if (line.find("no zone was unloaded") != std::string::npos || line.find("not an installed custom map") != std::string::npos ||
                 line.find("already in progress") != std::string::npos || line.find("restart the game") != std::string::npos ||
                 line.find("did not finish starting") != std::string::npos)
            result = -1;
    }
    return result;
}

std::vector<std::string> splitLaunchArgs(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, any = false;
    for (char ch : text) {
        if (ch == '"') {
            quoted = !quoted;
            any = true;
        } else if (!quoted && (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
            if (any) out.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur += ch;
            any = true;
        }
    }
    if (any) out.push_back(cur);
    return out;
}

namespace {

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
        if (n.size() > 12 && n.compare(n.size() - 12, 12, ".updater.exe") == 0) rank = 9;  // the client's updater, not a game
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

bool coverWanted(bool launchedByEditor, int64_t sinceStartRequestMs, const GameSnapshot& s) {
    if (!launchedByEditor) return false;  // a game that was already on screen is not hidden
    if (s.valid && s.inMatch) return false;
    if (sinceStartRequestMs < 0) return true;
    return s.valid && s.frontEnd() && sinceStartRequestMs < kCoverMaxAfterStartMs;
}

bool clientLineIsStart(const std::string& line) { return line.compare(0, 5, "start") == 0 || line.compare(0, 7, "loading") == 0; }

Decision decide(Phase phase, const GameSnapshot& s, const Context& c) {
    const State st = classify(s);
    if (phase == Phase::Done || phase == Phase::Failed) return wait(phase);
    if (c.processCount > 1) {
        // A launcher or updater can run beside the game for a moment while it starts: wait, but not forever.
        if (phase != Phase::Start && c.elapsedMs < kLobbyTimeoutMs) return wait(phase);
        return fail("More than one exe is running from the game folder; close the extra one and press F5 again.");
    }

    if (phase == Phase::Start) {
        if (c.processCount == 0)
            return make(Action::Launch, Phase::WaitBoot, "Replay is not running: deploying " + c.mapId + " and starting the game.");
        switch (st) {
            case State::Unknown:
                return fail("Cannot read the running game's state (not a Replay 1.20 exe, or no access). Deploy with Ctrl+F7 and load the map by hand.");
            case State::Booting:
                return make(Action::Deploy, Phase::WaitBoot, "The game is starting up: deploying " + c.mapId + " and waiting for the Multiplayer menu.");
            case State::NotMultiplayer:
                return make(Action::Deploy, Phase::Failed,
                            "Deployed " + c.mapId + ". The game is not in Multiplayer: pick Multiplayer in the game, then press F5 again.");
            case State::FrontEndNoParty:
                return make(Action::Deploy, Phase::WaitBoot, "Deploying " + c.mapId + "; then hosting a private match.");
            case State::LobbyHost:
                return startFromLobby(c, true);
            case State::LobbyGuest:
                return make(Action::Deploy, Phase::Failed,
                            "Deployed " + c.mapId + ". You are in someone else's lobby: only the host can start a map. Leave it and press F5 again.");
            case State::InMatchHost:
                if (onMap(s, c.mapId) && c.dwBuild && c.reloadAllowed && contains(c.dwInstalled, c.mapId))
                    return make(Action::DeployThenReload, Phase::WaitReload,
                                c.mapId + " is running: deploying the rebuild and reloading its zones (map_reload).");
                return make(Action::BackToLobby, Phase::WaitFrontEnd,
                            "Ending the running match (" + (s.uiMapname.empty() ? std::string("?") : s.uiMapname) +
                                ") so its zones unload; then deploying and starting " + c.mapId + ".");
            case State::InMatchGuest:
                return make(Action::Deploy, Phase::Failed,
                            "Deployed " + c.mapId + ". You are playing on someone else's server: only the host can change the map.");
        }
        return fail("Unexpected game state.");
    }

    if (phase == Phase::WaitBoot) {
        if (c.processCount == 0 && c.elapsedMs > 30000) return fail("The game is not running any more (it exited during start-up).");
        if (c.elapsedMs > kBootTimeoutMs)
            return fail(std::string("The game did not reach the Multiplayer menu within 5 minutes (state: ") + stateName(st) +
                        "). Open Multiplayer in the game and press F5 again.");
        if (c.clientRequest > 0) {
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
                return make(Action::HostLobby, Phase::WaitLobby, "Hosting a private match lobby (xstartprivatematch).");
            case State::LobbyHost:
                if (c.stableMs < kLobbySettleMs || !s.commandBufferIdle) return wait(phase);
                return startFromLobby(c, false);
            case State::InMatchHost:
                if (onMap(s, c.mapId)) return make(Action::Succeed, Phase::Done, "In game on " + c.mapId + ".");
                return make(Action::BackToLobby, Phase::WaitFrontEnd, "A match on " + s.uiMapname + " is running: ending it.");
            case State::LobbyGuest:
            case State::InMatchGuest:
                return fail("The game joined someone else's lobby; only the host can start a map.");
            case State::Booting: {
                // Mode select (mode 0) on the DW client: pick Multiplayer for the owner (the command line cannot:
                // this build drops "+mpMode 1"). The sign-in line of the client log is the signal; without it the
                // old fixed wait applies.
                const int64_t need = c.signedIn ? kModeSelectSignedInMs + kModeSelectRetryMs * c.selectMpSent
                                                : kModeSelectSettleMs * (c.selectMpSent + 1);
                if (s.gameMode == 0 && c.dwBuild && c.stableMs >= need && c.selectMpSent < 3 && s.commandBufferIdle)
                    return make(Action::SelectMultiplayer, Phase::WaitBoot, "Selecting Multiplayer (dw_localTestMenuMP).");
                return wait(phase);
            }
            default:
                return wait(phase);  // unreadable while loading, SP / CP
        }
    }

    if (phase == Phase::WaitLobby) {
        if (st == State::LobbyHost) {
            if (c.stableMs < kLobbySettleMs || !s.commandBufferIdle) return wait(phase);
            return startFromLobby(c, false);
        }
        if (st == State::InMatchHost && onMap(s, c.mapId)) return make(Action::Succeed, Phase::Done, "In game on " + c.mapId + ".");
        if (st == State::FrontEndNoParty && c.hostLobbySent < 2 && c.elapsedMs > kHostLobbyRetryMs && s.commandBufferIdle)
            return make(Action::HostLobby, Phase::WaitLobby, "No lobby after 10 s: asking once more (xstartprivatematch).");
        if (c.elapsedMs > kLobbyTimeoutMs)
            return fail(std::string("No private match lobby after 30 s (state: ") + stateName(st) +
                        "). Open Private Match (or Local Play > Create game) in the game and press F5 again: the start step works from any lobby you host.");
        return wait(phase);
    }

    if (phase == Phase::WaitFrontEnd) {
        if (st == State::LobbyHost && !s.serverRunning) {
            if (c.stableMs < kLobbySettleMs || !s.commandBufferIdle) return wait(phase);
            return startFromLobby(c, true);
        }
        if (st == State::FrontEndNoParty && c.stableMs >= kLobbySettleMs)
            return make(Action::Deploy, Phase::WaitBoot, "Back in the menu without a lobby: deploying, then hosting a private match.");
        if (c.elapsedMs > kFrontEndTimeoutMs)
            return fail(std::string("The match did not end within 60 s (state: ") + stateName(st) +
                        "). Leave the match in the game and press F5 again.");
        return wait(phase);
    }

    if (phase == Phase::WaitMatch) {
        if ((st == State::InMatchHost || st == State::InMatchGuest) && onMap(s, c.mapId))
            return make(Action::Succeed, Phase::Done, "In game on " + c.mapId + ".");
        if (st == State::LobbyHost && c.elapsedMs > 20000 && !s.serverRunning)
            return fail("The lobby did not start " + c.mapId + " (the start request was refused, or the map is missing). "
                        "Check the game console; Start Match in the lobby uses the same map setting.");
        if (c.elapsedMs > kMatchTimeoutMs) return fail(std::string("No match on ") + c.mapId + " after 5 minutes (state: " + stateName(st) + ").");
        return wait(phase);
    }

    if (phase == Phase::WaitReload) {
        if (c.dwReload > 0 && st == State::InMatchHost) return make(Action::Succeed, Phase::Done, "Reloaded " + c.mapId + " from disk; in game.");
        if (c.dwReload < 0)
            return make(Action::BackToLobby, Phase::WaitFrontEnd, "map_reload was refused: ending the match and starting " + c.mapId + " again.");
        if (c.elapsedMs > kReloadTimeoutMs) return fail("map_reload did not report within 3 minutes; check replay-dw-client.log.");
        return wait(phase);
    }
    return wait(phase);
}

}  // namespace iw8r::devmap
