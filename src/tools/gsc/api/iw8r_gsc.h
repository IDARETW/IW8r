#pragma once
// The project's IW8 GSC compiler (gsc-tool 1.6.2 by xensik, src/tools/gsc; GPL-3, see LICENSE there).
//
// compileIw8Script compiles one IW8 PC server script the way `gsc-tool -m comp -g iw8 -s pc` does and returns the
// .gscbin file gsc-tool writes ("GSC" asset: zlib-compressed stack + bytecode), or the zonetool pair (.cgsc and
// .cgsc.stack). Includes are read from `workdir` as gsc-tool reads them: a source .gsc / .gsh, else a compiled .gscbin
// by name or by token id. Only compiling for IW8 PC is here: no decompiler, other games or machines.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace iw8r::gsc {

struct CompileOutput {
    bool ok = false;
    std::string error;                   // gsc-tool's message, e.g. "[ERROR]:compiler:x.gsc:14:5: expected identifier"
    std::vector<std::uint8_t> gscbin;    // the .gscbin file (zonetool = false)
    std::vector<std::uint8_t> bytecode;  // zonetool = true: the .cgsc file
    std::vector<std::uint8_t> stack;     // zonetool = true: the .cgsc.stack file
    std::vector<std::uint8_t> devmap;    // dev builds: compiled/developer_maps/iw8/<name>.gscmap
};

// `name` is the script's path as passed on the command line (it is the name in error messages).
CompileOutput compileIw8Script(const std::string& name, const std::vector<std::uint8_t>& source,
                               const std::filesystem::path& workdir, bool dev = false, bool zonetool = false);

}  // namespace iw8r::gsc
