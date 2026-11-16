// IW8 Radiant - IW3-style brush contents and surface types for the collision JSON.
//
// The x64-zt converter reads IW3 source contents (`contents` per collision brush, `surfaceFlags` per collision
// material) and maps them to Replay values. This module derives the same source values from the editor's
// brushes so the compiler and the leak / gameplay checks agree on what blocks what.
//
// Citations "x64-zt:LINE" refer to E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp unless noted.
#pragma once

#include <cstdint>
#include <string>

#include "map_text.h"

namespace iw8r {

// IW3 source contents bits used by this module (x64-zt:6927-6945 ConvertContents and its comments).
constexpr uint32_t kIw3ContentsSolid = 0x1;        // solid; ConvertContents sets it for 0x10 glass too
constexpr uint32_t kIw3ContentsFoliage = 0x2;      // foliage (x64-zt:6929-6932)
constexpr uint32_t kIw3ContentsGlass = 0x10;       // IW3 glass, becomes solid (x64-zt:6935-6939)
constexpr uint32_t kIw3ContentsShootThrough = 0x40; // "can shoot through" clip, dropped by the mask (x64-zt:6935-6937)
constexpr uint32_t kIw3ContentsItemClip = 0x400;   // item clip (x64-zt:6940-6942)
constexpr uint32_t kIw3ContentsSky = 0x800;        // sky, dropped by the remapping (x64-zt:7097)
constexpr uint32_t kIw3ContentsAiNoSight = 0x1000; // AI no-sight (x64-zt:6941)
constexpr uint32_t kIw3ContentsPlayerClip = 0x10000;  // player clip (x64-zt:6944-6945)
constexpr uint32_t kIw3ConvertMask = 0x33683;      // Replay bits kept by ConvertContents (x64-zt:6933)
// Trigger hull contents (compile_iw8.cpp kTriggerContents; x64-zt AddBrushlessTriggerBoxes comment).
constexpr uint32_t kIw3TriggerContents = 0x8000001u;

// Source contents of one brush as the collision JSON wants them.
struct BrushContents {
    uint32_t contents = 1;   // IW3-style bits written to the collision brush "contents"
    bool collides = true;    // false: no world collision (trigger, hint, skip, origin, portal, water, nonColliding)
    bool rendered = true;    // false: tool surface, not drawn (clip, nodraw, caulk, sky, trigger, ...)
    bool ladder = false;     // a face uses the ladder tool material: climbable (compiler writes ladder_planes)
    bool water = false;      // a face uses the water tool material: swimmable volume (no collision)
    std::string reason;      // human readable derivation, one clause per face class or directive
};

// Contents of a brush from its face materials and its `contents X;` directives.
//
// Face classes (matched case-insensitively on the last path component of the material, e.g.
// "tm2co/clip" is "clip"). Bits as observed in x64-zt:6927-6945; where the converter does not name a bit
// the entry says UNKNOWN in its reason:
//   clip           contents 0x1,     collides, not drawn   (solid clip)
//   clip_player    contents 0x10000, collides, not drawn   (player clip)
//   clip_weapon    contents 0x400,   collides, not drawn   (item clip bit; weapon bit assumed, UNVERIFIED)
//   clip_nosight   contents 0x1000,  collides, not drawn   (AI no-sight; the 0x40 shoot-through bit is masked off)
//   clip_missile   contents 0,       no collision, not drawn (missile clip bit UNKNOWN, so no collision written)
//   ladder         contents 0x1,     collides, not drawn   (climbable faces come from ladder_planes, doc 15 §2.3)
//   water          contents 0x20,    collision (not solid), drawn   (IW3 water bit kept by the fork; swimming UNVERIFIED)
//   nodraw, caulk  contents 0x1,     collides, not drawn
//   sky            contents 0x1,     collides, not drawn   (0x800 sky bit is dropped by ConvertContents)
//   trigger        contents 0x8000001, no collision, not drawn (trigger hull contents, compile_iw8.cpp)
//   portal, hint, skip, origin   contents 0, no collision, not drawn
// Any other material is structural: contents 0x1, collides, drawn.
// A brush collides when any face collides; its contents are the OR of the colliding faces' contents
// (or of all faces when none collides). It is drawn when any face is drawn.
//
// Directives (a `contents X;` line inside the brush body, doc 02 §3 / samples iw8_sample_room.map):
//   detail        collides and is drawn; does not seal a leak (see brushIsDetail)
//   nonColliding  collides = false, contents = 0
//   weaponClip    contents |= 0x400, collides
// Other words are ignored and noted in `reason`.
BrushContents brushContents(const Primitive& brush);

// True when the brush body has `contents detail;` (case-insensitive word).
bool brushIsDetail(const Primitive& brush);

// IW3 surface type (1-based, the value stored in bits 20..24 of a collision material's surfaceFlags)
// for a material name, or 0 for default / no type. Matching is by name tokens (split on non-alphanumeric
// characters, last path component only), in the priority order documented in iw8_contents.cpp, so
// "tm2co/me_curb_stone_01" -> rock (17) and "tm2co/destruction_debris_plaster_concrete_edge_01" -> concrete (5).
// Types: 1 bark, 2 brick, 3 carpet, 4 cloth, 5 concrete, 6 dirt, 7 flesh, 8 foliage, 9 glass, 10 grass,
// 11 gravel, 12 ice, 13 metal, 14 mud, 15 paper, 16 plaster, 17 rock, 18 sand, 19 snow, 20 water, 21 wood,
// 22 asphalt, 23 ceramic, 24 plastic, 25 rubber, 26 cushion, 27 fruit, 28 paintedmetal.
int iw3SurfaceTypeForMaterial(const std::string& material);

// surfaceFlags value whose (flags >> 20) & 31 is `type`; 0 for type 0 or an out-of-range type (1..28 valid).
// x64-zt:7118-7122 reads that field.
uint32_t iw3SurfaceFlagsForType(int type);

// Single bit a material's surfaceTypeBits carries for `type` (1u << (type - 1)); 0 when out of range.
// x64-zt:2566 reads surfaceTypeBits, and x64-zt:7122 / iw3_fastfile.cpp:5136 pass the bit to NativeSurfaceMaterial.
uint32_t iw3SurfaceTypeBits(int type);

// Replay surface type for an IW3 surface type, from NativeSurfaceMaterial (x64-zt:6949-6986). The IW3 types
// 14 (mud) -> 15, 15 (paper) -> 16, ..., 25 (rubber) -> 26, 26 (cushion) -> 35. 0 when unmapped.
// The IW3 surface type of a stock material from its Replay surfaceFlags (the type sits in bits 19 and up: a traffic
// cone's 0xC80000 is 25, plastic). 0 when the flags carry no type this table knows; callers then use the name.
int iw3SurfaceTypeForStockSurfaceFlags(long long surfaceFlags);
int replaySurfaceTypeForIw3(int type);

// Name of an IW3 surface type (the table above); "default" for 0, "" for out of range.
const char* iw3SurfaceTypeName(int type);

// What a surface type does in game (6.16): the bullet-impact effect family the converter's impact table (x64-zt
// replay_impact_data.h, folder vfx/iw8/weap/_impact/<family>/) uses for it, and the stock scrape step alias when one
// exists (common_mp Step_scrape_*). Surface types without their own family fall back to the closest one. Footstep
// sounds come from the game's surface sound table and are not listed. Empty strings for out of range.
struct SurfaceFeedback {
    const char* impactFamily;  // e.g. "concrete", "wood_solid", "generic"
    const char* scrapeAlias;   // e.g. "Step_scrape_concrete", "" when none
};
SurfaceFeedback surfaceFeedback(int iw3Type);

// ConvertContents (x64-zt:6927-6947) applied to a source contents value.
uint32_t iw3ConvertContents(uint32_t source);

}  // namespace iw8r
