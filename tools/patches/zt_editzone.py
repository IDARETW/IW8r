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

p = 'src/convert/iw3_iw8/convert.cpp'
s = load(p)

# 1. the carry: any asset type as a root
s = rep(s, '''    std::map<std::string, void *> definitions;                      // definition name -> its header
''', '''    std::map<std::string, void *> definitions;                      // definition name -> its header
    std::set<AssetKeyOf> roots;                                     // every asset asked for and found
''')
s = rep(s, '''struct ScriptableCarry
{''', '''using AssetKeyOf = std::pair<std::int32_t, std::string>;

struct ScriptableCarry
{''')
s = rep(s, '''ScriptableCarry CarryScriptableDefinitions(const std::vector<iw8::ScriptableInstance> &instances,
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
''', '''// `wanted`: stock zone -> the assets (type, name) to take from it with everything they use.
ScriptableCarry CarryStockAssets(const std::map<std::string, std::set<AssetKey>> &wanted,
                                 const std::filesystem::path &zoneFolder)
{
    ScriptableCarry carry;
    if (wanted.empty())
        return carry;
''')
s = rep(s, '''        for (const auto &name : names)
        {
            const AssetKey key{zonetool::iw8::ASSET_TYPE_SCRIPTABLE, name};
            const auto found = sources[0].full.find(key);
            if (found == sources[0].full.end())
                throw std::runtime_error("stock zone " + zoneName + " does not hold the scriptable definition " + name);
            if (carry.definitions.emplace(name, found->second.header).second && sources[0].rootKeys.insert(key).second)
                sources[0].roots.push_back(found->second);
        }
''', '''        for (const auto &key : names)
        {
            const auto found = sources[0].full.find(key);
            if (found == sources[0].full.end())
            {
                const auto format = zonetool::iw8::find_asset_format(key.first);
                throw std::runtime_error("stock zone " + zoneName + " does not hold the " +
                                         std::string(format ? format->name : "asset") + " " + key.second);
            }
            carry.roots.insert(key);
            if (key.first == zonetool::iw8::ASSET_TYPE_SCRIPTABLE)
                carry.definitions.emplace(key.second, found->second.header);
            if (sources[0].rootKeys.insert(key).second)
                sources[0].roots.push_back(found->second);
        }
''')
s = rep(s, '''        info("scriptables: reading stock zone %s for %zu definition(s)", zoneName.c_str(), names.size());
''', '''        info("scriptables: reading stock zone %s for %zu asset(s)", zoneName.c_str(), names.size());
''')
# wrapper for the instances, placed right after CarryStockAssets (before the writeZone comment)
s = rep(s, '''// `prepare` (optional) runs on the loaded zone before it is written again and returns more additions.
''', '''ScriptableCarry CarryScriptableDefinitions(const std::vector<iw8::ScriptableInstance> &instances,
                                           const std::string &replayExecutable)
{
    std::map<std::string, std::set<AssetKey>> wanted; // stock zone -> definitions
    for (const auto &instance : instances)
        if (!instance.carryZone.empty())
            wanted[instance.carryZone].insert({zonetool::iw8::ASSET_TYPE_SCRIPTABLE, instance.definition});
    if (wanted.empty())
        return {};
    if (replayExecutable.empty())
        throw std::runtime_error("carrying a stock scriptable definition needs --replay (the game folder)");
    return CarryStockAssets(wanted, std::filesystem::path(replayExecutable).parent_path() / "zone");
}

// `prepare` (optional) runs on the loaded zone before it is written again and returns more additions.
''')

