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
''', '''	//   "loot_points": [{"origin": [x, y, z], "angles": [p, y, r], "cluster": n | "free" | "cache" | "auto",
	//       "zone": n | "auto"}] replaces the loot points of the map (MapEnts.spawnGroupLoot.points; IW8 Radiant
	//       research 87 section 2.4 and 89). The clusters and zones of the stock map are authored volumes and stay;
	//       their point lists, and the cluster list of each zone, are rebuilt from the points. "cluster" is a cluster
	//       index, "free" (0xFFFD: a point outside every cluster), "cache" (0xFFFB: a loot cache point) or "auto" (the
	//       first cluster whose box holds the point, else free); "zone" is 0 for none, a 1-based zone number, or "auto"
	//       (the first zone whose box holds the point, else none). The scriptable counts that include the loot count
	//       move with it.
	// Output: <output>/<zone>.ff, techsets_<zone>.ff when it changed, <output>/patch/<same names>.fp (the patches that
''')
rep('''		ScriptableCarry carry;
		if (edits.contains("carry"))
		{
''', '''		// Kept alive until the zone is written.
		std::vector<SpawnGroupPoint> loot_points;
		std::vector<std::vector<std::uint16_t>> loot_lists;
		if (edits.contains("loot_points"))
		{
			auto* ents = static_cast<MapEnts*>(find(ASSET_TYPE_MAP_ENTS));
			if (!ents)
			{
				throw std::runtime_error(zone_name + " has no map entities");
			}
			auto& loot = ents->spawnGroupLoot;
			const auto& list = edits.at("loot_points");
			if (!list.is_array() || list.size() > 65000)
			{
				throw std::runtime_error("loot_points must be a list of at most 65000 points");
			}
			const auto inside = [](const Bounds& box, const vec3_t& point)
			{
				for (auto axis = 0; axis < 3; axis++)
				{
					if (std::abs(point.v[axis] - box.midPoint.v[axis]) > box.halfSize.v[axis])
					{
						return false;
					}
				}
				return true;
			};
			constexpr std::uint16_t free_point = 0xFFFD, cache_point = 0xFFFB;
			loot_points.reserve(list.size());
			std::size_t index = 0;
			for (const auto& entry : list)
			{
				const auto fail = [&](const std::string& what)
				{
					throw std::runtime_error("loot_points[" + std::to_string(index) + "]: " + what);
				};
				SpawnGroupPoint point{};
				const auto vector = [&](const char* key, vec3_t& out, const bool required)
				{
					if (!entry.contains(key))
					{
						if (required)
						{
							fail(std::string("no ") + key);
						}
						return;
					}
					const auto& node = entry.at(key);
					if (!node.is_array() || node.size() != 3)
					{
						fail(std::string(key) + " is not three numbers");
					}
					for (auto axis = 0; axis < 3; axis++)
					{
						out.v[axis] = node[axis].get<float>();
						if (!std::isfinite(out.v[axis]))
						{
							fail(std::string(key) + " is not finite");
						}
					}
				};
				vector("origin", point.origin, true);
				vector("angles", point.angles, false);
				const auto cluster = entry.value("cluster", nlohmann::json("auto"));
				if (cluster.is_number_unsigned())
				{
					const auto value = cluster.get<std::uint64_t>();
					if (value >= loot.clusterCount && value != free_point && value != cache_point && value != 0xFFFF)
					{
						fail("cluster " + std::to_string(value) + " is not one of the map's " + std::to_string(loot.clusterCount) + " clusters");
					}
					point.cluster = static_cast<std::uint16_t>(value);
				}
				else if (cluster == "free")
				{
					point.cluster = free_point;
				}
				else if (cluster == "cache")
				{
					point.cluster = cache_point;
				}
				else if (cluster == "auto")
				{
					point.cluster = free_point;
					for (std::uint32_t i = 0; i < loot.clusterCount; i++)
					{
						if (inside(loot.clusters[i].bounds, point.origin))
						{
							point.cluster = static_cast<std::uint16_t>(i);
							break;
						}
					}
				}
				else
				{
					fail("cluster is not a number, \\"free\\", \\"cache\\" or \\"auto\\"");
				}
				const auto zone_value = entry.value("zone", nlohmann::json("auto"));
				if (zone_value.is_number_unsigned())
				{
					const auto value = zone_value.get<std::uint64_t>();
					if (value > loot.zoneCount)
					{
						fail("zone " + std::to_string(value) + " is beyond the map's " + std::to_string(loot.zoneCount) + " zones");
					}
					point.zone = static_cast<std::uint16_t>(value);
				}
				else if (zone_value == "auto")
				{
					point.zone = 0;
					for (std::uint32_t i = 0; i < loot.zoneCount; i++)
					{
						if (inside(loot.zones[i].bounds, point.origin))
						{
							point.zone = static_cast<std::uint16_t>(i + 1);
							break;
						}
					}
				}
				else
				{
					fail("zone is not a number or \\"auto\\"");
				}
				loot_points.push_back(point);
				++index;
			}

			// The point lists follow from the points (ascending, as in the stock block); the cluster list of a zone is
			// the clusters whose points are in it. Boxes, names and the zone of each cluster stay as authored.
			auto& cluster_lists = loot_lists;
			cluster_lists.assign(static_cast<std::size_t>(loot.clusterCount) + loot.zoneCount * 2, {});
			const auto zone_points = [&](const std::size_t zone) -> std::vector<std::uint16_t>& { return cluster_lists[loot.clusterCount + zone * 2]; };
			const auto zone_clusters = [&](const std::size_t zone) -> std::vector<std::uint16_t>& { return cluster_lists[loot.clusterCount + zone * 2 + 1]; };
			for (std::size_t i = 0; i < loot_points.size(); i++)
			{
				const auto& point = loot_points[i];
				if (point.cluster < loot.clusterCount)
				{
					cluster_lists[point.cluster].push_back(static_cast<std::uint16_t>(i));
				}
				if (point.zone)
				{
					zone_points(point.zone - 1u).push_back(static_cast<std::uint16_t>(i));
				}
			}
			for (std::uint32_t i = 0; i < loot.clusterCount; i++)
			{
				auto& cluster = loot.clusters[i];
				cluster.pointCount = static_cast<std::uint16_t>(cluster_lists[i].size());
				cluster.points = cluster_lists[i].empty() ? nullptr : cluster_lists[i].data();
				if (!cluster_lists[i].empty())
				{
					const auto zone = loot_points[cluster_lists[i].front()].zone;
					if (zone)
					{
						auto& of_zone = zone_clusters(zone - 1u);
						if (of_zone.empty() || of_zone.back() != i)
						{
							of_zone.push_back(static_cast<std::uint16_t>(i));
						}
					}
				}
			}
			for (std::uint32_t i = 0; i < loot.zoneCount; i++)
			{
				auto& zone = loot.zones[i];
				zone.pointCount = static_cast<std::uint16_t>(zone_points(i).size());
				zone.points = zone_points(i).empty() ? nullptr : zone_points(i).data();
				zone.clusterCount = static_cast<std::uint16_t>(zone_clusters(i).size());
				zone.clusters = zone_clusters(i).empty() ? nullptr : zone_clusters(i).data();
			}
			const auto before = static_cast<std::int64_t>(loot.pointCount);
			const auto after = static_cast<std::int64_t>(loot_points.size());
			loot.pointCount = static_cast<std::uint32_t>(loot_points.size());
			loot.points = loot_points.empty() ? nullptr : loot_points.data();
			// Each loot point takes one scriptable slot: the loot count and the three server counts that include it
			// (research 86: checked on 21 maps) move by the difference.
			auto& scriptables = ents->scriptableMapEnts;
			const auto moved = [&](std::uint32_t& count)
			{
				count = static_cast<std::uint32_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(count) + after - before));
			};
			if (scriptables.lootInstanceCount == static_cast<std::uint32_t>(before))
			{
				scriptables.lootInstanceCount = static_cast<std::uint32_t>(after);
				moved(scriptables.serverInstanceCount);
				moved(scriptables.serverFixedPartCount);
				moved(scriptables.serverUsablePartCount);
			}
			else
			{
				warn("editzone: %s has %u loot instances for %lld loot points; the scriptable counts are left as they are",
					zone_name.c_str(), scriptables.lootInstanceCount, static_cast<long long>(before));
			}
			report["loot_points_before"] = before;
			report["loot_points"] = after;
		}

		ScriptableCarry carry;
		if (edits.contains("carry"))
		{
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
