// IW8 Radiant Suite - Direct3D 11 offscreen renderer for the 3D camera view.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dirty_ranges.h"
#include "draw_chunks.h"
#include "reference_markers.h"
#include "stock_lights.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11DeviceContext1;
struct ID3D11Texture3D;
struct ID3D11Texture2D;
struct ID3D11Texture1D;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;
struct ID3D11SamplerState;
struct ID3D11Texture3D;

namespace iw8r {
struct StockProbeSet;  // stock_probes.h
}

namespace iw8r::app {

// Section timers for the viewport benchmark (off unless Prof::on is set; a scope costs two clock reads when on).
// Usage: IW8R_PROF("name"); at the top of a scope. dump() lists the total and the count per section, and resets.
struct Prof {
    static bool on;
    static void add(const char* name, long long ns);
    static std::string dump();
    // IW8R_BENCH_GPU=1 with on: the renderer drawing the frame also times every scope on the GPU (timestamp queries,
    // read back at the end of the frame, a stall the benchmark accepts); they are listed as "gpu.<name>".
    static void (*gpuMark)(const char* name, bool begin);
};
struct ProfScope {
    const char* name;
    long long t0;
    explicit ProfScope(const char* n);
    ~ProfScope();
};
#define IW8R_PROF_CAT2(a, b) a##b
#define IW8R_PROF_CAT(a, b) IW8R_PROF_CAT2(a, b)
#define IW8R_PROF(name) ::iw8r::app::ProfScope IW8R_PROF_CAT(profScope_, __LINE__)(name)

// Benchmark switch: true when `name` is in the comma list of the environment variable IW8R_PERF_OFF. Each name turns
// one optimisation of docs/research/85 off again, so its share can be measured on the same build: "inplace" (items
// written in place; everything is made again instead), "syncfast" (the scene cache's check for unchanged items),
// "settings" (worldspawn settings and file checks per frame), "shadow" (the sun shadow map only on caster changes), "refpoll" (waiting reference materials asked twice a second
// instead of every frame). Nothing is off in normal use.
bool perfOff(const char* name);

struct GpuVertex {
    float x, y, z;
    uint32_t rgba;  // R in the low byte
};

// Textured triangles: rgba multiplies the texture (shading and selection tint).
// An effect particle's corner (research 138): world position, atlas UV, linear colour x intensity (may exceed 1), alpha.
struct ParticleVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};
// One sprite image's particles, already sorted far to near. lit: the colour image's alpha blends and the scene's light
// scales it; else the emissive image adds (unlit effect sets: output alpha 0, research 138 section 6).
struct ParticleBatch {
    int texture = -1;
    bool lit = true;
    float emissive[3] = {1, 1, 1};  // an unlit material's emissive scale (constant 36) in Game Lighting
    std::vector<ParticleVertex> v;  // 6 per quad
};

struct GpuTexVertex {
    float x, y, z;
    float u, v;
    uint32_t rgba;
    // Surface normal and texture tangent (world), for normal-mapped shading (3.2); zero for surfaces without one.
    float nx = 0, ny = 0, nz = 0;
    float tx = 0, ty = 0, tz = 0;
    // View > Game Lighting: the vertex's lightmap UV in the preview bake (game_lightmap.h); negative = none.
    float lu = -1, lv = -1;
    // Edge data of a brush face for custom surface shaders (core face_edges packEdge); 0xFFFFFFFF = none.
    uint32_t edge = 0xFFFFFFFFu;
};

// A stock reference vertex's GpuTexVertex::edge: the triangle's GfxWorld surface index + 1 in the low 24 bits (0: of no
// known surface) and a zero top byte (edge data and "none" have 255 there). The vertex shader reads it for the Umbra
// tome cull (SceneRenderer::setReferenceSurfaceVisibility).
inline uint32_t referenceSurfaceTag(int64_t surface) {
    return surface >= 0 && surface < 0xFFFFFE ? uint32_t(surface) + 1u : 0u;
}

// 3.20 instanced static models: one model's triangles (model space) drawn once per instance. Each instance is three
// rows of a 3x4 transform: world_i = dot(row_i, (x, y, z, 1)).
struct InstancedMesh {
    std::vector<GpuVertex> tris;
    std::vector<float> rows;  // 12 floats per instance
    // Editor-only marker (props whose model is missing): on screen only, never in captures or the sun shadow map
    // (drawsReferencePiece in reference_markers.h).
    bool editorMarker = false;
};

// A textured instanced mesh (stock reference props with their textures): one material's triangles of a model, drawn
// once per placement. `rows` as in InstancedMesh.
// A textured instanced prop's mesh (model space): indexed, 28 bytes a vertex. The props used to be unindexed
// GpuTexVertex lists (168 bytes a triangle); Verdansk's filled the video memory and, with the kept copies, the commit
// limit of a 16 GB machine (2026-11-13).
struct PropVertex {
    float x, y, z;
    float u, v;
    uint32_t rgba;
    uint32_t normal;  // signed bytes x, y, z (w 0); zero = none (the pixel shader derives one)
    // The model's own vertex colour (sRGB bytes) of a VERTCOL material: x the albedo in Game Lighting (research 133
    // section 4); white for the others.
    uint32_t color = 0xFFFFFFFFu;
    // The static-model self visibility (research 105 section 4) as signed bytes: xyz the bent normal times the visible
    // amount, w = 1 + 126 x the cone value (0 = none).
    uint32_t selfVis = 0;
    // A lightmapped model's lightmap u, v (two unorm16, research 105 section 3.2) within its placement's rectangle.
    uint32_t lmapUv = 0;
    // The second UV set (stock `2` families; research 136), as stored; 0 when the model has none.
    float u1 = 0, v1 = 0;
};
// One extra layer of a model-layered prop material (research 132 G9): its colour and reveal textures (createTexture
// ids; reveal -1 = white) and the world layer pass's constants (layerP, layerTint, layerUvT, layerUvO).
struct PropLayer {
    int texture = -1, reveal = -1;
    float params[16] = {};  // layerP (on, flags, k, kind: 0 lerp, 1 multiply, 2 overlay), tint, UV transform
    int mask = -1;          // slot 126 (research 136), -1: none
    float aux[4] = {};      // row 8 of the layer buffer: the second UV set (1), the mask channel + 1
    int normal = -1;        // the layer's normal + gloss + cavity map (folded layers only)
    int tintMask = -1;      // slot 59 + L (flags & 64; folded layers only), -1: none
    float nrm[4] = {};      // normal mode + 1 (0: none), normal strength, gloss scale, gloss bias
};
struct PropMeshData {
    std::vector<PropVertex> vertices;
    std::vector<uint32_t> indices;  // triangles
};
inline uint32_t packPropNormal(float x, float y, float z) {
    const auto b = [](float c) {
        const float s = c < -1.f ? -1.f : c > 1.f ? 1.f : c;
        return uint32_t(uint8_t(int8_t(s >= 0 ? int(s * 127.f + 0.5f) : int(s * 127.f - 0.5f))));
    };
    return b(x) | (b(y) << 8) | (b(z) << 16);
}

// The stock props' light grid atlas for SceneRenderer::setStockLgvAtlas (core StockLgvAtlas; research 105): the two
// textures as they are and five float4 per box (rows 0..2, uvScale, uvBias).
struct LgvAtlasUpload {
    int width = 0, depth = 0;
    std::vector<uint32_t> tex0, tex1;
    std::vector<float> boxes;  // 20 floats per box
};

struct InstancedTexMesh {
    // Shared with the caller (a stock prop's converted mesh, kept between rebuilds): no copy per rebuild.
    std::shared_ptr<const PropMeshData> mesh;
    std::vector<float> rows;  // 12 floats per instance
    // Stock props: the model's last LOD distance (inches). The game stops drawing a placement beyond it (the grass
    // dissolves there with a dither); 0 = always drawn.
    float lodOut = 0;
    // Per instance, the scale on lodOut (the placement's lodScale, research 119); 0 = this placement is never left out
    // by distance. Empty: 1 for every instance.
    std::vector<float> cullScale;
    // The whole model's box in model space (mins, maxs): the distance for the LOD and the cull is measured to it, so
    // every material part of a placement goes at the same distance. hasModelBox false: this mesh's own box.
    bool hasModelBox = false;
    float modelBox[6] = {0, 0, 0, 0, 0, 0};
    // Per instance, the prop's light from its light grid box (research 105): 12 floats, DC rgb then the three first-
    // order terms per channel (slots 0, 2, 3), before bakedLightScale; DC r < 0 = none (the probes' fallback). Empty:
    // none for every instance.
    std::vector<float> light;
    // Stock prop LODs (plan 1.13): the meshes of one model's LODs share lodGroup (>= 0) and hold the same rows and
    // light in the same order; each draws the placements whose camera distance is in [lodNear, lodFar) (lodFar 0 = no
    // limit). -1: not part of a LOD group (every visible placement).
    int lodGroup = -1;
    float lodNear = 0, lodFar = 0;
    // Per instance, the placement's static model collection in the stock map (the Umbra tome's class 1 user ID), or
    // 0xFFFFFFFF for one the tome does not know (clutter, scripted objects). Empty: none known.
    std::vector<uint32_t> ids;
};

struct TexturedBatch {
    int texture = -1;  // id from SceneRenderer::createTexture
    std::vector<GpuTexVertex> tris;
    int normalTexture = -1;  // the material's normal map (createTexture), -1 = none
    int emissiveTexture = -1;  // 3.17 / 5.24: the material's emissive map (texture slot 18), added after lighting; -1 = none
    // 3.17: 0 opaque or alpha-tested (depth written, alpha below 0.3 cut), 1 alpha blended, 2 additive. Blended batches
    // draw after every opaque one, farthest first, without writing depth.
    int blend = 0;
    // 5.23 wind preview: foliage batches sway in the vertex shader; baseZ is the lowest point of the batch (no sway there).
    bool sway = false;
    float baseZ = 0;
    // An own material's specular map (RGB reflectance, alpha gloss; createTexture), -1 = none. Game Lighting draws its
    // highlight and reflection as the build's world shader does.
    int specTexture = -1;
};

