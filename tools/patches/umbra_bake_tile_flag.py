import io

# The occlusion bake's fault of the game test of 2026-11-14 (research 129): the baked tile had flags 3, the stock
# leaf tiles have 5. Bit 2 says how the game reads a view-tree leaf: set, the leaf's value is an offset into the cell
# index list (what the bake writes, as stock); clear, the value is the cell number itself. With 3 the game took the
# offsets for cell numbers and started its portal walk in an unrelated cell almost everywhere.
#   - core umbra_bake: tile flags 5.
#   - core umbra_tome (the decoder): cellsAt and validate follow the game's rule, so a tome with the wrong flag is
#     read the way the game reads it (the tests then fail on it, as the game did).
#   - tests: the baked tile has flags 5; the same tome with the flag put back to 3 is refused or misread.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/umbra_bake.h', '''    put(t, tile + 0x2C, uint32_t(uint32_t(tileBytes) << 8 | 3u));''',
      '''    // Flags 5, as every stock leaf tile with a view tree (shipment, rust, speedball): bit 2 tells the game that a view
    // leaf's value is an offset into the cell index list, which is what is written below. Without it the game takes
    // the value for the cell number itself (Umbra_KDTree_lookupLeafRange 0x1420079D0, the leaf reader 0x142007B60;
    // research 129): a tome written with 3 hid surfaces and models in the game test of 2026-11-14.
    put(t, tile + 0x2C, uint32_t(uint32_t(tileBytes) << 8 | 5u));''')

T = 'src/core/umbra_tome.cpp'
patch(T, '''    const uint64_t end = uint64_t(t.offset) + t.byteSize;
    for (uint64_t at = t.cellIndices + 4ull * v; at + 4 <= end; at += 4) {
        uint32_t c;
        std::memcpy(&c, d_.data() + at, 4);
        if (c == kUmbraNone || c >= t.cellCount) break;
        out.push_back(t.cellBase + c);
    }
    return out;''', '''    // The tile's flag bit 2 says what the value is (research 129, the game's leaf reader): set, an offset into the cell
    // index list (every stock leaf tile); clear, the cell number itself (the converter's one-cell tome: value 0, cell 0;
    // a value with the top bit set points into a node tree no known tome has: no cell).
    if (!(t.flags & 4u)) {
        if (v < t.cellCount) out.push_back(t.cellBase + v);
        return out;
    }
    const uint64_t end = uint64_t(t.offset) + t.byteSize;
    for (uint64_t at = t.cellIndices + 4ull * v; at + 4 <= end; at += 4) {
        uint32_t c;
        std::memcpy(&c, d_.data() + at, 4);
        if (c == kUmbraNone || c >= t.cellCount) break;
        out.push_back(t.cellBase + c);
    }
    return out;''')
patch(T, '''            uint32_t count = 0;
            bool terminated = false;
            for (uint64_t at = t.cellIndices + 4ull * v; at + 4 <= end; at += 4) {''', '''            if (!(t.flags & 4u)) {
                // Without flag bit 2 the game takes the value for the cell number (research 129).
                ++rep.viewLeaves;
                if (v >= t.cellCount) fail("view leaf of tile " + std::to_string(i) + " names cell " + std::to_string(v) + " of " +
                                           std::to_string(t.cellCount) + " (tile flags without bit 2: the value is the cell number)");
                continue;
            }
            uint32_t count = 0;
            bool terminated = false;
            for (uint64_t at = t.cellIndices + 4ull * v; at + 4 <= end; at += 4) {''')
print('ok')
