#include "play_behaviour.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "brush_ops.h"
#include "map_script.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lowered(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const std::string* nonEmpty(const Entity& e, const char* key) {
    const std::string* v = e.value(key);
    return v && !v->empty() ? v : nullptr;
}

// A rotation of `degrees` about the world axis 0 (x: roll), 1 (y: pitch) or 2 (z: yaw), as rotateroll / rotatepitch /
// rotateyaw turn an entity (positive yaw turns +x toward +y).
void axisRotation(int axis, double degrees, double R[3][3]) {
    const double a = degrees * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) R[i][j] = i == j ? 1.0 : 0.0;
    if (axis == 2) R[0][0] = c, R[0][1] = -s, R[1][0] = s, R[1][1] = c;
    else if (axis == 1) R[0][0] = c, R[0][2] = s, R[2][0] = -s, R[2][2] = c;
    else R[1][1] = c, R[1][2] = -s, R[2][1] = s, R[2][2] = c;
}

// moveto / rotateyaw without acceleration arguments move at a constant speed.
double smooth(double x) { return std::clamp(x, 0.0, 1.0); }

}  // namespace

bool PlayBehaviours::Volume::touches(const AABB& p) const {
    if (radius > 0) {
        const double cx = std::clamp(centre.x, p.mins.x, p.maxs.x), cy = std::clamp(centre.y, p.mins.y, p.maxs.y);
        const double dx = cx - centre.x, dy = cy - centre.y;
        return dx * dx + dy * dy <= radius * radius && p.maxs.z >= centre.z && p.mins.z <= centre.z + height;
    }
    if (!bounds.valid()) return false;
    for (int a = 0; a < 3; ++a)
        if (p.maxs[a] < bounds.mins[a] || p.mins[a] > bounds.maxs[a]) return false;
    // The player's box against each brush: the box is outside a brush when one plane has the whole box in front.
    for (const auto& planes : brushes) {
        bool inside = true;
        for (const Plane& pl : planes) {
            const Vec3 nearest{pl.n.x > 0 ? p.mins.x : p.maxs.x, pl.n.y > 0 ? p.mins.y : p.maxs.y, pl.n.z > 0 ? p.mins.z : p.maxs.z};
            if (dot(pl.n, nearest) > pl.d) {
                inside = false;
                break;
            }
        }
        if (inside) return true;
    }
    return brushes.empty();
}

bool PlayBehaviours::Volume::hitRay(const Vec3& start, const Vec3& end, Vec3* hitPoint) const {
    const Vec3 delta = end - start;
    const double len = length(delta);
    if (len < 1e-9) {
        AABB pt;
        pt.add(start - Vec3{0.5, 0.5, 0.5});
        pt.add(start + Vec3{0.5, 0.5, 0.5});
        if (!touches(pt)) return false;
        if (hitPoint) *hitPoint = start;
        return true;
    }
    if (radius > 0) {
        // Segment vs vertical cylinder at centre (x, y), radius, z in [centre.z, centre.z + height].
        double t0 = 0.0, t1 = 1.0;
        if (std::fabs(delta.z) < 1e-12) {
            if (start.z < centre.z || start.z > centre.z + height) return false;
        } else {
            double za = (centre.z - start.z) / delta.z;
            double zb = (centre.z + height - start.z) / delta.z;
            if (za > zb) std::swap(za, zb);
            t0 = std::max(t0, za);
            t1 = std::min(t1, zb);
            if (t0 > t1) return false;
        }
        const double ox = start.x - centre.x, oy = start.y - centre.y;
        const double a = delta.x * delta.x + delta.y * delta.y;
        const double b = 2.0 * (ox * delta.x + oy * delta.y);
        const double c = ox * ox + oy * oy - radius * radius;
        if (a < 1e-12) {
            if (c > 0.0) return false;
        } else {
            const double disc = b * b - 4.0 * a * c;
            if (disc < 0.0) return false;
            const double sq = std::sqrt(disc);
            t0 = std::max(t0, (-b - sq) / (2.0 * a));
            t1 = std::min(t1, (-b + sq) / (2.0 * a));
            if (t0 > t1) return false;
        }
        if (hitPoint) *hitPoint = start + delta * t0;
        return true;
    }
    if (!bounds.valid()) return false;
    if (!brushes.empty()) {
        double bestT = 2.0;
        for (const auto& planes : brushes) {
            double t0 = 0.0, t1 = 1.0;
            bool ok = true;
            for (const Plane& pl : planes) {
                const double d0 = dot(pl.n, start) - pl.d;
                const double denom = dot(pl.n, delta);
                if (std::fabs(denom) < 1e-12) {
                    if (d0 > 0.0) {
                        ok = false;
                        break;
                    }
                } else {
                    const double t = -d0 / denom;
                    if (denom < 0.0) t0 = std::max(t0, t);
                    else t1 = std::min(t1, t);
                    if (t0 > t1) {
                        ok = false;
                        break;
                    }
                }
            }
            if (ok && t0 < bestT) bestT = t0;
        }
        if (bestT <= 1.0) {
            if (hitPoint) *hitPoint = start + delta * bestT;
            return true;
        }
        return false;
    }
    // AABB slab test when volume has bounds only.
    double t0 = 0.0, t1 = 1.0;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(delta[a]) < 1e-12) {
            if (start[a] < bounds.mins[a] || start[a] > bounds.maxs[a]) return false;
        } else {
            double ta = (bounds.mins[a] - start[a]) / delta[a];
            double tb = (bounds.maxs[a] - start[a]) / delta[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
    }
    if (hitPoint) *hitPoint = start + delta * t0;
    return true;
}