// A vertex stream with the ranges the renderer has not uploaded yet.
struct GeoStreamBase {
    GeoStreamBase();
    uint32_t id;         // unique per stream object: the renderer's key for its GPU buffer
    DirtyRanges dirty;   // changed since the renderer took the stream
    bool all = true;     // everything changed (new size or layout)
    void markAll() { all = true; dirty.clear(); }
    void mark(size_t lo, size_t hi) {
        if (!all) dirty.add(lo, hi);
    }
    bool changed() const { return all || !dirty.empty(); }
    void taken() { all = false; dirty.clear(); }
};

template <class V>
struct GeoStream : GeoStreamBase {
    std::vector<V> v;
    std::vector<V> scratch;  // the next layout while a stream is put together again
};

// One textured batch (a texture, normal map, blend mode and sway flag), as TexturedBatch plus its change state.
struct SceneBatch : GeoStream<GpuTexVertex> {
    int key = 0;
    int texture = -1, normalTexture = -1, emissiveTexture = -1, specTexture = -1;
    int shader = -1;  // SceneRenderer::createCustomShader id of the material's custom shader (Game Lighting), -1 none
    int blend = 0;
    bool sway = false;
    float baseZ = 0;
    bool structural = true;  // slices moved since the renderer took it: its spatial chunks are made again
    bool moved = true;       // a vertex position changed since the renderer took it (not set by a colour-only change)
    bool seen = false, affected = false;  // scratch flags while the layout is rebuilt
    size_t firstChange = 0;
};

// What one frame draws: the camera's kept streams (scene.h CameraGeometry) and its own small ones. Null = nothing.
struct FrameInput {
    const float* viewProj = nullptr;  // row-major, row-vector convention (v * M)
    const float* clear = nullptr;
    // Flat triangles in draw order. shadowOnly: sun shadow pass only. casters, faceSel: both passes. noCast: screen only.
    GeoStream<GpuVertex>* shadowOnly = nullptr;
    GeoStream<GpuVertex>* casters = nullptr;
    GeoStream<GpuVertex>* faceSel = nullptr;
    GeoStream<GpuVertex>* noCast = nullptr;
    // Depth-tested lines, then lines drawn on top (in this order).
    GeoStream<GpuVertex>* lines = nullptr;
    GeoStream<GpuVertex>* linesMisc = nullptr;
    GeoStream<GpuVertex>* overlay[4] = {nullptr, nullptr, nullptr, nullptr};
    // Textured batches in creation order: the scene's, then the camera's own (sky box faces, effect particles).
    const std::vector<SceneBatch*>* batches = nullptr;
    const std::vector<SceneBatch*>* extraBatches = nullptr;
    uint64_t casterVersion = 0;  // the sun shadow map is drawn again when this changes
    bool geometryChanged = false;  // the kept geometry was brought up to date this frame (benchmark switch "shadow")
    bool clean = false;          // game surfaces only: no markers, tool faces, volumes or lines
    bool capture = false;        // a probe or clean capture: editor-only reference markers are left out
};

// Orthographic sun view over the box mins..maxs for the shadow map (row-vector, D3D depth 0..1). toward: unit vector
// toward the sun.
void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]);
// The same around a centre: radius R across the light, depth +-2 x depthRadius along it (the near shadow cascade covers a
// small box but takes casters from the whole scene).
void sunShadowMatrixAround(const double toward[3], const double centre[3], double R, double depthRadius, float out[16]);

