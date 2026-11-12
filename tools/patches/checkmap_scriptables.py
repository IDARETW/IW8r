import io
p = 'src/qtapp/find_dialog.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = ('        {' + nl +
     '            // 6.15: placed models (dynamic_prop clutter included) whose stock model the availability audit marks missing' + nl)
assert s.count(a) == 1
b = '''        {
            // 6.14: scriptable entities the build would leave out (research 86): unknown definition, a definition that
            // lives only in stock map zones, or one whose build numbers are not all known.
            const ScriptableCatalog& catalog = ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
            for (size_t i = 0; i < m.entities.size(); ++i) {
                const Entity& e = m.entities[i];
                if (e.classname() != "scriptable") continue;
                const std::string* name = e.value("scriptable");
                if (!name || name->empty()) name = e.value("definition");
                QString why;
                if (!name || name->empty()) why = "has no scriptable key (the stock definition's name)";
                else if (catalog.size() == 0) why = "cannot be checked: data/asset_catalog/iw8_scriptables.tsv was not found";
                else if (const ScriptableDefinition* def = catalog.find(*name); !def)
                    why = QString("names '%1', which is not a stock scriptable definition").arg(qs(*name));
                else if (!def->alwaysLoaded)
                    why = QString("names '%1', which lives only in stock map zones (%2); a map cannot carry a definition yet")
                              .arg(qs(def->name), qs(def->zones));
                else if (!def->verified)
                    why = QString("names '%1', whose build numbers are not all known").arg(qs(def->name));
                if (why.isEmpty()) continue;
                list_->addItem(QString("Scriptables: entity %1 %2; it is left out of the build").arg(i).arg(why));
                ++extra;
            }
        }
'''.replace('\n', nl)
s = s.replace(a, b + a)
inc = '#include "stock_scripted_catalog.h"'
assert s.count(inc) == 1
s = s.replace(inc, inc + nl + '#include "scriptable_catalog.h"')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