std::vector<Id> PlayBehaviours::build(const MapFile& map, CollisionWorld& world, const std::string& mapPath) {
    const std::vector<Id> moving = read(map, mapPath);
    world.build(map, 4, [this](const Entity& e) { return moves(e); });
    attach(map, world);
    return moving;
}

bool PlayBehaviours::moves(const Entity& e) const {
    if (e.id != kNoId && std::find(moverIds_.begin(), moverIds_.end(), e.id) != moverIds_.end()) return true;
    const std::string cls = lowered(e.classname());
    if (cls != "script_brushmodel" && cls != "script_model" && cls != "misc_model") return false;
    const std::string* tn = e.value("targetname");
    return tn && std::find(moverNames_.begin(), moverNames_.end(), *tn) != moverNames_.end();
}

std::vector<Id> PlayBehaviours::read(const MapFile& map, const std::string& mapPath) {
    moverNames_.clear();
    moverIds_.clear();
    script_.clear();
    scriptMovers_.clear();
    doors_.clear();
    movers_.clear();
    triggers_.clear();
    damageZones_.clear();
    visionVolumes_.clear();
    damageTriggers_.clear();
    activeVision_.clear();
    poses_.clear();
    warnings.clear();
    time_ = 0;

    const auto findByName = [&](const std::string& name) -> const Entity* {
        for (const Entity& e : map.entities)
            if (const std::string* tn = e.value("targetname"); tn && *tn == name) return &e;
        return nullptr;
    };
    const auto volumeOf = [](const Entity& e) {
        Volume v;
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush) {
                const ConvexBrush cb = brushFromPrimitive(*p);
                if (!isValidBrush(cb)) continue;
                std::vector<Plane> planes;
                for (const auto& f : cb) planes.push_back(f.plane);
                v.brushes.push_back(std::move(planes));
                v.bounds.add(brushBounds(cb));
            }
        if (lowered(e.classname()) == "trigger_radius" && v.brushes.empty()) {
            v.centre = e.origin().value_or(Vec3{});
            v.radius = 64, v.height = 72;  // matches map_script.cpp:328
            if (const std::string* r = e.value("radius")) v.radius = std::max(1.0, std::atof(r->c_str()));
            if (const std::string* h = e.value("height")) v.height = std::max(1.0, std::atof(h->c_str()));
            v.bounds.add(v.centre - Vec3{v.radius, v.radius, 0});
            v.bounds.add(v.centre + Vec3{v.radius, v.radius, v.height});
        }
        return v;
    };

    std::vector<Id> moving;
    // Doors (script_brushmodel, script_model, misc_model).
    for (const Entity& e : map.entities) {
        const std::string cls = lowered(e.classname());
        const bool isModel = cls == "script_model" || cls == "misc_model";
        if (cls != "script_brushmodel" && !isModel) continue;
        const std::string* dk = nonEmpty(e, "iw8r_door");
        const std::string* tn = nonEmpty(e, "targetname");
        if (!dk || !tn) continue;
        Door d;
        d.entity = e.id;
        d.name = *tn;
        char kind[16] = {};
        double v[5] = {0, 0, 0, 0, 0};
        const int n = std::sscanf(dk->c_str(), "%15s %lf %lf %lf %lf %lf", kind, &v[0], &v[1], &v[2], &v[3], &v[4]);
        const std::string k = lowered(kind);
        if (k == "slide" && n >= 4) {
            d.slide = true;
            d.offset = Vec3{v[0], v[1], v[2]};
            d.moveTime = n >= 5 ? std::max(0.05, v[3]) : 1.0;
            if (n >= 6) d.holdTime = std::max(0.0, v[4]);
        } else if ((k == "rotate" || k == "hinge") && n >= 2) {
            if (isModel) {
                d.hinged = true;
                d.toggle = k == "hinge";
                d.yaw = std::fabs(v[0]);
                d.hinge = e.origin().value_or(Vec3{});
                if (const std::string* hk = nonEmpty(e, "iw8r_hinge")) {
                    Vec3 hp;
                    if (std::sscanf(hk->c_str(), "%lf %lf %lf", &hp.x, &hp.y, &hp.z) == 3) d.hinge = hp;
                }
                double yawDeg = 0;
                if (const std::string* a = e.value("angles")) {
                    double p = 0, y = 0, r = 0;
                    if (std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &r) >= 2) yawDeg = y;
                }
                const double rad = yawDeg * kPi / 180.0;
                d.normal = Vec3{-std::sin(rad), std::cos(rad), 0};
                d.swing = 1.0;
            } else {
                const DoorHinge hg = doorHingeOf(e);
                if (!hg.ok) continue;
                d.hinged = true;
                d.toggle = k == "hinge";
                d.yaw = std::fabs(v[0]);
                d.hinge = hg.pos;
                d.normal = hg.normal;
                d.swing = hg.swing;
            }
            d.moveTime = n >= 3 ? std::max(0.05, v[1]) : 0.6;
            if (n >= 4) d.holdTime = std::max(0.0, v[2]);
        } else {
            warnings.push_back("door '" + d.name + "': iw8r_door '" + *dk + "' not understood");
            continue;
        }
        // Its use volume: the named trigger that targets it (matching compile_iw8.cpp: an unnamed trigger targeting
        // the door is ignored with a warning and the door uses its own 40-unit grown use area).
        bool found = false;
        std::string unnamedTrig;
        for (const Entity& t : map.entities) {
            const std::string tcls = lowered(t.classname());
            if (tcls.rfind("trigger_", 0) != 0) continue;
            if (const std::string* tt = t.value("target"); tt && *tt == d.name) {
                if (const std::string* ttn = nonEmpty(t, "targetname")) {
                    d.use = volumeOf(t);
                    d.triggerName = *ttn;
                    d.shotTrigger = tcls == "trigger_damage";
                    found = true;
                    break;
                }
                unnamedTrig = t.classname();
            }
        }
        if (!found && !unnamedTrig.empty())
            warnings.push_back(unnamedTrig + " targets the door '" + d.name +
                               "' but has no targetname; using the door's own use area instead");
        Volume own = volumeOf(e);
        if (!own.bounds.valid()) {
            if (!isModel) continue;
            const Vec3 o = e.origin().value_or(Vec3{});
            own.bounds.add(o - Vec3{32, 32, 0});
            own.bounds.add(o + Vec3{32, 32, 72});
        }
        if (!found) {
            d.use.bounds.add(Vec3{own.bounds.mins.x - 40, own.bounds.mins.y - 40, own.bounds.mins.z});
            d.use.bounds.add(Vec3{own.bounds.maxs.x + 40, own.bounds.maxs.y + 40, own.bounds.maxs.z});
            d.use.brushes.clear();
        }
        d.state = 0;
        doors_.push_back(std::move(d));
        moving.push_back(e.id);
    }
    // Lifts, spinners and bobbers of brush models and moving models (misc_model / script_model).
    for (const Entity& e : map.entities) {
        const std::string cls = lowered(e.classname());
        const bool isModel = cls == "script_model" || cls == "misc_model";
        if (cls != "script_brushmodel" && !isModel) continue;
        // Brush models require a targetname (compile_iw8.cpp); models are auto-named at build time.
        if (!isModel && !nonEmpty(e, "targetname")) continue;
        if (std::find(moving.begin(), moving.end(), e.id) != moving.end()) continue;
        Mover m;
        m.entity = e.id;
        if (isModel) m.centre = e.origin().value_or(Vec3{});
        if (const std::string* v = nonEmpty(e, "iw8r_lift")) {
            double dz = 0, t = 2, w = 2;
            const int n = std::sscanf(v->c_str(), "%lf %lf %lf", &dz, &t, &w);
            if (n < 1 || dz == 0) continue;
            m.kind = 0, m.a = dz, m.b = std::max(0.05, t), m.c = std::max(0.0, w);
        } else if (const std::string* sv = nonEmpty(e, "iw8r_spin")) {
            double seconds = 4;
            char axis[16] = "yaw";
            if (std::sscanf(sv->c_str(), "%lf %15s", &seconds, axis) < 1 || seconds == 0) continue;
            const std::string ax = lowered(axis);
            m.kind = 1, m.a = std::fabs(seconds), m.b = ax == "pitch" ? 1 : ax == "roll" ? 2 : 0;
        } else if (const std::string* bv = nonEmpty(e, "iw8r_bob")) {
            double h = 16, s = 2;
            if (std::sscanf(bv->c_str(), "%lf %lf", &h, &s) < 1 || h == 0 || s <= 0) continue;
            m.kind = 2, m.a = h, m.b = s;
        } else {
            continue;
        }
        movers_.push_back(m);
        moving.push_back(e.id);
    }
    // Triggers: teleporters, jump pads, trigger_hurt, iw8r_damage, iw8r_vision, trigger_damage, and fx_origin iw8r_damage.
    for (const Entity& e : map.entities) {
        const std::string cls = lowered(e.classname());
        // fx_origin iw8r_damage "<amount> [radius] [interval seconds]" (matches map_script.cpp:302-316).
        if (cls == "fx_origin") {
            const std::string* dv = nonEmpty(e, "iw8r_damage");
            const auto o = e.origin();
            double amount = 0, radius = 48, interval = 1;
            if (!dv || !o || std::sscanf(dv->c_str(), "%lf %lf %lf", &amount, &radius, &interval) < 1 || amount <= 0 || radius <= 0)
                continue;
            DamageZone dz;
            dz.amount = amount;
            dz.interval = std::max(0.05, interval);
            if (const std::string* tn = nonEmpty(e, "targetname")) dz.name = *tn;
            else if (const std::string* fx = nonEmpty(e, "fx")) dz.name = *fx;
            else dz.name = "fx_origin";
            dz.volume.centre = Vec3{o->x, o->y, o->z - 16.0};
            dz.volume.radius = radius;
            dz.volume.height = radius + 16.0;
            dz.volume.bounds.add(Vec3{o->x - radius, o->y - radius, o->z - 16.0});
            dz.volume.bounds.add(Vec3{o->x + radius, o->y + radius, o->z + radius});
            damageZones_.push_back(std::move(dz));
            continue;
        }
        if (e.isWorldspawn() || cls.rfind("trigger_", 0) != 0) continue;
        const Volume vol = volumeOf(e);
        if (!vol.bounds.valid()) continue;
        const std::string* tn = nonEmpty(e, "targetname");
        const std::string tname = tn ? *tn : std::string();
        const bool isShot = cls == "trigger_damage";

        if (isShot) {
            DamageTrigger dt;
            dt.volume = vol;
            dt.name = tname;
            if (const std::string* tg = nonEmpty(e, "target")) dt.target = *tg;
            if (const std::string* ex = nonEmpty(e, "script_exploder")) dt.exploder = *ex;
            damageTriggers_.push_back(std::move(dt));
        }

        // trigger_hurt: nameless allowed; dmg default 5, spawnflags bit 16 = 1 s interval else 0.05 s.
        if (cls == "trigger_hurt") {
            const std::string* dv = e.value("dmg");
            const double dmg = dv ? std::atof(dv->c_str()) : 5.0;
            if (dmg > 0) {
                const std::string* sf = e.value("spawnflags");
                DamageZone dz;
                dz.volume = vol;
                dz.name = tname.empty() ? "trigger_hurt" : tname;
                dz.amount = dmg;
                dz.interval = sf && (std::atoi(sf->c_str()) & 16) ? 1.0 : 0.05;
                damageZones_.push_back(std::move(dz));
            }
        }
        // trigger iw8r_damage "<amount> [interval seconds]" (requires targetname).
        if (!isShot) {
            if (const std::string* dv = nonEmpty(e, "iw8r_damage"); dv && tn) {
                double amount = 0, interval = 1.0;
                if (std::sscanf(dv->c_str(), "%lf %lf", &amount, &interval) >= 1 && amount > 0) {
                    DamageZone dz;
                    dz.volume = vol;
                    dz.name = *tn;
                    dz.amount = amount;
                    dz.interval = std::max(0.05, interval);
                    damageZones_.push_back(std::move(dz));
                }
            }
        }
        // trigger iw8r_vision (requires targetname).
        if (!isShot) {
            if (const std::string* vv = nonEmpty(e, "iw8r_vision"); vv && tn) {
                VisionVolume vis;
                vis.volume = vol;
                vis.name = *tn;
                vis.vision = *vv;
                visionVolumes_.push_back(std::move(vis));
            }
        }
        // Teleporter / jump pad.
        if (const std::string* v = nonEmpty(e, "iw8r_teleport")) {
            const Entity* dest = findByName(*v);
            if (!dest || !dest->origin()) {
                warnings.push_back("teleporter '" + tname + "': no entity named '" + *v + "'");
            } else {
                Trigger t;
                t.name = tname;
                t.teleport = true;
                t.shotOnly = isShot;
                t.destination = *dest->origin();
                if (const std::string* a = dest->value("angles")) {
                    double p = 0, y = 0, r = 0;
                    if (std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &r) >= 2) t.yaw = y;
                } else if (const std::string* a1 = dest->value("angle")) {
                    t.yaw = std::atof(a1->c_str());
                }
                t.volume = vol;
                triggers_.push_back(std::move(t));
            }
        } else if (const std::string* jv = nonEmpty(e, "iw8r_jump")) {
            double x = 0, y = 0, z = 0;
            if (std::sscanf(jv->c_str(), "%lf %lf %lf", &x, &y, &z) == 3) {
                Trigger t;
                t.name = tname;
                t.shotOnly = isShot;
                t.velocity = Vec3{x, y, z};
                t.volume = vol;
                triggers_.push_back(std::move(t));
            }
        }
    }

    // The map's own script (core gsc_emulate). Its brush models become movers: the named script_brushmodels whose
    // name is in the script's text or that it moves in its first 30 s (a run ahead; tick() starts it again from 0).
    const std::string src = !scriptOverride_.empty() ? scriptOverride_ : mapPath.empty() ? std::string() : readMapScript(map, mapPath);
    if (src != scriptText_) scriptNotesShown_ = 0;
    scriptText_ = src;
    if (!src.empty() && script_.load(src, map)) {
        script_.advanceTo(30);
        std::vector<Id> moved;
        for (const GscPose& p : script_.poses())
            if (p.brush) moved.push_back(p.entity);
        script_.advanceTo(0);
        for (const Entity& e : map.entities) {
            const std::string* tn = nonEmpty(e, "targetname");
            if (!tn || lowered(e.classname()) != "script_brushmodel") continue;
            if (std::find(moving.begin(), moving.end(), e.id) != moving.end()) continue;  // a door or mover of its keys
            if (std::find(moved.begin(), moved.end(), e.id) == moved.end() && src.find(*tn) == std::string::npos) continue;
            scriptMovers_.push_back({e.id, -1});
            moving.push_back(e.id);
        }
    }

    moverIds_ = moving;
    for (const Entity& e : map.entities)
        if (std::find(moving.begin(), moving.end(), e.id) != moving.end())
            if (const std::string* tn = e.value("targetname")) moverNames_.push_back(*tn);
    return moving;
}

