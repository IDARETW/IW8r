#include "script_check.h"

#include <cctype>
#include <set>

namespace iw8r {

namespace {

// Names the generated map script defines itself (map_script.cpp, hot_channels.cpp, hud_fonts.cpp).
const std::set<std::string>& reservedNames() {
    static const std::set<std::string> names = {
        "main",
        "iw8r_place_fx", "iw8r_spawn_fx", "iw8r_repeat_fx",
        "iw8r_place_sounds", "iw8r_loop_sound", "iw8r_repeat_sound",
        "iw8r_make_solid",
        "iw8r_hot_update", "iw8r_hot_channels", "iw8r_hot_fx", "iw8r_hot_fx_get", "iw8r_hot_snd", "iw8r_hot_sun",
        "iw8r_sun_shadow_range",
        "iw8r_vision_volumes", "iw8r_vision_inside",
        "iw8r_trigger_setup", "iw8r_trigger_add", "iw8r_trigger_shot", "iw8r_trigger_shot_wait",
        "iw8r_trigger_box", "iw8r_trigger_plane", "iw8r_trigger", "iw8r_in_trigger",
        "iw8r_trigger_holds", "iw8r_trigger_poll", "iw8r_hint_show",
        "iw8r_link_visual", "iw8r_spin_pivot", "iw8r_spin_model", "iw8r_bob_model", "iw8r_nearest",
        "iw8r_mode_rules", "iw8r_mode_rules_player", "iw8r_mode_ammo",
        "iw8r_prefab_inst", "iw8r_pname",
        "iw8r_font_init", "iw8r_font_text", "iw8r_font_clear", "iw8r_font_label",
        "iw8r_anim_0", "iw8r_anim_1", "iw8r_anim_2",
        "iw8r_door_0", "iw8r_door_1",
        "iw8r_teleport_0", "iw8r_jump_0", "iw8r_damage_0", "iw8r_lift_0",
    };
    return names;
}

bool isReservedName(const std::string& name) {
    if (reservedNames().count(name)) return true;
    if (name.rfind("iw8rp_", 0) == 0) return true;
    for (const char* prefix : {"iw8r_anim_", "iw8r_door_", "iw8r_teleport_", "iw8r_jump_", "iw8r_damage_", "iw8r_lift_"}) {
        const std::string p(prefix);
        if (name.size() > p.size() && name.compare(0, p.size(), p) == 0) {
            bool allDigits = true;
            for (size_t i = p.size(); i < name.size(); ++i)
                allDigits = allDigits && std::isdigit(static_cast<unsigned char>(name[i]));
            if (allDigits) return true;
        }
    }
    return false;
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The text with comments and string contents blanked (newlines kept), so bracket and name scans see code only.
// Unterminated comments / strings are reported into `issues`.
std::string codeOnly(const std::string& text, std::vector<ScriptIssue>* issues) {
    std::string out = text;
    int line = 1;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') out[i++] = ' ';
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const int start = line;
            bool closed = false;
            while (i < text.size()) {
                if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                    out[i] = out[i + 1] = ' ';
                    i += 2;
                    closed = true;
                    break;
                }
                if (text[i] == '\n') ++line;
                else out[i] = ' ';
                ++i;
            }
            if (!closed && issues) issues->push_back({start, true, "comment opened with /* is never closed"});
        } else if (c == '"') {
            const int start = line;
            out[i++] = ' ';
            bool closed = false;
            while (i < text.size() && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] != '\n') {
                    out[i] = out[i + 1] = ' ';
                    i += 2;
                    continue;
                }
                if (text[i] == '"') {
                    out[i++] = ' ';
                    closed = true;
                    break;
                }
                out[i++] = ' ';
            }
            if (!closed && issues) issues->push_back({start, true, "string is not closed on this line"});
        } else {
            ++i;
        }
    }
    return out;
}

}  // namespace

std::vector<std::string> scriptFunctionNames(const std::string& text) {
    const std::string code = codeOnly(text, nullptr);
    std::vector<std::string> names;
    int depth = 0;
    for (size_t i = 0; i < code.size();) {
        const char c = code[i];
        if (c == '{') ++depth;
        else if (c == '}') depth = depth > 0 ? depth - 1 : 0;
        if (depth == 0 && identStart(c) && (i == 0 || !identChar(code[i - 1]))) {
            size_t j = i;
            while (j < code.size() && identChar(code[j])) ++j;
            size_t k = j;
            while (k < code.size() && std::isspace(static_cast<unsigned char>(code[k]))) ++k;
            if (k < code.size() && code[k] == '(') {
                // name ( ... ) {  at depth 0 is a definition.
                int paren = 0;
                size_t m = k;
                for (; m < code.size(); ++m) {
                    if (code[m] == '(') ++paren;
                    else if (code[m] == ')' && --paren == 0) break;
                }
                size_t b = m + 1;
                while (b < code.size() && std::isspace(static_cast<unsigned char>(code[b]))) ++b;
                if (m < code.size() && b < code.size() && code[b] == '{') names.push_back(code.substr(i, j - i));
            }
            i = j;
            continue;
        }
        ++i;
    }
    return names;
}

