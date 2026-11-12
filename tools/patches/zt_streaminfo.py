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

patch('src/cli/commands.cpp', [
 ('''			command{"havokinfo", "havokinfo <file with Havok tagfile data> [--items]", havok_info},
''', '''			command{"havokinfo", "havokinfo <file with Havok tagfile data> [--items]", havok_info},
			command{"streaminfo", "streaminfo <zone> [<out.json>]", stream_info},
'''),
])
patch('src/cli/commands.hpp', [
 ('''	void havok_info(const settings& settings, std::span<const std::string> args);
''', '''	void havok_info(const settings& settings, std::span<const std::string> args);
	void stream_info(const settings& settings, std::span<const std::string> args);
'''),
])
patch('src/cli/zone_commands.cpp', [
 ('''	// Lists the objects of a Havok tagfile (the shape data of a collision map, a collision tile or a physics asset,
''', '''	// Writes the streaming info of a map zone as JSON: the transient sets, the streamed (transient) files with their
	// costs, the transient assets, the world grid tile maps and the counts (IW8 Radiant research 90). The game takes
	// the collision tile count and the names of the collision tile zones from this asset
	// (CL_TransientsCollisionMP_SetupFromAsset / RegisterFileIndex).
	void stream_info(const settings& settings, const std::span<const std::string> args)
	{
		if (settings.game->name != "iw8" || args.empty())
		{
			std::cout << "usage: -game iw8 streaminfo <zone> [<out.json>]\\n";
			return;
		}
		try
		{
			using namespace zonetool::iw8;
			const auto path = game_files::find_zone(args[0]);
			if (!path)
			{
				throw std::runtime_error("zone " + args[0] + " was not found");
			}
			const auto zone = load_zone(*path, {.keep_temp_data = true});
			const StreamingInfo* info = nullptr;
			for (const auto& asset : zone->assets)
			{
				if (asset.type == ASSET_TYPE_STREAMING_INFO && !asset.referenced)
				{
					info = static_cast<const StreamingInfo*>(asset.header);
				}
			}
			nlohmann::ordered_json out;
			out["zone"] = args[0];
			if (!info)
			{
				out["streaminginfo"] = nullptr;
			}
			else
			{
				out["name"] = info->name ? info->name : "";
				const auto& set = info->alwaysloadedFlagSets[0];
				out["alwaysloaded"] = {{"modelCount", set.modelCount}, {"modelSurfCount", set.modelSurfCount}};
				if (const auto* t = info->transientInfo)
				{
					auto& j = out["transientInfo"];
					j["flags"] = static_cast<std::uint32_t>(t->transientInfoFlags);
					j["forcedStubAssetCount"] = t->forcedStubAssetCount;
					j["transientFileCount"] = t->transientFileCount;
					j["transientAssetCount"] = t->transientAssetCount;
					j["transientWorldGridTileMapCount"] = t->transientWorldGridTileMapCount;
					j["transientCollisionTileCount"] = t->transientCollisionTileCount;
					j["transientMPCommonCount"] = t->transientMPCommonCount;
					j["transientWorldGridBoundsCount"] = t->transientWorldGridBoundsCount;
					j["transientWorldGridHighLodCount"] = t->transientWorldGridHighLodCount;
					j["transientWorldGridHighLodLookupCount"] = t->transientWorldGridHighLodLookupCount;
					j["skipLoading"] = {{"wordCount", t->transientSkipLoading.wordCount}, {"bitCount", t->transientSkipLoading.bitCount}};
					auto sets = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; i < t->transientSets.count; i++)
					{
						sets.push_back({{"name", t->transientSets.names && t->transientSets.names[i] ? t->transientSets.names[i] : ""},
							{"mask", t->transientSets.masks ? t->transientSets.masks[i].array[0] : 0}});
					}
					j["transientSets"] = sets;
					const auto& costs = t->transientCosts;
					j["costs"] = {{"streamingItemCount", costs.streamingItemCount}, {"transientCostCount", costs.transientCostCount}};
					auto files = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; i < t->transientFileCount; i++)
					{
						nlohmann::ordered_json file;
						file["name"] = t->transientFiles[i].name ? t->transientFiles[i].name : "";
						file["type"] = t->transientFiles[i].type;
						if (i < costs.transientCostCount)
						{
							if (costs.residentPageSizes) file["residentPages"] = costs.residentPageSizes[i];
							if (costs.tempPageSizes) file["tempPages"] = costs.tempPageSizes[i];
						}
						files.push_back(std::move(file));
					}
					j["transientFiles"] = files;
					auto stream_costs = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; costs.streamingCosts && i < costs.streamingItemCount && i < 64; i++)
					{
						stream_costs.push_back(costs.streamingCosts[i].costInBytes);
					}
					j["streamingCostsHead"] = stream_costs;
					auto content_counts = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; costs.streamContents.contentCounts && i < costs.transientCostCount && i < 4096; i++)
					{
						content_counts.push_back(costs.streamContents.contentCounts[i]);
					}
					j["contentCounts"] = content_counts;
					auto assets = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; i < t->transientAssetCount; i++)
					{
						assets.push_back({{"name", t->transientAssets[i].name ? t->transientAssets[i].name : ""},
							{"type", t->transientAssets[i].type}, {"mpParentIndex", t->transientAssets[i].mpParentIndex}});
					}
					j["transientAssets"] = assets;
					auto stubs = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; i < t->forcedStubAssetCount; i++)
					{
						stubs.push_back({{"name", t->forcedStubAssets[i].name ? t->forcedStubAssets[i].name : ""}, {"type", t->forcedStubAssets[i].type}});
					}
					j["forcedStubAssets"] = stubs;
					auto maps = nlohmann::ordered_json::array();
					for (std::uint32_t i = 0; i < t->transientWorldGridTileMapCount; i++)
					{
						const auto& m = t->transientWorldGridTileMaps[i];
						nlohmann::ordered_json map = {{"indexCount", m.indexCount}, {"level", m.level}, {"lodCount", m.lodCount},
							{"gridCountX", m.gridCountX}, {"gridCountY", m.gridCountY}, {"cellSize", m.cellSize}, {"minX", m.minX}, {"minY", m.minY}};
						auto head = nlohmann::ordered_json::array();
						for (std::uint32_t k = 0; m.spaceToTransientIndex && k < m.indexCount && k < 48; k++)
						{
							head.push_back(m.spaceToTransientIndex[k]);
						}
						map["indicesHead"] = head;
						maps.push_back(std::move(map));
					}
					j["worldGridTileMaps"] = maps;
				}
				else
				{
					out["transientInfo"] = nullptr;
				}
			}
			const auto text = out.dump(1);
			if (args.size() > 1)
			{
				std::ofstream(std::filesystem::path(args[1]), std::ios::binary) << text;
				std::cout << "streaminfo: wrote " << args[1] << "\\n";
			}
			else
			{
				std::cout << text << "\\n";
			}
		}
		catch (const std::exception& e)
		{
			std::cout << std::format("streaminfo failed: {}\\n", e.what());
			std::exit(1);
		}
	}

	// Lists the objects of a Havok tagfile (the shape data of a collision map, a collision tile or a physics asset,
'''),
])
print('ok')
