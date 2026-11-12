import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/compile_iw8.h', [
 ('''bool compileIw8Extraction(const MapFile& map, const std::string& outDir, const CompileOptions& opt, CompileReport& report,
''', '''// 11.4 (research 88): the brushes and patches added to a stock map. A stock map's world is compiled data, so they are
// built as custom models: one per entity that holds them (the worldspawn, each func_group), as a Y-up OBJ text (the
// custom model format: x, z, -y; usemtl = the editor material) centred on `centre`. Brushes whose faces are all tool
// materials (caulk, clip, nodraw, ...) draw nothing and are left out; other brush entities (triggers, brush models)
// are not geometry and are left out too. `notes` says what was left out.
struct StockOverlayModel {
    size_t entity = 0;   // index into map.entities
    std::string obj;     // Y-up OBJ text
    Vec3 centre;         // world position of the model origin
    size_t triangles = 0;
    std::set<std::string> materials;
};
std::vector<StockOverlayModel> stockOverlayModels(const MapFile& map, int patchSubdiv, std::vector<std::string>* notes = nullptr);

bool compileIw8Extraction(const MapFile& map, const std::string& outDir, const CompileOptions& opt, CompileReport& report,
'''),
])
patch('src/core/compile_iw8.cpp', [
 ('''// Models that move without scripting (iw8r_spin / iw8r_bob / iw8r_lift, map_script.h) must be live script_models with
''', r'''std::vector<StockOverlayModel> stockOverlayModels(const MapFile& map, int patchSubdiv, std::vector<std::string>* notes) {
    std::vector<StockOverlayModel> out;
    std::vector<std::string> warn;
    size_t otherEntities = 0, toolOnly = 0;
    for (size_t ei = 0; ei < map.entities.size(); ++ei) {
        const Entity& e = map.entities[ei];
        size_t prims = 0;
        for (const auto& it : e.items) prims += it.prim() ? 1 : 0;
        if (!prims) continue;
        const std::string cls = lower(e.classname());
        if (cls != "worldspawn" && cls != "func_group") {
            ++otherEntities;
            continue;
        }
        Gather g;
        g.subdiv = std::clamp(patchSubdiv, 1, 32);
        g.warnings = &warn;
        size_t group = 0;
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            const size_t before = g.chunks.size();
            if (p->kind == PrimKind::Brush) gatherBrush(*p, ei, group++, g);
            else if (p->kind == PrimKind::Patch) gatherPatch(*p, ei, group++, g);
            if (g.chunks.size() == before) ++toolOnly;
        }
        AABB box;
        for (const Chunk& c : g.chunks)
            for (const RenderVertex& v : c.verts) box.add(v.pos);
        if (g.chunks.empty() || !box.valid()) continue;
        StockOverlayModel m;
        m.entity = ei;
        m.centre = box.center();
        std::string& o = m.obj;
        o = "# IW8 Radiant: brushes added to a stock map (entity " + std::to_string(ei) + ")\n";
        size_t base = 1;
        std::map<std::string, std::vector<const Chunk*>> byMaterial;
        for (const Chunk& c : g.chunks) byMaterial[c.editorMaterial].push_back(&c);
        for (const auto& [material, chunks] : byMaterial) {
            m.materials.insert(material);
            o += "g " + (material.empty() ? std::string("default") : material) + "\nusemtl " + material + "\n";
            for (const Chunk* c : chunks) {
                for (const RenderVertex& v : c->verts) {
                    const Vec3 p = v.pos - m.centre;
                    o += "v " + formatNumber(p.x) + " " + formatNumber(p.z) + " " + formatNumber(-p.y) + "\n";
                    o += "vn " + formatNumber(v.normal.x) + " " + formatNumber(v.normal.z) + " " + formatNumber(-v.normal.y) + "\n";
                    o += "vt " + formatNumber(v.u) + " " + formatNumber(v.v) + "\n";
                }
                for (size_t i = 0; i + 2 < c->idx.size(); i += 3) {
                    o += "f";
                    for (int k = 0; k < 3; ++k) {
                        const std::string n = std::to_string(base + c->idx[i + size_t(k)]);
                        o += " " + n + "/" + n + "/" + n;
                    }
                    o += "\\n";
                    ++m.triangles;
                }
                base += c->verts.size();
            }
        }
        if (m.triangles) out.push_back(std::move(m));
    }
    if (notes) {
        if (toolOnly) notes->push_back(std::to_string(toolOnly) + " brush(es) of tool materials only (caulk, clip, nodraw, ...) draw nothing and are not built");
        if (otherEntities)
            notes->push_back(std::to_string(otherEntities) + " brush entit" + (otherEntities == 1 ? "y" : "ies") +
                             " other than the worldspawn and func_group (triggers, brush models) are not built into a stock map");
    }
    return out;
}

// Models that move without scripting (iw8r_spin / iw8r_bob / iw8r_lift, map_script.h) must be live script_models with
'''),
])
print('ok')
