import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/game_modes.h', [
 ('''struct GameModePreset {''', '''// Rules a mode adds to its game type (roadmap 13.6, research 91): the map script runs after the game type's main
// (VERIFIED order), so a map can change how the stock game type plays. Applied to every player at each spawn, a frame
// after the game type's own spawn handling: max health, move speed scale, one weapon instead of the loadout, and a
// clip that refills itself. Carried by the map (worldspawn iw8r_mode_rules); 0 / "" / false = unchanged.
struct ModeRules {
    int health = 0;          // 1 .. 1000
    double speed = 0;        // move speed scale 0.2 .. 3
    std::string weapon;      // a weapon name (a-z, 0-9, _; 1 to 64), e.g. iw8_pi_golf21_mp
    bool infiniteAmmo = false;
    bool empty() const { return health == 0 && speed == 0 && weapon.empty() && !infiniteAmmo; }
    bool operator==(const ModeRules& o) const {
        return health == o.health && speed == o.speed && weapon == o.weapon && infiniteAmmo == o.infiniteAmmo;
    }
};

struct GameModePreset {'''),
 ('''    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;
    bool builtin = false;
};''', '''    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;
    ModeRules rules;
    bool builtin = false;
};'''),
 ('''// The worldspawn iw8r_br words of a preset''', '''// "health=50; speed=1.2; weapon=iw8_pi_golf21_mp; ammo=infinite" (the worldspawn iw8r_mode_rules form) and back; a bad
// or unknown rule is dropped and named in `notes`.
std::string modeRulesText(const ModeRules& rules);
ModeRules parseModeRules(const std::string& text, std::vector<std::string>* notes = nullptr);

// The worldspawn iw8r_br words of a preset'''),
])

patch('src/core/game_modes.cpp', [
 ('''std::string gameModePresetJson(const GameModePreset& p) {''', '''std::string modeRulesText(const ModeRules& r) {
    std::string out;
    const auto add = [&](const std::string& part) { out += (out.empty() ? "" : "; ") + part; };
    if (r.health > 0) add("health=" + std::to_string(r.health));
    if (r.speed > 0) {
        std::ostringstream ss;
        ss << r.speed;
        add("speed=" + ss.str());
    }
    if (!r.weapon.empty()) add("weapon=" + r.weapon);
    if (r.infiniteAmmo) add("ammo=infinite");
    return out;
}

ModeRules parseModeRules(const std::string& text, std::vector<std::string>* notes) {
    ModeRules r;
    std::string part;
    std::istringstream in(text);
    const auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(" \\t\\r\\n"), b = s.find_last_not_of(" \\t\\r\\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    while (std::getline(in, part, ';')) {
        part = trim(part);
        if (part.empty()) continue;
        const size_t eq = part.find('=');
        std::string name = trim(part.substr(0, eq)), value = eq == std::string::npos ? std::string() : trim(part.substr(eq + 1));
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
        char* end = nullptr;
        if (name == "health") {
            const long v = std::strtol(value.c_str(), &end, 10);
            if (!value.empty() && *end == 0 && v >= 1 && v <= 1000) r.health = int(v);
            else if (notes) notes->push_back("rule health=" + value + ": a number 1 to 1000");
        } else if (name == "speed") {
            const double v = std::strtod(value.c_str(), &end);
            if (!value.empty() && *end == 0 && v >= 0.2 && v <= 3.0) r.speed = v;
            else if (notes) notes->push_back("rule speed=" + value + ": a scale 0.2 to 3");
        } else if (name == "weapon") {
            const bool ok = !value.empty() && value.size() <= 64 &&
                            std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
            if (ok) r.weapon = value;
            else if (notes) notes->push_back("rule weapon=" + value + ": a weapon name of a-z, 0-9 and _");
        } else if (name == "ammo") {
            if (value == "infinite") r.infiniteAmmo = true;
            else if (notes) notes->push_back("rule ammo=" + value + ": only ammo=infinite");
        } else if (notes) {
            notes->push_back("unknown rule '" + name + "' (health, speed, weapon, ammo)");
        }
    }
    return r;
}

std::string gameModePresetJson(const GameModePreset& p) {'''),
 ('''            ", \\"no_gulag\\": " + (p.brNoGulag ? "true" : "false") + ", \\"skip_drop\\": " + (p.brSkipDrop ? "true" : "false") + "}\\n}\\n";
    return json;''', '''            ", \\"no_gulag\\": " + (p.brNoGulag ? "true" : "false") + ", \\"skip_drop\\": " + (p.brSkipDrop ? "true" : "false") + "}";
    if (!p.rules.empty()) json += ",\\n  \\"rules\\": " + quote(modeRulesText(p.rules));
    return json + "\\n}\\n";'''),
 ('''        p.brNoCircle = flag("no_circle"), p.brNoGulag = flag("no_gulag"), p.brSkipDrop = flag("skip_drop");
    }
    out = std::move(p);''', '''        p.brNoCircle = flag("no_circle"), p.brNoGulag = flag("no_gulag"), p.brSkipDrop = flag("skip_drop");
    }
    if (const std::string rules = str("rules"); !rules.empty()) {
        std::vector<std::string> notes;
        p.rules = parseModeRules(rules, &notes);
        if (!notes.empty()) return fail(notes.front());
    }
    out = std::move(p);'''),
])

