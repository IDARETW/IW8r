// IW8 Radiant - live link, phase A: the editor side of the Replay proxy control file (docs/research/25_live_link.md).
//
// The proxy (mw120rproxy, inside Replay 1.20) polls <game>\.proxy\control.ini, section [request] with pid, id, action and
// value, and answers in <game>\.proxy\control-result.json. There is no socket. This module writes requests, reads and
// parses results, and validates every request before it is written:
//   - actions: status, command, select, player, read (nothing else is ever written);
//   - command text: printable ASCII, no ';' chains, a deny-list of engine commands that end, reload or re-route a match,
//     and an allowlist of verbs (only `set <dvar> <value>` in phase A);
//   - select ids: the proxy's own map id rule (mp_ prefix, 4 to 15 characters, [a-z0-9_]).
//
// No Qt, no sockets, no process control. Nothing here starts or stops the game, and nothing writes outside
// <game>\.proxy\control.ini. Tests use a scratch game folder.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r::live {

// Proxy requests are answered within one overlay frame in the lobby; 5 s covers a slow frame and a late poll.
inline constexpr int64_t kTimeoutMs = 5000;
// Proxy limit for the request value (docs/research/25 §1.2).
inline constexpr size_t kMaxValueChars = 4095;
// Editor limit for console text (the proxy accepts more; the editor keeps commands short and reviewable).
inline constexpr size_t kMaxCommandChars = 256;

enum class Action { Status, Command, Select, Player, Read };

const char* actionName(Action a);  // "status", "command", "select", "player", "read"

struct Request {
    uint32_t pid = 0;  // proxy GetCurrentProcessId(); requests with another pid are ignored by the proxy
    uint32_t id = 0;   // nonzero, different from the last id the proxy processed
    Action action = Action::Status;
    std::string value;
};

struct Verdict {
    bool ok = false;
    std::string reason;  // empty when ok; a short user-facing sentence otherwise
};

// Console text for action=command. Refuses control characters and non-ASCII, ';', any token on the deny-list
// (map, devmap, disconnect, noclip, hotkey, quit, exec, bind, cmd, ...), set on protected names (com_, fs_, sv_, the
// lobby and systemlink tokens), and anything outside the allowlist. Phase A allowlist: `set <dvar> <value>`, with a
// dvar name of [A-Za-z][A-Za-z0-9_]* and one value token of [A-Za-z0-9_.+-].
Verdict checkConsoleCommand(const std::string& text);

// Map id for action=select: the proxy's IsMapId rule (mp_ prefix, 4 to 15 characters, [a-z0-9_]).
Verdict checkMapId(const std::string& id);

// Read value "<hex rva> <decimal length>": rva below 0x1324B000, length 1 to 512 (the proxy's own bounds).
Verdict checkReadValue(const std::string& value);

// Whole-request check: pid and id nonzero, value bounds and printable ASCII, and the action's own value rule.
Verdict checkRequest(const Request& r);

// control.ini text with CRLF line endings. Empty when checkRequest fails.
std::string buildControlIni(const Request& r);

// Writes <gameDir>\.proxy\control.ini atomically: a control.ini.tmp in the same folder, then a replacing rename. Refuses
// (false, err) when the request is invalid or the folder has no .proxy directory (the proxy has never run there).
bool writeControlFile(const std::filesystem::path& gameDir, const Request& r, std::string* err = nullptr);

// One answer from control-result.json (docs/research/25 §1.4). `data` is the proxy's text payload.
struct Result {
    uint32_t pid = 0;
    uint32_t id = 0;
    bool ok = false;        // for command and select, "queued", not "ran"
    bool frontend = false;  // true in the lobby and menus
    std::string map;        // ui_mapname, empty when unset
    std::string selected;   // active custom package id, empty when none
    bool console = false;
    bool browser = false;
    std::string data;
};

// Parses control-result.json text. pid and id are required.
bool parseResult(const std::string& json, Result& out, std::string* err = nullptr);
// Reads <gameDir>\.proxy\control-result.json. A missing or half-written file is an error (the caller keeps waiting).
bool readResultFile(const std::filesystem::path& gameDir, Result& out, std::string* err = nullptr);

// Player camera from a `player` result. data is "player_origin=x,y,z entity_origin=... view_angles=p,y,r entity_angles=...".
// Only player_origin and view_angles are used: origin in game units, angles pitch yaw roll in degrees.
struct PlayerState {
    bool valid = false;
    Vec3 origin;
    Vec3 angles;
};
bool parsePlayerData(const std::string& data, PlayerState& out);

// Process ids of every row of `tasklist /fo csv /nh` whose image name equals exeName (case-insensitive). Rows that are
// not CSV (for example "INFO: No tasks are running") are skipped.
std::vector<uint32_t> parseTasklistPids(const std::string& csv, const std::string& exeName);

// Stateful link: pid, id counter, one request in flight, timeout. Times are caller-supplied milliseconds
// (a steady clock in the app, fixed values in tests).
class Link {
public:
    enum class Event { None, Answered, TimedOut };

    explicit Link(std::filesystem::path gameDir = {});

    void setGameDir(const std::filesystem::path& gameDir);
    const std::filesystem::path& gameDir() const { return gameDir_; }

    // Sets the proxy pid (0 = unknown). A pid change also reads the last result of that pid so ids continue from it.
    void setPid(uint32_t pid);
    uint32_t pid() const { return pid_; }

    bool pending() const { return pending_; }
    uint32_t pendingId() const { return pendingId_; }
    Action pendingAction() const { return pendingAction_; }
    uint32_t lastId() const { return lastId_; }

    // Writes a new request with the next id. Refused (false, err) when a request is pending, the pid is unknown, the game
    // folder is unset or the request is invalid. Command and select values are trimmed of spaces first.
    bool send(Action a, const std::string& value, int64_t nowMs, std::string* err = nullptr);

    // Checks for the answer to the pending request. Answered: out holds the matching result and pending clears. TimedOut:
    // no matching answer within kTimeoutMs; pending clears and the caller must not resend the same command.
    Event poll(int64_t nowMs, Result& out);

    // Forgets the pending request without a result (the caller decided it is stale).
    void cancel();

private:
    std::filesystem::path gameDir_;
    uint32_t pid_ = 0;
    uint32_t lastId_ = 0;  // last id written
    uint32_t nextId_ = 1;  // never 0
    bool pending_ = false;
    uint32_t pendingId_ = 0;
    Action pendingAction_ = Action::Status;
    int64_t deadlineMs_ = 0;
};

}  // namespace iw8r::live
