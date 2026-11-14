import io
import re

# x64-zt converter (C:\zsw-iw8r\scriptables): editzone "lit_filter" (IW8 Radiant plan M6, custom shaders on stock maps).
#
#   "lit_filter": {"program": "<compiled filter pixel shader>", "shared": ["<zone>", ...]}
#
# Every lit technique of the map's technique sets (zone techsets_<map>) gets its pixel shader with the filter spliced
# in before its end: the game's own lighting stays, the final colour passes through the filter. The shaders get new
# names, the techniques new names and pipeline-state hashes, the technique sets new names (research 114 section 7), and
# the map zone's materials name the new sets. "shared" lists zones to take a shader's program from when the map's
# techset zone only names it.
#
# The splicer is IW8 Radiant's src/core/dxbc_splice.{h,cpp}, copied here as one header.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


ROOT = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/'

# ---- the splicer as a header
h = io.open('E:/IW8-Radiant/src/core/dxbc_splice.h', encoding='utf-8').read()
c = io.open('E:/IW8-Radiant/src/core/dxbc_splice.cpp', encoding='utf-8').read()
h = h.replace('#pragma once\n', '')
c = c.replace('#include "dxbc_splice.h"\n', '')
for name in ('std::array<uint8_t, 16> dxbcChecksum(', 'bool dxbcChecksumValid(', 'bool dxbcInspect(', 'bool dxbcSpliceFilter('):
    # the definitions (in the .cpp part) become inline
    i = c.index('\n' + name)
    c = c[:i + 1] + 'inline ' + c[i + 1:]
header = ('// Copied from IW8 Radiant (src/core/dxbc_splice.h and .cpp) by tools/patches/zt_lit_filter.py: do not edit here.\n'
          '#pragma once\n' + h + '\n' + c)
io.open(ROOT + 'dxbc_splice.hpp', 'w', encoding='utf-8', newline='\n').write(header)

C = ROOT + 'convert.cpp'
patch(C, '''#include "stock_world_remove.h"
''', '''#include "stock_world_remove.h"
#include "dxbc_splice.hpp"
''')