void PlayBehaviours::attach(const MapFile& map, CollisionWorld& world) {
    for (const Entity& e : map.entities) {
        for (Door& d : doors_)
            if (d.entity == e.id) d.group = world.addMover(e);
        for (Mover& m : movers_)
            if (m.entity == e.id) {
                m.group = world.addMover(e);
                if (m.group >= 0) {
                    const AABB b = world.moverBounds(m.group);
                    if (b.valid()) m.centre = (b.mins + b.maxs) * 0.5;
                }
            }
        for (ScriptMover& m : scriptMovers_)
            if (m.entity == e.id) m.group = world.addMover(e);
    }
}

std::vector<GscNote> PlayBehaviours::takeScriptNotes() {
    std::vector<GscNote> out;
    const auto& all = script_.notes();
    for (size_t i = scriptNotesShown_; i < all.size(); ++i) out.push_back(all[i]);
    scriptNotesShown_ = std::max(scriptNotesShown_, all.size());
    return out;
}

std::vector<PlayBehaviours::Event> PlayBehaviours::tick(double dt, const Vec3& feet, const Vec3& mins, const Vec3& maxs, bool use,
                                                        CollisionWorld& world) {
    std::vector<Event> events;
    dt = std::clamp(dt, 0.0, 0.25);
    time_ += dt;
    AABB player;
    player.add(feet + mins);
    player.add(feet + maxs);
    poses_.clear();

    // Doors: closed -> (used) opening -> open -> (hold over, or used again for a hinge door) closing -> closed.
    for (Door& d : doors_) {
        const bool usedHere = !d.shotTrigger && use && d.use.touches(player);
        d.t += dt;
        if (d.state == 0 && usedHere) {
            d.state = 1, d.t = 0;
            // A hinged door swings away from the player (the script's dot test against the door's normal).
            d.dir = d.swing;
            if (d.hinged && dot(feet - d.hinge, Vec3{d.normal.x, d.normal.y, 0}) > 0) d.dir = -d.swing;
            events.push_back({Event::DoorUsed, {}, 0, {}, d.name});
        } else if (d.state == 1 && d.t >= d.moveTime) {
            d.state = 2, d.t = 0, d.usedWhileOpen = false;
        } else if (d.state == 2) {
            // The script waits 0.4 s after a hinged door's move before it listens again.
            const bool listen = d.t >= (d.hinged ? 0.4 : 0.0);
            if (d.hinged && d.toggle ? (listen && usedHere) : d.t >= (d.hinged ? 0.4 : 0.0) + d.holdTime) d.state = 3, d.t = 0;
        } else if (d.state == 3 && d.t >= d.moveTime) {
            d.state = 0, d.t = 0;
        }
        const double f = d.state == 0 ? 0.0 : d.state == 1 ? smooth(d.t / d.moveTime) : d.state == 2 ? 1.0 : 1.0 - smooth(d.t / d.moveTime);
        Pose p;
        p.entity = d.entity;
        if (d.slide) {
            p.move = d.offset * f;
        } else {
            axisRotation(2, d.yaw * d.dir * f, p.R);
            p.pivot = d.hinge;
        }
        world.setMoverPose(d.group, p.R, p.pivot, p.move);
        poses_.push_back(p);
    }
    // Lifts, spinners, bobbers: functions of time, as the script's loops.
    for (const Mover& m : movers_) {
        Pose p;
        p.entity = m.entity;
        if (m.kind == 0) {
            const double t = m.b, w = m.c, period = 2 * (t + w), x = std::fmod(time_, period);
            double f = 0;
            if (x < w) f = 0;
            else if (x < w + t) f = (x - w) / t;
            else if (x < 2 * w + t) f = 1;
            else f = 1 - (x - 2 * w - t) / t;
            p.move = Vec3{0, 0, m.a * f};
        } else if (m.kind == 1) {
            axisRotation(m.b == 1 ? 1 : m.b == 2 ? 0 : 2, 360.0 * std::fmod(time_, m.a) / m.a, p.R);
            p.pivot = m.centre;
        } else {
            // movez(height, half, half * 0.3, half * 0.3), then back: eased at both ends.
            const double half = m.b * 0.5, x = std::fmod(time_, m.b);
            const double u = x < half ? x / half : 1 - (x - half) / half;
            p.move = Vec3{0, 0, m.a * (u * u * (3 - 2 * u))};
        }
        world.setMoverPose(m.group, p.R, p.pivot, p.move);
        poses_.push_back(p);
    }
    // The map's own script on the same clock: whatever it moved, turned or hid (an entity the keys above already move
    // keeps their pose). Its brush models collide where it has them; a deleted one keeps its last place.
    if (script_.loaded()) {
        script_.advanceTo(time_);
        const size_t keyed = poses_.size();
        for (const GscPose& g : script_.poses()) {
            if (std::any_of(poses_.begin(), poses_.begin() + std::ptrdiff_t(keyed), [&](const Pose& q) { return q.entity == g.entity; }))
                continue;
            Pose p;
            p.entity = g.entity;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) p.R[i][j] = g.R[i][j];
            p.pivot = g.pivot;
            p.move = g.move;
            p.visible = g.visible;
            if (!g.deleted)
                for (const ScriptMover& m : scriptMovers_)
                    if (m.entity == g.entity) world.setMoverPose(m.group, p.R, p.pivot, p.move);
            poses_.push_back(p);
        }
    }
    // Teleporters and jump pads fire when the player enters them (the script's cooldowns of 1 s and 0.5 s only keep a
    // player who stays inside from firing again; a teleported or launched player has left the volume by then).
    for (Trigger& t : triggers_) {
        if (t.shotOnly) continue;
        const bool in = t.volume.touches(player);
        if (!in) {
            t.inside = false;
            continue;
        }
        if (t.inside) continue;
        t.inside = true;
        Event ev;
        ev.name = t.name;
        if (t.teleport) {
            ev.kind = Event::Teleport;
            ev.position = t.destination;
            ev.yaw = t.yaw;
        } else {
            ev.kind = Event::Launch;
            ev.velocity = t.velocity;
        }
        events.push_back(ev);
    }
    // Damage zones (trigger_hurt, trigger iw8r_damage, fx_origin iw8r_damage).
    for (DamageZone& dz : damageZones_) {
        if (!dz.volume.touches(player)) continue;
        if (time_ < dz.nextTime - 1e-9) continue;
        dz.nextTime = time_ + dz.interval;
        Event ev;
        ev.kind = Event::Damage;
        ev.name = dz.name;
        ev.damage = dz.amount;
        events.push_back(ev);
    }
    // Vision volumes (iw8r_vision): last matching volume wins; leaving all restores map base vision ("").
    if (!visionVolumes_.empty()) {
        std::string inside, insideName;
        for (const VisionVolume& vis : visionVolumes_) {
            if (vis.volume.touches(player)) {
                inside = vis.vision;
                insideName = vis.name;
            }
        }
        if (inside != activeVision_) {
            activeVision_ = inside;
            Event ev;
            ev.kind = Event::VisionChanged;
            ev.name = insideName;
            ev.vision = activeVision_;
            events.push_back(ev);
        }
    }
    return events;
}

