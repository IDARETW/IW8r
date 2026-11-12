// Stock effect inspector: reads ACTS "vfx" (ParticleSystemDef) exports. Synthetic files, plus an optional real export
// named by the IW8R_VFX_SAMPLE environment variable (skipped when unset).
#include "vfx_info.h"
#include "test_harness.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace iw8r;

namespace {

// Two emitters. Emitter 0 is one-shot: delay 0.5 s, no emitter life, particle life 2 s, so it ends at 2.5 s.
// Emitter 1 has an emitter life of 1.5 s and no infinite flags. The material name repeats with and without the shared
// ',' marker. The sound alias is inline (active_member "sound"). The model, the child effect and a PhysicsFXShape
// reference are the other kinds the reader sees.
const char* kOneShot = R"JSON({"format":"mw19-asset-json","format_version":1,"profile":"replay-1.20","pool":"vfx","pool_id":44,
"root_type":"ParticleSystemDef","asset":{"type":"ParticleSystemDef","address":"0x1","fields":{
 "$native":"78e3063800000000",
 "name":{"address":"0x2","type":"const char","string":"vfx/test/one_shot.vfx"},
 "emitterDefs":{"type":"ParticleEmitterDef","stride":160,"count":2,"values":[
  {"flags":2,"emitterLife":{"min":0.0,"max":0.0},"emitterDelay":{"min":0.0,"max":0.5},"particleLife":{"min":2.0,"max":2.0},
   "stateDefs":{"type":"ParticleStateDef","count":1,"values":[{"moduleGroupDefs":{"count":1,"values":[{"moduleDefs":{"count":3,"values":[
     {"moduleType":{"value":11,"name":"PARTICLE_MODULE_INIT_MATERIAL"},"moduleData":{"value":{"m_linkedAssetList":{"assetList":{"values":[
       {"active_member":"material","value":{"address":"0x3","type":"Material","asset_reference":true,"name":",elcq/vfx_ui_chevron_2_ds"}},
       {"active_member":"material","value":{"address":"0x4","type":"Material","asset_reference":true,"name":"elcq/vfx_ui_chevron_2_ds"}}]}}}}},
     {"moduleType":{"value":22,"name":"PARTICLE_MODULE_INIT_SOUND"},"moduleData":{"value":{"m_linkedAssetList":{"assetList":{"values":[
       {"active_member":"sound","value":{"type":"const char","string":"shell_eject_20mm"}}]}}}}},
     {"moduleType":{"value":13,"name":"PARTICLE_MODULE_INIT_MODEL"},"moduleData":{"value":{"m_linkedAssetList":{"assetList":{"values":[
       {"active_member":"model","value":{"type":"XModel","asset_reference":true,"name":"fx_rifle_shell"}}]}}}}}
   ]}}]}}]}},
  {"flags":2097152,"emitterLife":{"min":0.0,"max":1.5},"emitterDelay":{"min":0.0,"max":0.0},"particleLife":{"min":9.0,"max":9.0},
   "stateDefs":{"type":"ParticleStateDef","count":1,"values":[{"moduleGroupDefs":{"count":2,"values":[
     {"moduleDefs":{"count":1,"values":[{"moduleType":{"value":21,"name":"PARTICLE_MODULE_INIT_RUNNER"},"moduleData":{"value":{
       "m_linkedAssetList":{"assetList":{"values":[{"active_member":"vfx","value":{"type":"ParticleSystemDef","asset_reference":true,"name":"vfx/test/child_spark"}}]}}}}}]}},
     {"moduleDefs":{"count":1,"values":[{"moduleType":{"value":3,"name":"PARTICLE_MODULE_PHYSICS_RAY_CAST"},"moduleData":{"value":{
       "m_linkedAssetList":{"assetList":{"values":[{"active_member":"shape","value":{"type":"PhysicsFXShape","asset_reference":true,"name":"phys_proxy_test"}}]}}}}}]}}
   ]}}]}}]},
 "numEmitters":2,"flags":262145,"preRollMSec":0}}})JSON";

// The same effect with one emitter flagged 0x8 (infinite particle life).
const char* kLooping = R"JSON({"format":"mw19-asset-json","format_version":1,"pool":"vfx","pool_id":44,"root_type":"ParticleSystemDef",
"asset":{"type":"ParticleSystemDef","fields":{
 "name":{"type":"const char","string":"vfx/test/loop.vfx"},
 "emitterDefs":{"count":1,"values":[{"flags":8,"emitterLife":{"min":0.0,"max":0.0},"emitterDelay":{"min":0.0,"max":0.0},
   "particleLife":{"min":1.0,"max":1.0},"stateDefs":{"values":[]}}]},
 "numEmitters":1}}})JSON";

