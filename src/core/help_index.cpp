#include "help_index.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "leak_check.h"

namespace iw8r {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Occurrences of term in text: whole-word hits count 2, partial 1.
double termScore(const std::string& text, const std::string& term) {
    double s = 0;
    for (size_t pos = text.find(term); pos != std::string::npos; pos = text.find(term, pos + term.size())) {
        const bool startOk = pos == 0 || !isWordChar(text[pos - 1]);
        const bool endOk = pos + term.size() >= text.size() || !isWordChar(text[pos + term.size()]);
        s += startOk && endOk ? 2.0 : 1.0;
    }
    return s;
}

}  // namespace

std::vector<HelpSection> splitMarkdownSections(const std::string& document, const std::string& markdown) {
    std::vector<HelpSection> out;
    std::vector<std::string> stack;  // heading titles by level (index = level - 1)
    HelpSection cur{document, "", document, ""};
    bool fenced = false;
    std::istringstream in(markdown);
    std::string line;
    auto flush = [&] {
        if (!cur.title.empty() || !trim(cur.body).empty()) out.push_back(cur);
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.rfind("```", 0) == 0) fenced = !fenced;
        size_t level = 0;
        while (!fenced && level < t.size() && t[level] == '#') ++level;
        if (!fenced && level >= 1 && level <= 6 && level < t.size() && t[level] == ' ') {
            flush();
            const std::string title = trim(t.substr(level));
            stack.resize(level - 1);
            stack.push_back(title);
            std::string path = document;
            for (const std::string& h : stack)
                if (!h.empty()) path += " > " + h;
            cur = HelpSection{document, title, path, ""};
            continue;
        }
        cur.body += line + "\n";
    }
    flush();
    return out;
}

std::vector<HelpHit> searchHelp(const std::vector<HelpSection>& sections, const std::string& query, size_t limit) {
    std::vector<std::string> terms;
    {
        std::istringstream q(lower(query));
        std::string t;
        while (q >> t) terms.push_back(t);
    }
    std::vector<HelpHit> hits;
    if (terms.empty()) return hits;
    for (size_t i = 0; i < sections.size(); ++i) {
        const std::string title = lower(sections[i].title), body = lower(sections[i].body);
        double score = 0;
        bool all = true;
        for (const std::string& t : terms) {
            const double ts = termScore(title, t), bs = termScore(body, t);
            if (ts == 0 && bs == 0) {
                all = false;
                break;
            }
            score += ts * 5 + std::min(bs, 20.0);
        }
        if (!all) continue;
        // Snippet around the first body match of the first term.
        std::string snippet;
        const size_t at = body.find(terms.front());
        const std::string& raw = sections[i].body;
        if (at != std::string::npos) {
            const size_t from = at > 60 ? at - 60 : 0;
            snippet = raw.substr(from, 160);
        } else {
            snippet = raw.substr(0, 160);
        }
        for (char& c : snippet)
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        hits.push_back({i, score, trim(snippet)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const HelpHit& a, const HelpHit& b) { return a.score > b.score; });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

std::vector<FirstMapStep> firstMapSteps(const MapFile& map, bool compiled, bool deployed) {
    size_t worldBrushes = 0, lights = 0, tdm = 0, allies = 0, axis = 0, dom = 0, props = 0;
    bool sky = false, skyname = false;
    for (const Entity& e : map.entities) {
        const std::string c = e.classname();
        if (e.isWorldspawn()) {
            if (const std::string* s = e.value("skyname"); s && !s->empty()) skyname = true;
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) {
                    ++worldBrushes;
                    for (const auto& f : p->faces)
                        if (lower(f.material()) == "sky" || lower(f.material()).find("/sky") != std::string::npos) sky = true;
                }
        }
        lights += c == "light" || c == "light_spot" || c == "light_omni";
        tdm += c == "mp_tdm_spawn";
        allies += c == "mp_tdm_spawn_allies_start";
        axis += c == "mp_tdm_spawn_axis_start";
        dom += c == "trigger_radius" && e.value("script_gameobjectname") && *e.value("script_gameobjectname") == "dom";
        props += c == "misc_model" || c == "misc_prefab" || c == "script_model";
    }
    std::vector<FirstMapStep> s;
    s.push_back({"Floor and walls", "Draw brushes in the 2D view (drag with the left button); give them a stock material from the Materials dock.",
                 "", worldBrushes >= 5});
    s.push_back({"Sky", "Put a brush with the `sky` material over the play space and pick a sky in Compile > Sun & Sky.", "", sky && skyname});
    bool sealed = false;
    if (worldBrushes >= 5) {
        const LeakResult leak = checkLeak(map, 32, 8'000'000);
        sealed = leak.error.empty() && leak.sealed;
    }
    s.push_back({"Sealed", "Players must not be able to leave: Tools > Check Map reports a leak and where it is.", "CheckMap", sealed});
    s.push_back({"Spawns", "At least two mp_tdm_spawn and one allies / axis start each; Tools > Generate Spawns makes a full set.",
                 "GenerateSpawns", tdm >= 2 && allies >= 1 && axis >= 1});
    s.push_back({"Light", "Add a light (Entity > light) or rely on the sun; Ctrl+L previews the lighting.", "", lights >= 1});
    s.push_back({"Props", "Place stock models (Models dock) or prefabs (Prefabs dock: doors, windows, cover).", "", props >= 1});
    s.push_back({"Game mode", "Domination needs three trigger_radius flags (Primitives > Game Modes).", "", dom >= 3});
    s.push_back({"Build", "Compile > Compile Map (F7); read the warnings in the Compile dock.", "CompileMap", compiled});
    s.push_back({"Play", "F5 builds, deploys and starts the map in Replay, also from a closed game (or Compile > Deploy, then a private match lobby you host).", "BuildDeployLoad", deployed});
    return s;
}

}  // namespace iw8r
