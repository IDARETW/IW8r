import io
p = 'tests/core_tests.cpp'
t = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in t else '\n'
a = r'''    std::printf("\n%d checks, %d failures, %zu tests\n", test::checks(), test::failures(), tests().size());'''
assert t.count(a) == 1, t.count(a)
b = a + nl + r'''    if (test::skips())''' + nl + r'''        std::printf("%d test part(s) SKIPPED: a fixture is missing on this machine (search the output for SKIP)\n", test::skips());'''
io.open(p, 'w', encoding='utf-8', newline='').write(t.replace(a, b))
print('ok')
