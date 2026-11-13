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
CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'
MC = 'src/qtapp/main_window.cpp'

# ======================= renderer: storage, setters, upload, binding
patch(RH, '''        int reveal = -1;
        float params[16] = {};
    };
    std::vector<RefLayer> refLayers_;''', '''        int reveal = -1;
        float params[16] = {};
        // Research 107 (base batches): emissive scale (c36) + on, transmission tint (c18) + on, wrap (c103) + on; in
        // the same 256-byte slot of b7 after params. Their images (texture ids, -1 = none): t3 and t19.
        float extra[16] = {};
        int emissive = -1, transmission = -1;
    };
    std::vector<RefLayer> refLayers_;''')
patch(RH, '''        int normal = -1;            // setReferenceInstNormal
        float params[16] = {};      // setReferenceInstParams''', '''        int normal = -1;            // setReferenceInstNormal
        float params[16] = {};      // setReferenceInstParams
        float extra[16] = {};       // setReferenceInstExtra (RefLayer::extra)
        int emissive = -1, transmission = -1;''')
patch(RH, '''    void setReferenceBaseParams(size_t batch, const float params[16]);''', '''    void setReferenceBaseParams(size_t batch, const float params[16]);
    // Research 107 terms of a base batch / a prop mesh (RefLayer::extra layout) and their images (-1 = none).
    void setReferenceBaseExtra(size_t batch, const float extra[16], int emissiveTexture, int transmissionTexture);
    void setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture);''')
patch(R, '''bool SceneRenderer::uploadLayerParams() {''', '''void SceneRenderer::setReferenceBaseExtra(size_t batch, const float extra[16], int emissiveTexture, int transmissionTexture) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer > 0 || refLayers_[batch].decal) return;
    std::memcpy(refLayers_[batch].extra, extra, sizeof(refLayers_[batch].extra));
    refLayers_[batch].emissive = emissiveTexture, refLayers_[batch].transmission = transmissionTexture;
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture) {
    if (mesh >= refTexInst_.size()) return;
    std::memcpy(refTexInst_[mesh].extra, extra, sizeof(refTexInst_[mesh].extra));
    refTexInst_[mesh].emissive = emissiveTexture, refTexInst_[mesh].transmission = transmissionTexture;
    instParamsDirty_ = true;
}

bool SceneRenderer::uploadLayerParams() {''')
patch(R, '''                std::memcpy(&all[j * 64], refLayers_[k].params, sizeof(refLayers_[0].params));''',
      '''                std::memcpy(&all[j * 64], refLayers_[k].params, sizeof(refLayers_[0].params));
                std::memcpy(&all[j * 64 + 16], refLayers_[k].extra, sizeof(refLayers_[0].extra));''')
patch(R, '''                std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));''', '''                std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));
                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));''')
# props: bind the images with the normal map
patch(R, '''        if (propNormals) {
            const int n = g.normal >= 0 && size_t(g.normal) < textures_.size() ? g.normal : flatNrmTex_;
            ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]);
        }''', '''        if (propNormals) {
            const int n = g.normal >= 0 && size_t(g.normal) < textures_.size() ? g.normal : flatNrmTex_;
            ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]);
            // Research 107: emissive (t3) and transmission (t19) images of the mesh's material.
            ID3D11ShaderResourceView* e = g.emissive >= 0 && size_t(g.emissive) < textures_.size() ? textures_[size_t(g.emissive)] : nullptr;
            ID3D11ShaderResourceView* t = g.transmission >= 0 && size_t(g.transmission) < textures_.size() ? textures_[size_t(g.transmission)] : nullptr;
            ctx_->PSSetShaderResources(3, 1, &e);
            ctx_->PSSetShaderResources(19, 1, &t);
        }''')

# ======================= shader
# (PSIn: lgvBack replaced the stale lgv interpolant by hand)
patch(R, '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm;
    return o;''', '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm;
    o.lgvBack = float3(0, 0, 0);
    return o;''')
patch(R, '''        o.t = max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0);
        o.lm = float2(-2, -2);
    }''', '''        o.t = max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0);
        o.lm = float2(-2, -2);
        o.lgvBack = max(0.282095 * dc - 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0);  // E(-n): transmission
    } else {
        o.lgvBack = float3(0, 0, 0);
    }''')
