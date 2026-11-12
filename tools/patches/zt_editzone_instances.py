import io, sys
root = sys.argv[1]
p = root + '/src/convert/iw3_iw8/convert.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)

rep('''	// Output: <output>/<zone>.ff, techsets_<zone>.ff when it changed, <output>/patch/<same names>.fp (the patches that
''', '''	//   "scriptable_instances": {"move": [{"index": n, "origin": [x, y, z], "angles": [p, y, r]}],
	//       "add": [{"like": n, "origin": [x, y, z], "angles": [p, y, r], "server_instanced_parts": n, "usable_parts": n,
	//       "physics_limit": bool, "physics_limit_detail": bool}]} moves placed scriptables (MapEnts
	//       scriptableMapEnts.mapInstances) and adds copies of one (IW8 Radiant research 89 section 8). Only for
	//       instances without dynamic entities of their own (the Warzone buy station): an instance that a dynamic entity
	//       of this zone names is refused, because that entity holds its own placement. A copy takes the definition, the
	//       class name and the flags of instance "like"; the instance count, the three server counts and the physics
	//       limits rise by the numbers given (those of the definition, from the editor's catalogue). Nothing is removed:
	//       the dynamic entities of other instances name their instance by index.
	// Output: <output>/<zone>.ff, techsets_<zone>.ff when it changed, <output>/patch/<same names>.fp (the patches that
''')
rep('''		ScriptableCarry carry;
		if (edits.contains("carry"))
		{
''', '''		// Kept alive until the zone is written.
		using PlacedScriptable = std::remove_pointer_t<decltype(std::declval<MapEnts&>().scriptableMapEnts.mapInstances)>;
		std::vector<PlacedScriptable> scriptable_records;
		if (edits.contains("scriptable_instances"))
		{
			auto* ents = static_cast<MapEnts*>(find(ASSET_TYPE_MAP_ENTS));
			if (!ents)
			{
				throw std::runtime_error(zone_name + " has no map entities");
			}
			auto& block = ents->scriptableMapEnts;
			const auto& node = edits.at("scriptable_instances");
			if (!node.is_object())
			{
				throw std::runtime_error("scriptable_instances must be an object with \\"move\\" and \\"add\\"");
			}
			const auto before = block.mapInstanceCount;
			if (before && !block.mapInstances)
			{
				throw std::runtime_error(zone_name + ": the scriptable instances are not in the zone");
			}
			// The instances that a dynamic entity of this zone names: those keep their own placement and are refused.
			std::set<std::uint32_t> with_dynamic_entity;
			for (const auto& asset : zone->assets)
			{
				if (asset.type != ASSET_TYPE_DYNENTITYLIST || asset.referenced || !asset.header)
				{
					continue;
				}
				const auto* list = static_cast<const DynEntityList*>(asset.header);
				for (auto side = 0; side < 2; side++)
				{
					for (std::uint32_t i = 0; list->dynEntDefList[side] && i < list->dynEntCount[side]; i++)
					{
						const auto& entry = list->dynEntDefList[side][i];
						if (entry.type == DYNENT_TYPE_SCRIPTABLEINST || entry.type == DYNENT_TYPE_SCRIPTABLEPHYSICS)
						{
							with_dynamic_entity.insert(entry.scriptableMapIndex);
						}
					}
				}
			}
			const auto placement = [&](const nlohmann::json& entry, const std::string& where, PlacedScriptable& record)
			{
				const auto vector = [&](const char* key, vec3_t& out, const bool required)
				{
					if (!entry.contains(key))
					{
						if (required)
						{
							throw std::runtime_error(where + ": no " + key);
						}
						return;
					}
					const auto& value = entry.at(key);
					if (!value.is_array() || value.size() != 3)
					{
						throw std::runtime_error(where + ": " + key + " is not three numbers");
					}
					for (auto axis = 0; axis < 3; axis++)
					{
						if (!value[axis].is_number())
						{
							throw std::runtime_error(where + ": " + key + " is not three numbers");
						}
						out.v[axis] = value[axis].get<float>();
						if (!std::isfinite(out.v[axis]))
						{
							throw std::runtime_error(where + ": " + key + " is not finite");
						}
					}
				};
				vector("origin", record.initialOrigin, true);
				vector("angles", record.initialAngles, false);
			};
			const auto index_of = [&](const nlohmann::json& entry, const char* key, const std::string& where) -> std::uint32_t
			{
				if (!entry.is_object() || !entry.contains(key) || !entry.at(key).is_number_unsigned())
				{
					throw std::runtime_error(where + ": no \\"" + key + "\\" (the index of a placed instance)");
				}
				const auto value = entry.at(key).get<std::uint64_t>();
				if (value >= before)
				{
					throw std::runtime_error(where + ": instance " + std::to_string(value) + " is beyond the map's " + std::to_string(before));
				}
				if (with_dynamic_entity.contains(static_cast<std::uint32_t>(value)))
				{
					throw std::runtime_error(where + ": instance " + std::to_string(value) +
						" has a dynamic entity of its own (its placement is there too); only instances without one can be moved or copied");
				}
				return static_cast<std::uint32_t>(value);
			};
			scriptable_records.assign(block.mapInstances, block.mapInstances + before);
			std::size_t moved = 0, added = 0;
			if (node.contains("move"))
			{
				std::size_t at = 0;
				for (const auto& entry : node.at("move"))
				{
					const auto where = "scriptable_instances.move[" + std::to_string(at++) + "]";
					auto& record = scriptable_records[index_of(entry, "index", where)];
					placement(entry, where, record);
					++moved;
				}
			}
			std::int64_t server = 0, fixed_parts = 0, usable_parts = 0, limits = 0, detail_limits = 0;
			if (node.contains("add"))
			{
				std::size_t at = 0;
				for (const auto& entry : node.at("add"))
				{
					const auto where = "scriptable_instances.add[" + std::to_string(at++) + "]";
					// A copy of the stock record as it was read (a moved original does not move its copies).
					auto record = block.mapInstances[index_of(entry, "like", where)];
					record.initialAngles = {};
					placement(entry, where, record);
					record.target = record.targetname = record.script_linkname = record.script_linkto = record.script_noteworthy = 0;
					record.mapEntLookup = 0;
					const auto number = [&](const char* key) -> std::int64_t
					{
						if (!entry.contains(key) || !entry.at(key).is_number_unsigned() || entry.at(key).get<std::uint64_t>() > 255)
						{
							throw std::runtime_error(where + ": no " + key + " (a number of the definition)");
						}
						return entry.at(key).get<std::int64_t>();
					};
					const auto flag = [&](const char* key, const bool fallback)
					{
						if (!entry.contains(key))
						{
							return fallback;
						}
						if (!entry.at(key).is_boolean())
						{
							throw std::runtime_error(where + ": " + key + " is not true or false");
						}
						return entry.at(key).get<bool>();
					};
					const auto parts = number("server_instanced_parts");
					server += parts ? 1 : 0;
					fixed_parts += parts;
					usable_parts += number("usable_parts");
					const auto main_limit = flag("physics_limit", false);
					limits += main_limit ? 1 : 0;
					detail_limits += flag("physics_limit_detail", main_limit) ? 1 : 0;
					scriptable_records.push_back(record);
					++added;
				}
			}
			if (scriptable_records.size() > 60000)
			{
				throw std::runtime_error("more than 60000 scriptable instances");
			}
			block.mapInstances = scriptable_records.data();
			block.mapInstanceCount = static_cast<std::uint32_t>(scriptable_records.size());
			const auto raise = [](std::uint32_t& count, const std::int64_t by)
			{
				count = static_cast<std::uint32_t>(static_cast<std::int64_t>(count) + by);
			};
			raise(block.serverInstanceCount, server);
			raise(block.serverFixedPartCount, fixed_parts);
			raise(block.serverUsablePartCount, usable_parts);
			raise(block.physicsLimits.clientStandaloneMainRigidBodyCount, limits);
			raise(block.physicsLimits.clientStandaloneMotionCount, limits);
			raise(block.physicsLimits.serverStandaloneMainRigidBodyCount, limits);
			raise(block.physicsLimits.serverStandaloneMotionCount, limits);
			raise(block.physicsLimits.clientStandaloneDetailedRigidBodyCount, detail_limits);
			raise(block.physicsLimits.serverStandaloneDetailedRigidBodyCount, detail_limits);
			report["scriptable_instances_before"] = before;
			report["scriptable_instances"] = scriptable_records.size();
			report["scriptable_instances_moved"] = moved;
			report["scriptable_instances_added"] = added;
		}

		ScriptableCarry carry;
		if (edits.contains("carry"))
		{
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
