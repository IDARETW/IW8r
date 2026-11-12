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
 ('''	//   "sun": {"color": [r, g, b], "intensity": x} sets''', '''	//   "replace_materials": [{"from": "tw/sky_mp_backlot2", "to": "tw/sky_mp_shipment"}] points every world surface
	//       (GfxWorld surfaces) of material "from" at material "to": the stock sky, or a stock texture swapped. "to" must
	//       be in this zone or carried in with "carry" (type material). The surfaces' draw keys are left as they are: the
	//       engine fills them at load (the converter's own maps write zeros and draw).
	//   "sun": {"color": [r, g, b], "intensity": x} sets'''),
 ('''		const write_options options{.expected_size = zone->file.payload.size() + zone->file.payload.size() / 16};''',
  '''		if (edits.contains("replace_materials"))
		{
			const auto& list = edits.at("replace_materials");
			if (!list.is_array())
			{
				throw std::runtime_error("replace_materials must be a list of {\\"from\\", \\"to\\"}");
			}
			const auto plain = [](std::string name)
			{
				if (!name.empty() && name[0] == ',')
				{
					name.erase(0, 1);
				}
				return name;
			};
			// Every material this zone can point at: its own and referenced entries, and the carried ones.
			std::map<std::string, Material*> materials;
			const auto collect = [&](const auto& assets)
			{
				for (const auto& asset : assets)
				{
					if (asset.type == ASSET_TYPE_MATERIAL && asset.header)
					{
						materials.emplace(plain(zone_asset_name(asset.type, asset.header)), static_cast<Material*>(asset.header));
					}
				}
			};
			collect(zone->assets);
			collect(carry.assets);
			collect(carry.techsets);
			auto* world = static_cast<GfxWorld*>(find(ASSET_TYPE_GFX_MAP));
			if (!world)
			{
				throw std::runtime_error(zone_name + " has no world (gfx_map)");
			}
			std::size_t replaced = 0;
			std::size_t at = 0;
			for (const auto& entry : list)
			{
				const auto where = "replace_materials[" + std::to_string(at++) + "]";
				if (!entry.is_object() || !entry.contains("from") || !entry.contains("to") || !entry.at("from").is_string() || !entry.at("to").is_string())
				{
					throw std::runtime_error(where + ": needs \\"from\\" and \\"to\\" material names");
				}
				const auto from = plain(entry.at("from").get<std::string>()), to = plain(entry.at("to").get<std::string>());
				const auto found = materials.find(to);
				if (found == materials.end())
				{
					throw std::runtime_error(where + ": material " + to + " is not in this zone; carry it in (\\"carry\\" with type material)");
				}
				std::size_t count = 0;
				for (std::uint32_t i = 0; i < world->surfaces.count && world->surfaces.surfaces; i++)
				{
					auto& surface = world->surfaces.surfaces[i];
					if (surface.material && surface.material->name && plain(surface.material->name) == from)
					{
						surface.material = found->second;
						++count;
					}
				}
				if (!count)
				{
					throw std::runtime_error(where + ": no world surface of " + zone_name + " uses material " + from);
				}
				replaced += count;
			}
			report["world_surfaces_rematerialed"] = replaced;
		}

		const write_options options{.expected_size = zone->file.payload.size() + zone->file.payload.size() / 16};'''),
])
print('ok')
