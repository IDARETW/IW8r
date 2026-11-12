// Live link phase A (docs/research/25_live_link.md): request file format, atomic write, id sequence, allow and deny lists,
// result parsing, player readback, tasklist pid parsing and the timeout. Every file lives in a scratch game folder under
// the test temp directory. Nothing touches E:\IW8\Builds\1.20-replay.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "live_link.h"
#include "test_harness.h"

using namespace iw8r;
using namespace iw8r::live;
namespace fs = std::filesystem;

namespace {

// <tmp>/live_link/<name>/ with a .proxy folder, recreated empty for each test.
fs::path fakeGame(const std::string& name) {
    const fs::path root = fs::path(test::tmpDir()) / "live_link" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / ".proxy");
    return root;
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void writeResult(const fs::path& game, const std::string& json) {
    std::ofstream f(game / ".proxy" / "control-result.json", std::ios::binary | std::ios::trunc);
    f << json;
}

std::string resultJson(uint32_t pid, uint32_t id, const std::string& map = "", bool frontend = true) {
    return "{\"pid\":" + std::to_string(pid) + ",\"id\":" + std::to_string(id) +
           ",\"ok\":true,\"frontend\":" + (frontend ? "true" : "false") + ",\"map\":\"" + map +
           "\",\"selected\":\"\",\"console\":false,\"browser\":false,\"data\":\"\"}";
}

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

}  // namespace

TEST(live_link_request_format) {
    Request r{1234, 7, Action::Status, ""};
    CHECK(buildControlIni(r) == "[request]\r\npid=1234\r\nid=7\r\naction=status\r\nvalue=\r\n");
    Request c{1234, 8, Action::Command, "set r_fullbright 1"};
    CHECK(buildControlIni(c) == "[request]\r\npid=1234\r\nid=8\r\naction=command\r\nvalue=set r_fullbright 1\r\n");
    Request s{1234, 9, Action::Select, "mp_dm_test"};
    CHECK(buildControlIni(s) == "[request]\r\npid=1234\r\nid=9\r\naction=select\r\nvalue=mp_dm_test\r\n");
    // An invalid request produces no text at all.
    CHECK(buildControlIni(Request{0, 1, Action::Status, ""}).empty());
    CHECK(buildControlIni(Request{1, 0, Action::Status, ""}).empty());
    CHECK(buildControlIni(Request{1, 1, Action::Status, "x"}).empty());
}

TEST(live_link_write_is_atomic_and_replaces) {
    const fs::path game = fakeGame("write");
    std::string err;
    CHECK(writeControlFile(game, Request{42, 1, Action::Status, ""}, &err));
    const fs::path ini = game / ".proxy" / "control.ini";
    CHECK(fs::exists(ini));
    CHECK(!fs::exists(game / ".proxy" / "control.ini.tmp"));
    CHECK(readAll(ini) == "[request]\r\npid=42\r\nid=1\r\naction=status\r\nvalue=\r\n");

    CHECK(writeControlFile(game, Request{42, 2, Action::Command, "set r_x 0"}, &err));
    CHECK(readAll(ini) == "[request]\r\npid=42\r\nid=2\r\naction=command\r\nvalue=set r_x 0\r\n");
    CHECK(!fs::exists(game / ".proxy" / "control.ini.tmp"));
}

TEST(live_link_write_refuses_without_proxy_folder) {
    const fs::path game = fs::path(test::tmpDir()) / "live_link" / "no_proxy_dir";
    std::error_code ec;
    fs::remove_all(game, ec);
    fs::create_directories(game);
    std::string err;
    CHECK(!writeControlFile(game, Request{42, 1, Action::Status, ""}, &err));
    CHECK(contains(err, ".proxy"));
    CHECK(!fs::exists(game / ".proxy" / "control.ini"));
}

TEST(live_link_write_refuses_invalid_request) {
    const fs::path game = fakeGame("invalid");
    std::string err;
    CHECK(!writeControlFile(game, Request{42, 1, Action::Command, "map mp_x"}, &err));
    CHECK(!fs::exists(game / ".proxy" / "control.ini"));
    CHECK(!fs::exists(game / ".proxy" / "control.ini.tmp"));
}

