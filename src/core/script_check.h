#pragma once
// The map's own GSC (worldspawn iw8r_script, roadmap 6.3 / 6.11): checks the editor can make before a build, and the
// starting template. The file is appended to the generated map script, and the generated main() starts user_main().
#include <string>
#include <vector>

namespace iw8r {

struct ScriptIssue {
    int line = 0;  // 1-based; 0 = the whole file
    bool error = false;
    std::string message;
};

// Structure checks: unterminated comments and strings, unbalanced brackets (with the line that opened them), a missing
// user_main(), names that collide with the generated script, and #include / #using lines (the file is appended to the
// generated script, so they cannot be at the top).
std::vector<ScriptIssue> checkUserScript(const std::string& text);

// Function names defined at the top level of the script (`name(args) {`), in order.
std::vector<std::string> scriptFunctionNames(const std::string& text);

// A commented starting script with user_main() for a map called `mapName`.
std::string userScriptTemplate(const std::string& mapName);

// Ready-made behaviours for Script > Insert (6.12). Each is { id, title, GSC text defining one function and a comment
// that shows the call to put in user_main() }.
struct ScriptSnippet {
    const char* id;
    const char* title;
    const char* text;
};
const std::vector<ScriptSnippet>& scriptSnippets();

}  // namespace iw8r
