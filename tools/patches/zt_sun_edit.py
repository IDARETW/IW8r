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
 ('''	//   "loot_boxes": {"clusters": [{"mid": [x, y, z], "half": [x, y, z]}], "zones": [{"name": "...", "mid": [...],''',
  '''	//   "sun": {"color": [r, g, b], "intensity": x} sets the colour (linear sRGB) and / or the intensity of the stock
	//       sun: the ComWorld primary light the first stage names (clipMap_t.stages[0].sunPrimaryLightIndex). The
	//       direction is not changed: the baked sun shadow data holds it too (GfxCompressedSunShadowParams).
	//   "loot_boxes": {"clusters": [{"mid": [x, y, z], "half": [x, y, z]}], "zones": [{"name": "...", "mid": [...],'''),
 ('''		// Kept alive until the zone is written.
		std::vector<SpawnGroupCluster> loot_cluster_records;''', '''		if (edits.contains("sun"))
		{
			const auto& node = edits.at("sun");
			if (!node.is_object() || (!node.contains("color") && !node.contains("intensity")))
			{
				throw std::runtime_error("sun must be an object with \\"color\\" and / or \\"intensity\\"");
			}
			auto* clip = static_cast<clipMap_t*>(find(ASSET_TYPE_CLIPMAP));
			auto* com = static_cast<ComWorld*>(find(ASSET_TYPE_COM_MAP));
			if (!clip || !com || !clip->stageCount || !clip->stages)
			{
				throw std::runtime_error(zone_name + " has no stage or no ComWorld: the sun cannot be found");
			}
			const auto index = clip->stages[0].sunPrimaryLightIndex;
			if (index >= com->primaryLightCount || !com->primaryLights)
			{
				throw std::runtime_error(zone_name + ": the sun primary light " + std::to_string(index) + " is beyond the map's " +
					std::to_string(com->primaryLightCount));
			}
			auto& sun = com->primaryLights[index];
			report["sun_primary_light"] = index;
			report["sun_color_before"] = {sun.colorLinearSrgb.v[0], sun.colorLinearSrgb.v[1], sun.colorLinearSrgb.v[2]};
			report["sun_intensity_before"] = sun.intensity;
			if (node.contains("color"))
			{
				const auto& c = node.at("color");
				if (!c.is_array() || c.size() != 3)
				{
					throw std::runtime_error("sun.color is not three numbers");
				}
				for (auto k = 0; k < 3; k++)
				{
					const auto value = c[k].get<float>();
					if (!std::isfinite(value) || value < 0.0f || value > 16.0f)
					{
						throw std::runtime_error("sun.color values must be 0 to 16 (linear)");
					}
					sun.colorLinearSrgb.v[k] = value;
				}
			}
			if (node.contains("intensity"))
			{
				const auto value = node.at("intensity").get<float>();
				if (!std::isfinite(value) || value < 0.0f || value > 1000000.0f)
				{
					throw std::runtime_error("sun.intensity must be 0 to 1000000");
				}
				sun.intensity = value;
			}
			report["sun_color"] = {sun.colorLinearSrgb.v[0], sun.colorLinearSrgb.v[1], sun.colorLinearSrgb.v[2]};
			report["sun_intensity"] = sun.intensity;
		}

		// Kept alive until the zone is written.
		std::vector<SpawnGroupCluster> loot_cluster_records;'''),
])
print('ok')
