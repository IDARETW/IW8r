#include "live_link.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

#include "json_lite.h"

namespace iw8r::live {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kRvaLimit = 0x1324B000u;  // proxy bound for action=read (developer_ui.cpp)
constexpr uint32_t kReadMaxBytes = 512;
constexpr size_t kMaxResultBytes = 64 * 1024;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimSpaces(const std::string& s) {
    const size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(' ');
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitSpaces(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool isPrintableAscii(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u >= 32 && u <= 126;
    });
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Parses a decimal field that must fit in uint32 (pid, id, length). Returns false on anything else.
bool parseU32(const std::string& s, uint32_t& out) {
    if (!allDigits(s) || s.size() > 10) return false;
    const unsigned long long v = std::stoull(s);
    if (v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

// Deny-list: engine commands that end, reload or re-route a match, press keys, write config or bypass the editor's
// verb allowlist. Compared against whole tokens, lower case, so `ui_mapname` is not `map`.
const std::set<std::string>& denyTokens() {
    static const std::set<std::string> tokens = {
        "map",        "devmap",      "disconnect",   "noclip",      "mw_noclip",     "mw_setpos",  "mw_setang",
        "mw_getpos",  "hotkey",      "quit",         "exit",        "map_restart",   "fast_restart",
        "map_refresh", "dw_localtestkey", "exec",    "writeconfig", "bind",          "unbind",     "seta",
        "setu",       "cmd",         "console",      "key",         "click",         "rclick",     "wheel",
        // Lobby and systemlink state the proxy owns (developer_ui.cpp, docs/research/25 §4), in readable and token form.
        "lpspmqsnpq", "nsqlttmrmp",  "molposlomo",   "ui_mapname",
    };
    return tokens;
}

bool isDvarName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool isDvarValue(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == '+' || c == '-';
    });
}

bool parseTriple(const std::string& s, Vec3& out) {
    double v[3] = {0, 0, 0};
    const char* p = s.c_str();
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            if (*p != ',') return false;
            ++p;
        }
        char* end = nullptr;
        v[i] = std::strtod(p, &end);
        if (end == p || !std::isfinite(v[i])) return false;
        p = end;
    }
    if (*p != '\0') return false;
    out = Vec3(v[0], v[1], v[2]);
    return true;
}

// Reads one CSV field starting at pos (quoted fields may hold doubled quotes). Advances pos past the trailing comma.
std::string readCsvField(const std::string& line, size_t& pos) {
    std::string field;
    if (pos < line.size() && line[pos] == '"') {
        ++pos;
        while (pos < line.size()) {
            if (line[pos] == '"') {
                if (pos + 1 < line.size() && line[pos + 1] == '"') {
                    field += '"';
                    pos += 2;
                    continue;
                }
                ++pos;
                break;
            }
            field += line[pos++];
        }
        while (pos < line.size() && line[pos] != ',') ++pos;  // anything after the closing quote
    } else {
        while (pos < line.size() && line[pos] != ',') field += line[pos++];
    }
    if (pos < line.size() && line[pos] == ',') ++pos;
    return field;
}

}  // namespace

const char* actionName(Action a) {
    switch (a) {
        case Action::Status: return "status";
        case Action::Command: return "command";
        case Action::Select: return "select";
        case Action::Player: return "player";
        case Action::Read: return "read";
    }
    return "status";
}

Verdict checkConsoleCommand(const std::string& text) {
    if (text.empty()) return {false, "The console line is empty."};
    if (text.size() > kMaxCommandChars) return {false, "The command is longer than 256 characters."};
    if (!isPrintableAscii(text)) return {false, "Only printable ASCII is allowed."};
    if (text.find(';') != std::string::npos) return {false, "';' chains are refused. Send one command at a time."};

    const std::vector<std::string> tokens = splitSpaces(text);
    if (tokens.empty()) return {false, "The console line is empty."};
    for (const std::string& t : tokens) {
        const std::string l = lower(t);
        if (denyTokens().count(l)) return {false, "'" + t + "' is not allowed from the editor."};
    }

    // Allowlist: exactly `set <dvar> <value>`.
    if (lower(tokens[0]) != "set" || tokens.size() != 3)
        return {false, "Only 'set <dvar> <value>' is allowed from the editor."};
    const std::string& name = tokens[1];
    if (!isDvarName(name)) return {false, "The dvar name must be letters, digits and '_', starting with a letter."};
    const std::string ln = lower(name);
    if (ln.rfind("com_", 0) == 0 || ln.rfind("fs_", 0) == 0 || ln.rfind("sv_", 0) == 0)
        return {false, "com_, fs_ and sv_ dvars are not settable from the editor."};
    if (!isDvarValue(tokens[2])) return {false, "The value must be one token of letters, digits, '_', '.', '+' or '-'."};
    return {true, {}};
}

