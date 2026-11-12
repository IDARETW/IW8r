import io
p = 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''                const bool wasTextured = ctx.texturedView;
                if (QAction* m = w.action("CameraModeTextured")) m->trigger();
                for (int i = 0; i < 600; ++i) {'''
b = '''                const bool wasTextured = ctx.texturedView;
                const std::string boxMat0 = ctx.currentMaterial;
                ctx.currentMaterial = "tm2co/me_curb_stone_01";  // textured (an earlier section may leave any material current)
                if (QAction* m = w.action("CameraModeTextured")) m->trigger();
                for (int i = 0; i < 600; ++i) {'''
a = a.replace('\n', nl); b = b.replace('\n', nl)
assert s.count(a) == 1
s = s.replace(a, b)
a2 = '                    r.text += "  info scene chunk culling not checked (the box material has no texture here)\\n";'
assert s.count(a2) == 1, s.count(a2)
s = s.replace(a2, a2 + nl + '                ctx.currentMaterial = boxMat0;')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
