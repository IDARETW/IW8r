import io
for p in ('C:/Users/user/.claude/projects/E--IW8-Radiant/memory/dw-client-build-rules.md',
          'E:/IW8-Radiant/docs/research/77_loading_screen_first.md'):
    s = io.open(p, encoding='utf-8', newline='').read()
    n = s.count('compiler-base\tools') + s.count('source\tools') + s.count('data\x08r-roam-compat')
    s = s.replace('compiler-base\tools', 'compiler-base\\tools').replace('source\tools', 'source\\tools')
    s = s.replace('data\x08r-roam-compat', 'data\\br-roam-compat')
    assert '\t' not in s and '\x08' not in s, p
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print(p.rsplit('/', 1)[1], 'fixed', n)
