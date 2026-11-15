#include "shader_cache.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace iw8r::app {

namespace {

uint64_t fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

struct Entry {
    bool ready = false;
    bool ok = false;
    std::vector<uint8_t> code;
    std::string error;
};

// One process-wide cache. Never destroyed while workers run: the destructor joins them first.
struct Cache {
    std::mutex m;
    std::condition_variable cv;
    std::unordered_map<uint64_t, std::shared_ptr<Entry>> entries;
    std::wstring dir;
    bool diskOff = false;
    std::atomic<int> memoryHits{0}, diskHits{0}, compiled{0};
    std::atomic<long long> compileUs{0};
    // Prewarm workers.
    std::vector<ShaderJob> jobs;
    size_t nextJob = 0;
    std::atomic<bool> quit{false};
    std::vector<std::thread> workers;

    Cache() {
        const char* e = std::getenv("IW8R_SHADER_CACHE");
        diskOff = e && std::strcmp(e, "0") == 0;
    }
    ~Cache() {
        quit.store(true);
        for (auto& t : workers)
            if (t.joinable()) t.join();
    }
};

Cache& cache() {
    static Cache c;
    return c;
}

// A DXBC container: "DXBC", 16 bytes of checksum, version, the total size at offset 24.
bool validDxbc(const std::vector<uint8_t>& b) {
    if (b.size() < 32 || std::memcmp(b.data(), "DXBC", 4) != 0) return false;
    uint32_t total = 0;
    std::memcpy(&total, b.data() + 24, 4);
    return total == b.size();
}

std::filesystem::path diskPath(const Cache& c, uint64_t key) {
    wchar_t name[32];
    std::swprintf(name, 32, L"%016llx.cso", static_cast<unsigned long long>(key));
    return std::filesystem::path(c.dir) / name;
}

bool readDisk(const Cache& c, uint64_t key, std::vector<uint8_t>& out) {
    if (c.dir.empty() || c.diskOff) return false;
    std::ifstream f(diskPath(c, key), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return validDxbc(out);
}

void writeDisk(const Cache& c, uint64_t key, const std::vector<uint8_t>& code) {
    if (c.dir.empty() || c.diskOff) return;
    std::error_code ec;
    std::filesystem::create_directories(c.dir, ec);
    const std::filesystem::path final = diskPath(c, key);
    std::filesystem::path tmp = final;
    tmp += L".tmp" + std::to_wstring(GetCurrentThreadId());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(reinterpret_cast<const char*>(code.data()), std::streamsize(code.size()));
        if (!f) return;
    }
    std::filesystem::rename(tmp, final, ec);  // another process may have written it first: either copy is the same
    if (ec) std::filesystem::remove(tmp, ec);
}

HRESULT blobOf(const void* data, size_t n, ID3DBlob** out) {
    if (!out) return S_OK;
    *out = nullptr;
    if (FAILED(D3DCreateBlob(n, out)) || !*out) return E_OUTOFMEMORY;
    if (n) std::memcpy((*out)->GetBufferPointer(), data, n);
    return S_OK;
}

// Fills `e` (not yet ready, owned by this thread): from the disk cache or the compiler.
void produce(Cache& c, uint64_t key, Entry& e, const char* src, size_t len, const char* name, const char* entry, const char* target,
             unsigned flags) {
    if (readDisk(c, key, e.code)) {
        e.ok = true;
        ++c.diskHits;
        return;
    }
    e.code.clear();
    const auto t0 = std::chrono::steady_clock::now();
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    const HRESULT hr = D3DCompile(src, len, name, nullptr, nullptr, entry, target, flags, 0, &code, &err);
    c.compileUs += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    ++c.compiled;
    if (SUCCEEDED(hr) && code) {
        const auto* p = static_cast<const uint8_t*>(code->GetBufferPointer());
        e.code.assign(p, p + code->GetBufferSize());
        e.ok = true;
        writeDisk(c, key, e.code);
    } else {
        e.ok = false;
        e.error = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "D3DCompile failed";
    }
    if (code) code->Release();
    if (err) err->Release();
}

uint64_t keyOf(const char* src, size_t len, const char* name, const char* entry, const char* target, unsigned flags) {
    uint64_t h = fnv(src, len);
    const auto mix = [&](const char* s) {
        const char sep = '\x1f';
        h = fnv(&sep, 1, h);
        if (s) h = fnv(s, std::strlen(s), h);
    };
    mix(name), mix(entry), mix(target);
    const unsigned salt[2] = {flags, D3D_COMPILER_VERSION * 1000u + 1u};  // + 1: this cache's format
    return fnv(salt, sizeof(salt), h);
}

}  // namespace

HRESULT compileShaderCached(const char* src, size_t len, const char* name, const char* entry, const char* target, unsigned flags,
                            ID3DBlob** code, ID3DBlob** errors) {
    if (code) *code = nullptr;
    if (errors) *errors = nullptr;
    Cache& c = cache();
    const uint64_t key = keyOf(src, len, name, entry, target, flags);
    std::shared_ptr<Entry> e;
    bool mine = false;
    {
        std::unique_lock lock(c.m);
        auto it = c.entries.find(key);
        if (it == c.entries.end()) {
            e = std::make_shared<Entry>();
            c.entries.emplace(key, e);
            mine = true;
        } else {
            e = it->second;
            c.cv.wait(lock, [&] { return e->ready; });  // another thread is compiling it
            ++c.memoryHits;
        }
    }
    if (mine) {
        produce(c, key, *e, src, len, name, entry, target, flags);
        {
            std::lock_guard lock(c.m);
            e->ready = true;
        }
        c.cv.notify_all();
    }
    if (!e->ok) {
        if (errors) blobOf(e->error.data(), e->error.size(), errors);
        return E_FAIL;
    }
    return blobOf(e->code.data(), e->code.size(), code);
}

void setShaderCacheDir(const std::wstring& dir) {
    Cache& c = cache();
    std::lock_guard lock(c.m);
    c.dir = dir;
}

ShaderCacheStats shaderCacheStats() {
    Cache& c = cache();
    ShaderCacheStats s;
    s.memoryHits = c.memoryHits.load();
    s.diskHits = c.diskHits.load();
    s.compiled = c.compiled.load();
    s.compileMs = double(c.compileUs.load()) / 1000.0;
    return s;
}

void runShaderPrewarm(const std::vector<ShaderJob>& jobs) {
    Cache& c = cache();
    std::lock_guard lock(c.m);
    if (!c.workers.empty() || jobs.empty()) return;  // once per process
    c.jobs = jobs;
    c.nextJob = 0;
    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    const size_t n = std::min<size_t>(jobs.size(), std::clamp(hw - 1, 2u, 8u));
    for (size_t i = 0; i < n; ++i)
        c.workers.emplace_back([&c] {
            SetThreadDescription(GetCurrentThread(), L"iw8r shader prewarm");
            for (;;) {
                ShaderJob j{};
                {
                    std::lock_guard l(c.m);
                    if (c.quit.load() || c.nextJob >= c.jobs.size()) return;
                    j = c.jobs[c.nextJob++];
                }
                ID3DBlob* code = nullptr;
                compileShaderCached(j.src, std::strlen(j.src), j.name, j.entry, j.target, 0, &code, nullptr);
                if (code) code->Release();
            }
        });
}

}  // namespace iw8r::app
