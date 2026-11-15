import io

# x64-zt (iw8radiant/scriptables), after zt_preload_depth.py: instead of refusing a zone whose list asset nests
# preloaded assets three deep inline, the writer moves the middle asset into the list just before that entry (as stock
# zones order a scriptable's models before it) and writes the zone again. The owner's mp_cinderrail: scriptable
# door > xmodel > physicsasset crashed Replay on load (0x140D97ADD); with the model listed first the door aliases it.
# The collecting pass records every such chain (it does not stop); the moves are applied together and the collecting
# pass runs again until none is left; the writing pass still refuses one.

ROOT = 'C:/zsw-iw8r/scriptables/'
P = 'src/games/iw8/zone/writer.cpp'


def patch(a, b, count=1):
    s = io.open(ROOT + P, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('''		class zone_assets_writer
		{
		public:''', '''		struct preload_link
		{
			std::string label;
			std::int32_t type;
			void* header;
		};

		// A list entry (its header `top`) whose preloaded assets nest deeper than Replay's preload mark stack (2): the
		// second asset of the chain (`type`, `header`) is to be listed just before it.
		struct preload_move
		{
			void* top;
			std::int32_t type;
			void* header;
			std::string chain;
		};

		class zone_assets_writer
		{
		public:''')
patch('''			std::vector<std::string> preload_chain_;''', '''			std::vector<preload_link> preload_chain_;
			void* list_header_ = nullptr;  // the header of the list entry being written
			std::vector<preload_move> preload_moves_;

		public:
			const std::vector<preload_move>& preload_moves() const
			{
				return this->preload_moves_;
			}

		private:''')
patch('''					for (auto& entry : entries)
					{
						this->write_list_asset(stream, entry);''', '''					for (auto& entry : entries)
					{
						this->list_header_ = entry.header;
						this->write_list_asset(stream, entry);''')
patch('''					preload_chain_.push_back(std::string(format->name) + " \\"" + key.second + "\\"");
					if (preload_chain_.size() > 2)
					{
						std::string chain;
						for (const auto& link : preload_chain_)
						{
							chain += (chain.empty() ? "" : " > ") + link;
						}
						throw std::runtime_error(std::format("preloaded assets nest {} deep inline ({}); the game holds 2 "
							"and crashes on load: write the inner asset as a list entry before its users",
							preload_chain_.size(), chain));
					}''', '''					preload_chain_.push_back({std::string(format->name) + " \\"" + key.second + "\\"", type, header});
					if (preload_chain_.size() > 2)
					{
						std::string chain;
						for (const auto& link : preload_chain_)
						{
							chain += (chain.empty() ? "" : " > ") + link.label;
						}
						if (!this->collect_)
						{
							throw std::runtime_error(std::format("preloaded assets nest {} deep inline ({}); the game holds "
								"2 and crashes on load", preload_chain_.size(), chain));
						}
						// Collecting: write_zone lists the middle asset before this entry and collects again.
						this->preload_moves_.push_back({this->list_header_, preload_chain_[1].type, preload_chain_[1].header, chain});
					}''')
patch('''		script_string_table strings(script_strings);
		{
			zone_assets_writer collector(assets, strings, options, true);
			collector.write(list);
		}

		zone_assets_writer writer(assets, strings, options, false);
		return writer.write(list);''', '''		// Replay's preload mark stack holds two nested preloaded assets (see write_asset). An entry that nests three deep
		// gets its middle asset listed just before it (it then aliases that one), as stock zones order a scriptable's
		// models before it; the collecting pass runs again until no chain is left.
		std::vector<zone_asset> ordered(list.begin(), list.end());
		for (std::size_t round = 0;; ++round)
		{
			script_string_table strings(script_strings);
			std::vector<preload_move> moves;
			{
				zone_assets_writer collector(assets, strings, options, true);
				collector.write(ordered);
				moves = collector.preload_moves();
			}

			if (moves.empty())
			{
				zone_assets_writer writer(assets, strings, options, false);
				return writer.write(ordered);
			}

			if (round >= 64)
			{
				throw std::runtime_error(std::format("preloaded assets still nest 3 deep after listing their middle assets "
					"first ({})", moves.front().chain));
			}

			std::size_t moved = 0;
			for (const auto& move : moves)
			{
				const auto find = [&](void* header, const std::int32_t type)
				{
					return std::find_if(ordered.begin(), ordered.end(), [&](const zone_asset& a)
					{
						return a.header == header && (type < 0 || a.type == type);
					});
				};
				auto top = find(move.top, -1);
				auto middle = find(move.header, move.type);
				if (top == ordered.end() || (middle != ordered.end() && middle < top))
				{
					continue;  // already listed before it (a later move did it), or no list entry to put it before
				}
				zone_asset entry{move.type, move.header, false};
				if (middle != ordered.end())
				{
					entry = *middle;
					ordered.erase(middle);
					top = find(move.top, -1);
				}
				ordered.insert(top, entry);
				++moved;
			}

			if (!moved)
			{
				throw std::runtime_error(std::format("preloaded assets nest 3 deep inline ({}); the game holds 2 and "
					"crashes on load, and listing the middle asset first is not possible", moves.front().chain));
			}

			printf("[info] zone writer: %zu asset(s) listed before their users (Replay preloads 2 levels deep)\\n", moved);
		}''')
print('ok')
