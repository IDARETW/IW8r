import io
import sys

# Converter: editzone "lit_filter" with a "screen_program" (IW8 Radiant's iw8rFilterScreen: a stock map's ink lines
# from the scene depth). A lit technique whose pixel stage has the thermal mask argument (code image 79) gives that
# register to the scene depth (code image 0, "Frontend FloatZ") and its pixel shader gets the screen program spliced
# in (dxbc_splice's screen mode); every other lit shader keeps the plain program. The splicer header is copied again
# from IW8 Radiant's core.
# Argument: the converter checkout (default the IW8 Radiant worktree).

BASE = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'
ROOT = BASE + 'src/convert/iw3_iw8/'


def patch(p, a, b, count=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# ---- the splicer as a header (as tools/patches/zt_lit_filter.py made it)
h = io.open('E:/IW8-Radiant/src/core/dxbc_splice.h', encoding='utf-8').read()
c = io.open('E:/IW8-Radiant/src/core/dxbc_splice.cpp', encoding='utf-8').read()
h = h.replace('#pragma once\n', '')
c = c.replace('#include "dxbc_splice.h"\n', '')
for name in ('std::array<uint8_t, 16> dxbcChecksum(', 'bool dxbcChecksumValid(', 'bool dxbcInspect(', 'bool dxbcSpliceFilter('):
    i = c.index('\n' + name)
    c = c[:i + 1] + 'inline ' + c[i + 1:]
header = ('// Copied from IW8 Radiant (src/core/dxbc_splice.h and .cpp) by tools/patches/zt_lit_filter.py: do not edit here.\n'
          '#pragma once\n' + h + '\n' + c)
io.open(ROOT + 'dxbc_splice.hpp', 'w', encoding='utf-8', newline='\n').write(header)

C = ROOT + 'convert.cpp'
patch(C, '''		// "lit_filter": {"program": "<compiled filter pixel shader>", "shared": ["<zone>", ...]}''',
      '''		// "lit_filter": {"program": "<compiled filter pixel shader>", "screen_program": "<...>", "shared": ["<zone>", ...]}
		//   "screen_program" (optional): the filter with the screen (its input 1 is SV_Position, its texture t0 the
		//   scene depth). A lit technique whose pixel stage has the thermal mask argument (code image 79) gets it: the
		//   argument is re-pointed to the scene depth (code image 0, "Frontend FloatZ") and the shader is spliced in
		//   screen mode with that register. A shader that cannot take it gets "program".''')
patch(C, '''			if (!iw8r::dxbcChecksumValid(filter))
			{
				throw std::runtime_error("lit_filter: the program is not a DXBC shader with a valid checksum");
			}
''', '''			if (!iw8r::dxbcChecksumValid(filter))
			{
				throw std::runtime_error("lit_filter: the program is not a DXBC shader with a valid checksum");
			}
			std::vector<std::uint8_t> screen_filter;
			if (node.contains("screen_program") && node.at("screen_program").is_string() && !node.at("screen_program").get<std::string>().empty())
			{
				std::ifstream file(std::filesystem::path(node.at("screen_program").get<std::string>()), std::ios::binary);
				if (!file)
				{
					throw std::runtime_error("lit_filter: cannot read " + node.at("screen_program").get<std::string>());
				}
				screen_filter.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
				if (!iw8r::dxbcChecksumValid(screen_filter))
				{
					throw std::runtime_error("lit_filter: the screen program is not a DXBC shader with a valid checksum");
				}
			}
''')
patch(C, '''			const std::string filter_id = hex(filter.data() + 4, 16);''',
      '''			const std::string filter_id = hex(filter.data() + 4, 16) + (screen_filter.empty() ? std::string() : hex(screen_filter.data() + 4, 8));''')
patch(C, '''			std::size_t techniques = 0, shaders = 0, shaders_shared = 0, shaders_missing = 0, shaders_refused = 0, sets = 0;
			std::string first_refusal;''', '''			std::size_t techniques = 0, shaders = 0, shaders_shared = 0, shaders_missing = 0, shaders_refused = 0, sets = 0;
			std::string first_refusal;
			// The screen program: a filtered shader -> the register its scene depth is read at (absent: the plain filter).
			std::map<const ComputeShader*, std::uint16_t> screen_register;
			std::size_t shaders_screen = 0, techniques_screen = 0, techniques_screen_unbound = 0, screen_refused = 0;
			std::string first_screen_refusal;
			// A technique's pixel-stage argument for the thermal mask (code image 79): the slot the scene depth can take.
			const auto depth_argument = [&](MaterialTechnique* technique) -> MaterialShaderArgument*
			{
				if (screen_filter.empty() || !technique->args)
				{
					return nullptr;
				}
				const std::size_t count = std::size_t{technique->perPrimArgCount} + technique->perObjArgCount +
					technique->stableArgCount + technique->neverArgCount;
				for (std::size_t arg = 0; arg < count; arg++)
				{
					auto& entry = technique->args[arg];
					if (entry.type == MTL_ARG_CODE_TEXTURE && (entry.shader & 0x10) && entry.index == 79 && entry.dest < 128)
					{
						return &entry;
					}
				}
				return nullptr;
			};''')
patch(C, '''					auto* shader = technique->pixelShader;
					ComputeShader* fresh = nullptr;
					if (const auto known = filtered.find(shader); known != filtered.end())
					{
						fresh = known->second;
					}
					else
					{''', '''					auto* shader = technique->pixelShader;
					ComputeShader* fresh = nullptr;
					auto* const depth_slot = depth_argument(technique);
					if (const auto known = filtered.find(shader); known != filtered.end())
					{
						fresh = known->second;
						// A shader already spliced in screen mode reads the scene depth at one register: this technique
						// must bind it there too.
						if (const auto reg = fresh ? screen_register.find(fresh) : screen_register.end(); reg != screen_register.end())
						{
							if (depth_slot && depth_slot->dest == reg->second)
							{
								depth_slot->index = 0;
								++techniques_screen;
							}
							else
							{
								++techniques_screen_unbound;
							}
						}
					}
					else
					{''')
patch(C, '''						std::vector<std::uint8_t> spliced;
						std::string error;
						if (!source)
''', '''						std::vector<std::uint8_t> spliced;
						std::string error;
						// The screen program first, where the technique has a slot for the scene depth; else the plain one.
						bool with_screen = false;
						if (source && depth_slot)
						{
							std::string screen_error;
							iw8r::DxbcSpliceOptions screen_options;
							screen_options.sceneDepthRegister = depth_slot->dest;
							with_screen = iw8r::dxbcSpliceFilter({source->prog.loadDef.program, source->prog.loadDef.program + source->prog.loadDef.programSize},
								screen_filter, spliced, &screen_error, screen_options);
							if (!with_screen)
							{
								++screen_refused;
								if (first_screen_refusal.empty())
								{
									first_screen_refusal = plain + ": " + screen_error;
								}
							}
						}
						if (!source)
''')
patch(C, '''						else if (!iw8r::dxbcSpliceFilter({source->prog.loadDef.program, source->prog.loadDef.program + source->prog.loadDef.programSize},
							filter, spliced, &error))''', '''						else if (!with_screen && !iw8r::dxbcSpliceFilter({source->prog.loadDef.program, source->prog.loadDef.program + source->prog.loadDef.programSize},
							filter, spliced, &error))''')
patch(C, '''							std::memcpy(reinterpret_cast<std::uint8_t*>(fresh) + 36, program.data() + 4, 4);
							++shaders;''', '''							std::memcpy(reinterpret_cast<std::uint8_t*>(fresh) + 36, program.data() + 4, 4);
							++shaders;
							if (with_screen)
							{
								screen_register[fresh] = depth_slot->dest;
								depth_slot->index = 0;  // the scene depth instead of the thermal mask
								++shaders_screen;
								++techniques_screen;
							}''')
patch(C, '''				{"shaders_refused", shaders_refused},
				{"first_refusal", first_refusal},''', '''				{"shaders_refused", shaders_refused},
				{"first_refusal", first_refusal},
				{"screen_program", !screen_filter.empty()},
				{"shaders_with_screen", shaders_screen},
				{"techniques_with_screen", techniques_screen},
				{"techniques_screen_unbound", techniques_screen_unbound},
				{"screen_refused", screen_refused},
				{"first_screen_refusal", first_screen_refusal},''')
print('ok')
