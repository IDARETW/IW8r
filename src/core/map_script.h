// IW8 Radiant Suite - generated map script (extracted/maps/mp/<id>.gsc, IW3 script that x64-zt translates and, with
// gsc-tool configured, compiles into the map zone).
//
// It holds the exponential fog call (atmosphere.h) and the effect placements (roadmap 6.6): every `fx_origin` entity
// with an `fx` key ("vfx/..." stock Replay effect name). loadfx runs in main() (level load); the effects start a frame
// later: `delay` <= 0 spawns a persistent effect (spawnfx + triggerfx, looping effects keep looping), `delay` > 0
// replays it with playfx every `delay` seconds. `angles` orients it (forward/up from the angles).
//
// x64-zt translates each loadfx literal through its IW3 effect table and a stock stand-in by keyword
// (iw3_scripts.cpp StockEffectFor: glass, explo, fire/flame, smoke/steam, else a spark); a stand-in name maps to
// itself. Exact stock names for every effect need a pass-through for vfx/ literals in x64-zt (ROADMAP 6.6).
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "atmosphere.h"
#include "geometry.h"

namespace iw8r {

struct FxPlacement {
    std::string effect;  // "vfx/..."
    Vec3 origin;
    Vec3 angles;         // pitch yaw roll, degrees
    double delay = 0;    // seconds between plays; <= 0: one persistent spawn
};

// A stock sound alias played from a sound_emitter entity (roadmap 6.7): `soundalias` (a Replay alias in the
// always-loaded banks, data/asset_catalog/iw8_common_sounds.tsv), `loop` 1 = playloopsound on a spawned
// script_origin, else playsound every `delay` seconds (<= 0: once). x64-zt plays stock aliases listed in
// extracted/sound/<map>.stock_sounds.txt (stockSoundList).
struct SoundPlacement {
    std::string alias;
    Vec3 origin;
    bool loop = false;
    double delay = 0;
};

std::vector<SoundPlacement> readSoundPlacements(const MapFile& map, size_t* skipped = nullptr);

// "<alias> <0|1>" lines, one per distinct alias (lower case), for x64-zt.
std::string stockSoundList(const std::vector<SoundPlacement>& sounds);

// Effect placements from the map's fx_origin entities (entities without an fx key are skipped and counted in
// `skipped`).
std::vector<FxPlacement> readFxPlacements(const MapFile& map, size_t* skipped = nullptr);

// The script, or "" when there is neither fog nor an effect.
// Extra parts of the script (all optional):
//  - solidTargetnames: live script_models (targeted, not gameobjects/exploders) made solid for players at load; their
//    keyframed collision only stops bullets (x64-zt 0xB180 contents), stock scripts call solid() the same way.
//  - userScript: the map's own GSC (worldspawn iw8r_script); it must define user_main(), started after the load script.
struct MapScriptExtras {
    // A stock door scriptable is placed (6.14, research 83 / 86): main() starts the stock door script, as stock map
    // scripts and scripts/mp/load do. The converter lets the map script call it.
    bool stockDoors = false;
    // Game mode settings carried by the map (worldspawn iw8r_mode_dvars, game_modes.h): set at the start of main(), as
    // the stock Warzone tutorial map sets its own; and what a small map switches off in the br game type (worldspawn
    // iw8r_br: nocircle, nogulag, skipdrop; research 87 section 6).
    std::vector<std::pair<std::string, std::string>> modeDvars;
    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;
    std::vector<std::string> solidTargetnames;
    std::string userScript;
    // Hot update (roadmap 7.4): a thread that applies the dvar iw8r_hot ("seq targetname x y z pitch yaw roll") to the
    // live entities with that targetname, so the editor (Live Link) moves script entities in the running game.
    bool hotUpdate = true;
    // Sun shadow cascade size multipliers sent to every player (setclientdvar sm_sunCascadeSizeMultiplier1 / 2 and
    // sm_sunDistantShadows 1; the stock highway lighting script's values 3 / 2): the owner saw sun shadows end too
    // close. worldspawn iw8r_sun_shadow_range "<m1> <m2>"; 0 = off.
    double sunShadowRange[2] = {3, 2};
    // Stock animations on live script_models (4.21, research 61): script_model keys iw8r_anim "<xanim>" and
    // iw8r_anim_loop "<seconds>" (0 = play once). Each gets a generated function naming the anim literally, so the
    // converter lists it in the level anm table (fork; the xanim stays in its stock zone).
    struct AnimPlacement {
        std::string targetname, anim;
        double loopSeconds = 0;
    };
    std::vector<AnimPlacement> anims;
    // Vision / fog volumes (5.18, research 63): brush triggers with iw8r_vision; a player inside gets the volume's vision
    // (vision/<map>_v<N>.vision, built by the fork from extracted/vision/<map>.vol<N>.overrides.json) and the map's own
    // vision (baseVision) back on leaving.
    struct VisionVolume {
        std::string targetname, vision;
    };
    std::vector<VisionVolume> visionVolumes;
    // Doors without scripting (6.24, research 62): a script_brushmodel with iw8r_door "rotate <yaw> [time] [hold]" or
    // "slide <x> <y> <z> [time] [hold]" and a targetname, opened by a trigger_use / trigger_multiple targeting it.
    struct Door {
        std::string door, trigger;
        bool rotate = true;
        double yaw = 90;
        double offset[3] = {0, 0, 0};
        double moveTime = 1, holdTime = 3;
        // Hinged doors (owner request 2026-11-11: "a hinged swinging door, not the automatic sliding one"): the door turns
        // about a vertical hinge at one end of the brush (doorHingeOf), away from the player who uses it. toggle: it
        // stays open until it is used again ("hinge <angle> [time]"); otherwise it closes after holdTime ("rotate").
        bool hinged = false, toggle = false;
        double hinge[3] = {0, 0, 0};
        double normal[2] = {0, 1};  // horizontal unit vector across the door's thickness
        double swing = 1;           // sign of the yaw that moves the free end along +normal
    };
    std::vector<Door> doors;
    // 6.12: inspector-only behaviours (keys on triggers and script_brushmodels, no scripting).
    //  trigger iw8r_teleport "<destination targetname>": a player touching it moves to that entity's origin / angles.
    //  trigger iw8r_jump "<vx> <vy> <vz>": a player touching it is launched with that velocity.
    //  trigger iw8r_damage "<amount> [interval]": a player inside takes that damage every interval seconds.
    //  script_brushmodel iw8r_lift "<dz> [time] [wait]": moves up dz and back forever, waiting at each end.
    struct Teleport {
        std::string trigger, dest;
    };
    std::vector<Teleport> teleports;
    struct JumpPad {
        std::string trigger;
        double velocity[3] = {0, 0, 600};
    };
    std::vector<JumpPad> jumpPads;
    struct DamageZone {
        std::string trigger;
        double amount = 10, interval = 1;
    };
    std::vector<DamageZone> damageZones;
    struct Lift {
        std::string targetname;
        double dz = 128, moveTime = 2, waitTime = 2;
    };
    std::vector<Lift> lifts;
    std::string baseVision;
    // Trigger volumes tested by the map script itself (owner playtest 2026-11-11: the game's trigger entities did not
    // fire in built maps). Every brush trigger and trigger_radius is listed with its shape; a script thread tests each
    // living player against them 20 times a second and notifies "trigger" (with the player) on the object that
    // iw8r_trigger("<targetname>") returns, and on the entity itself when the game spawned it. trigger_use* volumes
    // also need the use button; trigger_hurt / trigger_damage apply `dmg` themselves.
    struct TriggerVolume {
        std::string targetname;  // may be empty (a nameless trigger_hurt still hurts)
        bool use = false;
        double damage = 0;       // per hit; 0 = none
        int damageIntervalMs = 50;
        struct Box {
            Vec3 mins, maxs;
            std::vector<Plane> planes;  // only for a brush that is not an axis-aligned box: dot(n, x) <= d
            double radius = 0;          // > 0: a cylinder around the box centre (trigger_radius)
        };
        std::vector<Box> boxes;
    };
    std::vector<TriggerVolume> triggerVolumes;
    // Targetnames of script_models that stay live without anything driving them (key iw8r_no_collision).
    std::vector<std::string> keepNames;
    // Generated models that draw a moving brush model: {model targetname, brush model targetname}; the script links
    // the first to the second so it moves with it.
    std::vector<std::pair<std::string, std::string>> visualLinks;
    // 6.12: models that move without scripting (script_model / script_brushmodel keys, by targetname):
    //  iw8r_spin "<seconds per turn> [yaw|pitch|roll]", iw8r_bob "<height> <seconds>".
    struct Spinner {
        std::string targetname;
        double seconds = 4;
        int axis = 0;  // 0 yaw, 1 pitch, 2 roll
    };
    std::vector<Spinner> spinners;
    struct Bobber {
        std::string targetname;
        double height = 16, seconds = 2;
    };
    std::vector<Bobber> bobbers;
    // Prefab bundle scripts (docs/PREFAB_BUNDLES.md): one snippet per prefab kind. It is included once, with every
    // identifier that starts with `prefix` renamed to iw8rp_<id>_..., and its entry function is started once per
    // placement with iw8r_prefab_inst(pid, origin, angles). `once` (may be empty) is started once per map, first.
    struct PrefabScript {
        std::string id;      // manifest id: 1-40 characters of a-z 0-9 _
        std::string source;  // the bundle folder, for the comment line and for messages
        std::string text;    // the snippet as its author wrote it
        std::string prefix = "prefab_", entry = "prefab_main", once;
        struct Placement {
            std::string pid;  // placement id; a prefab inside a prefab chains them ("p7_p2")
            Vec3 origin, angles;
        };
        std::vector<Placement> placements;
    };
    std::vector<PrefabScript> prefabScripts;
};

// A prefab snippet with every identifier that starts with `prefix` renamed to iw8rp_<id>_<rest>. Whole identifiers
// only: text inside strings and comments stays as it is.
std::string prefabScriptRenamed(const std::string& text, const std::string& prefix, const std::string& id);
// The generated name of one snippet function: ("jump_pad", "prefab_", "prefab_main") -> "iw8rp_jump_pad_main".
std::string prefabScriptFunctionName(const std::string& id, const std::string& prefix, const std::string& name);
// "" when the list can be generated. Otherwise why not: an id that is not 1-40 characters of a-z 0-9 _, or two
// entries with one id (the message names both sources). mapScriptGsc leaves such entries out.
std::string prefabScriptsError(const std::vector<MapScriptExtras::PrefabScript>& scripts);

// Every brush trigger and trigger_radius of the map as a script-tested volume (entities sharing a targetname merge).
std::vector<MapScriptExtras::TriggerVolume> readTriggerVolumes(const MapFile& map);

// Where a door brush model hinges: the vertical edge at one end of its longer horizontal side, at the middle of its
// thickness, on the floor of the brush. Key iw8r_hinge "x y z" names the hinge point itself; iw8r_hinge "far" takes the
// other end. ok is false when the entity has no brushes.
struct DoorHinge {
    bool ok = false;
    Vec3 pos;
    Vec3 normal{0, 1, 0};  // across the thickness
    double swing = 1;      // sign of the yaw that moves the free end along +normal
};
DoorHinge doorHingeOf(const Entity& e);

// The text the editor sets in the dvar iw8r_hot for one entity move.
std::string hotUpdateValue(unsigned seq, const std::string& targetname, const Vec3& origin, const Vec3& angles);

std::string mapScriptGsc(const std::optional<ExpFog>& fog, const std::vector<FxPlacement>& fx,
                         const std::vector<SoundPlacement>& sounds = {}, const MapScriptExtras& extras = {});

}  // namespace iw8r
