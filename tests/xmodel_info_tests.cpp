// Model inspector: reads ACTS "xmodel" exports. The real export is tests/data/xmodel_probe/body_pilot_helicopter_british.json
// (mp_hideout, 6 LODs, 123 bones with a skeleton) and its script_strings.json (trimmed to the ids it uses). Other checks use
// inline synthetic models.
#include "xmodel_info.h"
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b, double eps = 1e-3) {
    return std::fabs(a - b) <= eps;
}

// <repo>/tests/data/xmodel_probe/<name>, found by walking up from the working directory.
std::string fixturePath(const std::string& name) {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "xmodel_probe" / name;
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

bool readText(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    text = s.str();
    return true;
}

// One stock rig: 2 LODs (numLods 2 of 3 slots), collLod 1 (in range), no parentList and no baseMat, so a single root bone at
// the origin. The first two materials share a name with and without the ',' marker.
const char* kRigid = R"JSON({"format":"mw19-asset-json","format_version":1,"root_type":"XModel","asset":{"type":"XModel","fields":{
 "name":{"type":"const char","string":"synthetic_rigid"},
 "numLods":2,"collLod":1,
 "lodInfo":[{"modelSurfsStaging":{"name":"synth_lod0"},"dist":100.0,"numsurfs":1},
            {"modelSurfsStaging":null,"dist":400.0,"numsurfs":2},
            {"modelSurfsStaging":{"name":"unused"},"dist":1000000.0,"numsurfs":0}],
 "radius":5.0,"bounds":{"midPoint":{"v":[1.0,2.0,3.0]},"halfSize":{"v":[4.0,5.0,6.0]}},
 "boneNames":{"values":[{"value":3760}]},"parentList":null,"trans":null,"quats":null,"baseMat":null,
 "materialHandles":{"values":[{"name":",mco/one"},{"name":",mco/one"},{"name":"mco/two"}]}
}}})JSON";

}  // namespace

TEST(xmodel_info_real_export_lods_and_bounds) {
    const std::string path = fixturePath("body_pilot_helicopter_british.json");
    CHECK(!path.empty());
    XModelInfo m;
    std::string err;
    CHECK(readXModelExport(path, m, &err));
    CHECK(err.empty());
    CHECK(m.name == "body_pilot_helicopter_british");
    CHECK(m.lods.size() == 6);
    const double dists[] = {110, 200, 350, 700, 1200, 10000};
    const int counts[] = {15, 15, 15, 15, 14, 12};
    for (size_t i = 0; i + 1 < m.lods.size(); ++i) CHECK(m.lods[i].distance < m.lods[i + 1].distance);
    for (size_t i = 0; i < m.lods.size() && i < 6; ++i) {
        CHECK(near(m.lods[i].distance, dists[i], 1e-6));
        CHECK(m.lods[i].surfCount == counts[i]);
    }
    CHECK(m.lods[0].surfs == "body_pilot_helicopter_british_lod0207ccc86c5v000000050");
    // collLod is 8 in the export, which is past the six LODs, so it is reported as no collision LOD.
    CHECK(m.collLod == -1);
    CHECK(near(m.radius, 64.2124, 1e-3));
    for (int i = 0; i < 3; ++i) {
        CHECK(near(m.bounds[i], -96.3187, 1e-3));
        CHECK(near(m.bounds[i + 3], 96.3187, 1e-3));
    }
    CHECK(!m.materials.empty());
    CHECK(std::find(m.materials.begin(), m.materials.end(), "mco/flight_suit_pants") != m.materials.end());
    for (const auto& s : m.materials) CHECK(!s.empty() && s[0] != ',');
}

TEST(xmodel_info_real_export_bones_and_tags) {
    const std::string path = fixturePath("body_pilot_helicopter_british.json");
    XModelInfo m;
    CHECK(readXModelExport(path, m, nullptr));
    // 73 skeleton bones + 50 client bones = 123 (boneNames), one root.
    CHECK(m.bones.size() == 123);
    CHECK(!m.bones.empty());
    CHECK(m.bones[0].name == "tag_origin");
    CHECK(m.bones[0].parent == -1);
    CHECK(m.bones[1].name == "tag_sync");
    CHECK(m.bones[1].parent == 0);
    // Bone 3 is a child of bone 2 (parentList byte 1 at k = 2: 3 - 1), and its position is the chained offset.
    CHECK(m.bones[3].parent == 2);
    CHECK(near(m.bones[2].pos[0], -1.0100, 1e-3) && near(m.bones[2].pos[1], 0.0, 1e-3) && near(m.bones[2].pos[2], 38.2, 1e-3));
    CHECK(near(m.bones[3].pos[2], 41.733, 1e-3));
    for (size_t c = 0; c < m.bones.size(); ++c) {
        const int p = m.bones[c].parent;
        CHECK(c == 0 ? p == -1 : (p >= 0 && p < static_cast<int>(c)));
    }
    int tags = 0;
    bool haveStowedBack = false;
    for (const auto& b : m.bones) {
        if (isTag(b)) ++tags;
        if (b.name == "tag_stowed_back") haveStowedBack = true;
    }
    CHECK(tags >= 20);
    CHECK(haveStowedBack);
    CHECK(isTag(m.bones[0]) && !isTag(XModelBone{"j_mainroot", -1, {0, 0, 0}}));
}

