import io, sys
root = sys.argv[1]
def load(p):
    return io.open(root + '/' + p, encoding='utf-8', newline='').read()
def save(p, s):
    io.open(root + '/' + p, 'w', encoding='utf-8', newline='').write(s)
def rep(s, a, b, count=1):
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:80])
    return s.replace(a, b)

# ---------------- writer.hpp
p = 'src/games/iw8/zone/writer.hpp'
s = load(p)
s = rep(s, '''	xfile::fastfile rewrite_fastfile(const std::string& name, xfile::fastfile file);
}
''', '''	xfile::fastfile rewrite_fastfile(const std::string& name, xfile::fastfile file);

	// The name the writer keys an asset definition by (a loaded name stub has the name without its comma).
	std::string asset_name(std::int32_t type, const void* header);

	// The definitions of a loaded zone that a zone holding `roots` needs in full: the roots and every asset they use,
	// directly or through other assets, whose data the zone holds. Assets the zone only references (name stubs of a
	// common zone) are left out: they stay references. The order is the order of first use.
	std::vector<zone_asset> asset_closure(const loaded_zone& zone, std::span<const zone_asset> roots);

	// As rewrite_fastfile with additions, but the additions are chosen after the zone has loaded: `prepare` sees the
	// loaded definitions, may change them (for example point a field at an addition) and returns the additions.
	xfile::fastfile rewrite_fastfile(const std::string& name, xfile::fastfile file,
		const std::function<std::vector<zone_asset>(loaded_zone&)>& prepare, zone_inventory* inventory);
}
''')
if '#include <functional>' not in s:
    s = rep(s, '#pragma once\n', '#pragma once\n\n#include <functional>\n')
save(p, s)

# ---------------- writer.cpp
p = 'src/games/iw8/zone/writer.cpp'
s = load(p)
s = rep(s, '''	xfile::fastfile rewrite_fastfile(const std::string& name, xfile::fastfile file)
	{
		return rewrite_fastfile(name, std::move(file), {});
	}
''', '''	xfile::fastfile rewrite_fastfile(const std::string& name, xfile::fastfile file)
	{
		return rewrite_fastfile(name, std::move(file), {});
	}

	std::string asset_name(const std::int32_t type, const void* header)
	{
		return name_of(type, header);
	}

	std::vector<zone_asset> asset_closure(const loaded_zone& zone, const std::span<const zone_asset> roots)
	{
		// A scratch zone that lists only the roots, written with every definition of the source zone at hand: the
		// writer emits each asset at its first use, so what it emitted in full is the closure.
		std::vector<zone_asset> assets;
		std::map<asset_key, zone_asset> full;
		assets.reserve(zone.assets.size());
		for (const auto& asset : zone.assets)
		{
			assets.emplace_back(asset.type, asset.header, asset.referenced);
			if (!asset.referenced)
			{
				full.try_emplace(asset_key{asset.type, name_of(asset.type, asset.header)}, zone_asset{asset.type, asset.header, false});
			}
		}

		const std::vector<zone_asset> list(roots.begin(), roots.end());
		const auto written = write_zone(list, assets, zone.script_strings, {});

		std::vector<zone_asset> closure;
		std::set<asset_key> seen;
		for (const auto& [offset, type, name] : written.asset_offsets)
		{
			const asset_key key{type, name};
			const auto found = full.find(key);
			if (found != full.end() && seen.insert(key).second)
			{
				closure.push_back(found->second);
			}
		}
		return closure;
	}

	xfile::fastfile rewrite_fastfile(const std::string& name, xfile::fastfile file,
		const std::function<std::vector<zone_asset>(loaded_zone&)>& prepare, zone_inventory* inventory)
	{
		xfile::fastfile result{};
		result.xfile_version = file.xfile_version;
		result.transient = file.transient;
		const auto preload_walk_size = file.preload_walk_size;
		const auto expected_size = file.payload.size() + file.payload.size() / 16;

		auto zone = load_zone(name, std::move(file), {.keep_temp_data = true});
		const auto additional = prepare ? prepare(*zone) : std::vector<zone_asset>{};
		auto written = write_zone(*zone, {.expected_size = expected_size}, additional);
		zone.reset();

		result.payload = std::move(written.data);
		result.block_sizes = std::move(written.block_sizes);
		result.preload_walk_size = std::max<std::uint64_t>(written.preload_walk_size, preload_walk_size);
		if (inventory)
		{
			inventory->list_asset_count = written.list_asset_count;
			inventory->asset_offsets = std::move(written.asset_offsets);
		}
		return result;
	}
''')
save(p, s)

