// Stock lighting: the game's own lighting of a map's surfaces, as a shader you can edit (owner 2026-11-13: "edit stock
// shaders"). It is the formula the build's surface shader and the viewport's Game Lighting use for every lit surface:
// the baked light plus the sun, shadowed, falling off with the angle (Lambert). Start a new look from a copy of it
// (Tools > Shader Editor > New > Stock lighting), then change what you like.
//
// Inputs: IW8RSurface (src/core/custom_shader.h): albedo, alpha, normal, view, position, uv, sunDir, sunColor,
// sunShadow, ambient, gloss. Return the lit colour (linear, scene light units). Declare no textures or buffers.

float3 iw8rSurface(IW8RSurface s)
{
    const float sunAngle = saturate(dot(s.normal, s.sunDir));
    const float3 light = s.ambient + s.sunColor * sunAngle * s.sunShadow;
    return s.albedo * light;
}
