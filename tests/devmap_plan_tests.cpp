// One-key devmap (F5): game-state classification, command texts, DW log parsing and the step decisions
// (src/core/devmap_plan.*, docs/research/65_devmap_one_key.md).
#include <string>
#include <vector>

#include "devmap_plan.h"
#include "test_harness.h"

using namespace iw8r::devmap;

namespace {

GameSnapshot frontEnd() {
    GameSnapshot s;
    s.valid = true;
    s.gameMode = 2;
    s.frontEndState = 5;
    s.commandBufferIdle = true;
    return s;
}

GameSnapshot lobbyHost() {
    GameSnapshot s = frontEnd();
    s.inParty = true;
    s.partyHost = true;
    s.privateMatch = true;
    return s;
}

GameSnapshot matchHost(const std::string& map) {
    GameSnapshot s = lobbyHost();
    s.frontEndState = 0;
    s.inMatch = true;
    s.serverRunning = true;
    s.uiMapname = map;
    return s;
}

Context ctx(int processes = 1) {
    Context c;
    c.mapId = "mp_iw8r_test";
    c.processCount = processes;
    return c;
}

}  // namespace

TEST(devmap_classify_states) {
    CHECK(classify(GameSnapshot{}) == State::Unknown);
    GameSnapshot s = frontEnd();
    s.gameMode = 0;
    CHECK(classify(s) == State::Booting);
    s = frontEnd();
    s.frontEndState = 3;
    CHECK(classify(s) == State::Booting);
    s = frontEnd();
    s.gameMode = 1;
    CHECK(classify(s) == State::NotMultiplayer);
    CHECK(classify(frontEnd()) == State::FrontEndNoParty);
    CHECK(classify(lobbyHost()) == State::LobbyHost);
    s = lobbyHost();
    s.partyHost = false;
    CHECK(classify(s) == State::LobbyGuest);
    CHECK(classify(matchHost("mp_x")) == State::InMatchHost);
    s = matchHost("mp_x");
    s.serverRunning = false;
    CHECK(classify(s) == State::InMatchGuest);
}

TEST(devmap_command_texts) {
    CHECK(setMapCommand("mp_iw8r_test") == "set NSQLTTMRMP mp_iw8r_test");
    CHECK(setGametypeCommand("war") == "set MOLPOSLOMO war");
    const std::vector<std::string> host = hostLobbyCommands();
    CHECK(host.size() == 2 && host[0] == "set LSTLQTSSRM 1" && host[1] == "xstartprivatematch");
    // Private match lobby: "xpartygo 1" (the 1 skips the 5 + 3 s lobby countdown). System link (Local Play) lobby we
    // host: the two countdown dvars are zeroed, then sv_systemlinkStartMatch (it has no skip flag).
    auto start = startMatchCommands("mp_iw8r_test", "", lobbyHost());
    CHECK(start.size() == 3 && start[0] == "set NSQLTTMRMP mp_iw8r_test" && start[1] == "set MOLPOSLOMO war" && start[2] == "xpartygo 1");
    GameSnapshot sl = lobbyHost();
    sl.privateMatch = false;
    sl.systemlink = sl.systemlinkHost = true;
    start = startMatchCommands("mp_iw8r_test", "dom", sl);
    CHECK(start.size() == 5 && start[1] == "set MOLPOSLOMO dom" && start[2] == "set NMQNNPQPRS 0" && start[3] == "set LTLOQSNSOT 0" &&
          start[4] == "sv_systemlinkStartMatch");
    std::string startText;
    CHECK(joinCommands(start, startText));  // every line passes the command filter
    // A game mode preset's dvars are set before the map and the start (research 87: a private match applies none).
    const auto withMode = startMatchCommands("mp_iw8r_test", "br", lobbyHost(),
                                            {{"scr_br_teamsize", "3"}, {"loot_table_name", "mp/loot_item_defs_s3.csv"}, {"bad name", "1"}, {"x", "a;b"}});
    CHECK(withMode.size() == 5 && withMode[0] == "set scr_br_teamsize 3" && withMode[1] == "set loot_table_name mp/loot_item_defs_s3.csv" &&
          withMode[2] == "set NSQLTTMRMP mp_iw8r_test" && withMode[3] == "set MOLPOSLOMO br" && withMode[4] == "xpartygo 1");
    std::string modeText;
    CHECK(joinCommands(withMode, modeText));
    CHECK(backToLobbyCommand() == "xpartybackout");
    CHECK(reloadCommand() == "map_reload");

    std::string joined;
    CHECK(joinCommands({"set NSQLTTMRMP mp_a", "", "xpartygo"}, joined));
    CHECK(joined == "set NSQLTTMRMP mp_a\nxpartygo");
    std::string err;
    CHECK(!joinCommands({"set a 1; quit"}, joined, &err) && joined.empty() && !err.empty());
    CHECK(!joinCommands({"say \"hi\""}, joined));
    CHECK(!joinCommands({"set a\n1"}, joined));
    CHECK(!joinCommands({}, joined));
}

