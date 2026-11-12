// CoD4 port wizard, planning and validation (roadmap 11.9). See port_wizard.h and docs/research/43_cod4_port.md.
#include "port_wizard.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace iw8r {

namespace fs = std::filesystem;

namespace {

// IW3's unsigned zone header (x64-zt src/games/iw3/game.cpp:41, unsigned_magic). No signed IW3 header is listed there.
constexpr std::string_view kIw3ZoneMagic = "IWffu100";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// The converter's isMapId (src/convert/iw3_iw8/convert.cpp:118-129): "mp_" prefix, 4 to 15 characters, [a-z0-9_].
bool isMapName(const std::string& map) {
    if (map.size() < 4 || map.size() > 15 || map.compare(0, 3, "mp_") != 0) return false;
    return std::all_of(map.begin(), map.end(), [](unsigned char c) {
        return c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    });
}

// Splits "a, b, c" (the converter's JoinNames separator, iw3_fastfile.cpp:805-811). Empty items are dropped.
std::vector<std::string> splitNames(const std::string& list) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        const std::string item = trim(list.substr(start, comma - start));
        if (!item.empty()) out.push_back(item);
        start = comma + 1;
    }
    return out;
}

// Reads the decimal number that starts at `pos`. False when there are no digits.
bool readCount(const std::string& line, std::size_t pos, int& out) {
    if (pos >= line.size() || !std::isdigit(static_cast<unsigned char>(line[pos]))) return false;
    long long value = 0;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) {
        value = value * 10 + (line[pos] - '0');
        if (value > 1000000000LL) value = 1000000000LL;  // a log count, not a size: clamp rather than overflow
        ++pos;
    }
    out = static_cast<int>(value);
    return true;
}

// The number in "iw3: <n> <rest>" for the line that holds `phrase`. False when the line does not match.
bool countBeforePhrase(const std::string& line, const std::string& phrase, int& count) {
    const std::size_t at = line.find(phrase);
    if (at == std::string::npos) return false;
    const std::size_t head = line.find("iw3: ");
    if (head == std::string::npos || head > at) return false;
    return readCount(line, head + 5, count);
}

}  // namespace

PortSource detectPortSource(const std::string& path) {
    const fs::path p(path);
    const std::string ext = lower(p.extension().string());
    if (ext == ".map") return PortSource::RadiantMap;
    if (ext != ".ff") return PortSource::Unknown;

    std::ifstream in(p, std::ios::binary);
    if (!in) return PortSource::BuiltFastFile;  // missing or unreadable: planPort reports the missing file
    char magic[8]{};
    in.read(magic, sizeof magic);
    if (in.gcount() < static_cast<std::streamsize>(sizeof magic)) return PortSource::Unknown;
    return std::string_view(magic, sizeof magic) == kIw3ZoneMagic ? PortSource::BuiltFastFile : PortSource::Unknown;
}

PortPlan planPort(const PortInput& in) {
    PortPlan plan;
    plan.source = detectPortSource(in.path);

    if (in.path.empty())
        plan.errors.push_back("no input file: pick a CoD4 .ff zone or a CoD4 .map");
    else if (!fs::is_regular_file(fs::path(in.path)))
        plan.errors.push_back("input file does not exist: " + in.path);

    if (!isMapName(in.mapName))
        plan.errors.push_back("map name '" + in.mapName +
                              "' is invalid: use mp_ plus lower-case letters, digits or '_', 15 characters at most in all");
    if (in.zonetoolExe.empty()) plan.errors.push_back("zonetool path is not set (Tools > Tool Paths)");
    if (in.outDir.empty()) plan.errors.push_back("output folder is not set");

    if (plan.source == PortSource::Unknown && !in.path.empty() && plan.errors.empty())
        plan.errors.push_back("input is neither a built CoD4 .ff (header IWffu100) nor a .map: " + in.path);

    const fs::path outDir(in.outDir);
    if (plan.source == PortSource::BuiltFastFile) {
        if (in.cod4Root.empty()) plan.errors.push_back("route A needs the CoD4 install folder (dumpzone -path)");
        if (in.replayExe.empty())
            plan.warnings.push_back("no Replay exe: stock content from the Replay install (void table, IES, chyron) is not "
                                    "copied (research 43 section 2.3)");
        if (in.unlinkerExe.empty())
            plan.warnings.push_back("no Unlinker path: the converter looks for IW8_ZONETOOL_UNLINKER, MW120R_UNLINKER, then "
                                    "PATH (iw3_fastfile.cpp:390)");
    } else if (plan.source == PortSource::RadiantMap) {
        if (in.replayExe.empty()) plan.errors.push_back("route B needs the Replay exe (buildextraction --replay)");
        plan.warnings.push_back("a .map has no baked light: CoD4 lightmaps and the light grid are not in it, so surfaces "
                                "are unlit or atlas-only (research 43 section 4.5)");
        plan.warnings.push_back("materials with no entry in the mapper's library compile grey (research 43 section 4.4)");
    }

    if (!plan.errors.empty()) return plan;

    if (plan.source == PortSource::BuiltFastFile) {
        const std::string zone = fs::path(in.path).stem().string();
        PortStep step;
        step.title = "Convert the built CoD4 zone with the fork converter";
        step.program = in.zonetoolExe;
        step.args = {"-game", "iw3", "-path", in.cod4Root, "dumpzone", zone, "--target", "iw8",
                     "--map", in.mapName, "--style", "iw8"};
        if (!in.unlinkerExe.empty()) step.args.insert(step.args.end(), {"--unlinker", in.unlinkerExe});
        if (!in.replayExe.empty()) step.args.insert(step.args.end(), {"--replay", in.replayExe});
        step.args.insert(step.args.end(), {"--iw3_mode_assets", "0"});
        step.workDir = in.outDir;  // dumpzone always writes to "output" under the working folder
        step.explains = "Unlinks the CoD4 zone '" + zone + "' and writes the five IW8 zones and map.json to " +
                        (outDir / "output" / in.mapName).string();
        plan.steps.push_back(step);
        return plan;
    }

    // Route B.
    const std::string extraction = (outDir / "extraction").string();
    const std::string zones = (outDir / "zones").string();
    PortStep compile;
    compile.title = "Compile the CoD4 .map to an extraction folder (editor)";
    compile.program = "";  // done by the editor's own compile, not a process
    compile.workDir = in.outDir;
    compile.explains = "Writes the extraction the converter reads to " + extraction + " (complete marker, world, collision, "
                       "materials, images, xmodels, .ents)";
    plan.steps.push_back(compile);

    PortStep build;
    build.title = "Build the Replay zones from the extraction";
    build.program = in.zonetoolExe;
    build.args = {"-game", "iw8", "-path", fs::path(in.replayExe).parent_path().string(), "buildextraction", extraction,
                  "--map", in.mapName, "--replay", in.replayExe, "--output", zones, "--iw3_mode_assets", "off"};
    build.workDir = in.outDir;
    build.explains = "Writes the five Replay zones to " + zones + ". -path is the Replay folder (stock lookups only)";
    plan.steps.push_back(build);
    return plan;
}

