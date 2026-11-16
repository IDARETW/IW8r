import io

# Owner 2026-11-16: "a lot of the vfx dont show anything in the preview". The preview drew billboards, oriented sprites,
# tails and flares only; 38 of the 159 exported effects hold none of those (research 138 section 7: 9 runner = child
# effect, 7 model, 5 / 6 light, 2 decal). The simulation now reads what those elements need: a runner's child effects
# (initRunner's ParticleSystemDef references), a model element's XModels (initModel), a light's brightness and kind
# (initLightOmni / initLightSpot), a decal's materials (initDecal). The preview draws them (vfx_preview.cpp).

ROOT = 'E:/IW8-Radiant/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('src/core/vfx_sim.h', [
('''    int element = 0;             // stateDefs[0].elementType: 0 billboard, 8 oriented sprite, 10 tail (others not drawn)''',
 '''    int element = 0;             // stateDefs[0].elementType: 0 billboard, 2 decal, 3 flare, 5 / 6 light, 7 model, 8 oriented
                                 // sprite, 9 runner (child effect), 10 tail, 11 vector field (research 138 section 7)
    std::vector<std::string> runners;  // initRunner: the child effects (ParticleSystemDef names) a runner particle plays
    std::vector<std::string> models;   // initModel: the XModels a model particle shows (one picked per particle)
    struct Light {
        bool present = false;
        bool spot = false;               // initLightSpot (else initLightOmni)
        double brightness = 1;           // m_brightness
    } light;'''),
])

rw('src/core/vfx_sim.cpp', [
('''void findMaterialRefs(const Value& node, int depth, std::vector<std::string>& out) {
    if (depth > 64) return;
    if (node.type == Value::Type::Array) {
        for (const auto& item : node.arr) findMaterialRefs(item, depth + 1, out);
        return;
    }
    if (node.type != Value::Type::Object) return;
    const Value* isRef = node.get("asset_reference");
    const Value* type = node.get("type");
    const Value* name = node.get("name");
    if (isRef && isRef->type == Value::Type::Bool && isRef->b && type && type->string() && *type->string() == "Material" &&
        name && name->string()) {
        const std::string& n = *name->string();
        const std::string m = !n.empty() && n[0] == ',' ? n.substr(1) : n;
        if (!m.empty() && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
        return;
    }
    for (const auto& kv : node.obj) {
        if (kv.first == "$native") continue;
        findMaterialRefs(kv.second, depth + 1, out);
    }
}''', '''// Every asset reference of `assetType` under `node` (asset_reference true, type, name; a leading ',' dropped), once each.
void findAssetRefs(const Value& node, int depth, const char* assetType, std::vector<std::string>& out) {
    if (depth > 64) return;
    if (node.type == Value::Type::Array) {
        for (const auto& item : node.arr) findAssetRefs(item, depth + 1, assetType, out);
        return;
    }
    if (node.type != Value::Type::Object) return;
    const Value* isRef = node.get("asset_reference");
    const Value* type = node.get("type");
    const Value* name = node.get("name");
    if (isRef && isRef->type == Value::Type::Bool && isRef->b && type && type->string() && *type->string() == assetType &&
        name && name->string()) {
        const std::string& n = *name->string();
        const std::string m = !n.empty() && n[0] == ',' ? n.substr(1) : n;
        if (!m.empty() && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
        return;
    }
    for (const auto& kv : node.obj) {
        if (kv.first == "$native") continue;
        findAssetRefs(kv.second, depth + 1, assetType, out);
    }
}

void findMaterialRefs(const Value& node, int depth, std::vector<std::string>& out) { findAssetRefs(node, depth, "Material", out); }'''),
('''    if (const Value* m = mod("initMaterial")) findMaterialRefs(*m, 0, e.materials);
    if (!e.materials.empty()) e.material = e.materials[0];''', '''    if (const Value* m = mod("initMaterial")) findMaterialRefs(*m, 0, e.materials);
    // A decal element names its materials in initDecal (research 138 section 7).
    if (const Value* d = mod("initDecal"); d && e.materials.empty()) findMaterialRefs(*d, 0, e.materials);
    if (!e.materials.empty()) e.material = e.materials[0];
    if (const Value* r = mod("initRunner")) findAssetRefs(*r, 0, "ParticleSystemDef", e.runners);
    if (const Value* m = mod("initModel")) findAssetRefs(*m, 0, "XModel", e.models);
    for (const char* lightModule : {"initLightOmni", "initLightSpot"})
        if (const Value* l = mod(lightModule)) {
            e.light.present = true;
            e.light.spot = std::string(lightModule) == "initLightSpot";
            e.light.brightness = std::max(0.0, numberOr(l->get("m_brightness"), 1.0));
            break;
        }'''),
])

rw('tests/vfx_sim_tests.cpp', [
('''TEST(vfx_flare_modules_and_screen_layout) {''', '''TEST(vfx_runner_model_light_decal_elements) {
    // Owner 2026-11-16: effects of runners, models, lights and decals previewed nothing; the simulation reads what they
    // draw from (research 138 section 7).
    const auto ref = [](const char* type, const char* name) {
        return std::string(R"({"assetList":{"values":[{"value":{"asset_reference":true,"type":")") + type + R"(","name":")" + name + R"("}}]}})";
    };
    const std::string spawn = module("initSpawn", 0, "");
    {
        VfxSimDef def;
        CHECK(buildVfxSim(moduleExport(0, 2, 0, 3, 0, spawn + "," + module("initRunner", 0, R"("m_linkedAssetList":)" + ref("ParticleSystemDef", "vfx/test/child_ch")), "", 9), def));
        CHECK(def.emitters.size() == 1 && def.emitters[0].element == 9 && def.emitters[0].runners == std::vector<std::string>{"vfx/test/child_ch"});
    }
    {
        VfxSimDef def;
        CHECK(buildVfxSim(moduleExport(2, 0, 3, 2, 0, spawn + "," + module("initModel", 0, R"("m_linkedAssetList":)" + ref("XModel", "shell_casing_9mm")), "", 7), def));
        CHECK(def.emitters.size() == 1 && def.emitters[0].models == std::vector<std::string>{"shell_casing_9mm"} && def.emitters[0].materials.empty());
    }
    {
        VfxSimDef def;
        CHECK(buildVfxSim(moduleExport(0, 4, 0, 1, 0, spawn + "," + module("initLightSpot", 0, R"("m_brightness":2.5)"), "", 6), def));
        CHECK(def.emitters.size() == 1 && def.emitters[0].light.present && def.emitters[0].light.spot && std::fabs(def.emitters[0].light.brightness - 2.5) < 1e-9);
    }
    {
        VfxSimDef def;
        CHECK(buildVfxSim(moduleExport(2, 0, 1, 5, 0, spawn + "," + module("initDecal", 0, R"("m_linkedAssetList":)" + ref("Material", ",i/vfx_decal_scorch")), "", 2), def));
        CHECK(def.emitters.size() == 1 && def.emitters[0].material == "i/vfx_decal_scorch");
    }
}

TEST(vfx_flare_modules_and_screen_layout) {'''),
])
print('ok')
