// iw8r-acts: reads Replay 1.20 zones with the game's own loader, for IW8 Radiant (the ACTS fork's mw19replay handler,
// src/tools/acts; ACTS by ate47, GPL-3 or MIT, used here under MIT: LICENSE.md there).
//
// Command line as acts.exe takes it from the editor and the converter:
//   iw8r-acts [--noUpdater] [-l|--log <p|t|d|i|w|e>] fastfile -r mw19replay -g <Replay exe> [options] <file.ff>...
// Only the fastfile command is here (fastfile_replay.cpp); ACTS's other tools, UI and updater are not.

#include <includes.hpp>
#include <hook/error.hpp>

namespace fastfile {
    int FastfileReplay(int argc, const char* argv[]);
}

int main(int argc, const char* argv[]) {
    core::logs::setbasiclog(true);
    hook::error::InstallErrorHooks();

    // ACTS's global options come before the command (main.cpp HandleACTSOptions): the ones the callers use.
    std::vector<const char*> args{ argv[0] };
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char* arg = argv[i];
        if (!_strcmpi("--noUpdater", arg)) {
            continue;  // there is no updater
        } else if ((!strcmp("-l", arg) || !_strcmpi("--log", arg)) && i + 1 < argc) {
            switch (argv[++i][0]) {
            case 'p': core::logs::setlevel(core::logs::LVL_TRACE_PATH); break;
            case 't': core::logs::setlevel(core::logs::LVL_TRACE); break;
            case 'd': core::logs::setlevel(core::logs::LVL_DEBUG); break;
            case 'i': core::logs::setlevel(core::logs::LVL_INFO); break;
            case 'w': core::logs::setlevel(core::logs::LVL_WARNING); break;
            case 'e': core::logs::setlevel(core::logs::LVL_ERROR); break;
            default: LOG_ERROR("Bad log level '{}'", argv[i]); return tool::BASIC_ERROR;
            }
        } else {
            LOG_ERROR("Unknown option '{}'", arg);
            return tool::BASIC_ERROR;
        }
    }
    if (i >= argc || _strcmpi(argv[i], "fastfile")) {
        LOG_ERROR("usage: iw8r-acts [--noUpdater] fastfile -r mw19replay -g <Replay exe> [options] <file.ff>...");
        return tool::BASIC_ERROR;
    }
    for (; i < argc; i++) args.push_back(argv[i]);
    try {
        return fastfile::FastfileReplay((int)args.size(), args.data());
    } catch (std::exception& e) {
        LOG_ERROR("{}", e.what());
        return tool::BASIC_ERROR;
    }
}