TEST(live_link_id_increments_per_send) {
    const fs::path game = fakeGame("ids");
    Link link(game);
    link.setPid(1234);
    std::string err;
    CHECK(link.send(Action::Status, "", 0, &err));
    CHECK(link.pendingId() == 1);
    CHECK(contains(readAll(game / ".proxy" / "control.ini"), "id=1\r\n"));

    writeResult(game, resultJson(1234, 1));
    Result r;
    CHECK(link.poll(10, r) == Link::Event::Answered);
    CHECK(r.id == 1 && r.frontend);

    CHECK(link.send(Action::Status, "", 20, &err));
    CHECK(link.pendingId() == 2);
    CHECK(contains(readAll(game / ".proxy" / "control.ini"), "id=2\r\n"));
    writeResult(game, resultJson(1234, 2));
    CHECK(link.poll(30, r) == Link::Event::Answered);

    CHECK(link.send(Action::Command, "set r_fullbright 1", 40, &err));
    CHECK(link.pendingId() == 3);
    CHECK(link.lastId() == 3);
}

TEST(live_link_id_continues_from_last_result) {
    const fs::path game = fakeGame("resume");
    writeResult(game, resultJson(1234, 41));
    Link link(game);
    link.setPid(1234);
    std::string err;
    CHECK(link.send(Action::Status, "", 0, &err));
    CHECK(link.pendingId() == 42);  // the proxy would ignore an id it has already processed

    // A result from another process does not move the counter.
    const fs::path other = fakeGame("resume_other_pid");
    writeResult(other, resultJson(999, 500));
    Link link2(other);
    link2.setPid(1234);
    CHECK(link2.send(Action::Status, "", 0, &err));
    CHECK(link2.pendingId() == 1);
}

TEST(live_link_refuses_without_pid_folder_or_while_pending) {
    const fs::path game = fakeGame("refuse");
    Link noPid(game);
    std::string err;
    CHECK(!noPid.send(Action::Status, "", 0, &err));
    CHECK(contains(err, "not running"));
    CHECK(!fs::exists(game / ".proxy" / "control.ini"));

    Link link(game);
    link.setPid(77);
    CHECK(link.send(Action::Status, "", 0, &err));
    CHECK(!link.send(Action::Command, "set r_x 1", 1, &err));  // one request in flight
    CHECK(contains(err, "still waiting"));
    CHECK(link.pendingAction() == Action::Status);
}

TEST(live_link_timeout_after_5_seconds) {
    const fs::path game = fakeGame("timeout");
    Link link(game);
    link.setPid(55);
    std::string err;
    CHECK(link.send(Action::Command, "set r_x 1", 1000, &err));
    Result r;
    CHECK(link.poll(1000 + kTimeoutMs - 1, r) == Link::Event::None);
    CHECK(link.pending());
    CHECK(link.poll(1000 + kTimeoutMs, r) == Link::Event::TimedOut);
    CHECK(!link.pending());
    // The next request is a new id, never a retry of the timed out one.
    CHECK(link.send(Action::Status, "", 9000, &err));
    CHECK(link.pendingId() == 2);
}

TEST(live_link_ignores_result_for_other_id_or_pid) {
    const fs::path game = fakeGame("mismatch");
    Link link(game);
    link.setPid(55);
    std::string err;
    CHECK(link.send(Action::Status, "", 0, &err));
    Result r;
    writeResult(game, resultJson(55, 99));  // wrong id
    CHECK(link.poll(1, r) == Link::Event::None);
    writeResult(game, resultJson(56, 1));  // wrong pid (game restarted)
    CHECK(link.poll(2, r) == Link::Event::None);
    writeResult(game, "{\"pid\":55,\"id\":1,\"ok\":true");  // half written: keep waiting
    CHECK(link.poll(3, r) == Link::Event::None);
    writeResult(game, resultJson(55, 1, "mp_dm_test", false));
    CHECK(link.poll(4, r) == Link::Event::Answered);
    CHECK(r.map == "mp_dm_test" && !r.frontend);
}

