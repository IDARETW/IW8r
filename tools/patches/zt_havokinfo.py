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
 ('''			command{"patchzone", "patchzone <base.ff> <new.ff> [<out.fp>]", patch_zone},
''', '''			command{"patchzone", "patchzone <base.ff> <new.ff> [<out.fp>]", patch_zone},
			command{"havokinfo", "havokinfo <file with Havok tagfile data> [--items]", havok_info},
'''),
])
patch('src/cli/commands.hpp', [
 ('''	void edit_zone(const settings& settings, std::span<const std::string> args);
''', '''	void edit_zone(const settings& settings, std::span<const std::string> args);
	void havok_info(const settings& settings, std::span<const std::string> args);
'''),
])
patch('src/cli/zone_commands.cpp', [
 ('''	void edit_zone(const settings& settings, const std::span<const std::string> args)
	{
		constexpr std::string_view usage = "editzone <zone> <edits.json> <output folder>";
''', '''	// Lists the objects of a Havok tagfile (the shape data of a collision map, a collision tile or a physics asset,
	// saved as a file): per type the number of items, elements and bytes; with --items every item with its pointers.
	void havok_info(const settings& settings, const std::span<const std::string> args)
	{
		if (settings.game->name != "iw8" || args.empty())
		{
			std::cout << "usage: -game iw8 havokinfo <file with Havok tagfile data> [--items]\\n";
			return;
		}
		try
		{
			using namespace zonetool::iw8::havok;
			std::ifstream stream(std::filesystem::path(args[0]), std::ios::binary);
			if (!stream)
			{
				throw std::runtime_error("cannot read " + args[0]);
			}
			const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			const auto& compendium = replay_compendium(settings.zone_folder());
			const auto file = read_tagfile(data, &compendium);
			const auto& types = file.types ? *file.types : compendium;
			const auto listed = std::ranges::find(args, std::string("--items")) != args.end();
			std::cout << std::format("{}: {} bytes, sdk {}, {}, {} items\\n", args[0], data.size(), file.sdk_version,
				file.compendium ? "compact (compendium types)" : "self-describing", file.items.size());
			struct total
			{
				std::size_t items{}, elements{}, bytes{};
			};
			std::map<std::string, total> totals;
			for (std::size_t index = 1; index < file.items.size(); index++)
			{
				const auto& entry = file.items[index];
				const auto name = types.identity(entry.type);
				auto& sum = totals[name];
				sum.items++;
				sum.elements += entry.count;
				sum.bytes += entry.data.size();
				if (listed || index == 1)
				{
					std::string targets;
					for (std::size_t p = 0; p < entry.pointers.size() && p < 12; p++)
					{
						targets += std::format(" +0x{:X}->{}", entry.pointers[p].offset, entry.pointers[p].target);
					}
					std::cout << std::format("  item {} {} x{} ({} bytes, flags {}, {} pointers{})\\n", index, name, entry.count,
						entry.data.size(), entry.flags, entry.pointers.size(), targets);
				}
			}
			for (const auto& [name, sum] : totals)
			{
				std::cout << std::format("  {:>8} items {:>10} elements {:>12} bytes  {}\\n", sum.items, sum.elements, sum.bytes, name);
			}
		}
		catch (const std::exception& e)
		{
			std::cout << std::format("havokinfo failed: {}\\n", e.what());
			std::exit(1);
		}
	}

	void edit_zone(const settings& settings, const std::span<const std::string> args)
	{
		constexpr std::string_view usage = "editzone <zone> <edits.json> <output folder>";
'''),
])
print('ok')