Verdict checkMapId(const std::string& id) {
    // The proxy's IsMapId rule (custom_maps.cpp): mp_ prefix, 4 to 15 characters, [a-z0-9_].
    const bool shape = id.size() >= 4 && id.size() <= 15 && id.rfind("mp_", 0) == 0 &&
                       std::all_of(id.begin(), id.end(), [](char c) {
                           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                       });
    if (!shape) return {false, "A map id starts with mp_, has 4 to 15 characters, and uses only a-z, 0-9 and _."};
    return {true, {}};
}

Verdict checkReadValue(const std::string& value) {
    const std::vector<std::string> tokens = splitSpaces(value);
    if (tokens.size() != 2) return {false, "The read value is '<hex rva> <length>'."};
    const std::string& rva = tokens[0];
    if (rva.empty() || rva.size() > 8 ||
        !std::all_of(rva.begin(), rva.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
        return {false, "The RVA must be 1 to 8 hex digits."};
    const uint64_t addr = std::stoull(rva, nullptr, 16);
    if (addr >= kRvaLimit) return {false, "The RVA is outside the proxy's read range."};
    uint32_t len = 0;
    if (!parseU32(tokens[1], len) || len == 0 || len > kReadMaxBytes) return {false, "The length must be 1 to 512."};
    return {true, {}};
}

Verdict checkRequest(const Request& r) {
    if (r.pid == 0) return {false, "The proxy pid is unknown."};
    if (r.id == 0) return {false, "The request id must not be 0."};
    if (r.value.size() > kMaxValueChars) return {false, "The value is too long."};
    if (!isPrintableAscii(r.value)) return {false, "The value has characters outside printable ASCII."};
    switch (r.action) {
        case Action::Status:
        case Action::Player:
            if (!r.value.empty()) return {false, std::string(actionName(r.action)) + " takes no value."};
            return {true, {}};
        case Action::Command: return checkConsoleCommand(r.value);
        case Action::Select: return checkMapId(r.value);
        case Action::Read: return checkReadValue(r.value);
    }
    return {false, "Unknown action."};
}

std::string buildControlIni(const Request& r) {
    if (!checkRequest(r).ok) return {};
    std::ostringstream out;
    out << "[request]\r\n"
        << "pid=" << r.pid << "\r\n"
        << "id=" << r.id << "\r\n"
        << "action=" << actionName(r.action) << "\r\n"
        << "value=" << r.value << "\r\n";
    return out.str();
}

bool writeControlFile(const fs::path& gameDir, const Request& r, std::string* err) {
    const Verdict v = checkRequest(r);
    if (!v.ok) {
        if (err) *err = v.reason;
        return false;
    }
    std::error_code ec;
    const fs::path proxyDir = gameDir / ".proxy";
    if (!fs::is_directory(proxyDir, ec)) {
        if (err) *err = "The game folder has no .proxy directory, so the proxy has not run there.";
        return false;
    }
    const std::string text = buildControlIni(r);
    const fs::path tmp = proxyDir / "control.ini.tmp";
    const fs::path dst = proxyDir / "control.ini";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "Could not create control.ini.tmp.";
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        if (!f) {
            fs::remove(tmp, ec);
            if (err) *err = "Could not write control.ini.tmp.";
            return false;
        }
    }
    // The proxy reads control.ini with the Win32 profile API, which can hold the file for a moment. Retry the replace.
    for (int attempt = 0; attempt < 10; ++attempt) {
        fs::rename(tmp, dst, ec);
        if (!ec) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    fs::remove(tmp, ec);
    if (err) *err = "Could not replace control.ini (another program holds it).";
    return false;
}

bool parseResult(const std::string& json, Result& out, std::string* err) {
    std::string perr;
    const std::optional<json::Value> root = json::parse(json, &perr);
    if (!root || root->type != json::Value::Type::Object) {
        if (err) *err = "control-result.json is not a JSON object" + (perr.empty() ? std::string() : ": " + perr);
        return false;
    }
    Result r;
    const json::Value* pid = root->get("pid");
    const json::Value* id = root->get("id");
    if (!pid || !id || !pid->number() || !id->number()) {
        if (err) *err = "control-result.json has no pid or id";
        return false;
    }
    const double dp = *pid->number();
    const double di = *id->number();
    if (dp < 0 || dp > 4294967295.0 || di < 0 || di > 4294967295.0 || dp != std::floor(dp) || di != std::floor(di)) {
        if (err) *err = "control-result.json has an invalid pid or id";
        return false;
    }
    r.pid = static_cast<uint32_t>(dp);
    r.id = static_cast<uint32_t>(di);
    auto flag = [&](const char* key, bool& dst) {
        const json::Value* v = root->get(key);
        if (v && v->type == json::Value::Type::Bool) dst = v->b;
    };
    auto text = [&](const char* key, std::string& dst) {
        const json::Value* v = root->get(key);
        if (v && v->string()) dst = *v->string();
    };
    flag("ok", r.ok);
    flag("frontend", r.frontend);
    flag("console", r.console);
    flag("browser", r.browser);
    text("map", r.map);
    text("selected", r.selected);
    text("data", r.data);
    out = std::move(r);
    return true;
}

bool readResultFile(const fs::path& gameDir, Result& out, std::string* err) {
    const fs::path p = gameDir / ".proxy" / "control-result.json";
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size == 0 || size > kMaxResultBytes) {
        if (err) *err = "No control-result.json yet.";
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "Could not open control-result.json.";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseResult(text, out, err);
}

bool parsePlayerData(const std::string& data, PlayerState& out) {
    PlayerState s;
    bool haveOrigin = false;
    bool haveAngles = false;
    for (const std::string& token : splitSpaces(data)) {
        const size_t eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = token.substr(0, eq);
        const std::string val = token.substr(eq + 1);
        if (key == "player_origin") haveOrigin = parseTriple(val, s.origin);
        else if (key == "view_angles") haveAngles = parseTriple(val, s.angles);
    }
    s.valid = haveOrigin && haveAngles;
    out = s;
    return s.valid;
}

std::vector<uint32_t> parseTasklistPids(const std::string& csv, const std::string& exeName) {
    std::vector<uint32_t> pids;
    const std::string want = lower(exeName);
    std::istringstream in(csv);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] != '"') continue;  // "INFO: No tasks are running..." and blank lines
        size_t pos = 0;
        const std::string image = readCsvField(line, pos);
        const std::string pidText = readCsvField(line, pos);
        uint32_t pid = 0;
        if (lower(image) == want && parseU32(pidText, pid) && pid != 0) {
            if (std::find(pids.begin(), pids.end(), pid) == pids.end()) pids.push_back(pid);
        }
    }
    return pids;
}

