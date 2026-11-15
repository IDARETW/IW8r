// IW8 Radiant Suite - compiled shader cache (owner 2026-11-15: "the viewport rendering ... never frozen, and other
// workloads on others").
//
// The renderer's built-in shaders took about 10 s of D3DCompile on the GUI thread when the editor started, and again
// for every 3D preview window (each has its own renderer). compileShaderCached() is D3DCompile with two caches in
// front: in memory for the process (a second renderer gets its shaders at once) and on disk (a compiled shader of the
// same source, entry, target and flags is read back on the next start). A shader another thread is compiling is
// waited for, not compiled twice. SceneRenderer::prewarmShaders() compiles every built-in shader on worker threads, so
// they are ready, or nearly, when the camera view is made.
#pragma once

#include <d3dcommon.h>

#include <string>
#include <vector>

namespace iw8r::app {

// D3DCompile(src, len, name, nullptr, nullptr, entry, target, flags, 0, code, errors) through the caches. On failure
// *errors holds the compiler's message (when errors is given). Thread-safe. Warnings of a successful compile are not
// returned.
HRESULT compileShaderCached(const char* src, size_t len, const char* name, const char* entry, const char* target, unsigned flags,
                            ID3DBlob** code, ID3DBlob** errors);

// The folder of the disk cache ("" = memory only, the default). Set once at start-up, before any compile.
// IW8R_SHADER_CACHE=0 turns the disk cache off whatever is set here.
void setShaderCacheDir(const std::wstring& dir);

struct ShaderCacheStats {
    int memoryHits = 0;    // given from memory (also: waited for another thread's compile)
    int diskHits = 0;      // read back from the disk cache
    int compiled = 0;      // compiled by D3DCompile
    double compileMs = 0;  // the time spent compiling (all threads)
};
ShaderCacheStats shaderCacheStats();

// One shader for runShaderPrewarm (the strings must live as long as the process: the built-in sources and literals).
struct ShaderJob {
    const char* src;
    const char* name;
    const char* entry;
    const char* target;
};
// Compiles `jobs` through the cache on worker threads, in order (longest first is best); returns at once. The workers
// are joined when the process ends (a compile under way is finished first).
void runShaderPrewarm(const std::vector<ShaderJob>& jobs);

}  // namespace iw8r::app
