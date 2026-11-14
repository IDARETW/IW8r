import io
import sys

# Converter: editzone "lit_filter" for a shared technique-set zone (IW8 Radiant research 128: the first-person weapon
# and hands use sets of techsets_global_stream_mp, which other zones name by name).
#   "keep_names": true   the technique sets keep their names and the zone's own shaders are not changed in place (a
#                        filtered copy under a new name is added), so every name another zone uses still resolves.
#                        Techniques still get a new name and new pipeline-state hashes.
#   "materials": "<regex>" only the sets of this zone's materials whose name matches (case-insensitive search) are
#                        filtered.
# Argument: the converter checkout (default the IW8 Radiant worktree).

BASE = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'
C = BASE + 'src/convert/iw3_iw8/convert.cpp'


def patch(a, b, count=1):
    s = io.open(C, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    io.open(C, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('''		//   argument is re-pointed to the scene depth (code image 0, "Frontend FloatZ") and the shader is spliced in
		//   screen mode with that register. A shader that cannot take it gets "program".''',
      '''		//   argument is re-pointed to the scene depth (code image 0, "Frontend FloatZ") and the shader is spliced in
		//   screen mode with that register. A shader that cannot take it gets "program".
		//   "keep_names" (optional, for a zone whose sets and shaders other zones name: research 128): the technique
		//   sets keep their names and a shader of the zone is not changed in place; its filtered copy is added under a
		//   new name. "materials" (optional): a regular expression; only the sets of this zone's materials whose name
		//   matches are filtered (the first-person weapon and hands: "_vm_|vm_").''')
patch('''			// The screen program: a filtered shader -> the register its scene depth is read at (absent: the plain filter).''',
      '''			const bool keep_names = node.value("keep_names", false);
			// "materials": the technique sets of the matching materials of the techset zone (null: every set).
			std::set<const MaterialTechniqueSet*> wanted_sets;
			const bool only_wanted = node.contains("materials") && node.at("materials").is_string() &&
				!node.at("materials").get<std::string>().empty();
			std::size_t materials_matched = 0;
			if (only_wanted)
			{
				const std::regex pattern(node.at("materials").get<std::string>(), std::regex::icase | std::regex::optimize);
				for (const auto& asset : filtered_techsets->assets)
				{
					if (asset.type != ASSET_TYPE_MATERIAL || asset.referenced)
					{
						continue;
					}
					const auto* material = static_cast<const Material*>(asset.header);
					if (material->name && material->techniqueSet && std::regex_search(material->name, pattern))
					{
						wanted_sets.insert(material->techniqueSet);
						++materials_matched;
					}
				}
				if (wanted_sets.empty())
				{
					throw std::runtime_error("lit_filter: no material of techsets_" + zone_name + " matches \\"materials\\"");
				}
			}
			// The screen program: a filtered shader -> the register its scene depth is read at (absent: the plain filter).''')
patch('''				auto* set = static_cast<MaterialTechniqueSet*>(asset.header);
				if (!set->name || *set->name == ',' || !set->maskedTechniques)
				{
					continue;
				}
				std::size_t count = 0;''', '''				auto* set = static_cast<MaterialTechniqueSet*>(asset.header);
				if (!set->name || *set->name == ',' || !set->maskedTechniques)
				{
					continue;
				}
				if (only_wanted && !wanted_sets.contains(set))
				{
					continue;
				}
				std::size_t count = 0;''')
patch('''							if (source == shader)
							{
								fresh = shader;  // owned by the techset zone: changed in place
							}''', '''							if (source == shader && !keep_names)
							{
								fresh = shader;  // owned by the techset zone: changed in place
							}''')
patch('''				if (changed)
				{
					const std::string old_name = set->name;''', '''				if (changed && keep_names)
				{
					++sets;  // the set keeps its name: other zones name it
				}
				else if (changed)
				{
					const std::string old_name = set->name;''')
patch('''				{"screen_program", !screen_filter.empty()},''', '''				{"keep_names", keep_names},
				{"materials_matched", materials_matched},
				{"sets_wanted", wanted_sets.size()},
				{"screen_program", !screen_filter.empty()},''')
print('ok')
