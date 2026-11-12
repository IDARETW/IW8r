// Simplified FX preview simulation for stock Replay ParticleSystemDef effects (ACTS pool "vfx").
// This is a preview, not the game's simulation: it spawns particles on a fixed schedule, draws them at the origin, and
// uses only the module values listed below. Input is one structured ACTS export (format "mw19-asset-json", root_type
// "ParticleSystemDef"), the same file that src/core/vfx_info.* reads. See docs/research/50_stock_effects.md.
//
// Field paths read from the export (asset.fields.* unless noted; schema in
// E:\mw124\atian-cod-tools\config\data\mw19\schema.json, ParticleSystemDef at ~17671, ParticleEmitterDef at ~17953):
//   name.string                                         effect name
//   numEmitters, emitterDefs.values[i]                  emitter rows (row count is used for the emitter list)
//   emitterDefs.values[i].flags                         looping: bits 0x8 or 0x10 (same rule as vfx_info.cpp)
//   emitterDefs.values[i].particleSpawnRate {min,max}   spawn rate, particles per second (midpoint)
//   emitterDefs.values[i].particleBurstCount {min,max}  burst count at the emitter start (midpoint, rounded)
//   emitterDefs.values[i].particleLife {min,max}        particle life range, seconds
//   emitterDefs.values[i].emitterDelay {min,max}        start delay, seconds (midpoint)
//   emitterDefs.values[i].emitterLife {min,max}         emitter life, seconds (midpoint); 0 = until the last particle dies
//   emitterDefs.values[i].stateDefs.values[0]...moduleDefs.values[j]
//       moduleType.name == PARTICLE_MODULE_INIT_ATTRIBUTES, moduleData.value:
//           m_sizeMin/m_sizeMax .v.m128_f32[0]          size range (x component, midpoint)
//           m_colorMin/m_colorMax .v.m128_f32[0..3]     colour range rgba (midpoint)
//           m_velocityMin/m_velocityMax .v.m128_f32[0..2]  velocity range, effect space Z-up, inches/s
//       moduleType.name == PARTICLE_MODULE_INIT_MATERIAL    first linked Material reference (asset_reference, type "Material")
//
// Defaults (used where the export has no value; each one is a constant in vfx_sim.cpp):
//   spawnRate 0 (no continuous spawn), burstCount 0, lifeMin/lifeMax 1.0 s, startDelay 0, emitterLife 0,
//   velMin/velMax (0,0,0), gravity 0, sizeStart/sizeEnd 1.0 inch, colorStart/colorEnd (1,1,1,1), material "", looping false.
// Not decoded on purpose: gravity (no gravity field in the emitter struct; the gravity module is not decoded), size and
// colour over life (the emitter graph modules), spawn shape, child effects and physics. For those, sizeEnd = sizeStart and
// colorEnd = colorStart, and gravity stays 0.
//
// Behaviour of sampleVfx (see vfx_sim.cpp for the code):
//   - Emitter i starts at startDelay. Burst particles all spawn at startDelay. Continuous particles spawn at
//     startDelay + k / spawnRate for k = 0, 1, 2, ...
//   - A looping emitter spawns forever. A one-shot emitter stops spawning after its window, which is emitterLife when it
//     is > 0 and the particle life max otherwise. VfxSimDef::duration is the time when the last particle of a one-shot
//     effect dies (-1 when any emitter loops).
//   - A particle is returned at time t when it was spawned at or before t and age < its life. Position is
//     v * age + 0.5 * g * age^2 with g = (0, 0, -gravity). Size and colour lerp from start to end by age / life.
//   - Randomness (life, velocity) comes from a hash of (seed, emitter, particle index), so the result is deterministic.
//   - At most kVfxMaxParticles (2000) particles are returned per call. Emitters are filled in index order.
#pragma once

#include "geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

constexpr size_t kVfxMaxParticles = 2000;

struct VfxEmitterSim {
    std::string name;            // "emitter<index>" (the export has no per-emitter name)
    std::string material;        // first Material reference of the INIT_MATERIAL module, or ""
    double spawnRate = 0;        // particles per second; 0 means no continuous spawn (burst only)
    int burstCount = 0;          // particles spawned together at startDelay
    double lifeMin = 1.0, lifeMax = 1.0;   // particle life range, seconds
    double startDelay = 0;       // seconds before the emitter starts
    double velMin[3] = {0, 0, 0}, velMax[3] = {0, 0, 0};  // velocity range, inches/s, effect space Z-up
    double gravity = 0;          // inches/s^2 pulling down (-Z); always 0 in this version (not decoded)
    double sizeStart = 1.0, sizeEnd = 1.0;  // inches
    float colorStart[4] = {1, 1, 1, 1}, colorEnd[4] = {1, 1, 1, 1};
    bool looping = false;        // infinite particle life or loop flag (bits 0x8 / 0x10, as in vfx_info.cpp)
    double emitterLife = 0;      // seconds the emitter keeps spawning (0 = until the particle life max); an addition to the
                                 // brief's fields, needed to compute the one-shot end time
};

struct VfxSimDef {
    std::string name;                        // ParticleSystemDef name
    std::vector<VfxEmitterSim> emitters;
    double duration = 0;                     // seconds until the last particle of a one-shot effect dies; -1 when looping
};

struct VfxParticle {
    int emitter = 0;      // index into VfxSimDef::emitters
    double pos[3] = {0, 0, 0};  // inches, relative to the effect origin, Z-up
    double size = 0;      // inches
    float rgba[4] = {0, 0, 0, 0};
};

// Builds the simulation definition from the export text. Returns false and sets *err when the text is not an mw19
// ParticleSystemDef export (same checks as parseVfxExport in vfx_info.h). Defaults apply to missing modules and fields.
bool buildVfxSim(const std::string& exportJson, VfxSimDef& out, std::string* err = nullptr);

// Particles alive at time t (seconds from the effect start). Deterministic for the same (def, t, seed).
// Returns an empty list for t < 0 and for a one-shot effect with t > def.duration.
std::vector<VfxParticle> sampleVfx(const VfxSimDef& def, double t, uint32_t seed = 1);

// 8.9: the space the effect's particles cover (each particle's centre +- half its size), sampled at `steps` times over
// its run (one-shot: its duration; looping: `loopSeconds`). Invalid box when nothing is alive at any sample.
AABB vfxBounds(const VfxSimDef& def, double loopSeconds = 4.0, int steps = 48, uint32_t seed = 1);

}  // namespace iw8r