class SceneRenderer {
public:
    ~SceneRenderer();
    bool init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string* err);
    void resize(int w, int h);
    // Draws one frame from the camera's kept streams; only the vertex ranges that changed are uploaded.
    void drawFrame(const FrameInput& in);
    // RGBA8 texture with a full mip chain (linear, wrap). Returns an id >= 0, or -1 on failure.
    int createTexture(const uint8_t* rgba, int width, int height);
    void releaseTextures();
    // 3.20 texture memory budget: GPU bytes of the textures made so far (with their mip chains).
    size_t textureBytes() const { return textureBytes_; }
    // A map's custom surface shader (core custom_shader): the textured pixel shader with the shader's iw8rSurface for
    // the Game Lighting colour. Returns its id (>= 0) or -1 with *err (the compiler's message).
    int createCustomShader(const std::string& source, std::string* err);
    static const char* texShaderSource();  // the base the custom shaders are made from
    // Compiles a textured pixel shader source (ps_tex, ps_5_0) without creating it: false with the compiler's errors.
    static bool compileCheck(const std::string& source, std::string* err);
    // Compiles a pixel shader (ps_5_0) to its bytecode: false with the compiler's errors.
    static bool compileProgram(const std::string& source, const char* entry, std::vector<uint8_t>& program, std::string* err);
    // The stock reference (world and props) drawn with this textured pixel shader source instead of the editor's own
    // (customShaderFilterViewportSource: a map shader's filter on surfaces that keep the game's lighting). Empty source:
    // the editor's own again. False with the errors when it does not compile (the editor's own is kept).
    bool setReferenceFilterShader(const std::string& source, std::string* err);
    bool referenceFilterShader() const { return filterPs_ != nullptr; }
    // Does the device take this compiled pixel shader (checksum and bytecode valid)? For checks of spliced programs.
    bool pixelShaderAccepted(const std::vector<uint8_t>& program);
    // Video memory the renderer holds, by kind (MB): "props a+b world c extras d streams e textures f lightmaps g
    // probes h shadows i targets j". Windows charges video memory to the process's commit, so this is part of what
    // the bench's memory.log shows.
    std::string gpuMemoryReport() const;
    // Static read-only reference triangles (stock map world geometry), uploaded once and drawn under the scene.
    // An empty vector clears it. Returns false when the buffer could not be created.
    bool setReference(const std::vector<GpuVertex>& tris);
    // Textured stock reference (3.19): batches uploaded once (vertex ranges in one static buffer); a batch draws with
    // its texture once setReferenceTexture gave it one, else with a plain white texture. Empty clears it. While a
    // textured reference is set, the flat reference is not drawn.
    // layers (optional, parallel to batches): 0 for a base batch, L >= 1 for layer L of a layered stock material, drawn
    // after every base batch (in layer order) with setReferenceLayer's blend; not drawn until then.
    bool setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers = nullptr);
    // A layer batch's blend (research 97): reveal = its alpha + reveal texture (-1: none, white), params = flags, reveal
    // sharpness k, tint RGB, UV transform (cos, sin, scale u, scale v, offset u, offset v); see the shader's cbuffer Layer.
    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);
    // A base batch's material constants (research 95), in the layer constant layout with layerP.x = 0:
    // layerP.y = 1 when the colour alpha holds the specular code (192..254), layerTint = (spec scale, spec bias, gloss
    // scale, gloss bias), layerUvO = (normal strength, 1 = constants given).
    void setReferenceBaseParams(size_t batch, const float params[16]);
    // Research 107 terms of a base batch / a prop mesh (RefLayer::extra layout) and their images (-1 = none).
    void setReferenceBaseExtra(size_t batch, const float extra[16], int emissiveTexture, int transmissionTexture);
    // A base batch's dual scrolling normal (research 133 section 5.3): constants 110 and 111; rows 12-13 of its record.
    void setReferenceBaseDualNormal(size_t batch, const float dual[8]);
    void setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture);
    // A prop mesh's flag / foliage waves (research 133 section 6): constants 79, 80, 81, 82.
    void setReferenceInstWave(size_t mesh, const float wave[16]);
    // A prop mesh's static-model displacement (research 133 section 3): its height image (slot 95) and constant 45,
    // P += normalize(N) (h.r scale + bias) in the vertex shader (record row 30, VS t43).
    void setReferenceInstDisp(size_t mesh, int texture, float scale, float bias) {
        if (mesh >= refTexInst_.size()) return;
        auto& g = refTexInst_[mesh];
        g.disp[0] = 1.0f, g.disp[1] = scale, g.disp[2] = bias, g.disp[3] = 0.0f;
        g.dispTex = texture;
        instParamsDirty_ = true;
    }
    // A prop mesh's extra layers (model-layered materials, research 132 G9), drawn over it after the props.
    // The first two are folded into the mesh's own pass (research 136); the rest are drawn as passes.
    void setReferenceInstLayers(size_t mesh, std::vector<PropLayer> layers) {
        if (mesh >= refTexInst_.size()) return;
        auto& g = refTexInst_[mesh];
        std::fill(std::begin(g.fold), std::end(g.fold), 0.0f);
        std::fill(std::begin(g.foldTex), std::end(g.foldTex), -1);
        g.foldMask = -1;
        std::fill(std::begin(g.coat), std::end(g.coat), 0.0f);
        g.coatTex[0] = g.coatTex[1] = -1;
        static const bool noCoat = std::getenv("IW8R_NO_COAT") != nullptr;
        static const bool noFold = std::getenv("IW8R_NO_LAYER_FOLD") != nullptr;  // A/B: every layer a pass
        size_t f = 0;
        std::vector<PropLayer> rest;
        for (PropLayer& L : layers) {
            if (L.params[3] > 3.5f) {  // the clear coat: its own block (research 137)
                if (noCoat || g.coat[0] > 0.5f) continue;
                const bool map = L.normal >= 0;
                const float c[16] = {1.0f, L.params[1], L.aux[1], map ? 1.0f : 0.0f, L.params[8], L.params[9], L.params[10], L.params[11],
                                     L.params[12], L.params[13], L.aux[0], map ? 1.0f : 0.0f, L.nrm[1], L.nrm[2], L.nrm[3], 0.0f};
                std::copy(c, c + 16, g.coat);
                g.coatTex[0] = L.normal, g.coatTex[1] = L.reveal;
                if (L.mask >= 0) g.foldMask = L.mask;
                continue;
            }
            if (noFold || f >= 2) {
                if (L.params[3] < 2.5f) rest.push_back(std::move(L));  // kind 3 (normal / gloss only): not as a pass
                continue;
            }
            float* r = &g.fold[16 * f];
            r[0] = 1.0f, r[1] = L.params[1], r[2] = L.params[2], r[3] = L.params[3];
            r[4] = L.params[4], r[5] = L.params[5], r[6] = L.params[6], r[7] = L.aux[1];
            for (int k = 0; k < 4; ++k) r[8 + k] = L.params[8 + k];
            r[12] = L.params[12], r[13] = L.params[13], r[14] = L.aux[0];
            g.foldTex[2 * f] = L.texture, g.foldTex[2 * f + 1] = L.reveal, g.foldTex[4 + f] = L.normal, g.foldTex[6 + f] = L.tintMask;
            for (int k = 0; k < 4; ++k) g.fold[32 + 4 * f + k] = L.nrm[k];
            if (L.mask >= 0) g.foldMask = L.mask;
            ++f;
        }
        g.layers = std::move(rest);
        instParamsDirty_ = true;
    }
    // The near-camera terrain layer patch's core (x, y, radius; radius 0 = none): the coarse terrain is not drawn there.
    void setTerrainCut(float x, float y, float radius) { terrainCut_[0] = x, terrainCut_[1] = y, terrainCut_[2] = radius; }
    // The loaded streamed tiles' areas (x0, y0, x1, y1; at most 256): the terrain's hole filler (vertex alpha 253) is
    // discarded inside them.
    void setStreamedTileBoxes(const std::vector<std::array<float, 4>>& boxes) {
        tileBoxes_ = boxes;
        if (tileBoxes_.size() > 256) tileBoxes_.resize(256);
        tileBoxesDirty_ = true;
    }
    // A batch of a stock decal material (sortKey 10-17, techset blend `_bad_`; research 98): blended over the opaque
    // world (premultiplied by its coverage B, depth tested with a small offset, no depth write), in sortKey order.
    // Layer 0 (layer == 0) takes params / reveal as a layer would (its weight is in the tangent length too).
    void setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]);
    // Decal batches added after the textured reference (stock decal volumes projected on the props once they are
    // loaded): their own vertex buffer, batch indices from referenceTexturedBatches() on; each is a decal waiting for
    // setReferenceDecal. A second call replaces the previous ones; setReferenceTextured drops them.
    bool appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches);
    // The extras as keyed groups (terrain layer patch, prop decals, each streamed world tile), each in its own vertex
    // buffer: a group given again with the same key and version (batches null) keeps its buffer and every batch's
    // textures and constants; only new or changed groups are uploaded. World groups hold ordinary world batches (base
    // and layer batches: drawn, cast sun shadows); the others are decal-like (setReferenceDecal makes them ready).
    struct ExtraGroupInput {
        uint64_t key = 0, version = 0;
        const std::vector<std::vector<GpuTexVertex>>* batches = nullptr;  // null: reuse this key's group
        std::vector<int> layers;  // world groups: per batch, 0 base / L >= 1 a layer batch
        bool world = false;
        bool nearOnly = false;    // drawn only in view and within 4,000 units (prop decals)
    };
    bool setReferenceExtras(const std::vector<ExtraGroupInput>& groups);
    // A stock batch's normal map (the flat one until then).
    // Layer batches of the stock reference: {all, ready to draw, multiply, with a normal map} (benchmark report).
    std::array<size_t, 4> referenceLayerStats() const {
        std::array<size_t, 4> s{refLayerOrder_.size(), 0, 0, 0};
        for (size_t k : refLayerOrder_) {
            s[1] += refLayers_[k].ready ? 1 : 0;
            s[2] += refLayers_[k].ready && refLayers_[k].params[3] > 0.5f ? 1 : 0;
            s[3] += k < refNrmIds_.size() && refNrmIds_[k] >= 0 ? 1 : 0;
        }
        return s;
    }
    // Decal batches: {world, world ready, appended (props), appended ready, appended triangles}.
    std::array<size_t, 5> referenceDecalStats() const {
        std::array<size_t, 5> s{};
        for (size_t k = 0; k < refLayers_.size(); ++k) {
            if (!refLayers_[k].decal) continue;
            const bool extra = k >= refTexExtraFrom_;
            s[extra ? 2 : 0]++;
            if (refLayers_[k].ready) s[extra ? 3 : 1]++;
            if (extra && k < refTexCount_.size()) s[4] += refTexCount_[k] / 3;
        }
        return s;
    }
    // Triangles stored in the textured reference: {base batches, layer batches, decal batches}.
    size_t layerPassDraws_ = 0, layerPassTris_ = 0, layerPassBatches_ = 0;
    std::array<size_t, 3> layerPassStats() const { return {layerPassBatches_, layerPassDraws_, layerPassTris_}; }
    // Base batches: total, with material constants, with the specular code in their colour alpha.
    // Batches whose material traces screen-space reflections: {base, layers}.
    std::array<size_t, 2> ssrBatchStats() const {
        std::array<size_t, 2> r{0, 0};
        for (const RefLayer& L : refLayers_) {
            if (L.layer == 0 && !L.decal && L.params[2] > 0.5f) ++r[0];
            if ((L.layer > 0 || L.decal) && L.params[0] > 0.5f && L.params[0] < 1.5f && L.params[7] > 0.5f) ++r[1];
        }
        return r;
    }
    std::array<size_t, 3> baseMaterialStats() const {
        std::array<size_t, 3> r{0, 0, 0};
        for (const RefLayer& L : refLayers_)
            if (L.layer == 0 && !L.decal) ++r[0], r[1] += L.params[13] > 0.5f, r[2] += L.params[1] > 0.5f && L.params[13] > 0.5f;
        return r;
    }
    std::array<size_t, 3> referenceTriangleSplit() const {
        std::array<size_t, 3> s{};
        for (size_t k = 0; k < refTexCount_.size(); ++k) {
            const bool layer = k < refLayers_.size() && refLayers_[k].layer > 0, decal = k < refLayers_.size() && refLayers_[k].decal;
            s[decal ? 2 : layer ? 1 : 0] += refTexCount_[k] / 3;
        }
        return s;
    }
    // An own (custom) material's shader state, keyed by its colour texture (createTexture id; the scene's batches are
    // one per texture). What the build writes into the material (compile_iw8.cpp customMaterialJson) and the converter's
    // world shader draws (map_surface_realtime.hlsl 1626-1640, 1770-1806, 1871-1896); Game Lighting only.
    struct TextureMaterial {
        bool unlit = false;     // the colour as is (unlit, additive and multiply materials)
        float emission = 0;     // an unlit material's glow gain (> 1), 0 = none
        bool multiply = false;  // dst x colour (blend zero / srccolor)
        bool glass = false;     // a blended glass material: premultiplied, the probe reflection added unscaled
        int sortKey = 4;        // blended batches draw in sort key order, then far to near (the converter's default 4)
        bool cullBack = false;  // one-sided (not twoSided): back faces are hidden in Game Lighting (ps_tex, batchFlags2.z = -1)
    };
    void setTextureMaterial(int texture, const TextureMaterial& m) {
        if (texture < 0) return;
        if (size_t(texture) >= texMat_.size()) texMat_.resize(size_t(texture) + 1), texMatSet_.resize(size_t(texture) + 1, 0);
        texMat_[size_t(texture)] = m, texMatSet_[size_t(texture)] = 1;
    }
    const TextureMaterial* textureMaterial(int texture) const {
        return texture >= 0 && size_t(texture) < texMat_.size() && texMatSet_[size_t(texture)] ? &texMat_[size_t(texture)] : nullptr;
    }
    void setReferenceNormal(size_t batch, int textureId) {
        if (batch < refNrmIds_.size()) refNrmIds_[batch] = textureId;
    }
    // Reference props as instanced meshes (3.20): each model uploaded once, drawn with DrawInstanced in the camera and
    // the sun shadow pass. Empty clears them.
    bool setReferenceInstances(const std::vector<InstancedMesh>& meshes);
    size_t referenceInstances() const { return refInstTotal_; }
    // Textured instanced reference props: uploaded once, each drawn with the texture setReferenceInstTexture gave it
    // (a plain white texture until then). Empty clears them.
    bool setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes);
    // Cull by tome (View > Umbra Visibility): one bit per GfxWorld surface and one per static model collection, set =
    // drawn. The reference world's triangles of a hidden surface and the props of a hidden collection are not drawn on
    // screen (they still cast sun shadows); what the tome does not know is drawn. Empty vectors turn it off.
    void setReferenceTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits);
    void bindTomeVisibility();  // (internal) the surface bits for the vertex shader, uploaded when they changed
    // Custom surface shaders read the frame before (iw8rSceneDepth). True after a frame that was drawn from another
    // view than the depth it read, while a custom shader is in use: one more frame shows the settled picture.
    bool sceneDepthStale() const { return sceneZStale_ && sceneDepthWanted(); }
    // A shader that may read the scene depth is in use: a map's surface shader, or a filter with iw8rFilterScreen.
    bool sceneDepthWanted() const { return !customPs_.empty() || (filterPs_ && filterScreen_); }
    bool referenceTomeCull() const { return !tomeSurfBits_.empty() || !tomeModelBits_.empty(); }
    size_t tomeCulledProps() const { return tomeCulledProps_; }  // placements the tome hid in the last frame
    // The same cull for the stock map's lamps (bits by primaryLights row), its reflection probe boxes (by probe
    // instance) and its decal volume batches (by decal volume collection): the game sets a light's, a probe's and a
    // collection's visibility bit from the same query. Empty = nothing hidden. The map-wide fallback probe stays.
    void setReferenceTomeClasses(std::vector<uint32_t> lightBits, std::vector<uint32_t> probeBits, std::vector<uint32_t> decalBits);
    void setReferenceDecalCollection(size_t batch, int collection);
    size_t tomeCulledLights() const { return tomeCulledLights_; }   // lamps in view the tome hid in the last frame
    size_t tomeCulledProbes() const { return tomeCulledProbes_; }   // probe boxes hidden now
    size_t tomeCulledDecals() const { return tomeCulledDecals_; }   // decal batches the tome hid in the last frame
    // New per-placement light (InstancedTexMesh::light, 12 floats per instance) for mesh `mesh` of the last
    // setReferenceTexInstances: only its instance buffer is made again. False when the counts do not match.
    bool setReferenceTexInstanceLight(size_t mesh, const std::vector<float>& light);
    void setReferenceInstTexture(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].texture = textureId, instParamsDirty_ = true;
    }
    // A textured prop mesh's normal map (-1: the flat one) and material constants (setReferenceBaseParams layout).
    void setReferenceInstNormal(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].normal = textureId;
    }
    // A prop mesh whose material is a decal (paint bands, stencils and grime laid over the model's own surface): drawn
    // after the other props, pulled toward the camera, without writing depth, casting no shadow. Drawn as an opaque
    // mesh it fought the surface under it (owner 2026-11-13: a yellow band on mp_rust flickered with every move).
    void setReferenceInstDecal(size_t mesh, int sortKey) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].decal = sortKey;
    }
    // A see-through prop mesh (glass): drawn after the others, blended by its alpha, without writing depth.
    void setReferenceInstTranslucent(size_t mesh, bool on) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].translucent = on;
    }
    // A stock material's draw class (materialDrawFor: 0 drawn, 1 shadow only, 2 never) for a reference batch / prop mesh.
    void setReferenceDrawClass(size_t batch, int cls) {
        if (batch < refLayers_.size() && refLayers_[batch].drawClass != cls) refLayers_[batch].drawClass = cls, shadowDirty_ = nearDirty_ = true;
    }
    // A material that casts no sun shadow (surfaceFlags 0x40000: tree canopies, *_noshadow): drawn, left out of the
    // shadow maps. Owner 2026-11-13: shadows inside the trees' leaves were darker than in game.
    void setReferenceNoCast(size_t batch, bool on) {
        if (batch < refLayers_.size() && refLayers_[batch].noCast != on) refLayers_[batch].noCast = on, shadowDirty_ = nearDirty_ = true;
    }
    void setReferenceInstNoCast(size_t mesh, bool on) {
        if (mesh < refTexInst_.size() && refTexInst_[mesh].noCast != on) refTexInst_[mesh].noCast = on, shadowDirty_ = nearDirty_ = true;
    }
    void setReferenceInstDrawClass(size_t mesh, int cls) {
        if (mesh < refTexInst_.size() && refTexInst_[mesh].drawClass != cls) refTexInst_[mesh].drawClass = cls, shadowDirty_ = nearDirty_ = true;
    }
    void setReferenceInstParams(size_t mesh, const float params[16]) {
        if (mesh >= refTexInst_.size()) return;
        std::memcpy(refTexInst_[mesh].params, params, sizeof(refTexInst_[mesh].params));
        instParamsDirty_ = true;
    }
    size_t referenceTexInstanceMeshes() const { return refTexInst_.size(); }
    // Instanced draws of the textured meshes (a model with three materials counts each placement three times).
    size_t referenceTexInstances() const {
        size_t n = 0;
        for (const auto& g : refTexInst_) n += g.instances;
        return n;
    }
    void setReferenceTexture(size_t batch, int textureId) {
        if (batch < refTexIds_.size()) refTexIds_[batch] = textureId, layerParamsDirty_ = true;
    }
    // A block-compressed colour texture uploaded as it is stored (texture_decode loadDdsBlocks). Its alpha is the stock
    // specular code, never a cut-out: batches drawing it are opaque (the shaders get "raw spec", no alpha test).
    int createTextureBlocks(int dxgiFormat, int width, int height, const std::vector<std::string>& levels);
    bool textureRawAlpha(int id) const { return id >= 0 && size_t(id) < texRawAlpha_.size() && texRawAlpha_[size_t(id)]; }
    // A texture whose alpha is data (an RGBM sky face): never cut out by it.
    void setTextureRawAlpha(int id, bool on) {
        if (id >= 0 && size_t(id) < texRawAlpha_.size()) texRawAlpha_[size_t(id)] = on ? 1 : 0;
    }
    size_t referenceTexturedBatches() const { return refTexIds_.size(); }
    // 3.20: the stock reference is cut into spatial chunks (core draw_chunks.h) and only the chunks in the view
    // frustum are drawn. Off draws everything (for comparison).
    void setReferenceCulling(bool on) { refCulling_ = on; }
    bool referenceCulling() const { return refCulling_; }
    size_t referenceChunks() const { return refChunkTotal_; }
    size_t lastCulledReferenceChunks() const { return refCulledLast_; }
    size_t lastCulledSceneChunks() const { return texCulledLast_; }
    size_t referenceVertexCount() const { return refCount_; }
    void setReferenceVisible(bool on) {
        if (on != refVisible_) shadowDirty_ = true;
        refVisible_ = on;
    }
    // Exponential fog preview (setExpFog: 50% at `halfway` units past `start`). halfway <= 0 turns it off.
    // Colour grade preview (3.15): worldspawn iw8r_film (contrast, brightness, desaturation, invert, light / dark
    // tints) and an iw8r_lut 3D LUT, applied last to the camera's surfaces (lines keep their colours).
    void setGrade(bool film, float contrast, float brightness, float desaturation, bool invert, const float lightTint[3],
                  const float darkTint[3]) {
        grade_[0] = contrast, grade_[1] = brightness, grade_[2] = desaturation;
        gradeFlags_ = (gradeFlags_ & 4) | (film ? 1 : 0) | (invert ? 2 : 0);
        for (int k = 0; k < 3; ++k) grade_[4 + k] = lightTint[k], grade_[8 + k] = darkTint[k];
    }
    // size^3 RGBA texels, red fastest (core parseCubeLut); size 0 (or null) turns the LUT off.
    void setGradeLut(int size, const uint8_t* rgba);
    // Reflection preview (3.16): six size x size RGBA faces of a captured probe (D3D face order; the capture's world
    // directions match the cube's), sampled along the reflection vector on glossy surfaces. null = off.
    void setReflectionProbe(const uint8_t* faces, int size);
    bool reflectionProbeOn() const { return probeSrv_ != nullptr; }
    void setFog(const float eye[3], float start, float halfway, const float color[3]) {
        fog_[0] = eye[0], fog_[1] = eye[1], fog_[2] = eye[2], fog_[3] = start;
        fog_[4] = color[0], fog_[5] = color[1], fog_[6] = color[2], fog_[7] = halfway;
    }
    // Per-pixel light preview on textured surfaces (3.14): up to kMaxPixelLights omni / spot lights.
    static constexpr int kMaxPixelLights = 64;
    struct PixelLight {
        float pos[3], radius;
        float color[3], cosOuter;  // cosOuter < -1.5: omni
        float dir[3], cosInner;
    };
    void setPixelLights(const std::vector<PixelLight>& lights) { pixelLights_ = lights; }
    // View > Game Lighting (research 95, "Editor maps in game"): textured surfaces shade as the converter's world
    // shader shades a built map: albedo (sRGB decoded) x (lightmap A N.z + B sat(N.D) + sun visibility x N.L x
    // suncolor) + albedo x local lights in the game's units (pixel lights: colour already x the game intensity,
    // falloff 1 / d^2 in a smooth window), then the game's exposure (lutScale) and tone map LUT. `sky`: the light of
    // surfaces without a lightmap texel (models; the game's light grid, approximated by the sky and sun).
    struct GameLook {
        bool on = false;
        float sunToward[3] = {0, 0, 1};
        float sunColor[3] = {1, 1, 1};
        float sky[3] = {0.2f, 0.2f, 0.2f};
        bool skyIsStock = false;  // sky = the stock map's mean baked light: unlit surfaces take the probes at that level
        // An own map that the build gives no lightmap: surfaces take the converter's constant sky fill (research 117).
        bool skyIsUnbaked = false;
        // The stock map's atmosphere (core GameFog): fog0 = start, half distance, max opacity, on; fog1 = inscattered
        // radiance (rgb, fog colour x scale), Mie blend; fog2 = Rayleigh albedo, Mie anisotropy; fog3 = Mie albedo,
        // desaturation; fog4 = height base, height half distance, sky distance, sky intensity; fog5.x = max distance;
        // skyScale: the sky texture (LDR) to scene light.
        float fog[6][4] = {};  // only fog[0][3] (on) is read now; the rest of the fit is kept for the GUI's reports
        float fogK[8][4] = {};  // the game's fog constants cb7[131..138] (core gameFogConstants, research 111)
        // The volumetric fog (research 109 section 2): albedo rgb + air x 0.01; (absorption, ambient brightness, height
        // base, height exponent -ln2 / half or 0); (sun brightness, anisotropy g, on, 0).
        float vol[3][4] = {};
        // The volumetric air is an own map's (its haze, research 126): always drawn. The Volumetric Fog option is for
        // stock maps, where the match with the game is not calibrated yet.
        bool volOwn = false;
        // Volumetric boxes (an own map's iw8r_volume, research 127): per box centre + density, the three axes with the
        // half sizes, (1 / falloff, -0.1 x height fade, 0, 0). At most 8 (the nearest to the camera).
        float volBoxes[8][20] = {};
        int volBoxCount = 0;
        // Per box: four mask codes (picture slice x 4 + axis; below 0: none), then per mask 1 / sizeU, 1 / sizeV and
        // the two offset terms (core volume_mask, volumeMaskTerms).
        float volBoxMasks[8][20] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1},
                                    {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
        // Lamps that light the air (iw8r_volumetric): as PixelLight (position + radius, colour + cos outer, axis + cos
        // inner), the 8 nearest; the vision's spot and omni brightness (volumetricSpotBrightness / OmniBrightness).
        float volLamps[8][12] = {};
        int volLampCount = 0;
        float volLampBrightness[2] = {1, 1};
        float skyScale = 1.0f;
        float bakedScale = 1.0f;  // the stock map's bakedLightScale, on its lightmap
        float lutScale = 1.0f;
        // The map's post effects (research 109): the bloom veil's strength and the vignette's constants
        // (core gameVignetteConstants); zero = none.
        float veil = 0;
        float vignette[12] = {};
        float grain = 0;  // film grain strength (the vision's tonemapGrainStrength x 0.25; research 109 section 4.2)
        // The map's white balance (research 120, core gameWhiteBalanceMatrix): out = M in, row-major, on the linear
        // colour before the exposure. whiteBalanceOn false = none.
        bool whiteBalanceOn = false;
        float whiteBalance[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    };
    void setGameLook(const GameLook& g) {
        // The shadow maps' cut-outs follow the view's threshold (0.5 in Game Lighting, 0.3 otherwise): drawn again.
        if (g.on != game_.on) shadowDirty_ = nearDirty_ = true;
        game_ = g;
    }
    const GameLook& gameLook() const { return game_; }
    bool gameLookOn() const { return game_.on; }
    // The preview lightmap: width x height texels of four floats each (a, b, d as GameLightmap). null = none.
    void setGameLightmap(int width, int height, const float* a, const float* b, const float* d);
    bool hasGameLightmap() const { return gameLmSrv_[0] != nullptr; }
    // The stock reference world's own lightmap (Game Lighting on a stock map, research 94 / 95): four floats per texel,
    // a = (ambient part f x C, 1), b = (directional part (1 - f) x C / max(D.z, .001), 0), d = (the tangent-space
    // direction D, 0). The reference vertices carry its UVs. null = none.
    void setStockLightmap(int width, int height, const float* a, const float* b, const float* d,
                          const std::vector<std::array<float, 4>>& rects = {});
    bool hasStockLightmap() const { return stockLmSrv_[0] != nullptr; }
    // The stock world shader's environment BRDF table (64 x 64, A and B as R8G8; game_look_assets.h fetches it from the
    // owner's install). null = the analytic fit.
    void setEnvBrdfLut(int width, int height, const uint8_t* rg);
    // Stock local lights (research 106): the map's packed spot / omni records (core stock_lights.h). Each frame the
    // nearest 256 in view are uploaded (t20) and binned to 32-pixel screen tiles (t22); Game Lighting shades the stock
    // reference and its props with them. Empty = none. IW8R_NO_STOCK_LIGHTS turns them off (A/B).
    // shadows: per light (same order), its shadow camera (research 106 section 4); empty or cast = false: unshadowed.
    void setStockLocalLights(std::vector<iw8r::StockGameLight> lights, std::vector<iw8r::StockLightShadow> shadows = {}) {
        stockLights_ = std::move(lights);
        stockLightShadows_ = std::move(shadows);
        for (int& s : lightSlotOf_) s = -1;
        lightSlotOf_.assign(stockLights_.size(), -1);
        for (auto& sl : lightSlots_) sl = LightSlot{};
    }
    size_t stockLightShadowsDrawn() const { return lightShadowsDrawn_; }
    // View > Game Lighting switches (saved settings; the IW8R_NO_* environment variables still turn them off for benches).
    void setStockLightsEnabled(bool on) { stockLightsEnabled_ = on; }
    void setLightShadowsEnabled(bool on) { lightShadowsEnabled_ = on; }
    // Debug view of Game Lighting: 0 the lit scene, 1 the AO buffer, 2 the albedo, 3 the reflections mask.
    void setGameDebugView(int v) { gameDebugView_ = v; }
    // The AO strength (research 107 section 4.3: the vision's r_ssaoStrength, 1 on MP maps): ao = lerp(1, ao, s).
    void setGameAoStrength(float s) { gameAoStrength_ = s; }
    // Seconds on the game's clock for animated materials (research 133 section 1: UV animation by constant 63).
    void setGameTime(double seconds) { gameTime_ = seconds; }
    // The map's bloom veil and vignette (research 109) in Game Lighting.
    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }
    void setFilmGrainEnabled(bool on) { filmGrainEnabled_ = on; }
    void setWhiteBalanceEnabled(bool on) { whiteBalanceEnabled_ = on; }
    bool whiteBalanceDrawn() const { return whiteBalanceDrawn_; }  // the last frame's post pass applied a matrix
    void setVolumetricFogEnabled(bool on) { volumetricOn_ = on; }
    // The mask pictures of the volumetric boxes: each kVolMaskSize x kVolMaskSize grey bytes, at most kVolMaskSlices.
    // `version` names the set: the same number again does nothing.
    static constexpr int kVolMaskSize = 256, kVolMaskSlices = 4;
    void setVolumeMasks(const std::vector<std::vector<uint8_t>>& pictures, uint64_t version);
    bool veilDrawn() const { return veilDrawn_; }  // the last post pass mixed the veil (bench)
    float gameVeil() const { return game_.veil; }
    size_t stockLocalLightCount() const { return stockLights_.size(); }
    size_t stockLocalLightsDrawn() const { return stockLightsDrawn_; }
    // The map's IES strip (*ieslookup level 0: width R8 texels, texel 0 white), t21. null = every light white.
    void setIesStrip(int width, const uint8_t* r8);
    // The stock specular occlusion table (gtso_lut, one byte per texel, x fastest; research 95), t18.
    void setGtsoLut(int width, int height, int depth, const uint8_t* r);
    bool hasGtsoLut() const { return gtsoSrv_ != nullptr; }
    // The stock props' light grid atlas (research 105 sections 1.3-1.4, research 132 G7): texture 0 (R11G11B10F,
    // width x width x depth), texture 1 (RGBA8, 2 width x 3 width x depth, six tiles) and per box five float4 (the
    // world -> box rows, uvScale, uvBias). A prop instance whose light record starts with -3 samples its box ([1]) per
    // vertex in vs_tex_inst. width 0 clears it. False when the textures cannot be made (the records' fallback lights).
    bool setStockLgvAtlas(int width, int depth, const uint32_t* tex0, const uint32_t* tex1, const float* boxes, size_t boxCount);
    bool hasStockLgvAtlas() const { return lgvSrv_[2] != nullptr; }
    // The stock reference's reflection probes (core stock_probes, research 95 "Indirect specular"): every probe's
    // octahedral BC6H image as one array slice (all levels) and the influence boxes, drawn highest priority first.
    // null clears them.
    void setStockProbes(const iw8r::StockProbeSet* set);
    size_t stockProbeCount() const { return stockProbeSlices_; }
    bool hasEnvBrdfLut() const { return envBrdfSrv_ != nullptr; }
    // The game's tone map LUT (core game_look buildGameTonemapLut): size^3 RGBA8, red fastest.
    void setGameTonemapLut(int size, const uint8_t* rgba);
    // Auto exposure metering (stock maps' visions meter the frame, research 95): after each game-look frame a small
    // pass turns the shown pixels back into scene luminance (inverseGrey: 256 LUT inputs for display greys 0..1, from
    // core gameTonemapInverseGrey, divided by lutScale) and averages their logarithm, centre-weighted. The result is read
    // back a few frames later without waiting for the GPU. null turns it off.
    void setGameMetering(const float* inverseGrey256);
    // The latest metered log-average luminance (false until one is available). Each value is returned once.
    bool takeMeteredLuminance(double& avg);
    void setLightGain(float g) { lightGain_ = g; }
    // Lit preview: vertex colours carry light / 2 and the sun's share in alpha (scene.cpp); decode 2 turns that on, 1 is
    // the plain unlit modes. exposure scales the result before a soft shoulder.
    void setLitDecode(float decode, float exposure) { litDecode_ = decode, exposure_ = exposure; }
    // Sun shadow map for the lit preview (2048 x 2048 depth from the sun, re-rendered when the geometry or the matrix
    // changes). sunShadowMatrix builds the matrix.
    void setSunShadow(bool enabled, const float sunViewProj[16], float bias);
    // The near cascade (a box around the camera at the same 2048 texels): sharp shadows of small props and poles.
    void setSunShadowNear(bool enabled, const float sunViewProj[16], float bias);
    // Wind preview (5.23, core foliage_sway): direction (unit, xy), amplitude in units at full height, wave phase
    // 2 pi gust t. Amplitude 0 = off.
    void setWind(float dirX, float dirY, float amplitude, float phase) { wind_[0] = dirX * amplitude, wind_[1] = dirY * amplitude, wind_[2] = phase; }
    size_t pixelLightCount() const { return pixelLights_.size(); }
    size_t textureCount() const { return textures_.size(); }
    ID3D11ShaderResourceView* srv() const { return srv_; }
    ID3D11Texture2D* colorTexture() const { return color_; }
    // Reads the last frame back and counts pixels that differ from `clear` (smoke tests).
    size_t countNonClearPixels(const float clear[4]);
    // Copies the last frame to tightly packed RGBA8 rows (screenshots, tests).
    bool readback(std::vector<uint8_t>& rgba, int& w, int& h);
    int width() const { return w_; }
    int height() const { return h_; }
    // Counters of the last draw() (benchmark, tests). The sun shadow pass is counted apart from the camera pass.
    struct FrameStats {
        size_t drawCalls = 0, triangles = 0, lines = 0, batches = 0;
        size_t shadowDrawCalls = 0, shadowPasses = 0;
        size_t nearShadowPasses = 0, lampShadowMaps = 0;  // the near sun cascade and local light maps drawn this frame
        size_t uploadBytes = 0;  // vertex / index bytes sent to the GPU this frame
        size_t referenceInstanceVertices = 0;  // instanced reference props drawn on screen (vertices x instances)
    };
    const FrameStats& lastStats() const { return stats_; }
    // Blocks until the GPU has finished everything submitted so far (benchmark).
    void finishGpu();

