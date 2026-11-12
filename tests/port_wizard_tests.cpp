// CoD4 port wizard (roadmap 11.9): route choice, input checks, exact command lines and converter log summary.
// The command lines are the ones in docs/research/43_cod4_port.md sections 2.1 and 4.2 and compile_panel.cpp:390-407.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "port_wizard.h"
#include "test_harness.h"

using namespace iw8r;

namespace fs = std::filesystem;

namespace {

fs::path scratchDir() {
    const fs::path d = fs::temp_directory_path() / "iw8r_port_wizard_tests";
    fs::create_directories(d);
    return d;
}

// Writes `bytes` to scratchDir()/name and returns the path.
std::string writeFile(const std::string& name, const std::string& bytes) {
    const fs::path p = scratchDir() / name;
    std::ofstream out(p, std::ios::binary);
    out << bytes;
    return p.string();
}

// An IW3 zone: the IW3 unsigned header followed by zero padding.
std::string iw3ZoneBytes() { return std::string("IWffu100") + std::string(16, '\0'); }

bool containsText(const std::vector<std::string>& items, const std::string& needle) {
    return std::any_of(items.begin(), items.end(), [&](const std::string& s) { return s.find(needle) != std::string::npos; });
}

// Index of `value` in `args`, or -1.
long indexOf(const std::vector<std::string>& args, const std::string& value) {
    const auto it = std::find(args.begin(), args.end(), value);
    return it == args.end() ? -1 : static_cast<long>(it - args.begin());
}

// A valid route A input over a real temp zone file.
PortInput builtInput() {
    PortInput in;
    in.path = writeFile("mp_backlot.ff", iw3ZoneBytes());
    in.mapName = "mp_port_test";
    in.cod4Root = "C:/CoD4";
    in.zonetoolExe = "C:/x64zt/zonetool.exe";
    in.outDir = "C:/work/port";
    in.replayExe = "C:/Replay/game_dx12_ship_replay.exe";
    in.unlinkerExe = "C:/tools/Unlinker.exe";
    return in;
}

// A valid route B input over a real temp .map file.
PortInput mapInput() {
    PortInput in;
    in.path = writeFile("mp_test.map", "iwmap 4\n// CoD4 source\n");
    in.mapName = "mp_port_test";
    in.zonetoolExe = "C:/x64zt/zonetool.exe";
    in.outDir = "C:/work/port";
    in.replayExe = "C:/Replay/game_dx12_ship_replay.exe";
    return in;
}

}  // namespace

TEST(port_wizard_detect_ff_header) {
    CHECK(detectPortSource(writeFile("pw_iw3.ff", iw3ZoneBytes())) == PortSource::BuiltFastFile);
    CHECK(detectPortSource(writeFile("pw_iw8.ff", std::string("IWffc100") + std::string(16, '\0'))) == PortSource::Unknown);
    CHECK(detectPortSource(writeFile("pw_tiny.ff", "IWf")) == PortSource::Unknown);
    // Unreadable (missing) .ff keeps the extension's route; planPort then reports the missing file.
    CHECK(detectPortSource((scratchDir() / "pw_missing.ff").string()) == PortSource::BuiltFastFile);
}

TEST(port_wizard_detect_map_and_unknown) {
    CHECK(detectPortSource("C:/CoD4/map_source/mp_test.map") == PortSource::RadiantMap);
    CHECK(detectPortSource("C:/CoD4/map_source/MP_TEST.MAP") == PortSource::RadiantMap);
    CHECK(detectPortSource("C:/tmp/archive.zip") == PortSource::Unknown);
    CHECK(detectPortSource("C:/tmp/notes.txt") == PortSource::Unknown);
    CHECK(detectPortSource("C:/tmp/mp_test.ff.bak") == PortSource::Unknown);
    CHECK(detectPortSource("C:/tmp/no_extension") == PortSource::Unknown);
}

