// IW8 Radiant Suite - Direct3D 11 offscreen renderer for the 3D camera view.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "dirty_ranges.h"
#include "draw_chunks.h"
#include "reference_markers.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
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
struct GpuTexVertex {
    float x, y, z;
    float u, v;
    uint32_t rgba;
    // Surface normal and texture tangent (world), for normal-mapped shading (3.2); zero for surfaces without one.
    float nx = 0, ny = 0, nz = 0;
    float tx = 0, ty = 0, tz = 0;
    // View > Game Lighting: the vertex's lightmap UV in the preview bake (game_lightmap.h); negative = none.
    float lu = -1, lv = -1;
};

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
struct InstancedTexMesh {
    std::vector<GpuTexVertex> tris;
    std::vector<float> rows;  // 12 floats per instance
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
    int texture = -1, normalTexture = -1, emissiveTexture = -1;
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
    // A batch of a stock decal material (sortKey 10-17, techset blend `_bad_`; research 98): blended over the opaque
    // world (premultiplied by its coverage B, depth tested with a small offset, no depth write), in sortKey order.
    // Layer 0 (layer == 0) takes params / reveal as a layer would (its weight is in the tangent length too).
    void setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]);
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
    void setReferenceInstTexture(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].texture = textureId;
    }
    size_t referenceTexInstanceMeshes() const { return refTexInst_.size(); }
    // Instanced draws of the textured meshes (a model with three materials counts each placement three times).
    size_t referenceTexInstances() const {
        size_t n = 0;
        for (const auto& g : refTexInst_) n += g.instances;
        return n;
    }
    void setReferenceTexture(size_t batch, int textureId) {
        if (batch < refTexIds_.size()) refTexIds_[batch] = textureId;
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
        // The stock map's atmosphere (core GameFog): fog0 = start, half distance, max opacity, on; fog1 = inscattered
        // radiance (rgb, fog colour x scale), Mie blend; fog2 = Rayleigh albedo, Mie anisotropy; fog3 = Mie albedo,
        // desaturation; fog4 = height base, height half distance, sky distance, sky intensity; fog5.x = max distance;
        // skyScale: the sky texture (LDR) to scene light.
        float fog[6][4] = {};
        float skyScale = 1.0f;
        float lutScale = 1.0f;
    };
    void setGameLook(const GameLook& g) { game_ = g; }
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
    float wind_[4] = {0, 0, 0, 0};
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
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;
        int texture = -1;
    };
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
    static constexpr unsigned kShadowSize = 2048;
    float litDecode_ = 1, exposure_ = 1, shadowBias_ = 0.0005f;
    float sunViewProj_[16] = {};
    bool shadowEnabled_ = false, shadowDirty_ = true, shadowFailed_ = false;
    ID3D11Texture2D* shadowTex_ = nullptr;
    ID3D11DepthStencilView* shadowDsv_ = nullptr;
    ID3D11DepthStencilView* shadowDsvNear_ = nullptr;
    float sunViewProjNear_[16] = {};
    float shadowBiasNear_ = 0.0005f;
    bool nearEnabled_ = false, nearDirty_ = true;
    void drawReferenceTexInstancesShadow();
    ID3D11ShaderResourceView* shadowSrv_ = nullptr;
    ID3D11SamplerState* cmpSampler_ = nullptr;
    ID3D11RasterizerState* rsShadow_ = nullptr;
    ID3D11Buffer* cbWind_ = nullptr;
    ID3D11Buffer* cbBatch_ = nullptr;  // per textured batch: x = has a normal map
    ID3D11Buffer* cbLayer_ = nullptr;  // b7: a stock layer batch's blend (zero = not a layer)
    struct RefLayer {
        int layer = 0;  // 0 = a base batch
        bool ready = false;
        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer
        int sortKey = 0;
        int reveal = -1;
        float params[16] = {};
    };
    std::vector<RefLayer> refLayers_;
    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch
    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch
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
    void meterFrame();
    ID3D11Texture2D* gameLmTex_[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* gameLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Texture2D* stockLmTex_[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Buffer* cbLmRects_ = nullptr;
    static constexpr int kMaxLightmapRects = 1024;
    ID3D11Texture2D* envBrdfTex_ = nullptr;
    // Game Lighting's depth pre-pass (the game shader is costly and its alpha clip stops early depth rejection): depth
    // first through the clip shader with colour writes off, then the shading pass tests depth without writing it.
    struct ID3D11BlendState* blendNoColor_ = nullptr;
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
    ID3D11DepthStencilState* dsWrite_ = nullptr;
    ID3D11DepthStencilState* dsTest_ = nullptr;
    ID3D11DepthStencilState* dsOff_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    ID3D11BlendState* blendAdd_ = nullptr;  // additive (ONE, ONE) for blend 2 batches
    ID3D11BlendState* blendMul_ = nullptr;  // dst x src colour: stock layers in colour mode multiply
    ID3D11VertexShader* vsTex_ = nullptr;
    ID3D11PixelShader* psTex_ = nullptr;
    ID3D11PixelShader* psShadowClip_ = nullptr;  // sun shadow pass: alpha-test clip of textured batches
    ID3D11InputLayout* layoutTex_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    std::vector<ID3D11ShaderResourceView*> textures_;
    int w_ = 0, h_ = 0;
};

}  // namespace iw8r::app