TEST(live_link_deny_list_refuses_engine_commands) {
    const char* refused[] = {
        "map mp_dm_test",  "MAP mp_dm_test",  "devmap mp_dm_test",  "disconnect",        "noclip",
        "mw_noclip 1",     "cmd mw_setpos 1 2 3", "hotkey f6",        "hotkey enter",      "quit",
        "exit",            "map_restart",     "fast_restart",       "map_refresh",       "dw_localTestKey 1",
        "exec autoexec",   "writeconfig x",   "bind a b",           "unbind a",          "seta r_x 1",
        "setu r_x 1",      "set r_x 1; quit", "set r_x 1;set r_y 2", ";quit",             "set LPSPMQSNPQ 1",
        "set NSQLTTMRMP mp_x", "set MOLPOSLOMO war", "set ui_mapname mp_x", "set com_x 1", "set fs_game x",
        "set sv_cheats 1",  "console open",    "key w",              "click 1 2",         "say hello",
        "set r_x",          "set r_x 1 2",     "set r_x \"a b\"",    "set r_x a\tb",      "",
        "   ",              "set r_x\n1",      "set r_\xC3\xA9 1",   "set 1bad 1",
    };
    for (const char* text : refused) {
        const Verdict v = checkConsoleCommand(text);
        if (v.ok) std::printf("  refused text was allowed: [%s]\n", text);
        CHECK(!v.ok);
        CHECK(!v.reason.empty());
    }
    // Refusals of the same request also stop the write.
    const fs::path game = fakeGame("deny_write");
    CHECK(!writeControlFile(game, Request{1, 1, Action::Command, "quit"}));
    CHECK(!fs::exists(game / ".proxy" / "control.ini"));
    // A too long command is refused too.
    CHECK(!checkConsoleCommand("set r_x " + std::string(300, 'a')).ok);
}

TEST(live_link_allow_list_accepts_set_only) {
    CHECK(checkConsoleCommand("set r_fullbright 1").ok);
    CHECK(checkConsoleCommand("set ui_showfps 0").ok);
    CHECK(checkConsoleCommand("set cg_fov 65.5").ok);
    CHECK(checkConsoleCommand("set r_gamma -1").ok);
    CHECK(checkConsoleCommand("SET r_fullbright 1").ok);  // the verb is case-insensitive
    CHECK(!checkConsoleCommand("echo hi").ok);
    CHECK(!checkConsoleCommand("vstr x").ok);
}

TEST(live_link_map_id_rule) {
    CHECK(checkMapId("mp_dm_test").ok);
    CHECK(checkMapId("mp_abcdefghijkl").ok);     // 15 characters
    CHECK(!checkMapId("mp_abcdefghijklm").ok);   // 16 characters
    CHECK(!checkMapId("mp_").ok);
    CHECK(!checkMapId("dm_test").ok);
    CHECK(!checkMapId("MP_dm_test").ok);
    CHECK(!checkMapId("mp_dm-test").ok);
    CHECK(!checkMapId("mp_dm test").ok);
    CHECK(!checkMapId("").ok);
}

TEST(live_link_read_value_bounds) {
    CHECK(checkReadValue("1A2B 16").ok);
    CHECK(checkReadValue("1324AFFF 512").ok);
    CHECK(!checkReadValue("1324B000 16").ok);  // at the RVA limit
    CHECK(!checkReadValue("xyz 4").ok);
    CHECK(!checkReadValue("10 0").ok);
    CHECK(!checkReadValue("10 513").ok);
    CHECK(!checkReadValue("10").ok);
    CHECK(!checkReadValue("123456789 4").ok);
}

