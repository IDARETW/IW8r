import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/stock_smodels.h'
patch(H, '''    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags
};''', '''    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags
    // The map's material for each surface of the model (every LOD, in the model's surface order; XModelLod::surfIndex
    // and surfCount give a LOD's range): smodels.smodelSurfMatIndirection from models[].firstStaticModelSurfaceIndex,
    // smodelExpansionData[].numSurfs entries, into smodels.materials (leading ',' dropped). The map chooses them per
    // static model (a material skin): Speedball's containers use "..._second" / "_blue" materials, not the model's own
    // (whose placeholder tint drew them pink). Null when the export lacks the tables; shared by a model's placements.
    std::shared_ptr<const std::vector<std::string>> surfaceMaterials;
};''')
s = io.open(H, encoding='utf-8', newline='').read()
if '#include <memory>' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#include <array>', '#include <array>' + nl + '#include <memory>', 1)
    assert '#include <memory>' in s
    io.open(H, 'w', encoding='utf-8', newline='').write(s)

C = 'src/core/stock_smodels.cpp'
patch(C, '''    std::vector<std::string> names;
    std::vector<uint32_t> modelFlags;
    forEachRecord(models, [&](const Value& m) {
        const Value* mm = m.get("model");
        const std::string* n = mm && mm->get("name") ? mm->get("name")->string() : nullptr;
        names.push_back(n ? *n : std::string());
        const Value* fl = m.get("flags");  // {"value": n, "name": "..."} or a plain number
        const Value* fv = fl && fl->type == Value::Type::Object ? fl->get("value") : fl;
        modelFlags.push_back(uint32_t(num(fv)));
    });
    text.close();  // everything needed is decoded''', '''    std::vector<std::string> names;
    std::vector<uint32_t> modelFlags;
    std::vector<int64_t> firstSurf;
    forEachRecord(models, [&](const Value& m) {
        const Value* mm = m.get("model");
        const std::string* n = mm && mm->get("name") ? mm->get("name")->string() : nullptr;
        names.push_back(n ? *n : std::string());
        const Value* fl = m.get("flags");  // {"value": n, "name": "..."} or a plain number
        const Value* fv = fl && fl->type == Value::Type::Object ? fl->get("value") : fl;
        modelFlags.push_back(uint32_t(num(fv)));
        const Value* fs0 = m.get("firstStaticModelSurfaceIndex");
        firstSurf.push_back(fs0 ? int64_t(num(fs0)) : -1);
    });
    // The map's materials per static model surface (the material skins; see surfaceMaterials in the header).
    std::vector<std::shared_ptr<const std::vector<std::string>>> skins(names.size());
    {
        std::vector<int64_t> numSurfs;
        forEachRecord(sm.get("smodelExpansionData").get("values"), [&](const Value& e) {
            const Value* ns = e.get("numSurfs");
            numSurfs.push_back(ns ? int64_t(num(ns)) : -1);
        });
        std::vector<int64_t> matOf;
        forEachRecord(sm.get("smodelSurfMatIndirection").get("values"), [&](const Value& r) {
            const Value* mi = r.get("smodelMaterialIndex");
            matOf.push_back(mi ? int64_t(num(mi)) : -1);
        });
        std::vector<std::string> mats;
        json::View matNode = sm.get("materials");
        if (matNode.get("values").valid()) matNode = matNode.get("values");
        forEachRecord(matNode, [&](const Value& m) {
            const std::string* n = m.get("name") ? m.get("name")->string() : nullptr;
            std::string name = n ? *n : std::string();
            if (!name.empty() && name[0] == ',') name.erase(0, 1);
            mats.push_back(std::move(name));
        });
        if (numSurfs.size() == names.size() && !matOf.empty() && !mats.empty())
            for (size_t m = 0; m < names.size(); ++m) {
                if (firstSurf[m] < 0 || numSurfs[m] <= 0 || size_t(firstSurf[m] + numSurfs[m]) > matOf.size()) continue;
                auto v = std::make_shared<std::vector<std::string>>();
                bool ok = true;
                for (int64_t k = 0; k < numSurfs[m] && ok; ++k) {
                    const int64_t mi = matOf[size_t(firstSurf[m] + k)];
                    ok = mi >= 0 && size_t(mi) < mats.size();
                    if (ok) v->push_back(mats[size_t(mi)]);
                }
                if (ok) skins[m] = std::move(v);
            }
    }
    text.close();  // everything needed is decoded''')
patch(C, '''            s.modelFlags = modelFlags[size_t(model)];''', '''            s.modelFlags = modelFlags[size_t(model)];
            s.surfaceMaterials = skins[size_t(model)];''')
print('ok')
