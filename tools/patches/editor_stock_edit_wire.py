import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window.h', [(
 '    bool prepareStockRebuild(const QString& work, QStringList* args, QString* err);\n',
 '    bool prepareStockRebuild(const QString& work, QStringList* args, QString* err);\n'
 '    // The fast path (converter editzone, research 88): writes <work>/edit.ents and <work>/edits.json and gives the\n'
 '    // converter call. `notes`: what the rebuild will carry or hide, and warnings.\n'
 '    bool prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes = nullptr);\n'
 '    void rebuildStockZoneFast();\n')])

# The menu command runs the fast path; the dump script stays as "Rebuild Stock Zone (dump script)".
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (scratch)...", QKeySequence(), [this] {' + nl
assert s.count(a) == 1
b = ('    // The stock map with the document\'s entities, spawns, added models and hidden props, through the converter\'s\n'
     '    // editzone (a few seconds; research 88). The Replay install is only read; the result is not installed.\n'
     '    addCommand(file, "RebuildStockZoneFast", "Rebuild Stock Zone...", QKeySequence(), [this] { rebuildStockZoneFast(); });\n'
     '    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (dump script, slow)...", QKeySequence(), [this] {\n').replace('\n', nl)
s = s.replace(a, b)
io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)

# GUI check beside the existing stock rebuild check
p = 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('r.check(w.prepareStockRebuild(work, &args, &perr) && args.contains("-Spawns")')
j = s.index(nl, s.index(';', i))
add = ('''
            {
                // The fast path writes one entity text (the spawn entities ride in it) and an edits file for the
                // converter's editzone command.
                QString program, eerr;
                QStringList eargs, enotes;
                const QString ework = work + "_edit";
                const bool prepared = w.prepareStockEdit(ework, &program, &eargs, &eerr, &enotes);
                QFile ef(ework + "/edit.ents"), jf(ework + "/edits.json");
                const QByteArray etext = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
                const QByteArray ejson = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
                r.check(prepared && eargs.contains("editzone") && eargs.contains(w.stockZone()) && etext.contains("mp_") &&
                            etext.contains("spawn") && ejson.startsWith("{\\"entities\\":") && ejson.contains("edit.ents"),
                        QString("the fast stock rebuild writes the entity text with the spawn entities and an edits file (%1; %2 bytes of text)")
                            .arg(prepared ? QString("ok") : eerr).arg(etext.size()));
            }''').replace('\n', nl)
s = s[:j] + add + s[j:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