private:
    FrameStats stats_;
    bool inShadowPass_ = false;
    void drawN(unsigned count, unsigned first, bool lines = false);
    void drawIdx(unsigned count, unsigned first);
    // The GPU side of a kept stream: its vertex buffer and, for big textured batches, the spatial chunks (an index
    // buffer in chunk order over the vertices, which stay in item order).
    struct StreamGpu {
        ID3D11Buffer* vb = nullptr;
        size_t capBytes = 0;
        uint64_t lastSeen = 0;
        ID3D11Buffer* ib = nullptr;
        size_t ibCapBytes = 0;
        std::vector<iw8r::DrawChunk> chunks;
        std::vector<uint32_t> posOf;  // triangle -> its place in chunk order
        size_t loose = 0;             // triangles whose chunk box grew since the chunks were made
        uint64_t posHash = 0;         // hash of the positions the chunks were made from
        bool posHashValid = false;
        bool chunked = false, synced = false;
        double centroid[3] = {0, 0, 0};
    };
    struct BatchDraw {
        SceneBatch* b = nullptr;
        StreamGpu* g = nullptr;
        float depth = 0;
    };
    static constexpr size_t kChunkTriangles = 2048, kChunkMinTriangles = 4096;
    std::unordered_map<uint32_t, StreamGpu> gpu_;
    std::vector<BatchDraw> drawList_;
    std::vector<uint32_t> drawOrder_, chunkOrder_, indices_;
    std::vector<iw8r::Vec3> corners_;
    uint64_t frame_ = 0, shadowCasterVersion_ = ~0ull;
    size_t streamsSeen_ = 0;  // streams synced this frame (the others' buffers are released)
    StreamGpu& syncVertices(GeoStreamBase& s, const void* data, size_t count, size_t stride);
    StreamGpu* syncBatch(SceneBatch& b);
    void buildChunks(const SceneBatch& b, StreamGpu& g);
    void releaseStreams();
    void drawInst(unsigned vertices, unsigned instances, unsigned firstVertex, unsigned firstInstance);
    void drawInstIdx(unsigned indices, unsigned instances, unsigned firstInstance = 0);
    void releaseTargets();

    ID3D11Device* dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    ID3D11Texture2D* color_ = nullptr;
    ID3D11Texture2D* depth_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11Buffer* cbLights_ = nullptr;
    std::vector<PixelLight> pixelLights_;
    float lightGain_ = 0.6f;
    float wind_[4] = {0, 0, 0, 0};  // w = 1 while the tome cull is on (the vertex shader reads the surface bits)
    std::vector<uint32_t> tomeSurfBits_, tomeModelBits_;
    bool tomeDirty_ = false;
    ID3D11Buffer* tomeBuf_ = nullptr;
    ID3D11ShaderResourceView* tomeSrv_ = nullptr;
    size_t tomeBufWords_ = 0;
    size_t tomeCulledProps_ = 0;
    std::vector<uint32_t> tomeLightBits_, tomeProbeBits_, tomeDecalBits_;
    size_t tomeCulledLights_ = 0, tomeCulledProbes_ = 0, tomeCulledDecals_ = 0;
    std::vector<float> probeCb_;         // setStockProbes: the probe constants as built (no box hidden)
    std::vector<int> probeBoxInstance_;  // per packed box: its probe instance
    int probeFallbackBox_ = -1;          // the packed box of the map-wide fallback probe, -1 none
    void applyTomeProbes();
    ID3D11Buffer* vbRefTex_ = nullptr;
    std::vector<unsigned> refTexFirst_, refTexCount_;
    std::vector<int> refTexIds_;
    std::vector<std::vector<iw8r::DrawChunk>> refTexChunks_;  // per textured reference batch (triangles in the batch)
    std::vector<iw8r::DrawChunk> refChunks_;                   // flat reference
    bool refCulling_ = true;
    struct GpuInstanced {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;
        bool editorMarker = false;
    };
    std::vector<GpuInstanced> refInst_;
    size_t refInstTotal_ = 0;
    struct GpuTexInstanced {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;  // PropMeshData::indices (16-bit when the vertices allow)
        int ibFormat = 42;  // DXGI_FORMAT_R32_UINT (render.h does not include the DXGI headers)
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;  // vertices: the index count (vertices drawn per instance)
        // The placements sorted into 2,048-unit cells: a shadow map draws only the cells in its view (the lamp maps and
        // the near sun cascade drew every placement of the map: Verdansk's 1.2 million, 263 ms a frame, 2026-11-13).
        // `order`: GPU placement i is the caller's placement order[i] (rows and light are kept in GPU order).
        struct InstCell {
            iw8r::AABB box;
            uint32_t first = 0, count = 0;
        };
        std::vector<InstCell> cells;
        std::vector<uint32_t> order;
        int texture = -1;
        // Screen culling (cullTexInstances): the placements on the CPU, the model's bounding sphere in model space, and
        // a dynamic buffer of the placements that pass (drawn instead of `inst` while culling is on).
        std::vector<float> rows;
        std::vector<float> light;  // InstancedTexMesh::light (12 per instance)
        std::vector<uint32_t> ids; // InstancedTexMesh::ids in GPU order (empty: none)
        float centre[3] = {0, 0, 0}, radius = 0;
        float lodOut = 0;  // InstancedTexMesh::lodOut
        std::vector<float> cullScale;  // InstancedTexMesh::cullScale in GPU order (empty: 1 for all)
        float boxMid[3] = {0, 0, 0}, boxHalf[3] = {0, 0, 0};  // the mesh's box in model space
        int lodGroup = -1;          // InstancedTexMesh::lodGroup / lodNear / lodFar
        float lodNear = 0, lodFar = 0;
        int normal = -1;            // setReferenceInstNormal
        int decal = 0;              // setReferenceInstDecal: the material's decal sort key (0 = not a decal)
        bool translucent = false;   // setReferenceInstTranslucent
        int drawClass = 0;          // setReferenceInstDrawClass: 1 shadow only, 2 never drawn
        bool noCast = false;        // setReferenceInstNoCast: drawn, casting no shadow (tree canopy leaf cards)
        float params[16] = {};      // setReferenceInstParams
        float extra[16] = {};       // setReferenceInstExtra (RefLayer::extra)
        float wave[16] = {};        // setReferenceInstWave: constants 79-82 (research 133 section 6)
        // Research 136: the first two colour layers folded into the mesh's own pass (rows 16-23 of its record) and their
        // images (colour 1, alpha / reveal 1, colour 2, alpha / reveal 2; -1 none) and the slot-126 mask.
        float fold[40] = {};  // rows 16-23 colour, rows 24-25 normal (one per folded layer)
        int foldTex[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // colour, alpha / reveal (1, 2), normal 1, 2, tint mask 1, 2
        int foldMask = -1;
        // Research 137: the clear coat (rows 26-29: (on, flags, mask channel + 1, map), UV transform, (offset, second
        // UV, map), (normal strength, gloss scale, gloss bias, 0)) and its map / alpha images.
        float coat[16] = {};
        int coatTex[2] = {-1, -1};
        // Static-model displacement (row 30: on, scale, bias, 0) and its height image (VS t43).
        float disp[4] = {};
        int dispTex = -1;
        std::vector<PropLayer> layers;  // setReferenceInstLayers: layers 1.. of a model-layered material
        int emissive = -1, transmission = -1;
        ID3D11Buffer* visInst = nullptr;
        unsigned visible = 0;
        // How many of visInst's placements, at its start, are large on screen (the depth pre-pass draws only those:
        // Aniyah Tac 23.5 -> 16 ms).
        unsigned big = 0;
    };
    // Each frame on screen: props outside the view, or smaller than about a pixel there, are left out (Hideout drew 21 M
    // prop triangles a frame at full detail). The sun shadow pass keeps them all.
    void cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle);
    std::vector<std::vector<size_t>> texLodGroups_;  // refTexInst_ indices per LOD group (setReferenceTexInstances)
    bool instCull_ = true;
    float pixelAngle_ = 0.0015f;  // setPixelAngle