TEST(xmodel_info_composed_positions_match_basemat) {
    // Removing the baseMat key forces the parent-chain composition from the local trans and quats; it must match baseMat.
    const std::string path = fixturePath("body_pilot_helicopter_british.json");
    std::string text;
    CHECK(readText(path, text));
    XModelInfo withBase, composed;
    CHECK(parseXModelExport(text, withBase, nullptr));
    const std::string key = "\"baseMat\":";
    const auto at = text.find(key);
    CHECK(at != std::string::npos);
    text.replace(at, key.size(), "\"baseMatX\":");
    CHECK(parseXModelExport(text, composed, nullptr));
    CHECK(withBase.bones.size() == composed.bones.size());
    double worst = 0;
    for (size_t c = 0; c < withBase.bones.size() && c < composed.bones.size(); ++c) {
        for (int i = 0; i < 3; ++i) worst = std::max(worst, std::fabs(withBase.bones[c].pos[i] - composed.bones[c].pos[i]));
        CHECK(withBase.bones[c].parent == composed.bones[c].parent);
    }
    CHECK(worst < 1e-3);
}

TEST(xmodel_info_bone_names_need_the_zone_table) {
    // parseXModelExport has no zone folder, so names stay "#<id>"; readXModelExport resolves them.
    const std::string path = fixturePath("body_pilot_helicopter_british.json");
    std::string text;
    CHECK(readText(path, text));
    XModelInfo raw;
    CHECK(parseXModelExport(text, raw, nullptr));
    CHECK(!raw.bones.empty() && raw.bones[0].name == "#8");
    XModelInfo resolved;
    CHECK(readXModelExport(path, resolved, nullptr));
    CHECK(!resolved.bones.empty() && resolved.bones[0].name == "tag_origin");
}

TEST(xmodel_info_lod_for_distance) {
    XModelInfo m;
    CHECK(readXModelExport(fixturePath("body_pilot_helicopter_british.json"), m, nullptr));
    // Distances 110, 200, 350, 700, 1200, 10000: each LOD is drawn up to its distance.
    CHECK(lodForDistance(m, 0) == 0);
    CHECK(lodForDistance(m, 109.9) == 0);
    CHECK(lodForDistance(m, 110) == 1);    // at the distance the next LOD takes over
    CHECK(lodForDistance(m, 200) == 2);
    CHECK(lodForDistance(m, 699.9) == 3);
    CHECK(lodForDistance(m, 700) == 4);
    CHECK(lodForDistance(m, 9999) == 5);
    CHECK(lodForDistance(m, 1e6) == -1);   // past the last LOD: not drawn
    XModelInfo empty;
    CHECK(lodForDistance(empty, 50) == 0);
    XModelInfo open = m;
    open.lods.back().distance = 0;          // 0: no limit
    CHECK(lodForDistance(open, 1e6) == 5);
}

TEST(xmodel_info_synthetic_rigid_model) {
    XModelInfo m;
    std::string err;
    CHECK(parseXModelExport(kRigid, m, &err));
    CHECK(err.empty());
    CHECK(m.name == "synthetic_rigid");
    // numLods 2 of 3 slots: the third (unused) slot is ignored.
    CHECK(m.lods.size() == 2);
    CHECK(m.lods[0].surfs == "synth_lod0" && m.lods[0].surfCount == 1);
    CHECK(m.lods[1].surfs.empty() && m.lods[1].surfCount == 2);
    CHECK(m.collLod == 1);  // in range
    CHECK(near(m.bounds[0], -3) && near(m.bounds[1], -3) && near(m.bounds[2], -3));
    CHECK(near(m.bounds[3], 5) && near(m.bounds[4], 7) && near(m.bounds[5], 9));
    CHECK(near(m.radius, 5));
    // Materials: the ',' marker is removed and the duplicate is dropped.
    CHECK(m.materials.size() == 2 && m.materials[0] == "mco/one" && m.materials[1] == "mco/two");
    // One root bone, at the origin, with the id unresolved.
    CHECK(m.bones.size() == 1 && m.bones[0].parent == -1 && m.bones[0].name == "#3760");
    CHECK(near(m.bones[0].pos[0], 0) && near(m.bones[0].pos[1], 0) && near(m.bones[0].pos[2], 0));
    CHECK(lodForDistance(m, 99) == 0 && lodForDistance(m, 100) == 1 && lodForDistance(m, 399) == 1 && lodForDistance(m, 400) == -1);
}