TEST(devmap_dw_log_parsing) {
    const std::string log =
        "DW_CUSTOM_MAPS installed: mp_old \r\n"
        "noise\r\n"
        "DW_CUSTOM_MAPS installed: mp_4doffice mp_iw8r_test mp_test \r\n"
        "DW_LOCAL_BUILD_INIT host=127.0.0.1\r\n";
    bool found = false;
    const auto ids = parseDwInstalledMaps(log, &found);
    CHECK(found);
    CHECK(ids.size() == 3 && ids[1] == "mp_iw8r_test" && ids[2] == "mp_test");
    CHECK(parseDwInstalledMaps("nothing here", &found).empty() && !found);

    CHECK(parseDwReloadResult("", "mp_a") == 0);
    CHECK(parseDwReloadResult("map_reload: mp_a: restart queued; the level zones unload\r\n", "mp_a") == 0);
    CHECK(parseDwReloadResult("map_reload: mp_a: level zones unloaded; loading from disk.\r\n"
                              "map_reload: mp_a: level zones reloaded from disk; server running.\r\n",
                              "mp_a") == 1);
    CHECK(parseDwReloadResult("map_reload: mp_a is not an installed custom map package.\r\n", "mp_a") == -1);
    CHECK(parseDwReloadResult("map_reload: the native restart was refused; no zone was unloaded.\r\n", "mp_a") == -1);
}

TEST(devmap_launch_args) {
    const auto a = splitLaunchArgs(kDefaultLaunchArgs);
    CHECK(a.size() == 2 && a[0] == "+mpMode" && a[1] == "1");
    const auto b = splitLaunchArgs("  +set \"x y\"  +mpMode 1 \"\" ");
    CHECK(b.size() == 5 && b[1] == "x y" && b[4].empty());
    CHECK(splitLaunchArgs("").empty());
}

TEST(devmap_decide_start) {
    // Not running: deploy + launch, then wait for the menu.
    Decision d = decide(Phase::Start, GameSnapshot{}, ctx(0));
    CHECK(d.action == Action::Launch && d.next == Phase::WaitBoot);
    // Several exes: stop.
    d = decide(Phase::Start, frontEnd(), ctx(2));
    CHECK(d.action == Action::Fail);
    // Unreadable game: stop with a manual hint.
    d = decide(Phase::Start, GameSnapshot{}, ctx());
    CHECK(d.action == Action::Fail);
    // MP menu: deploy, then host a lobby (WaitBoot does that).
    d = decide(Phase::Start, frontEnd(), ctx());
    CHECK(d.action == Action::Deploy && d.next == Phase::WaitBoot);
    // Lobby host: deploy and start.
    d = decide(Phase::Start, lobbyHost(), ctx());
    CHECK(d.action == Action::DeployThenStart && d.next == Phase::WaitMatch);
    // Guest anywhere: deploy, then stop with the reason.
    GameSnapshot g = lobbyHost();
    g.partyHost = false;
    d = decide(Phase::Start, g, ctx());
    CHECK(d.action == Action::Deploy && d.next == Phase::Failed);
    // SP mode: deploy, then ask for Multiplayer.
    GameSnapshot sp = frontEnd();
    sp.gameMode = 1;
    d = decide(Phase::Start, sp, ctx());
    CHECK(d.action == Action::Deploy && d.next == Phase::Failed);
}

