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

# The struct classes of the stock Warzone maps (vehicle spawns, spawn selection cameras and areas, ladder tops) are
# Replay map entities.
patch('src/games/iw8/dump/maps.cpp', [
 ('''           std::find(modes.begin(), modes.end(), classname) != modes.end() ||
           classname.starts_with("trigger_multiple_bcs_");
''', '''           std::find(modes.begin(), modes.end(), classname) != modes.end() ||
           classname.starts_with("trigger_multiple_bcs_") ||
           // The structs of the stock Warzone maps (IW8 Radiant research 89): script_struct_mp_atv, _tacrover, _jeep,
           // _cargotruck, _littlebird (vehicle spawns), _spawn_selection_camera, _spawn_selection_area, _ladder_top.
           classname.starts_with("script_struct_mp_");
'''),
])

patch('src/convert/iw3_iw8/iw3_fastfile.h', [
 ('''    // The world collision is built as streamed collision tiles (collision JSON "collision_tiles": true).
''', '''    // Stock assets to carry into the map zone (collision JSON "carry_assets": [{"zone", "type", "name"}]), for example
    // the vehicle definitions of the stock Warzone maps; each comes with everything it uses.
    struct CarryAsset
    {
        std::string zone, type, name;
    };
    std::vector<CarryAsset> carryAssets;
    // The world collision is built as streamed collision tiles (collision JSON "collision_tiles": true).
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.cpp', [
 ('''    , collisionTiles(other.collisionTiles)
''', '''    , carryAssets(std::move(other.carryAssets))
    , collisionTiles(other.collisionTiles)
'''),
 ('''        collisionTiles = other.collisionTiles;
''', '''        collisionTiles = other.collisionTiles;
        carryAssets = std::move(other.carryAssets);
'''),
 ('''    result.lootPoints = ReadLootPoints(collision);
''', '''    if (collision.contains("carry_assets"))
    {
        const auto &list = collision.at("carry_assets");
        if (!list.is_array() || list.size() > 256)
            throw std::runtime_error("carry_assets must be a list of at most 256 entries");
        for (std::size_t index = 0; index < list.size(); ++index)
        {
            const auto &entry = list[index];
            const auto text = [&](const char *key) {
                if (!entry.is_object() || !entry.contains(key) || !entry.at(key).is_string() || entry.at(key).get<std::string>().empty())
                    throw std::runtime_error("carry_assets[" + std::to_string(index) + "]: no " + key);
                return entry.at(key).get<std::string>();
            };
            const auto zone = text("zone");
            for (const char c : zone)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                    throw std::runtime_error("carry_assets[" + std::to_string(index) + "]: zone '" + zone + "' is not a zone name");
            result.carryAssets.push_back({zone, text("type"), text("name")});
        }
        zt::info("iw3: %zu stock asset(s) to carry", result.carryAssets.size());
    }
    result.lootPoints = ReadLootPoints(collision);
'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''    bool empty() const
    {
        return definitions.empty();
''', '''    bool empty() const
    {
        return definitions.empty() && assets.empty() && techsets.empty();
'''),
 ('''ScriptableCarry CarryScriptableDefinitions(const std::vector<iw8::ScriptableInstance> &instances,
                                           const std::string &replayExecutable)
{
    std::map<std::string, std::set<AssetKey>> wanted; // stock zone -> definitions
    for (const auto &instance : instances)
        if (!instance.carryZone.empty())
            wanted[instance.carryZone].insert({zonetool::iw8::ASSET_TYPE_SCRIPTABLE, instance.definition});
''', '''// The asset type of a type name as the zone formats name it ("vehicle", "xmodel", ...); -1 when none does.
std::int32_t AssetTypeOfName(const std::string &typeName)
{
    for (std::int32_t candidate = 0; candidate < 256; candidate++)
    {
        const auto format = zonetool::iw8::find_asset_format(candidate);
        if (format && typeName == format->name)
            return candidate;
    }
    return -1;
}

ScriptableCarry CarryScriptableDefinitions(const std::vector<iw8::ScriptableInstance> &instances,
                                           const std::string &replayExecutable,
                                           const std::vector<iw3::PreparedMap::CarryAsset> &more = {})
{
    std::map<std::string, std::set<AssetKey>> wanted; // stock zone -> definitions
    for (const auto &instance : instances)
        if (!instance.carryZone.empty())
            wanted[instance.carryZone].insert({zonetool::iw8::ASSET_TYPE_SCRIPTABLE, instance.definition});
    for (const auto &asset : more)
    {
        const auto type = AssetTypeOfName(asset.type);
        if (type < 0)
            throw std::runtime_error("carry_assets: unknown asset type " + asset.type);
        wanted[asset.zone].insert({type, asset.name});
    }
'''),
 ('''        const ScriptableCarry carry = CarryScriptableDefinitions(scriptableInstances, args.replayExecutable);
''', '''        static const std::vector<iw3::PreparedMap::CarryAsset> noCarryAssets;
        const ScriptableCarry carry =
            CarryScriptableDefinitions(scriptableInstances, args.replayExecutable, prepared ? prepared->carryAssets : noCarryAssets);
        if (prepared && !prepared->carryAssets.empty())
            info("carry: %zu stock asset(s) asked for, %zu asset(s) for the map zone, %zu for the techsets zone",
                 prepared->carryAssets.size(), carry.assets.size(), carry.techsets.size());
'''),
 ('''            auto additions = NewAdditions(zone, carry.assets);
            if (collision.tiled)
                additions.push_back({zonetool::iw8::ASSET_TYPE_STREAMING_INFO, &tiled.info, false});
            if (carry.empty())
                return additions;
''', '''            auto additions = NewAdditions(zone, carry.assets);
            if (collision.tiled)
                additions.push_back({zonetool::iw8::ASSET_TYPE_STREAMING_INFO, &tiled.info, false});
            // A carried model that an entity names (MapEnts key 652, for example the model of a vehicle spawn) joins the
            // level's model table, as the map's own models do above: script spawns it by name.
            if (!carry.assets.empty())
            {
                static const std::regex entityModel(R"re((?:^|[\\s{])652 "([^"*][^"]*)")re");
                std::set<std::string> named;
                for (auto match = std::sregex_iterator(mapEntities.begin(), mapEntities.end(), entityModel);
                     match != std::sregex_iterator(); ++match)
                    named.insert((*match)[1].str());
                std::set<std::string> carriedModels;
                for (const auto &asset : carry.assets)
                    if (asset.type == zonetool::iw8::ASSET_TYPE_XMODEL)
                        if (const auto name = zonetool::iw8::zone_asset_name(asset.type, asset.header); named.contains(name))
                            carriedModels.insert(name);
                if (!carriedModels.empty())
                    for (const auto &asset : zone.assets)
                    {
                        if (asset.type != zonetool::iw8::ASSET_TYPE_NET_CONST_STRINGS || asset.referenced)
                            continue;
                        auto *table = static_cast<zonetool::iw8::NetConstStrings *>(asset.header);
                        if (static_cast<std::uint32_t>(table->stringType) != 0 || static_cast<std::uint32_t>(table->sourceType) != 3)
                            continue;
                        std::set<std::string> listed;
                        for (std::uint32_t i = 0; i < table->entryCount; i++)
                            listed.insert(table->stringList[i] ? table->stringList[i] : "");
                        const auto before = listed.size();
                        listed.insert(carriedModels.begin(), carriedModels.end());
                        for (const auto &text : listed)
                            carriedModelNames.push_back(text);
                        carriedModelList.clear();
                        for (const auto &text : carriedModelNames)
                            carriedModelList.push_back(text.c_str());
                        table->stringList = carriedModelList.data();
                        table->entryCount = static_cast<std::uint32_t>(carriedModelList.size());
                        info("carry: %zu carried model(s) added to the level model table", listed.size() - before);
                        break;
                    }
            }
            if (carry.definitions.empty())
                return additions;
'''),
 ('''        // Carried scriptable definitions join the zone here, and each instance record gets its definition.
''', '''        // Kept alive until the main zone is written: the level model table with carried models.
        std::deque<std::string> carriedModelNames;
        std::vector<const char *> carriedModelList;
        // Carried scriptable definitions join the zone here, and each instance record gets its definition.
'''),
 ('''			file.transient = source.file.transient;
			file.preload_walk_size = std::max<std::uint64_t>(written.preload_walk_size, source.file.preload_walk_size);
''', '''			file.transient = source.file.transient;
			file.transient_type = source.file.transient_type;
			file.preload_walk_size = std::max<std::uint64_t>(written.preload_walk_size, source.file.preload_walk_size);
'''),
])
print('ok')