# ---------------- replay_dynentity.h: carried flag
p = 'src/games/iw8/assets/replay_dynentity.h'
s = load(p)
s = rep(s, '''// map-wide counts need. The definition itself is a name-only reference to an always-loaded zone.
struct ScriptableInstance
{
    std::string definition;
''', '''// map-wide counts need. The definition itself is a name-only reference to an always-loaded zone, or, with carryZone,
// a copy of the stock definition taken from that stock zone when the map zone is written.
struct ScriptableInstance
{
    std::string definition;
    std::string carryZone; // stock zone that holds the definition in full ("" = always loaded)
''')
save(p, s)

# ---------------- map_zone.h: no alias for a carried definition
p = 'src/games/iw8/zone/map_zone.h'
s = load(p)
s = rep(s, '''                       for (const auto &instance : scriptables->instances)
                           scriptables->definitionAliases.push_back(
                               output.assetAlias(ASSET_TYPE_SCRIPTABLE, instance.definition));
''', '''                       // A carried definition is not in this body: its pointer is set when the zone is written
                       // again with the definition added (convert.cpp).
                       for (const auto &instance : scriptables->instances)
                           scriptables->definitionAliases.push_back(
                               instance.carryZone.empty() ? output.assetAlias(ASSET_TYPE_SCRIPTABLE, instance.definition)
                                                          : PTR_NULL);
''')
save(p, s)

# ---------------- iw3_fastfile.cpp: read carry_zone
p = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
s = load(p)
s = rep(s, '''        instance.origin = vec3(entry, "origin", index, true);
        instance.angles = vec3(entry, "angles", index, false);
        instance.targetname = text(entry, "targetname", index);
''', '''        instance.carryZone = text(entry, "carry_zone", index);
        for (const char c : instance.carryZone)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                fail(index, "carry_zone '" + instance.carryZone + "' is not a zone name");
        instance.origin = vec3(entry, "origin", index, true);
        instance.angles = vec3(entry, "angles", index, false);
        instance.targetname = text(entry, "targetname", index);
''')
save(p, s)