std::vector<ScriptIssue> checkUserScript(const std::string& text) {
    std::vector<ScriptIssue> issues;
    const std::string code = codeOnly(text, &issues);
    // Brackets.
    struct Open {
        char ch;
        int line;
    };
    std::vector<Open> stack;
    int line = 1;
    for (size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '\n') ++line;
        else if (c == '(' || c == '{' || c == '[') stack.push_back({c, line});
        else if (c == ')' || c == '}' || c == ']') {
            const char want = c == ')' ? '(' : c == '}' ? '{' : '[';
            if (stack.empty()) {
                issues.push_back({line, true, std::string("'") + c + "' has nothing to close"});
            } else if (stack.back().ch != want) {
                issues.push_back({line, true, std::string("'") + c + "' closes the '" + stack.back().ch + "' opened on line " +
                                                  std::to_string(stack.back().line)});
                stack.pop_back();
            } else {
                stack.pop_back();
            }
        } else if (c == '#') {
            size_t j = i + 1;
            while (j < code.size() && identChar(code[j])) ++j;
            const std::string word = code.substr(i + 1, j - i - 1);
            if (word == "include" || word == "using")
                issues.push_back({line, true, "#" + word + " cannot be used: this file is appended to the generated map script. Call "
                                                  "other scripts with their full path, e.g. scripts\\mp\\utility\\game::fn()"});
        }
    }
    for (const Open& o : stack) issues.push_back({o.line, true, std::string("'") + o.ch + "' is never closed"});
    // Names.
    bool hasMain = false;
    std::set<std::string> seen;
    for (const std::string& n : scriptFunctionNames(text)) {
        if (n == "user_main") hasMain = true;
        if (isReservedName(n))
            issues.push_back({0, true, n + "() is defined by the generated map script; rename yours"});
        if (!seen.insert(n).second) issues.push_back({0, true, n + "() is defined twice"});
    }
    if (!hasMain)
        issues.push_back({0, false, "no user_main(): the file is included in the build, but nothing starts it"});
    return issues;
}

std::string userScriptTemplate(const std::string& mapName) {
    return "// Map script for " + mapName +
           " (IW8 Radiant).\n"
           "// This file is appended to the generated map script; the build starts user_main() once the level has loaded.\n"
           "// Entities are found by their targetname. Script > Insert adds ready-made behaviours below.\n"
           "\n"
           "user_main()\n"
           "{\n"
           "    level endon(\"game_ended\");\n"
           "    wait 1;\n"
           "    // Example: a script_brushmodel with targetname \"door1\" that opens when a trigger_multiple\n"
           "    // with targetname \"door1_trigger\" is touched (Script > Insert > Sliding door):\n"
           "    // level thread iw8r_door(\"door1\", \"door1_trigger\", (0, 0, 96), 1.5, 3);\n"
           "}\n";
}