public:
    // The angle one pixel spans at the frame's centre (instance culling); the camera sets it from its FOV.
    void setPixelAngle(float a) { pixelAngle_ = a > 0 ? a : 0.0015f; }
    size_t instancesCulled() const { return instCulledLast_; }

private:
    size_t instCulledLast_ = 0;
    std::vector<GpuTexInstanced> refTexInst_;
    ID3D11VertexShader* vsTexInst_ = nullptr;
    ID3D11InputLayout* layoutTexInst_ = nullptr;
    void drawReferenceTexInstances();
    ID3D11VertexShader* vsInst_ = nullptr;
    ID3D11InputLayout* layoutInst_ = nullptr;
    void drawReferenceInstances(ReferencePass pass);
    size_t textureBytes_ = 0;
    size_t texCulledLast_ = 0;
    size_t refChunkTotal_ = 0, refCulledLast_ = 0;
    int whiteTex_ = -1;
    bool createShadowResources();
    unsigned shadowSize_ = 2048;  // the side of both sun shadow maps (setSunShadowMapSize)
    float litDecode_ = 1, exposure_ = 1, shadowBias_ = 0.0005f;
    float sunViewProj_[16] = {};
    bool shadowEnabled_ = false, shadowDirty_ = true, shadowFailed_ = false;
    ID3D11Texture2D* shadowTex_ = nullptr;
    ID3D11DepthStencilView* shadowDsv_ = nullptr;
    ID3D11DepthStencilView* shadowDsvNear_ = nullptr;
    float sunViewProjNear_[16] = {};
    float shadowBiasNear_ = 0.0005f;
    bool nearEnabled_ = false, nearDirty_ = true;
    void drawReferenceTexInstancesShadow(const iw8r::Frustum* view = nullptr);  // view: only the cells inside it
    ID3D11ShaderResourceView* shadowSrv_ = nullptr;
    ID3D11SamplerState* cmpSampler_ = nullptr;
    ID3D11RasterizerState* rsShadow_ = nullptr;
    ID3D11Buffer* cbWind_ = nullptr;
    ID3D11Buffer* cbBatch_ = nullptr;  // per textured batch: x = has a normal map
    // b1 (cbuffer Batch): batchFlags + batchFlags2 (flags, 8 floats) and batchMat (mat, 4 floats; null = zero).
    void writeBatchCb(const float flags[8], const float* mat = nullptr);
    std::vector<TextureMaterial> texMat_;  // setTextureMaterial
    std::vector<uint8_t> texMatSet_;
    ID3D11Buffer* cbLayer_ = nullptr;
    // Every layer / decal batch's constants in one buffer, 256 bytes per batch, bound per draw with a D3D 11.1 offset
    // (Hideout: 4,300 draws a frame each copying its constants cost 40 ms). Rebuilt when a batch's constants change.
    ID3D11DeviceContext1* ctx1_ = nullptr;
    std::vector<ID3D11Buffer*> cbLayerAll_;  // 64 KB each: 256 batches (a 1.3 MB buffer lost the device on Hideout)
    std::vector<ID3D11Buffer*> cbInstAll_;   // the same for the textured prop meshes' material constants
    size_t cbInstAllMeshes_ = 0;
    bool instParamsDirty_ = true;
    size_t cbLayerAllBatches_ = 0;
    bool layerParamsDirty_ = true;
    ID3D11Texture3D* probeGridTex_ = nullptr;  // setStockProbes: candidate boxes per cell
    ID3D11ShaderResourceView* probeGridSrv_ = nullptr;  // b7: a stock layer batch's blend (zero = not a layer)
    struct RefLayer {
        int layer = 0;  // 0 = a base batch
        bool ready = false;
        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer
        int tomeCollection = -1;  // a decal volume batch's collection (setReferenceDecalCollection), -1 none
        // Research 66 (materialDrawFor): 1 = shadow only (tw/shadowcaster: its "Shadow" image showed on mp_rust's
        // walls and floors), 2 = never drawn.
        int drawClass = 0;
        bool noCast = false;  // surfaceFlags 0x40000: drawn, casting no shadow
        int sortKey = 0;
        int reveal = -1;
        float params[16] = {};
        // Research 107 (base batches): emissive scale (c36) + on, transmission tint (c18) + on, wrap (c103) + on; in
        // the same 256-byte slot of b7 after params. Their images (texture ids, -1 = none): t3 and t19.
        float extra[16] = {};
        float dual[8] = {};  // setReferenceBaseDualNormal: rows 12-13 (matDual0 / matDual1)
        int emissive = -1, transmission = -1;
    };
    std::vector<RefLayer> refLayers_;
    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch
    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch
    float terrainCut_[3] = {0, 0, 0};
    std::vector<std::array<float, 4>> tileBoxes_;
    bool tileBoxesDirty_ = true;
    ID3D11Buffer* cbTiles_ = nullptr;  // b9: count, then the boxes
    // Uploads every batch's 16 constants into the 64 KB chunks (when changed); true when per-batch offsets can be bound.
    bool uploadLayerParams();
    ID3D11Buffer* vbRefTexExtra_ = nullptr;  // unused (the extras are groups now: extraGroups_)
    struct ExtraGroupGpu {
        uint64_t key = 0, version = 0;
        ID3D11Buffer* vb = nullptr;
        std::vector<uint32_t> first, count;
        std::vector<iw8r::AABB> bounds;
        std::vector<int> layer;
        bool world = false, nearOnly = false;
    };
    std::vector<ExtraGroupGpu> extraGroups_;
    std::vector<ID3D11Buffer*> refTexVb_;     // per batch: its vertex buffer (null: vbRefTex_, the main reference)
    std::vector<uint8_t> refTexNearOnly_;     // per batch: drawn only within 4,000 units of the camera
    void releaseExtraGroups();
    ID3D11Buffer* refVbFor(size_t k) const { return k < refTexVb_.size() && refTexVb_[k] ? refTexVb_[k] : vbRefTex_; }
    // An extra batch whose bounds are outside the frustum (main batches use their chunks instead).
    bool refExtraOutside(size_t k, const iw8r::Frustum& f) const {
        if (refTexExtraFrom_ == ~size_t(0) || k < refTexExtraFrom_) return false;
        const size_t e = k - refTexExtraFrom_;
        if (e >= refTexExtraBounds_.size() || !refTexExtraBounds_[e].valid()) return false;
        const iw8r::AABB& bb = refTexExtraBounds_[e];
        const iw8r::Vec3 c = bb.center();
        const double r = iw8r::length(bb.maxs - bb.mins) * 0.5;
        for (const auto& pl : f.planes)
            if (pl.n.x * c.x + pl.n.y * c.y + pl.n.z * c.z - pl.d > r) return true;
        return false;
    }
    size_t refTexExtraFrom_ = ~size_t(0);    // the first batch in vbRefTexExtra_
    std::vector<iw8r::AABB> refTexExtraBounds_;  // per appended batch: drawn only in view and near (prop decals)
    bool refDecalDirty_ = false;
    ID3D11RasterizerState* rsDecal_ = nullptr;
    std::vector<int> refNrmIds_;         // per stock batch: its normal map texture, -1 = the flat one
    int flatNrmTex_ = -1;
    int whiteFullTex_ = -1;
    ID3D11Buffer* vbRef_ = nullptr;
    float fog_[8] = {};
    float grade_[12] = {1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0};  // contrast brightness desat _, light tint + lut size, dark tint
    int gradeFlags_ = 0;  // 1 film, 2 invert, 4 lut
    GameLook game_;
    bool metering_ = false;
    ID3D11Texture2D* meterTex_ = nullptr;
    ID3D11RenderTargetView* meterRtv_ = nullptr;
    ID3D11ShaderResourceView* meterSrv_ = nullptr;
    ID3D11Texture2D* meterStage_[3] = {nullptr, nullptr, nullptr};
    bool meterStageBusy_[3] = {false, false, false};
    unsigned meterNext_ = 0;
    ID3D11Texture1D* invGreyTex_ = nullptr;
    ID3D11ShaderResourceView* invGreySrv_ = nullptr;
    ID3D11VertexShader* meterVs_ = nullptr;
    ID3D11PixelShader* meterPs_ = nullptr;
    ID3D11Buffer* cbMeter_ = nullptr;
    bool meterReady_ = false;
    double meterValue_ = 0;
    bool meterHas_ = false;
    bool createMeterResources();
