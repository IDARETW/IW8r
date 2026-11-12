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


patch('src/convert/iw3_iw8/convert.cpp', [
 ('''				throw std::runtime_error("scriptable_instances must be an object with \\"move\\" and \\"add\\"");''',
  '''				throw std::runtime_error("scriptable_instances must be an object with \\"move\\", \\"add\\" and \\"remove\\"");'''),
 ('''			if (scriptable_records.size() > 60000)
			{
				throw std::runtime_error("more than 60000 scriptable instances");
			}''', '''			// "remove": [{"index": n, "server_instanced_parts": n, "usable_parts": n, "physics_limit": bool,
			// "physics_limit_detail": bool}] drops stock instances without a dynamic entity of their own (the numbers are
			// the definition's, as for "add"). The dynamic entities that name later instances are renumbered; the spatial
			// tree holds split planes only (no instance indices), so it stays.
			std::set<std::uint32_t> removed;
			if (node.contains("remove"))
			{
				std::size_t at = 0;
				for (const auto& entry : node.at("remove"))
				{
					const auto where = "scriptable_instances.remove[" + std::to_string(at++) + "]";
					const auto index = index_of(entry, "index", where);
					if (!removed.insert(index).second)
					{
						throw std::runtime_error(where + ": instance " + std::to_string(index) + " is removed twice");
					}
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
					server -= parts ? 1 : 0;
					fixed_parts -= parts;
					usable_parts -= number("usable_parts");
					const auto main_limit = flag("physics_limit", false);
					limits -= main_limit ? 1 : 0;
					detail_limits -= flag("physics_limit_detail", main_limit) ? 1 : 0;
				}
				if (!removed.empty())
				{
					std::vector<PlacedScriptable> kept;
					kept.reserve(scriptable_records.size() - removed.size());
					for (std::size_t i = 0; i < scriptable_records.size(); i++)
					{
						if (!removed.contains(static_cast<std::uint32_t>(i)))
						{
							kept.push_back(scriptable_records[i]);
						}
					}
					scriptable_records = std::move(kept);
					std::size_t renumbered = 0;
					for (auto& asset : zone->assets)
					{
						if (asset.type != ASSET_TYPE_DYNENTITYLIST || asset.referenced || !asset.header)
						{
							continue;
						}
						auto* list = static_cast<DynEntityList*>(asset.header);
						for (auto side = 0; side < 2; side++)
						{
							for (std::uint32_t i = 0; list->dynEntDefList[side] && i < list->dynEntCount[side]; i++)
							{
								auto& entry = list->dynEntDefList[side][i];
								if (entry.type != DYNENT_TYPE_SCRIPTABLEINST && entry.type != DYNENT_TYPE_SCRIPTABLEPHYSICS)
								{
									continue;
								}
								const auto below = static_cast<std::uint32_t>(std::distance(removed.begin(), removed.lower_bound(entry.scriptableMapIndex)));
								if (below)
								{
									entry.scriptableMapIndex -= below;
									++renumbered;
								}
							}
						}
					}
					report["scriptable_dynamic_entities_renumbered"] = renumbered;
				}
			}
			if (scriptable_records.size() > 60000)
			{
				throw std::runtime_error("more than 60000 scriptable instances");
			}'''),
 ('''			report["scriptable_instances_added"] = added;''', '''			report["scriptable_instances_added"] = added;
			report["scriptable_instances_removed"] = removed.size();'''),
])
print('ok')