TEST(devmap_decide_running_match) {
    // Same map, DW client that listed the package, reload allowed: deploy + map_reload.
    Context c = ctx();
    c.dwBuild = true;
    c.dwInstalled = {"mp_iw8r_test"};
    Decision d = decide(Phase::Start, matchHost("mp_iw8r_test"), c);
    CHECK(d.action == Action::DeployThenReload && d.next == Phase::WaitReload);
    // Map id compare ignores case.
    d = decide(Phase::Start, matchHost("MP_IW8R_TEST"), c);
    CHECK(d.action == Action::DeployThenReload);
    // Reload turned off, or not a DW client, or a package the client did not scan: end the match first.
    c.reloadAllowed = false;
    CHECK(decide(Phase::Start, matchHost("mp_iw8r_test"), c).action == Action::BackToLobby);
    c.reloadAllowed = true;
    c.dwInstalled = {"mp_other"};
    CHECK(decide(Phase::Start, matchHost("mp_iw8r_test"), c).action == Action::BackToLobby);
    CHECK(decide(Phase::Start, matchHost("mp_iw8r_test"), ctx()).action == Action::BackToLobby);
    // Another map: end the match, then (WaitFrontEnd) deploy + start.
    d = decide(Phase::Start, matchHost("mp_shipment"), c);
    CHECK(d.action == Action::BackToLobby && d.next == Phase::WaitFrontEnd);
    // Someone else's server.
    GameSnapshot guest = matchHost("mp_shipment");
    guest.serverRunning = false;
    d = decide(Phase::Start, guest, c);
    CHECK(d.action == Action::Deploy && d.next == Phase::Failed);
}