TEST(live_link_request_checks_pid_id_and_action_value) {
    CHECK(!checkRequest(Request{0, 1, Action::Status, ""}).ok);
    CHECK(!checkRequest(Request{1, 0, Action::Status, ""}).ok);
    CHECK(!checkRequest(Request{1, 1, Action::Status, "x"}).ok);   // status takes no value
    CHECK(!checkRequest(Request{1, 1, Action::Player, "x"}).ok);   // player takes no value
    CHECK(!checkRequest(Request{1, 1, Action::Select, "map_x"}).ok);
    CHECK(checkRequest(Request{1, 1, Action::Player, ""}).ok);
    CHECK(!checkRequest(Request{1, 1, Action::Command, std::string(4096, 'a')}).ok);
}

TEST(live_link_parse_result_json) {
    // Sample shape from docs/research/25 §1.4.
    const std::string json =
        "{\"pid\":23504,\"id\":16911,\"ok\":true,\"frontend\":true,\"map\":\"mp_dm_test\","
        "\"selected\":\"mp_dm_test\",\"console\":false,\"browser\":true,\"data\":\"\"}";
    Result r;
    std::string err;
    CHECK(parseResult(json, r, &err));
    CHECK(r.pid == 23504 && r.id == 16911 && r.ok && r.frontend);
    CHECK(r.map == "mp_dm_test" && r.selected == "mp_dm_test");
    CHECK(!r.console && r.browser);
    CHECK(r.data.empty());

    Result bad;
    CHECK(!parseResult("not json", bad, &err));
    CHECK(!parseResult("[1,2]", bad, &err));
    CHECK(!parseResult("{\"id\":1}", bad, &err));  // no pid
    CHECK(!parseResult("{\"pid\":-1,\"id\":1}", bad, &err));
    CHECK(!parseResult("{\"pid\":1.5,\"id\":1}", bad, &err));
    Result minimal;
    CHECK(parseResult("{\"pid\":3,\"id\":4}", minimal, &err));  // other fields default
    CHECK(minimal.map.empty() && !minimal.frontend);
}

TEST(live_link_player_data) {
    PlayerState s;
    CHECK(parsePlayerData("player_origin=1024.5,-88,32 entity_origin=0,0,0 view_angles=10,90,0 entity_angles=0,0,0", s));
    CHECK(s.valid);
    CHECK(s.origin.x == 1024.5 && s.origin.y == -88 && s.origin.z == 32);
    CHECK(s.angles.x == 10 && s.angles.y == 90 && s.angles.z == 0);

    PlayerState missing;
    CHECK(!parsePlayerData("entity_origin=0,0,0", missing));
    CHECK(!missing.valid);
    PlayerState broken;
    CHECK(!parsePlayerData("player_origin=1,2 view_angles=0,0,0", broken));
    CHECK(!parsePlayerData("player_origin=1,2,x view_angles=0,0,0", broken));
    CHECK(!parsePlayerData("player_origin=1,2,3 view_angles=1,2,3,4", broken));
    CHECK(!parsePlayerData("", broken));
}

TEST(live_link_tasklist_pids) {
    const std::string csv =
        "\"game_dx12_ship_replay.exe\",\"23504\",\"Console\",\"1\",\"4,512,000 K\"\r\n"
        "\"game_dx12_ship_replay_dw.exe\",\"100\",\"Console\",\"1\",\"1 K\"\r\n"
        "\"GAME_DX12_SHIP_REPLAY.EXE\",\"24000\",\"Console\",\"1\",\"1 K\"\r\n"
        "\"game_dx12_ship_replay.exe\",\"23504\",\"Console\",\"1\",\"1 K\"\r\n"
        "\"other.exe\",\"5\",\"Services\",\"0\",\"1 K\"\r\n";
    const std::vector<uint32_t> pids = parseTasklistPids(csv, "game_dx12_ship_replay.exe");
    CHECK(pids.size() == 2);
    CHECK(pids.size() == 2 && pids[0] == 23504 && pids[1] == 24000);  // exact name, case-insensitive, deduplicated
    CHECK(parseTasklistPids("INFO: No tasks are running which match the specified criteria.\r\n",
                            "game_dx12_ship_replay.exe")
              .empty());
    CHECK(parseTasklistPids("", "game_dx12_ship_replay.exe").empty());
}
