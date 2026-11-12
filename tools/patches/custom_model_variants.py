import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/core/compile_iw8.h', [
(r'''    const std::map<std::string, ImportedModel>* stockModels = nullptr;''',
 r'''    const std::map<std::string, ImportedModel>* stockModels = nullptr;
    // Re-skinned copies of the mapper's own models (model folders: shape models, imported OBJ / glTF), made by the
    // iw8r_material pre-pass: variant name -> the model it is a copy of and the materials to put on it. The variant is
    // built from the base model's file with the materials replaced. Not owned; must outlive the compile.
    struct CustomModelVariant {
        std::string base;                           // the model in the model folders
        std::string material;                       // every surface, unless a slot names it ("" = slots only)
        std::map<std::string, std::string> slots;   // lower-case surface material -> replacement
    };
    const std::map<std::string, CustomModelVariant>* customModelVariants = nullptr;'''),
])

edit('src/core/compile_iw8.cpp', [
(r'''    std::map<std::string, std::string> customModelPaths;  // model name -> .obj''',
 r'''    std::map<std::string, std::string> customModelPaths;  // model name -> .obj
    // Re-skinned copies of custom models (iw8r_material on a shape or imported model): the variant is read from its
    // base model's file; its materials are replaced when it is imported below.
    if (opt.customModelVariants)
        for (const auto& [variant, v] : *opt.customModelVariants)
            if (const std::string obj = findCustomModel(opt.modelRoots, v.base); !obj.empty()) customModelPaths[variant] = obj;'''),
(r'''        im.name = modelName;  // the xmodel name is the entity's model key
        for (auto& mesh : im.meshes) mesh.material = materialFor(mesh.material);''',
 r'''        im.name = modelName;  // the xmodel name is the entity's model key
        if (opt.customModelVariants)
            if (const auto v = opt.customModelVariants->find(modelName); v != opt.customModelVariants->end())
                for (auto& mesh : im.meshes) {
                    if (const auto s = v->second.slots.find(lower(mesh.material)); s != v->second.slots.end()) mesh.material = s->second;
                    else if (!v->second.material.empty()) mesh.material = v->second.material;
                }
        for (auto& mesh : im.meshes) mesh.material = materialFor(mesh.material);'''),
(r'''    MapFile work = mapIn;
    std::map<std::string, ImportedModel> models;
    if (opt.stockModels) models = *opt.stockModels;
    size_t skinned = 0;''',
 r'''    MapFile work = mapIn;
    std::map<std::string, ImportedModel> models;
    if (opt.stockModels) models = *opt.stockModels;
    std::map<std::string, CompileOptions::CustomModelVariant> customVariants;
    if (opt.customModelVariants) customVariants = *opt.customModelVariants;
    size_t skinned = 0;'''),
(r'''        const auto found = opt.stockModels ? opt.stockModels->find(base) : models.end();
        if (!opt.stockModels || found == opt.stockModels->end() || found->second.meshes.empty()) {
            notes.push_back("model '" + base + "' iw8r_material '" + material +
                            "': only stock models from the model cache can take a material; it keeps its own");
            continue;
        }
        const auto slots = slotsOf(e);
        std::string sig = material;
        for (const auto& [from, to] : slots) sig += "|" + from + ">" + to;
        const std::string variant = base + "_iw8rmat_" + customNameHash(sig);''',
 r'''        const auto found = opt.stockModels ? opt.stockModels->find(base) : models.end();
        const auto slots = slotsOf(e);
        std::string sig = material;
        for (const auto& [from, to] : slots) sig += "|" + from + ">" + to;
        if (!opt.stockModels || found == opt.stockModels->end() || found->second.meshes.empty()) {
            // The mapper's own model (a shape model or an imported file, owner report 2026-11-11: a staircase of
            // shape cubes): a copy read from the same file, with the materials replaced.
            const std::string hash = customNameHash(sig);
            std::string variant = base + "_m" + hash;
            if (variant.size() > 64) variant = base.substr(0, 64 - 2 - hash.size()) + "_m" + hash;
            if (!isCustomModelName(base) || findCustomModel(opt.modelRoots, base).empty() || !isCustomModelName(variant)) {
                notes.push_back("model '" + base + "' iw8r_material '" + material +
                                "': not a stock model from the model cache and not a model in the model folders; it keeps its own");
                continue;
            }
            customVariants[variant] = CompileOptions::CustomModelVariant{base, material, slots};
            for (auto& it : e.items)
                if (KeyValueLine* kv = it.kv(); kv && kv->name() == "model") kv->set(variant);
            ++skinned;
            continue;
        }
        const std::string variant = base + "_iw8rmat_" + customNameHash(sig);'''),
(r'''    CompileOptions opt2 = opt;
    if (skinned) opt2.stockModels = &models;
    const bool ok = compileIw8ExtractionFlat(work, outDir, opt2, rep, err);
    if (skinned)
        rep.warnings.insert(rep.warnings.begin(), std::to_string(skinned) + " model(s) built with their iw8r_material on every surface");''',
 r'''    CompileOptions opt2 = opt;
    if (skinned && !models.empty()) opt2.stockModels = &models;
    if (!customVariants.empty()) opt2.customModelVariants = &customVariants;
    const bool ok = compileIw8ExtractionFlat(work, outDir, opt2, rep, err);
    if (skinned)
        rep.warnings.insert(rep.warnings.begin(), std::to_string(skinned) + " model(s) built with their iw8r_material on every surface");'''),
])
print('ok')