TEST(xmodel_info_rejects_malformed_input) {
    XModelInfo m;
    std::string err;
    CHECK(!parseXModelExport("not json at all", m, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseXModelExport("[1,2,3]", m, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseXModelExport(R"({"format":"other-format","root_type":"XModel","asset":{"fields":{}}})", m, &err));
    CHECK(!err.empty());
    // A vfx export has the same envelope but a different root type.
    err.clear();
    CHECK(!parseXModelExport(R"({"format":"mw19-asset-json","root_type":"ParticleSystemDef","asset":{"fields":{}}})", m, &err));
    CHECK(err.find("XModel") != std::string::npos);
    // XModel root without a name.
    err.clear();
    CHECK(!parseXModelExport(R"({"format":"mw19-asset-json","root_type":"XModel","asset":{"fields":{"numLods":0}}})", m, &err));
    CHECK(!err.empty());

    // Bone parent offset 0 would make a bone its own parent.
    const char* selfParent = R"JSON({"format":"mw19-asset-json","root_type":"XModel","asset":{"fields":{
        "name":{"string":"bad_parent"},"boneNames":{"values":[{"value":1},{"value":2}]},
        "parentList":{"bytes":"00"}}}})JSON";
    err.clear();
    CHECK(!parseXModelExport(selfParent, m, &err));
    CHECK(err.find("parent") != std::string::npos);

    // Parent offset that reaches before bone 0 is also invalid.
    const char* tooFar = R"JSON({"format":"mw19-asset-json","root_type":"XModel","asset":{"fields":{
        "name":{"string":"bad_parent2"},"boneNames":{"values":[{"value":1},{"value":2}]},
        "parentList":{"bytes":"05"}}}})JSON";
    CHECK(!parseXModelExport(tooFar, m, nullptr));

    // Odd-length or non-hex parent bytes.
    CHECK(!parseXModelExport(R"({"format":"mw19-asset-json","root_type":"XModel","asset":{"fields":{"name":{"string":"x"},"boneNames":{"values":[{"value":1},{"value":2}]},"parentList":{"bytes":"0"}}}})", m, nullptr));
    CHECK(!parseXModelExport(R"({"format":"mw19-asset-json","root_type":"XModel","asset":{"fields":{"name":{"string":"x"},"boneNames":{"values":[{"value":1},{"value":2}]},"parentList":{"bytes":"zz"}}}})", m, nullptr));

    // LOD distances must not decrease.
    const char* unsorted = R"JSON({"format":"mw19-asset-json","root_type":"XModel","asset":{"fields":{
        "name":{"string":"unsorted"},"numLods":2,"lodInfo":[{"dist":400.0,"numsurfs":1},{"dist":100.0,"numsurfs":1}]}}})JSON";
    err.clear();
    CHECK(!parseXModelExport(unsorted, m, &err));
    CHECK(err.find("ascending") != std::string::npos);

    // A missing file is reported, not a crash.
    XModelInfo missing;
    err.clear();
    CHECK(!readXModelExport("does_not_exist.json", missing, &err));
    CHECK(!err.empty());
}

// 3.18: per-LOD surface materials (materialHandles[surfIndex + i]) and the preview LOD with lod_scale.
TEST(xmodel_info_lod_materials_and_preview) {
    const std::string path = fixturePath("body_pilot_helicopter_british.json");
    XModelInfo m;
    std::string err;
    CHECK(!path.empty() && readXModelExport(path, m, &err));
    if (m.lods.size() != 6) return;
    const int first[] = {0, 15, 30, 45, 60, 74};
    for (size_t i = 0; i < 6; ++i) {
        CHECK(m.lods[i].surfIndex == first[i]);
        CHECK(m.lods[i].materials.size() == size_t(m.lods[i].surfCount));
        for (const auto& s : m.lods[i].materials) CHECK(!s.empty() && s[0] != ',');
    }
    CHECK(previewLodFor(m, 100, 1) == 0);
    CHECK(previewLodFor(m, 150, 1) == 1);
    CHECK(previewLodFor(m, 150, 2) == 0);   // lod_scale 2 doubles every switch distance
    CHECK(previewLodFor(m, 150, 0) == 1);   // 0 counts as 1
    CHECK(previewLodFor(m, 20000, 1) == -1);
}
