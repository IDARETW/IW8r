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

rep('''	//   "entities": path of an IW8 entity text (numeric keys, as the game stores it); "rebuild_spawns" (default true)
	//       also rebuilds the compiled spawn list from it, as the game expects the two to agree;
''', '''	//   "entities": path of an IW8 entity text (numeric keys, as the game stores it). A stock map keeps its spawn
	//       points only in a compiled list, not in the text; the editor shows them as mp_*_spawn* entities. When the
	//       text has such entities they become the compiled list (in file order) and are taken out of the text;
	//       when it has none the stock list stays. "rebuild_spawns": false keeps the stock list in every case;
''')
rep('''			report["entity_chars_before"] = ents->numEntityChars;
			ents->entityString = entity_text.data(); // c_str: the terminator is part of numEntityChars
			ents->numEntityChars = static_cast<std::int32_t>(entity_text.size() + 1);
			report["entity_chars"] = ents->numEntityChars;

			if (edits.value("rebuild_spawns", true))
			{
				// The compiled spawn list: one record per mp_*_spawn* entity in file order, as the stock list is (checked:
				// the records rebuilt from the stock text of mp_shipment equal the stock records).
				const ::iw8::ReplaySpawns spawns(entity_text);
''', '''			report["entity_chars_before"] = ents->numEntityChars;
			const ::iw8::ReplaySpawns spawns(entity_text);
			const auto rebuild_spawns = edits.value("rebuild_spawns", true) && !spawns.records.empty();
			if (rebuild_spawns)
			{
				// The spawn entities leave the text, as in a stock map (the same test as ReplaySpawns: classname key 212
				// starting with mp_ and holding _spawn).
				static const std::regex entity(R"(\\{([^{}]*)\\}\\r?\\n?)");
				static const std::regex classname(R"re((?:^|\\n)212\\s+"([^"]*)")re");
				std::string kept;
				kept.reserve(entity_text.size());
				std::size_t last = 0, removed = 0;
				for (auto match = std::sregex_iterator(entity_text.begin(), entity_text.end(), entity);
					match != std::sregex_iterator(); ++match)
				{
					const auto body = (*match)[1].str();
					std::smatch name;
					const auto spawn = std::regex_search(body, name, classname) && name[1].str().compare(0, 3, "mp_") == 0 &&
						name[1].str().find("_spawn") != std::string::npos;
					if (!spawn)
					{
						continue;
					}
					kept.append(entity_text, last, static_cast<std::size_t>(match->position()) - last);
					last = static_cast<std::size_t>(match->position() + match->length());
					++removed;
				}
				kept.append(entity_text, last, std::string::npos);
				entity_text = std::move(kept);
				report["spawn_entities_taken_out"] = removed;
			}
			ents->entityString = entity_text.data(); // c_str: the terminator is part of numEntityChars
			ents->numEntityChars = static_cast<std::int32_t>(entity_text.size() + 1);
			report["entity_chars"] = ents->numEntityChars;

			if (rebuild_spawns)
			{
				// The compiled spawn list: one record per mp_*_spawn* entity in file order (checked: the records rebuilt
				// from the stock spawns of mp_shipment written as entities equal the stock records).
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
