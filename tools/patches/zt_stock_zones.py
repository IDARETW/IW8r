import io

# Converter (x64-zt worktree): editzone "stock_zones". An edited stock zone is installed in the game as a whole
# unsigned zone file in place of the stock one (research 116: a patch of a signed zone must carry the game's own
# signature), so the stock file lives elsewhere while an edit is installed. "stock_zones" names, per zone file of the
# game's zone folder, the file to read instead; the patch beside that file is the one applied.

ROOT = 'C:/zsw-iw8r/scriptables/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/xfile/xfile.hpp', '''	void use_patch_file(const std::filesystem::path& zone_path, const std::filesystem::path& patch_path);
''', '''	void use_patch_file(const std::filesystem::path& zone_path, const std::filesystem::path& patch_path);
	// Names the file that a zone path is read from, in place of the file at that path; the patch beside the named file
	// is the one applied. For a stock zone that an installed edit replaced in the game folder: the stock file kept
	// elsewhere gives the stock data again (IW8 Radiant research 116).
	void use_zone_file(const std::filesystem::path& zone_path, const std::filesystem::path& file);
''')

X = 'src/core/xfile/xfile.cpp'
patch(X, '''		std::filesystem::path override_key(const std::filesystem::path& zone_path)
''', '''		std::map<std::filesystem::path, std::filesystem::path>& zone_overrides()
		{
			static std::map<std::filesystem::path, std::filesystem::path> overrides;
			return overrides;
		}

		std::filesystem::path override_key(const std::filesystem::path& zone_path)
''')
patch(X, '''	fastfile read(const std::filesystem::path& path, const format& format, const bool apply_patch)
	{
		std::string data;
		if (!game_files::read(path, data))
		{
			throw std::runtime_error("cannot read file");
		}

		return read(path, std::move(data), format, apply_patch);
	}
''', '''	void use_zone_file(const std::filesystem::path& zone_path, const std::filesystem::path& file)
	{
		zone_overrides()[override_key(zone_path)] = file;
	}

	fastfile read(const std::filesystem::path& path, const format& format, const bool apply_patch)
	{
		auto source = path;
		if (const auto named = zone_overrides().find(override_key(path)); named != zone_overrides().end())
		{
			source = named->second;
		}

		std::string data;
		if (!game_files::read(source, data))
		{
			throw std::runtime_error("cannot read file");
		}

		return read(source, std::move(data), format, apply_patch);
	}
''')

C = 'src/convert/iw3_iw8/convert.cpp'
patch(C, '''	//       Without the key every zone is read with the patch beside it, as the game reads it.
''', '''	//       Without the key every zone is read with the patch beside it, as the game reads it.
	//   "stock_zones": {"<zone>.ff": "<file>"} names, per zone file of the game's zone folder, the file to read in its
	//       place (with the patch beside that file). The game takes a patch of a signed zone only with its own
	//       signature, so an edit is installed as a whole unsigned zone in place of the stock file, which is kept
	//       elsewhere: named here, the edit starts from the stock zone again (IW8 Radiant research 116).
''')
patch(C, '''		auto zone = load_zone(zone_path, {.keep_temp_data = true});
		const auto find = [&](const std::int32_t type) -> void*
''', '''		if (edits.contains("stock_zones"))
		{
			const auto& named = edits.at("stock_zones");
			if (!named.is_object())
			{
				throw std::runtime_error("stock_zones must be an object: zone file name -> stock zone file");
			}
			auto list = nlohmann::ordered_json::array();
			for (const auto& [name, value] : named.items())
			{
				const std::filesystem::path file_name(name);
				if (!value.is_string() || file_name.has_parent_path() || file_name.extension() != ".ff")
				{
					throw std::runtime_error("stock_zones: \\"" + name + "\\" must be a zone file name (<zone>.ff) with a file path");
				}
				const auto file = value.get<std::string>();
				if (file.empty() || !std::filesystem::exists(std::filesystem::path(file)))
				{
					throw std::runtime_error("stock_zones: the stock zone " + file + " was not found");
				}
				zonetool::xfile::use_zone_file(zone_folder / file_name, std::filesystem::path(file));
				list.push_back(name);
			}
			report["stock_zones"] = list;
		}

		auto zone = load_zone(zone_path, {.keep_temp_data = true});
		const auto find = [&](const std::int32_t type) -> void*
''')
print('ok')
