// FX preview simulation for stock Replay ParticleSystemDef effects (ACTS pool "vfx"), research 50 and 138.
// It is a preview built from the decoded modules, not the game's simulation: the game integrates particles per frame,
// lights lit particles from the scene, runs physics and child effects; this samples each particle's state at a time t
// from its birth, deterministically. Input is one structured ACTS export (format "mw19-asset-json", root_type
// "ParticleSystemDef"), the same file src/core/vfx_info.* reads.
//
// Read from the export (asset.fields; research 138 names each path and the schema / x64-zt lines):
//   name.string; emitterDefs.values[i]: flags (0x2 = burst mode), particleSpawnRate, particleBurstCount, particleLife,
//   emitterDelay, emitterLife, particleCountMax, fadeOutMaxDistance, stateDefs.values[0].elementType and its three module
//   groups (0 init, 1 update). Modules are matched on moduleData.active_member (the union member; the printed
//   moduleType names alias: value 0 prints as PARTICLE_MODULE_INIT_BEGIN but is the atlas). When a state holds two
//   modules of one kind, the one without m_flags & 0x1 is taken (research 138 section 10, INFERRED).
//   - initAttributes: m_sizeMin/Max, m_colorMin/Max, m_velocityMin/Max (one random per vector for size and colour, one
//     per component for velocity).
//   - initMaterial: every Material reference (m_linkedAssetList); a particle picks one at random.
//   - initRelativeVelocity: m_velocityType (0 local, 1 world, 2 relative to the effect origin, 3 / 5 with bolt info =
//     local for a placed effect).
//   - spawn shapes: initSpawnShapeBox (absolute m_dimensionsMin/Max), Sphere (m_radius, ONLY_ON_SURFACE), Cylinder
//     (m_radius, m_halfHeight, m_normalAxis), Ellipsoid (m_radiusMin/Max); m_offset, m_axisFlags (allowed half-axes).
//   - graphs (curves over age / life, control points lerped, x scale; two curve sets, m_flags & 0x10 = a random lerp
//     between them per particle, else set A; firstCurve = the graph replaces the start value, else multiplies it):
//     colorGraph (RGBA), sizeGraph (x width, y height), velocityGraph (inches/s, m_flags & 0x80 = world axes, added to
//     the start velocity), rotationGraph (rad/s with m_useRotationRate), intensityGraph (x RGB), forceDragGraph (drag k).
//   - gravity (m_gravityPercentage x 800 in/s^2 down world Z), force (m_forceMin/Max, & 0x80 world), initRotation
//     (m_rotationAngle, m_rotationRate), initAtlas (start frame, loops, random start, play over life, frame curves),
//     initMirrorTexture (0 none, 1 always, 2 random), initTail (m_scaleWithVelocity), initOrientedSprite (a quaternion).
// Defaults where the export has no value are the constants in vfx_sim.cpp (size 1 inch, white, life 1 s, no motion).
//
// Behaviour of sampleVfx:
//   - Burst particles (flags & 0x2, or an emitter without a spawn rate) spawn at the emitter delay. Continuous emitters
//     spawn at delay + k / rate for the emitter life (0: as long as the effect plays); at most particleCountMax of them
//     are alive (the newest are kept).
//   - A particle is returned when spawned at or before t with age < its life. Position = spawn point + start velocity x
//     age + the velocity graph's integral + half the constant acceleration x age^2; with a drag graph the motion is
//     stepped instead. Everything is in the effect's axes turned into the placement's (VfxAxes; identity = effect space).
//   - At most kVfxMaxParticles (2000) particles per call; emitters in index order.
#pragma once

#include "geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

constexpr size_t kVfxMaxParticles = 2000;

// One curve: control points (time 0..1, value), linearly interpolated and held outside, times `scale`.
struct VfxCurve {
    std::vector<float> t, v;
    float scale = 0;
    float eval(float x) const;
};

// A graph module: curve set A and set B of the same length (research 138 section 2).
struct VfxGraph {
    std::vector<VfxCurve> a, b;
    bool present = false;
    bool random = false;   // m_flags & 0x10: per particle a random lerp between A and B
    bool replace = true;   // firstCurve: the graph is the value (false: it multiplies the start value)
    bool world = false;    // m_flags & 0x80 (velocity graph): world axes
    // Curve k at age fraction x with the particle's random r (A when !random).
    float eval(size_t k, float x, float r) const;
};

