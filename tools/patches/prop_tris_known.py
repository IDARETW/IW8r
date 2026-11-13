import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


MH = 'src/qtapp/main_window.h'
patch(MH, '''    size_t refPropBuiltTris_ = 0;''', '''    size_t refPropBuiltTris_ = 0;
    // LOD0 triangles of every model read by this map's rebuilds: a model known to be past the triangle budget is not
    // read again (Verdansk: thousands of models past it were read and converted, then dropped, on every rebuild).
    std::map<std::string, size_t> refPropTrisKnown_;''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''            if (const auto kept = refPropBuilt_.find(in.model); kept != refPropBuilt_.end()) {
                built = kept->second;
            } else if (auto im = modelCacheStock_->loadModel(qs(in.model))) {''', '''            if (const auto kept = refPropBuilt_.find(in.model); kept != refPropBuilt_.end()) {
                built = kept->second;
            } else if (const auto known = refPropTrisKnown_.find(in.model);
                       known != refPropTrisKnown_.end() && meshTris + known->second > kPropTriangleBudget) {
                // Past the budget (known from an earlier rebuild): a box, without reading the model again.
            } else if (auto im = modelCacheStock_->loadModel(qs(in.model))) {''')
patch(M, '''                built = fresh;
                if (refPropBuiltTris_ + fresh->tris <= kPropTriangleBudget) {''', '''                built = fresh;
                refPropTrisKnown_[in.model] = fresh->tris;
                if (refPropBuiltTris_ + fresh->tris <= kPropTriangleBudget) {''')
patch(M, '''    refPropFailuresLogged_ = false;
    refPropBuilt_.clear();
    refPropBuiltTris_ = 0;''', '''    refPropFailuresLogged_ = false;
    refPropBuilt_.clear();
    refPropBuiltTris_ = 0;
    refPropTrisKnown_.clear();''')
patch(M, '''            if (const auto it = refPropBuilt_.find(name.toStdString()); it != refPropBuilt_.end()) {
                refPropBuiltTris_ -= std::min(refPropBuiltTris_, it->second->tris);
                refPropBuilt_.erase(it);
            }''', '''            refPropTrisKnown_.erase(name.toStdString());
            if (const auto it = refPropBuilt_.find(name.toStdString()); it != refPropBuilt_.end()) {
                refPropBuiltTris_ -= std::min(refPropBuiltTris_, it->second->tris);
                refPropBuilt_.erase(it);
            }''')
print('ok')
