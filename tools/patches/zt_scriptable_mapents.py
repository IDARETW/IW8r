import io, sys
root = sys.argv[1]
def load(p):
    return io.open(root + '/' + p, encoding='utf-8', newline='').read()
def save(p, s):
    io.open(root + '/' + p, 'w', encoding='utf-8', newline='').write(s)
def rep(s, a, b, count=1):
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    return s.replace(a, b)

# ---------------- map_zone.h
p = 'src/games/iw8/zone/map_zone.h'
s = load(p)
s = rep(s, '''// VIRTUAL(8). Caller is responsible for the surrounding push/pop. `ents` is the IW8 numeric-keyId
// entityString (NUL added here).
inline void emitMapEntsBody(ZoneBuffer &zb, const char *assetName, const std::string &ents,
                            const ReplaySpawns *spawns = nullptr,
                            const havok::BakeResult *collision = nullptr,
                            const uint64_t edgeListAlias = PTR_NULL,
                            const DynamicEntityCounts &dynamic = {},
                            const ClientTriggerSet *clientTriggers = nullptr)
{
''', '''// The map's scriptable instances with their interned script strings (classname, target, targetname, script_linkname,
// script_linkto, script_noteworthy: the ScriptableInstance field order) and, at emit time, the alias of each
// instance's definition reference.
struct MapScriptables
{
    std::vector<ScriptableInstance> instances;
    std::vector<std::array<uint32_t, 6>> strings;
    std::vector<uint64_t> definitionAliases;
};

// VIRTUAL(8). Caller is responsible for the surrounding push/pop. `ents` is the IW8 numeric-keyId
// entityString (NUL added here).
inline void emitMapEntsBody(ZoneBuffer &zb, const char *assetName, const std::string &ents,
                            const ReplaySpawns *spawns = nullptr,
                            const havok::BakeResult *collision = nullptr,
                            const uint64_t edgeListAlias = PTR_NULL,
                            const DynamicEntityCounts &dynamic = {},
                            const ClientTriggerSet *clientTriggers = nullptr,
                            const MapScriptables *scriptables = nullptr)
{
''')
s = rep(s, '''        if (dynamicModelCount > UINT16_MAX || dynamicBrushCount > UINT16_MAX)
            throw std::runtime_error("Replay no-spatial dynamic-entity count exceeds 16 bits");
        // Model clutter is spatial (replay_dynentity.cpp); brushes are not.
        stamp16(me, kME_dynEntNoSpatialCount, 0);
''', '''        if (dynamicModelCount > UINT16_MAX || dynamicBrushCount > UINT16_MAX)
            throw std::runtime_error("Replay no-spatial dynamic-entity count exceeds 16 bits");
        // Model clutter and scriptable instance entries are spatial (replay_dynentity.cpp); scriptable physics
        // entries and brushes are not.
        stamp16(me, kME_dynEntNoSpatialCount, static_cast<uint16_t>(dynamic.noSpatialModels));
''')
s = rep(s, '''    stamp64(me, 0x2C0, PTR_FOLLOWS);
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
''', '''    stamp64(me, 0x2C0, PTR_FOLLOWS);
    const bool hasScriptables = scriptables && !scriptables->instances.empty();
    if (hasScriptables)
    {
        // ScriptableMapEnts at 0x1F8. The counts follow the definitions of the instances as in stock maps (IW8 Radiant
        // research 86, checked on mp_shipment and mp_backlot2): server instances are those whose definition has
        // server-instanced parts, the fixed and usable part counts are the sums over the instances, and each physics
        // limit is the number of door-like instances. No loot and no reserved instances.
        const auto &instances = scriptables->instances;
        if (scriptables->strings.size() != instances.size() || scriptables->definitionAliases.size() != instances.size())
            throw std::runtime_error("Replay scriptable instance tables disagree");
        uint32_t server = 0, fixedParts = 0, usableParts = 0, limits = 0;
        for (const auto &instance : instances)
        {
            server += instance.serverInstancedParts ? 1 : 0;
            fixedParts += instance.serverInstancedParts;
            usableParts += instance.usableParts;
            limits += instance.physicsLimit ? 1 : 0;
        }
        stamp32(me, 0x1FC, static_cast<uint32_t>(instances.size())); // mapInstanceCount
        stamp32(me, 0x208, server);                                  // serverInstanceCount
        stamp32(me, 0x20C, fixedParts);                              // serverFixedPartCount
        stamp32(me, 0x210, usableParts);                             // serverUsablePartCount
        for (unsigned k = 0; k < 6; ++k)
            stamp32(me, 0x214 + k * 4, limits); // ScriptablePhysicsLimits
        stamp64(me, 0x230, PTR_FOLLOWS);        // mapInstances
        // ffMemCost: stock 1240 = this 216-byte block + its two 64-entry reserved-dynent tables, which are not written.
        stamp32(me, 0x2C8, 216);
    }
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
''')
s = rep(s, '''        const bool clutter = i == 0 && dynamicModelCount;
        for (unsigned k = 0; k < 3; ++k)
            zb.writeT<float>(clutter ? dynamic.modelMins[k] : -100000.f);
        for (unsigned k = 0; k < 3; ++k)
            zb.writeT<float>(clutter ? dynamic.modelMaxs[k] : 100000.f);
        zb.align(3);
        zb.writeT<uint32_t>(0xFFFFFFFFu);
    }
    // Load_ScriptableMapEnts DBCF30 -> DDFE70 (one empty spatial leaf).
    zb.align(7);
''', '''        // Population 1 is the sparse occluders (door instance entries), bounded the same way.
        const bool clutter = i == 0 && dynamic.radialModels;
        const bool occluders = i == 1 && dynamic.occluderModels;
        const auto &mins = occluders ? dynamic.occluderMins : dynamic.modelMins;
        const auto &maxs = occluders ? dynamic.occluderMaxs : dynamic.modelMaxs;
        for (unsigned k = 0; k < 3; ++k)
            zb.writeT<float>(clutter || occluders ? mins[k] : -100000.f);
        for (unsigned k = 0; k < 3; ++k)
            zb.writeT<float>(clutter || occluders ? maxs[k] : 100000.f);
        zb.align(3);
        zb.writeT<uint32_t>(0xFFFFFFFFu);
    }
    // Load_ScriptableMapEnts DBCF30: the instance records (72 bytes, align 8; the definition is an alias of the
    // name-only reference registered before this asset, the six names are script-string indices), the two reserved
    // dynent tables (none), then DDFE70, the spatial tree (one leaf that holds every instance).
    if (hasScriptables)
    {
        zb.align(7);
        for (size_t index = 0; index < scriptables->instances.size(); ++index)
        {
            const auto &instance = scriptables->instances[index];
            uint8_t record[72]{};
            stamp64(record, 0, scriptables->definitionAliases[index]);
            for (unsigned k = 0; k < 3; ++k)
            {
                stampf(record, 8 + k * 4, instance.origin[k]);
                stampf(record, 20 + k * 4, instance.angles[k]);
            }
            for (unsigned k = 0; k < 6; ++k)
                stamp32(record, 32 + k * 4, scriptables->strings[index][k]);
            record[70] = instance.flags;
            zb.write(record, sizeof(record));
        }
    }
    zb.align(7);
''')
s = rep(s, '''    std::shared_ptr<staticmodelcollision::Plan> staticCollisionPlan;
    bool hasLinearLightDef = false;
    uint32_t lightDefNameStringIndex = 0;
};
inline MapWorldStrings internMapWorldStrings(
    ZoneWriter &writer, const std::string &ents, const MapSun &lighting,
    const std::vector<staticmodelcollision::Model> &collisionModels)
{
''', '''    std::shared_ptr<staticmodelcollision::Plan> staticCollisionPlan;
    bool hasLinearLightDef = false;
    uint32_t lightDefNameStringIndex = 0;
    std::shared_ptr<MapScriptables> scriptables;
};
inline MapWorldStrings internMapWorldStrings(
    ZoneWriter &writer, const std::string &ents, const MapSun &lighting,
    const std::vector<staticmodelcollision::Model> &collisionModels,
    const std::vector<ScriptableInstance> *scriptableInstances = nullptr)
{
''')
s = rep(s, '''        result.lightDefNameStringIndex = result.spawns->intern("light_point_linear");
    }
    const auto &strings = result.spawns->strings;
''', '''        result.lightDefNameStringIndex = result.spawns->intern("light_point_linear");
    }
    if (scriptableInstances && !scriptableInstances->empty())
    {
        // The class name of an instance is "scriptable_<definition>" (stock: 8005 = scriptable_com_trafficcone_01);
        // an empty name is the null string (index 0), as in stock records.
        result.scriptables = std::make_shared<MapScriptables>();
        result.scriptables->instances = *scriptableInstances;
        const auto name = [&](const std::string &text) { return text.empty() ? 0u : result.spawns->intern(text); };
        for (const auto &instance : *scriptableInstances)
            result.scriptables->strings.push_back({result.spawns->intern("scriptable_" + instance.definition),
                                                   name(instance.target), name(instance.targetname), name(instance.linkname),
                                                   name(instance.linkto), name(instance.noteworthy)});
    }
    const auto &strings = result.spawns->strings;
''')
s = rep(s, '''    const auto spawns = strings.spawns;
    const auto staticCollisionPlan = strings.staticCollisionPlan;
''', '''    const auto spawns = strings.spawns;
    const auto scriptables = strings.scriptables;
    const auto staticCollisionPlan = strings.staticCollisionPlan;
''')
s = rep(s, '''               [assetName, ents, spawns, &collision, hasLadders, dynamic, clientTriggers](ZoneWriter &output) {
                   const uint64_t edgeListAlias =
                       hasLadders ? output.assetAlias(ASSET_TYPE_EDGELIST, assetName) : PTR_NULL;
                   emitMapEntsBody(output.buffer(), assetName.c_str(), ents, spawns.get(),
                                   &collision, edgeListAlias, dynamic, clientTriggers.get());
               });
''', '''               [assetName, ents, spawns, &collision, hasLadders, dynamic, clientTriggers, scriptables](ZoneWriter &output) {
                   const uint64_t edgeListAlias =
                       hasLadders ? output.assetAlias(ASSET_TYPE_EDGELIST, assetName) : PTR_NULL;
                   if (scriptables)
                   {
                       scriptables->definitionAliases.clear();
                       for (const auto &instance : scriptables->instances)
                           scriptables->definitionAliases.push_back(
                               output.assetAlias(ASSET_TYPE_SCRIPTABLE, instance.definition));
                   }
                   emitMapEntsBody(output.buffer(), assetName.c_str(), ents, spawns.get(),
                                   &collision, edgeListAlias, dynamic, clientTriggers.get(), scriptables.get());
               });
''')
save(p, s)

