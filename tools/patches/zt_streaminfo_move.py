import io, sys
root = sys.argv[1]
p = root + '/src/cli/zone_commands.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('#include "games/iw8/iw8.hpp"' + nl + '#include "games/iw8/zone/loader.hpp"' + nl, '')
a = s.index('			using namespace zonetool::iw8;' + nl + '			const auto path = game_files::find_zone(args[0]);')
b = s.index('			const auto text = out.dump(1);', a)
body = s[a:b]
call = ('			const auto path = game_files::find_zone(args[0]);' + nl +
        '			if (!path)' + nl + '			{' + nl + '				throw std::runtime_error("zone " + args[0] + " was not found");' + nl + '			}' + nl +
        '			const auto text = convert::iw3_iw8::stream_info_json(*path, args[0]);' + nl)
end_text = '			const auto text = out.dump(1);' + nl
assert s.count(end_text) == 1
s = s[:a] + call + s[b + len(end_text):]
decl = '	void edit_zone(const std::filesystem::path& game_path, const std::string& zone_name,' + nl
assert s.count(decl) == 1
s = s.replace(decl, '	std::string stream_info_json(const std::filesystem::path& zone_path, const std::string& zone_name);' + nl + decl)
io.open(p, 'w', encoding='utf-8', newline='').write(s)

# The body becomes a function of the converter library, which has the zone loader and the game structures.
lines = body.split(nl)
drop = []
out_lines = []
skip = 0
for i, line in enumerate(lines):
    out_lines.append(line[1:] if line.startswith('\t') else line)   # one level less
fn = nl.join(out_lines)
fn = fn.replace('		const auto path = game_files::find_zone(args[0]);' + nl + '		if (!path)' + nl + '		{' + nl +
                '			throw std::runtime_error("zone " + args[0] + " was not found");' + nl + '		}' + nl, '')
fn = fn.replace('load_zone(*path, {.keep_temp_data = true})', 'load_zone(zone_path, {.keep_temp_data = true})')
fn = fn.replace('out["zone"] = args[0];', 'out["zone"] = zone_name;')
assert 'args[' not in fn, fn[:400]
q = root + '/src/convert/iw3_iw8/convert.cpp'
c = io.open(q, encoding='utf-8', newline='').read()
cnl = '\r\n' if '\r\n' in c else '\n'
anchor = '	void edit_zone(const std::filesystem::path& game_path, const std::string& zone_name,' + cnl
assert c.count(anchor) == 1
i = c.rfind('	// Edits a stock', 0, c.index(anchor))
if i < 0:
    # the comment block before edit_zone starts at the first "	//" line of the run above it
    j = c.index(anchor)
    k = j
    while True:
        prev = c.rfind(cnl, 0, k - len(cnl))
        line = c[prev + len(cnl):k]
        if not line.startswith('	//'):
            break
        k = prev + len(cnl)
    i = k
func = ('	// The streaming info of a map zone as JSON (the streaminfo command; IW8 Radiant research 90).' + cnl +
        '	std::string stream_info_json(const std::filesystem::path& zone_path, const std::string& zone_name)' + cnl +
        '	{' + cnl + fn.replace(nl, cnl) + '		return out.dump(1);' + cnl + '	}' + cnl + cnl)
c = c[:i] + func + c[i:]
io.open(q, 'w', encoding='utf-8', newline='').write(c)
print('ok')
