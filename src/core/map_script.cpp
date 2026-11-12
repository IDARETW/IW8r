#include "map_script.h"
#include "game_modes.h"
#include "hot_channels.h"
#include "brush_ops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>

namespace iw8r {

namespace {

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string num(double v) { return formatNumber(v); }
std::string vec(const Vec3& v) { return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ")"; }

bool safeEffectName(const std::string& s) {
    if (s.empty() || s.size() > 200) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '-' || c == '.')) return false;
    return true;
}

const char* const kTriggerGsc = R"GSC(
iw8r_trigger_add(name, use, dmg, interval)
{
    t = spawnstruct();
    t.name = name;
    t.use = use;
    t.dmg = dmg;
    t.interval = interval;
    t.proxy = spawnstruct();
    t.boxes = [];
    level.iw8r_trig[level.iw8r_trig.size] = t;
    return t;
}

iw8r_trigger_box(t, mins, maxs, radius)
{
    b = spawnstruct();
    b.mins = mins;
    b.maxs = maxs;
    b.radius = radius;
    b.normals = [];
    b.dists = [];
    t.boxes[t.boxes.size] = b;
    return b;
}

iw8r_trigger_plane(b, normal, dist)
{
    b.normals[b.normals.size] = normal;
    b.dists[b.dists.size] = dist;
}

// The object that is notified "trigger" (with the player) while a player is inside the map trigger `name`:
//     trig = iw8r_trigger("door1_trigger");
//     trig waittill("trigger", player);
iw8r_trigger(name)
{
    foreach (t in level.iw8r_trig)
    {
        if (t.name == name)
            return t.proxy;
    }
    return getent(name, "targetname");
}

// 1 while `player` is inside the map trigger `name`.
iw8r_in_trigger(player, name)
{
    foreach (t in level.iw8r_trig)
    {
        if (t.name == name && iw8r_trigger_holds(t, player.origin))
            return 1;
    }
    return 0;
}

// Damage (owner playtest 2, 2026-11-11): `player dodamage(...)` did nothing to the player. radiusdamage at the
// player's feet is what the converter's own out-of-bounds script uses, so the trigger and effect damage use it too.
// The player box (15 units around, 70 high, origin at the feet) against the shapes of the trigger.
iw8r_trigger_holds(t, o)
{
    foreach (b in t.boxes)
    {
        if (o[0] < b.mins[0] - 15 || o[0] > b.maxs[0] + 15)
            continue;
        if (o[1] < b.mins[1] - 15 || o[1] > b.maxs[1] + 15)
            continue;
        if (o[2] < b.mins[2] - 70 || o[2] > b.maxs[2])
            continue;
        if (b.radius > 0)
        {
            dx = o[0] - (b.mins[0] + b.maxs[0]) * 0.5;
            dy = o[1] - (b.mins[1] + b.maxs[1]) * 0.5;
            if (dx * dx + dy * dy > (b.radius + 15) * (b.radius + 15))
                continue;
        }
        c = o + (0, 0, 35);
        inside = 1;
        for (i = 0; i < b.normals.size; i += 1)
        {
            if (vectordot(b.normals[i], c) - b.dists[i] > 36)
                inside = 0;
        }
        if (inside)
            return 1;
    }
    return 0;
}

iw8r_trigger_poll()
{
    wait 0.05;
    foreach (t in level.iw8r_trig)
    {
        if (t.name != "")
            t.ent = getent(t.name, "targetname");
    }
    for (;;)
    {
        wait 0.05;
        if (!isdefined(level.players))
            continue;
        foreach (p in level.players)
        {
            if (!isdefined(p) || !isalive(p))
                continue;
            foreach (t in level.iw8r_trig)
            {
                if (!iw8r_trigger_holds(t, p.origin))
                    continue;
                if (t.use && !p usebuttonpressed())
                    continue;
                if (t.dmg > 0)
                {
                    if (!isdefined(p.iw8r_hurt) || gettime() >= p.iw8r_hurt)
                    {
                        p.iw8r_hurt = gettime() + t.interval;
                        radiusdamage(p.origin, 32, t.dmg, t.dmg);
                    }
                }
                t.proxy notify("trigger", p);
                if (isdefined(t.ent))
                    t.ent notify("trigger", p);
            }
        }
    }
}
)GSC";

}  // namespace

DoorHinge doorHingeOf(const Entity& e) {
    DoorHinge h;
    AABB box;
    for (const auto& it : e.items)
        if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush) box.add(brushBounds(brushFromPrimitive(*p)));
    if (!box.valid()) return h;
    const double dx = box.maxs.x - box.mins.x, dy = box.maxs.y - box.mins.y;
    const bool alongX = dx >= dy;  // the door's long side
    bool far = false;
    if (const std::string* hk = e.value("iw8r_hinge"); hk && !hk->empty()) {
        Vec3 p;
        if (std::sscanf(hk->c_str(), "%lf %lf %lf", &p.x, &p.y, &p.z) == 3) {
            // An explicit point: the free end is the end of the long side further from it.
            const double mid = alongX ? (box.mins.x + box.maxs.x) * 0.5 : (box.mins.y + box.maxs.y) * 0.5;
            far = (alongX ? p.x : p.y) > mid;
            h.pos = p;
            h.ok = true;
        } else if (lowerStr(*hk) == "far") {
            far = true;
        }
    }
    if (!h.ok) {
        h.pos = alongX ? Vec3{far ? box.maxs.x : box.mins.x, (box.mins.y + box.maxs.y) * 0.5, box.mins.z}
                       : Vec3{(box.mins.x + box.maxs.x) * 0.5, far ? box.maxs.y : box.mins.y, box.mins.z};
        h.ok = true;
    }
    // d: from the hinge toward the free end; a positive yaw (counter-clockwise seen from above) moves the free end
    // along (-d.y, d.x). normal is +Y for a door along X and +X for a door along Y.
    const double sign = far ? -1.0 : 1.0;
    const Vec3 d = alongX ? Vec3{sign, 0, 0} : Vec3{0, sign, 0};
    h.normal = alongX ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    h.swing = (-d.y) * h.normal.x + d.x * h.normal.y >= 0 ? 1.0 : -1.0;
    return h;
}