struct VfxEmitterSim {
    std::string name;            // "emitter<index>" (the export has no per-emitter name)
    std::string material;        // the first Material reference of the initMaterial module, or ""
    std::vector<std::string> materials;  // every Material reference (a particle picks one)
    double spawnRate = 0;        // particles per second; 0 means no continuous spawn (burst only)
    int burstCount = 0;          // particles spawned together at startDelay (burst mode)
    bool burst = false;          // emitter flags & 0x2 (research 138 section 8)
    int countMax = 0;            // particleCountMax: alive cap of a continuous emitter (0 = none)
    double fadeOutMaxDistance = 0;  // the emitter is not drawn beyond this distance (0 = no limit)
    int element = 0;             // stateDefs[0].elementType: 0 billboard, 8 oriented sprite, 10 tail (others not drawn)
    double lifeMin = 1.0, lifeMax = 1.0;   // particle life range, seconds
    double startDelay = 0;       // seconds before the emitter starts
    double emitterLife = 0;      // seconds the emitter keeps spawning (0 = while the effect plays)
    double velMin[3] = {0, 0, 0}, velMax[3] = {0, 0, 0};  // start velocity range, inches/s
    int relVelType = 0;          // initRelativeVelocity: 0 local, 1 world, 2 from the effect origin outward
    double gravity = 0;          // constant pull down world Z, inches/s^2 (hand-made emitters and tests)
    double gravityMin = 0, gravityMax = 0;  // the gravity module's fraction of 800 in/s^2
    double forceMin[3] = {0, 0, 0}, forceMax[3] = {0, 0, 0};
    bool forceWorld = false;
    double sizeStart = 1.0, sizeEnd = 1.0;  // inches; lerped over life when there is no attribute size or size graph
    bool hasAttrSize = false, hasAttrColor = false;
    double sizeMin[2] = {1, 1}, sizeMax[2] = {1, 1};      // initAttributes width, height
    float colorStart[4] = {1, 1, 1, 1}, colorEnd[4] = {1, 1, 1, 1};
    float colorMin[4] = {1, 1, 1, 1}, colorMax[4] = {1, 1, 1, 1};
    bool looping = false;        // the effect plays on (IW7 bits 0x8 / 0x10, or a continuous emitter without a life)
    VfxGraph color, size, velocity, rotation, intensity, drag;
    // emissiveGraph (curves 0-2 RGB, 3 intensity, 4 a second term; research 138 section 2): the colour an unlit set
    // adds. The unlit effect and flare pixel shaders read this colour and only the alpha of the particle colour
    // (VERIFIED in their disassembly, research 139 section 5; that the vertex's emissive is this graph is INFERRED).
    VfxGraph emissive;
    double rotAngleMin = 0, rotAngleMax = 0, rotRateMin = 0, rotRateMax = 0;  // radians, rad/s
    struct Shape {
        int type = 0;            // 0 none (the origin), 1 box, 2 sphere, 3 cylinder, 4 ellipsoid
        double offset[3] = {0, 0, 0};
        double dimMin[3] = {0, 0, 0}, dimMax[3] = {0, 0, 0};   // box (absolute); ellipsoid radii
        double radiusMin = 0, radiusMax = 0, halfHeight = 0;
        int axis = 2;            // cylinder axis (0 x, 1 y, 2 z)
        bool surface = false;    // ONLY_ON_SURFACE
        int axisFlags = 63;      // allowed half-axes: +x 1, -x 2, +y 4, -y 8, +z 16, -z 32
    } shape;
    struct Atlas {
        bool present = false;
        int start = 0, loops = -1;
        bool randomIndex = false, overLife = false, random = false;
        VfxCurve c0, c1;
    } atlas;
    int mirrorH = 0, mirrorV = 0;  // 0 none, 1 always, 2 random
    bool tailScaleWithVelocity = false;
    double orientQuat[4] = {0, 0, 0, 1};  // oriented sprite, effect axes (x, y, z, w)
    // A flare (element 3, research 139 section 1): INIT_FLARE. position: where along the line from the source through the
    // screen's centre the piece sits (1 at the source, 0 at the centre, -1 mirrored); the radial scales are lerped by
    // curves 0 / 1, the radial rotation by curve 2 and the depth scale by curve 3 (INFERRED, section 1.3).
    struct Flare {
        bool present = false;
        double position = 1;
        double direction[3] = {0, 0, 0};
        int angularRotCount = 0;
        uint32_t flags = 0;
        double depthRangeMin = 0, depthRangeMax = 0, depthValueMin = 1, depthValueMax = 1;
        double radialRotMin = 0, radialRotMax = 0;
        double scaleXMin = 1, scaleXMax = 1, scaleYMin = 1, scaleYMax = 1;
        VfxCurve curves[4];
    } flare;
    // INIT_OCCLUSION_QUERY: the source's visibility test (a 3x3 screen mask around the source, Replay's
    // cs_flare_occlusion; world size of the tested area, its scale).
    struct Occlusion {
        bool present = false;
        double scale = 1;
        double worldSize[2] = {0, 0};
        double depthBias = 0;
        bool offscreenRaycast = false;
    } occlusion;
};

