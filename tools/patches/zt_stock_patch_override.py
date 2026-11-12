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

patch('src/core/xfile/xfile.hpp', [
 ('''	fastfile read(const std::filesystem::path& path, const format& format);
''', '''	fastfile read(const std::filesystem::path& path, const format& format);
	// Names the Replay patch (.fp) that a zone file is read with, in place of the one beside it; an empty patch path
	// reads the zone without a patch. For a stock zone whose patch in the game folder was replaced by an edit: the
	// stock patch saved elsewhere gives the stock data again (IW8 Radiant research 88).
	void use_patch_file(const std::filesystem::path& zone_path, const std::filesystem::path& patch_path);
'''),
])
patch('src/core/xfile/xfile.cpp', [
 ('''		std::string apply_iw8_patch(const std::filesystem::path& zone_path, iw8_file_header& header, std::string payload)
		{
''', '''		std::map<std::filesystem::path, std::filesystem::path>& patch_overrides()
		{
			static std::map<std::filesystem::path, std::filesystem::path> overrides;
			return overrides;
		}

		std::filesystem::path override_key(const std::filesystem::path& zone_path)
		{
			std::error_code error;
			auto key = std::filesystem::weakly_canonical(zone_path, error);
			if (error)
			{
				key = zone_path.lexically_normal();
			}
			auto text = key.generic_string();
			for (auto& c : text)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return text;
		}

		std::string apply_iw8_patch(const std::filesystem::path& zone_path, iw8_file_header& header, std::string payload)
		{
'''),
 ('''			auto patch_path = zone_path;
			patch_path.replace_extension(".fp");

			std::string data;
			if (!game_files::read(patch_path, data))
			{
				return payload;
			}
''', '''			auto patch_path = zone_path;
			patch_path.replace_extension(".fp");

			std::string data;
			if (const auto named = patch_overrides().find(override_key(zone_path)); named != patch_overrides().end())
			{
				if (named->second.empty())
				{
					return payload;
				}
				patch_path = named->second;
				if (!game_files::read(patch_path, data))
				{
					throw std::runtime_error(std::format("the patch {} named for {} cannot be read", patch_path.string(),
						zone_path.filename().string()));
				}
			}
			else if (!game_files::read(patch_path, data))
			{
				return payload;
			}
'''),
 ('''	fastfile read(const std::filesystem::path& path, const format& format, const bool apply_patch)
	{
''', '''	void use_patch_file(const std::filesystem::path& zone_path, const std::filesystem::path& patch_path)
	{
		patch_overrides()[override_key(zone_path)] = patch_path;
	}

	fastfile read(const std::filesystem::path& path, const format& format, const bool apply_patch)
	{
'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''	// Output: <output>/<zone>.ff, techsets_<zone>.ff when it changed, <output>/patch/<same names>.fp (the patches that
	// turn the stock files into them; "patch": false leaves them out) and <zone>.edit-report.json.
	void edit_zone(const std::filesystem::path& game_path, const std::string& zone_name,
''', '''	//   "stock_patches": {"<zone>.fp": "<file>" | ""} names, per patch file of the game's zone folder, the stock patch
	//       to read that zone with ("" = the stock game has no patch of that name). The game ships patches with real
	//       map data (mp_br_quarry.fp, mp_donetsk.fp, ...), and an installed edit replaces the patch of its zone: with
	//       the saved stock patch named here the edit starts from the stock map again, not from the installed edit.
	//       Without the key every zone is read with the patch beside it, as the game reads it.
	// Output: <output>/<zone>.ff, techsets_<zone>.ff when it changed, <output>/patch/<same names>.fp (the patches that
	// turn the stock files into them; "patch": false leaves them out) and <zone>.edit-report.json.
	void edit_zone(const std::filesystem::path& game_path, const std::string& zone_name,
'''),
 ('''		nlohmann::ordered_json report;
		report["zone"] = zone_name;

		auto zone = load_zone(zone_path, {.keep_temp_data = true});
''', '''		nlohmann::ordered_json report;
		report["zone"] = zone_name;

		if (edits.contains("stock_patches"))
		{
			const auto& named = edits.at("stock_patches");
			if (!named.is_object())
			{
				throw std::runtime_error("stock_patches must be an object: patch file name -> stock patch file or \\"\\"");
			}
			auto list = nlohmann::ordered_json::array();
			for (const auto& [name, value] : named.items())
			{
				std::filesystem::path patch_name(name);
				if (!value.is_string() || patch_name.has_parent_path() || patch_name.extension() != ".fp")
				{
					throw std::runtime_error("stock_patches: \\"" + name + "\\" must be a patch file name (<zone>.fp) with a file path or \\"\\"");
				}
				const auto file = value.get<std::string>();
				if (!file.empty() && !std::filesystem::exists(std::filesystem::path(file)))
				{
					throw std::runtime_error("stock_patches: the stock patch " + file + " was not found");
				}
				auto zone_file = zone_folder / patch_name;
				zone_file.replace_extension(".ff");
				zonetool::xfile::use_patch_file(zone_file, std::filesystem::path(file));
				list.push_back(name + (file.empty() ? " (no stock patch)" : ""));
			}
			report["stock_patches"] = list;
		}

		auto zone = load_zone(zone_path, {.keep_temp_data = true});
'''),
])
print('ok')
