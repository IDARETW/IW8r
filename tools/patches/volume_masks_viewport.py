import io

# Volumetric box masks in the viewport (research 127 part A): the march multiplies a box's fog by its mask pictures,
# sampled by the game's rule; scrolling masks move while Game Lighting is on.
#   renderer: a picture array (t24, sampler s3), per box one row of mask codes and four rows of terms (cbuffer Volume)
#   camera view: the masks of each iw8r_volume, the pictures loaded from the map's and the editor's volume_masks

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


R = 'src/viewport/render.cpp'
if 'volBoxMask' not in io.open(ROOT + R, encoding='utf-8').read():
    patch(R, '''                                 float4 volLamp[24]; };  // per lamp 3 rows, as PixelLight: position + radius, colour + cos
                                                             // outer (below -1.5: omni), axis + cos inner''',
          '''                                 float4 volLamp[24];     // per lamp 3 rows, as PixelLight: position + radius, colour + cos
                                                             // outer (below -1.5: omni), axis + cos inner
                                     float4 volBoxMask[40]; };  // per box 5 rows: a code per mask (picture x 4 + axis, below 0:
                                                                // none), then per mask 1 / sizeU, 1 / sizeV, the two offsets
    // The mask pictures of the volumetric boxes (research 127 section A.2): one grey picture per slice, repeated.
    Texture2DArray<float> volMask : register(t24);
    SamplerState volMaskSamp : register(s3);''')
    patch(R, '''        sum += volBox[b * 5].w * saturate(1.0 - length(q)) * exp(volBox[b * 5 + 4].y * (local.z + halfSize.z));''',
          '''        // Each mask multiplies: the picture lies on the two box axes other than the mask's own, v turned over.
            float m = 1.0;
            const float4 codes = volBoxMask[b * 5];
            [loop] for (int k = 0; k < 4; ++k) {
                const float code = codes[k];
                if (code < 0.0) continue;
                const int axis = int(code) & 3;
                const float2 uv = axis == 0 ? local.yz : (axis == 1 ? local.xz : local.xy);
                const float4 t = volBoxMask[b * 5 + 1 + k];
                m *= volMask.SampleLevel(volMaskSamp, float3(uv.x * t.x + t.z, -uv.y * t.y + t.w, floor(code * 0.25)), 0);
            }
            sum += volBox[b * 5].w * saturate(1.0 - length(q)) * exp(volBox[b * 5 + 4].y * (local.z + halfSize.z)) * m;''')
    patch(R, '''        float cb[308] = {};
            const bool volOn =''', '''        float cb[468] = {};
            const bool volOn =''')
    patch(R, '''            std::memcpy(cb + 212, game_.volLamps, size_t(lamps) * 12 * sizeof(float));
            }''', '''            std::memcpy(cb + 212, game_.volLamps, size_t(lamps) * 12 * sizeof(float));
                // Masks: without a picture array every code is "none".
                for (int b = 0; b < boxes; ++b)
                    for (int k = 0; k < 20; ++k) cb[308 + b * 20 + k] = volMaskSrv_ || k >= 4 ? game_.volBoxMasks[b][k] : -1.0f;
            }''')
    patch(R, '''        ID3D11ShaderResourceView* d = volOn ? depthSrv_ : nullptr;
            ctx_->PSSetShaderResources(28, 1, &d);''', '''        ID3D11ShaderResourceView* d = volOn ? depthSrv_ : nullptr;
            ctx_->PSSetShaderResources(28, 1, &d);
            ID3D11ShaderResourceView* masks = volOn ? volMaskSrv_ : nullptr;
            ctx_->PSSetShaderResources(24, 1, &masks);
            if (sampler_) ctx_->PSSetSamplers(3, 1, &sampler_);''')

