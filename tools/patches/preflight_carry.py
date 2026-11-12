import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/build_preflight.cpp', [
 ('''    // Models: xmodel\\<name>.json (the table) and .replay.json (the geometry, with the surface materials).
    for (const auto& [model, where] : modelWhere) {
''', '''    // Models the converter carries from a stock zone (collision json carry_assets, e.g. the model of a vehicle spawn)
    // are not files of the extraction: the converter adds them when it writes the zone.
    {
        std::string colText, cerr;
        if (readFile((mapDir / (mapId + ".d3dbsp.replay-collision.json")).string(), colText))
            if (const std::optional<json::Value> col = json::parse(colText, &cerr))
                if (const json::Value* carry = col->get("carry_assets"); carry && carry->type == json::Value::Type::Array)
                    for (const json::Value& entry : carry->arr) {
                        const json::Value* type = entry.get("type");
                        const json::Value* name = entry.get("name");
                        if (type && type->string() && *type->string() == "xmodel" && name && name->string())
                            for (auto it = modelWhere.begin(); it != modelWhere.end();)
                                it = it->first == *name->string() ? modelWhere.erase(it) : std::next(it);
                    }
    }
    // Models: xmodel\\<name>.json (the table) and .replay.json (the geometry, with the surface materials).
    for (const auto& [model, where] : modelWhere) {
'''),
])
print('ok')
