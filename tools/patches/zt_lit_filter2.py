import io

# x64-zt converter: editzone "lit_filter" follow-up. "techsets_only": true leaves the map zone unwritten (a filter alone
# changes nothing in it: the materials live in the techset zone and point at the sets directly); the report names the
# shaders whose program no listed zone holds.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/convert.cpp'
patch(C, '''			std::size_t techniques = 0, shaders = 0, shaders_shared = 0, shaders_missing = 0, shaders_refused = 0, sets = 0;
			std::string first_refusal;''', '''			std::size_t techniques = 0, shaders = 0, shaders_shared = 0, shaders_missing = 0, shaders_refused = 0, sets = 0;
			std::string first_refusal;
			auto missing_names = nlohmann::ordered_json::array();''')
patch(C, '''						if (!source)
						{
							++shaders_missing;
						}''', '''						if (!source)
						{
							++shaders_missing;
							if (missing_names.size() < 32)
							{
								missing_names.push_back(plain);
							}
						}''')
patch(C, '''				{"shaders_without_program", shaders_missing},''', '''				{"shaders_without_program", shaders_missing},
				{"shaders_without_program_names", missing_names},''')
patch(C, '''		report["assets_added"] = write(*zone, carry.assets, zone_name, options);
''', '''		// "techsets_only": true: the map zone is not written (an edit that changes only the techset zone, a lit filter).
		if (!edits.value("techsets_only", false))
		{
			report["assets_added"] = write(*zone, carry.assets, zone_name, options);
		}
''')
print('ok')