TEST(port_wizard_plan_built_ff_command) {
    const PortInput in = builtInput();
    const PortPlan plan = planPort(in);
    CHECK(plan.source == PortSource::BuiltFastFile);
    CHECK(plan.errors.empty());
    CHECK(plan.warnings.empty());
    CHECK(plan.steps.size() == 1);
    if (plan.steps.size() != 1) return;

    const PortStep& step = plan.steps[0];
    const std::vector<std::string> expected = {
        "-game", "iw3", "-path", "C:/CoD4", "dumpzone", "mp_backlot", "--target", "iw8",
        "--map", "mp_port_test", "--style", "iw8", "--unlinker", "C:/tools/Unlinker.exe",
        "--replay", "C:/Replay/game_dx12_ship_replay.exe", "--iw3_mode_assets", "0"};
    CHECK(step.program == in.zonetoolExe);
    CHECK(step.args == expected);
    CHECK(step.workDir == in.outDir);
    // The converter reads options only after --target (zone_commands.cpp:173-200).
    CHECK(indexOf(step.args, "--target") < indexOf(step.args, "--map"));
    CHECK(indexOf(step.args, "dumpzone") == 4);
}

TEST(port_wizard_plan_built_ff_without_optional_tools) {
    PortInput in = builtInput();
    in.replayExe.clear();
    in.unlinkerExe.clear();
    const PortPlan plan = planPort(in);
    CHECK(plan.errors.empty());
    CHECK(plan.warnings.size() == 2);
    CHECK(containsText(plan.warnings, "Replay exe"));
    CHECK(containsText(plan.warnings, "Unlinker"));
    CHECK(plan.steps.size() == 1);
    if (plan.steps.size() != 1) return;
    CHECK(indexOf(plan.steps[0].args, "--unlinker") == -1);
    CHECK(indexOf(plan.steps[0].args, "--replay") == -1);
}

TEST(port_wizard_plan_built_ff_needs_cod4_root) {
    PortInput in = builtInput();
    in.cod4Root.clear();
    const PortPlan plan = planPort(in);
    CHECK(containsText(plan.errors, "CoD4 install folder"));
    CHECK(plan.steps.empty());
}

TEST(port_wizard_plan_map_route_b_commands) {
    const PortInput in = mapInput();
    const PortPlan plan = planPort(in);
    CHECK(plan.source == PortSource::RadiantMap);
    CHECK(plan.errors.empty());
    // No baked light and grey materials are always said for a .map.
    CHECK(containsText(plan.warnings, "baked light"));
    CHECK(containsText(plan.warnings, "grey"));
    CHECK(plan.steps.size() == 2);
    if (plan.steps.size() != 2) return;

    // Step 1 is the editor's own compile: no process, so an empty program.
    CHECK(plan.steps[0].program.empty());
    CHECK(plan.steps[0].args.empty());

    const std::string extraction = (fs::path(in.outDir) / "extraction").string();
    const std::string zones = (fs::path(in.outDir) / "zones").string();
    const std::string replayDir = fs::path(in.replayExe).parent_path().string();
    const std::vector<std::string> expected = {
        "-game", "iw8", "-path", replayDir, "buildextraction", extraction, "--map", "mp_port_test",
        "--replay", in.replayExe, "--output", zones, "--iw3_mode_assets", "off"};
    CHECK(plan.steps[1].program == in.zonetoolExe);
    CHECK(plan.steps[1].args == expected);
    CHECK(plan.steps[1].workDir == in.outDir);
}

TEST(port_wizard_plan_map_needs_replay) {
    PortInput in = mapInput();
    in.replayExe.clear();
    const PortPlan plan = planPort(in);
    CHECK(containsText(plan.errors, "Replay exe"));
    CHECK(plan.steps.empty());
}

TEST(port_wizard_invalid_map_names) {
    const std::vector<std::string> bad = {"mp_", "port_test", "MP_test", "mp_Test", "mp_a-b", "mp_1234567890123", ""};
    for (const std::string& name : bad) {
        PortInput in = builtInput();
        in.mapName = name;
        const PortPlan plan = planPort(in);
        CHECK(containsText(plan.errors, "map name"));
        CHECK(plan.steps.empty());
    }
    // The longest allowed name: mp_ plus 12 characters, 15 in all.
    PortInput ok = builtInput();
    ok.mapName = "mp_123456789012";
    CHECK(!containsText(planPort(ok).errors, "map name"));
}

