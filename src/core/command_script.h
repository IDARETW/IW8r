// IW8 Radiant Suite - command scripts (roadmap 9.10): a small line-based automation language over the Document,
// the same operations the web API and `iw8r edit` expose, plus a macro recorder the UI can feed.
//
// Grammar (one command per line; blank lines and `#` comments are skipped; `#` starts a comment only at the
// start of a token; a quoted argument is "..." with the escapes \" \\ \n \t \r, and a bare argument has no spaces
// or quotes):
//   select class <classname> [key value]     entities with the classname (optionally also key == value)
//   select key <key> <value>                 entities whose key equals value ("" = key present with any value)
//   select all | select none
//   select add class|key|all ...             the same selectors, added to the current selection
//   set <key> <value>                        on every selected entity
//   remove <key>                             on every selected entity
//   move <dx> <dy> <dz>                      translate the selection (not grid-snapped)
//   clone <dx> <dy> <dz>                     duplicate the selection offset by d; the copies are selected
//   delete                                   delete the selection
//   add <classname> <x> <y> <z> [key value]...   new point entity, selected afterwards
//   material <name>                          apply to the selected brushes and patches
//   array <dx> <dy> <dz> <count>             copies k * d for k = 1..count
//   align <x|y|z> <min|center|max>           align the selection bounds on an axis
//   replace-key <key> <old> <new> [substring] [selected]   key value find & replace (whole map unless `selected`)
//   undo | redo                              not allowed inside a one-step script
//
// Every command that changes the map is one undo step (named "Script <command>"). With oneUndoStep the whole script
// is one step, and a failing line undoes everything the script did so far, restoring the text and the selection.
// A command that needs a selection fails with "nothing selected" when there is none, and every command checks its
// arguments before it changes anything, so a failed command leaves no partial edit.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "document.h"
#include "geometry.h"

namespace iw8r {

struct ScriptResult {
    bool ok = true;
    int line = 0;             // 1-based line of the failing command; 0 when ok
    std::string message;      // "ran N commands" on success, "line N: reason" on failure
    size_t commands = 0;      // commands that ran successfully (a rolled-back script still reports the ones that ran)
};

// Runs `text` against `doc`. With oneUndoStep the script is one undo group and a failure rolls it back. With
// oneUndoStep false each command is its own undo step and the commands before a failing line stay applied.
// `log`, when given, receives one "line N: result" entry per executed command. Must not be called while the document
// is inside another beginGroup (the rollback could not isolate the script's step); that returns ok = false.
ScriptResult runCommandScript(Document& doc, const std::string& text, bool oneUndoStep, std::vector<std::string>* log = nullptr);

// Records the commands a UI operation performs, one per line, so the result can be replayed with runCommandScript.
class MacroRecorder {
public:
    // Stores `line` with surrounding whitespace trimmed. Empty lines and an exact repeat of the last line are ignored;
    // consecutive move lines merge into one move by the summed delta.
    void record(const std::string& line);
    // The recorded lines, each terminated by '\n' (empty when nothing was recorded).
    std::string text() const;
    size_t size() const { return lines_.size(); }
    void clear() { lines_.clear(); }

private:
    std::vector<std::string> lines_;
};

// Command line builders for the common operations. Arguments are quoted when needed, so any key or value round-trips.
// Numbers use formatNumber (shortest exact text).
std::string quoteArg(const std::string& s);                         // bare when safe, else "..." with escapes
std::string cmdMove(const Vec3& delta);                              // move dx dy dz
std::string cmdClone(const Vec3& delta);                             // clone dx dy dz
std::string cmdDelete();                                             // delete
std::string cmdSet(const std::string& key, const std::string& value);  // set key value
std::string cmdRemove(const std::string& key);                       // remove key
// select class <classname> targetname <targetname>. An empty targetname selects every entity of the class.
std::string cmdSelectEntity(const std::string& classname, const std::string& targetname);

}  // namespace iw8r
