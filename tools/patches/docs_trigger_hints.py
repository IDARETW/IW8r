import io

# Docs for the "hint" key of use triggers (ROADMAP 4.27, 2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **A hint on a use trigger, without a script.** Give a `trigger_use` (or `trigger_use_touch`) the key `hint`, for
  example `Press ^3[{+activate}]^7 to open the gate`. The build makes the text one of the map's localized strings,
  and the map's own script shows it on the screen of a player who stands in the trigger and takes it away when they
  leave. The entity inspector lists the key. A map without the key gets the script it got before. Proven offline:
  the build passes the game's loaders and the text is read back from the map's language zone
  (`tools/map_strings_build_test.py`); whether the game draws the hint has not been seen.

''')
patch('docs/PROGRESS.md', '''  ROADMAP 4.27 map text: core map_strings.{h,cpp}''', '''  ROADMAP 4.27 trigger hints: key "hint" on trigger_use / trigger_use_touch -> TriggerVolume::hintText,
  assignTriggerHintRefs ("<MAP ID>/TRIGGER_HINT_<n>", same order in the strings block and the script block of
  compile_iw8.cpp), generated set-up "t.hint = &\\"...\\"; t.hintid = n; level.iw8r_hints = 1;", runtime
  iw8r_hint_show in kTriggerGsc (a newclienthudelem per player; only reached with level.iw8r_hints). Key added in
  tools/gen_iw8_entity_defs.py (defs and ENTITY_REFERENCE regenerated).
  ROADMAP 4.27 map text: core map_strings.{h,cpp}''')
patch('docs/USER_GUIDE.md', '''- **Your own text in the game.**''', '''- **A hint on a use trigger.** Select a `trigger_use` brush and add the key `hint` with the text the player should
  see, for example `Press ^3[{+activate}]^7 to open the gate`. Nothing else is needed: the build carries the text and
  the map shows it while a player stands in the trigger.
- **Your own text in the game.**''')
patch('docs/features/FEATURES.md', '''- Your own text for hint strings and HUD labels:''', '''- A hint on a use trigger: the key `hint` on a `trigger_use` or `trigger_use_touch` is shown to a player standing in it; no script needed. (not yet seen in game)
- Your own text for hint strings and HUD labels:''')
patch('docs/TEST_CHECKLIST.md', '''## In game (optional, 2026-11-14): your own text''', '''## In game (optional, 2026-11-14): a hint on a use trigger
- [ ] Give a `trigger_use` brush the key `hint` = `Press ^3[{+activate}]^7 to test`. Build and play, walk into the
  trigger: the text appears a little below the middle of the screen and goes when you step out. If nothing shows, or
  the trigger stops reacting to Use, say so (the hint is drawn by the map's script).

## In game (optional, 2026-11-14): your own text''')
print('ok')
