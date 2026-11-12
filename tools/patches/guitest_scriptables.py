import io
p = 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = ('            (void)dp;' + nl + '        }' + nl + '        while (ctx.doc.text() != before && ctx.doc.undo()) {}')
assert s.count(a) == 1
b = '''            (void)dp;
        }
        {
            // 6.14: a scriptable whose definition lives in a stock map zone, and an unknown one, are reported; the
            // hardpoint marker (always loaded, fully known) is not.
            ctx.doc.addEntity({{"classname", "scriptable"}, {"origin", "0 0 0"}, {"scriptable", "com_trafficcone_01"}}, {}, "test scriptable");
            ctx.doc.addEntity({{"classname", "scriptable"}, {"origin", "16 0 0"}, {"scriptable", "no_such_definition_x"}}, {}, "test scriptable");
            ctx.doc.addEntity({{"classname", "scriptable"}, {"origin", "32 0 0"}, {"scriptable", "hardpoint_chevron"}}, {}, "test scriptable");
            ctx.documentEdited();
            dlg.recheck();
            int mapZone = 0, unknown = 0, chevron = 0;
            for (const QString& t : dlg.itemTexts()) {
                if (!t.startsWith("Scriptables:")) continue;
                mapZone += t.contains("com_trafficcone_01") && t.contains("stock map zones");
                unknown += t.contains("no_such_definition_x") && t.contains("not a stock scriptable definition");
                chevron += t.contains("hardpoint_chevron");
            }
            r.check(mapZone == 1 && unknown == 1 && chevron == 0,
                    QString("Check Map reports scriptables the build leaves out (map zone %1, unknown %2) and not the hardpoint marker (%3)")
                        .arg(mapZone).arg(unknown).arg(chevron));
            for (int k = 0; k < 3; ++k) ctx.doc.undo();
            ctx.documentEdited();
        }
        while (ctx.doc.text() != before && ctx.doc.undo()) {}'''.replace('\n', nl)
s = s.replace(a, b)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
