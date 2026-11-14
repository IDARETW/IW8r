import io

# Docs for the Vision Editor web form (2026-11-14): the web page has no listed gap left.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: the Vision Editor.** Compile > Vision Editor (every field) in the web page lists all 213 vision fields a
  number can set, group by group, with the stock value under each. A filled field is the map's override; an emptied
  one goes back to stock. Each value is checked by the field's own rule before anything is written.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done except: the full Vision Editor (218 fields; overrides by name are in the''',
      '''  Web form "vision": every scalar / vector field of core vision_fields as a text field (key "v:<name>"), written
  to worldspawn iw8r_vision through parseVisionOverrides / checkVisionValue / formatVisionOverrides.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it). Earlier note: (218 fields; overrides by name are in the''')
patch('docs/USER_GUIDE.md', '''  Atmosphere form also takes vision overrides (`name=value; name=x y z`).''', '''  Atmosphere form also takes vision overrides (`name=value; name=x y z`), and Compile > Vision Editor (every
  field) lists them all with their stock values.''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: the full Vision Editor (all 218 fields by name; overrides by name are in the Atmosphere form).''',
      '''- The Vision Editor is a form too: every settable vision field by name, with its stock value.''')
patch('docs/HANDOVER.md', '''- Web page: the full Vision Editor (vision overrides by name are in the Atmosphere form).
''', '''''')
patch('docs/HANDOVER.md', '''the editor's menus, eleven dialogs and docks as forms,''', '''the editor's menus, twelve dialogs and docks as forms,''')
print('ok')
