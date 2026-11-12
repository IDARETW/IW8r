import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/devmap_launch.cpp', [
(r'''#include <QDir>
#include <QFile>''', r'''#include <QDir>
#include <QElapsedTimer>
#include <QFile>'''),
])

edit('tests/devmap_plan_tests.cpp', [
(r'''    // Private match lobby: xpartygo. System link (Local Play) lobby we host: sv_systemlinkStartMatch.
    auto start = startMatchCommands("mp_iw8r_test", "", lobbyHost());
    CHECK(start.size() == 3 && start[0] == "set NSQLTTMRMP mp_iw8r_test" && start[1] == "set MOLPOSLOMO war" && start[2] == "xpartygo");''',
 r'''    // Private match lobby: "xpartygo 1" (the 1 skips the 5 + 3 s lobby countdown). System link (Local Play) lobby we
    // host: the two countdown dvars are zeroed, then sv_systemlinkStartMatch (it has no skip flag).
    auto start = startMatchCommands("mp_iw8r_test", "", lobbyHost());
    CHECK(start.size() == 3 && start[0] == "set NSQLTTMRMP mp_iw8r_test" && start[1] == "set MOLPOSLOMO war" && start[2] == "xpartygo 1");'''),
(r'''    CHECK(start.size() == 3 && start[1] == "set MOLPOSLOMO dom" && start[2] == "sv_systemlinkStartMatch");''',
 r'''    CHECK(start.size() == 5 && start[1] == "set MOLPOSLOMO dom" && start[2] == "set NMQNNPQPRS 0" && start[3] == "set LTLOQSNSOT 0" &&
          start[4] == "sv_systemlinkStartMatch");
    std::string startText;
    CHECK(joinCommands(start, startText));  // every line passes the command filter'''),
(r'''    // WaitBoot: the menu must settle and the command buffer be idle before hosting.
    c.stableMs = 1000;
    CHECK(decide(Phase::WaitBoot, frontEnd(), c).action == Action::None);
    c.stableMs = kFrontEndSettleMs;
    Decision d = decide(Phase::WaitBoot, frontEnd(), c);
    CHECK(d.action == Action::HostLobby && d.next == Phase::WaitLobby);''',
 r'''    // WaitBoot: the menu must settle and the command buffer be idle before hosting.
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
    }'''),
(r'''    CHECK(decide(Phase::WaitBoot, boot, c).action == Action::None);  // not a DW client: the owner picks the mode
    CHECK(selectMultiplayerCommand() == "dw_localTestMenuMP");''',
 r'''    CHECK(decide(Phase::WaitBoot, boot, c).action == Action::None);  // not a DW client: the owner picks the mode
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
    }'''),
(r'''    c.elapsedMs = kLobbyTimeoutMs + 1;
    CHECK(decide(Phase::WaitLobby, frontEnd(), c).action == Action::Fail);
    c.elapsedMs = 0;''',
 r'''    c.elapsedMs = kLobbyTimeoutMs + 1;
    CHECK(decide(Phase::WaitLobby, frontEnd(), c).action == Action::Fail);
    // No lobby 10 s after the host commands: they are sent once more, then the timeout applies.
    c.elapsedMs = kHostLobbyRetryMs + 1;
    c.hostLobbySent = 1;
    d = decide(Phase::WaitLobby, frontEnd(), c);
    CHECK(d.action == Action::HostLobby && d.next == Phase::WaitLobby);
    c.hostLobbySent = 2;
    CHECK(decide(Phase::WaitLobby, frontEnd(), c).action == Action::None);
    c.hostLobbySent = 0;
    c.elapsedMs = 0;'''),
])

s = io.open('tests/devmap_plan_tests.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
add = r'''
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
'''
s = s.rstrip('\r\n') + nl + add.replace('\n', nl)
io.open('tests/devmap_plan_tests.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
