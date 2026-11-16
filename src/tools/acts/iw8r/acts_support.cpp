// The parts of ACTS's hashutils.cpp and tools/tools.cpp the Replay reader links against (ACTS by ate47, GPL-3 or MIT,
// used here under MIT: src/tools/acts/LICENSE.md). No hash dictionaries are loaded (the Replay names are strings), and
// tools register nowhere: iw8r-acts has only the fastfile command.

#include <includes.hpp>
#include <core/hashes/hash_store.hpp>

namespace hashutils {
    namespace {
        std::mutex& HashMutex() {
            static std::mutex m;
            return m;
        }
    } // namespace

    bool Add(const char* str, bool ignoreCol, bool iw, bool async, bool clone) {
        std::lock_guard lg{ HashMutex() };
        return core::hashes::Add(str, ignoreCol, iw, async, clone);
    }

    char* ExtractTmp(const char* type, uint64_t hash) { return core::hashes::ExtractTmp(type, hash); }
} // namespace hashutils

namespace tool {
    toolfunctiondata::toolfunctiondata(
        const char* name, const char* filename, size_t line, const char* category, const char* usage,
        const char* description, tool::toolfunctionnf func
    )
        : toolfunctiondata(
              name, filename, line, category, usage, description, nullptr,
              [func](Process& proc, int argc, const char* argv[]) -> int { return func(argc, argv); }
          ) {}

    toolfunctiondata::toolfunctiondata(
        const char* name, const char* filename, size_t line, const char* category, const char* usage,
        const char* description, const wchar_t* game, toolfunction func
    )
        : m_name(name), m_category(category), m_usage(usage), m_description(description), m_game(game), m_func(func),
          filename(filename), line(line) {}
} // namespace tool
