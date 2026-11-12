import io

p = 'E:/IW8-Radiant/docs/PROGRESS.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - CLIENT BUILD STATE (2026-11-11 evening). Owner: "we should be updating game_dx12_ship_replay_local_dw.exe, the',
 '    droplet is down and we dont need to publish anything there". tools/dw_candidate_build.py gets through prepare',
 '    (job E:\\DW\\IW8\\data\\dw-client\\jobs\\1794440667375391600 is prepared and kept) and then stops at the start of the',
 '    build: "Configured codec tool is unavailable: C:\\Users\\user\\AppData\\Local\\ReplayCompilerWorkstation\\',
 '    script-source-packs\\mw169_mp_frontendc3s5\\manifest.json" (sha256 86dd0e83... in compiler-base',
 '    replay-project.local.json). That folder is an owner-supplied pack; AppData\\Local last changed on 2026-11-08 16:10,',
 '    so it went then (not found on C:, D:\\offload or E:). NEXT: when the owner has put it back, run',
 '    dw_candidate_build.py --resume <that job>, then dw_candidate_install.py <that job> (backs up, replaces the game',
 '    folder dw exe + map + pdb, remakes the local exe). E: was full (0 bytes) during this: a full build needs 9 GB.',
 '    Freed: old out/gui-c* folders, out/audit-bin*, this session\'s failed job folders; a Haiku agent cleaned further',
 '    (out/cleanup_report.md).',
]
s = s.replace(a, nl.join(add) + nl + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'C:/Users/user/.claude/projects/E--IW8-Radiant/memory/dw-client-build-rules.md'
s = io.open(p, encoding='utf-8', newline='').read()
a = '- Quick check without the pipeline:'
assert s.count(a) == 1
s = s.replace(a, '- State on 2026-11-11: prepare passes; the build stops because the owner-supplied pack\n'
                 '  C:\\Users\\user\\AppData\\Local\\ReplayCompilerWorkstation\\script-source-packs\\mw169_mp_frontendc3s5 is gone (since\n'
                 '  2026-11-08). Prepared job kept: data\\dw-client\\jobs\\1794440667375391600; resume with\n'
                 '  dw_candidate_build.py --resume <job>. A full build needs about 9 GB free on E: (7 GB database snapshot per job).\n' + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
