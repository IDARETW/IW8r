import io

# x64-zt (iw8radiant/scriptables), after zt_preload_hoist.py. The stack of two is DB_PatchMem's asset stack (named from
# the local_dw build's PDB: q_DB_PatchMem_PushAsset 0x140D97A80 records at [counter] without a bound check,
# DB_PatchMem_PostLoad_PushAsset 0x140D97860, DB_PatchMem_PopAsset 0x140D97850, DB_PatchMem_BeginLoad resets it,
# DB_PatchMem_TryDiscard checks counter < 2). Nearly every asset loader pushes it (Load_Material, Load_GfxImage,
# Load_XAnimParts, Load_ScriptableDef, q_Load_XModel, Load_PhysicsAsset, Load_MaterialTechniqueSet, the particle
# systems, ...: 210 call sites), not only the map-preloaded types. Count every asset written inline, of any type.

ROOT = 'C:/zsw-iw8r/scriptables/'
P = 'src/games/iw8/zone/writer.cpp'


def patch(a, b, count=1):
    s = io.open(ROOT + P, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('''				const auto preloaded = format->preload_block == preload_block;
				if (preloaded)
				{
					// Replay 1.20 keeps the preload marks of nested preloaded assets in a stack of two without a bound
					// check (push 0x140D97A80, stack 0x145DA4830, counter 0x145DA48B8): a third level overwrites the
					// counter and the next push faults (0xC0000005 at 0x140D97ADD). Refuse such a zone here.
					preload_chain_.push_back({std::string(format->name) + " \\"" + key.second + "\\"", type, header});
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
					}
					writer.push_stream(preload_block);''', '''				// Replay 1.20's loaders push every asset they load onto DB_PatchMem's asset stack, which holds two records
				// and has no bound check (q_DB_PatchMem_PushAsset 0x140D97A80 records at [counter]; stack 0x145DA4830,
				// counter 0x145DA48B8): an asset loaded inline in an inline asset of a list entry overwrites the counter
				// and the next push faults (0xC0000005 at 0x140D97ADD). Every type counts (210 loaders push, the
				// map-preloaded ones and the others alike). Refuse such a zone here; collecting, record it to move.
				preload_chain_.push_back({std::string(format->name) + " \\"" + key.second + "\\"", type, header});
				if (preload_chain_.size() > 2)
				{
					std::string chain;
					for (const auto& link : preload_chain_)
					{
						chain += (chain.empty() ? "" : " > ") + link.label;
					}
					if (!this->collect_)
					{
						throw std::runtime_error(std::format("assets nest {} deep inline ({}); the game holds 2 and crashes "
							"on load", preload_chain_.size(), chain));
					}
					// Collecting: write_zone lists the middle asset before this entry and collects again.
					this->preload_moves_.push_back({this->list_header_, preload_chain_[1].type, preload_chain_[1].header, chain});
				}

				const auto preloaded = format->preload_block == preload_block;
				if (preloaded)
				{
					writer.push_stream(preload_block);''')
patch('''				if (preloaded)
				{
					writer.pop_stream();
					writer.pop_stream();
					preload_chain_.pop_back();
				}''', '''				if (preloaded)
				{
					writer.pop_stream();
					writer.pop_stream();
				}
				preload_chain_.pop_back();''')
patch('''			printf("[info] zone writer: %zu asset(s) listed before their users (Replay preloads 2 levels deep)\\n", moved);''',
      '''			printf("[info] zone writer: %zu asset(s) listed before their users (Replay's asset stack holds 2 levels)\\n", moved);
			for (const auto& move : moves)
			{
				printf("[info]   %s\\n", move.chain.c_str());
			}''')
print('ok')