patch(C, '''		const write_options options{.expected_size = zone->file.payload.size() + zone->file.payload.size() / 16};
		std::filesystem::create_directories(output);''', '''		// "lit_filter": {"program": "<compiled filter pixel shader>", "shared": ["<zone>", ...]}
		//   IW8 Radiant's map shader filter on a stock map: every lit technique of the map's technique sets (zone
		//   techsets_<map>) gets its pixel shader with the filter's instructions spliced in before its end
		//   (dxbc_splice.hpp), so the game's own lighting stays and its final colour passes through the filter. What
		//   changes gets a new identity (research 114 section 7): the shader a new name, the technique a new name and
		//   new pipeline-state hashes, the technique set a new name; the materials of this zone then name the new sets.
		//   A shader the techset zone only names (its program is in a shared zone) is looked up in the "shared" zones
		//   and added to the techset zone under its new name.
		std::unique_ptr<loaded_zone> filtered_techsets;
		std::vector<std::unique_ptr<loaded_zone>> filter_shared_zones;
		std::deque<std::vector<std::uint8_t>> filter_programs;
		std::deque<ComputeShader> filter_shaders;
		std::vector<zone_asset> filter_additions;
		if (edits.contains("lit_filter"))
		{
			const auto& node = edits.at("lit_filter");
			if (!node.is_object() || !node.contains("program") || !node.at("program").is_string())
			{
				throw std::runtime_error("lit_filter: needs \\"program\\", the compiled filter pixel shader file");
			}
			std::vector<std::uint8_t> filter;
			{
				std::ifstream file(std::filesystem::path(node.at("program").get<std::string>()), std::ios::binary);
				if (!file)
				{
					throw std::runtime_error("lit_filter: cannot read " + node.at("program").get<std::string>());
				}
				filter.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			}
			if (!iw8r::dxbcChecksumValid(filter))
			{
				throw std::runtime_error("lit_filter: the program is not a DXBC shader with a valid checksum");
			}
			const auto techsets_path = zone_folder / ("techsets_" + zone_name + ".ff");
			if (!std::filesystem::exists(techsets_path))
			{
				throw std::runtime_error("stock zone " + techsets_path.string() + " was not found");
			}
			filtered_techsets = load_zone(techsets_path, {.keep_temp_data = true});

			const auto hex = [](const std::uint8_t* bytes, const std::size_t count)
			{
				static const char digits[] = "0123456789abcdef";
				std::string text;
				for (std::size_t i = 0; i < count; i++)
				{
					text += digits[bytes[i] >> 4];
					text += digits[bytes[i] & 15];
				}
				return text;
			};
			const auto fnv = [](const std::string& text, std::uint64_t seed)
			{
				std::uint64_t value = 1469598103934665603ull ^ seed;
				for (const unsigned char byte : text)
				{
					value = (value ^ byte) * 1099511628211ull;
				}
				return value;
			};
			const auto hex64 = [&](const std::uint64_t value)
			{
				std::uint8_t bytes[8];
				for (int i = 0; i < 8; i++)
				{
					bytes[i] = static_cast<std::uint8_t>(value >> (56 - 8 * i));
				}
				return hex(bytes, 8);
			};
			const std::string filter_id = hex(filter.data() + 4, 16);

			// A shader's program by name from the shared zones (loaded on first use, in the order given).
			std::vector<std::string> shared_names;
			if (node.contains("shared"))
			{
				for (const auto& entry : node.at("shared"))
				{
					shared_names.push_back(entry.get<std::string>());
				}
			}
			std::map<std::string, const ComputeShader*> shared_shaders;
			std::size_t shared_loaded = 0;
			const auto shared_program = [&](const std::string& name) -> const ComputeShader*
			{
				for (;;)
				{
					if (const auto found = shared_shaders.find(name); found != shared_shaders.end())
					{
						return found->second;
					}
					if (shared_loaded >= shared_names.size())
					{
						return nullptr;
					}
					const auto path = zone_folder / (shared_names[shared_loaded++] + ".ff");
					if (!std::filesystem::exists(path))
					{
						continue;
					}
					filter_shared_zones.push_back(load_zone(path, {.keep_temp_data = true}));
					for (const auto& asset : filter_shared_zones.back()->assets)
					{
						if (asset.type != ASSET_TYPE_PIXELSHADER || asset.referenced)
						{
							continue;
						}
						const auto* shader = static_cast<const ComputeShader*>(asset.header);
						if (shader->name && *shader->name != ',' && shader->prog.loadDef.program && shader->prog.loadDef.programSize)
						{
							shared_shaders.try_emplace(shader->name, shader);
						}
					}
				}
			};

			const auto lit = [](const MaterialTechniqueType type)
			{
				const auto value = static_cast<std::uint32_t>(type);
				return value == 34 || value == 35 || value == 36 || value == 151;  // LIT_FORWARDPLUS_BITMASK, its VRS forms, WORLDTESS
			};
			std::map<const ComputeShader*, ComputeShader*> filtered;  // a stock shader -> its filtered one (nullptr: not possible)
			std::set<const MaterialTechnique*> done_techniques;
			std::set<const MaterialPipelineState*> done_states;
			std::map<std::string, std::string> renamed_sets;
			std::size_t techniques = 0, shaders = 0, shaders_shared = 0, shaders_missing = 0, shaders_refused = 0, sets = 0;
			std::string first_refusal;
			for (auto& asset : filtered_techsets->assets)
			{
				if (asset.type != ASSET_TYPE_TECHNIQUE_SET || asset.referenced)
				{
					continue;
				}
				auto* set = static_cast<MaterialTechniqueSet*>(asset.header);
				if (!set->name || *set->name == ',' || !set->maskedTechniques)
				{
					continue;
				}
				std::size_t count = 0;
				for (const auto word : set->techniqueMask.mask)
				{
					count += static_cast<std::size_t>(std::popcount(word));
				}
				bool changed = false;
				for (std::size_t i = 0; i < count; i++)
				{
					auto* technique = set->maskedTechniques[i];
					if (!technique || !lit(technique->type) || !technique->pixelShader || !technique->pixelShader->name)
					{
						continue;
					}
					if (!done_techniques.insert(technique).second)
					{
						changed = changed || filtered.contains(technique->pixelShader);
						continue;
					}
					auto* shader = technique->pixelShader;
					ComputeShader* fresh = nullptr;
					if (const auto known = filtered.find(shader); known != filtered.end())
					{
						fresh = known->second;
					}
					else
					{
						std::string plain = shader->name;
						const bool named_only = !plain.empty() && plain.front() == ',';
						if (named_only)
						{
							plain.erase(0, 1);
						}
						const ComputeShader* source = shader;
						if (named_only || !shader->prog.loadDef.program || !shader->prog.loadDef.programSize)
						{
							source = shared_program(plain);
						}
						std::vector<std::uint8_t> spliced;
						std::string error;
						if (!source)
						{
							++shaders_missing;
						}
						else if (!iw8r::dxbcSpliceFilter({source->prog.loadDef.program, source->prog.loadDef.program + source->prog.loadDef.programSize},
							filter, spliced, &error))
						{
							++shaders_refused;
							if (first_refusal.empty())
							{
								first_refusal = plain + ": " + error;
							}
						}
						else
						{
							filter_programs.push_back(std::move(spliced));
							auto& program = filter_programs.back();
							names.push_back("iw8rf_" + hex(program.data() + 4, 13));
							if (source == shader)
							{
								fresh = shader;  // owned by the techset zone: changed in place
							}
							else
							{
								filter_shaders.push_back(*source);
								fresh = &filter_shaders.back();
								filter_additions.push_back({ASSET_TYPE_PIXELSHADER, fresh, false});
								++shaders_shared;
							}
							fresh->name = names.back().data();
							fresh->prog.cs = nullptr;
							fresh->prog.loadDef.program = program.data();
							fresh->prog.loadDef.programSize = static_cast<std::uint32_t>(program.size());
							// The 4-byte identity after the program size (research 114 section 7.3).
							std::memcpy(reinterpret_cast<std::uint8_t*>(fresh) + 36, program.data() + 4, 4);
							++shaders;
						}
						filtered[shader] = fresh;
						if (fresh)
						{
							filtered[fresh] = fresh;
						}
					}
					if (!fresh)
					{
						continue;
					}
					const std::string old_name = technique->name ? technique->name : "TECHNIQUE";
					technique->pixelShader = fresh;
					// TECHNIQUE_..._<16 hex>: the hash part is replaced; any other name gets one appended.
					std::string stem = old_name;
					if (stem.size() > 17 && stem[stem.size() - 17] == '_' &&
						stem.find_first_not_of("0123456789abcdef", stem.size() - 16) == std::string::npos)
					{
						stem.erase(stem.size() - 17);
					}
					names.push_back(stem + "_" + hex64(fnv(old_name + ";" + fresh->name, 1)));
					technique->name = names.back().data();
					const std::size_t states = std::size_t{technique->pipelineStateCount} * technique->pipelineStateInputLayoutCount;
					if (technique->pipelineStates && done_states.insert(technique->pipelineStates).second)
					{
						for (std::size_t state = 0; state < states; state++)
						{
							auto& entry = technique->pipelineStates[state];
							entry.hash = fnv(std::to_string(entry.hash) + ";" + fresh->name + ";" + filter_id, 2 + state);
							entry.pso = nullptr;
						}
					}
					changed = true;
					++techniques;
				}
				if (changed)
				{
					const std::string old_name = set->name;
					const auto slash = old_name.find('/');
					names.push_back((slash == std::string::npos ? std::string() : old_name.substr(0, slash + 1)) + "iw8rf_" +
						hex64(fnv(old_name + ";" + filter_id, 3)) + hex64(fnv(old_name + ";" + filter_id, 4)).substr(0, 8));
					set->name = names.back().data();
					renamed_sets.emplace(old_name, names.back());
					++sets;
				}
			}

			// The materials of this zone name the new sets.
			std::set<const void*> renamed_references;
			std::size_t materials = 0;
			for (const auto& asset : zone->assets)
			{
				if (asset.type != ASSET_TYPE_MATERIAL || asset.referenced)
				{
					continue;
				}
				auto* material = static_cast<Material*>(asset.header);
				if (!material->techniqueSet || !material->techniqueSet->name)
				{
					continue;
				}
				std::string plain = material->techniqueSet->name;
				const bool comma = !plain.empty() && plain.front() == ',';
				if (comma)
				{
					plain.erase(0, 1);
				}
				const auto found = renamed_sets.find(plain);
				if (found == renamed_sets.end())
				{
					// Already renamed through another material of the same reference?
					if (renamed_references.contains(material->techniqueSet))
					{
						++materials;
					}
					continue;
				}
				names.push_back((comma ? "," : "") + found->second);
				material->techniqueSet->name = names.back().data();
				renamed_references.insert(material->techniqueSet);
				++materials;
			}
			report["lit_filter"] = {
				{"filter", filter_id},
				{"technique_sets", sets},
				{"techniques", techniques},
				{"shaders", shaders},
				{"shaders_from_shared_zones", shaders_shared},
				{"shaders_without_program", shaders_missing},
				{"shaders_refused", shaders_refused},
				{"first_refusal", first_refusal},
				{"materials_renamed", materials},
				{"shared_zones_read", filter_shared_zones.size()},
			};
			if (!techniques)
			{
				throw std::runtime_error("lit_filter: no lit technique of techsets_" + zone_name + " could take the filter" +
					(first_refusal.empty() ? std::string() : " (" + first_refusal + ")"));
			}
		}

		const write_options options{.expected_size = zone->file.payload.size() + zone->file.payload.size() / 16};
		std::filesystem::create_directories(output);''')