# 2. edit_zone at the end of the public namespace
a = '''	void convert_extraction(const std::filesystem::path& zone_path, const std::filesystem::path& extraction,
		const zone_conversion& conversion)
	{
		convert_map(zone_path, conversion, extraction);
	}
'''
b = a + '''
	// editzone (IW8 Radiant research 88): a stock map zone is loaded, changed in memory and written again. With no
	// change the written data equals the stock zone byte for byte (the loader keeps the zone's pointer markers), so
	// everything an edit does not touch stays as shipped. Edits (a JSON file):
	//   "entities": path of an IW8 entity text (numeric keys, as the game stores it); "rebuild_spawns" (default true)
	//       also rebuilds the compiled spawn list from it, as the game expects the two to agree;
	//   "hide_static_models": indices into GfxWorld.smodels.smodelInstanceData whose scale becomes 0;
	//   "carry": [{"zone", "type", "name"}] assets of other stock zones added with everything they use (their
	//       materials and technique sets go to the techsets companion); a carried xmodel is also named in the
	//       level's model table so that a script_model entity can use it.
	// Output: <output>/<zone>.ff, techsets_<zone>.ff when it changed, and <zone>.edit-report.json.
	void edit_zone(const std::filesystem::path& game_path, const std::string& zone_name,
		const std::filesystem::path& edits_path, const std::filesystem::path& output)
	{
		using namespace zonetool::iw8;
		const auto zone_folder = game_path / "zone";
		const auto zone_path = zone_folder / (zone_name + ".ff");
		if (!std::filesystem::exists(zone_path))
		{
			throw std::runtime_error("stock zone " + zone_path.string() + " was not found");
		}

		std::ifstream input(edits_path, std::ios::binary);
		if (!input)
		{
			throw std::runtime_error("cannot read the edits file " + edits_path.string());
		}
		const auto edits = nlohmann::json::parse(input);
		nlohmann::ordered_json report;
		report["zone"] = zone_name;

		auto zone = load_zone(zone_path, {.keep_temp_data = true, .keep_markers = true});
		const auto find = [&](const std::int32_t type) -> void*
		{
			for (const auto& asset : zone->assets)
			{
				if (asset.type == type && !asset.referenced)
				{
					return asset.header;
				}
			}
			return nullptr;
		};

		// Kept alive until the zone is written.
		std::string entity_text;
		std::vector<SpawnPointEntityRecord> spawn_records;
		std::vector<std::vector<const char*>> string_lists;
		std::deque<std::string> names;

		if (edits.contains("entities"))
		{
			auto* ents = static_cast<MapEnts*>(find(ASSET_TYPE_MAP_ENTS));
			if (!ents)
			{
				throw std::runtime_error(zone_name + " has no map entities");
			}
			std::ifstream text(std::filesystem::path(edits.at("entities").get<std::string>()), std::ios::binary);
			if (!text)
			{
				throw std::runtime_error("cannot read the entity text " + edits.at("entities").get<std::string>());
			}
			entity_text.assign(std::istreambuf_iterator<char>(text), {});
			while (!entity_text.empty() && entity_text.back() == '\\0')
			{
				entity_text.pop_back();
			}
			if (entity_text.empty() || entity_text.size() > 0x7FFFFFF0u)
			{
				throw std::runtime_error("the entity text is empty or too large");
			}
			report["entity_chars_before"] = ents->numEntityChars;
			ents->entityString = entity_text.data(); // c_str: the terminator is part of numEntityChars
			ents->numEntityChars = static_cast<std::int32_t>(entity_text.size() + 1);
			report["entity_chars"] = ents->numEntityChars;

			if (edits.value("rebuild_spawns", true))
			{
				// The compiled spawn list: one record per mp_*_spawn* entity in file order, as the stock list is (checked:
				// the records rebuilt from the stock text of mp_shipment equal the stock records).
				const ::iw8::ReplaySpawns spawns(entity_text);
				const auto id = [&](const std::uint32_t index) -> scr_string_t
				{
					const auto& value = spawns.strings.at(index);
					return static_cast<scr_string_t>(value.empty() ? 0 : SL_GetString(value.data()));
				};
				spawn_records.reserve(spawns.records.size());
				for (const auto& record : spawns.records)
				{
					SpawnPointEntityRecord native{};
					native.index = record.index;
					native.name = id(record.name);
					native.target = id(record.target);
					native.script_noteworthy = id(record.noteworthy);
					for (auto axis = 0; axis < 3; axis++)
					{
						native.origin[axis] = record.origin[axis];
						native.angles[axis] = record.angles[axis];
					}
					spawn_records.push_back(native);
				}
				report["spawns_before"] = ents->spawnList.spawnsCount;
				ents->spawnList.spawnsCount = static_cast<std::uint16_t>(spawn_records.size());
				ents->spawnList.spawns = spawn_records.empty() ? nullptr : spawn_records.data();
				report["spawns"] = spawn_records.size();
			}
		}

		if (edits.contains("hide_static_models"))
		{
			auto* world = static_cast<GfxWorld*>(find(ASSET_TYPE_GFXWORLD));
			if (!world || !world->smodels.smodelInstanceData)
			{
				throw std::runtime_error(zone_name + " has no static model instances");
			}
			std::size_t hidden = 0;
			for (const auto& entry : edits.at("hide_static_models"))
			{
				const auto index = entry.get<std::uint64_t>();
				if (index >= world->smodels.instanceCount)
				{
					throw std::runtime_error("hide_static_models: instance " + std::to_string(index) + " is beyond the " +
						std::to_string(world->smodels.instanceCount) + " instances of the map");
				}
				world->smodels.smodelInstanceData[index].scale = 0.0f;
				++hidden;
			}
			report["static_model_instances"] = world->smodels.instanceCount;
			report["hidden_static_models"] = hidden;
		}

		ScriptableCarry carry;
		if (edits.contains("carry"))
		{
			std::map<std::string, std::set<AssetKey>> wanted;
			std::set<std::string> models;
			for (const auto& entry : edits.at("carry"))
			{
				const auto type_name = entry.at("type").get<std::string>();
				std::int32_t type = -1;
				for (std::int32_t candidate = 0; candidate < 256; candidate++)
				{
					const auto format = find_asset_format(candidate);
					if (format && type_name == format->name)
					{
						type = candidate;
						break;
					}
				}
				if (type < 0)
				{
					throw std::runtime_error("carry: unknown asset type " + type_name);
				}
				const auto name = entry.at("name").get<std::string>();
				wanted[entry.at("zone").get<std::string>()].insert({type, name});
				if (type == ASSET_TYPE_XMODEL)
				{
					models.insert(name);
				}
			}
			carry = CarryStockAssets(wanted, zone_folder);

			// A carried model joins the level's model table (NetConstStrings of type 0 from the map), kept sorted.
			if (!models.empty())
			{
				for (const auto& asset : zone->assets)
				{
					if (asset.type != ASSET_TYPE_NET_CONST_STRINGS || asset.referenced)
					{
						continue;
					}
					auto* table = static_cast<NetConstStrings*>(asset.header);
					if (static_cast<std::uint32_t>(table->stringType) != 0 ||
						static_cast<std::uint32_t>(table->sourceType) != 3)
					{
						continue;
					}
					std::set<std::string> listed;
					for (std::uint32_t i = 0; i < table->entryCount; i++)
					{
						listed.insert(table->stringList[i] ? table->stringList[i] : "");
					}
					const auto before = listed.size();
					listed.insert(models.begin(), models.end());
					auto& list = string_lists.emplace_back();
					for (const auto& text : listed)
					{
						list.push_back(names.emplace_back(text).data());
					}
					table->stringList = list.data();
					table->entryCount = static_cast<std::uint32_t>(list.size());
					report["level_models_added"] = listed.size() - before;
					break;
				}
			}
		}

		const write_options options{.markers = &zone->markers, .strings_marker = zone->strings_marker,
			.assets_marker = zone->assets_marker};
		std::filesystem::create_directories(output);
		const auto write = [&](const loaded_zone& source, const std::vector<zone_asset>& wanted_additions,
			const std::string& name, const write_options& how)
		{
			const auto additions = NewAdditions(source, wanted_additions);
			auto written = write_zone(source, how, additions);
			xfile::fastfile file{};
			file.payload = std::move(written.data);
			file.block_sizes = std::move(written.block_sizes);
			file.xfile_version = source.file.xfile_version;
			file.transient = source.file.transient;
			file.preload_walk_size = std::max<std::uint64_t>(written.preload_walk_size, source.file.preload_walk_size);
			// Stock zones declare more for the preload block than the writer computes: never less than the stock size.
			for (std::size_t block = 0; block < file.block_sizes.size() && block < source.file.block_sizes.size(); block++)
			{
				file.block_sizes[block] = std::max(file.block_sizes[block], source.file.block_sizes[block]);
			}
			xfile::write(output / (name + ".ff"), definition().format, file);
			return additions.size();
		};
		report["assets_added"] = write(*zone, carry.assets, zone_name, options);

		if (!carry.techsets.empty())
		{
			const auto techsets_path = zone_folder / ("techsets_" + zone_name + ".ff");
			if (!std::filesystem::exists(techsets_path))
			{
				throw std::runtime_error("stock zone " + techsets_path.string() + " was not found");
			}
			const auto techsets = load_zone(techsets_path, {.keep_temp_data = true, .keep_markers = true});
			const write_options techset_options{.markers = &techsets->markers, .strings_marker = techsets->strings_marker,
				.assets_marker = techsets->assets_marker};
			report["techset_assets_added"] = write(*techsets, carry.techsets, "techsets_" + zone_name, techset_options);
		}

		std::ofstream(output / (zone_name + ".edit-report.json"), std::ios::binary | std::ios::trunc) << report.dump(2) << "\\n";
		ZONETOOL_INFO("editzone: wrote %s (%s)", (output / (zone_name + ".ff")).string().data(), report.dump().data());
	}
'''
s = rep(s, a.replace('\n', '\n'), b)
if '#include <deque>' not in s:
    s = rep(s, '#include <cstring>\n', '#include <cstring>\n#include <deque>\n')