TEST(port_wizard_missing_zonetool) {
    PortInput in = builtInput();
    in.zonetoolExe.clear();
    const PortPlan plan = planPort(in);
    CHECK(containsText(plan.errors, "zonetool path is not set"));
    CHECK(plan.steps.empty());
}

TEST(port_wizard_missing_input_file_and_output) {
    PortInput in = builtInput();
    in.path = (scratchDir() / "pw_absent_zone.ff").string();
    CHECK(containsText(planPort(in).errors, "does not exist"));

    PortInput noOut = builtInput();
    noOut.outDir.clear();
    CHECK(containsText(planPort(noOut).errors, "output folder"));
}

TEST(port_wizard_unknown_input_is_error) {
    PortInput in = builtInput();
    in.path = writeFile("pw_wrong_header.ff", std::string("IWffc100") + std::string(16, '\0'));
    const PortPlan plan = planPort(in);
    CHECK(plan.source == PortSource::Unknown);
    CHECK(containsText(plan.errors, "neither"));
    CHECK(plan.steps.empty());
}

TEST(port_wizard_log_summary_mixed) {
    // Real formats from the converter (see port_wizard.h for the source lines).
    const std::string log =
        "iw3: ledger mp_backlot material: converted=12 unused=3\n"
        "iw3: ledger mp_backlot model: converted=7 stock=2\n"
        "iw3: ledger: 2 xmodel declaration(s) of the map zone are UNEXPLAINED: foo; bar\n"
        "iw3: image 'mc/brick_wall_a' is missing; using 'mc/brick_wall_b' from another compile\n"
        "iw3: 2 physics preset(s) have no exported payload; their models stay static: pre_a, pre_b\n"
        "iw3: 1 declared source FX graph(s) have no exported payload; first: fx/old_spark\n"
        "iw3: converted 5 IW3 window prefab(s) to native breakable glass\n";
    const PortLogSummary s = summarizePortLog(log);
    CHECK(s.convertedAssets == 19);  // 12 + 7 converted rows; the UNEXPLAINED line and the stock fate are not counted
    CHECK(s.missingAssets == 4);     // 1 image + 2 presets + 1 FX graph
    const std::vector<std::string> names = {"mc/brick_wall_a", "pre_a", "pre_b", "fx/old_spark"};
    CHECK(s.missing == names);
    CHECK(!s.ok);
}

TEST(port_wizard_log_summary_clean) {
    const PortLogSummary s = summarizePortLog("iw3: ledger mp_test model: converted=4\r\nbuild-map: done\r\n");
    CHECK(s.convertedAssets == 4);
    CHECK(s.missingAssets == 0);
    CHECK(s.missing.empty());
    CHECK(s.ok);
}

TEST(port_wizard_log_summary_error_line_fails) {
    const std::string log = "iw3: ledger mp_test model: converted=4\n"
                            "error: IW3 entity model assets are absent from the map fastfile (mp_x)\n";
    const PortLogSummary s = summarizePortLog(log);
    CHECK(s.convertedAssets == 4);
    CHECK(!s.ok);
}

TEST(port_wizard_log_summary_empty) {
    const PortLogSummary s = summarizePortLog("");
    CHECK(s.convertedAssets == 0);
    CHECK(s.missingAssets == 0);
    CHECK(!s.ok);  // nothing converted is not a success
}

TEST(port_wizard_log_summary_small_glass_shards) {
    const PortLogSummary s = summarizePortLog("iw3: small_glass shard model export missing: glass_a, glass_b\n");
    CHECK(s.missingAssets == 2);
    const std::vector<std::string> names = {"glass_a", "glass_b"};
    CHECK(s.missing == names);
}