patch(C, '''		if (!carry.techsets.empty())
		{
			const auto techsets_path = zone_folder / ("techsets_" + zone_name + ".ff");
			if (!std::filesystem::exists(techsets_path))
			{
				throw std::runtime_error("stock zone " + techsets_path.string() + " was not found");
			}
			const auto techsets = load_zone(techsets_path, {.keep_temp_data = true});
			const write_options techset_options{};
			report["techset_assets_added"] = write(*techsets, carry.techsets, "techsets_" + zone_name, techset_options);
		}''', '''		if (!carry.techsets.empty() || filtered_techsets)
		{
			const auto techsets_path = zone_folder / ("techsets_" + zone_name + ".ff");
			if (!std::filesystem::exists(techsets_path))
			{
				throw std::runtime_error("stock zone " + techsets_path.string() + " was not found");
			}
			// The techset zone as the lit filter changed it, or as shipped.
			const auto techsets = filtered_techsets ? std::move(filtered_techsets) : load_zone(techsets_path, {.keep_temp_data = true});
			auto additions = carry.techsets;
			additions.insert(additions.end(), filter_additions.begin(), filter_additions.end());
			const write_options techset_options{.expected_size = techsets->file.payload.size() + techsets->file.payload.size() / 4};
			report["techset_assets_added"] = write(*techsets, additions, "techsets_" + zone_name, techset_options);
		}''')
print('ok')
