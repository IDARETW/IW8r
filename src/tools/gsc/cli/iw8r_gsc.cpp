// iw8r-gsc: the IW8 GSC compiler of this project (gsc-tool 1.6.2 by xensik, src/tools/gsc; GPL-3, see LICENSE there).
//
// Command line compatible with what the build uses: `iw8r-gsc -m comp -g iw8 -s pc [-d] [-w <dir>] [-z] <file.gsc>...`
// writes compiled/iw8/<name>.gscbin (or, with -z, compiled/iw8/<name>.cgsc and .cgsc.stack, gsc-tool's zonetool
// layout) under the current folder. Includes are read from the working folder (-w, default the current one), as
// gsc-tool reads them. Output and messages match gsc-tool's (tools/gsc_parity in docs/PLAN_OWN_TOOLS.md).

#include "../api/iw8r_gsc.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool readFile(const fs::path& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

void saveFile(const fs::path& p, const std::vector<std::uint8_t>& data) {
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

int usage() {
    std::cerr << "usage: iw8r-gsc -m comp -g iw8 -s pc [-d] [-z] [-w <dir>] <file.gsc>...\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, game, system;
    bool dev = false, zonetool = false;
    fs::path workdir = fs::current_path();
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "-m" || a == "--mode") && i + 1 < argc) mode = argv[++i];
        else if ((a == "-g" || a == "--game") && i + 1 < argc) game = argv[++i];
        else if ((a == "-s" || a == "--system") && i + 1 < argc) system = argv[++i];
        else if ((a == "-w" || a == "--workdir") && i + 1 < argc) workdir = fs::path{argv[++i]};
        else if (a == "-d" || a == "--dev") dev = true;
        else if (a == "-z" || a == "--zonetool") zonetool = true;
        else if (!a.empty() && a[0] == '-') return usage();
        else inputs.emplace_back(a);
    }
    if (mode != "comp" || game != "iw8" || system != "pc" || inputs.empty()) return usage();

    int failures = 0;
    for (const fs::path& file : inputs) {
        std::vector<std::uint8_t> source;
        if (!readFile(file, source)) {
            std::cerr << "[ERROR]: couldn't open file " << file.string() << " at " << file.generic_string() << "\n";
            ++failures;
            continue;
        }
        const auto out = iw8r::gsc::compileIw8Script(file.string(), source, workdir, dev, zonetool);
        if (!out.ok) {
            std::cerr << out.error << " at " << file.generic_string() << "\n";
            ++failures;
            continue;
        }
        const fs::path rel = fs::path{"iw8"} / file.filename().replace_extension(zonetool ? ".cgsc" : ".gscbin");
        if (zonetool) {
            fs::path path = fs::path{"compiled"} / rel;
            saveFile(path, out.bytecode);
            saveFile(path.replace_extension(".cgsc.stack"), out.stack);
            std::cout << "compiled " << rel.generic_string() << "\n";
        } else {
            saveFile(fs::path{"compiled"} / rel, out.gscbin);
            std::cout << "compiled " << rel.generic_string() << "\n";
            if (dev) {
                saveFile(fs::path{"compiled"} / "developer_maps" / fs::path{rel}.replace_extension(".gscmap"), out.devmap);
                std::cout << "saved developer map " << rel.generic_string() << "\n";
            }
        }
    }
    return failures ? 1 : 0;
}