patch(R, '''cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };''',
      '''cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO;
                               float4 matE; float4 matTr; float4 matW; float4 matX; };  // matE/Tr/W: research 107 (RefLayer::extra)
Texture2D tr0 : register(t19);  // the transmission image (texture slot 68, sRGB) when matTr.w = 1
// Wrap diffuse (research 107 section 2.2): w = constant 103 per channel.
float wrapF(float x, float w) {
    w = max(w, 1e-6);
    const float m = max(x, -w) + w;
    return saturate(x < w ? m * m * 3.0 / (4.0 * w * (w * w + 3.0)) : 3.0 * x / (w * w + 3.0));
}
float3 wrap3(float x, float3 w) { return float3(wrapF(x, w.r), wrapF(x, w.g), wrapF(x, w.b)); }''')
# the lightmap's directional part, wrapped where the material says so
patch(R, '''        float3 stockDw = Nf, stockB = 0;  // the stock lightmap's directional part, for E(R) below''',
      '''        float3 stockDw = Nf, stockB = 0;  // the stock lightmap's directional part, for E(R) below
        float3 stockDirN = 0;  // that part as it entered `indirect` (wrapped on wrap-diffuse materials)''')
patch(R, '''                indirect = a.rgb + b * saturate(dot(N, Dw));
                stockDw = Dw, stockB = b;''', '''                stockDirN = b * (matW.w > 0.5 ? wrap3(dot(N, Dw), matW.rgb) : saturate(dot(N, Dw)).xxx);
                indirect = a.rgb + stockDirN;
                stockDw = Dw, stockB = b;''')
patch(R, '''            float3 Lr = (indirect - stockB * saturate(dot(N, stockDw))) + stockB * saturate(dot(R, stockDw));''',
      '''            float3 Lr = (indirect - stockDirN) + stockB * saturate(dot(R, stockDw));''')
# the sun: wrap diffuse with the coloured penumbra
patch(R, '''            float3 Ls = gameSun.xyz;
            float nl = dot(N, Ls);
            if (nl > 0) {''', '''            float3 Ls = gameSun.xyz;
            float nl = dot(N, Ls);
            if (matW.w > 0.5) {
                // Wrap diffuse (research 107 section 2.2): the diffuse part wraps past the terminator per channel and the
                // shadow takes a per-channel power (a coloured penumbra); the env BRDF and the specular keep the plain N.L.
                const float mw = min(matW.r, min(matW.g, matW.b));
                const float3 kw = 1.0 + 1.35 * (matW.rgb - mw);
                const float3 ew = float3(kw.b * kw.g, kw.b * kw.r, kw.g * kw.r);
                const float2 abw = gameEnvBrdf(saturate(nl), gp);
                const float3 flw = f0 * abw.x + abw.y;
                c += gameSunCol.rgb * pow(max(saturate(visibility), 1e-6), ew) * wrap3(nl, matW.rgb) * diffAlbedo *
                     (1.0 - max(flw.r, max(flw.g, flw.b)));
            }
            if (nl > 0) {''')
patch(R, '''                c += gameSunCol.rgb * visibility * saturate(nl) *
                     (diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625 * (baseMat ? nmCavity : 1.0));
            }''', '''                c += gameSunCol.rgb * visibility * saturate(nl) *
                     ((matW.w > 0.5 ? 0.0 : diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b)))) + F * Dg * Vg * 1.0390625 * (baseMat ? nmCavity : 1.0));
            }
            // Transmission (research 107 section 1): grass, foliage, tarps, cloth, flags. The baked light seen from the
            // back side, and the sun through the blade as a GGX lobe of the surface's own roughness.
            if (matTr.w > 0.5) {
                const float3 T = srgbToLinear3(tr0.Sample(samp0, uvS).rgb) * matTr.rgb;
                const float kTr = 1.0 - exp2(-3.32193 * a2);
                const float3 Eb = lmOk ? (indirect - stockDirN) + stockB * saturate(dot(-N, stockDw))
                                       : (propLit(i) ? i.lgvBack * gameSkyScale.y : indirect);
                c += T * (1.0 - fmax) * kTr * Eb * aoMB;
                const float nlb = dot(gameSun.xyz, -N);
                if (nlb > 0.0) {
                    const float3 Hb = normalize(gameSun.xyz - R);
                    const float hb = dot(-N, Hb), qb = hb * hb * (a2 - 1.0) + 1.0;
                    const float Db = a2 / max(qb * qb, 1e-8);
                    const float Vb = 1.0 / ((nlb + sqrt(a2 + (1.0 - a2) * nlb * nlb)) * (nv + sqrt(a2 + (1.0 - a2) * nv * nv)));
                    const float2 abb = gameEnvBrdf(nlb, gp);
                    const float3 flb = f0 * abb.x + abb.y;
                    c += gameSunCol.rgb * T * visibility * nlb * Db * Vb * 1.0390625 * (1.0 - max(flb.r, max(flb.g, flb.b)));
                }
            }
            // Emissive (research 107 section 5): the image x constant 36, unlit, Fresnel-weighted, exposed as the rest.
            if (matE.w > 0.5) c += srgbToLinear3(emi0.Sample(samp0, uvS).rgb) * matE.rgb * (1.0 - fmax);''')

