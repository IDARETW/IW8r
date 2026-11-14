import io

# ROADMAP 4.27, "localized hint text" without scripting: a use trigger (trigger_use, trigger_use_touch) with the key
# "hint" shows that text to a player who stands in it. The build adds the text to the map's localized strings
# ("<MAP ID>/TRIGGER_HINT_<n>") and the generated map script shows it in a HUD element of that player while the
# trigger holds them. A map without the key gets the script it got before (the hint code is only reached when a
# trigger has one).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


H = 'src/core/map_script.h'
patch(H, '''        bool use = false;
        double damage = 0;       // per hit; 0 = none''', '''        bool use = false;
        // A use trigger's key "hint": the text shown to a player standing in it. hintRef is its localized string name
        // ("<MAP ID>/TRIGGER_HINT_<n>", set by assignTriggerHintRefs); the script shows a hint only when it is set.
        std::string hintText, hintRef;
        double damage = 0;       // per hit; 0 = none''')
patch(H, '''std::vector<MapScriptExtras::TriggerVolume> readTriggerVolumes(const MapFile& map);''',
      '''std::vector<MapScriptExtras::TriggerVolume> readTriggerVolumes(const MapFile& map);
// Names the hints of the volumes: the n-th volume with a hint text gets "<PREFIX IN CAPITALS>/TRIGGER_HINT_<n>" (n from
// 1, in the volumes' order, so two calls on the same map agree). Returns the name and text pairs, sorted by name.
std::vector<std::pair<std::string, std::string>> assignTriggerHintRefs(std::vector<MapScriptExtras::TriggerVolume>& volumes,
                                                                        const std::string& prefix);''')
C = 'src/core/map_script.cpp'
patch(C, '''        auto& v = volumeFor(name);
        v.use = v.use || cls.rfind("trigger_use", 0) == 0;''', '''        auto& v = volumeFor(name);
        v.use = v.use || cls.rfind("trigger_use", 0) == 0;
        // The hint of a use trigger: the first one of the entities that share the name. Line breaks and tabs become
        // spaces; 255 characters at most.
        if (cls.rfind("trigger_use", 0) == 0 && v.hintText.empty())
            if (const std::string* h = e.value("hint"); h && !h->empty()) {
                std::string text = h->substr(0, 255);
                for (char& c : text)
                    if (c == '\\n' || c == '\\r' || c == '\\t') c = ' ';
                v.hintText = text;
            }''')
patch(C, '''std::vector<FxPlacement> readFxPlacements(const MapFile& map, size_t* skipped) {''',
      '''std::vector<std::pair<std::string, std::string>> assignTriggerHintRefs(std::vector<MapScriptExtras::TriggerVolume>& volumes,
                                                                        const std::string& prefix) {
    std::string pre = prefix;
    for (char& c : pre) c = char(std::toupper(static_cast<unsigned char>(c)));
    std::vector<std::pair<std::string, std::string>> out;
    size_t n = 0;
    for (auto& v : volumes) {
        v.hintRef.clear();
        if (!v.use || v.hintText.empty()) continue;
        v.hintRef = pre + "/TRIGGER_HINT_" + std::to_string(++n);
        out.emplace_back(v.hintRef, v.hintText);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<FxPlacement> readFxPlacements(const MapFile& map, size_t* skipped) {''')
# The generated set-up: the hint's name on the trigger.
patch(C, '''            s += "    t = iw8r_trigger_add(\\"" + tv.targetname + "\\", " + (tv.use ? "1" : "0") + ", " + num(tv.damage) + ", " +
                 std::to_string(tv.damageIntervalMs) + ");\\n";''', '''            s += "    t = iw8r_trigger_add(\\"" + tv.targetname + "\\", " + (tv.use ? "1" : "0") + ", " + num(tv.damage) + ", " +
                 std::to_string(tv.damageIntervalMs) + ");\\n";
            // A hint (key "hint" of a use trigger): its localized string, shown while a player stands in the trigger.
            const bool refOk = !tv.hintRef.empty() && tv.hintRef.size() <= 120 &&
                               std::all_of(tv.hintRef.begin(), tv.hintRef.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '/'; });
            if (refOk) {
                s += "    t.hint = &\\"" + tv.hintRef + "\\";\\n    t.hintid = " + std::to_string(++hintCount) + ";\\n    level.iw8r_hints = 1;\\n";
            }''')
patch(C, '''        s += "\\n// Trigger volumes of the map, tested by this script (see iw8r_trigger).\\niw8r_trigger_setup()\\n{\\n    level.iw8r_trig = [];\\n";''',
      '''        s += "\\n// Trigger volumes of the map, tested by this script (see iw8r_trigger).\\niw8r_trigger_setup()\\n{\\n    level.iw8r_trig = [];\\n";
        size_t hintCount = 0;''')
