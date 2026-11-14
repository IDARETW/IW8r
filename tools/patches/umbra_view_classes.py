import io

# Plan 2.2: the Umbra view also culls primary lights, reflection probe instances and decal volume collections
# (user ID classes 4, 5, 6). Core bit sets, renderer hooks, plumbing.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


# ---- core umbra_view ----
patch('src/core/umbra_view.h', '''    std::vector<uint32_t> knownSurfaces, knownModels;  // bit sets by index
};''', '''    std::vector<uint32_t> knownSurfaces, knownModels;  // bit sets by index
    // The same for primary lights (by primaryLights row), reflection probe instances and decal volume collections.
    std::vector<uint32_t> knownLights, knownProbes, knownDecals;
};''')
patch('src/core/umbra_view.h', '''    std::vector<uint32_t> surfaceBits, modelBits;
    uint32_t visibleByClass[kUmbraClassCount] = {};''', '''    std::vector<uint32_t> surfaceBits, modelBits;
    // By primary light row, reflection probe instance and decal volume collection (the game sets the light's
    // primaryLightVisData bit, the probe's reflectionProbeVisData bit and the collection's decalVisData bit from the
    // same query). Same rule: a clear bit means "the game leaves this out from here".
    std::vector<uint32_t> lightBits, probeBits, decalBits;
    uint32_t visibleByClass[kUmbraClassCount] = {};''')
patch('src/core/umbra_view.h', '''//   - umbraOverlayLines:''', '''//     The same for primary lights, reflection probe instances and decal volume collections (classes 4, 5, 6).
//   - umbraOverlayLines:''')
patch('src/core/umbra_view.cpp', '''        else if (cls == kUmbraStaticModel) setBit(ix.knownModels, umbraUserIdIndex(id));
    }
    return ix;''', '''        else if (cls == kUmbraStaticModel) setBit(ix.knownModels, umbraUserIdIndex(id));
        else if (cls == kUmbraPrimaryLight) setBit(ix.knownLights, umbraUserIdIndex(id));
        else if (cls == kUmbraReflectionProbe) setBit(ix.knownProbes, umbraUserIdIndex(id));
        else if (cls == kUmbraDecalCollection) setBit(ix.knownDecals, umbraUserIdIndex(id));
    }
    return ix;''')
patch('src/core/umbra_view.cpp', '''    for (size_t i = 0; i < r.modelBits.size(); ++i) r.modelBits[i] = ~index.knownModels[i];
    for (const uint32_t id : vis.userIds) {
        const unsigned cls = umbraUserIdClass(id);
        const uint32_t i = umbraUserIdIndex(id);
        if (cls == kUmbraSurface && (i >> 5) < r.surfaceBits.size()) r.surfaceBits[i >> 5] |= 1u << (i & 31u);
        else if (cls == kUmbraStaticModel && (i >> 5) < r.modelBits.size()) r.modelBits[i >> 5] |= 1u << (i & 31u);
    }
    return r;''', '''    for (size_t i = 0; i < r.modelBits.size(); ++i) r.modelBits[i] = ~index.knownModels[i];
    const auto unknown = [](const std::vector<uint32_t>& known, std::vector<uint32_t>& bits) {
        bits.assign(known.size(), 0u);
        for (size_t i = 0; i < bits.size(); ++i) bits[i] = ~known[i];
    };
    unknown(index.knownLights, r.lightBits);
    unknown(index.knownProbes, r.probeBits);
    unknown(index.knownDecals, r.decalBits);
    const auto set = [](std::vector<uint32_t>& bits, uint32_t i) {
        if ((i >> 5) < bits.size()) bits[i >> 5] |= 1u << (i & 31u);
    };
    for (const uint32_t id : vis.userIds) {
        const unsigned cls = umbraUserIdClass(id);
        const uint32_t i = umbraUserIdIndex(id);
        if (cls == kUmbraSurface) set(r.surfaceBits, i);
        else if (cls == kUmbraStaticModel) set(r.modelBits, i);
        else if (cls == kUmbraPrimaryLight) set(r.lightBits, i);
        else if (cls == kUmbraReflectionProbe) set(r.probeBits, i);
        else if (cls == kUmbraDecalCollection) set(r.decalBits, i);
    }
    return r;''')
print('core ok')
