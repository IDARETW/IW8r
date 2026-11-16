import io

# gsc-tool integrated for the IW8 compiler only (src/tools/gsc): the context keeps the parser, assembler, disassembler
# (compiled includes) and compiler; the decompiler and printer are not part of this project.

def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if a not in s and (not b or b in s):
            continue
        assert s.count(a) == 1, (p, a)
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


G = 'E:/IW8-Radiant/src/tools/gsc/'
rw(G + 'include/xsk/gsc/context.hpp', [
    ('#include "xsk/gsc/printer.hpp"\n', ''),
    ('#include "xsk/gsc/decompiler.hpp"\n', ''),
    ('    auto printer() -> printer& { return printer_; }\n', ''),
    ('    auto decompiler() -> decompiler& { return decompiler_; }\n', ''),
    ('    gsc::printer printer_;\n', ''),
    ('    gsc::decompiler decompiler_;\n', ''),
])
rw(G + 'src/gsc/context.cpp', [
    ('printer_{ this }, parser_{ this }, assembler_{ this }, disassembler_{ this }, compiler_{ this }, decompiler_{ this }',
     'parser_{ this }, assembler_{ this }, disassembler_{ this }, compiler_{ this }'),
])
print('ok')
