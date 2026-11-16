// The project's IW8 GSC compiler entry point (gsc-tool 1.6.2 by xensik; GPL-3, see src/tools/gsc/LICENSE).
// compile_file and fs_read follow gsc-tool's src/tool/main.cpp for IW8 PC server scripts.

#include "iw8r_gsc.h"

#include "xsk/stdinc.hpp"
#include "xsk/gsc/engine/iw8.hpp"
#include "xsk/utils/file.hpp"
#include "xsk/utils/zlib.hpp"

#include <cstring>
#include <unordered_map>

namespace iw8r::gsc {

namespace fs = std::filesystem;
using namespace std::literals;
using namespace xsk;
using namespace xsk::gsc;

namespace {

struct Reader {
    fs::path workdir;
    std::unordered_map<std::string, std::vector<u8>> files;

    auto read(context const* ctx, std::string const& name) -> std::pair<buffer, std::vector<u8>> {
        auto path = workdir / fs::path{name};
        const auto* bin_ext = ctx->instance() == xsk::gsc::instance::client ? ".cscbin" : ".gscbin";
        const auto* gsc_ext = ctx->instance() == xsk::gsc::instance::client ? ".csc" : ".gsc";
        const auto* gsh_ext = ".gsh";
        if (!utils::file::exists(path)) {
            auto const name_noext = path.replace_extension("").string();
            if (auto id = ctx->token_id(name_noext); id > 0) path = fs::path{std::to_string(id) + bin_ext};
            if (!utils::file::exists(path)) path = fs::path{name_noext + bin_ext};
        }
        auto data = utils::file::read(path);
        if (path.extension().string() == bin_ext || (path.extension().string() != gsh_ext && path.extension().string() != gsc_ext)) {
            asset s;
            s.deserialize(data);
            auto stk = utils::zlib::decompress(s.buffer, s.length);
            auto res = files.insert({path.filename().string(), std::move(s.bytecode)});
            if (res.second) return {{res.first->second.data(), res.first->second.size()}, stk};
            throw std::runtime_error("file read error");
        }
        return {{}, data};
    }
};

std::vector<std::uint8_t> bytes(const u8* data, size_t size) { return {data, data + size}; }

}  // namespace

CompileOutput compileIw8Script(const std::string& name, const std::vector<std::uint8_t>& source, const fs::path& workdir,
                               bool dev, bool zonetool) {
    CompileOutput out;
    Reader reader{workdir, {}};
    try {
        iw8::context ctx(xsk::gsc::instance::server);
        ctx.init(dev ? build::dev : build::prod,
                 [&reader](context const* c, std::string const& n) { return reader.read(c, n); });
        std::vector<u8> data = source;
        auto outasm = ctx.compiler().compile(name, data);
        auto outbin = ctx.assembler().assemble(*outasm);
        if (zonetool) {
            out.bytecode = bytes(std::get<0>(outbin).data, std::get<0>(outbin).size);
            out.stack = bytes(std::get<1>(outbin).data, std::get<1>(outbin).size);
        } else {
            asset script;
            script.name = "GSC"s;
            script.bytecode.resize(std::get<0>(outbin).size);
            std::memcpy(script.bytecode.data(), std::get<0>(outbin).data, script.bytecode.size());
            script.buffer.resize(std::get<1>(outbin).size);
            std::memcpy(script.buffer.data(), std::get<1>(outbin).data, script.buffer.size());
            script.buffer = utils::zlib::compress(script.buffer);
            script.length = static_cast<std::uint32_t>(std::get<1>(outbin).size);
            script.compressed_length = static_cast<std::uint32_t>(script.buffer.size());
            script.bytecode_length = static_cast<std::uint32_t>(script.bytecode.size());
            out.gscbin = script.serialize();
            if ((ctx.build() & build::dev_maps) != build::prod)
                out.devmap = bytes(std::get<2>(outbin).data, std::get<2>(outbin).size);
        }
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

}  // namespace iw8r::gsc
