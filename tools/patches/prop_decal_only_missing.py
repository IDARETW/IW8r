import io
p = 'src/qtapp/main_window.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''    std::map<std::string, std::string> maskImage;  // mask asset -> its blend map image
};''', '''    std::map<std::string, std::string> maskImage;  // mask asset -> its blend map image
    std::vector<bool> onWorld;  // per volume: its projection found world triangles
};''')
rep('''    const std::vector<StockDecalBatch> batches = projectDecalVolumes(receivers, set);
    int order = 0;''', '''    const std::vector<StockDecalBatch> batches = projectDecalVolumes(receivers, set);
    data->onWorld.assign(set.volumes.size(), false);
    for (const StockDecalBatch& b : batches)
        if (b.volume < set.volumes.size() && !b.idx.empty()) data->onWorld[b.volume] = true;
    int order = 0;''')
rep('''    const std::vector<StockDecalBatch> projected = projectDecalVolumes(receivers, d.set);''',
    '''    // Only the volumes that found no world surface (stencils on crates, paint on barriers: research 99 found every
    // empty Speedball volume beside a prop): ground decals (sand, dirt) on every prop inside their box cost Hideout
    // 790k triangles and 38 ms a frame.
    StockDecalVolumeSet propSet = d.set;
    propSet.volumes.clear();
    std::vector<size_t> back;
    for (size_t i = 0; i < d.set.volumes.size(); ++i)
        if (i >= d.onWorld.size() || !d.onWorld[i]) propSet.volumes.push_back(d.set.volumes[i]), back.push_back(i);
    std::vector<StockDecalBatch> projected = projectDecalVolumes(receivers, propSet);
    for (StockDecalBatch& b : projected)
        if (b.volume < back.size()) b.volume = back[b.volume];''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
