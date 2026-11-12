import io
p = 'src/core/compile_iw8.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
for nl in ('\r\n', '\n'):
    a = 'customDecalMaterialJson(*cm, name) + "' + nl + '"});'
    if s.count(a) == 1:
        s = s.replace(a, 'customDecalMaterialJson(*cm, name) + "' + chr(92) + 'n"});')
        io.open(p, 'w', encoding='utf-8', newline='').write(s)
        print('fixed')
        break
else:
    raise SystemExit('not found')