TEST(devmap_decide_waits) {
    Context c = ctx();
    // WaitBoot: the menu must settle and the command buffer be idle before hosting.
    c.stableMs = 1000;
    CHECK(decide(Phase::WaitBoot, frontEnd(), c).action == Action::None);
    c.stableMs = kFrontEndSettleMs;
    Decision d = decide(Phase::WaitBoot, frontEnd(), c);
    CHECK(d.action == Action::HostLobby && d.next == Phase::WaitLobby);
    // A DW client that has not signed in yet gets the longer wait; once it has, the short one.
    {
        Context dwc = ctx();
        dwc.dwBuild = true;
        dwc.stableMs = kFrontEndSettleMs;
        CHECK(decide(Phase::WaitBoot, frontEnd(), dwc).action == Action::None);
        dwc.stableMs = kFrontEndUnsignedSettleMs;
        CHECK(decide(Phase::WaitBoot, frontEnd(), dwc).action == Action::HostLobby);
        dwc.signedIn = true;
        dwc.stableMs = kFrontEndSettleMs;
        CHECK(decide(Phase::WaitBoot, frontEnd(), dwc).action == Action::HostLobby);
        CHECK(kFrontEndSettleMs <= 2000);  // no long fixed wait on the menu (research 76 step 1)
    }
    GameSnapshot busy = frontEnd();
    busy.commandBufferIdle = false;
    CHECK(decide(Phase::WaitBoot, busy, c).action == Action::None);
    // Booting state keeps waiting until the timeout.
    GameSnapshot boot = frontEnd();
    boot.gameMode = 0;
    CHECK(decide(Phase::WaitBoot, boot, c).action == Action::None);
    c.elapsedMs = kBootTimeoutMs + 1;
    CHECK(decide(Phase::WaitBoot, boot, c).action == Action::Fail);
    c.elapsedMs = 0;
    // Mode select (mode 0) on the DW client: pick Multiplayer after it settles, at most three times.
    Context dw = ctx();
    dw.dwBuild = true;
    dw.stableMs = kModeSelectSettleMs - 1;
    CHECK(decide(Phase::WaitBoot, boot, dw).action == Action::None);
    dw.stableMs = kModeSelectSettleMs;
    d = decide(Phase::WaitBoot, boot, dw);
    CHECK(d.action == Action::SelectMultiplayer && d.next == Phase::WaitBoot);
    dw.selectMpSent = 1;
    CHECK(decide(Phase::WaitBoot, boot, dw).action == Action::None);  // the next try waits twice as long
    dw.stableMs = 2 * kModeSelectSettleMs;
    CHECK(decide(Phase::WaitBoot, boot, dw).action == Action::SelectMultiplayer);
    dw.selectMpSent = 3;
    dw.stableMs = 10 * kModeSelectSettleMs;
    CHECK(decide(Phase::WaitBoot, boot, dw).action == Action::None);
    CHECK(decide(Phase::WaitBoot, boot, c).action == Action::None);  // not a DW client: the owner picks the mode
    CHECK(selectMultiplayerCommand() == "dw_localTestMenuMP");
    // With the sign-in line of the client log the selection goes out after a short settle, not after 15 s.
    {
        Context si = ctx();
        si.dwBuild = si.signedIn = true;
        si.stableMs = kModeSelectSignedInMs - 1;
        CHECK(decide(Phase::WaitBoot, boot, si).action == Action::None);
        si.stableMs = kModeSelectSignedInMs;
        CHECK(decide(Phase::WaitBoot, boot, si).action == Action::SelectMultiplayer);
        si.selectMpSent = 1;
        CHECK(decide(Phase::WaitBoot, boot, si).action == Action::None);
        si.stableMs = kModeSelectSignedInMs + kModeSelectRetryMs;
        CHECK(decide(Phase::WaitBoot, boot, si).action == Action::SelectMultiplayer);
        si.selectMpSent = 3;
        si.stableMs = 100000;
        CHECK(decide(Phase::WaitBoot, boot, si).action == Action::None);
        CHECK(kModeSelectSignedInMs < kModeSelectSettleMs);
    }
    // A client that runs the start itself (DW_DEVMAP lines): the editor sends nothing and only watches.
    {
        Context cl = ctx();
        cl.dwBuild = cl.signedIn = true;
        cl.clientRequest = 1;
        cl.stableMs = 100000;
        CHECK(decide(Phase::WaitBoot, boot, cl).action == Action::None);
        CHECK(decide(Phase::WaitBoot, frontEnd(), cl).action == Action::None);
        CHECK(decide(Phase::WaitBoot, lobbyHost(), cl).action == Action::None);
        CHECK(decide(Phase::WaitBoot, matchHost("mp_iw8r_test"), cl).action == Action::Succeed);
        cl.elapsedMs = kBootTimeoutMs + 1;
        CHECK(decide(Phase::WaitBoot, boot, cl).action == Action::Fail);
        cl.elapsedMs = 0;
        // After the client reports a failure the editor's own steps take over.
        cl.clientRequest = -1;
        CHECK(decide(Phase::WaitBoot, frontEnd(), cl).action == Action::HostLobby);
    }
    // The launched game went away.
    Context gone = ctx(0);
    gone.elapsedMs = 31000;
    CHECK(decide(Phase::WaitBoot, GameSnapshot{}, gone).action == Action::Fail);
    // A short second process (updater) is tolerated while waiting.
    Context two = ctx(2);
    two.elapsedMs = 1000;
    CHECK(decide(Phase::WaitBoot, frontEnd(), two).action == Action::None);

    // WaitLobby: lobby host -> start (no second deploy).
    c.stableMs = kLobbySettleMs;
    d = decide(Phase::WaitLobby, lobbyHost(), c);
    CHECK(d.action == Action::StartMatch && d.next == Phase::WaitMatch);
    c.elapsedMs = kLobbyTimeoutMs + 1;
    c.hostLobbySent = 2;
    CHECK(decide(Phase::WaitLobby, frontEnd(), c).action == Action::Fail);
    // No lobby 10 s after the host commands: they are sent once more, then the timeout applies.
    c.elapsedMs = kHostLobbyRetryMs + 1;
    c.hostLobbySent = 1;
    d = decide(Phase::WaitLobby, frontEnd(), c);
    CHECK(d.action == Action::HostLobby && d.next == Phase::WaitLobby);
    c.hostLobbySent = 2;
    CHECK(decide(Phase::WaitLobby, frontEnd(), c).action == Action::None);
    c.hostLobbySent = 0;
    c.elapsedMs = 0;

    // WaitFrontEnd (after xpartybackout): still in the match -> wait; lobby -> deploy + start.
    CHECK(decide(Phase::WaitFrontEnd, matchHost("mp_shipment"), c).action == Action::None);
    d = decide(Phase::WaitFrontEnd, lobbyHost(), c);
    CHECK(d.action == Action::DeployThenStart && d.next == Phase::WaitMatch);
    d = decide(Phase::WaitFrontEnd, frontEnd(), c);
    CHECK(d.action == Action::Deploy && d.next == Phase::WaitBoot);

    // WaitMatch: on the map -> done; still in the lobby after 20 s -> failed start.
    d = decide(Phase::WaitMatch, matchHost("mp_iw8r_test"), c);
    CHECK(d.action == Action::Succeed && d.next == Phase::Done);
    CHECK(decide(Phase::WaitMatch, matchHost("mp_shipment"), c).action == Action::None);
    c.elapsedMs = 21000;
    CHECK(decide(Phase::WaitMatch, lobbyHost(), c).action == Action::Fail);
    c.elapsedMs = 0;

    // WaitReload: DW report decides.
    CHECK(decide(Phase::WaitReload, matchHost("mp_iw8r_test"), c).action == Action::None);
    c.dwReload = 1;
    CHECK(decide(Phase::WaitReload, matchHost("mp_iw8r_test"), c).action == Action::Succeed);
    c.dwReload = -1;
    d = decide(Phase::WaitReload, matchHost("mp_iw8r_test"), c);
    CHECK(d.action == Action::BackToLobby && d.next == Phase::WaitFrontEnd);

    // Finished phases do nothing.
    CHECK(decide(Phase::Done, lobbyHost(), c).action == Action::None);
    CHECK(decide(Phase::Failed, lobbyHost(), c).action == Action::None);
}

