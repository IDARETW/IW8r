"""Repairs C++ string literals that a shell heredoc broke: a line break inside a "..." literal becomes \\n again.
Usage: py -3 -I tools/patches/fix_broken_strings.py <file> <start marker> <end marker>"""
import io, sys
path, start, end = sys.argv[1], sys.argv[2], sys.argv[3]
s = io.open(path, encoding='utf-8', newline='').read()
a = s.index(start)
b = s.index(end, a)
block = s[a:b]
out = []
in_string = False
i = 0
fixed = 0
while i < len(block):
    c = block[i]
    if in_string and c == '\\':
        out.append(block[i:i + 2]); i += 2; continue
    if c == '"':
        in_string = not in_string
    if in_string and c in '\r\n':
        if c == '\r' and block[i + 1:i + 2] == '\n':
            i += 1
        out.append('\\n'); fixed += 1; i += 1; continue
    out.append(c); i += 1
io.open(path, 'w', encoding='utf-8', newline='').write(s[:a] + ''.join(out) + s[b:])
print('fixed', fixed, 'line breaks inside string literals')
