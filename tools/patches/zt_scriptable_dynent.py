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

# ---------------- replay_dynentity.h
p = 'src/games/iw8/assets/replay_dynentity.h'
s = load(p)
s = rep(s, '''struct DynamicEntity
{
    std::string model;
''', '''// DynEntityType of an entry. Every dynamic entity of a stock map belongs to a scriptable instance (IW8 Radiant research
// 86): one SCRIPTABLEINST entry for most definitions, and one SCRIPTABLEPHYSICS entry per dynamic entity the
// definition requires (maxNumDynEntsRequired).
enum class DynamicEntityKind : std::uint8_t
{
    clutter = 1,
    scriptableInstance = 3,
    scriptablePhysics = 4,
};

struct DynamicEntity
{
    std::string model;
''')
s = rep(s, '''    DynamicEntityBasis basis{DynamicEntityBasis::model};
    std::uint16_t brushModel{};
};
''', '''    DynamicEntityBasis basis{DynamicEntityBasis::model};
    std::uint16_t brushModel{};
    // Scriptable entries (model basis, no XModel): the instance, the entry's index among the instance's physics
    // entries, the definition's maxNumDynEntPartsBase (kept in rigidBodies: the two share a byte) and, for an
    // instance entry, sparseOccluder (stock doors: priority 3, the second spatial population).
    DynamicEntityKind kind{DynamicEntityKind::clutter};
    std::uint32_t scriptableMapIndex{};
    std::uint16_t scriptableSubIndex{};
    bool sparseOccluder{};
};

// One scriptable instance of the map (MapEnts ScriptableMapEnts.mapInstances) with the numbers of its definition the
// map-wide counts need. The definition itself is a name-only reference to an always-loaded zone.
struct ScriptableInstance
{
    std::string definition;
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};
    std::string targetname, target, linkname, linkto, noteworthy;
    std::uint8_t flags{};
    std::uint32_t serverInstancedParts{}; // ScriptableDef.serverInstancedPartCount
    std::uint32_t usableParts{};          // ScriptableDef.usablePartCount
    bool physicsLimit{};                  // counts once in each ScriptablePhysicsLimits field (stock doors)
    std::uint8_t maxParts{1};             // ScriptableDef.maxNumDynEntPartsBase
    bool instanceEntry{true};             // has a SCRIPTABLEINST dynamic entity
    bool sparseOccluder{};                // that entry is a sparse occluder (doors)
    std::uint8_t collisionFlags{};        // of the instance entry (doors: 1)
    std::uint8_t physicsEntries{};        // ScriptableDef.maxNumDynEntsRequired
};

// The dynamic entities of the instances, in stock order (per instance: its instance entry, then its physics entries).
std::vector<DynamicEntity> ScriptableDynamicEntities(const std::vector<ScriptableInstance> &instances);
''')
s = rep(s, '''    std::uint8_t maxPoseParts{1};
    std::uint32_t entriesWithExtraPoseParts{};
};
''', '''    std::uint8_t maxPoseParts{1};
    std::uint32_t entriesWithExtraPoseParts{};
    // Model entries by where they are linked: the radial population (clutter and scriptable instance entries), the
    // sparse-occluder population (door instance entries) and the no-spatial list (scriptable physics entries).
    std::uint32_t radialModels{};
    std::uint32_t occluderModels{};
    std::uint32_t noSpatialModels{};
    std::array<float, 3> occluderMins{};
    std::array<float, 3> occluderMaxs{};
};
''')
save(p, s)