const std::vector<ScriptSnippet>& scriptSnippets() {
    static const std::vector<ScriptSnippet> snippets = {
        {"door", "Sliding door (opens on a trigger, closes again)",
         "// Sliding door: every entity named `doorName` moves by `offset` when `triggerName` is touched,\n"
         "// stays open `holdTime` seconds, then closes. In user_main():\n"
         "//     level thread iw8r_door(\"door1\", \"door1_trigger\", (0, 0, 96), 1.5, 3);\n"
         "iw8r_door(doorName, triggerName, offset, moveTime, holdTime)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    doors = getentarray(doorName, \"targetname\");\n"
         "    trig = getent(triggerName, \"targetname\");\n"
         "    if (!isdefined(trig) || doors.size == 0)\n"
         "        return;\n"
         "    for (;;)\n"
         "    {\n"
         "        trig waittill(\"trigger\");\n"
         "        foreach (door in doors)\n"
         "            door moveto(door.origin + offset, moveTime);\n"
         "        wait moveTime + holdTime;\n"
         "        foreach (door in doors)\n"
         "            door moveto(door.origin - offset, moveTime);\n"
         "        wait moveTime + 0.1;\n"
         "    }\n"
         "}\n"},
        {"rotating_door", "Rotating door (swings on a trigger)",
         "// Rotating door: `doorName` swings by `yaw` degrees around a pivot script_origin at `hingePos` when\n"
         "// `triggerName` is touched, then turns back (IW8 drops origin brushes; for a no-script door, set\n"
         "// iw8r_door and iw8r_hinge on the script_brushmodel). In user_main():\n"
         "//     level thread iw8r_swing_door(\"gate1\", \"gate1_trigger\", (128, 64, 0), 90, 1, 3);\n"
         "iw8r_swing_door(doorName, triggerName, hingePos, yaw, moveTime, holdTime)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    door = getent(doorName, \"targetname\");\n"
         "    trig = iw8r_trigger(triggerName);\n"
         "    if (!isdefined(door) || !isdefined(trig))\n"
         "        return;\n"
         "    pivot = spawn(\"script_origin\", hingePos);\n"
         "    door linkto(pivot);\n"
         "    for (;;)\n"
         "    {\n"
         "        trig waittill(\"trigger\");\n"
         "        pivot rotateyaw(yaw, moveTime);\n"
         "        wait moveTime + holdTime;\n"
         "        pivot rotateyaw(0 - yaw, moveTime);\n"
         "        wait moveTime + 0.1;\n"
         "    }\n"
         "}\n"},
        {"lift", "Lift / moving platform (travels between two heights)",
         "// Lift: `platformName` rises by `height`, waits, comes back, waits; forever. In user_main():\n"
         "//     level thread iw8r_lift(\"lift1\", 192, 3, 4);\n"
         "iw8r_lift(platformName, height, moveTime, waitTime)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    platform = getent(platformName, \"targetname\");\n"
         "    if (!isdefined(platform))\n"
         "        return;\n"
         "    for (;;)\n"
         "    {\n"
         "        platform movez(height, moveTime, moveTime * 0.25, moveTime * 0.25);\n"
         "        wait moveTime + waitTime;\n"
         "        platform movez(0 - height, moveTime, moveTime * 0.25, moveTime * 0.25);\n"
         "        wait moveTime + waitTime;\n"
         "    }\n"
         "}\n"},
        {"teleporter", "Teleporter (trigger sends the player to a script_origin)",
         "// Teleporter: a player touching `triggerName` is moved to the script_origin `destName` (its origin and angles).\n"
         "// In user_main():\n"
         "//     level thread iw8r_teleporter(\"tele1_trigger\", \"tele1_dest\");\n"
         "iw8r_teleporter(triggerName, destName)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    trig = getent(triggerName, \"targetname\");\n"
         "    dest = getent(destName, \"targetname\");\n"
         "    if (!isdefined(trig) || !isdefined(dest))\n"
         "        return;\n"
         "    for (;;)\n"
         "    {\n"
         "        trig waittill(\"trigger\", player);\n"
         "        if (!isdefined(player) || !isplayer(player))\n"
         "            continue;\n"
         "        player setorigin(dest.origin);\n"
         "        if (isdefined(dest.angles))\n"
         "            player setplayerangles(dest.angles);\n"
         "    }\n"
         "}\n"},
        {"jump_pad", "Jump pad (trigger launches the player)",
         "// Jump pad: a player touching `triggerName` is launched with `velocity`. In user_main():\n"
         "//     level thread iw8r_jump_pad(\"pad1_trigger\", (0, 0, 900));\n"
         "iw8r_jump_pad(triggerName, velocity)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    trig = getent(triggerName, \"targetname\");\n"
         "    if (!isdefined(trig))\n"
         "        return;\n"
         "    for (;;)\n"
         "    {\n"
         "        trig waittill(\"trigger\", player);\n"
         "        if (isdefined(player) && isplayer(player))\n"
         "            player setvelocity(velocity);\n"
         "        wait 0.25;\n"
         "    }\n"
         "}\n"},
        {"spinner", "Spinning object (fan, sign, radar dish)",
         "// Spinner: every entity named `name` turns one full circle every `seconds`. In user_main():\n"
         "//     level thread iw8r_spin(\"fan1\", 4);\n"
         "iw8r_spin(name, seconds)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    ents = getentarray(name, \"targetname\");\n"
         "    for (;;)\n"
         "    {\n"
         "        foreach (ent in ents)\n"
         "            ent rotateyaw(360, seconds);\n"
         "        wait seconds;\n"
         "    }\n"
         "}\n"},
        {"timed_fx", "Timed effect (plays an effect at an origin every N seconds)",
         "// Timed effect: plays `fxName` at the script_origin `originName` every `interval` seconds. In user_main():\n"
         "//     level thread iw8r_timed_fx(\"vfx/core/expl/spark_death\", \"sparks1\", 6);\n"
         "iw8r_timed_fx(fxName, originName, interval)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    fx = loadfx(fxName);\n"
         "    at = getent(originName, \"targetname\");\n"
         "    if (!isdefined(at))\n"
         "        return;\n"
         "    for (;;)\n"
         "    {\n"
         "        playfx(fx, at.origin);\n"
         "        wait interval;\n"
         "    }\n"
         "}\n"},
        {"hud_message", "HUD message and icon (on screen for a few seconds)",
         "// HUD message: shows `text` (and a stock HUD icon such as \"hud_icon_c4\") in the middle of every player's\n"
         "// screen for `seconds`. Stock icons from the always-loaded zones work; a custom image needs the converter to\n"
         "// carry its material (docs/research/36). In-game look NOT VERIFIED. In user_main(), the precache first (before\n"
         "// any wait; the build lists precached names in the level material table):\n"
         "//     precacheshader(\"hud_icon_c4\");\n"
         "//     level thread iw8r_hud_message(\"Capture the flags\", \"hud_icon_c4\", 5);\n"
         "iw8r_hud_message(text, icon, seconds)\n"
         "{\n"
         "    msg = newhudelem();\n"
         "    msg.x = 0;\n"
         "    msg.y = -60;\n"
         "    msg.alignx = \"center\";\n"
         "    msg.aligny = \"middle\";\n"
         "    msg.horzalign = \"center\";\n"
         "    msg.vertalign = \"middle\";\n"
         "    msg.fontscale = 1.5;\n"
         "    msg.alpha = 1;\n"
         "    msg settext(text);\n"
         "    pic = undefined;\n"
         "    if (isdefined(icon) && icon != \"\")\n"
         "    {\n"
         "        pic = newhudelem();\n"
         "        pic.x = 0;\n"
         "        pic.y = -110;\n"
         "        pic.alignx = \"center\";\n"
         "        pic.aligny = \"middle\";\n"
         "        pic.horzalign = \"center\";\n"
         "        pic.vertalign = \"middle\";\n"
         "        pic setshader(icon, 48, 48);\n"
         "    }\n"
         "    wait seconds;\n"
         "    msg destroy();\n"
         "    if (isdefined(pic))\n"
         "        pic destroy();\n"
         "}\n"},
        {"hud_font_countdown", "Countdown in the map's own font (from <map>.hud/fonts.txt)",
         "// A countdown written in the map's own font. List the font in \"<map name>.hud/fonts.txt\" beside the map:\n"
         "//     glyphs | digits | MyFont.ttf | 48 | #ffffff | 0123456789:\n"
         "// (a label is one picture of a line of text: label | title | MyFont.ttf | 64 | #ffcc00 | Welcome). The build draws\n"
         "// them into the map's own HUD pictures and adds iw8r_font_text / iw8r_font_label / iw8r_font_clear to the map\n"
         "// script. In-game look NOT VERIFIED. In user_main():\n"
         "//     level thread iw8r_font_countdown(\"digits\", 90, 20, 100, 40);\n"
         "iw8r_font_countdown(set, seconds, x, y, height)\n"
         "{\n"
         "    for (s = seconds; s >= 0; s--)\n"
         "    {\n"
         "        r = s % 60;\n"
         "        text = int(s / 60) + \":\";\n"
         "        if (r < 10)\n"
         "            text = text + \"0\";\n"
         "        text = text + r;\n"
         "        shown = iw8r_font_text(set, text, x, y, height);\n"
         "        wait 1;\n"
         "        iw8r_font_clear(shown);\n"
         "    }\n"
         "}\n"},
        {"trigger_message", "Trigger message (prints a line when a trigger is touched)",
         "// Trigger message: prints `text` to the player who touches `triggerName`. In user_main():\n"
         "//     level thread iw8r_trigger_message(\"hint1_trigger\", \"You found the secret room\");\n"
         "iw8r_trigger_message(triggerName, text)\n"
         "{\n"
         "    level endon(\"game_ended\");\n"
         "    trig = getent(triggerName, \"targetname\");\n"
         "    if (!isdefined(trig))\n"
         "        return;\n"
         "    for (;;)\n"
         "    {\n"
         "        trig waittill(\"trigger\", player);\n"
         "        if (isdefined(player) && isplayer(player))\n"
         "            player iprintlnbold(text);\n"
         "        wait 2;\n"
         "    }\n"
         "}\n"},
    };
    return snippets;
}

}  // namespace iw8r
