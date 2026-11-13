import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/game_lightmap.h', '''GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor);''',
      '''GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor);
// The geometry part of gatherGameBake's key (every item's stamp), without gathering: a cheap "did anything change".
uint64_t gameBakeGeometryKey(const SceneCache& scene);''')
patch('src/viewport/game_lightmap.cpp', '''GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor) {''',
      '''uint64_t gameBakeGeometryKey(const SceneCache& scene) {
    uint64_t key = 1469598103934665603ull;
    for (const PrimGeom* g : scene.items()) key = mixKey(key, int64_t(g->stamp));
    return key;
}

GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor) {''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''    app::GameBakeInput in = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor);
    // The game draws the sun''', '''    // The settings first; the geometry is gathered (about a second on a stock map: every model triangle is an
    // occluder) only when it or the settings changed. A revision bump alone (textures arriving) does not re-bake.
    const bool forcedBake = gameBakeRev_ == ~0ull;
    app::GameBakeInput in;
    // The game draws the sun''')
patch(C, '''            if (const std::string* rs = e.value("iw8r_bake_rays")) in.settings.skyRays = std::clamp(std::atoi(rs->c_str()), 1, 1024);
            break;
        }
    gameBaking_ = true;''', '''            if (const std::string* rs = e.value("iw8r_bake_rays")) in.settings.skyRays = std::clamp(std::atoi(rs->c_str()), 1, 1024);
            break;
        }
    {
        uint64_t h = app::gameBakeGeometryKey(ctx_->scene);
        const auto mixD = [&h](double v) {
            uint64_t b = 0;
            std::memcpy(&b, &v, sizeof(b));
            h ^= b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h *= 0x100000001b3ull;
        };
        for (const Vec3& v : {in.scene.sunDir, in.scene.sunColor, in.scene.skyColor}) mixD(v.x), mixD(v.y), mixD(v.z);
        mixD(in.scene.sunIntensity), mixD(in.scene.skyIntensity), mixD(in.settings.texelSize), mixD(double(in.settings.skyRays));
        mixD(double(in.key));
        for (const BakeLight& b : in.scene.lights)
            mixD(b.pos.x), mixD(b.pos.y), mixD(b.pos.z), mixD(b.color.x), mixD(b.color.y), mixD(b.color.z), mixD(b.radius),
                mixD(b.dir.x), mixD(b.dir.y), mixD(b.dir.z), mixD(b.cosOuter), mixD(b.cosInner);
        if (!forcedBake && gameLm_ && h == gameBakeInputKey_) {
            gameBakeRev_ = rev;
            return;
        }
        gameBakeInputKey_ = h;
    }
    {
        app::GameBakeInput g = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor);
        in.polys = std::move(g.polys);
        in.occluders = std::move(g.occluders);
        in.key ^= g.key;
    }
    gameBaking_ = true;''')
patch('src/qtapp/camera_view.h', '''    uint64_t gameBakeRev_ = ~0ull, gameSeenRev_ = ~0ull;''',
      '''    uint64_t gameBakeRev_ = ~0ull, gameSeenRev_ = ~0ull;
    uint64_t gameBakeInputKey_ = 0;  // geometry stamps + bake settings of the last bake (maybeBakeGameLook)''')
print('ok')
