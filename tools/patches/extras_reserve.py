import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.cpp', '''    if (!dev_ || batches.empty()) return true;
    std::vector<GpuTexVertex> all;
    for (const auto& b : batches) {
        refTexFirst_.push_back(UINT(all.size()));
        refTexCount_.push_back(UINT(b.size()));
        all.insert(all.end(), b.begin(), b.end());''', '''    if (!dev_ || batches.empty()) return true;
    std::vector<GpuTexVertex> all;
    {
        size_t total = 0;
        for (const auto& b : batches) total += b.size();
        all.reserve(total);  // one allocation (growing by doubling briefly needed up to 3x)
    }
    for (const auto& b : batches) {
        refTexFirst_.push_back(UINT(all.size()));
        refTexCount_.push_back(UINT(b.size()));
        all.insert(all.end(), b.begin(), b.end());''')
print('ok')
