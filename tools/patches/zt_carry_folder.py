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

rep('''		if (edits.contains("carry"))
		{
			std::map<std::string, std::set<AssetKey>> wanted;
			std::set<std::string> models;
			for (const auto& entry : edits.at("carry"))
			{
''', '''		if (edits.contains("carry"))
		{
			// Per source folder (the game's zone folder, or the "folder" of an entry: a zone the editor built, such as
			// the brushes added to a stock map, research 88), the zones and the assets to take from them.
			std::map<std::filesystem::path, std::map<std::string, std::set<AssetKey>>> wanted_by_folder;
			std::set<std::string> models;
			for (const auto& entry : edits.at("carry"))
			{
				auto& wanted = wanted_by_folder[entry.contains("folder") ? std::filesystem::path(entry.at("folder").get<std::string>()) : zone_folder];
'''),
rep('''			carry = CarryStockAssets(wanted, zone_folder);
''', '''			for (auto& [folder, wanted] : wanted_by_folder)
			{
				if (!std::filesystem::is_directory(folder))
				{
					throw std::runtime_error("carry: the zone folder " + folder.string() + " was not found");
				}
				auto part = CarryStockAssets(wanted, folder);
				carry.assets.insert(carry.assets.end(), part.assets.begin(), part.assets.end());
				carry.techsets.insert(carry.techsets.end(), part.techsets.begin(), part.techsets.end());
				carry.definitions.insert(part.definitions.begin(), part.definitions.end());
				carry.roots.insert(part.roots.begin(), part.roots.end());
				carry.materialNames.insert(part.materialNames.begin(), part.materialNames.end());
				for (auto& zone_in_memory : part.zones)
				{
					carry.zones.push_back(std::move(zone_in_memory));
				}
			}
'''),
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