std::string writeTemp(const std::string& name, const std::string& text) {
    const std::string path = iw8r::test::tmpDir() + "/" + name;
    std::ofstream out(path, std::ios::binary);
    out << text;
    return path;
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    for (const auto& s : list) {
        if (s == value) return true;
    }
    return false;
}

}  // namespace

TEST(vfx_info_one_shot_fields) {
    VfxInfo info;
    std::string err;
    CHECK(parseVfxExport(kOneShot, info, &err));
    CHECK(err.empty());
    CHECK(info.name == "vfx/test/one_shot.vfx");
    CHECK(info.emitterCount == 2);
    CHECK(!info.looping);
    CHECK(info.lifeSeconds > 2.49 && info.lifeSeconds < 2.51);  // max(0.5 + 2.0, 0 + 1.5)
    CHECK(info.materials.size() == 1 && info.materials[0] == "elcq/vfx_ui_chevron_2_ds");
    CHECK(info.sounds.size() == 1 && info.sounds[0] == "shell_eject_20mm");
    CHECK(info.models.size() == 1 && info.models[0] == "fx_rifle_shell");
    CHECK(info.childEffects.size() == 1 && info.childEffects[0] == "vfx/test/child_spark");
    // PhysicsFXShape is not one of the four lists; it must not leak into any of them.
    CHECK(!contains(info.materials, "phys_proxy_test") && !contains(info.models, "phys_proxy_test"));
}

TEST(vfx_info_looping_flag) {
    VfxInfo info;
    CHECK(parseVfxExport(kLooping, info, nullptr));
    CHECK(info.name == "vfx/test/loop.vfx");
    CHECK(info.looping);
    CHECK(info.lifeSeconds == -1.0);
    CHECK(info.emitterCount == 1);
    CHECK(info.materials.empty() && info.sounds.empty() && info.childEffects.empty());
}

TEST(vfx_info_count_falls_back_to_rows) {
    // numEmitters absent: the emitterDefs row count is used. The key is renamed so the JSON stays valid.
    std::string text = kLooping;
    const std::string field = "\"numEmitters\":1";
    const auto at = text.find(field);
    CHECK(at != std::string::npos);
    text.replace(at, field.size(), "\"unusedKey\":1");
    VfxInfo info;
    CHECK(parseVfxExport(text, info, nullptr));
    CHECK(info.emitterCount == 1);
}

TEST(vfx_info_rejects_other_inputs) {
    VfxInfo info;
    std::string err;
    CHECK(!parseVfxExport("not json at all", info, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseVfxExport("[1,2,3]", info, &err));
    CHECK(!parseVfxExport(R"({"format":"other-format","root_type":"ParticleSystemDef","asset":{"fields":{}}})", info, &err));
    CHECK(!err.empty());
    // A material export has the same envelope but a different root type.
    CHECK(!parseVfxExport(R"({"format":"mw19-asset-json-v1","root_type":"Material","asset":{"fields":{}}})", info, &err));
    CHECK(err.find("ParticleSystemDef") != std::string::npos);
    // ParticleSystemDef root without a name.
    CHECK(!parseVfxExport(R"({"format":"mw19-asset-json","root_type":"ParticleSystemDef","asset":{"fields":{"numEmitters":0}}})",
                          info, &err));
    CHECK(!err.empty());
}

TEST(vfx_info_read_file) {
    const std::string path = writeTemp("vfx_info_one_shot.asset.json", kOneShot);
    VfxInfo info;
    std::string err;
    CHECK(readVfxExport(path, info, &err));
    CHECK(info.name == "vfx/test/one_shot.vfx" && info.emitterCount == 2);

    VfxInfo missing;
    CHECK(!readVfxExport(iw8r::test::tmpDir() + "/does_not_exist.asset.json", missing, &err));
    CHECK(!err.empty());
}

// Optional: a real export from an ACTS run (the x64-zt stock cache holds 440 vfx files). Set IW8R_VFX_SAMPLE to the file,
// for example .../mw19replay/mp_shipment/assets/vfx/vfx/core/mp/core/vfx_hp_chev_grey.vfx.33.asset.json.
TEST(vfx_info_real_export_optional) {
    const char* sample = std::getenv("IW8R_VFX_SAMPLE");
    if (!sample || !*sample) {
        std::printf("  (IW8R_VFX_SAMPLE not set: real-export check skipped)\n");
        return;
    }
    VfxInfo info;
    std::string err;
    CHECK(readVfxExport(sample, info, &err));
    CHECK(err.empty());
    CHECK(info.name.rfind("vfx/", 0) == 0);
    CHECK(info.emitterCount > 0);
    CHECK(info.lifeSeconds != 0);
    std::printf("  real export: %s emitters=%d looping=%d life=%.2f materials=%zu models=%zu sounds=%zu children=%zu\n",
                info.name.c_str(), info.emitterCount, info.looping ? 1 : 0, info.lifeSeconds, info.materials.size(),
                info.models.size(), info.sounds.size(), info.childEffects.size());
}