struct VfxSimDef {
    std::string name;                        // ParticleSystemDef name
    std::vector<VfxEmitterSim> emitters;
    double duration = 0;                     // seconds until the last particle of a one-shot effect dies; -1 when looping
};

struct VfxParticle {
    int emitter = 0;            // index into VfxSimDef::emitters
    double pos[3] = {0, 0, 0};  // inches, relative to the effect origin (placement axes applied)
    double size = 0;            // inches (width)
    double size2[2] = {0, 0};   // width, height (a tail: width, length)
    float rgba[4] = {0, 0, 0, 0};  // linear colour x intensity (may exceed 1), alpha
    float emissive[3] = {0, 0, 0};  // the emissive graph's RGB x its intensity (hasEmissive)
    bool hasEmissive = false;
    double vel[3] = {0, 0, 0};  // inches/s at t (placement axes)
    double angle = 0;           // sprite rotation, radians
    double atlasPos = -1;       // play over life: 0..1; -1 otherwise
    double atlasFrames = 0;     // frames played (the integral of the frame-rate curve)
    double rnd = 0;             // a per-particle unit random (atlas start frame)
    bool flipU = false, flipV = false;
    int material = 0;           // index into the emitter's materials
    double age = 0, life = 0;
};

// The effect's axes in the world (forward x, left y, up z; each a unit vector), from the placement's angles.
struct VfxAxes {
    double x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
};
// The axes of a placement's pitch / yaw / roll (degrees, the game's angles order).
VfxAxes vfxAxesFromAngles(double pitch, double yaw, double roll);

// Builds the simulation definition from the export text. Returns false and sets *err when the text is not an mw19
// ParticleSystemDef export (same checks as parseVfxExport in vfx_info.h). Defaults apply to missing modules and fields.
bool buildVfxSim(const std::string& exportJson, VfxSimDef& out, std::string* err = nullptr);

// Particles alive at time t (seconds from the effect start). Deterministic for the same (def, t, seed, axes).
// Returns an empty list for t < 0 and for a one-shot effect with t > def.duration.
std::vector<VfxParticle> sampleVfx(const VfxSimDef& def, double t, uint32_t seed = 1, const VfxAxes* axes = nullptr);

// The atlas frame of a particle for a material of `frameCount` frames (research 138 section 5): play over life
// = start + floor(p x (N - start)); else the start (random when randomIndex) + the frames played, wrapping forever
// (loops -1) or `loops` times then holding the last frame. 0 without an atlas or when frameCount < 2.
int vfxAtlasFrame(const VfxEmitterSim& e, const VfxParticle& p, int frameCount);

// Research 132 G13 (the world distortion pixel shader, VERIFIED) applied to an effect quad (research 139 section 2):
// the distortion image texel m (slot 157, each channel 0..1) and the vertex alpha a give
//   k = saturate(m.z a - 0.25) x 4/3,  D = scale.x k (2 m.x - 1) T + scale.y k (2 m.y - 1) B
// (T, B the quad's unit tangent and bitangent in the world, scale = material constant 127.xy in inches): the pixel
// shows the scene at its point + D, with alpha saturate(4 m.z a).
struct VfxDistortion {
    double offset[3] = {0, 0, 0};  // D, inches
    double k = 0;
    double alpha = 0;
};
VfxDistortion vfxDistortion(const double m[3], double vertexAlpha, const double scale[2], const double tangent[3], const double bitangent[3]);

// The screen quad of one flare piece (research 139 section 1.3, INFERRED from the stock flares' data and the converter's
// notes). sourceNdc = the effect's origin on screen (x right, y up, -1..1), aspect = width / height, depth = its distance
// from the camera (inches), size = the particle's attribute size (percent of the screen height).
struct VfxFlareSprite {
    double centre[2] = {0, 0};  // NDC
    double half[2] = {0, 0};    // half width, half height, NDC
    double angle = 0;           // radians, counter-clockwise
    double radial = 0;          // the source's distance from the centre, 0..1 (1 = a screen corner)
};
VfxFlareSprite vfxFlareSprite(const VfxEmitterSim& e, const double size[2], const double sourceNdc[2], double aspect, double depth);

// 8.9: the space the effect's particles cover (each particle's centre +- half its size), sampled at `steps` times over
// its run (one-shot: its duration; looping: `loopSeconds`). Invalid box when nothing is alive at any sample.
AABB vfxBounds(const VfxSimDef& def, double loopSeconds = 4.0, int steps = 48, uint32_t seed = 1);

}  // namespace iw8r
