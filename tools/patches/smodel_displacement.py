import io

# Static-model displacement (research 133 section 3, VS_36fc68f3:231-235): a model material with a displaced layer
# (slot 95 height image, constant 45) moves each vertex along its normal by t95(uv).r * c45.x + c45.y. Its stock users:
# tmo/stone_wall_01 (Hideout), mo/skl_sand_dune_01_blend, tmo/snow_slush_02 (Verdansk). Record row 30 = (on, scale,
# bias, 0) in the per-mesh record (PropWave, b8 in vs_tex_inst); the height image at VS t43 (a "\x07" data key: the
# file's sRGB format is kept). A/B: IW8R_NO_SMODEL_DISP.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return  # already applied
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


MC = 'src/qtapp/material_cache.cpp'
patch(MC, '''        if (finite && (index == 18 || index == 36 || index == 63 || (index >= 79 && index <= 82) || index == 103 || index == 104 ||
                       index == 110 || index == 111))''',
      '''        if (finite && (index == 18 || index == 36 || index == 45 || index == 63 || (index >= 79 && index <= 82) || index == 103 ||
                       index == 104 || index == 110 || index == 111))''')
patch(MC, '''            o.insert("constRules", 6);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111; 6: code-layer rows''',
      '''            o.insert("constRules", 7);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111; 6: code-layer rows; 7: + 45''')
patch(MC, '''        // Layered materials (tw4 / twc4) resolved before their layer images and constants were kept (research 97), or''',
      '''        // Resolved before constant 45 was kept (static-model displacement, research 133 section 3): model materials with
        // a slot-95 height image resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 7 && !e.techset.startsWith("tw")) {
            bool height = false;
            for (const QString& t : e.textures) height = height || t.startsWith("95=");
            if (height) continue;
        }
        // Layered materials (tw4 / twc4) resolved before their layer images and constants were kept (research 97), or''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''        std::string layerMaskImage;''', '''        std::string layerMaskImage;
        // Static-model displacement (research 133 section 3): the slot-95 height image and constant 45 (scale, bias);
        // "" / zeros: none.
        std::string dispImage;
        std::array<float, 2> disp{0.0f, 0.0f};''')

MW = 'src/qtapp/main_window.cpp'
patch(MW, '''        for (int k = 0; k < 4; ++k)  // the flag / foliage waves (research 133 section 6)
            if (const auto* c = cst(79 + k)) std::copy(c->begin(), c->end(), f.wave.begin() + 4 * k);''',
      '''        for (int k = 0; k < 4; ++k)  // the flag / foliage waves (research 133 section 6)
            if (const auto* c = cst(79 + k)) std::copy(c->begin(), c->end(), f.wave.begin() + 4 * k);
        // Static-model displacement (research 133 section 3): a model material's slot-95 height image with constant 45.
        if (!e->techset.startsWith("tw"))
            if (const auto* c = cst(45); c && ((*c)[0] != 0.0f || (*c)[1] != 0.0f))
                for (const QString& t : e->textures)
                    if (t.startsWith("95=")) f.dispImage = t.mid(3).toStdString(), f.disp = {(*c)[0], (*c)[1]};''')

CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''    static const bool noDual = std::getenv("IW8R_NO_DUAL_NORMAL") != nullptr;  // A/B: one normal layer as before''',
      '''    // Static-model displacement (research 133 section 3): the height image and constant 45, moved along the normal in
    // the props' vertex shader.
    static const bool noDisp = std::getenv("IW8R_NO_SMODEL_DISP") != nullptr;  // A/B: undisplaced, as before
    if (f && propMesh && !noDisp && !f->dispImage.empty()) {
        const std::string key = "\\x07" + f->dispImage;
        const int id = textureFor(key);
        if (!texIds_.count(key)) return false;  // still loading
        if (id >= 0 && id != checkerTex_) renderer_.setReferenceInstDisp(index, id, f->disp[0], f->disp[1]);
    }
    static const bool noDual = std::getenv("IW8R_NO_DUAL_NORMAL") != nullptr;  // A/B: one normal layer as before''')