std::vector<MapScriptExtras::TriggerVolume> readTriggerVolumes(const MapFile& map) {
    std::vector<MapScriptExtras::TriggerVolume> out;
    const auto volumeFor = [&](const std::string& name) -> MapScriptExtras::TriggerVolume& {
        if (!name.empty())
            for (auto& v : out)
                if (v.targetname == name) return v;
        out.emplace_back();
        out.back().targetname = name;
        return out.back();
    };
    for (const auto& e : map.entities) {
        const std::string cls = lowerStr(e.classname());
        // An effect that hurts (owner playtest 2026-11-11: fire did no damage): fx_origin iw8r_damage
        // "<amount> [radius] [interval seconds]" is a nameless damage cylinder around the effect.
        if (cls == "fx_origin") {
            const std::string* dv = e.value("iw8r_damage");
            const auto o = e.origin();
            double amount = 0, radius = 48, interval = 1;
            if (!dv || !o || std::sscanf(dv->c_str(), "%lf %lf %lf", &amount, &radius, &interval) < 1 || amount <= 0 || radius <= 0) continue;
            MapScriptExtras::TriggerVolume v;
            v.damage = amount;
            v.damageIntervalMs = static_cast<int>(std::max(0.05, interval) * 1000 + 0.5);
            MapScriptExtras::TriggerVolume::Box b;
            b.mins = Vec3{o->x - radius, o->y - radius, o->z - 16};
            b.maxs = Vec3{o->x + radius, o->y + radius, o->z + radius};
            b.radius = radius;
            v.boxes.push_back(b);
            out.push_back(std::move(v));
            continue;
        }
        if (e.isWorldspawn() || cls.rfind("trigger_", 0) != 0) continue;
        const std::string* tn = e.value("targetname");
        const std::string name = tn && safeEffectName(*tn) && tn->find('/') == std::string::npos ? *tn : std::string();
        std::vector<MapScriptExtras::TriggerVolume::Box> boxes;
        if (cls == "trigger_radius") {
            const std::string* o = e.value("origin");
            const std::string* r = e.value("radius");
            const std::string* h = e.value("height");
            Vec3 org{0, 0, 0};
            if (!o || std::sscanf(o->c_str(), "%lf %lf %lf", &org.x, &org.y, &org.z) != 3) continue;
            const double radius = r ? std::atof(r->c_str()) : 64, height = h ? std::atof(h->c_str()) : 72;
            if (radius <= 0 || height <= 0) continue;
            MapScriptExtras::TriggerVolume::Box b;
            b.mins = Vec3{org.x - radius, org.y - radius, org.z};
            b.maxs = Vec3{org.x + radius, org.y + radius, org.z + height};
            b.radius = radius;
            boxes.push_back(b);
        } else {
            for (const auto& it : e.items) {
                const Primitive* p = it.prim();
                if (!p || p->kind != PrimKind::Brush) continue;
                const ConvexBrush cb = brushFromPrimitive(*p);
                const AABB bb = brushBounds(cb);
                if (!bb.valid()) continue;
                MapScriptExtras::TriggerVolume::Box b;
                b.mins = bb.mins;
                b.maxs = bb.maxs;
                bool axial = true;
                for (const auto& f : cb) {
                    const double m = std::max({std::fabs(f.plane.n.x), std::fabs(f.plane.n.y), std::fabs(f.plane.n.z)});
                    if (m < 0.9999) axial = false;
                }
                if (!axial)
                    for (const auto& f : cb) b.planes.push_back(f.plane);
                boxes.push_back(std::move(b));
            }
        }
        if (boxes.empty()) continue;
        auto& v = volumeFor(name);
        v.use = v.use || cls.rfind("trigger_use", 0) == 0;
        if (cls == "trigger_hurt" || cls == "trigger_damage") {
            const std::string* d = e.value("dmg");
            v.damage = d ? std::atof(d->c_str()) : 5;
            const std::string* sf = e.value("spawnflags");
            v.damageIntervalMs = sf && (std::atoi(sf->c_str()) & 16) ? 1000 : 50;
        }
        for (auto& b : boxes) v.boxes.push_back(std::move(b));
    }
    return out;
}

std::vector<FxPlacement> readFxPlacements(const MapFile& map, size_t* skipped) {
    std::vector<FxPlacement> out;
    size_t skip = 0;
    for (const auto& e : map.entities) {
        if (lowerStr(e.classname()) != "fx_origin") continue;
        const std::string* fx = e.value("fx");
        if (!fx || !safeEffectName(*fx)) {
            ++skip;
            continue;
        }
        FxPlacement p;
        p.effect = *fx;
        p.origin = e.origin().value_or(Vec3{});
        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &p.angles.x, &p.angles.y, &p.angles.z);
        if (const std::string* d = e.value("delay")) p.delay = std::atof(d->c_str());
        out.push_back(p);
    }
    if (skipped) *skipped = skip;
    return out;
}