# ---------------- replay_dynentity.cpp
p = 'src/games/iw8/assets/replay_dynentity.cpp'
s = load(p)
s = rep(s, '''using EntityPartition = std::array<std::vector<const DynamicEntity *>, 2>;
''', '''using EntityPartition = std::array<std::vector<const DynamicEntity *>, 2>;

// Where a model entry is linked: 0 radial population, 1 sparse-occluder population, 2 the no-spatial list.
std::size_t ChainOf(const DynamicEntity &entity)
{
    if (entity.kind == DynamicEntityKind::scriptablePhysics)
        return 2;
    return entity.kind == DynamicEntityKind::scriptableInstance && entity.sparseOccluder ? 1 : 0;
}

// Quaternion (x, y, z, w) of Euler angles (pitch, yaw, roll in degrees), as AnglesToQuat.
std::array<float, 4> QuaternionOf(const std::array<float, 3> &angles)
{
    const double k = 3.14159265358979323846 / 360.0;
    const double sp = std::sin(angles[0] * k), cp = std::cos(angles[0] * k);
    const double sy = std::sin(angles[1] * k), cy = std::cos(angles[1] * k);
    const double sr = std::sin(angles[2] * k), cr = std::cos(angles[2] * k);
    return {static_cast<float>(sr * cp * cy - cr * sp * sy), static_cast<float>(cr * sp * cy + sr * cp * sy),
            static_cast<float>(cr * cp * sy - sr * sp * cy), static_cast<float>(cr * cp * cy + sr * sp * sy)};
}
''')
s = rep(s, '''void WriteDefinitions(ZoneWriter &writer, const std::vector<const DynamicEntity *> &entities,
                      const DynamicEntityBasis basis, const std::uint32_t globalBase)
{
    if (entities.empty())
        return;

    writer.align(7);
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        const auto &entity = *entities[index];
        const auto globalIndex = globalBase + static_cast<std::uint32_t>(index);
        std::uint8_t definition[0x50]{};
        if (basis == DynamicEntityBasis::model)
            Put(definition, 0x00, writer.assetAlias(ASSET_TYPE_XMODEL, entity.model));
        std::memcpy(definition + 0x10, entity.quaternion.data(), sizeof(entity.quaternion));
        std::memcpy(definition + 0x20, entity.origin.data(), sizeof(entity.origin));
        // Each chain runs from its last entry down to its basis' first.
        Put(definition, 0x2C, globalIndex == globalBase ? InvalidNode : globalIndex - 1);
        definition[0x31] = static_cast<std::uint8_t>(basis);
        definition[0x32] = 1; // DYNENT_TYPE_CLUTTER
        if (basis == DynamicEntityBasis::model)
        {
            definition[0x33] = 0;                            // DYNENT_RADIAL_PRIORITY_LEVEL_LOW
            Put<std::uint32_t>(definition, 0x34, 0);         // spatial leaf 0
            Put<std::uint16_t>(definition, 0x38, 0xFFFF);    // clientId: none yet
            Put<std::uint16_t>(definition, 0x3A, 0xFFFF);
            Put<std::uint32_t>(definition, 0x3C, 0);         // scriptableMapIndex (stock 0)
        }
        else
        {
''', '''// `lastNoSpatialModel`: the last model entry of the no-spatial list (scriptable physics), which the first brush entry
// continues; InvalidNode without one.
void WriteDefinitions(ZoneWriter &writer, const std::vector<const DynamicEntity *> &entities,
                      const DynamicEntityBasis basis, const std::uint32_t globalBase,
                      const std::uint32_t lastNoSpatialModel = InvalidNode)
{
    if (entities.empty())
        return;

    writer.align(7);
    // The previous entry of each chain of model entries (ChainOf).
    std::array<std::uint32_t, 3> previous{InvalidNode, InvalidNode, InvalidNode};
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        const auto &entity = *entities[index];
        const auto globalIndex = globalBase + static_cast<std::uint32_t>(index);
        const bool scriptable = entity.kind != DynamicEntityKind::clutter;
        std::uint8_t definition[0x50]{};
        if (basis == DynamicEntityBasis::model && !scriptable)
            Put(definition, 0x00, writer.assetAlias(ASSET_TYPE_XMODEL, entity.model));
        std::memcpy(definition + 0x10, entity.quaternion.data(), sizeof(entity.quaternion));
        std::memcpy(definition + 0x20, entity.origin.data(), sizeof(entity.origin));
        // Each chain runs from its last entry down to its first.
        if (basis == DynamicEntityBasis::model)
        {
            auto &link = previous[ChainOf(entity)];
            Put(definition, 0x2C, link);
            link = globalIndex;
        }
        else
            Put(definition, 0x2C, globalIndex == globalBase ? lastNoSpatialModel : globalIndex - 1);
        definition[0x31] = static_cast<std::uint8_t>(basis);
        definition[0x32] = static_cast<std::uint8_t>(entity.kind);
        if (basis == DynamicEntityBasis::model && scriptable)
        {
            // Stock scriptable entries (mp_shipment, mp_backlot2; IW8 Radiant research 86): clientId 0 / 0, the
            // instance's index; an instance entry is radial (priority 0) or a sparse occluder (3), a physics entry is
            // always active, spawned active and on the no-spatial list.
            const bool physics = entity.kind == DynamicEntityKind::scriptablePhysics;
            definition[0x33] = physics ? 0xFF : entity.sparseOccluder ? 3 : 0;
            Put<std::uint32_t>(definition, 0x34, 0); // spatial leaf 0
            Put<std::uint32_t>(definition, 0x3C, entity.scriptableMapIndex);
            Put(definition, 0x44, entity.scriptableSubIndex);
            definition[0x49] = physics ? 1 : 0; // spawnActive
        }
        else if (basis == DynamicEntityBasis::model)
        {
            definition[0x33] = 0;                            // DYNENT_RADIAL_PRIORITY_LEVEL_LOW
            Put<std::uint32_t>(definition, 0x34, 0);         // spatial leaf 0
            Put<std::uint16_t>(definition, 0x38, 0xFFFF);    // clientId: none yet
            Put<std::uint16_t>(definition, 0x3A, 0xFFFF);
            Put<std::uint32_t>(definition, 0x3C, 0);         // scriptableMapIndex (stock 0)
        }
        else
        {
''')
s = rep(s, '''        Put<std::uint16_t>(definition, 0x40, 0); // dynentitylist0
        definition[0x48] = entity.rigidBodies;  // physicsAssetRigidBodiesCount
        definition[0x4A] = entity.noPhysics ? 1 : 0;
''', '''        Put<std::uint16_t>(definition, 0x40, 0); // dynentitylist0
        definition[0x48] = entity.rigidBodies;  // physicsAssetRigidBodiesCount / scriptableMaxPartCount
        if (basis != DynamicEntityBasis::model || !scriptable)
            definition[0x49] = definition[0x49]; // (set above for brushes; clutter spawns inactive)
        definition[0x4A] = entity.noPhysics ? 1 : 0;
''')
s = rep(s, '''    // dynEntNoSpatialList head/tail: brush dynents only.
    Put(list, 0x38, counts.brushes ? total - 1 : InvalidNode);
    Put(list, 0x3C, counts.brushes ? counts.models : InvalidNode);
''', '''    // The first and last model entry of each chain (ChainOf).
    std::array<std::uint32_t, 3> first{InvalidNode, InvalidNode, InvalidNode}, last = first;
    for (std::size_t index = 0; index < partition[0].size(); ++index)
    {
        const auto chain = ChainOf(*partition[0][index]);
        if (first[chain] == InvalidNode)
            first[chain] = static_cast<std::uint32_t>(index);
        last[chain] = static_cast<std::uint32_t>(index);
    }
    // dynEntNoSpatialList head/tail: scriptable physics entries, then brush dynents (head = the last entry).
    Put(list, 0x38, counts.brushes ? total - 1 : last[2]);
    Put(list, 0x3C, first[2] != InvalidNode ? first[2] : counts.brushes ? counts.models : InvalidNode);
''')
s = rep(s, '''    WriteDefinitions(writer, partition[1], DynamicEntityBasis::brush, counts.models);
''', '''    WriteDefinitions(writer, partition[1], DynamicEntityBasis::brush, counts.models, last[2]);
''')
s = rep(s, '''    // Population 0 = radial clutter, one leaf; population 1 (sparse occluders) empty.
    std::uint8_t radial[SpatialPopulationSize]{};
    if (counts.models)
    {
        Put(radial, 0x00, PTR_FOLLOWS);
        Put(radial, 0x08, PTR_FOLLOWS);
        Put(radial, 0x10, PTR_FOLLOWS);
        Put<std::uint32_t>(radial, 0x30, 1);
        Put<std::uint32_t>(radial, 0x34, counts.models);
    }
    writer.align(7);
    writer.write(radial, sizeof(radial));
    if (counts.models)
    {
        writer.align(3);
        writer.writeT<std::uint32_t>(counts.models - 1); // bucket head
        writer.align(3);
        writer.writeT<std::uint32_t>(0); // bucket tail
        writer.align(3);
        writer.writeT<std::uint32_t>(0); // partition id
    }
    const std::uint8_t emptyPopulation[SpatialPopulationSize]{};
    writer.align(7);
    writer.write(emptyPopulation, sizeof(emptyPopulation));
''', '''    // Population 0 = radial (clutter, scriptable instance entries), population 1 = sparse occluders (door instance
    // entries); one leaf each, empty without entries.
    const std::array<std::uint32_t, 2> populations{counts.radialModels, counts.occluderModels};
    for (std::size_t chain = 0; chain < populations.size(); ++chain)
    {
        std::uint8_t population[SpatialPopulationSize]{};
        if (populations[chain])
        {
            Put(population, 0x00, PTR_FOLLOWS);
            Put(population, 0x08, PTR_FOLLOWS);
            Put(population, 0x10, PTR_FOLLOWS);
            Put<std::uint32_t>(population, 0x30, 1);
            Put<std::uint32_t>(population, 0x34, populations[chain]);
        }
        writer.align(7);
        writer.write(population, sizeof(population));
        if (populations[chain])
        {
            writer.align(3);
            writer.writeT<std::uint32_t>(last[chain]); // bucket head
            writer.align(3);
            writer.writeT<std::uint32_t>(first[chain]); // bucket tail
            writer.align(3);
            writer.writeT<std::uint32_t>(0); // partition id
        }
    }
''')
s = rep(s, '''    counts.modelMins.fill(std::numeric_limits<float>::max());
    counts.modelMaxs.fill(std::numeric_limits<float>::lowest());
    for (const auto &entity : entities)
    {
        std::uint32_t *count = nullptr;
        switch (entity.basis)
        {
        case DynamicEntityBasis::model:
            count = &counts.models;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                counts.modelMins[axis] =
                    std::min(counts.modelMins[axis], entity.origin[axis] - SpatialLeafPadding);
                counts.modelMaxs[axis] =
                    std::max(counts.modelMaxs[axis], entity.origin[axis] + SpatialLeafPadding);
            }
            break;
''', '''    counts.modelMins.fill(std::numeric_limits<float>::max());
    counts.modelMaxs.fill(std::numeric_limits<float>::lowest());
    counts.occluderMins.fill(std::numeric_limits<float>::max());
    counts.occluderMaxs.fill(std::numeric_limits<float>::lowest());
    for (const auto &entity : entities)
    {
        std::uint32_t *count = nullptr;
        switch (entity.basis)
        {
        case DynamicEntityBasis::model:
            count = &counts.models;
            if (const auto chain = ChainOf(entity); chain == 2)
                ++counts.noSpatialModels;
            else
            {
                ++(chain ? counts.occluderModels : counts.radialModels);
                auto &mins = chain ? counts.occluderMins : counts.modelMins;
                auto &maxs = chain ? counts.occluderMaxs : counts.modelMaxs;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    mins[axis] = std::min(mins[axis], entity.origin[axis] - SpatialLeafPadding);
                    maxs[axis] = std::max(maxs[axis], entity.origin[axis] + SpatialLeafPadding);
                }
            }
            break;
''')
s = rep(s, '''        const std::uint8_t parts =
            entity.basis == DynamicEntityBasis::model && !entity.noPhysics ? entity.rigidBodies : 1;
''', '''        // A scriptable entry: scriptableMaxPartCount, the same byte (stock: dynEntMaxPosePartCount is the largest and
        // dynEntsWithExtraPosePartsCount the entries above 1, mp_shipment 47 / 163, mp_backlot2 39 / 194).
        const std::uint8_t parts =
            entity.basis == DynamicEntityBasis::model && !entity.noPhysics ? entity.rigidBodies : 1;
''')
s = rep(s, '''    if (!counts.models)
    {
        counts.modelMins.fill(0.0f);
        counts.modelMaxs.fill(0.0f);
    }
    return counts;
}
''', '''    if (!counts.radialModels)
    {
        counts.modelMins.fill(0.0f);
        counts.modelMaxs.fill(0.0f);
    }
    if (!counts.occluderModels)
    {
        counts.occluderMins.fill(0.0f);
        counts.occluderMaxs.fill(0.0f);
    }
    return counts;
}

std::vector<DynamicEntity> ScriptableDynamicEntities(const std::vector<ScriptableInstance> &instances)
{
    std::vector<DynamicEntity> result;
    for (std::size_t index = 0; index < instances.size(); ++index)
    {
        const auto &instance = instances[index];
        DynamicEntity entity;
        entity.quaternion = QuaternionOf(instance.angles);
        entity.origin = instance.origin;
        entity.noPhysics = false;
        entity.rigidBodies = instance.maxParts; // scriptableMaxPartCount
        entity.scriptableMapIndex = static_cast<std::uint32_t>(index);
        if (instance.instanceEntry)
        {
            entity.kind = DynamicEntityKind::scriptableInstance;
            entity.sparseOccluder = instance.sparseOccluder;
            entity.collisionFlags = instance.collisionFlags;
            result.push_back(entity);
        }
        entity.kind = DynamicEntityKind::scriptablePhysics;
        entity.sparseOccluder = false;
        entity.collisionFlags = 0;
        for (std::uint16_t sub = 0; sub < instance.physicsEntries; ++sub)
        {
            entity.scriptableSubIndex = sub;
            result.push_back(entity);
        }
    }
    return result;
}
''')
s = rep(s, '''        if (entity.basis == DynamicEntityBasis::model && entity.model.empty())
            throw std::runtime_error("Replay model dynamic entity has no XModel");
''', '''        if (entity.basis == DynamicEntityBasis::model && entity.kind == DynamicEntityKind::clutter && entity.model.empty())
            throw std::runtime_error("Replay model dynamic entity has no XModel");
''')
save(p, s)
print('ok')