patch('src/core/map_script.h', [
 ('''    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;
    std::vector<std::string> solidTargetnames;''', '''    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;
    // Game mode rules carried by the map (worldspawn iw8r_mode_rules, game_modes.h ModeRules; research 91: the map
    // script runs after the game type's main): applied to every player a frame after each spawn.
    ModeRules modeRules;
    std::vector<std::string> solidTargetnames;'''),
])

patch('src/core/map_script.cpp', [
 ('''    if (extras.brSkipDrop) s += "    level.skipprematchdropspawn = 1;\\n";
''', '''    if (extras.brSkipDrop) s += "    level.skipprematchdropspawn = 1;\\n";
    if (!extras.modeRules.empty()) s += "    level thread iw8r_mode_rules();\\n";
'''),
 ('''    if (!extras.userScript.empty()) s += "\\n// ---- map script (worldspawn iw8r_script) ----\\n" + extras.userScript + "\\n";''',
  '''    if (!extras.modeRules.empty()) {
        const ModeRules& r = extras.modeRules;
        s += "\\n// Game mode rules (worldspawn iw8r_mode_rules: " + modeRulesText(r) +
             "): every player, a frame after each spawn (after the game type's own spawn handling).\\n"
             "iw8r_mode_rules()\\n{\\n    for (;;)\\n    {\\n        level waittill(\\"connected\\", player);\\n"
             "        player thread iw8r_mode_rules_player();\\n    }\\n}\\n"
             "\\niw8r_mode_rules_player()\\n{\\n    self endon(\\"disconnect\\");\\n    for (;;)\\n    {\\n"
             "        self waittill(\\"spawned_player\\");\\n        waitframe();\\n        if (!isalive(self))\\n            continue;\\n";
        if (r.health > 0) s += "        self.maxhealth = " + std::to_string(r.health) + ";\\n        self.health = " + std::to_string(r.health) + ";\\n";
        if (r.speed > 0) s += "        self setmovespeedscale(" + num(r.speed) + ");\\n";
        if (!r.weapon.empty() && validModeDvarValue(r.weapon))
            s += "        weapon = makeweapon(\\"" + r.weapon + "\\");\\n        self takeallweapons();\\n        self giveweapon(weapon);\\n"
                 "        self switchtoweaponimmediate(weapon);\\n";
        if (r.infiniteAmmo) s += "        self thread iw8r_mode_ammo();\\n";
        s += "    }\\n}\\n";
        if (r.infiniteAmmo)
            s += "\\niw8r_mode_ammo()\\n{\\n    self endon(\\"disconnect\\");\\n    self endon(\\"death\\");\\n    for (;;)\\n    {\\n"
                 "        wait 0.5;\\n        weapon = self getcurrentweapon();\\n        if (isdefined(weapon))\\n"
                 "            self setweaponammoclip(weapon, weaponclipsize(weapon));\\n    }\\n}\\n";
    }
    if (!extras.userScript.empty()) s += "\\n// ---- map script (worldspawn iw8r_script) ----\\n" + extras.userScript + "\\n";'''),
])

