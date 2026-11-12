You are going to build a complete, original multiplayer map for Call of Duty: Modern Warfare (2019, "IW8") using IW8 Radiant, a custom level editor that lives in `E:\IW8-Radiant` on this Windows machine. You control the desktop, so you will work in the editor's own window the way a human mapper does, and you also have a shell for file work and headless builds. This is a long session: plan for many hours of work and keep going until the map is finished to the standard described below. Nobody is watching while you work, so make your own decisions, write them down, and only stop early for the few things listed under "When to stop and ask".

## What I want

A map that could pass for a shipped 6v6 map, not a test arena. The editor's own samples are boxes with props in them; I already have those. I want to see what this editor can produce when someone spends real time in it: a layout that plays well, buildings you can go inside, vertical play, a consistent setting, proper lighting and atmosphere, and working game modes.

The session has a second purpose: you are the first agent to build something large through the editor's GUI, so anything in the editor that is broken, confusing or slow is worth knowing about. Log those as you go (see "Notes and bug log").

## The map

Pick the setting yourself after surveying which assets are available, because the theme has to follow the stock props and materials that exist. My default suggestion is a small border town built around a rail depot: a market street, a depot or warehouse with a mezzanine, a row of two-storey houses, a courtyard, and a back alley route. If the catalog supports a different setting better, use that and say why in your notes.

Whatever the setting, the finished map should have:

- **Layout**: three lanes with at least four cross connections, a playable area around 4,000 by 3,500 units, no dead ends, and no sight line longer than about 2,500 units without cover breaking it. Each team's starting side should be different in character, but neither side should have an advantage.
- **Buildings**: at least six you can enter, at least three of them with a second floor reached by stairs or a ladder, with windows and doorways that overlook the lanes. Interiors are furnished, not empty rooms.
- **Height**: at least three positions above ground level that matter in a fight (balcony, roof, mezzanine, raised walkway), each with more than one way up and a way to be countered.
- **Landmarks**: every area recognisable at a glance, so a player always knows where they are. Add callout zones that name them.
- **Detail**: trim, kerbs, pillars and broken-up wall lines instead of flat boxes; prop clusters that tell a small story (a market stall, a repair bay, a guard post); varied materials per building; some terrain or patch work so the ground is not one flat plane; decals where the editor supports them.
- **Lighting and atmosphere**: a deliberate time of day, sun and sky that agree, fog, light entities inside every interior, a reflection probe per room and per outdoor area, a few ambient effects (smoke, dust, sparks) and sound emitters.
- **Gameplay**: Team Deathmatch, Free-for-all, Domination, Hardpoint and Search and Destroy, using the editor's game mode templates and then moving every objective and spawn to a sensible place. Spawns must be out of sight of enemy spawns and objectives. Add an intermission camera with a good view, at least two usable doors, and at least one ladder.
- **Finish**: hidden faces caulked, small brushes marked detail, the play space sealed with clip so nobody can leave it or stand on a roof they should not reach, and scenery beyond the boundary so the edges do not look like the end of the world.

## Read these first

Read them before you touch the editor. They are short, and they are the only source of truth for this tool, since nothing about it exists on the internet.

- `docs/AGENT_MAP_GUIDE.md`: the map file format, valid assets, prefabs, headless build, screenshots. The ground rules in it apply to you.
- `docs/USER_GUIDE.md` and `docs/TUTORIAL.md`: how the editor is driven (menus, shortcuts, tools). In the editor, F1 shows the shortcut sheet and Ctrl+Shift+P opens a command palette that finds any command by name, which is usually faster than hunting through menus.
- `docs/ENTITY_REFERENCE.md`: every entity class and key.
- `data/prefabs/iw8r/index.tsv` and `README.md` (120 hand-built pieces: doors, windows, stairs, walls, cover) and `data/prefabs/stock/index.tsv` (about 6,000 prop groups lifted from shipped maps). Use these heavily; they are how a map gets dense quickly.
- `data/asset_catalog/iw8_materials_status.tsv` and `iw8_models_status.tsv`: only names marked `ok` load in the game. `grey` and `missing` assets build as flat grey or not at all.
- `samples/tutorial/iw8r_tutorial.map`: a small complete arena to read for conventions.
- "What is not expected to work yet" at the end of `docs/TEST_CHECKLIST.md`, so you do not spend time on features that are not there.

To see how shipped maps handle scale, cover and prop density, use File > Open IW8 Stock Map or View > Stock Map Reference and look around one or two of them before you design your own. Borrow ideas, not layouts: the map must be original.

## How to work

Work in `out/agent/<map id>/`, with the map at `out/agent/<map id>/<map id>.map`. Choose a map id of the form `mp_<name>` that nothing else in `out/` or `samples/` uses.

Start the editor on your map with `bin\release\IW8Radiant.exe <path to your map>`. Use the GUI for the work a mapper would do by hand: blocking out, shaping brushes, texturing, placing prefabs and props from the browsers, lighting, game mode templates, Check Map. For bulk or repetitive work (a row of identical windows, fence runs, scattering clutter by rule) you may use the editor's own tools (Arrange, Scatter, macros, command scripts), the `iw8r edit` command line, or a script that writes map text. If you change the map file outside the editor, close the map in the editor first and reopen it afterwards, so the editor does not save an old copy over your change.