# ======================= camera: facts and their application
patch(CH, '''        StockLayeredMaterial constants;  // layer 0 at least when the body has a pixel constant buffer
    };''', '''        StockLayeredMaterial constants;  // layer 0 at least when the body has a pixel constant buffer
        // Research 107: the emissive (slot 18) and transmission (slot 68) images ("" = none) and constants: emissive
        // scale c36, transmission tint c18, wrap c103 (rgb; .w = 1 when the term applies).
        std::string emissiveFile, transmissionFile;
        std::array<float, 4> emissiveScale{0, 0, 0, 0}, transTint{0, 0, 0, 0}, wrap{0, 0, 0, 0};
    };''')
patch(CH, '''    std::optional<std::array<float, 16>> materialConstantsFor(const std::string& material) const;''',
      '''    std::optional<std::array<float, 16>> materialConstantsFor(const std::string& material) const;
    // Research 107 terms of a stock material into the renderer (a base batch, else a prop mesh): false while one of its
    // images is still loading (asked again next frame).
    bool applyMaterialExtras(const std::string& material, size_t index, bool propMesh);''')
patch(CC, '''void CameraView::setBaseMaterialConstants(size_t k) {''', '''bool CameraView::applyMaterialExtras(const std::string& m, size_t index, bool propMesh) {
    if (m.empty() || !factsLookup_ || m.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return true;
    const auto f = factsLookup_(m);
    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0)) return true;
    int ids[2] = {-1, -1};
    const std::string files[2] = {f->emissiveScale[3] > 0 ? f->emissiveFile : std::string(), f->transTint[3] > 0 ? f->transmissionFile : std::string()};
    for (int k = 0; k < 2; ++k) {
        if (files[k].empty()) continue;
        const std::string key = "\\x06" + files[k];
        const int id = textureFor(key);
        if (!texIds_.count(key)) return false;  // still loading
        ids[k] = id == checkerTex_ ? -1 : id;
    }
    float x[16] = {f->emissiveScale[0], f->emissiveScale[1], f->emissiveScale[2], ids[0] >= 0 ? 1.0f : 0.0f,
                   f->transTint[0], f->transTint[1], f->transTint[2], ids[1] >= 0 ? 1.0f : 0.0f,
                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, 0, 0, 0, 0};
    if (propMesh) renderer_.setReferenceInstExtra(index, x, ids[0], ids[1]);
    else renderer_.setReferenceBaseExtra(index, x, ids[0], ids[1]);
    return true;
}

void CameraView::setBaseMaterialConstants(size_t k) {''')
patch(CC, '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    setBaseMaterialConstants(k);''', '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    setBaseMaterialConstants(k);
                    if (!applyMaterialExtras(refTexMaterials_[k], k, false)) {
                        pending = true;
                        continue;
                    }''')
patch(CC, '''                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_.setReferenceInstParams(k, p->data());
                    texInstResolved_[k] = true;''', '''                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_.setReferenceInstParams(k, p->data());
                    if (!applyMaterialExtras(texInstMaterials_[k], k, true)) {
                        pending = true;
                        continue;
                    }
                    texInstResolved_[k] = true;''')

# ======================= main window: the facts from the material cache
patch(MC, '''        if (auto c = matCache_->materialConstants(qs(m))) f.constants = *c;
        return f;''', '''        if (auto c = matCache_->materialConstants(qs(m))) f.constants = *c;
        // Research 107: emissive x c36 (1 when the material has no c36), transmission x c18, wrap c103.
        const auto cst = [&](int k) -> const std::array<float, 4>* {
            const auto it = e->consts.find(k);
            return it == e->consts.end() ? nullptr : &it->second;
        };
        if (!e->emissiveDds.isEmpty()) {
            f.emissiveFile = e->emissiveDds.toStdString();
            const auto* c36 = cst(36);
            f.emissiveScale = c36 ? std::array<float, 4>{(*c36)[0], (*c36)[1], (*c36)[2], 1.0f} : std::array<float, 4>{1, 1, 1, 1};
        }
        if (!e->transmissionDds.isEmpty()) {
            f.transmissionFile = e->transmissionDds.toStdString();
            const auto* c18 = cst(18);
            f.transTint = c18 ? std::array<float, 4>{(*c18)[0], (*c18)[1], (*c18)[2], 1.0f} : std::array<float, 4>{1, 1, 1, 1};
        }
        if (const auto* c103 = cst(103)) f.wrap = {(*c103)[0], (*c103)[1], (*c103)[2], 1.0f};
        return f;''')
print('ok')