patch('src/core/compile_iw8.cpp', [
 ('''            if (const std::string* br = worldspawn->value("iw8r_br")) {
                parseModeBr(''', '''            if (const std::string* rules = worldspawn->value("iw8r_mode_rules")) {
                std::vector<std::string> notes;
                extras.modeRules = parseModeRules(*rules, &notes);
                for (const std::string& n : notes) warn.push_back("iw8r_mode_rules: " + n);
                if (!extras.modeRules.empty())
                    warn.push_back("game mode rules applied by the map script at every spawn (" + modeRulesText(extras.modeRules) +
                                   "; research 91; not seen in game)");
            }
            if (const std::string* br = worldspawn->value("iw8r_br")) {
                parseModeBr('''),
])

patch('src/qtapp/main_window_game_modes.cpp', [
 ('''    if (br.empty()) ctx_.doc.removeKey(id, "iw8r_br");
    else ctx_.doc.setKeyValue(id, "iw8r_br", br);
''', '''    if (br.empty()) ctx_.doc.removeKey(id, "iw8r_br");
    else ctx_.doc.setKeyValue(id, "iw8r_br", br);
    const std::string rules = modeRulesText(preset.rules);
    if (rules.empty()) ctx_.doc.removeKey(id, "iw8r_mode_rules");
    else ctx_.doc.setKeyValue(id, "iw8r_mode_rules", rules);
'''),
 ('''    ctx_.documentEdited(QString("Game mode '%1' is carried by the map: %2 dvar(s)%3 are set by the map script at start")
                            .arg(qs(preset.title)).arg(preset.dvars.size()).arg(br.empty() ? QString() : " and the Warzone switches (" + qs(br) + ")"));''',
  '''    ctx_.documentEdited(QString("Game mode '%1' is carried by the map: %2 dvar(s)%3 are set by the map script at start%4")
                            .arg(qs(preset.title)).arg(preset.dvars.size()).arg(br.empty() ? QString() : " and the Warzone switches (" + qs(br) + ")")
                            .arg(rules.empty() ? QString() : "; rules at every spawn: " + qs(rules)));'''),
 ('''    form->addRow("Warzone on a small map", brRow);
''', '''    form->addRow("Warzone on a small map", brRow);
    auto* rulesEdit = new QLineEdit(&dlg);
    rulesEdit->setObjectName("gameModeRules");
    rulesEdit->setPlaceholderText("health=50; speed=1.2; weapon=iw8_pi_golf21_mp; ammo=infinite   (applied at every spawn)");
    form->addRow("Rules", rulesEdit);
'''),
 ('''        p.dvars = parseModeDvars(dvarEdit->toPlainText().replace('\\n', ';').toStdString(), &notes);
''', '''        p.dvars = parseModeDvars(dvarEdit->toPlainText().replace('\\n', ';').toStdString(), &notes);
        p.rules = parseModeRules(rulesEdit->text().toStdString(), &notes);
'''),
 ('''        skipDrop->setChecked(m.brSkipDrop);
        del->setEnabled(!m.builtin);''', '''        skipDrop->setChecked(m.brSkipDrop);
        rulesEdit->setText(qs(modeRulesText(m.rules)));
        del->setEnabled(!m.builtin);'''),
 ('''"\\"Carry in this map\\" makes the map's own script set them at every start. Not seen in game.");''',
  '''"\\"Carry in this map\\" makes the map's own script set them at every start. Rules change how the game type plays "
                  "(the map's script runs after the game type's own start, research 91); they work only carried in the map. Not seen in game.");'''),
])
print('ok')