# The runtime: the hint of the first trigger that holds the player, shown in that player's own HUD element.
patch(C, '''        foreach (p in level.players)
        {
            if (!isdefined(p) || !isalive(p))
                continue;
            foreach (t in level.iw8r_trig)
            {
                if (!iw8r_trigger_holds(t, p.origin))
                    continue;
                if (t.use && !p usebuttonpressed())
                    continue;''', '''        foreach (p in level.players)
        {
            if (!isdefined(p))
                continue;
            if (!isalive(p))
            {
                if (isdefined(level.iw8r_hints))
                    p iw8r_hint_show(undefined, 0);
                continue;
            }
            hint = undefined;
            hintid = 0;
            foreach (t in level.iw8r_trig)
            {
                if (!iw8r_trigger_holds(t, p.origin))
                    continue;
                if (isdefined(t.hint) && !isdefined(hint))
                {
                    hint = t.hint;
                    hintid = t.hintid;
                }
                if (t.use && !p usebuttonpressed())
                    continue;''')
patch(C, '''                t.proxy notify("trigger", p);
                if (isdefined(t.ent))
                    t.ent notify("trigger", p);
            }
        }
    }
}
)GSC";''', '''                t.proxy notify("trigger", p);
                if (isdefined(t.ent))
                    t.ent notify("trigger", p);
            }
            if (isdefined(level.iw8r_hints))
                p iw8r_hint_show(hint, hintid);
        }
    }
}

// The hint of a use trigger (its key "hint"), in the player's own HUD element while the trigger holds them. Only
// reached on a map that has such a trigger (level.iw8r_hints).
iw8r_hint_show(hint, hintid)
{
    if (!isdefined(hint))
    {
        if (isdefined(self.iw8r_hint))
        {
            self.iw8r_hint destroy();
            self.iw8r_hint = undefined;
            self.iw8r_hintid = 0;
        }
        return;
    }
    if (!isdefined(self.iw8r_hint))
    {
        h = newclienthudelem(self);
        h.x = 0;
        h.y = 110;
        h.alignx = "center";
        h.aligny = "middle";
        h.horzalign = "center";
        h.vertalign = "middle";
        h.fontscale = 1.2;
        h.alpha = 1;
        self.iw8r_hint = h;
        self.iw8r_hintid = 0;
    }
    if (self.iw8r_hintid != hintid)
    {
        self.iw8r_hint settext(hint);
        self.iw8r_hintid = hintid;
    }
}
)GSC";''')

# ---- the build: the hints join the map's text
K = 'src/core/compile_iw8.cpp'
patch(K, '''    // 4.27: the map's own text for its script.
    if (!opt.stringsFile.empty()) {
        std::string strText;
        if (readFile(opt.stringsFile, strText)) {
            std::vector<std::string> strWarnings;
            const std::vector<MapString> strings = parseMapStrings(strText, id, &strWarnings);
            for (const std::string& w : strWarnings) rep.warnings.push_back(w);
            if (!strings.empty()) {
                customFiles.push_back({root / "extracted" / "localize" / "strings.tsv", mapStringsTsv(strings)});
                for (const MapString& s : strings) rep.mapStrings.push_back(s.key);
                rep.warnings.push_back(std::to_string(strings.size()) + " text entr" + (strings.size() == 1 ? "y" : "ies") +
                                       " for the map script (&\\"" + strings.front().key + "\\"" + (strings.size() > 1 ? ", ..." : "") + ")");
            }
        }
    }''', '''    // 4.27: the map's own text: the entries of its text file for the script, and the hints of its use triggers.
    {
        std::vector<MapString> strings;
        std::string strText;
        if (!opt.stringsFile.empty() && readFile(opt.stringsFile, strText)) {
            std::vector<std::string> strWarnings;
            strings = parseMapStrings(strText, id, &strWarnings);
            for (const std::string& w : strWarnings) rep.warnings.push_back(w);
        }
        const size_t fromFile = strings.size();
        std::vector<MapScriptExtras::TriggerVolume> hintVolumes = readTriggerVolumes(map);
        size_t hints = 0;
        for (const auto& [key, text] : assignTriggerHintRefs(hintVolumes, id)) {
            const bool taken = std::any_of(strings.begin(), strings.end(), [&](const MapString& s) { return s.key == key; });
            if (taken) {
                rep.warnings.push_back("the text file has an entry " + key + ", which is the name of a trigger's hint; the trigger's hint is used");
                strings.erase(std::remove_if(strings.begin(), strings.end(), [&](const MapString& s) { return s.key == key; }), strings.end());
            }
            strings.push_back({key, text});
            ++hints;
        }
        std::sort(strings.begin(), strings.end(), [](const MapString& a, const MapString& b) { return a.key < b.key; });
        if (!strings.empty()) {
            customFiles.push_back({root / "extracted" / "localize" / "strings.tsv", mapStringsTsv(strings)});
            for (const MapString& s : strings) rep.mapStrings.push_back(s.key);
            if (fromFile)
                rep.warnings.push_back(std::to_string(fromFile) + " text entr" + (fromFile == 1 ? "y" : "ies") + " for the map script (&\\"" +
                                       strings.front().key + "\\"" + (strings.size() > 1 ? ", ..." : "") + ")");
            if (hints) rep.warnings.push_back(std::to_string(hints) + " use trigger(s) show a hint (key hint)");
        }
    }''')
patch(K, '''            extras.triggerVolumes = readTriggerVolumes(map);''', '''            extras.triggerVolumes = readTriggerVolumes(map);
            assignTriggerHintRefs(extras.triggerVolumes, id);  // the same names as in localize/strings.tsv''')
print('ok')
