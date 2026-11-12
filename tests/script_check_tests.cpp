// Map script checks, template and behaviour snippets (6.11 / 6.12).
#include <algorithm>
#include <string>

#include "script_check.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
size_t errorsOf(const std::string& text) {
    const auto issues = checkUserScript(text);
    return size_t(std::count_if(issues.begin(), issues.end(), [](const ScriptIssue& i) { return i.error; }));
}
bool mentions(const std::string& text, const std::string& word) {
    for (const ScriptIssue& i : checkUserScript(text))
        if (i.message.find(word) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST(script_template_is_clean) {
    const std::string t = userScriptTemplate("mp_test");
    CHECK(checkUserScript(t).empty());
    const auto names = scriptFunctionNames(t);
    CHECK(names.size() == 1 && names[0] == "user_main");
}

TEST(script_check_brackets_strings_comments) {
    CHECK(errorsOf("user_main()\n{\n    x = (1 + 2;\n}\n") >= 1);
    CHECK(mentions("user_main()\n{\n    if (x)\n    {\n}\n", "never closed"));
    CHECK(mentions("user_main()\n{\n}\n}\n", "nothing to close"));
    CHECK(mentions("user_main()\n{\n    s = \"abc;\n}\n", "string is not closed"));
    CHECK(mentions("/* open\nuser_main()\n{\n}\n", "never closed"));
    // Brackets inside strings and comments do not count.
    CHECK(errorsOf("user_main()\n{\n    s = \"}{)(\"; // }}}\n    /* ((( */\n}\n") == 0);
    // The line of the problem is reported.
    const auto issues = checkUserScript("user_main()\n{\n    a = [1, 2;\n}\n");
    bool line3 = false;
    for (const auto& i : issues) line3 |= i.line == 3 || i.line == 4;
    CHECK(line3);
}

TEST(script_check_names) {
    CHECK(mentions("helper()\n{\n}\n", "no user_main"));
    CHECK(errorsOf("helper()\n{\n}\n") == 0);  // a warning, not an error
    CHECK(mentions("main()\n{\n}\nuser_main()\n{\n}\n", "generated map script"));
    CHECK(mentions("a()\n{\n}\na()\n{\n}\nuser_main()\n{\n}\n", "defined twice"));
    CHECK(mentions("#include scripts\\mp\\utility;\nuser_main()\n{\n}\n", "#include"));
    // Calls inside a body are not definitions.
    const auto names = scriptFunctionNames("user_main()\n{\n    level thread helper(1);\n    if (x) { other(); }\n}\nhelper(a)\n{\n}\n");
    CHECK(names.size() == 2 && names[0] == "user_main" && names[1] == "helper");
}

TEST(script_snippets_are_valid) {
    CHECK(scriptSnippets().size() >= 8);
    std::string all = userScriptTemplate("mp_test");
    for (const ScriptSnippet& s : scriptSnippets()) {
        const auto names = scriptFunctionNames(s.text);
        CHECK(names.size() == 1);
        CHECK(names[0].rfind("iw8r_", 0) == 0);
        CHECK(errorsOf(std::string("user_main()\n{\n}\n") + s.text) == 0);
        // The comment shows how to call it.
        CHECK(std::string(s.text).find("level thread " + names[0] + "(") != std::string::npos);
        all += "\n";
        all += s.text;
    }
    CHECK(checkUserScript(all).empty());  // all together: no clashes
}