H = 'src/viewport/render.h'
patch(H, '''    void setReferenceInstWave(size_t mesh, const float wave[16]);''',
      '''    void setReferenceInstWave(size_t mesh, const float wave[16]);
    // A prop mesh's static-model displacement (research 133 section 3): its height image (slot 95) and constant 45,
    // P += normalize(N) (h.r scale + bias) in the vertex shader (record row 30, VS t43).
    void setReferenceInstDisp(size_t mesh, int texture, float scale, float bias) {
        if (mesh >= refTexInst_.size()) return;
        auto& g = refTexInst_[mesh];
        g.disp[0] = 1.0f, g.disp[1] = scale, g.disp[2] = bias, g.disp[3] = 0.0f;
        g.dispTex = texture;
        instParamsDirty_ = true;
    }''')
patch(H, '''        float coat[16] = {};
        int coatTex[2] = {-1, -1};''', '''        float coat[16] = {};
        int coatTex[2] = {-1, -1};
        // Static-model displacement (row 30: on, scale, bias, 0) and its height image (VS t43).
        float disp[4] = {};
        int dispTex = -1;''')

R = 'src/viewport/render.cpp'
patch(R, '''cbuffer PropWave : register(b8) { float4 pwRows[8]; float4 wave79; float4 wave80; float4 wave81; float4 wave82; };''',
      '''cbuffer PropWave : register(b8) { float4 pwRows[8]; float4 wave79; float4 wave80; float4 wave81; float4 wave82; float4 pwRows12[18];
                                   float4 dispC; };  // row 30: static-model displacement (on, scale, bias, 0)
Texture2D<float4> dispTex : register(t43);  // its height image (slot 95, research 133 section 3)''')
patch(R, '''        p.xyz += h * i.vcol.a * i.n;
    }
    float3 w = float3(dot(i.r0, p), dot(i.r1, p), dot(i.r2, p));''', '''        p.xyz += h * i.vcol.a * i.n;
    }
    // Static-model displacement (research 133 section 3, VS_36fc68f3:231-235): along the vertex normal by the height
    // image's red channel x constant 45 (scale, bias; (2, -1) on stone_wall_01: +-1 unit).
    if (dispC.x > 0.5 && dot(i.n, i.n) > 1e-12)
        p.xyz += normalize(i.n) * (dispTex.SampleLevel(samp0, i.uv, 0).r * dispC.y + dispC.z);
    float3 w = float3(dot(i.r0, p), dot(i.r1, p), dot(i.r2, p));''')
patch(R, '''                std::memcpy(&all[j * 128 + 104], gi.coat, sizeof(gi.coat));  // rows 26-29 (research 137)''',
      '''                std::memcpy(&all[j * 128 + 104], gi.coat, sizeof(gi.coat));  // rows 26-29 (research 137)
                std::memcpy(&all[j * 128 + 120], gi.disp, sizeof(gi.disp));  // row 30 (static-model displacement, VS)''')
# The height image per mesh at VS t43 wherever the mesh's record is bound to the VS (main pass, layer passes, shadows),
# and the wrap sampler at VS s0.
BIND = '''
                ID3D11ShaderResourceView* dsp = g.disp[0] > 0.5f && g.dispTex >= 0 && size_t(g.dispTex) < textures_.size() ? textures_[size_t(g.dispTex)] : nullptr;
                ctx_->VSSetShaderResources(43, 1, &dsp);  // the static-model displacement's height image'''
patch(R, '''                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
            }''', '''                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);''' + BIND + '''
            }''')
patch(R, '''                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);  // PropWave
            }''', '''                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);  // PropWave''' + BIND + '''
            }''')
patch(R, '''                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / 128], &first, &num);  // PropWave
            }''', '''                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / 128], &first, &num);  // PropWave''' + BIND + '''
            }''')
patch(R, '''    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : (filterPs_ ? filterPs_ : psTex_), nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);''', '''    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : (filterPs_ ? filterPs_ : psTex_), nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    ctx_->VSSetSamplers(0, 1, &sampler_);  // the props' displacement height images (vs_tex_inst)''')
patch(R, '''    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);''',
      '''    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);
    if (sampler_) ctx_->VSSetSamplers(0, 1, &sampler_);  // the props' displacement height images''')
print('ok')
