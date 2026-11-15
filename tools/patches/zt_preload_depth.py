import io
import sys

# x64-zt (iw8radiant/scriptables): the game keeps the map preload marks of nested preloaded assets in a stack of two
# (Replay 1.20, 0x145DA4830, 0x44 bytes each, counter at 0x145DA48B8; the push at 0x140D97A80 has no bound check). A
# preloaded asset written inline inside a preloaded asset inside another overflows it: the third push writes over the
# counter and the next push faults (owner's mp_cinderrail crash, 2026-11-14, 0x140D97ADD). The writer counts that
# nesting and refuses a zone that would overflow it, naming the chain.

ROOT = 'C:/zsw-iw8r/scriptables/'
P = 'src/games/iw8/zone/writer.cpp'


def patch(a, b, count=1):
    s = io.open(ROOT + P, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('''			std::set<asset_key> stub_keys_;''', '''			std::set<asset_key> stub_keys_;
			// Inline preloaded assets in progress (type name and asset name), outermost first: Replay's preload mark
			// stack holds two (see write_asset).
			std::vector<std::string> preload_chain_;''')
patch('''				if (preloaded)
				{
					writer.push_stream(preload_block);
					writer.align(3);
					writer.align(format->alignment);
					writer.reserve(format->size);
					writer.push_stream(write_stream::temp_block);
				}''', '''				if (preloaded)
				{
					// Replay 1.20 keeps the preload marks of nested preloaded assets in a stack of two without a bound
					// check (push 0x140D97A80, stack 0x145DA4830, counter 0x145DA48B8): a third level overwrites the
					// counter and the next push faults (0xC0000005 at 0x140D97ADD). Refuse such a zone here.
					preload_chain_.push_back(std::string(format->name) + " \\"" + key.second + "\\"");
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
					}
					writer.push_stream(preload_block);
					writer.align(3);
					writer.align(format->alignment);
					writer.reserve(format->size);
					writer.push_stream(write_stream::temp_block);
				}''')
patch('''				if (preloaded)
				{
					writer.pop_stream();
					writer.pop_stream();
				}''', '''				if (preloaded)
				{
					writer.pop_stream();
					writer.pop_stream();
					preload_chain_.pop_back();
				}''')
print('ok')