// ---- Link ------------------------------------------------------------------------------------------------------------

Link::Link(fs::path gameDir) : gameDir_(std::move(gameDir)) {}

void Link::setGameDir(const fs::path& gameDir) { gameDir_ = gameDir; }

void Link::setPid(uint32_t pid) {
    pid_ = pid;
    if (pid == 0) return;
    // The proxy ignores an id it has already processed. Continue after the last answer for this pid, so a restarted
    // editor never resends an id the running proxy has seen.
    Result r;
    if (readResultFile(gameDir_, r) && r.pid == pid && r.id >= nextId_) {
        const uint64_t next = static_cast<uint64_t>(r.id) + 1;
        nextId_ = next > 0xFFFFFFFFull ? 1u : static_cast<uint32_t>(next);
    }
}

bool Link::send(Action a, const std::string& value, int64_t nowMs, std::string* err) {
    if (pending_) {
        if (err) *err = "The previous request is still waiting for the proxy.";
        return false;
    }
    if (pid_ == 0) {
        if (err) *err = "Replay is not running, so there is no proxy pid.";
        return false;
    }
    if (gameDir_.empty()) {
        if (err) *err = "No game folder is set.";
        return false;
    }
    Request r;
    r.pid = pid_;
    r.id = nextId_;
    r.action = a;
    r.value = trimSpaces(value);
    const Verdict v = checkRequest(r);
    if (!v.ok) {
        if (err) *err = v.reason;
        return false;
    }
    if (!writeControlFile(gameDir_, r, err)) return false;
    lastId_ = r.id;
    nextId_ = r.id == 0xFFFFFFFFu ? 1u : r.id + 1;
    pending_ = true;
    pendingId_ = r.id;
    pendingAction_ = a;
    deadlineMs_ = nowMs + kTimeoutMs;
    return true;
}

Link::Event Link::poll(int64_t nowMs, Result& out) {
    if (!pending_) return Event::None;
    Result r;
    if (readResultFile(gameDir_, r) && r.pid == pid_ && r.id == pendingId_) {
        pending_ = false;
        out = std::move(r);
        return Event::Answered;
    }
    if (nowMs >= deadlineMs_) {
        pending_ = false;
        return Event::TimedOut;
    }
    return Event::None;
}

void Link::cancel() { pending_ = false; }

}  // namespace iw8r::live
