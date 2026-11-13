import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

patch(RH, '''struct Prof {
    static bool on;
    static void add(const char* name, long long ns);
    static std::string dump();
};''', '''struct Prof {
    static bool on;
    static void add(const char* name, long long ns);
    static std::string dump();
    // IW8R_BENCH_GPU=1 with on: the renderer drawing the frame also times every scope on the GPU (timestamp queries,
    // read back at the end of the frame, a stall the benchmark accepts); they are listed as "gpu.<name>".
    static void (*gpuMark)(const char* name, bool begin);
};''')
patch(R, '''bool Prof::on = false;
''', '''bool Prof::on = false;
void (*Prof::gpuMark)(const char*, bool) = nullptr;
''')
patch(R, '''ProfScope::ProfScope(const char* n) : name(n), t0(Prof::on ? profNow() : 0) {}
ProfScope::~ProfScope() {
    if (Prof::on) Prof::add(name, profNow() - t0);
}''', '''ProfScope::ProfScope(const char* n) : name(n), t0(Prof::on ? profNow() : 0) {
    if (Prof::on && Prof::gpuMark) Prof::gpuMark(n, true);
}
ProfScope::~ProfScope() {
    if (Prof::on) {
        Prof::add(name, profNow() - t0);
        if (Prof::gpuMark) Prof::gpuMark(name, false);
    }
}

// GPU scope timing of one frame (Prof::gpuMark): timestamp queries in pairs, inside one disjoint query.
namespace {
struct GpuProfState {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Query* disjoint = nullptr;
    std::vector<ID3D11Query*> pool;
    size_t used = 0;
    struct Rec {
        const char* name;
        size_t q0, q1;
    };
    std::vector<Rec> recs;
    std::vector<size_t> stack;
};
GpuProfState& gpuProf() {
    static GpuProfState s;
    return s;
}
size_t gpuQuery() {
    GpuProfState& g = gpuProf();
    if (g.used == g.pool.size()) {
        D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP, 0};
        ID3D11Query* q = nullptr;
        if (FAILED(g.dev->CreateQuery(&qd, &q))) return size_t(-1);
        g.pool.push_back(q);
    }
    g.ctx->End(g.pool[g.used]);
    return g.used++;
}
void gpuMarkImpl(const char* name, bool begin) {
    GpuProfState& g = gpuProf();
    if (begin) {
        g.recs.push_back({name, gpuQuery(), size_t(-1)});
        g.stack.push_back(g.recs.size() - 1);
    } else if (!g.stack.empty()) {
        g.recs[g.stack.back()].q1 = gpuQuery();
        g.stack.pop_back();
    }
}
const char* gpuName(const char* name) {
    static std::vector<std::unique_ptr<std::string>> names;
    for (const auto& n : names)
        if (n->compare(4, std::string::npos, name) == 0) return n->c_str();
    names.push_back(std::make_unique<std::string>(std::string("gpu.") + name));
    return names.back()->c_str();
}
}  // namespace

struct GpuProfFrame {
    bool active = false;
    GpuProfFrame(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        static const bool want = std::getenv("IW8R_BENCH_GPU") != nullptr;
        if (!want || !Prof::on || !dev || !ctx || Prof::gpuMark) return;
        GpuProfState& g = gpuProf();
        g.dev = dev, g.ctx = ctx;
        if (!g.disjoint) {
            D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            if (FAILED(dev->CreateQuery(&qd, &g.disjoint))) return;
        }
        g.used = 0, g.recs.clear(), g.stack.clear();
        ctx->Begin(g.disjoint);
        Prof::gpuMark = gpuMarkImpl;
        active = true;
    }
    ~GpuProfFrame() {
        if (!active) return;
        Prof::gpuMark = nullptr;
        GpuProfState& g = gpuProf();
        g.ctx->End(g.disjoint);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        while (g.ctx->GetData(g.disjoint, &dj, sizeof(dj), 0) == S_FALSE) {}
        if (dj.Disjoint || dj.Frequency == 0) return;
        std::vector<UINT64> t(g.used, 0);
        for (size_t i = 0; i < g.used; ++i)
            while (g.ctx->GetData(g.pool[i], &t[i], sizeof(UINT64), 0) == S_FALSE) {}
        for (const auto& r : g.recs)
            if (r.q0 < g.used && r.q1 < g.used && t[r.q1] >= t[r.q0])
                Prof::add(gpuName(r.name), (long long)(double(t[r.q1] - t[r.q0]) * 1e9 / double(dj.Frequency)));
    }
};''')
patch(R, '''void SceneRenderer::drawFrame(const FrameInput& in) {
    if (!rtv_ || !in.viewProj || !in.clear) return;
    IW8R_PROF("d.total");''', '''void SceneRenderer::drawFrame(const FrameInput& in) {
    if (!rtv_ || !in.viewProj || !in.clear) return;
    GpuProfFrame gpuFrame(dev_, ctx_);  // IW8R_BENCH_GPU: every scope below timed on the GPU too
    IW8R_PROF("d.total");''')
print('ok')