TEST(devmap_start_request_and_launch_log) {
    auto a = startRequestArgs("mp_iw8r_test", "", 0);
    CHECK(a.size() == 4 && a[0] == "--devmap" && a[1] == "mp_iw8r_test" && a[2] == "--gametype" && a[3] == "war");
    a = startRequestArgs("mp_iw8r_test", "dom", 500);
    CHECK(a.size() == 6 && a[3] == "dom" && a[4] == "--bots" && a[5] == "199");
    CHECK(startRequestArgs("iw8r_test", "war").empty());            // not an mp_ id
    CHECK(startRequestArgs("mp_Test", "war").empty());              // upper case
    CHECK(startRequestArgs("mp_a b", "war").empty());               // a space would split the argument
    CHECK(startRequestArgs("mp_abcdefghijklmnop", "war").empty());  // longer than a package id
    CHECK(startRequestArgs("mp_iw8r_test", "war;quit").empty());

    LaunchLog l = parseDwLaunchLog("");
    CHECK(!l.signedIn && !l.probeFailed && l.client == 0 && l.clientLine.empty());
    l = parseDwLaunchLog("x DW_LOCAL_PROBE_FAILED host=127.0.0.1\n");
    CHECK(l.probeFailed && !l.signedIn);
    l = parseDwLaunchLog("x DW_LOCAL_PROBE_FAILED host=127.0.0.1\nx DW_LOCAL_PROBE_OK host=127.0.0.1 backend health reached\n"
                         "x DW_LOCAL_PLATFORM_SIGNED_IN native profile transition completed\n");
    CHECK(!l.probeFailed && l.signedIn && l.client == 0);
    CHECK(parseDwLaunchLog("t DW_LOCAL_IDENTITY_CLAIMS_RESTORED native ID\r\n").signedIn);
    l = parseDwLaunchLog("t DW_DEVMAP request mp_iw8r_test war\r\nt DW_DEVMAP hosting\r\n");
    CHECK(l.client == 1 && l.clientLine == "hosting");
    l = parseDwLaunchLog("t DW_DEVMAP request mp_iw8r_test war\nt DW_DEVMAP failed: mp_iw8r_test is not installed\n");
    CHECK(l.client == -1 && l.clientLine == "failed: mp_iw8r_test is not installed");
    CHECK(std::string(kClientRequestMarker) == "DW_DEVMAP ");
}