s = io.open(ROOT + R, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
# The picture array's maker goes before the first member function that mentions cbVol_'s release, found by a stable
# anchor: the definition of setGameMetering.
anchor = 'void SceneRenderer::setGameMetering('
assert s.count(anchor) == 1, s.count(anchor)
fn = '''void SceneRenderer::setVolumeMasks(const std::vector<std::vector<uint8_t>>& pictures, uint64_t version) {
    if (version == volMaskVersion_) return;
    volMaskVersion_ = version;
    if (volMaskSrv_) volMaskSrv_->Release(), volMaskSrv_ = nullptr;
    if (!dev_ || pictures.empty()) return;
    const UINT n = UINT(std::min<size_t>(pictures.size(), size_t(kVolMaskSlices)));
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = UINT(kVolMaskSize);
    td.MipLevels = 1;
    td.ArraySize = n;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(n);
    for (UINT i = 0; i < n; ++i) {
        if (pictures[i].size() != size_t(kVolMaskSize) * kVolMaskSize) return;
        init[i].pSysMem = pictures[i].data();
        init[i].SysMemPitch = UINT(kVolMaskSize);
    }
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, init.data(), &tex)) || !tex) return;
    dev_->CreateShaderResourceView(tex, nullptr, &volMaskSrv_);
    tex->Release();
}

'''.replace('\n', nl)
s = s.replace(anchor, fn + anchor)
io.open(ROOT + R, 'w', encoding='utf-8', newline='').write(s)

H = 'src/viewport/render.h'
patch(H, '''        int volBoxCount = 0;''', '''        int volBoxCount = 0;
        // Per box: four mask codes (picture slice x 4 + axis; below 0: none), then per mask 1 / sizeU, 1 / sizeV and
        // the two offset terms (core volume_mask, volumeMaskTerms).
        float volBoxMasks[8][20] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1},
                                    {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};''')
patch(H, '''    void setVolumetricFogEnabled(bool on) { volumetricOn_ = on; }''', '''    void setVolumetricFogEnabled(bool on) { volumetricOn_ = on; }
    // The mask pictures of the volumetric boxes: each kVolMaskSize x kVolMaskSize grey bytes, at most kVolMaskSlices.
    // `version` names the set: the same number again does nothing.
    static constexpr int kVolMaskSize = 256, kVolMaskSlices = 4;
    void setVolumeMasks(const std::vector<std::vector<uint8_t>>& pictures, uint64_t version);''')
patch(H, '''    ID3D11Buffer* cbVol_ = nullptr;  // b13: the volumetric fog (drawPost)''', '''    ID3D11Buffer* cbVol_ = nullptr;  // b13: the volumetric fog (drawPost)
    ID3D11ShaderResourceView* volMaskSrv_ = nullptr;  // t24: the volumetric boxes' mask pictures
    uint64_t volMaskVersion_ = 0;''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    struct OwnVolumeBox {
        Vec3 center, axis[3];
        double half[3] = {0, 0, 0}, density = 0, invFalloff = 0, heightK = 0;
    };
    std::vector<OwnVolumeBox> ownVolumes_;''', '''    struct OwnVolumeBox {
        Vec3 center, axis[3];
        double half[3] = {0, 0, 0}, density = 0, invFalloff = 0, heightK = 0;
        std::vector<VolumeMask> masks;  // keys mask .. mask4 (core volume_mask)
    };
    std::vector<OwnVolumeBox> ownVolumes_;
    // The mask pictures in use, by name, in the order of the renderer's slices; whether any mask scrolls.
    std::vector<std::string> ownMaskImages_;
    bool ownMaskScroll_ = false;
    QElapsedTimer maskClock_;''')
s = io.open(ROOT + CH, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
inc = '#include "render.h"'
assert s.count(inc) == 1, s.count(inc)
s = s.replace(inc, inc + nl + '#include "volume_mask.h"')
io.open(ROOT + CH, 'w', encoding='utf-8', newline='').write(s)

C = 'src/qtapp/camera_view.cpp'
patch(C, '''                box.heightK = -0.1 * fade;
                if (box.density > 0) ownVolumes_.push_back(box);
            }''', '''                box.heightK = -0.1 * fade;
                box.masks = volumeMasksOf(e);
                if (box.density > 0) ownVolumes_.push_back(box);
            }
            {
                // The boxes' mask pictures: the first four names in use, from the map's volume_masks folder or the
                // editor's, each scaled to the renderer's slice size (the red channel).
                std::vector<std::string> folders;
                if (!ctx_->doc.path().empty())
                    folders.push_back(QFileInfo(QString::fromStdString(ctx_->doc.path())).absoluteDir().filePath(kVolumeMaskFolder).toStdString());
                QDir data(QCoreApplication::applicationDirPath());
                for (int up = 0; up < 3 && !QFileInfo::exists(data.filePath("data/volume_masks")); ++up) data.cdUp();
                folders.push_back(data.filePath("data/volume_masks").toStdString());
                ownMaskImages_.clear();
                ownMaskScroll_ = false;
                std::vector<std::vector<uint8_t>> pictures;
                uint64_t version = 1469598103934665603ull;
                for (const OwnVolumeBox& box : ownVolumes_)
                    for (const VolumeMask& m : box.masks) {
                        if (std::find(ownMaskImages_.begin(), ownMaskImages_.end(), m.image) != ownMaskImages_.end()) {
                            ownMaskScroll_ = ownMaskScroll_ || m.scroll[0] != 0 || m.scroll[1] != 0;
                            continue;
                        }
                        if (int(ownMaskImages_.size()) >= app::SceneRenderer::kVolMaskSlices) continue;
                        const std::string file = findVolumeMaskFile(m.image, folders);
                        QImage img = file.empty() ? QImage() : QImage(QString::fromStdString(file));
                        if (img.isNull()) continue;
                        const int n = app::SceneRenderer::kVolMaskSize;
                        img = img.convertToFormat(QImage::Format_RGBA8888).scaled(n, n, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        std::vector<uint8_t> grey(size_t(n) * n);
                        for (int y = 0; y < n; ++y) {
                            const uchar* row = img.constScanLine(y);
                            for (int x = 0; x < n; ++x) grey[size_t(y) * n + x] = row[x * 4];
                        }
                        for (const char ch : m.image) version = (version ^ uint8_t(ch)) * 1099511628211ull;
                        version = (version ^ uint64_t(QFileInfo(QString::fromStdString(file)).lastModified().toMSecsSinceEpoch())) * 1099511628211ull;
                        ownMaskImages_.push_back(m.image);
                        ownMaskScroll_ = ownMaskScroll_ || m.scroll[0] != 0 || m.scroll[1] != 0;
                        pictures.push_back(std::move(grey));
                    }
                renderer_.setVolumeMasks(pictures, pictures.empty() ? 0 : version);
            }''')
patch(C, '''                    r[16] = float(b.invFalloff), r[17] = float(b.heightK);
                }''', '''                    r[16] = float(b.invFalloff), r[17] = float(b.heightK);
                    // Masks: the code of each (its picture's slice and its axis) and where it reads the picture now.
                    float* mk = g.volBoxMasks[i];
                    for (int k = 0; k < 4; ++k) mk[k] = -1.0f;
                    if (!maskClock_.isValid()) maskClock_.start();
                    const double now = ctx_->scene.captureMode ? 0.0 : maskClock_.elapsed() / 1000.0;
                    for (size_t k = 0; k < b.masks.size() && k < 4; ++k) {
                        const auto found = std::find(ownMaskImages_.begin(), ownMaskImages_.end(), b.masks[k].image);
                        if (found == ownMaskImages_.end()) continue;  // no picture: the mask does nothing in the preview
                        mk[k] = float((found - ownMaskImages_.begin()) * 4 + b.masks[k].axis);
                        volumeMaskTerms(b.masks[k], now, mk + 4 + k * 4);
                    }
                }''')
patch(C, '''        const bool animate = ctx_->windPreview || ctx_->behaviourPreview || (ctx_->effectPreview && ctx_->texturedView);''',
      '''        // (Scrolling masks of volumetric boxes move while Game Lighting shows them.)
        const bool animate = ctx_->windPreview || ctx_->behaviourPreview || (ctx_->effectPreview && ctx_->texturedView) ||
                             (ownMaskScroll_ && game && ctx_->texturedView);''')
print('ok')
