// IW8 Radiant Suite - splicing a colour filter into a compiled pixel shader (plan M6: custom shaders on stock maps).
//
// A stock map's surfaces are lit by the game's own pixel shaders, which exist only as compiled DXBC (SM 5.0). A map
// shader's filter function (`float3 iw8rFilter(float3 colour)`, core custom_shader) is compiled on its own into a
// small pixel shader that reads the colour from input v0 and writes the result to o0; this module moves that shader's
// instructions into a stock shader, right before its end, so the stock shader's final colour passes through the
// filter. Nothing else of the stock shader changes: its inputs, resources and every instruction stay as shipped.
//
//   stock:   ... ; min o0.xyzw, r0.xyzw, l(32255...) ; ret
//   spliced: ... ; min rT.xyzw, r0.xyzw, l(32255...) ; mov rI, rT ; <filter: v0 -> rI, o0 -> o0, rk -> r(base + k)> ; ret
//
// Requirements, checked: both blobs are DXBC with a SHEX / SHDR chunk of a pixel shader; the stock shader ends in one
// `ret` and has no other `ret` / `retc` / discard after an output write; the filter shader declares only v0, o0 and
// temps (no resources, constant buffers, immediate constant buffers or indexable temps).
//
// A filter that reads the screen (DxbcSpliceOptions::sceneDepthRegister >= 0; a map shader's iw8rFilterScreen, the ink
// lines of a stock map): its program also has input 1, SV_Position, and the texture t0, read with Load only. Input 1
// becomes the stock shader's own position input (it must have one that carries every part the filter reads), t0
// becomes the stock shader's texture register `sceneDepthRegister`, where the technique binds the scene depth; when
// the stock shader does not declare that register the filter's declaration is added.
//
// The container's checksum is recomputed (dxbcChecksum: Direct3D refuses a blob whose checksum is wrong).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

// The checksum Direct3D stores at bytes 4..19 of a DXBC container, computed over bytes 20..end (a modified MD5).
std::array<uint8_t, 16> dxbcChecksum(const uint8_t* blob, size_t size);
// True when the blob is a DXBC container whose stored checksum is right.
bool dxbcChecksumValid(const std::vector<uint8_t>& blob);

struct DxbcInfo {
    bool pixelShader = false;
    unsigned major = 0, minor = 0;
    unsigned temps = 0;          // dcl_temps
    unsigned instructions = 0;   // non-declaration instructions
    unsigned outputWrites = 0;   // instructions whose destination is o0
    unsigned rets = 0;           // ret instructions
    bool endsInRet = false;
};
bool dxbcInspect(const std::vector<uint8_t>& blob, DxbcInfo& info, std::string* err = nullptr);

struct DxbcSpliceOptions {
    // >= 0: the filter reads the screen (see the top); the stock shader's texture register for the scene depth.
    int sceneDepthRegister = -1;
};

// The stock shader with the filter shader's instructions between its last output write and its final ret (see the
// top). False with *err when a requirement is not met; `out` is then empty.
bool dxbcSpliceFilter(const std::vector<uint8_t>& stock, const std::vector<uint8_t>& filter, std::vector<uint8_t>& out,
                      std::string* err = nullptr, const DxbcSpliceOptions& options = {});

}  // namespace iw8r