std::vector<PlayBehaviours::Event> PlayBehaviours::hitDamageTrigger(const Vec3& start, const Vec3& end, const Vec3& playerFeet) {
    std::vector<Event> events;
    for (const DamageTrigger& dt : damageTriggers_) {
        Vec3 hitPos;
        if (!dt.volume.hitRay(start, end, &hitPos)) continue;
        Event hitEv;
        hitEv.kind = Event::TriggerDamaged;
        hitEv.position = hitPos;
        hitEv.name = dt.name;
        events.push_back(hitEv);
        // Open/toggle doors wired to this trigger_damage.
        for (Door& d : doors_) {
            const bool matches = (!dt.target.empty() && dt.target == d.name) ||
                                 (d.shotTrigger && !dt.name.empty() && d.triggerName == dt.name);
            if (!matches) continue;
            if (d.state == 0) {
                d.state = 1, d.t = 0;
                d.dir = d.swing;
                if (d.hinged && dot(playerFeet - d.hinge, Vec3{d.normal.x, d.normal.y, 0}) > 0) d.dir = -d.swing;
                events.push_back({Event::DoorUsed, {}, 0, {}, d.name});
            } else if (d.state == 2 && d.hinged && d.toggle && d.t >= 0.4) {
                d.state = 3, d.t = 0;
                events.push_back({Event::DoorUsed, {}, 0, {}, d.name});
            }
        }
        // Fire teleporters / jump pads on this trigger_damage.
        if (!dt.name.empty()) {
            for (const Trigger& t : triggers_) {
                if (!t.shotOnly || t.name != dt.name) continue;
                Event ev;
                ev.name = t.name;
                if (t.teleport) {
                    ev.kind = Event::Teleport;
                    ev.position = t.destination;
                    ev.yaw = t.yaw;
                } else {
                    ev.kind = Event::Launch;
                    ev.velocity = t.velocity;
                }
                events.push_back(ev);
            }
        }
    }
    return events;
}

}  // namespace iw8r
