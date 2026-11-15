// IW8 Radiant Suite - the scripted objects of a map, working in the editor's play preview (research 131 gap 9).
//
// The built map's script (core map_script) opens doors on Use, moves lifts, spins and bobs brush models, teleports and
// launches players from triggers. The play preview runs the same behaviours, by the same keys and numbers, so walking
// the map in the editor meets them as a player does in game:
//   - script_brushmodel iw8r_door: "hinge <angle> [time]" (stays open until used again), "rotate <angle> [time]
//     [hold]" (closes after hold, default 3 s), "slide <x> <y> <z> [time] [hold]"; a hinged door swings away from the
//     player who uses it. Used at the trigger that targets it, else within 40 units of it (the build's own use area).
//     Default move time: 0.6 s for hinge / rotate, 1 s for slide.
//   - iw8r_lift "<dz> [time] [wait]": up, wait, down, wait, for ever.
//   - iw8r_spin "<seconds per turn> [yaw|pitch|roll]": about the brush model's centre.
//   - iw8r_bob "<height> [seconds]": up and back down once per period.
//   - trigger iw8r_teleport "<destination targetname>": to the destination's origin, facing its yaw.
//   - trigger iw8r_jump "<vx> <vy> <vz>": launch velocity.
// Their brushes are movers of the collision world (CollisionWorld::addMover); poses() says where each mover is for
// drawing it.
#pragma once

#include <string>
#include <vector>

#include "collision_world.h"
#include "map_text.h"

namespace iw8r {

class PlayBehaviours {
public:
    // A mover's pose: every point p of the entity goes to pivot + R (p - pivot) + move.
    struct Pose {
        Id entity = kNoId;
        double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        Vec3 pivot, move;
    };
    struct Event {
        enum Kind { Teleport, Launch, DoorUsed } kind = Teleport;
        Vec3 position;       // Teleport: the new feet position
        double yaw = 0;      // Teleport: the new view yaw (degrees)
        Vec3 velocity;       // Launch
        std::string name;    // the trigger's or the door's targetname
    };

    // Reads the map's behaviours. Their brush entities are taken out of `world` (it is rebuilt from the map without
    // them) and added back as movers. Returns the entity ids of the movers.
    std::vector<Id> build(const MapFile& map, CollisionWorld& world);
    // The same in two steps, for a world built some other way (core play_world): read the behaviours, build the world
    // without the brush entities moves() names, then attach the movers to it.
    std::vector<Id> read(const MapFile& map);
    void attach(const MapFile& map, CollisionWorld& world);
    // Whether an entity's brushes move (a script_brushmodel whose targetname is a door's or a mover's; by name, so it
    // also answers for a copy of the map such as a flattened one).
    bool moves(const Entity& e) const;

    // Advances `dt` seconds. `feet` and the box mins / maxs (relative to the feet) are the player; `use` is true on the
    // frame the Use key goes down. Movers in `world` get their new poses.
    std::vector<Event> tick(double dt, const Vec3& feet, const Vec3& mins, const Vec3& maxs, bool use, CollisionWorld& world);

    const std::vector<Pose>& poses() const { return poses_; }
    double time() const { return time_; }
    size_t doorCount() const { return doors_.size(); }
    size_t moverCount() const { return movers_.size(); }
    std::vector<std::string> warnings;

private:
    struct Volume {
        std::vector<std::vector<Plane>> brushes;  // inside: every plane of one brush, dot(n, x) <= d
        AABB bounds;
        double radius = 0, height = 0;            // trigger_radius: a cylinder at `centre`
        Vec3 centre;
        bool touches(const AABB& player) const;
    };
    struct Door {
        Id entity = kNoId;
        int group = -1;
        std::string name;
        bool hinged = false, toggle = false, slide = false;
        double yaw = 90, moveTime = 0.6, holdTime = 3;
        Vec3 offset, hinge, normal{0, 1, 0};
        double swing = 1;
        Volume use;
        // State: 0 closed, 1 opening, 2 open, 3 closing; `t` seconds in the state; dir the swing sign of this opening.
        int state = 0;
        double t = 0, dir = 1;
        bool usedWhileOpen = false;
    };
    struct Mover {  // lift, spinner or bobber
        Id entity = kNoId;
        int group = -1;
        int kind = 0;  // 0 lift, 1 spin, 2 bob
        double a = 0, b = 0, c = 0;  // lift: dz, time, wait; spin: seconds, axis; bob: height, seconds
        Vec3 centre;
    };
    struct Trigger {
        Volume volume;
        std::string name;
        bool teleport = false;
        Vec3 destination, velocity;
        double yaw = 0;
        bool inside = false;  // fires once per entry
    };
    std::vector<Door> doors_;
    std::vector<Mover> movers_;
    std::vector<Trigger> triggers_;
    std::vector<Pose> poses_;
    std::vector<std::string> moverNames_;
    double time_ = 0;
};

}  // namespace iw8r
