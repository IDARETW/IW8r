import io


def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (p, a)
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


R = 'E:/IW8-Radiant/'
rw(R + 'CHANGELOG.md', [
    ('## 2026-11-16\n\n',
     '''## 2026-11-16

- **Ready for a public repository.** The project is GPL-3 ([LICENSE](LICENSE)); [THIRD_PARTY.md](THIRD_PARTY.md) lists
  the three tools built from `src/tools` and every library with its licence and source; the package's `licenses`
  folder carries all of them. The README builds from a clone (`git clone --recursive`, any Qt folder), names the
  helper exes, generates the key table from the repository's own gsc-tool copy and runs `tools/run_all_tests.ps1`.
'''),
])
rw(R + 'docs/PROGRESS.md', [
    ('''  - Next: LICENSE (GPL-3), THIRD_PARTY.md, README (building, what the user provides), package licence notes.
''',
     '''  - Release files done: LICENSE (GPL-3, gsc-tool's text), THIRD_PARTY.md, README (clone --recursive, helper exes,
    key table from src/tools/gsc, run_all_tests), package_release.ps1 copies every licence into licenses/.
  - Before making the repo public (owner): 10 tracked files hold C:\\Users\\<user> paths (tools/dw_candidate_*.py,
    tools/patches/docs_round*.py, ...); no email or keys are tracked. The fork checkouts are no longer needed to build.
'''),
])
print('ok')
