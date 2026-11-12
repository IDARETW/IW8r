import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/compile_iw8.cpp', [
 ('''    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
''', '''    // Loot points (13.5, research 89): `iw8r_loot_point` point entities become the collision json's loot_points, which
    // the converter writes as MapEnts.spawnGroupLoot (no clusters and no zones, as the small stock maps with loot).
    // loot_cluster "cache" makes a loot cache point; every other point is a free point.
    std::string lootJson;
    size_t lootCount = 0;
    {
        size_t tooMany = 0;
        for (const Entity* pe : points) {
            if (!isLootPointClass(pe->classname())) continue;
            if (lootCount >= kLootPointLimit) {
                ++tooMany;
                continue;
            }
            const Vec3 o = pe->origin().value_or(Vec3{});
            double a[3] = {0, 0, 0};
            if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, a)) a[0] = a[1] = a[2] = 0;
            const std::string* cv = pe->value("loot_cluster");
            const bool cache = cv && lower(*cv) == "cache";
            lootJson += std::string(lootCount ? "," : "") + "{\\"origin\\":[" + num(o.x) + "," + num(o.y) + "," + num(o.z) + "],\\"angles\\":[" +
                        num(a[0]) + "," + num(a[1]) + "," + num(a[2]) + "],\\"cache\\":" + (cache ? "true" : "false") + "}";
            ++lootCount;
        }
        if (tooMany) warn.push_back(std::to_string(tooMany) + " loot point(s) left out: a map takes at most " + std::to_string(kLootPointLimit));
        if (lootCount)
            warn.push_back(std::to_string(lootCount) + " loot point(s) written for the battle royale game type (needs a converter with loot points, "
                           "research 89; not seen in game)");
    }
    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
'''),
 ('''        if (lcls == "scriptable") continue;     // written as a collision json scriptable instance (6.14)
''', '''        if (lcls == "scriptable") continue;     // written as a collision json scriptable instance (6.14)
        if (lcls == kLootPointClass) continue;  // written as a collision json loot point (13.5)
'''),
 ('''           (scriptableCount ? ",\\"scriptable_instances\\":[" + scriptablesJson + "]" : std::string()) + "}";
''', '''           (scriptableCount ? ",\\"scriptable_instances\\":[" + scriptablesJson + "]" : std::string()) +
           (lootCount ? ",\\"loot_points\\":[" + lootJson + "]" : std::string()) + "}";
'''),
])
print('ok')
