#pragma once
// CoD4 port wizard, planning and validation (roadmap 11.9). Pure core: no UI, no process launch. The dialog (not in
// this file) shows the steps and runs them; this file only decides which route a CoD4 input takes, checks the inputs,
// and lists the exact zonetool command lines, and reads the converter log afterwards.
//
// Routes (docs/research/43_cod4_port.md sections 1, 2, 4 and 7):
//   Route A, BuiltFastFile: a built CoD4 zone (.ff, header "IWffu100", IW3 in game.cpp). One command, the converter
//     unlinks and converts it: zonetool -game iw3 -path <CoD4> dumpzone <zone> --target iw8 --map mp_<id> ...
//     Output lands in <working folder>\output\<map>\ (zone_commands.cpp:107, convert.cpp:2656).
//   Route B, RadiantMap: a CoD4 source .map. The editor compiles it to an extraction folder (compile_iw8, the same
//     writer the editor uses for its own maps), then buildextraction writes the zones (compile_panel.cpp:389-407).
//
// Command lines copied from the editor's own launch (src/qtapp/compile_panel.cpp:390-407) and the research command.
#include <string>
#include <vector>

namespace iw8r {

enum class PortSource { BuiltFastFile, RadiantMap, Unknown };

struct PortInput {
    std::string path;        // the CoD4 .ff (route A) or the CoD4 .map (route B)
    std::string mapName;     // target map id, mp_ plus lower-case letters, digits or '_', 4 to 15 characters in all
    std::string cod4Root;    // CoD4 install folder (route A: -path for dumpzone, required there)
    std::string zonetoolExe; // x64-zt zonetool.exe, required
    std::string outDir;      // folder the wizard writes into (route A: becomes the working folder, output in outDir\output)
    // Beyond the brief (research 43 section 7.1 lists them as inputs): the Replay exe for --replay (both routes) and the
    // OpenAssetTools Unlinker for --unlinker (route A). Empty is allowed; planPort then warns or errors as described there.
    std::string replayExe;
    std::string unlinkerExe;
};

struct PortStep {
    std::string title;
    // Program to run. Empty for a step done inside the editor (route B's compile to an extraction folder): the dialog
    // runs the editor's own compile for that step, and must not start a process for it.
    std::string program;
    std::vector<std::string> args;  // exact argument list, one entry per token (no shell quoting)
    std::string workDir;
    std::string explains;           // one plain sentence for the step list
};

struct PortPlan {
    PortSource source = PortSource::Unknown;
    std::vector<PortStep> steps;        // empty whenever errors is not empty
    std::vector<std::string> warnings;  // the plan can run; these are things the mapper should know
    std::vector<std::string> errors;    // the plan cannot run
};

// By extension, case-insensitive. ".map" is RadiantMap. ".ff" is BuiltFastFile when its first 8 bytes are "IWffu100"
// or when the file cannot be opened (planPort then reports it missing). A readable .ff with another header, or one
// shorter than 8 bytes, is Unknown. Anything else is Unknown.
PortSource detectPortSource(const std::string& path);

// Validates the input and lists the steps of the route for its source. Checks: the input file exists; mapName passes the
// converter's rule (isMapId in convert.cpp: "mp_" prefix, 4 to 15 characters, [a-z0-9_]); zonetoolExe is set; outDir is
// set; route A needs cod4Root; route B needs replayExe. Warnings: route A without replayExe or unlinkerExe (the converter
// then finds the Unlinker by IW8_ZONETOOL_UNLINKER, MW120R_UNLINKER, or PATH), and route B's lack of baked light.
PortPlan planPort(const PortInput& in);

// The converter's messages (x64-zt, src/convert/iw3_iw8, zt::info/zt::warn), matched by substring so a level prefix does
// not matter. Formats quoted in the code:
//   "iw3: ledger <zone> <type>: <fate>=<n> <fate>=<n> ..."        (iw3_fastfile.cpp:2136-2143) fate "converted" is counted
//   "iw3: ledger: <n> <type> declaration(s) of the map zone are UNEXPLAINED: ..." (iw3_fastfile.cpp:2149) not counted, not missing
//   "iw3: image '<name>' is missing; using '<x>' from another compile" (iw3_render_assets.cpp:1333) missing: <name>
//   "iw3: <n> physics preset(s) have no exported payload; their models stay static: <a>, <b>" (iw3_fastfile.cpp:10808) missing: n
//   "iw3: <n> declared source FX graph(s) have no exported payload; first: <name>" (iw3_fastfile.cpp:10813) missing: n
//   "iw3: small_glass shard model export missing: <a>, <b>" (iw3_fastfile.cpp:11144) missing: one per name
// The log lines with "error" (any case) or with "IW3 entity model assets are absent" / "not found in common_mp.ff"
// (the converter's throws, iw3_fastfile.cpp:10760-10770, 11277) make ok false.
struct PortLogSummary {
    int missingAssets = 0;
    int convertedAssets = 0;
    std::vector<std::string> missing;  // names as the log writes them, in log order
    bool ok = false;                   // no missing assets, at least one converted, no error line
};
PortLogSummary summarizePortLog(const std::string& logText);

}  // namespace iw8r