public:
    // View > Game Lighting: screen-space ambient occlusion on the indirect light (on by default).
    void setGameAo(bool on) { aoOn_ = on; }
    // The effect preview's particles of this frame (drawn after the scene's blended batches; empty = none).
    void setParticles(std::vector<ParticleBatch> batches) { particles_ = std::move(batches); }
    size_t particleQuads() const {
        size_t n = 0;
        for (const auto& b : particles_) n += b.v.size() / 6;
        return n;
    }
    // View > Game Lighting Options > Stock Map Detail: the game's GTAO (else the stand-in SSAO) and the shader parts
    // turned off (1 self-visibility, 2 detail layers, 4 clear coats, 8 model displacement, 16 thin push).
    void setGameAoGtao(bool on) { gtaoOn_ = on; }
    void setStockDetailOff(uint32_t bits) { stockDetailOff_ = bits; }
    uint32_t stockDetailOff() const { return stockDetailOff_; }
    // The side of the sun shadow maps in texels (the lit preview and Game Lighting): 1024, 2048 (the default) or 4096.
    // Other values go to the nearest of the three. The maps are made again at the next frame that draws shadows.
    void setSunShadowMapSize(unsigned size);
    unsigned sunShadowMapSize() const { return shadowSize_; }
    bool gameAo() const { return aoOn_; }
    bool gameAoDrawn() const { return aoValid_; }
    bool gameAoGtao() const { return aoGtaoDrawn_; }  // the last AO was GTAO (research 135), not the stand-in SSAO
    bool aoGtaoDrawn_ = false;
    bool gtaoOn_ = true;
    uint32_t stockDetailOff_ = 0;
    bool gameHdrDrawn() const { return hdrFrame_; }
    // View > Game Lighting: screen-space reflections on the stock materials that trace them (on by default).
    void setGameSsr(bool on) { ssrOn_ = on; }
    bool gameSsr() const { return ssrOn_; }
