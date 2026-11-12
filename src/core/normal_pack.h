#pragma once
// IW8 (Replay) packed normal maps, the "<name>_n&<name>_g" images (roadmap 3.2 / 4.14).
//
// Layout, from the x64-zt converter (EncodeNativeWorldNormal / EncodeOctDirection) and measured on cached stock images:
//   R = gloss, B = occlusion, G and A = the tangent-space normal as an octahedral pair:
//     ox = slopeX / (1 + |slopeX| + |slopeY|),  oy = slopeY / (1 + |slopeX| + |slopeY|)
//     G = (1 + ox + oy) * 127.5,                A = (1 + ox - oy) * 127.5
//   where slope = normal.xy / normal.z.
//
// The converter's `normalMap` input is the IW3 slope layout instead: slopeX = 4.08 R/255 - 2.08, slopeY = 4.0645 G/255 -
// 2.0645, with gloss taken from the specular map's alpha. These helpers move a stock image between the two, so a stock
// material built by the editor keeps its bump and gloss.
#include <cstddef>
#include <cstdint>

namespace iw8r {

// Slopes (normal.xy / normal.z) of one packed texel.
void replayNormalSlopes(uint8_t g, uint8_t a, double& slopeX, double& slopeY);

// The packed pair for slopes (the converter's EncodeOctDirection).
void replayNormalPack(double slopeX, double slopeY, uint8_t& g, uint8_t& a);

// IW3 slope-map bytes (R, G) for slopes; slopes beyond the layout's range are clamped.
void slopeMapBytes(double slopeX, double slopeY, uint8_t& r, uint8_t& g);

// RGBA8 packed stock normal -> RGBA8 slope map (R, G = slopes, B = 255, A = 255). `texels` pixels each.
void replayNormalToSlopeMap(const uint8_t* packedRgba, size_t texels, uint8_t* slopeRgba);

// Specular map for the converter from the stock pair, A = gloss (packed R). The colour image's alpha measures 255 on
// stone, dirt, rubber and foliage and low on metal (dryer 41, antenna 160), so it is read as "how non-metal": RGB goes
// from the albedo (metal: coloured reflection) to 56 (a plain dielectric, 4% reflectance). In-game look NOT VERIFIED.
// Both inputs are RGBA8 with `texels` pixels; `colorRgba` may be null (specular 56).
void replaySpecularMap(const uint8_t* colorRgba, const uint8_t* packedRgba, size_t texels, uint8_t* specRgba);

}  // namespace iw8r