TEST(devmap_exe_choice_and_host) {
    CHECK(pickLaunchExe({}) == -1);
    // The local DW client first, then the DW client, then any recompiled exe, then a stock exe.
    std::vector<ExeCandidate> all = {{"E:/g/game_dx12_ship_replay.exe", false},
                                     {"E:/g/game_dx12_ship_replay_dw.exe", true},
                                     {"E:/g/game_dx12_ship_replay_dw_b.exe", true},
                                     {"E:\\g\\Game_DX12_ship_replay_local_dw.exe", true}};
    CHECK(pickLaunchExe(all) == 3);
    all.pop_back();
    CHECK(pickLaunchExe(all) == 1);
    all.erase(all.begin() + 1);
    CHECK(pickLaunchExe(all) == 1);  // _dw_b: recompiled
    all.pop_back();
    CHECK(pickLaunchExe(all) == 0);  // only the stock exe is left
    // A file with the local name but without the recompiled marker is not preferred over a recompiled one.
    CHECK(pickLaunchExe({{"a/game_dx12_ship_replay_local_dw.exe", false}, {"a/game_dx12_ship_replay_optimized.exe", true}}) == 1);

    // The client's updater matches the exe name pattern; it is never the game.
    CHECK(pickLaunchExe({{"a/game_dx12_ship_replay_dw.exe.updater.exe", false}, {"a/game_dx12_ship_replay_z.exe", false}}) == 1);

    CHECK(healthIsDwHost("{\"server\":\"iw8-dw\",\"ok\":true}"));
    CHECK(healthIsDwHost("HTTP/1.0 200 OK\r\n\r\n{ \"ok\": true, \"server\" :\n \"iw8-dw\" }"));
    CHECK(!healthIsDwHost("{\"server\":\"nginx\"}"));
    CHECK(!healthIsDwHost("{\"servers\":\"iw8-dw\"}"));
    CHECK(!healthIsDwHost(""));

    CHECK(iniValue("host=127.0.0.1\nbypass_update_restart=1\n", "host") == "127.0.0.1");
    CHECK(iniValue("a=1\r\n  host = localhost \r\n", "host") == "localhost");
    CHECK(iniValue("hostname=x\n", "host").empty());
    CHECK(iniValue("", "host").empty());
    CHECK(isLoopbackHost("127.0.0.1") && isLoopbackHost("LOCALHOST") && isLoopbackHost("127.9.9.9") && isLoopbackHost("::1"));
    CHECK(!isLoopbackHost("192.168.1.2") && !isLoopbackHost("") && !isLoopbackHost("example.com"));
}

TEST(devmap_cover_rules) {
    // A game the editor did not start is never covered.
    CHECK(!coverWanted(false, -1, GameSnapshot{}));
    // From the launch until the start request: covered, whatever the game shows (boot, mode select, menu, lobby).
    CHECK(coverWanted(true, -1, GameSnapshot{}));
    CHECK(coverWanted(true, -1, frontEnd()));
    CHECK(coverWanted(true, -1, lobbyHost()));
    // After the start request: only while the front end is still on screen, and never longer than the limit.
    CHECK(coverWanted(true, 100, lobbyHost()));
    CHECK(!coverWanted(true, kCoverMaxAfterStartMs, lobbyHost()));
    GameSnapshot loading = lobbyHost();
    loading.frontEndState = 0;
    CHECK(!coverWanted(true, 100, loading));
    CHECK(!coverWanted(true, 100, GameSnapshot{}));  // unreadable while the map loads
    // In a match the card is gone even if no start request was seen (the client started the map by itself).
    CHECK(!coverWanted(true, -1, matchHost("mp_iw8r_test")));

    CHECK(clientLineIsStart("start requested mp_iw8r_test war"));
    CHECK(clientLineIsStart("loading mp_iw8r_test"));
    CHECK(!clientLineIsStart("request mp_iw8r_test war") && !clientLineIsStart("hosting") && !clientLineIsStart(""));
}