save(p, s)

# 3. command line
p = 'src/cli/zone_commands.cpp'
s = load(p)
s = rep(s, '''	void convert_extraction(const std::filesystem::path& zone_path, const std::filesystem::path& extraction,
''', '''	void edit_zone(const std::filesystem::path& game_path, const std::string& zone_name,
		const std::filesystem::path& edits_path, const std::filesystem::path& output);
	void convert_extraction(const std::filesystem::path& zone_path, const std::filesystem::path& extraction,
''')
s = rep(s, '''	void build_extraction(const settings& settings, const std::span<const std::string> args)
	{
''', '''	void edit_zone(const settings& settings, const std::span<const std::string> args)
	{
		constexpr std::string_view usage = "editzone <zone> <edits.json> <output folder>";
		if (settings.game->name != "iw8")
		{
			std::cout << "editzone is only supported for -game iw8\\n";
			return;
		}
		if (args.size() != 3)
		{
			std::cout << std::format("usage: {}\\n", usage);
			return;
		}
		try
		{
			convert::iw3_iw8::edit_zone(settings.game_path, args[0], args[1], args[2]);
		}
		catch (const std::exception& e)
		{
			std::cout << std::format("editzone failed: {}\\n", e.what());
			std::exit(1);
		}
	}

	void build_extraction(const settings& settings, const std::span<const std::string> args)
	{
''')
save(p, s)
p = 'src/cli/commands.hpp'
s = load(p)
s = rep(s, '''	void build_extraction(const settings& settings, std::span<const std::string> args);
''', '''	void build_extraction(const settings& settings, std::span<const std::string> args);
	void edit_zone(const settings& settings, std::span<const std::string> args);
''')
save(p, s)
p = 'src/cli/commands.cpp'
s = load(p)
s = rep(s, '''			command{"refreshzone",''', '''			command{"editzone", "editzone <zone> <edits.json> <output folder>", edit_zone},
			command{"refreshzone",''')
save(p, s)
print('ok')
