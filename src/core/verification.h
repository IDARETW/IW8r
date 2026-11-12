// In-game verification matrix (roadmap 1.13).
// Parses docs/TEST_CHECKLIST.md into items, keeps dated results (build, date, ok / wrong / not-tested) in a JSON store,
// marks the checklist from the latest results, and derives the editor's "seen in game" marks per roadmap item.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace iw8r {

struct CheckItem {
    std::string section;  // "In the editor", "In game (after deploy)"
    std::string id;       // stable: slug of the bold name, or of the first 8 words of the text (suffix _2, _3 on repeats)
    std::string text;     // item text without the checkbox, the bold name and continuation indentation
    bool checked = false;
    std::vector<std::string> roadmapItems;  // "3.2", "5.13": written in the text, plus the mapping table in verification.cpp
};

struct CheckResult {
    std::string id;
    std::string result;  // ok | wrong | not-tested
    std::string build;   // editor build date or map id
    std::string date;    // YYYY-MM-DD
    std::string note;
};

// Every "- [ ]" / "- [x]" item under a "## " section, with indented continuation lines folded into the text.
std::vector<CheckItem> parseChecklist(const std::string& markdown);

// Store: {"schema": ..., "results": [ {id, result, build, date, note}, ... ]}, history kept in file order.
// A missing file loads as an empty history. Unknown result words are an error.
bool loadResults(const std::string& path, std::vector<CheckResult>& out, std::string& err);
bool saveResults(const std::string& path, const std::vector<CheckResult>& results, std::string& err);

// Latest result per id (latest date; on equal dates the later entry) decides the mark:
//   ok    -> "[x]"
//   wrong -> "[ ]" and " (wrong <date>: note)" appended to the item's last line
//   other -> the item is left as it was
// Only those marks change; the rest of the markdown is byte-identical (CRLF kept). Applying it twice gives the same text.
std::string updateChecklistMarks(const std::string& markdown, const std::vector<CheckResult>& results);

// Roadmap item -> "seen in game <date>" when every "In game" checklist item that names it has an ok latest result.
// The date is the newest of those results. Items with no in-game check get no entry.
std::map<std::string, std::string> roadmapSeenInGame(const std::vector<CheckItem>& items,
                                                     const std::vector<CheckResult>& results);

}  // namespace iw8r