Build in stages, and do not move to the next stage until the current one builds and you have looked at it:

1. **Plan**: survey the assets, pick the setting, and write the layout into your notes: lanes, buildings, heights, objective positions, intended sight lines.
2. **Blockout**: plain brushes only, full layout, all routes and heights. Walk it with play preview (P) and time the routes: the two teams should reach the middle at about the same moment, and no route should be a long empty run. Fix the layout now, while it is cheap to change.
3. **Architecture**: turn the blockout into real buildings with walls of proper thickness, doorways, windows, stairs, roofs and interiors, using prefabs where they fit.
4. **Materials and detail**: textures, trim, props, terrain, clutter.
5. **Lighting and atmosphere**: sun, sky, fog, interior lights, probes, effects, sounds.
6. **Gameplay**: spawns, objectives for every mode, callouts, clip, doors, ladders, intermission camera.
7. **Polish**: fix everything Check Map and the build log report, then review the whole map again from player height and improve the weakest areas until none stands out as unfinished.

At the end of every stage: save, run Check Map, build (F7 in the editor, or headless with `--compile` as the guide describes), read `compile-result.txt` and the warnings in `compile-log.txt`, and copy the map to `out/agent/<map id>/versions/<stage>.map`. A build that passes only tells you the map compiles. To judge how it looks and plays, look at it: fly the camera through every area in textured and lit preview (Ctrl+L), walk it in play preview, and take screenshots from player eye height at each spawn area, each objective and each landmark. Compare them honestly against the stock maps you studied. If an area looks like boxes with textures on them, it is not done.

Scale mistakes are the most common failure, so check against the player constantly: the player is 72 units tall, a doorway is about 64 wide and 100 or more high, a storey is about 128 to 144, a step is at most 18, and cover is about 40 (crouch) or 60 (standing). Walking the map in play preview shows wrong proportions immediately.

## Rules of this machine

- Write only inside `out/agent/<map id>/`. Do not edit or overwrite anything in `samples/`, `tests/`, `data/`, `src/`, `docs/` or `tools/`: `samples/` holds my own working maps, and the rest is the editor itself, which other sessions are developing right now.
- Do not deploy the map, do not launch the game, and do not write anything under `E:\IW8\Builds`. That means no Ctrl+F7, no F5, no Deploy or Launch buttons, and no Live Link. I test in game myself.
- Keep at most one editor window of your own open. If an editor window is already open when you start, it belongs to me or to another session: leave it alone, never close it, and never save or discard its changes. If the editor crashes, check `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\crashes\` for the report, note it in the bug log, and reopen your map. Close stray windows of your own before starting another.
- `bin\release\IW8Radiant.exe` may be rebuilt while you work. If it is missing or locked, wait 30 seconds and try again. Do not rebuild the editor yourself, and do not touch `E:\mw124`.
- Disk space is tight (roughly 20 GB free on E: and 12 GB on C:, and the asset caches grow on C:). Check free space at each stage. If either drive drops below 4 GB, stop and tell me instead of deleting anything.
- Do not download or install anything.

## Notes and bug log

You will be working long enough that you may lose track of earlier decisions, so keep two files current as you go and read them back whenever you are unsure where you are:

- `out/agent/<map id>/NOTES.md`: the design (setting, layout, the assets you chose and why), the current stage, what is finished, what is next, and anything you learned about making the editor do what you want.
- `out/agent/<map id>/EDITOR_ISSUES.md`: every editor problem you meet, each with what you did, what you expected, what happened, a screenshot path where it helps, and how you worked around it. When something in the editor does not work, find another way (a different tool, the command palette, a text edit) and keep building; a broken tool is a finding to log, not a reason to stop.

## When to stop and ask

Stop only if: the editor will not start at all after several tries, builds fail for a reason that is not in your map (for example, the tutorial sample fails too), a drive is nearly full, or continuing would require breaking one of the rules above. In every other case, decide, note the decision, and carry on.

## Finishing

The map is finished when every item under "The map" is true, the final build ends with `ok` and no `error:` lines, the prefetch line shows every model and material available, Check Map shows nothing you have not either fixed or explained in your notes, and you have reviewed every area from player height and would not be embarrassed to show it.

Then leave things like this:

- The editor open on the final map, already compiled.
- `out/agent/<map id>/screenshots/` with an overview from above, the overview PNG export, and eye-level shots of each landmark, each objective and both spawn sides.
- `out/agent/<map id>/TEST_ME.md`: what I should check in game, area by area and mode by mode: routes, doors, ladders, the sight lines you were unsure about, spawns you want confirmed, anything that could only be judged in the running game.
- A final message that gives the map id and path, a one-paragraph description of the map, what you are confident in, what you could not verify without the game, anything in the brief you did not achieve and why, and the five editor issues that cost you the most time.

Report honestly. If an area is weaker than the rest or a mode is only roughly placed, say so; I would rather read that than find it in game.
