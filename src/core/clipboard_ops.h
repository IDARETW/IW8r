// IW8 Radiant Suite - copy / paste of map items as text (roadmap 2.28): the system clipboard carries an iwmap 4 text, so
// items move between two open maps (two editor windows) or out to a text editor and back.
//   Copy: a point entity, or a brush entity with every primitive selected, is copied whole (keys + primitives);
//   selected primitives of worldspawn, and of brush entities only partly selected, go into the worldspawn block.
//   Paste: worldspawn primitives join the document's worldspawn, every other entity is added as a new one, all in ONE
//   undo step, in place (the same coordinates); the pasted items become the selection.
#pragma once

#include <string>

#include "document.h"

namespace iw8r {

// The selection as map text ("" when nothing is selected). *items receives the number of entities + primitives.
std::string selectionToClipboardText(const Document& doc, size_t* items = nullptr);

// Pastes map text; returns the number of entities + primitives added (0 and *err when the text is not a map or has
// nothing to paste).
size_t pasteClipboardText(Document& doc, const std::string& text, std::string* err = nullptr);

}  // namespace iw8r
