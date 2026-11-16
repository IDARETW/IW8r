import io

# Opening a stock map frames its play area, the spawns (owner checklist 2026-11-15, D1: the camera started far away
# above the whole map, out-of-bounds helpers and far scenery included).

P = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
a = '''    ctx_.scene.sync(ctx_.doc);
    AABB b;
    for (const app::PrimGeom* g : ctx_.scene.items()) b.add(g->bounds);
    if (b.valid()) {
        ctx_.cam.yaw = 45;'''
b = '''    ctx_.scene.sync(ctx_.doc);
    AABB b;
    for (const app::PrimGeom* g : ctx_.scene.items()) b.add(g->bounds);
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        // A stock map: its play area (the spawns) instead of everything, which put the camera far above the map.
        AABB play;
        // The compiled spawns; the entity string's spawn entities only when there are none (Shipment's entity string
        // also holds spawn-named camera spots far outside the play area).
        for (const auto& sp : ctx_.scene.spawns) play.add(sp.origin);
        if (ctx_.scene.spawns.empty())
            for (const auto& e : ctx_.doc.map().entities) {
                const std::string cls = e.classname();
                if (cls.find("intermission") != std::string::npos || cls.find("camera") != std::string::npos) continue;
                if ((cls.rfind("mp_", 0) == 0 && cls.find("spawn") != std::string::npos) || cls == "info_player_start")
                    if (const auto o = e.origin()) play.add(*o);
            }
        if (play.valid()) {
            play.add(play.mins - Vec3{256, 256, 0});
            play.add(play.maxs + Vec3{256, 256, 384});
            b = play;
        }
    }
    if (b.valid()) {
        ctx_.cam.yaw = 45;'''
if b not in s:
    assert s.count(a) == 1, s.count(a)
    s = s.replace(a, b)
io.open(P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
