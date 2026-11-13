import io
p = 'src/qtapp/camera_view.h'
s = io.open(p, encoding='utf-8', newline='').read()
lines = s.split('\n')
for i, l in enumerate(lines):
    if 'fileTextureStats' in l:
        lines[i - 1] = '    // File-keyed textures ("\\x06<path>": the super terrain\'s albedo): {asked, loaded, failed}.' + ('\r' if lines[i - 1].endswith('\r') else '')
    if "k[0] == '\x06'" in l:
        lines[i] = l.replace("k[0] == '\x06'", "k[0] == '\\x06'")
io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('ok')
