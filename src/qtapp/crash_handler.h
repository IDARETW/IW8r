// IW8 Radiant Suite - crash handler (roadmap 9.5): an unhandled exception writes
// <dir>/crash-<yyyyMMdd-HHmmss>.txt (exception code, address, symbolized stack with file:line from the PDB) and a
// minidump beside it, then lets Windows end the process. The editor shows the newest report on the next start.
#pragma once

#include <QString>

namespace iw8r::qt {

// Installs the handler; reports go to `dir` (created if needed).
void installCrashHandler(const QString& dir);
// Newest crash report written after `sinceMsecsEpoch` (0 = any), or "" when there is none.
QString newestCrashReport(const QString& dir, qint64 sinceMsecsEpoch = 0);
// The process's committed private memory in MB (0 when unknown): bench diagnosis of load steps.
size_t processCommitMb();

}  // namespace iw8r::qt
