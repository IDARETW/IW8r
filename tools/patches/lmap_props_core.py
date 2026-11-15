import io

# Research 105 section 3.2: lightmapped static models (tml / tmlc, model flag 2) read the world lightmap through their
# per-vertex lmapCoords (ACTS fork 834568b9 exports them as _LMAPUV) and the placement's GfxWorld.lmapLookup entry
# (u16 lmapIndex, vec2 offset, scaleWidth, scaleHeight; 20 bytes): lightmap uv = offset + lmapUv x scale in page
# lmapIndex. Core: ImportedMesh::lmapUv, StockModelInstance::lmapPage / lmapRect, tests.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/model_import.h', '''    uint32_t sourceFlags = 0;  // glTF primitive extras.sourceFlags (the XSurface flags ACTS copies; 0x40 = self visibility)''',
      '''    uint32_t sourceFlags = 0;  // glTF primitive extras.sourceFlags (the XSurface flags ACTS copies; 0x40 = self visibility)
    // Lightmapped stock models (ACTS GLB attribute _LMAPUV, research 105 section 3.2): per vertex the lightmap u, v
    // (0..1) within the placement's lmapLookup rectangle. Empty when the primitive has none.
    std::vector<std::array<float, 2>> lmapUv;''')

patch('src/core/glb_mesh.cpp', '''            if (const json::Value* ex = prim.get("extras"))''', '''            if (const json::Value* lu = attrs->get("_LMAPUV")) {
                // ACTS: PackedLmapCoords as VEC2 UNSIGNED_SHORT, normalised (research 105 section 3.2).
                size_t nl = 0;
                if (!readAccessor(int(lu->num), 2, v, nl)) return false;
                if (nl != n) return fail("_LMAPUV count differs from POSITION");
                m.lmapUv.resize(n);
                for (size_t i = 0; i < n; ++i) m.lmapUv[i] = {float(v[i * 2]), float(v[i * 2 + 1])};
            }
            if (const json::Value* ex = prim.get("extras"))''')

H = 'src/core/stock_smodels.h'
patch(H, '''    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags''',
      '''    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags
    // modelFlags & 2 (LIGHTMAP): GfxWorld.lmapLookup[lightingIndex] (research 105 section 3.2): the world lightmap page
    // and the rectangle (offset u, v, scale u, v) the model's per-vertex lightmap coordinates map into. -1: none.
    int lmapPage = -1;
    std::array<float, 4> lmapRect{0, 0, 0, 0};''')