# ---------------- iw3_fastfile.h / .cpp
p = 'src/convert/iw3_iw8/iw3_fastfile.h'
s = load(p)
s = rep(s, '''    std::vector<iw8::DynamicEntity> dynamicEntities;
''', '''    std::vector<iw8::DynamicEntity> dynamicEntities;
    // Scriptable instances of the map (collision JSON "scriptable_instances"); their dynamic entities are already in
    // dynamicEntities.
    std::vector<iw8::ScriptableInstance> scriptableInstances;
''')
save(p, s)

p = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
s = load(p)
s = rep(s, '''    , dynamicEntities(std::move(other.dynamicEntities))
''', '''    , dynamicEntities(std::move(other.dynamicEntities))
    , scriptableInstances(std::move(other.scriptableInstances))
''')
s = rep(s, '''        dynamicEntities = std::move(other.dynamicEntities);
''', '''        dynamicEntities = std::move(other.dynamicEntities);
        scriptableInstances = std::move(other.scriptableInstances);
''')
s = rep(s, '''SourceDynamicEntities ReadDynamicEntities(const Json &collision, const std::filesystem::path &root,
                                          const std::filesystem::path &skeletonRoot)
{
''', '''// "scriptable_instances" of the collision JSON (IW8 Radiant research 86): one object per placed scriptable with the
// definition's name and the numbers of that definition the map-wide tables need. The definition must be in a zone the
// game always has loaded: the map zone gets a name-only reference. A bad entry stops the build: a wrong count here is a
// crash at load.
std::vector<iw8::ScriptableInstance> ReadScriptableInstances(const Json &collision)
{
    std::vector<iw8::ScriptableInstance> result;
    if (!collision.contains("scriptable_instances"))
        return result;
    const auto &list = collision.at("scriptable_instances");
    if (!list.is_array())
        throw std::runtime_error("scriptable_instances is not a list");
    const auto fail = [&](const std::size_t index, const std::string &what) {
        throw std::runtime_error("scriptable_instances[" + std::to_string(index) + "]: " + what);
    };
    const auto vec3 = [&](const Json &entry, const char *key, const std::size_t index, const bool required) {
        std::array<float, 3> value{};
        if (!entry.contains(key))
        {
            if (required)
                fail(index, std::string("no ") + key);
            return value;
        }
        const auto &node = entry.at(key);
        if (!node.is_array() || node.size() != 3)
            fail(index, std::string(key) + " is not three numbers");
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!node[axis].is_number())
                fail(index, std::string(key) + " is not three numbers");
            value[axis] = node[axis].get<float>();
            if (!std::isfinite(value[axis]) || std::abs(value[axis]) > 1000000.0f)
                fail(index, std::string(key) + " is out of range");
        }
        return value;
    };
    const auto text = [&](const Json &entry, const char *key, const std::size_t index) {
        if (!entry.contains(key))
            return std::string{};
        if (!entry.at(key).is_string())
            fail(index, std::string(key) + " is not text");
        auto value = entry.at(key).get<std::string>();
        for (const char c : value)
            if (static_cast<unsigned char>(c) < 0x20 || c == '"')
                fail(index, std::string(key) + " has a character that a script string cannot hold");
        return value;
    };
    const auto number = [&](const Json &entry, const char *key, const std::size_t index, const unsigned maximum,
                            const unsigned fallback) {
        if (!entry.contains(key))
            return fallback;
        const auto &node = entry.at(key);
        if (!node.is_number_unsigned() || node.get<std::uint64_t>() > maximum)
            fail(index, std::string(key) + " is not a whole number from 0 to " + std::to_string(maximum));
        return node.get<unsigned>();
    };
    const auto flag = [&](const Json &entry, const char *key, const std::size_t index, const bool fallback) {
        if (!entry.contains(key))
            return fallback;
        if (!entry.at(key).is_boolean())
            fail(index, std::string(key) + " is not true or false");
        return entry.at(key).get<bool>();
    };
    if (list.size() > 2000)
        throw std::runtime_error("scriptable_instances has more than 2000 entries");
    for (std::size_t index = 0; index < list.size(); ++index)
    {
        const auto &entry = list[index];
        if (!entry.is_object())
            fail(index, "not an object");
        iw8::ScriptableInstance instance;
        instance.definition = text(entry, "definition", index);
        if (instance.definition.empty() || instance.definition.size() > 120)
            fail(index, "no definition name");
        for (const char c : instance.definition)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                fail(index, "definition name '" + instance.definition + "' has a character other than a letter, a digit or _");
        instance.origin = vec3(entry, "origin", index, true);
        instance.angles = vec3(entry, "angles", index, false);
        instance.targetname = text(entry, "targetname", index);
        instance.target = text(entry, "target", index);
        instance.linkname = text(entry, "script_linkname", index);
        instance.linkto = text(entry, "script_linkto", index);
        instance.noteworthy = text(entry, "script_noteworthy", index);
        instance.serverInstancedParts = number(entry, "server_instanced_parts", index, 255, 0);
        instance.usableParts = number(entry, "usable_parts", index, 255, 0);
        instance.physicsLimit = flag(entry, "physics_limit", index, false);
        instance.maxParts = static_cast<std::uint8_t>(number(entry, "max_dynent_parts", index, 255, 1));
        if (!instance.maxParts)
            fail(index, "max_dynent_parts is 0");
        instance.instanceEntry = flag(entry, "instance_dynent", index, true);
        instance.sparseOccluder = flag(entry, "sparse_occluder", index, false);
        instance.collisionFlags = static_cast<std::uint8_t>(number(entry, "collision_flags", index, 255, 0));
        instance.physicsEntries = static_cast<std::uint8_t>(number(entry, "physics_dynents", index, 16, 0));
        result.push_back(std::move(instance));
    }
    return result;
}

SourceDynamicEntities ReadDynamicEntities(const Json &collision, const std::filesystem::path &root,
                                          const std::filesystem::path &skeletonRoot)
{
''')
s = rep(s, '''        entity.basis = iw8::DynamicEntityBasis::brush;
        entity.brushModel = source.model;
        result.dynamicEntities.push_back(std::move(entity));
    }
''', '''        entity.basis = iw8::DynamicEntityBasis::brush;
        entity.brushModel = source.model;
        result.dynamicEntities.push_back(std::move(entity));
    }
    // Scriptable instances and their dynamic entities (model basis: the list writer places them after the clutter
    // entries and before the brushes).
    result.scriptableInstances = ReadScriptableInstances(collision);
    if (!result.scriptableInstances.empty())
    {
        const auto scriptableEntities = iw8::ScriptableDynamicEntities(result.scriptableInstances);
        result.dynamicEntities.insert(result.dynamicEntities.end(), scriptableEntities.begin(), scriptableEntities.end());
        zt::info("iw3: %zu scriptable instances, %zu dynamic entities for them", result.scriptableInstances.size(),
                 scriptableEntities.size());
    }
''')
save(p, s)

# ---------------- convert.cpp
p = 'src/convert/iw3_iw8/convert.cpp'
s = load(p)
s = rep(s, '''        const auto worldStrings = iw8::internMapWorldStrings(
            writer, mapEntities, lighting,
            prepared ? prepared->staticModelCollisionModels : noStaticCollisionModels);
''', '''        const auto worldStrings = iw8::internMapWorldStrings(
            writer, mapEntities, lighting,
            prepared ? prepared->staticModelCollisionModels : noStaticCollisionModels,
            prepared ? &prepared->scriptableInstances : nullptr);
''')
s = rep(s, '''            iw8::RegisterDynamicEntityList(writer, prepared->dynamicEntities);
''', '''            // The definitions of the scriptable instances: name-only references to an always-loaded zone, which
            // the MapEnts instance records alias.
            for (const auto &instance : prepared->scriptableInstances)
                iw8::stock::RegisterNameReference(writer, ASSET_TYPE_SCRIPTABLE, instance.definition);
            iw8::RegisterDynamicEntityList(writer, prepared->dynamicEntities);
''')
save(p, s)
print('ok')
