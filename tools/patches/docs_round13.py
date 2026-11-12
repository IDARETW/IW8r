import io, re

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = nl.join([
 '3. The drop sets the key `iw8r_material` on the model. The build makes a copy of the stock model with that material on',
 '   every surface. Custom models keep their own materials.'])
assert s.count(a) == 1, s.count(a)
b = nl.join([
 '3. The drop sets the key `iw8r_material` on the model. The build makes a copy of the model with that material on',
 '   every surface. This works for stock models and for your own models (shape models from Primitives, imported files).',
 '4. You can also select one or more models (and brushes) and click a material in the Materials dock: the material is',
 '   applied to everything selected, in one undo step.'])
s = s.replace(a, b)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| F.16 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 4
status = cells[3].rstrip(' |')
add = (' Third report (2026-11-11 evening, owner: "my custom mc_grass_custom texture isnt applying to the blocks shaped as a staircase"): the stairs were shape-model cubes (misc_model), and two things were wrong. '
       'Picking a material in the Materials dock applied it only to selected brushes and patches, never to selected models; it now sets iw8r_material on selected models too, in the same undo step. '
       'And the build refused iw8r_material on every model that was not a stock model; a model from the map\'s model folders (shape models, imported files) now gets a re-skinned copy built from the same file (compile option customModelVariants). '
       'Checked on a copy of the owner\'s map: 11 models built with the material, the copy references mc_grass_custom, validation passed. Tests: core compile_iw8_custom_model_material, GUI "a picked material is applied to the selected model". NOT SEEN in game.')
assert '|' not in add
lines[hit[0]] = ' | '.join(cells[:3]) + ' | ' + status + add + ' |'
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: map debug overlay (2026-11-11), needs the new client build, not seen in game'
assert s.count(a) == 1
sec = [
 '## In the editor: materials on your own models (2026-11-11 evening)',
 '',
 '- [ ] Restart the editor (the fix is in the new build). Select the staircase cubes in `mp_iw8r_fix1`, then click',
 '      `mc_grass_custom` in the Materials dock: all selected cubes show the grass. One Ctrl+Z takes it off again.',
 '- [ ] Build (F7): the log says `N model(s) built with their iw8r_material on every surface` and has no line that',
 '      ends in "it keeps its own" for the cubes.',
 '- [ ] In game: the staircase cubes show the grass texture.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - MATERIALS ON OWN MODELS (F.16 third report): brush_tools applyMaterialToSelection now also sets iw8r_material on',
 '    selected model entities; compile_iw8 CompileOptions::customModelVariants + the Variants pre-pass build a',
 '    re-skinned copy "<model>_m<hash>" of a model-folder model. (A struct field named "slots" breaks Qt builds:',
 '    it is bySlot.) Tests: core 1078 / 0.',
 '  - CLIENT BUILD: the pack was never missing. Builds run from the Codex app see AppData\\Local through its package',
 '    folder (...\\Packages\\OpenAI.Codex_2p2nqsd0c76g0\\LocalCache\\Local). From this session a junction',
 '    C:\\Users\\user\\AppData\\Local\\ReplayCompilerWorkstation -> that folder is needed while building; remove it after',
 '    (cmd /c rmdir, never a recursive delete). Link error fixed: data globals that are not in the linker catalog',
 '    must be declared as gl_<older-build VA> (DW_MapDebug.cpp). The pipeline can also stop with "SQLite committed',
 '    while checking snapshot reuse" when another tool writes the analysis database: just run it again.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