std::string hotUpdateValue(unsigned seq, const std::string& targetname, const Vec3& origin, const Vec3& angles) {
    return std::to_string(seq) + " " + targetname + " " + num(origin.x) + " " + num(origin.y) + " " + num(origin.z) + " " + num(angles.x) +
           " " + num(angles.y) + " " + num(angles.z);
}

std::vector<SoundPlacement> readSoundPlacements(const MapFile& map, size_t* skipped) {
    std::vector<SoundPlacement> out;
    size_t skip = 0;
    for (const auto& e : map.entities) {
        // An fx_origin with a soundalias key carries its sound (owner playtest 2026-11-11: effects were silent): a loop
        // unless sound_loop is 0, in which case it repeats with the effect's delay.
        const std::string cls = lowerStr(e.classname());
        const bool onEffect = cls == "fx_origin";
        if (cls != "sound_emitter" && !onEffect) continue;
        const std::string* a = e.value("soundalias");
        if (onEffect && (!a || a->empty())) continue;
        if (!a || !safeEffectName(*a) || a->find('/') != std::string::npos) {
            ++skip;
            continue;
        }
        SoundPlacement p;
        p.alias = lowerStr(*a);
        p.origin = e.origin().value_or(Vec3{});
        if (onEffect) {
            const std::string* sl = e.value("sound_loop");
            p.loop = !(sl && *sl == "0");
        }
        if (const std::string* l = e.value("loop")) p.loop = *l == "1";
        if (const std::string* d = e.value("delay")) p.delay = std::atof(d->c_str());
        out.push_back(p);
    }
    if (skipped) *skipped = skip;
    return out;
}

std::string stockSoundList(const std::vector<SoundPlacement>& sounds) {
    std::map<std::string, bool> kinds;
    for (const auto& p : sounds) kinds.emplace(p.alias, p.loop);
    std::string out;
    for (const auto& [alias, loop] : kinds) out += alias + (loop ? " 1\n" : " 0\n");
    return out;
}

