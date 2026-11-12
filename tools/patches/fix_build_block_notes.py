import io, re

p = 'C:/Users/user/.claude/projects/E--IW8-Radiant/memory/dw-client-build-rules.md'
s = io.open(p, encoding='utf-8', newline='').read()
start = s.index('- Non-publishing build:')
end = s.index('- Quick check without the pipeline:')
new = ('- Non-publishing build. The owner said on 2026-11-11: "we should be updating\n'
       '  game_dx12_ship_replay_local_dw.exe, the droplet is down and we dont need to publish anything there". Use\n'
       '  E:\\IW8-Radiant\\tools\\dw_candidate_build.py (the pipeline\'s validate-only + carry-installed-gulag build; its\n'
       '  carry check is run in a child process against the retained published exe data\\dw-client\\game_dx12_ship_replay_dw.exe,\n'
       '  because the game folder holds a hand-installed candidate and the check wants the release in deployment.json).\n'
       '  Install with tools\\dw_candidate_install.py <job folder>: it backs up, replaces the game folder\'s dw exe + map + pdb\n'
       '  and remakes the local exe with the project\'s setup-local-client.py --refresh (the local exe is always derived from\n'
       '  the dw exe; Host-Local-Server.ps1 remakes it when they disagree). Never edit pipeline files or records.\n'
       '  Run pipeline checks only as child processes: helper modules loaded in-process break later prepare steps.\n')
s = s[:start] + new + s[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'E:/IW8-Radiant/docs/research/77_loading_screen_first.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = s[s.index('3. A read-only what-if'):s.index("Both are the client project's own state.")]
b = nl.join([
 '3. Correction (same day): an earlier version of this note listed a missing helper module',
 '   (`workstation_preparation_context`) as a second blocker. That was wrong. The module is in `E:\\DW\\IW8\\tools`; my',
 '   what-if script ran from another folder and so did not find it. Run the way the pipeline runs it, the check passes',
 '   once it reads the published exe.',
 '',
 'How it was resolved (owner: "we should be updating game_dx12_ship_replay_local_dw.exe, the droplet is down and we',
 'dont need to publish anything there"): `tools/dw_candidate_build.py` in this repository runs the pipeline\'s own',
 'validate-only carry-forward build and answers the check\'s reads of the installed exe with the retained published exe',
 '`data\\dw-client\\game_dx12_ship_replay_dw.exe` (same hash as `deployment.json`), in a child process as the pipeline',
 'does. No pipeline file or record is edited and nothing is published. `tools/dw_candidate_install.py` then installs',
 'the candidate as the game folder\'s dw exe and remakes the local exe with `setup-local-client.py --refresh`.',
 '',
 '',
])
s = s.replace(a, b)
s = s.replace("Both are the client project's own state. The three client changes of this session wait for a build by the owner.",
              "The build and install results are recorded in docs/PROGRESS.md.")
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
