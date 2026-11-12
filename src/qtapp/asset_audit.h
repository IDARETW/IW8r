// IW8 Radiant Suite - stock asset availability audit (the "any listed asset builds" catalog).
//
// `IW8Radiant.exe --asset-audit materials|models|effects|images --out <status.tsv> [--limit N] [--batch N]` resolves catalog
// assets (effects and images, 4.12: one ACTS export per zone and batch, judged by the ACTS journal) with
// the editor's own caches (ACTS exports) against a scratch cache root that is wiped after every batch, so the audit
// never fills the disk. One line per asset is appended: name<TAB>status<TAB>reason, status ok | grey (material without
// a colour image: builds grey) | missing (not exportable). Names already in the file are skipped, so the audit resumes.
// The browsers read data/asset_catalog/iw8_<kind>_status.tsv to mark assets before anyone tries them.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace iw8r::qt {

// reverse: walk the catalog from the end (a second process can cover the other half into its own file).
int runAssetAudit(const QString& kind, const QString& outTsv, int limit, int batch, bool reverse = false);

// Shared with the thumbnail pack builder (thumb_pack.h): catalog names of a kind, the tool paths from IW8Radiant.ini,
// an event loop that runs until busy() is false, and the recorded audit status (name -> ok | grey | missing).
QStringList auditCatalogNames(const QString& kind);
QString auditToolPath(const QString& key);
void auditWaitIdle(const std::function<bool()>& busy, int timeoutMs);
QHash<QString, QString> auditStatus(const QString& kind);

}  // namespace iw8r::qt