namespace {

// Prefab ids and placement ids go into function names and string literals of the generated script.
bool isPrefabScriptId(const std::string& id) {
    if (id.empty() || id.size() > 40) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool isPrefabPlacementName(const std::string& pid) {
    if (pid.empty() || pid.size() > 120) return false;
    for (char c : pid)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool isScriptIdentifier(const std::string& s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

}  // namespace

std::string prefabScriptFunctionName(const std::string& id, const std::string& prefix, const std::string& name) {
    if (!prefix.empty() && name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
        return "iw8rp_" + id + "_" + name.substr(prefix.size());
    return name;
}

std::string prefabScriptRenamed(const std::string& text, const std::string& prefix, const std::string& id) {
    if (prefix.empty()) return text;
    const auto identStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    const auto identChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::string out;
    out.reserve(text.size() + 64);
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const char c = text[i];
        size_t e = i + 1;
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {  // line comment
            e = text.find('\n', i);
            if (e == std::string::npos) e = n;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '*') {  // block comment
            e = text.find("*/", i + 2);
            e = e == std::string::npos ? n : e + 2;
        } else if (c == '"') {  // string literal
            while (e < n && text[e] != '"') e += text[e] == '\\' && e + 1 < n ? 2 : 1;
            if (e < n) ++e;
        } else if (identStart(c)) {
            while (e < n && identChar(text[e])) ++e;
            out += prefabScriptFunctionName(id, prefix, text.substr(i, e - i));
            i = e;
            continue;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {  // a number is not the start of an identifier
            while (e < n && (identChar(text[e]) || text[e] == '.')) ++e;
        }
        out.append(text, i, e - i);
        i = e;
    }
    return out;
}

std::string prefabScriptsError(const std::vector<MapScriptExtras::PrefabScript>& scripts) {
    std::map<std::string, std::string> seen;
    for (const auto& ps : scripts) {
        if (!isPrefabScriptId(ps.id))
            return "prefab script id '" + ps.id + "' (" + ps.source + ") is not 1-40 characters of a-z 0-9 _";
        const auto [it, fresh] = seen.emplace(ps.id, ps.source);
        if (!fresh)
            return "two prefab bundles use the id '" + ps.id + "': " + it->second + " and " + ps.source +
                   " (give one of them another id)";
    }
    return {};
}

std::string mapScriptGsc(const std::optional<ExpFog>& fog, const std::vector<FxPlacement>& fx,
                         const std::vector<SoundPlacement>& sounds, const MapScriptExtras& extras) {
    // Every map gets a script: like a stock map's main(), it starts with the load script (x64-zt translates
    // maps\mp\_load::main to iw3compat_load_main: exploders, destructibles, gameobjects of other modes removed,
    // out-of-bounds volume), so scripted entities behave as in stock maps.
    // One loadfx per distinct effect (the converter registers each literal in the level vfx table).
    std::map<std::string, size_t> index;
    std::vector<std::string> effects;
    for (const auto& p : fx)
        if (index.emplace(p.effect, effects.size()).second) effects.push_back(p.effect);
    // Prefab bundle scripts that can be generated: a good id, some text, the first entry of each id.
    std::vector<const MapScriptExtras::PrefabScript*> prefabScripts;
    for (const auto& ps : extras.prefabScripts) {
        bool dup = false;
        for (const auto* have : prefabScripts) dup |= have->id == ps.id;
        if (!dup && isPrefabScriptId(ps.id) && !ps.text.empty() && isScriptIdentifier(ps.entry)) prefabScripts.push_back(&ps);
    }

    std::string s = "// Generated by IW8 Radiant from the map (load script, worldspawn iw8r_fog, fx_origin and sound_emitter "
                    "entities).\nmain()\n{\n    maps\\mp\\_load::main();\n";
    if (extras.stockDoors) s += "    scripts\\engine\\scriptable_door::system_init();\n";
    for (const auto& [name, value] : extras.modeDvars)
        // "iw8r:" lets the name through the converter's dvar filter (iw3compat_setdvar drops every other name).
        if (validModeDvarName(name) && validModeDvarValue(value)) s += "    setdvar(\"iw8r:" + name + "\", \"" + value + "\");\n";
    if (extras.brNoCircle) s += "    level.br_circle_disabled = 1;\n    scripts\\mp\\gametypes\\br_gametypes::disablefeature(\"circle\");\n";
    if (extras.brNoGulag) s += "    level.usegulag = 0;\n";
    if (extras.brSkipDrop) s += "    level.skipprematchdropspawn = 1;\n";
    if (fog) {
        const double start = std::clamp(fog->start, 0.0, 20000.0), halfway = std::clamp(fog->halfway, 1.0, 70000.0);
        const auto c01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
        s += "    setExpFog(" + num(start) + ", " + num(halfway) + ", " + num(c01(fog->color.x)) + ", " + num(c01(fog->color.y)) + ", " +
             num(c01(fog->color.z)) + ");\n";
    }
    if (!fx.empty()) {
        for (size_t i = 0; i < effects.size(); ++i)
            s += "    level.iw8r_fx[" + std::to_string(i) + "] = loadfx(\"" + effects[i] + "\");\n";
        if (extras.hotUpdate) {  // the hot effect channel plays only effects loaded here, by name
            s += "    level.iw8r_hotfx = [];\n";
            for (size_t i = 0; i < effects.size(); ++i)
                s += "    level.iw8r_hotfx[\"" + effects[i] + "\"] = level.iw8r_fx[" + std::to_string(i) + "];\n";
        }
        s += "    level thread iw8r_place_fx();\n";
    }
    if (!sounds.empty()) s += "    level thread iw8r_place_sounds();\n";
    for (const std::string& t : extras.solidTargetnames)
        if (safeEffectName(t) && t.find('/') == std::string::npos) s += "    iw8r_make_solid(\"" + t + "\");\n";
    const bool triggers = !extras.triggerVolumes.empty();
    if (triggers) s += "    iw8r_trigger_setup();\n    level thread iw8r_trigger_poll();\n";
    else if (!extras.userScript.empty() || !extras.doors.empty() || !prefabScripts.empty()) s += "    level.iw8r_trig = [];\n";
    // Names the script only mentions, so the converter keeps those script_models as entities (a live script_model has
    // no player collision: the ladder model of Make Ladder must not stand in the way of its ladder brush).
    for (const auto& [vis, parent] : extras.visualLinks)
        if (safeEffectName(vis) && safeEffectName(parent) && vis.find('/') == std::string::npos && parent.find('/') == std::string::npos)
            s += "    level thread iw8r_link_visual(\"" + vis + "\", \"" + parent + "\");\n";
    if (!extras.keepNames.empty()) s += "    level.iw8r_keep = [];\n";
    {
        size_t k = 0;
        for (const std::string& n : extras.keepNames)
            if (safeEffectName(n) && n.find('/') == std::string::npos)
                s += "    level.iw8r_keep[" + std::to_string(k++) + "] = \"" + n + "\";\n";
    }
    if (!extras.userScript.empty()) s += "    level thread user_main();\n";
    if (extras.hotUpdate) s += "    level thread iw8r_hot_update();\n    level thread iw8r_hot_channels();\n";
    const bool shadowRange = extras.sunShadowRange[0] > 0 && extras.sunShadowRange[1] > 0;
    if (shadowRange) s += "    level thread iw8r_sun_shadow_range();\n";
    std::vector<MapScriptExtras::AnimPlacement> anims;
    for (const auto& a : extras.anims)
        if (safeEffectName(a.anim) && safeEffectName(a.targetname) && a.anim.find('/') == std::string::npos) anims.push_back(a);
    for (size_t i = 0; i < anims.size(); ++i) s += "    level thread iw8r_anim_" + std::to_string(i) + "();\n";
    std::vector<MapScriptExtras::VisionVolume> vols;
    for (const auto& v : extras.visionVolumes)
        if (safeEffectName(v.targetname) && safeEffectName(v.vision) && v.targetname.find('/') == std::string::npos) vols.push_back(v);
    const bool visionVolumes = !vols.empty() && safeEffectName(extras.baseVision);
    if (visionVolumes) s += "    level thread iw8r_vision_volumes();\n";
    std::vector<MapScriptExtras::Door> doors;
    for (const auto& d : extras.doors)
        if (safeEffectName(d.door) && safeEffectName(d.trigger) && d.door.find('/') == std::string::npos) doors.push_back(d);
    for (size_t i = 0; i < doors.size(); ++i) s += "    level thread iw8r_door_" + std::to_string(i) + "();\n";
    const auto plainName = [](const std::string& n) { return safeEffectName(n) && n.find('/') == std::string::npos; };
    std::vector<MapScriptExtras::Teleport> teleports;
    for (const auto& t : extras.teleports)
        if (plainName(t.trigger) && plainName(t.dest)) teleports.push_back(t);
    std::vector<MapScriptExtras::JumpPad> jumps;
    for (const auto& j : extras.jumpPads)
        if (plainName(j.trigger)) jumps.push_back(j);
    std::vector<MapScriptExtras::DamageZone> hurts;
    for (const auto& h : extras.damageZones)
        if (plainName(h.trigger)) hurts.push_back(h);
    std::vector<MapScriptExtras::Lift> lifts;
    for (const auto& l : extras.lifts)
        if (plainName(l.targetname)) lifts.push_back(l);
    for (size_t i = 0; i < teleports.size(); ++i) s += "    level thread iw8r_teleport_" + std::to_string(i) + "();\n";
    for (size_t i = 0; i < jumps.size(); ++i) s += "    level thread iw8r_jump_" + std::to_string(i) + "();\n";
    for (size_t i = 0; i < hurts.size(); ++i) s += "    level thread iw8r_damage_" + std::to_string(i) + "();\n";
    for (size_t i = 0; i < lifts.size(); ++i) s += "    level thread iw8r_lift_" + std::to_string(i) + "();\n";
    for (const auto& sp : extras.spinners)
        if (plainName(sp.targetname))
            s += "    level thread iw8r_spin_model(\"" + sp.targetname + "\", " + num(std::max(0.1, sp.seconds)) + ", " +
                 std::to_string(std::clamp(sp.axis, 0, 2)) + ");\n";
    for (const auto& bo : extras.bobbers)
        if (plainName(bo.targetname))
            s += "    level thread iw8r_bob_model(\"" + bo.targetname + "\", " + num(bo.height) + ", " +
                 num(std::max(0.1, bo.seconds)) + ");\n";
    // Prefab bundle scripts: the once function of each kind, then its entry function once per placement.
    for (const auto* ps : prefabScripts) {
        if (isScriptIdentifier(ps->once)) s += "    level thread " + prefabScriptFunctionName(ps->id, ps->prefix, ps->once) + "();\n";
        for (const auto& pl : ps->placements)
            if (isPrefabPlacementName(pl.pid))
                s += "    level thread " + prefabScriptFunctionName(ps->id, ps->prefix, ps->entry) + "(iw8r_prefab_inst(\"" + pl.pid +
                     "\", " + vec(pl.origin) + ", " + vec(pl.angles) + "));\n";
    }
    s += "}\n";
    if (triggers) {
        s += "\n// Trigger volumes of the map, tested by this script (see iw8r_trigger).\niw8r_trigger_setup()\n{\n    level.iw8r_trig = [];\n";
        for (const auto& tv : extras.triggerVolumes) {
            s += "    t = iw8r_trigger_add(\"" + tv.targetname + "\", " + (tv.use ? "1" : "0") + ", " + num(tv.damage) + ", " +
                 std::to_string(tv.damageIntervalMs) + ");\n";
            for (const auto& b : tv.boxes) {
                s += "    b = iw8r_trigger_box(t, " + vec(b.mins) + ", " + vec(b.maxs) + ", " + num(b.radius) + ");\n";
                for (const auto& pl : b.planes) s += "    iw8r_trigger_plane(b, " + vec(pl.n) + ", " + num(pl.d) + ");\n";
            }
        }
        s += "}\n";
        s += kTriggerGsc;
    } else if (!extras.userScript.empty() || !extras.doors.empty() || !prefabScripts.empty()) {
        s += "\n// The map has no trigger volumes: the name is looked up among the entities.\n"
             "iw8r_trigger(name)\n{\n    return getent(name, \"targetname\");\n}\n";
    }
    if (!extras.visualLinks.empty())
        s += "\n// The model that draws a moving brush model follows it.\n"
             "iw8r_link_visual(visName, parentName)\n{\n    wait 0.05;\n    vis = getent(visName, \"targetname\");\n"
             "    parent = getent(parentName, \"targetname\");\n    if (!isdefined(vis) || !isdefined(parent))\n        return;\n"
             "    vis linkto(parent);\n}\n";
    if (!extras.spinners.empty())
        s += "\n// script_model / script_brushmodel iw8r_spin: one turn every `seconds` (axis 0 yaw, 1 pitch, 2 roll).\n"
             "iw8r_spin_model(name, seconds, axis)\n{\n    wait 0.05;\n    ents = getentarray(name, \"targetname\");\n"
             "    if (ents.size == 0)\n        return;\n    for (;;)\n    {\n        foreach (ent in ents)\n        {\n"
             "            if (axis == 1)\n                ent rotatepitch(360, seconds);\n            else if (axis == 2)\n"
             "                ent rotateroll(360, seconds);\n            else\n                ent rotateyaw(360, seconds);\n"
             "        }\n        wait seconds;\n    }\n}\n";
    if (!extras.bobbers.empty())
        s += "\n// script_model / script_brushmodel iw8r_bob: up `height` and back down every `seconds`.\n"
             "iw8r_bob_model(name, height, seconds)\n{\n    wait 0.05;\n    ents = getentarray(name, \"targetname\");\n"
             "    if (ents.size == 0)\n        return;\n    half = seconds * 0.5;\n    for (;;)\n    {\n"
             "        foreach (ent in ents)\n            ent movez(height, half, half * 0.3, half * 0.3);\n        wait half;\n"
             "        foreach (ent in ents)\n            ent movez(0 - height, half, half * 0.3, half * 0.3);\n        wait half;\n"
             "    }\n}\n";
    // 6.12 behaviour threads. Each waits for its trigger, ignores non-players, and keeps a short per-player cooldown so
    // one touch does one thing.
    const auto trigHead = [](const std::string& fn, const std::string& comment, const std::string& trig) {
        return "\n// " + comment + "\n" + fn + "()\n{\n    wait 0.05;\n    trig = iw8r_trigger(\"" + trig +
               "\");\n    if (!isdefined(trig))\n        return;\n";
    };
    for (size_t i = 0; i < teleports.size(); ++i) {
        const auto& t = teleports[i];
        s += trigHead("iw8r_teleport_" + std::to_string(i), "Teleporter " + t.trigger + " -> " + t.dest + " (trigger iw8r_teleport)", t.trigger);
        s += "    dest = getent(\"" + t.dest + "\", \"targetname\");\n    if (!isdefined(dest))\n        return;\n"
             "    for (;;)\n    {\n        trig waittill(\"trigger\", user);\n"
             "        if (!isdefined(user) || !isplayer(user) || !isalive(user))\n            continue;\n"
             "        if (isdefined(user.iw8r_tp) && gettime() < user.iw8r_tp)\n            continue;\n"
             "        user.iw8r_tp = gettime() + 1000;\n        user setorigin(dest.origin);\n"
             "        user setplayerangles(dest.angles);\n    }\n}\n";
    }
    for (size_t i = 0; i < jumps.size(); ++i) {
        const auto& j = jumps[i];
        s += trigHead("iw8r_jump_" + std::to_string(i), "Jump pad " + j.trigger + " (trigger iw8r_jump)", j.trigger);
        s += "    for (;;)\n    {\n        trig waittill(\"trigger\", user);\n"
             "        if (!isdefined(user) || !isplayer(user) || !isalive(user))\n            continue;\n"
             "        if (isdefined(user.iw8r_jp) && gettime() < user.iw8r_jp)\n            continue;\n"
             "        user.iw8r_jp = gettime() + 500;\n        user setvelocity(" +
             vec(Vec3{j.velocity[0], j.velocity[1], j.velocity[2]}) + ");\n    }\n}\n";
    }
    for (size_t i = 0; i < hurts.size(); ++i) {
        const auto& h = hurts[i];
        const int ms = static_cast<int>(std::max(0.05, h.interval) * 1000 + 0.5);
        s += trigHead("iw8r_damage_" + std::to_string(i), "Damage zone " + h.trigger + " (trigger iw8r_damage)", h.trigger);
        s += "    for (;;)\n    {\n        trig waittill(\"trigger\", user);\n"
             "        if (!isdefined(user) || !isplayer(user) || !isalive(user))\n            continue;\n"
             "        if (isdefined(user.iw8r_dz) && gettime() < user.iw8r_dz)\n            continue;\n"
             "        user.iw8r_dz = gettime() + " + std::to_string(ms) + ";\n        radiusdamage(user.origin, 32, " + num(h.amount) + ", " + num(h.amount) +
             ");\n    }\n}\n";
    }
    for (size_t i = 0; i < lifts.size(); ++i) {
        const auto& l = lifts[i];
        const double t = std::max(0.05, l.moveTime), w = std::max(0.0, l.waitTime);
        s += "\n// Lift " + l.targetname + " (script_brushmodel iw8r_lift)\niw8r_lift_" + std::to_string(i) +
             "()\n{\n    wait 0.05;\n    lift = getent(\"" + l.targetname + "\", \"targetname\");\n    if (!isdefined(lift))\n"
             "        return;\n    bottom = lift.origin;\n    for (;;)\n    {\n        wait " + num(w) + ";\n"
             "        lift moveto(bottom + (0, 0, " + num(l.dz) + "), " + num(t) + ");\n        wait " + num(t + w) + ";\n"
             "        lift moveto(bottom, " + num(t) + ");\n        wait " + num(t) + ";\n    }\n}\n";
    }
    if (!extras.solidTargetnames.empty())
        s += "\niw8r_make_solid(name)\n{\n    ents = getentarray(name, \"targetname\");\n"
             "    foreach (ent in ents)\n        ent solid();\n}\n";
    if (extras.hotUpdate)
        s += "\n// Hot update (IW8 Radiant Live Link): dvar iw8r_hot = \"seq targetname x y z pitch yaw roll\".\n"
             "iw8r_hot_update()\n{\n    last = \"\";\n    for (;;)\n    {\n        wait 0.1;\n"
             "        v = getdvar(\"iw8r_hot\");\n        if (!isdefined(v) || v == \"\" || v == last)\n            continue;\n"
             "        last = v;\n        t = strtok(v, \" \");\n        if (t.size < 8)\n            continue;\n"
             "        ents = getentarray(t[1], \"targetname\");\n        foreach (ent in ents)\n        {\n"
             "            ent.origin = (float(t[2]), float(t[3]), float(t[4]));\n"
             "            ent.angles = (float(t[5]), float(t[6]), float(t[7]));\n        }\n    }\n}\n";
    if (extras.hotUpdate) s += "\n" + hotChannelsGsc();
    for (size_t i = 0; i < doors.size(); ++i) {
        const auto& d = doors[i];
        if (d.hinged) {
            // The door hangs on a pivot at its hinge and swings away from the player who uses it. A brush model turns
            // about its own origin (the map origin for an ordinary brush), so it is linked to a script_origin there.
            const std::string hinge = vec(Vec3{d.hinge[0], d.hinge[1], d.hinge[2]});
            const std::string normal = vec(Vec3{d.normal[0], d.normal[1], 0});
            s += "\n// Hinged door " + d.door + " (script_brushmodel iw8r_door), used at " + d.trigger + "\niw8r_door_" + std::to_string(i) +
                 "()\n{\n    wait 0.05;\n    door = getent(\"" + d.door + "\", \"targetname\");\n    trig = iw8r_trigger(\"" + d.trigger +
                 "\");\n    if (!isdefined(door) || !isdefined(trig))\n        return;\n    hinge = " + hinge +
                 ";\n    pivot = spawn(\"script_origin\", hinge);\n    door linkto(pivot);\n    for (;;)\n    {\n"
                 "        trig waittill(\"trigger\", user);\n        if (!isdefined(user) || !isplayer(user))\n            continue;\n"
                 "        dir = " + num(d.swing) + ";\n        if (vectordot(user.origin - hinge, " + normal + ") > 0)\n            dir = " +
                 num(-d.swing) + ";\n        pivot rotateyaw(" + num(d.yaw) + " * dir, " + num(d.moveTime) + ");\n        wait " +
                 num(d.moveTime + 0.4) + ";\n";
            if (d.toggle)
                s += "        trig waittill(\"trigger\", user);\n";
            else
                s += "        wait " + num(d.holdTime) + ";\n";
            s += "        pivot rotateyaw(0 - " + num(d.yaw) + " * dir, " + num(d.moveTime) + ");\n        wait " + num(d.moveTime + 0.4) +
                 ";\n    }\n}\n";
            continue;
        }
        const std::string open = d.rotate ? "door rotateyaw(" + num(d.yaw) + ", " + num(d.moveTime) + ");"
                                          : "door moveto(door.origin + " + vec(Vec3{d.offset[0], d.offset[1], d.offset[2]}) + ", " +
                                                num(d.moveTime) + ");";
        const std::string close = d.rotate ? "door rotateyaw(" + num(-d.yaw) + ", " + num(d.moveTime) + ");"
                                           : "door moveto(door.origin - " + vec(Vec3{d.offset[0], d.offset[1], d.offset[2]}) + ", " +
                                                 num(d.moveTime) + ");";
        s += "\n// Door " + d.door + " (script_brushmodel iw8r_door), opened by " + d.trigger + "\niw8r_door_" + std::to_string(i) +
             "()\n{\n    wait 0.05;\n    door = getent(\"" + d.door + "\", \"targetname\");\n    trig = iw8r_trigger(\"" + d.trigger +
             "\");\n    if (!isdefined(door) || !isdefined(trig))\n        return;\n    for (;;)\n    {\n"
             "        trig waittill(\"trigger\", user);\n        if (!isdefined(user) || !isplayer(user))\n            continue;\n"
             "        " + open + "\n        wait " + num(d.moveTime + d.holdTime) + ";\n        " + close + "\n        wait " +
             num(d.moveTime + 0.1) + ";\n    }\n}\n";
    }
    if (visionVolumes) {
        s += "\n// Vision / fog volumes (brush triggers with iw8r_vision): a player inside sees the volume's vision.\n"
             "iw8r_vision_volumes()\n{\n    wait 0.05;\n    vols = [];\n    names = [];\n    vnames = [];\n";
        for (size_t i = 0; i < vols.size(); ++i)
            s += "    vols[" + std::to_string(i) + "] = getent(\"" + vols[i].targetname + "\", \"targetname\");\n    vnames[" + std::to_string(i) + "] = \"" + vols[i].targetname + "\";\n    names[" +
                 std::to_string(i) + "] = \"" + vols[i].vision + "\";\n";
        s += "    for (;;)\n    {\n        wait 0.25;\n        if (!isdefined(level.players))\n            continue;\n"
             "        foreach (p in level.players)\n        {\n            if (!isdefined(p) || !isalive(p))\n                continue;\n"
             "            inside = \"\";\n            for (i = 0; i < vols.size; i += 1)\n            {\n"
             "                if (iw8r_vision_inside(p, vols[i], vnames[i]))\n                    inside = names[i];\n            }\n"
             "            if (!isdefined(p.iw8r_vis))\n                p.iw8r_vis = \"\";\n            if (inside == p.iw8r_vis)\n"
             "                continue;\n            p.iw8r_vis = inside;\n            if (inside == \"\")\n"
             "                p visionsetnakedforplayer(\"" + extras.baseVision + "\", 0.5);\n            else\n"
             "                p visionsetnakedforplayer(inside, 0.5);\n        }\n    }\n}\n";
        s += std::string("\niw8r_vision_inside(p, ent, name)\n{\n") +
             (triggers ? "    if (iw8r_in_trigger(p, name))\n        return 1;\n" : "") +
             "    if (isdefined(ent) && p istouching(ent))\n        return 1;\n    return 0;\n}\n";
    }
    for (size_t i = 0; i < anims.size(); ++i) {
        const auto& a = anims[i];
        s += "\n// " + a.targetname + ": stock animation " + a.anim + " (script_model iw8r_anim)\niw8r_anim_" + std::to_string(i) +
             "()\n{\n    wait 0.05;\n    ents = getentarray(\"" + a.targetname + "\", \"targetname\");\n    for (;;)\n    {\n"
             "        foreach (ent in ents)\n            ent scriptmodelplayanim(\"" + a.anim + "\");\n";
        if (a.loopSeconds > 0) s += "        wait " + num(a.loopSeconds) + ";\n    }\n}\n";
        else s += "        return;\n    }\n}\n";
    }
    if (shadowRange)
        s += "\n// Longer sun shadows (worldspawn iw8r_sun_shadow_range): every player once.\niw8r_sun_shadow_range()\n{\n"
             "    for (;;)\n    {\n        if (isdefined(level.players))\n        {\n            foreach (player in level.players)\n"
             "            {\n                if (!isdefined(player) || isdefined(player.iw8r_sm))\n                    continue;\n"
             "                player.iw8r_sm = 1;\n"
             "                player setclientdvar(\"sm_sunCascadeSizeMultiplier1\", \"" + num(extras.sunShadowRange[0]) + "\");\n"
             "                player setclientdvar(\"sm_sunCascadeSizeMultiplier2\", \"" + num(extras.sunShadowRange[1]) + "\");\n"
             "                player setclientdvar(\"sm_sunDistantShadows\", \"1\");\n"
             "            }\n        }\n        wait 1;\n    }\n}\n";
    if (!extras.userScript.empty()) s += "\n// ---- map script (worldspawn iw8r_script) ----\n" + extras.userScript + "\n";
    if (!prefabScripts.empty()) {
        s += "\n// Prefab bundles: what the entry function of a prefab script gets for one placement.\n"
             "iw8r_prefab_inst(pid, origin, angles)\n{\n    inst = spawnstruct();\n    inst.pid = pid;\n"
             "    inst.origin = origin;\n    inst.angles = angles;\n    return inst;\n}\n"
             "\n// The name an entity of this placement has in the map: iw8r_pname(inst, \"door\") is \"p7_door\".\n"
             "iw8r_pname(inst, name)\n{\n    return inst.pid + \"_\" + name;\n}\n";
        for (const auto* ps : prefabScripts) {
            std::string source;
            for (char c : ps->source) source += c == '\n' || c == '\r' ? ' ' : c;
            s += "\n// ---- prefab script " + ps->id + (source.empty() ? "" : " (" + source + ")") + ": " + ps->prefix +
                 "* is " + "iw8rp_" + ps->id + "_* here ----\n" + prefabScriptRenamed(ps->text, ps->prefix, ps->id) + "\n";
        }
    }
    if (!sounds.empty()) {
        // Loops play on a spawned script_origin; one-shots repeat every delay seconds (or play once).
        s += "\niw8r_place_sounds()\n{\n    wait 0.05;\n";
        for (const auto& p : sounds) {
            if (p.loop)
                s += "    iw8r_loop_sound(\"" + p.alias + "\", " + vec(p.origin) + ");\n";
            else
                s += "    level thread iw8r_repeat_sound(\"" + p.alias + "\", " + vec(p.origin) + ", " + num(p.delay) + ");\n";
        }
        s += "}\n\niw8r_loop_sound(alias, origin)\n{\n"
             "    ent = spawn(\"script_origin\", origin);\n"
             "    ent playloopsound(alias);\n}\n\n"
             "iw8r_repeat_sound(alias, origin, delay)\n{\n"
             "    ent = spawn(\"script_origin\", origin);\n"
             "    for (;;)\n    {\n        ent playsound(alias);\n        if (delay <= 0)\n            return;\n        wait delay;\n    }\n}\n";
    }
    if (!fx.empty()) {
        s += "\niw8r_place_fx()\n{\n    wait 0.05;\n";
        for (const auto& p : fx) {
            const std::string id = "level.iw8r_fx[" + std::to_string(index[p.effect]) + "]";
            const std::string ang = vec(p.angles);
            if (p.delay > 0)
                s += "    level thread iw8r_repeat_fx(" + id + ", " + vec(p.origin) + ", " + ang + ", " + num(p.delay) + ");\n";
            else
                s += "    iw8r_spawn_fx(" + id + ", " + vec(p.origin) + ", " + ang + ");\n";
        }
        // isdefined guards: x64-zt turns a loadfx of a name it cannot resolve into undefined (iw3_fastfile.cpp
        // ~12436-12450); the placement then does nothing instead of raising a script error in game.
        s += "}\n\niw8r_spawn_fx(effect, origin, angles)\n{\n"
             "    if (!isdefined(effect))\n        return;\n"
             "    ent = spawnfx(effect, origin, anglestoforward(angles), anglestoup(angles));\n"
             "    triggerfx(ent);\n}\n\n"
             "iw8r_repeat_fx(effect, origin, angles, delay)\n{\n"
             "    if (!isdefined(effect))\n        return;\n"
             "    for (;;)\n    {\n        playfx(effect, origin, anglestoforward(angles), anglestoup(angles));\n        wait delay;\n    }\n}\n";
    }
    return s;
}

}  // namespace iw8r