C = 'src/core/stock_smodels.cpp'
s = io.open(ROOT + C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
# the end of readStockModelInstances: the last "    return true;\n}\n\nstd::map<std::string, size_t> stockModelUsage"
a = '''            out.push_back(std::move(s));
        }
    }
    return true;
}

std::map<std::string, size_t> stockModelUsage('''
b = '''            out.push_back(std::move(s));
        }
    }
    // Lightmapped placements (model flag 2): GfxWorld.lmapLookup (GfxModelLightmapInst, 20 bytes: u16 lmapIndex, two
    // pad bytes, vec2 offset, scaleWidth, scaleHeight), values or hex bytes; optional.
    struct LmapInst {
        int page = -1;
        std::array<float, 4> rect{0, 0, 0, 0};
    };
    std::vector<LmapInst> lmap;
    if (const json::View ll = fields.get("lmapLookup"); ll.valid()) {
        const json::View lb = ll.get("bytes");
        const json::View lv = ll.get("values");
        std::vector<uint8_t> raw;
        if (stringOf(lb, scratch, hex)) {
            if (unhex(hex, raw) && raw.size() % 20 == 0)
                for (size_t o = 0; o + 20 <= raw.size(); o += 20)
                    lmap.push_back({int(u16At(raw, o)), {f32At(raw, o + 4), f32At(raw, o + 8), f32At(raw, o + 12), f32At(raw, o + 16)}});
        } else if (lv.type() == Value::Type::Array) {
            forEachRecord(lv, [&](const Value& r) {
                LmapInst li;
                li.page = int(num(r.get("lmapIndex")));
                const Value* o = r.get("offset");
                const Value* ov = o ? o->get("v") : nullptr;
                for (int k = 0; k < 2; ++k) li.rect[size_t(k)] = float(ov && size_t(k) < ov->arr.size() ? num(&ov->arr[size_t(k)]) : 0.0);
                li.rect[2] = float(num(r.get("scaleWidth")));
                li.rect[3] = float(num(r.get("scaleHeight")));
                lmap.push_back(li);
            });
        }
    }
    for (StockModelInstance& s : out)
        if ((s.modelFlags & 2u) && s.lightingIndex < lmap.size()) s.lmapPage = lmap[s.lightingIndex].page, s.lmapRect = lmap[s.lightingIndex].rect;
    return true;
}

std::map<std::string, size_t> stockModelUsage('''
assert s.count(a.replace('\n', nl)) == 1
s = s.replace(a.replace('\n', nl), b.replace('\n', nl))
io.open(ROOT + C, 'w', encoding='utf-8', newline='').write(s)

T = 'tests/stock_smodels_tests.cpp'
patch(T, '''        if (withIndirection)
            s += R"(,"modelStaticIndirection":{"stride":8,"count":2,"values":[)"
                 R"({"lightingInstanceIndirection":11,"splinedModelInstanceIndex":0,"worldTransient":2},)"
                 R"({"lightingInstanceIndirection":0,"splinedModelInstanceIndex":0,"worldTransient":0}]})";
        return s + "}}}}";''', '''        if (withIndirection)
            s += R"(,"modelStaticIndirection":{"stride":8,"count":2,"values":[)"
                 R"({"lightingInstanceIndirection":11,"splinedModelInstanceIndex":0,"worldTransient":2},)"
                 R"({"lightingInstanceIndirection":0,"splinedModelInstanceIndex":0,"worldTransient":0}]})";
        s += "}";  // smodels
        if (withIndirection)  // the lightmapped placement's rectangle (research 105 section 3.2)
            s += R"(,"lmapLookup":{"stride":20,"count":1,"values":[{"lmapIndex":3,"offset":{"$union_views":true,"v":[0.25,0.5]},)"
                 R"("scaleWidth":0.125,"scaleHeight":0.0625}]})";
        return s + "}}}";''')
patch(T, '''        CHECK(v[1].lightingIndex == 0 && v[1].worldTransient == 0);''', '''        CHECK(v[1].lightingIndex == 0 && v[1].worldTransient == 0);
        // tm/lm is lightmapped (flag 2): lmapLookup[0]; tm/p (probe-lit) has none.
        CHECK(v[1].lmapPage == 3 && v[1].lmapRect[0] == 0.25f && v[1].lmapRect[1] == 0.5f && v[1].lmapRect[2] == 0.125f &&
              v[1].lmapRect[3] == 0.0625f);
        CHECK(v[0].lmapPage == -1);''')

G = 'tests/glb_mesh_tests.cpp'
patch(G, '''    for (uint32_t w : {up, down, up, down}) put32(bin, w);  // at 140''', '''    for (uint32_t w : {up, down, up, down}) put32(bin, w);  // at 140
    for (uint32_t k = 0; k < 4; ++k) put32(bin, (16384u * k) | ((65535u - 16384u * k) << 16));  // _LMAPUV at 156''')
patch(G, '''        "\\"meshes\\":[{\\"primitives\\":[{\\"attributes\\":{\\"POSITION\\":0,\\"NORMAL\\":1,\\"TEXCOORD_0\\":2,\\"_SELFVIS\\":4},\\"indices\\":3,"''',
      '''        "\\"meshes\\":[{\\"primitives\\":[{\\"attributes\\":{\\"POSITION\\":0,\\"NORMAL\\":1,\\"TEXCOORD_0\\":2,\\"_SELFVIS\\":4,\\"_LMAPUV\\":5},\\"indices\\":3,"''')
patch(G, '''        "{\\"bufferView\\":4,\\"componentType\\":5121,\\"count\\":4,\\"type\\":\\"VEC4\\"}],"''',
      '''        "{\\"bufferView\\":4,\\"componentType\\":5121,\\"count\\":4,\\"type\\":\\"VEC4\\"},"
        "{\\"bufferView\\":5,\\"componentType\\":5123,\\"count\\":4,\\"type\\":\\"VEC2\\",\\"normalized\\":true}],"''')
patch(G, '''        "{\\"buffer\\":0,\\"byteOffset\\":140,\\"byteLength\\":16}],"
        "\\"buffers\\":[{\\"byteLength\\":156}]}";''', '''        "{\\"buffer\\":0,\\"byteOffset\\":140,\\"byteLength\\":16},{\\"buffer\\":0,\\"byteOffset\\":156,\\"byteLength\\":16}],"
        "\\"buffers\\":[{\\"byteLength\\":172}]}";''')
patch(G, '''    // Without the attribute: empty.''', '''    CHECK(mesh.lmapUv.size() == 4);
    if (mesh.lmapUv.size() == 4) CHECK(near(mesh.lmapUv[1][0], 16384.0 / 65535.0, 1e-6) && near(mesh.lmapUv[1][1], 49151.0 / 65535.0, 1e-6));
    // Without the attribute: empty.''')
print('ok')