private:
    void meterFrame();
    ID3D11Texture2D* gameLmTex_[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* gameLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Texture2D* stockLmTex_[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Buffer* cbLmRects_ = nullptr;
    static constexpr int kMaxLightmapRects = 1024;
    ID3D11Texture2D* envBrdfTex_ = nullptr;
    ID3D11Texture3D* gtsoTex_ = nullptr;
    ID3D11Texture3D* lgvTex_[2] = {nullptr, nullptr};  // setStockLgvAtlas
    ID3D11Buffer* cbPropLayer_ = nullptr;  // the prop layer pass's b7 (256 bytes: PropLayer params, row 8 aux)
    ID3D11Buffer* lgvBoxBuf_ = nullptr;
    ID3D11ShaderResourceView* lgvSrv_[3] = {nullptr, nullptr, nullptr};  // VS t40 / t41 / t42
    ID3D11SamplerState* lgvSampler_ = nullptr;                          // VS s4
    ID3D11ShaderResourceView* gtsoSrv_ = nullptr;
    // Stock local lights (setStockLocalLights): all of the map's; per frame the drawn ones and the tile masks.
    void prepareStockLocalLights(const float* viewProj);
    std::vector<iw8r::StockGameLight> stockLights_;
    std::vector<iw8r::StockLightShadow> stockLightShadows_;
    bool stockLightsEnabled_ = true, lightShadowsEnabled_ = true;
    int gameDebugView_ = 0;
    float gameAoStrength_ = 1.0f;
    double gameTime_ = 0.0;
    size_t stockLightsDrawn_ = 0;
    // Local light shadow maps: kLightSlots slices of kLightSlotSize^2 depth (t24), one light each; their matrices and
    // lookup constants (t23). A slot is drawn once (static casters), again when the casters change.
    static constexpr int kLightSlots = 64;
    static constexpr unsigned kLightSlotSize = 512;
    struct LightSlot {
        int light = -1;      // stockLights_ index, -1 = free
        bool drawn = false;  // its depth is current
        uint64_t lastUsed = 0;
        float vp[16] = {};
        float params[4] = {};  // near, far, k, bias
    };
    std::array<LightSlot, kLightSlots> lightSlots_{};
    std::vector<int> lightSlotOf_;  // per light: its slot or -1
    size_t lightShadowsDrawn_ = 0;  // slots with a current depth (bench)
    bool lightSlotsChanged_ = true;
    ID3D11Texture2D* lightShadowTex_ = nullptr;
    ID3D11ShaderResourceView* lightShadowSrv_ = nullptr;
    std::array<ID3D11DepthStencilView*, kLightSlots> lightShadowDsv_{};
    ID3D11Buffer* lightSlotBuf_ = nullptr;
    ID3D11ShaderResourceView* lightSlotSrv_ = nullptr;
    bool createLightShadowResources();
    bool stockLightsOn_ = false;
    std::vector<std::pair<float, uint32_t>> stockLightOrder_;
    std::vector<uint32_t> stockTileMask_;
    ID3D11Buffer* stockLightBuf_ = nullptr;
    ID3D11ShaderResourceView* stockLightSrv_ = nullptr;
    ID3D11Buffer* stockTileBuf_ = nullptr;
    ID3D11ShaderResourceView* stockTileSrv_ = nullptr;
    size_t stockTileCap_ = 0;  // uints
    ID3D11Buffer* cbLocal_ = nullptr;
    ID3D11Texture1D* iesTex_ = nullptr;
    ID3D11ShaderResourceView* iesSrv_ = nullptr;
    // Game Lighting's depth pre-pass (the game shader is costly and its alpha clip stops early depth rejection): depth
    // first through the clip shader with colour writes off, then the shading pass tests depth without writing it.
    struct ID3D11BlendState* blendNoColor_ = nullptr;
    ID3D11PixelShader* filterPs_ = nullptr;  // setReferenceFilterShader
    bool filterScreen_ = false;              // ... and it has iw8rFilterScreen (reads the scene depth)
    ID3D11PixelShader* texPsOverride_ = nullptr;  // set during the pre-pass: drawReferenceTexInstances uses it
    ID3D11Texture2D* probeArrTex_ = nullptr;
    ID3D11ShaderResourceView* probeArrSrv_ = nullptr;
    ID3D11Buffer* cbProbes_ = nullptr;
    size_t stockProbeSlices_ = 0;
    static constexpr int kMaxProbeBoxes = 128;
    ID3D11ShaderResourceView* envBrdfSrv_ = nullptr;
    void writeGameCb();
    ID3D11Texture3D* gameLutTex_ = nullptr;
    ID3D11ShaderResourceView* gameLutSrv_ = nullptr;
    ID3D11Buffer* cbGame_ = nullptr;
    ID3D11ShaderResourceView* lutSrv_ = nullptr;
    ID3D11Texture3D* lutTex_ = nullptr;
    ID3D11SamplerState* lutSampler_ = nullptr;
    ID3D11ShaderResourceView* probeSrv_ = nullptr;
    ID3D11Texture2D* probeTex_ = nullptr;
    size_t refCount_ = 0;
    bool refVisible_ = true;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11RasterizerState* rsTris_ = nullptr;
    ID3D11RasterizerState* rsLines_ = nullptr;
    ID3D11RasterizerState* rsLinesFast_ = nullptr;  // the same without line antialiasing (very many lines)
    ID3D11DepthStencilState* dsWrite_ = nullptr;
    ID3D11DepthStencilState* dsTest_ = nullptr;
    ID3D11DepthStencilState* dsOff_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    ID3D11BlendState* blendAdd_ = nullptr;  // additive (ONE, ONE) for blend 2 batches
    // The particle pass (research 138).
    std::vector<ParticleBatch> particles_;
    ID3D11VertexShader* partVs_ = nullptr;
    ID3D11PixelShader* partPs_ = nullptr;
    ID3D11InputLayout* partLayout_ = nullptr;
    ID3D11Buffer* partVb_ = nullptr;
    size_t partVbBytes_ = 0;
    ID3D11Buffer* partCb_ = nullptr;
    ID3D11BlendState* blendPremul_ = nullptr;  // ONE, INV_SRC_ALPHA
    bool partTried_ = false;
    bool createParticleResources();
    void drawParticles(const float viewProj[16]);
    ID3D11BlendState* blendMul_ = nullptr;  // dst x src colour: stock layers in colour mode multiply
    ID3D11VertexShader* vsTex_ = nullptr;
    ID3D11PixelShader* psTex_ = nullptr;
    std::vector<ID3D11PixelShader*> customPs_;  // createCustomShader
    ID3D11PixelShader* psShadowClip_ = nullptr;  // sun shadow pass: alpha-test clip of textured batches
    ID3D11PixelShader* psPrepass_ = nullptr;     // depth pre-pass: the shading pass's cut-outs and terrain cut
    // Game Lighting ambient occlusion (screen space, from the pre-pass depth; research 95 "GTAO"): the depth's shader
    // view, the raw and blurred AO targets, the full-screen shaders and their constants (b8).
    ID3D11ShaderResourceView* depthSrv_ = nullptr;
    // Game Lighting's HDR scene (docs/PLAN_REMAINING.md 1.2): the lit scene in linear light (alpha marks drawn pixels),
    // then one post pass (exposure, log shaper, LUT) into the frame; editor overlays are drawn after it.
    ID3D11Texture2D* hdrTex_ = nullptr;
    ID3D11RenderTargetView* hdrRtv_ = nullptr;
    ID3D11ShaderResourceView* hdrSrv_ = nullptr;
    ID3D11VertexShader* postVs_ = nullptr;
    ID3D11PixelShader* postPs_ = nullptr;
    // The bloom veil chain (research 109 section 1.2): scene mips 1..n+1 and the upsampled levels 2..n, R16G16B16A16F.
    struct VeilLevel {
        ID3D11Texture2D* tex = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
    };
    std::vector<VeilLevel> veilS_, veilV_;  // index = mip level
    int veilCount_ = 0, veilW_ = 0, veilH_ = 0;
    ID3D11PixelShader* veilDownPs_ = nullptr;
    ID3D11PixelShader* veilKarisPs_ = nullptr;
    ID3D11PixelShader* veilUpPs_ = nullptr;
    ID3D11Buffer* cbVeil_ = nullptr;
    ID3D11Buffer* cbVol_ = nullptr;  // b13: the volumetric fog (drawPost)
    ID3D11ShaderResourceView* volMaskSrv_ = nullptr;  // t24: the volumetric boxes' mask pictures
    uint64_t volMaskVersion_ = 0;
    bool volumetricOn_ = false;      // View > Game Lighting Options > Volumetric Fog (game/volumetricFog, off by default)
    float lastViewProj_[16] = {};    // the frame's view (drawPost's volumetric march)
    bool postFxEnabled_ = true;
    bool filmGrainEnabled_ = true;
    bool whiteBalanceEnabled_ = true, whiteBalanceDrawn_ = false;
    bool veilDrawn_ = false;
    bool createVeilResources();
    void releaseVeilResources();
    // Builds the veil (V2) from the HDR scene; false when the map has no veil or it is off.
    bool drawVeil();
    ID3D11PixelShader* meterHdrPs_ = nullptr;
    ID3D11RenderTargetView* sceneRtv_ = nullptr;  // where the lit scene goes this frame (hdrRtv_ or rtv_)
    bool hdrFrame_ = false;
    void drawPost();
    // Screen-space reflections (research 104 section 8): this frame's HDR scene (half size, 4 mips) and linear depth
    // (half size) kept for the next frame's trace in ps_tex, with the view they were drawn from (b10).
    ID3D11Texture2D* prevColorTex_ = nullptr;
    ID3D11RenderTargetView* prevColorRtv_ = nullptr;
    ID3D11ShaderResourceView* prevColorSrv_ = nullptr;
    ID3D11Texture2D* prevZTex_ = nullptr;
    ID3D11RenderTargetView* prevZRtv_ = nullptr;
    ID3D11ShaderResourceView* prevZSrv_ = nullptr;
    ID3D11PixelShader* prevColorPs_ = nullptr;
    ID3D11PixelShader* prevZPs_ = nullptr;
    // The finished frame's depth at full size, for custom surface shaders in the next frame (iw8rSceneDepth, t31).
    ID3D11Texture2D* sceneZTex_ = nullptr;
    ID3D11RenderTargetView* sceneZRtv_ = nullptr;
    ID3D11ShaderResourceView* sceneZSrv_ = nullptr;
    ID3D11PixelShader* sceneZPs_ = nullptr;
    bool sceneZValid_ = false;     // it holds a frame
    bool sceneZStale_ = false;     // ... drawn from another view than the frame just drawn
    float sceneZViewProj_[16] = {};
    void keepSceneDepth(const float* viewProj);
    ID3D11Buffer* cbPrev_ = nullptr;
    float prevViewProj_[16] = {};
    bool prevValid_ = false;   // the copies hold a usable previous frame
    bool ssrOn_ = true;
    void keepFrameForSsr(const float* viewProj);
    ID3D11Texture2D* aoTex_[2] = {nullptr, nullptr};
    ID3D11RenderTargetView* aoRtv_[2] = {nullptr, nullptr};
    ID3D11ShaderResourceView* aoSrv_[2] = {nullptr, nullptr};
    static constexpr int kAoZMips = 5;  // GTAO depth pyramid: mips 0-4 (research 135)
    ID3D11Texture2D* aoZTex_ = nullptr;
    ID3D11RenderTargetView* aoZRtv_[kAoZMips] = {};
    ID3D11ShaderResourceView* aoZMipSrv_[kAoZMips] = {};
    ID3D11ShaderResourceView* aoZSrv_ = nullptr;
    ID3D11PixelShader* aoZPs_ = nullptr;
    ID3D11PixelShader* aoZDownPs_ = nullptr;
    ID3D11PixelShader* aoSsaoPs_ = nullptr;  // the stand-in SSAO (IW8R_AO_SSAO)
    ID3D11SamplerState* aoPointSampler_ = nullptr;
    ID3D11VertexShader* aoVs_ = nullptr;
    ID3D11PixelShader* aoPs_ = nullptr;
    ID3D11PixelShader* aoBlurPs_ = nullptr;
    ID3D11Buffer* cbAo_ = nullptr;
    bool aoTried_ = false;
    bool aoOn_ = true;
    bool aoValid_ = false;  // this frame's AO is in aoSrv_[1]
    bool createAoResources();
    // Uploads the textured prop meshes' constants (setReferenceInstParams; raw-alpha textures marked) when changed;
    // true when per-mesh offsets can be bound (cbInstAll_).
    bool uploadInstParams();
    void drawAo(const float* viewProj);
    ID3D11InputLayout* layoutTex_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    std::vector<ID3D11ShaderResourceView*> textures_;
    std::vector<uint8_t> texRawAlpha_;  // per texture: 1 = alpha is the specular code (createTextureBlocks)
    int w_ = 0, h_ = 0;
};

}  // namespace iw8r::app
