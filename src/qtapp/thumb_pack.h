// IW8 Radiant - full thumbnail packs (roadmap 4.13).
//
// `IW8Radiant.exe --thumb-pack materials|models [--limit N] [--batch N]` makes a thumbnail of every buildable stock
// material (colour image) or model (rendered mesh) of the catalog, skipping what the availability audit marked
// missing / grey, into <AppLocalData>/thumbs/<kind>.full.pack. The exports go to a scratch cache root that is wiped after
// every batch, so the run never fills the disk. Names already in the pack are skipped, so a stopped run resumes. The
// browsers read the full pack when they have no export of their own (thumb_store.h, ThumbStore::getAny).
#pragma once

#include <QString>

namespace iw8r::qt {

int runThumbPack(const QString& kind, int limit, int batch);

}  // namespace iw8r::qt
