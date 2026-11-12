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
 ('''	//   "scriptable_instances": {"move": [''', '''	//   "loot_boxes": {"clusters": [{"mid": [x, y, z], "half": [x, y, z]}], "zones": [{"name": "...", "mid": [...],
	//       "half": [...]}]} adds loot clusters and named loot zones after the stock ones (IW8 Radiant research 89
	//       section 9). The stock boxes stay. A new zone's name is what the loot tables pick the loot by, so it should
	//       be a name the map's tables know. A new cluster's zone is the first zone whose box holds its middle (0:
	//       none). With "loot_points" (applied after this) "auto" points find the new boxes like the stock ones.
	//   "scriptable_instances": {"move": ['''),
 ('''		// Kept alive until the zone is written.
		std::vector<SpawnGroupPoint> loot_points;
''', '''		// Kept alive until the zone is written.
		std::vector<SpawnGroupCluster> loot_cluster_records;
		std::vector<SpawnGroupZone> loot_zone_records;
		if (edits.contains("loot_boxes"))
		{
			auto* ents = static_cast<MapEnts*>(find(ASSET_TYPE_MAP_ENTS));
			if (!ents)
			{
				throw std::runtime_error(zone_name + " has no map entities");
			}
			auto& loot = ents->spawnGroupLoot;
			const auto& boxes = edits.at("loot_boxes");
			if (!boxes.is_object())
			{
				throw std::runtime_error("loot_boxes must be an object with \\"clusters\\" and \\"zones\\" lists");
			}
			const auto box_of = [](const nlohmann::json& entry, const std::string& where)
			{
				Bounds bounds{};
				for (const char* key : {"mid", "half"})
				{
					if (!entry.is_object() || !entry.contains(key) || !entry.at(key).is_array() || entry.at(key).size() != 3)
					{
						throw std::runtime_error(where + ": " + key + " is not three numbers");
					}
					auto& out = std::string(key) == "mid" ? bounds.midPoint : bounds.halfSize;
					for (auto axis = 0; axis < 3; axis++)
					{
						out.v[axis] = entry.at(key)[axis].get<float>();
						if (!std::isfinite(out.v[axis]) || (std::string(key) == "half" && out.v[axis] <= 0.0f))
						{
							throw std::runtime_error(where + ": " + key + " is not finite" + (std::string(key) == "half" ? " and above 0" : ""));
						}
					}
				}
				return bounds;
			};
			const auto list_of = [&](const char* key) -> nlohmann::json
			{
				if (!boxes.contains(key))
				{
					return nlohmann::json::array();
				}
				if (!boxes.at(key).is_array())
				{
					throw std::runtime_error(std::string("loot_boxes.") + key + " is not a list");
				}
				return boxes.at(key);
			};
			// Zones first: a new cluster takes the zone that holds its middle.
			const auto zones = list_of("zones");
			loot_zone_records.assign(loot.zones, loot.zones + loot.zoneCount);
			std::size_t index = 0;
			for (const auto& entry : zones)
			{
				const auto where = "loot_boxes.zones[" + std::to_string(index++) + "]";
				const auto name = entry.is_object() ? entry.value("name", std::string()) : std::string();
				if (name.empty())
				{
					throw std::runtime_error(where + ": no name");
				}
				for (const char c : name)
				{
					if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
					{
						throw std::runtime_error(where + ": '" + name + "' is not a lower-case name");
					}
				}
				SpawnGroupZone zone{};
				zone.name = static_cast<scr_string_t>(SL_GetString(name.data()));
				zone.bounds = box_of(entry, where);
				loot_zone_records.push_back(zone);
			}
			const auto clusters = list_of("clusters");
			loot_cluster_records.assign(loot.clusters, loot.clusters + loot.clusterCount);
			index = 0;
			for (const auto& entry : clusters)
			{
				SpawnGroupCluster cluster{};
				cluster.bounds = box_of(entry, "loot_boxes.clusters[" + std::to_string(index++) + "]");
				for (std::size_t i = 0; i < loot_zone_records.size(); i++)
				{
					const auto& zone_box = loot_zone_records[i].bounds;
					bool holds = true;
					for (auto axis = 0; axis < 3; axis++)
					{
						holds = holds && std::abs(cluster.bounds.midPoint.v[axis] - zone_box.midPoint.v[axis]) <= zone_box.halfSize.v[axis];
					}
					if (holds)
					{
						cluster.zone = static_cast<std::uint16_t>(i + 1);
						break;
					}
				}
				loot_cluster_records.push_back(cluster);
			}
			// Cluster indices from 0xFFFB up are the special point values; zone numbers are u16 and 1-based.
			if (loot_cluster_records.size() >= 0xFFFB || loot_zone_records.size() >= 0xFFFF)
			{
				throw std::runtime_error("loot_boxes: too many clusters or zones");
			}
			if (!clusters.empty())
			{
				loot.clusterCount = static_cast<std::uint16_t>(loot_cluster_records.size());
				loot.clusters = loot_cluster_records.data();
			}
			if (!zones.empty())
			{
				loot.zoneCount = static_cast<std::uint16_t>(loot_zone_records.size());
				loot.zones = loot_zone_records.data();
			}
			report["loot_clusters_added"] = clusters.size();
			report["loot_zones_added"] = zones.size();
		}

		std::vector<SpawnGroupPoint> loot_points;
'''),
])
print('ok')