# ---------------- convert.cpp
p = 'src/convert/iw3_iw8/convert.cpp'
s = load(p)
# writeZone: prepare hook
s = rep(s, '''bool writeZone(const std::string &outputPath, std::vector<uint8_t> body, const Iw8WriteParams &params,
               const std::span<const zonetool::iw8::zone_asset> additional = {},
               zonetool::iw8::zone_inventory *inventory = nullptr)
{
    if (g_converterLayout)
    {
        if (!additional.empty())
''', '''using ZonePrepare = std::function<std::vector<zonetool::iw8::zone_asset>(zonetool::iw8::loaded_zone &)>;

// `prepare` (optional) runs on the loaded zone before it is written again and returns more additions.
bool writeZone(const std::string &outputPath, std::vector<uint8_t> body, const Iw8WriteParams &params,
               const std::span<const zonetool::iw8::zone_asset> additional = {},
               zonetool::iw8::zone_inventory *inventory = nullptr, const ZonePrepare &prepare = {})
{
    if (g_converterLayout)
    {
        if (!additional.empty() || prepare)
''')
s = rep(s, '''        auto written = zonetool::iw8::rewrite_fastfile(
            std::filesystem::path(outputPath).stem().string(), std::move(file), additional, inventory);
''', '''        auto written = prepare
                           ? zonetool::iw8::rewrite_fastfile(
                                 std::filesystem::path(outputPath).stem().string(), std::move(file),
                                 [&](zonetool::iw8::loaded_zone &zone) {
                                     std::vector<zonetool::iw8::zone_asset> all(additional.begin(), additional.end());
                                     const auto more = prepare(zone);
                                     all.insert(all.end(), more.begin(), more.end());
                                     return all;
                                 },
                                 inventory)
                           : zonetool::iw8::rewrite_fastfile(std::filesystem::path(outputPath).stem().string(),
                                                             std::move(file), additional, inventory);
''')
# carry: before writeZone definition, add the carry code
s = rep(s, '''using ZonePrepare = std::function<std::vector<zonetool::iw8::zone_asset>(zonetool::iw8::loaded_zone &)>;
''', '''using ZonePrepare = std::function<std::vector<zonetool::iw8::zone_asset>(zonetool::iw8::loaded_zone &)>;

// Stock scriptable definitions a map carries (IW8 Radiant research 86, step F3): the definition and everything it
// uses that its stock zone holds in full (models, their surfaces, materials, images, physics assets, effects), taken
// from the stock zone in memory and added when the map zone is written again; and the technique sets of those
// materials, taken from the stock zone's techsets companion and added to the map's techsets zone. Assets the stock
// zone only references (common zones) stay name references.
struct ScriptableCarry
{
    std::vector<std::unique_ptr<zonetool::iw8::loaded_zone>> zones; // alive until both zones are written
    std::vector<zonetool::iw8::zone_asset> assets;                  // for the map zone
    std::vector<zonetool::iw8::zone_asset> techsets;                // for the techsets zone
    std::map<std::string, void *> definitions;                      // definition name -> its header
    bool empty() const
    {
        return definitions.empty();
    }
};

using AssetKey = std::pair<std::int32_t, std::string>;

// Additions that the loaded zone does not hold or name yet, each once.
std::vector<zonetool::iw8::zone_asset> NewAdditions(const zonetool::iw8::loaded_zone &zone,
                                                    const std::vector<zonetool::iw8::zone_asset> &wanted)
{
    std::set<AssetKey> have;
    for (const auto &asset : zone.assets)
        have.emplace(asset.type, zonetool::iw8::asset_name(asset.type, asset.header));
    for (const auto &asset : zone.list)
        have.emplace(asset.type, zonetool::iw8::asset_name(asset.type, asset.header));
    std::vector<zonetool::iw8::zone_asset> result;
    for (const auto &asset : wanted)
        if (have.emplace(asset.type, zonetool::iw8::asset_name(asset.type, asset.header)).second)
            result.push_back(asset);
    return result;
}

ScriptableCarry CarryScriptableDefinitions(const std::vector<iw8::ScriptableInstance> &instances,
                                           const std::string &replayExecutable)
{
    ScriptableCarry carry;
    std::map<std::string, std::set<std::string>> wanted; // stock zone -> definitions
    for (const auto &instance : instances)
        if (!instance.carryZone.empty())
            wanted[instance.carryZone].insert(instance.definition);
    if (wanted.empty())
        return carry;
    if (replayExecutable.empty())
        throw std::runtime_error("carrying a stock scriptable definition needs --replay (the game folder)");
    const auto zoneFolder = std::filesystem::path(replayExecutable).parent_path() / "zone";
    for (const auto &[zoneName, names] : wanted)
    {
        const auto path = zoneFolder / (zoneName + ".ff");
        if (!std::filesystem::exists(path))
            throw std::runtime_error("stock zone " + path.string() + " (scriptable definitions) was not found");
        info("scriptables: reading stock zone %s for %zu definition(s)", zoneName.c_str(), names.size());
        auto zone = zonetool::iw8::load_zone(path, {.keep_temp_data = true});
        std::vector<zonetool::iw8::zone_asset> roots;
        for (const auto &asset : zone->assets)
        {
            if (asset.type != zonetool::iw8::ASSET_TYPE_SCRIPTABLE || asset.referenced)
                continue;
            const auto name = zonetool::iw8::asset_name(asset.type, asset.header);
            if (names.count(name) && carry.definitions.emplace(name, asset.header).second)
                roots.push_back({asset.type, asset.header, false});
        }
        for (const auto &name : names)
            if (!carry.definitions.count(name))
                throw std::runtime_error("stock zone " + zoneName + " does not hold the scriptable definition " + name);
        const auto closure = zonetool::iw8::asset_closure(*zone, roots);
        std::map<std::string, std::size_t> byType;
        std::set<std::string> techsetNames;
        for (const auto &asset : closure)
        {
            const auto format = zonetool::iw8::find_asset_format(asset.type);
            ++byType[format ? format->name : "?"];
            if (asset.type == zonetool::iw8::ASSET_TYPE_MATERIAL)
            {
                const auto *material = static_cast<const zonetool::iw8::Material *>(asset.header);
                if (material->techniqueSet)
                    techsetNames.insert(
                        zonetool::iw8::asset_name(zonetool::iw8::ASSET_TYPE_TECHNIQUE_SET, material->techniqueSet));
            }
        }
        std::string summary;
        for (const auto &[type, count] : byType)
            summary += (summary.empty() ? "" : ", ") + std::to_string(count) + " " + type;
        info("scriptables: %s gives %zu asset(s): %s", zoneName.c_str(), closure.size(), summary.c_str());
        carry.assets.insert(carry.assets.end(), closure.begin(), closure.end());
        carry.zones.push_back(std::move(zone));

        // The technique sets of the carried materials live in the stock zone's techsets companion.
        if (!techsetNames.empty())
        {
            const auto techsetsPath = zoneFolder / ("techsets_" + zoneName + ".ff");
            if (!std::filesystem::exists(techsetsPath))
                throw std::runtime_error("stock zone " + techsetsPath.string() + " (technique sets) was not found");
            auto techsetZone = zonetool::iw8::load_zone(techsetsPath, {.keep_temp_data = true});
            std::vector<zonetool::iw8::zone_asset> techsetRoots;
            std::set<std::string> found;
            for (const auto &asset : techsetZone->assets)
            {
                if (asset.type != zonetool::iw8::ASSET_TYPE_TECHNIQUE_SET || asset.referenced)
                    continue;
                const auto name = zonetool::iw8::asset_name(asset.type, asset.header);
                if (techsetNames.count(name) && found.insert(name).second)
                    techsetRoots.push_back({asset.type, asset.header, false});
            }
            std::string missing;
            for (const auto &name : techsetNames)
                if (!found.count(name))
                    missing += (missing.empty() ? "" : ", ") + name;
            if (!missing.empty())
                warn("scriptables: technique set(s) not in techsets_%s (expected in a common zone): %s",
                     zoneName.c_str(), missing.c_str());
            const auto techsetClosure = zonetool::iw8::asset_closure(*techsetZone, techsetRoots);
            info("scriptables: techsets_%s gives %zu asset(s) for %zu technique set(s)", zoneName.c_str(),
                 techsetClosure.size(), techsetRoots.size());
            carry.techsets.insert(carry.techsets.end(), techsetClosure.begin(), techsetClosure.end());
            carry.zones.push_back(std::move(techsetZone));
        }
    }
    return carry;
}
''')
# use: load before techsets write
s = rep(s, '''        const auto techsetParams = writeParams(techsets);
        if (!writeZone(path_join(outputDirectory, "techsets_" + map + ".ff"), techsets.takeBody(), techsetParams))
        {
            result = 1;
        }
''', '''        static const std::vector<iw8::ScriptableInstance> noScriptables;
        const auto &scriptableInstances = prepared ? prepared->scriptableInstances : noScriptables;
        const ScriptableCarry carry = CarryScriptableDefinitions(scriptableInstances, args.replayExecutable);
        timing.lap("stock scriptable definitions");

        const auto techsetParams = writeParams(techsets);
        const ZonePrepare techsetPrepare = carry.techsets.empty() ? ZonePrepare{} : [&carry](zonetool::iw8::loaded_zone &zone) {
            auto additions = NewAdditions(zone, carry.techsets);
            info("scriptables: %zu stock technique-set asset(s) added to the techsets zone", additions.size());
            return additions;
        };
        if (!writeZone(path_join(outputDirectory, "techsets_" + map + ".ff"), techsets.takeBody(), techsetParams, {},
                       nullptr, techsetPrepare))
        {
            result = 1;
        }
''')
s = rep(s, '''        zonetool::iw8::zone_inventory nativeInventory;
        if (!writeZone(path_join(outputDirectory, map + ".ff"), writer.takeBody(), mapParams, native,
                       native.empty() ? nullptr : &nativeInventory))
        {
            result = 1;
        }
''', '''        zonetool::iw8::zone_inventory nativeInventory;
        // Carried scriptable definitions join the zone here, and each instance record gets its definition.
        const ZonePrepare mapPrepare = carry.empty() ? ZonePrepare{} : [&](zonetool::iw8::loaded_zone &zone) {
            auto additions = NewAdditions(zone, carry.assets);
            std::size_t pointed = 0;
            for (const auto &asset : zone.assets)
            {
                if (asset.type != zonetool::iw8::ASSET_TYPE_MAP_ENTS || asset.referenced)
                    continue;
                auto &block = static_cast<zonetool::iw8::MapEnts *>(asset.header)->scriptableMapEnts;
                if (block.mapInstanceCount != scriptableInstances.size() || !block.mapInstances)
                    throw std::runtime_error("the map zone's scriptable instances do not match the extraction");
                for (std::size_t index = 0; index < scriptableInstances.size(); ++index)
                {
                    if (scriptableInstances[index].carryZone.empty())
                        continue;
                    block.mapInstances[index].initialDef = static_cast<zonetool::iw8::ScriptableDef *>(
                        carry.definitions.at(scriptableInstances[index].definition));
                    ++pointed;
                }
            }
            info("scriptables: %zu stock asset(s) added to the map zone, %zu instance(s) given their definition",
                 additions.size(), pointed);
            return additions;
        };
        const bool wantInventory = !native.empty() || !carry.empty();
        if (!writeZone(path_join(outputDirectory, map + ".ff"), writer.takeBody(), mapParams, native,
                       wantInventory ? &nativeInventory : nullptr, mapPrepare))
        {
            result = 1;
        }
''')
# name-only references only for definitions that are not carried
s = rep(s, '''            for (const auto &instance : prepared->scriptableInstances)
                iw8::stock::RegisterNameReference(writer, ASSET_TYPE_SCRIPTABLE, instance.definition);
''', '''            for (const auto &instance : prepared->scriptableInstances)
                if (instance.carryZone.empty())
                    iw8::stock::RegisterNameReference(writer, ASSET_TYPE_SCRIPTABLE, instance.definition);
''')
s = rep(s, '''        for (const auto &instance : prepared->scriptableInstances)
            set(ASSET_TYPE_SCRIPTABLE, instance.definition,
                "name-only reference to the definition of a scriptable instance of the map");
''', '''        for (const auto &instance : prepared->scriptableInstances)
            set(ASSET_TYPE_SCRIPTABLE, instance.definition,
                instance.carryZone.empty() ? "name-only reference to the definition of a scriptable instance of the map"
                                           : "stock scriptable definition carried from " + instance.carryZone);
''')
if '#include <functional>' not in s:
    s = rep(s, '#include <fstream>\n', '#include <fstream>\n#include <functional>\n')
save(p, s)
print('ok')
