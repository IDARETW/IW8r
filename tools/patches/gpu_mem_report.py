import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''    size_t textureBytes() const { return textureBytes_; }''', '''    size_t textureBytes() const { return textureBytes_; }
    // Video memory the renderer holds, by kind (MB): "props a+b world c extras d streams e textures f lightmaps g
    // probes h shadows i targets j". Windows charges video memory to the process's commit, so this is part of what
    // the bench's memory.log shows.
    std::string gpuMemoryReport() const;''')

R = 'src/viewport/render.cpp'
patch(R, '''bool SceneRenderer::setReferenceTexInstanceLight(size_t mesh, const std::vector<float>& light) {''', '''namespace {
size_t bufferBytes(ID3D11Buffer* b) {
    if (!b) return 0;
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    return d.ByteWidth;
}
double formatBytesPerTexel(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
        return f == DXGI_FORMAT_BC4_UNORM || f == DXGI_FORMAT_BC4_SNORM ? 0.5 : 0.5;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_TYPELESS:
        return 1;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_TYPELESS:
        return 2;
    default:
        return 4;
    }
}
size_t texture2dBytes(ID3D11Texture2D* t) {
    if (!t) return 0;
    D3D11_TEXTURE2D_DESC d{};
    t->GetDesc(&d);
    double bytes = 0;
    for (UINT m = 0; m < std::max(1u, d.MipLevels); ++m)
        bytes += double(std::max(1u, d.Width >> m)) * double(std::max(1u, d.Height >> m)) * formatBytesPerTexel(d.Format);
    return size_t(bytes * std::max(1u, d.ArraySize));
}
}  // namespace

std::string SceneRenderer::gpuMemoryReport() const {
    size_t props = 0, propInst = 0, world = 0, extras = 0, streams = 0, lightmaps = 0, probes = 0, shadows = 0, targets = 0;
    for (const auto& g : refTexInst_) props += bufferBytes(g.vb), propInst += bufferBytes(g.inst) + bufferBytes(g.visInst) + bufferBytes(g.bigInst);
    for (const auto& g : refInst_) props += bufferBytes(g.vb), propInst += bufferBytes(g.inst);
    world += bufferBytes(vbRefTex_) + bufferBytes(vbRef_);
    {
        std::vector<ID3D11Buffer*> seen;
        for (ID3D11Buffer* b : refTexVb_)
            if (b && std::find(seen.begin(), seen.end(), b) == seen.end()) seen.push_back(b), world += bufferBytes(b);
    }
    for (const auto& g : extraGroups_) extras += bufferBytes(g.vb);
    for (const auto& [id, s] : gpu_) streams += bufferBytes(s.vb) + bufferBytes(s.ib);
    for (ID3D11Texture2D* t : stockLmTex_) lightmaps += texture2dBytes(t);
    for (ID3D11Texture2D* t : gameLmTex_) lightmaps += texture2dBytes(t);
    probes += texture2dBytes(probeArrTex_) + texture2dBytes(probeTex_);
    shadows += texture2dBytes(shadowTex_) + texture2dBytes(lightShadowTex_);
    targets += texture2dBytes(color_) + texture2dBytes(depth_) + texture2dBytes(hdrTex_) + texture2dBytes(prevColorTex_) +
               texture2dBytes(prevZTex_) + texture2dBytes(aoTex_[0]) + texture2dBytes(aoTex_[1]);
    const auto mb = [](size_t b) { return std::to_string((b + (1u << 19)) >> 20); };
    return "props " + mb(props) + "+" + mb(propInst) + " world " + mb(world) + " extras " + mb(extras) + " streams " + mb(streams) +
           " textures " + mb(textureBytes_) + " lightmaps " + mb(lightmaps) + " probes " + mb(probes) + " shadows " + mb(shadows) +
           " targets " + mb(targets);
}

bool SceneRenderer::setReferenceTexInstanceLight(size_t mesh, const std::vector<float>& light) {''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }''',
      '''    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }
    std::string gpuMemoryReport() const { return renderer_.gpuMemoryReport(); }''')

# The bench logs it every 10 s next to the memory samples (main.cpp's status hook writes it into memory.log).
M = 'src/qtapp/main.cpp'
patch(M, '''        QObject::connect(&iw8r::qt::EditorLog::instance(), &iw8r::qt::EditorLog::added, &w, [t0, path](const iw8r::qt::LogEntry& e) {''',
      '''        auto* gpuTick = new QTimer(&w);
        gpuTick->setInterval(10000);
        QObject::connect(gpuTick, &QTimer::timeout, &w, [&w] {
            if (auto* cam = w.findChild<iw8r::qt::CameraView*>())
                iw8r::qt::EditorLog::instance().add(iw8r::qt::LogLevel::Info, "bench", "gpu memory MB: " + QString::fromStdString(cam->gpuMemoryReport()));
        });
        gpuTick->start();
        QObject::connect(&iw8r::qt::EditorLog::instance(), &iw8r::qt::EditorLog::added, &w, [t0, path](const iw8r::qt::LogEntry& e) {''')
print('ok')
