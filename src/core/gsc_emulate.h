// IW8 Radiant Suite - map script emulation: the map's own GSC (worldspawn iw8r_script) run in the editor, so the
// entities it moves move in the viewport as they do in game (owner 2026-11-15: the showcase's script_models "should be
// made live thru game script emulation").
//
// A small interpreter for the subset of GSC that map scripts use to animate entities. It runs deterministically on a
// clock that starts at 0 when user_main() starts (the Behaviour Preview clock, or the play preview's time):
// advanceTo(t) runs every thread up to t (going back in time starts again from 0) and poses() says where each entity
// the script moved, turned, hid or deleted is at t.
//
// Supported:
//   - functions `name(a, b) { ... }` (names are case-insensitive, as in GSC); user_main() starts at time 0. Calls
//     `f(args)`, `obj f(args)` (self = obj), `thread f(args)`, `level thread f(args)`, `obj thread f(args)`
//     (childthread is thread), `[[ ::f ]](args)`, `::f` function references, return values. #include / #using lines
//     and /# ... #/ developer blocks are skipped.
//   - locals, `level.x` / `self.x` / `ent.x` fields, arrays (`[]`, `a[i]`, `a["key"]`, `.size`), vectors `(x, y, z)`
//     and `v[0]`, numbers, strings (`&"..."` and `#"..."` too), undefined / true / false.
//   - statements: `=` `+=` `-=` `*=` `/=` `%=` `|=` `&=` `^=`, `++` / `--`, if / else, while, for (;;) /
//     for (i = 0; i < n; i++), foreach (v in a) / foreach (k, v in a), switch / case / default, break, continue,
//     return, `wait <seconds>`, waitframe(), waittillframeend.
//   - operators: + - * / % (vectors and string concatenation too), == != < > <= >=, && || !, & | ^ ~ << >>.
//   - entities: getent / getentarray (value, key) by any key ("targetname", "target", "script_noteworthy",
//     "classname" ...), getstruct / getstructarray, spawnstruct(), spawn(classname, origin) (invisible), `ent.origin`,
//     `ent.angles` (read: where it is now; write: put it there) and its map keys as fields.
//   - entity methods: rotateyaw / rotatepitch / rotateroll (angle, time[, accel, decel]), rotateto and rotateby
//     (angles, time[, accel, decel]), rotatevelocity (degrees per second, time[, accel, decel]), movex / movey /
//     movez (distance, time[, accel, decel]), moveto (point, time[, accel, decel]), movegravity (velocity, time),
//     linkto (entity) / unlink, hide / show, delete, notify / waittill / endon (with arguments; "movedone" and
//     "rotatedone" are sent when a move or turn ends), getorigin. Accel / decel give the game's trapezoid speed
//     curve; rotateto takes the shorter way per angle.
//   - functions: isdefined, int, float, abs, min, max, floor, ceil, sqrt, squared, sin / cos / tan (degrees),
//     randomint / randomfloat / randomintrange / randomfloatrange (a fixed seed: the same every run), gettime,
//     distance / distancesquared / distance2d, length / lengthsquared, vectornormalize, vectordot, vectorcross,
//     vectortoangles, anglestoforward / anglestoright / anglestoup, getarraykeys, isarray, isstring, tolower,
//     getdvar / getdvarint / getdvarfloat (empty / 0).
//   - calls that do nothing visible here (sounds, effects, hint strings, prints, solid, ...) do nothing.
// Not emulated: players, triggers firing (a thread waiting for "trigger" waits for ever), damage, animations, model
// swaps (setmodel), effects. Anything else - an unknown function or method, a statement that does not parse - is
// skipped with a note (notes(): the line and what was skipped, each once); a loop without a wait stops its thread with
// a note. Nothing the script does can crash the editor.
//
// Where entities are: a point entity at its origin / angles keys; a brush entity (script_brushmodel, triggers) at its
// origin key or else the map origin, as the build writes it (a plain brush model turns about the map origin in game,
// research 131 G1).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

// Where the script has one map entity now.
struct GscPose {
    Id entity = kNoId;
    std::string targetname;
    bool brush = false;            // a brush entity (drawn by its brushes), else a point entity / model
    Vec3 restOrigin, restAngles;   // where it is in the map
    Vec3 origin, angles;           // where the script has it (angles: pitch yaw roll, degrees)
    bool visible = true;           // false after hide(), and for ever after delete()
    bool deleted = false;
    // Every point p of the entity as the map has it goes to pivot + R (p - pivot) + move (the play preview's pose).
    double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 pivot, move;
};

struct GscNote {
    int line = 0;  // 1-based line of the script; 0 = the whole script
    std::string text;
};

class GscEmulation {
public:
    GscEmulation();
    ~GscEmulation();
    GscEmulation(GscEmulation&&) noexcept;
    GscEmulation& operator=(GscEmulation&&) noexcept;

    // Parses `source` and takes the map's entities. False when nothing can run (no user_main(), or no source); the
    // notes say why. A script that loads is at time 0, nothing run yet.
    bool load(const std::string& source, const MapFile& map);
    void clear();
    bool loaded() const;
    // Runs the script up to `t` seconds (earlier than now: starts again from 0, so the result depends on t only).
    void advanceTo(double t);
    double time() const;
    // The entities the script has moved, turned, linked, hidden or deleted so far, at time(). Entities made with
    // spawn() are not listed (nothing in the map draws them).
    const std::vector<GscPose>& poses() const;
    const GscPose* pose(const std::string& targetname) const;
    // Skipped statements and calls, parse problems, stopped threads: each (line, text) once, in the order met.
    const std::vector<GscNote>& notes() const;
    size_t functionCount() const;
    size_t threadCount() const;  // threads alive (running or waiting)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The map's own GSC: worldspawn iw8r_script, relative to the map's folder (as the build reads it, compile_iw8.cpp).
// "" when there is no key, no map path for a relative name, or the file cannot be read. `path` gets the file's path.
std::string readMapScript(const MapFile& map, const std::string& mapPath, std::string* path = nullptr);

// The map's script for a view: loads it (again when the map's entities, the script key or the file change), runs it to
// the asked time and keeps the notes the view has not shown yet.
class MapScriptRunner {
public:
    // `revision` is the document's revision: the map is looked at again only when it moves. The file is looked at
    // again at most every `recheckSeconds` of script time (and whenever the time goes back).
    void update(const MapFile& map, const std::string& mapPath, uint64_t revision, double t, double recheckSeconds = 0.5);
    void clear();
    bool active() const { return emu_.loaded(); }
    const std::vector<GscPose>& poses() const { return emu_.poses(); }
    const GscEmulation& emulation() const { return emu_; }
    const std::string& scriptPath() const { return path_; }
    // Notes not handed out before (each note once per script text; a reload with the same text gives none again).
    std::vector<GscNote> takeNewNotes();

private:
    GscEmulation emu_;
    std::string path_, text_;
    uint64_t revision_ = ~0ull, mapKey_ = 0, fileKey_ = 0;
    double lastCheck_ = -1e9;
    size_t notesShown_ = 0;
};

}  // namespace iw8r