PortLogSummary summarizePortLog(const std::string& logText) {
    PortLogSummary s;
    bool sawError = false;
    std::istringstream in(logText);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string lc = lower(line);
        if (lc.find("error") != std::string::npos || lc.find("iw3 entity model assets are absent") != std::string::npos ||
            lc.find("not found in common_mp.ff") != std::string::npos)
            sawError = true;

        // "iw3: ledger <zone> <type>: <fate>=<n> ..." Counts converted rows. The UNEXPLAINED line has a colon after
        // "ledger" ("iw3: ledger: <n> <type> declaration(s) ..."), so it is skipped here.
        const std::size_t ledger = line.find("iw3: ledger ");
        if (ledger != std::string::npos) {
            const std::string rest = line.substr(ledger + 12);
            if (!rest.empty() && rest[0] != ':') {
                std::istringstream tokens(rest);
                std::string token;
                while (tokens >> token) {
                    const std::size_t eq = token.find('=');
                    if (eq == std::string::npos || token.substr(0, eq) != "converted") continue;
                    int n = 0;
                    if (readCount(token, eq + 1, n)) s.convertedAssets += n;
                }
            }
            continue;
        }

        // "iw3: image '<name>' is missing; using '<x>' from another compile"
        const std::size_t image = line.find("iw3: image '");
        if (image != std::string::npos) {
            const std::size_t open = image + 12;
            const std::size_t close = line.find('\'', open);
            if (close != std::string::npos) {
                s.missing.push_back(line.substr(open, close - open));
                ++s.missingAssets;
            }
            continue;
        }

        // "iw3: <n> physics preset(s) have no exported payload; their models stay static: <a>, <b>"
        const std::string presetPhrase = "physics preset(s) have no exported payload; their models stay static: ";
        int n = 0;
        if (countBeforePhrase(line, "physics preset(s) have no exported payload", n)) {
            s.missingAssets += n;
            const std::size_t at = line.find(presetPhrase);
            if (at != std::string::npos)
                for (const std::string& name : splitNames(line.substr(at + presetPhrase.size())))
                    s.missing.push_back(name);
            continue;
        }

        // "iw3: <n> declared source FX graph(s) have no exported payload; first: <name>"
        if (countBeforePhrase(line, "declared source FX graph(s) have no exported payload", n)) {
            s.missingAssets += n;
            const std::string firstPhrase = "first: ";
            const std::size_t at = line.find(firstPhrase);
            if (at != std::string::npos) s.missing.push_back(trim(line.substr(at + firstPhrase.size())));
            continue;
        }

        // "iw3: small_glass shard model export missing: <a>, <b>"
        const std::string shardPhrase = "small_glass shard model export missing: ";
        const std::size_t shard = line.find(shardPhrase);
        if (shard != std::string::npos && line.find("iw3: ") != std::string::npos) {
            for (const std::string& name : splitNames(line.substr(shard + shardPhrase.size()))) {
                s.missing.push_back(name);
                ++s.missingAssets;
            }
        }
    }
    s.ok = s.missingAssets == 0 && s.convertedAssets > 0 && !sawError;
    return s;
}

}  // namespace iw8r
