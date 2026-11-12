import io, sys
root = sys.argv[1]

def patch(rel, pairs):
    p = root + '/' + rel
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (rel, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/games/iw8/assets/replay_dynentity.h', [
 ('''// One scriptable instance of the map (MapEnts ScriptableMapEnts.mapInstances) with the numbers of its definition the
''', '''// One loot point of the map (MapEnts.spawnGroupLoot.points; IW8 Radiant research 87 section 2.4 and 89): a place where
// the battle royale scripts may put loot. A custom map has no clusters and no zones (stock mp_backlot2 has no zones,
// stock mp_br_tut no clusters): every point is a free point (0xFFFD) or a loot cache point (0xFFFB).
struct MapLootPoint
{
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};
    bool cache = false;
};

// One scriptable instance of the map (MapEnts ScriptableMapEnts.mapInstances) with the numbers of its definition the
'''),
])
patch('src/games/iw8/zone/map_zone.h', [
 ('''struct MapScriptables
{
    std::vector<ScriptableInstance> instances;
''', '''struct MapScriptables
{
    // Loot points (spawnGroupLoot). Each takes one scriptable slot at run time: the loot count and the three server
    // counts include them (research 86: on every stock map with loot).
    std::vector<MapLootPoint> loot;
    std::vector<ScriptableInstance> instances;
'''),
 ('''    stamp64(me, 0x2C0, PTR_FOLLOWS);
    const bool hasScriptables = scriptables && !scriptables->instances.empty();
''', '''    stamp64(me, 0x2C0, PTR_FOLLOWS);
    const bool hasScriptables = scriptables && !scriptables->instances.empty();
    const uint32_t lootCount = scriptables ? static_cast<uint32_t>(scriptables->loot.size()) : 0u;
    if (lootCount > 65000)
        throw std::runtime_error("a map takes at most 65000 loot points");
    if (lootCount)
    {
        // SpawnGroup at 0x2D0 (after ScriptableMapEnts, which ends with ffMemCost at 0x2C8): pointCount, points; no
        // clusters, no zones.
        stamp16(me, 0x2D0, static_cast<uint16_t>(lootCount));
        stamp64(me, 0x2D8, PTR_FOLLOWS);
        stamp32(me, 0x204, lootCount); // lootInstanceCount
        stamp32(me, 0x208, lootCount); // the three server counts: the instances are added below
        stamp32(me, 0x20C, lootCount);
        stamp32(me, 0x210, lootCount);
        stamp32(me, 0x2C8, 216);
    }
'''),
 ('''        stamp32(me, 0x208, server);                                  // serverInstanceCount
        stamp32(me, 0x20C, fixedParts);                              // serverFixedPartCount
        stamp32(me, 0x210, usableParts);                             // serverUsablePartCount
''', '''        stamp32(me, 0x208, server + lootCount);                      // serverInstanceCount
        stamp32(me, 0x20C, fixedParts + lootCount);                  // serverFixedPartCount
        stamp32(me, 0x210, usableParts + lootCount);                 // serverUsablePartCount
'''),
 ('''    for (unsigned k = 0; k < 3; ++k)
        zb.writeT<float>(100000.f);
    // MapEnts.audioPASpeakers: after the scriptables (and the empty spawn-group,
''', '''    for (unsigned k = 0; k < 3; ++k)
        zb.writeT<float>(100000.f);
    // MapEnts.spawnGroupLoot.points: SpawnGroupPoint, 28 bytes (origin, angles, u16 cluster, u16 zone), align 4.
    if (lootCount)
    {
        zb.align(3);
        for (const auto &point : scriptables->loot)
        {
            for (const float value : point.origin)
                zb.writeT(value);
            for (const float value : point.angles)
                zb.writeT(value);
            zb.writeT<uint16_t>(point.cache ? 0xFFFBu : 0xFFFDu);
            zb.writeT<uint16_t>(0);
        }
    }
    // MapEnts.audioPASpeakers: after the scriptables (and the empty spawn-group,
'''),
 ('''    const std::vector<ScriptableInstance> *scriptableInstances = nullptr)
{
    MapWorldStrings result;
''', '''    const std::vector<ScriptableInstance> *scriptableInstances = nullptr,
    const std::vector<MapLootPoint> *lootPoints = nullptr)
{
    MapWorldStrings result;
'''),
 ('''    const auto &strings = result.spawns->strings;
    if (strings.size() > 2)
''', '''    if (lootPoints && !lootPoints->empty())
    {
        if (!result.scriptables)
            result.scriptables = std::make_shared<MapScriptables>();
        result.scriptables->loot = *lootPoints;
    }
    const auto &strings = result.spawns->strings;
    if (strings.size() > 2)
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.h', [
 ('''    std::vector<iw8::ScriptableInstance> scriptableInstances;
''', '''    std::vector<iw8::ScriptableInstance> scriptableInstances;
    // Loot points of the map (collision JSON "loot_points").
    std::vector<iw8::MapLootPoint> lootPoints;
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.cpp', [
 ('''    , scriptableInstances(std::move(other.scriptableInstances))
''', '''    , scriptableInstances(std::move(other.scriptableInstances))
    , lootPoints(std::move(other.lootPoints))
'''),
 ('''        scriptableInstances = std::move(other.scriptableInstances);
''', '''        scriptableInstances = std::move(other.scriptableInstances);
        lootPoints = std::move(other.lootPoints);
'''),
 ('''// "scriptable_instances" of the collision JSON (IW8 Radiant research 86): one object per placed scriptable with the
''', '''// "loot_points" of the collision JSON (IW8 Radiant research 89): [{"origin": [x, y, z], "angles": [p, y, r],
// "cache": true | false}], the places where the battle royale scripts may put loot ("cache": a loot cache point).
std::vector<iw8::MapLootPoint> ReadLootPoints(const Json &collision)
{
    std::vector<iw8::MapLootPoint> result;
    if (!collision.contains("loot_points"))
        return result;
    const auto &list = collision.at("loot_points");
    if (!list.is_array() || list.size() > 65000)
        throw std::runtime_error("loot_points must be a list of at most 65000 points");
    for (std::size_t index = 0; index < list.size(); ++index)
    {
        const auto &entry = list[index];
        const auto fail = [&](const std::string &what) {
            throw std::runtime_error("loot_points[" + std::to_string(index) + "]: " + what);
        };
        if (!entry.is_object())
            fail("not an object");
        iw8::MapLootPoint point;
        const auto vector = [&](const char *key, std::array<float, 3> &out, const bool required) {
            if (!entry.contains(key))
            {
                if (required)
                    fail(std::string("no ") + key);
                return;
            }
            const auto &node = entry.at(key);
            if (!node.is_array() || node.size() != 3)
                fail(std::string(key) + " is not three numbers");
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                if (!node[axis].is_number())
                    fail(std::string(key) + " is not three numbers");
                out[axis] = node[axis].get<float>();
                if (!std::isfinite(out[axis]))
                    fail(std::string(key) + " is not finite");
            }
        };
        vector("origin", point.origin, true);
        vector("angles", point.angles, false);
        if (entry.contains("cache"))
        {
            if (!entry.at("cache").is_boolean())
                fail("cache is not true or false");
            point.cache = entry.at("cache").get<bool>();
        }
        result.push_back(point);
    }
    return result;
}

// "scriptable_instances" of the collision JSON (IW8 Radiant research 86): one object per placed scriptable with the
'''),
 ('''    result.scriptableInstances = ReadScriptableInstances(collision);
''', '''    result.lootPoints = ReadLootPoints(collision);
    if (!result.lootPoints.empty())
        zt::info("iw3: %zu loot points", result.lootPoints.size());
    result.scriptableInstances = ReadScriptableInstances(collision);
'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''            prepared ? &prepared->scriptableInstances : nullptr);
        replayrender::RegisterMapMaterials(writer, techsets, renderMesh);
''', '''            prepared ? &prepared->scriptableInstances : nullptr,
            prepared ? &prepared->lootPoints : nullptr);
        replayrender::RegisterMapMaterials(writer, techsets, renderMesh);
'''),
])
print('ok')
