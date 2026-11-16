// IW8 Radiant (Qt shell) - the body of a detached worker thread. An exception that leaves a std::thread ends the
// process (std::terminate -> abort): on Verdansk an allocation that failed in a terrain-layer or lightmap worker took
// the editor down (2026-11-13). guardedWorker runs the body and logs such an exception instead.
#pragma once

#include <exception>
#include <new>
#include <utility>

#include <QString>

#include "editor_log.h"

namespace iw8r::qt {

template <class F>
auto guardedWorker(const char* file, int line, F body) {
    return [file, line, body = std::move(body)]() mutable {
        const auto where = [&] {
            const char* base = file;
            for (const char* p = file; *p; ++p)
                if (*p == '/' || *p == '\\') base = p + 1;
            return QString("%1:%2").arg(base).arg(line);
        };
        try {
            body();
        } catch (const std::bad_alloc&) {
            EditorLog::instance().add(LogLevel::Error, "memory",
                                      "A background task ran out of memory and stopped (" + where() +
                                          "); close other programs or lower the streamed world radius in the settings");
        } catch (const std::exception& e) {
            EditorLog::instance().add(LogLevel::Error, "worker", "A background task stopped (" + where() + "): " + QString::fromUtf8(e.what()));
        } catch (...) {
            EditorLog::instance().add(LogLevel::Error, "worker", "A background task stopped (" + where() + ")");
        }
    };
}

}  // namespace iw8r::qt
