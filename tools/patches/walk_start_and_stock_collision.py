import io

# Play Preview (Walk) fixes from the owner's checklist 2026-11-15 (C3 / D2: "when i entered play preview, i was still in
# noclip mode", on the compact showcase and on Hideout):
#  - a stock map's world is collision now: CameraView::setPlayWorldExtras, filled by MainWindow with the stock world's
#    opaque surfaces as facets (CollisionWorld::addTriangle; a grid broad phase keeps the traces fast);
#  - with no spawn selected and the camera outside the map (or above it all, as after opening a map, which frames
#    the whole map from far away), the walk starts at the spawn nearest the camera instead of falling back to noclip.

ROOT = 'E:/IW8-Radiant/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('src/qtapp/camera_view.h', [
('''    void buildPlayWorld();''', '''    void buildPlayWorld();
public:
    // Collision the play preview adds to the map's own (a stock map's world: MainWindow fills it).
    void setPlayWorldExtras(std::function<void(CollisionWorld&)> fn) { playWorldExtras_ = std::move(fn); }
private:
    std::function<void(CollisionWorld&)> playWorldExtras_;'''),
])

rw('src/qtapp/camera_view.cpp', [
('''    iw8r::buildPlayWorld(ctx_->doc.map(), o, world_, &st);
    playWorldModels_ = st.models;''', '''    iw8r::buildPlayWorld(ctx_->doc.map(), o, world_, &st);
    if (playWorldExtras_) playWorldExtras_(world_);  // a stock map's world (MainWindow)
    playWorldModels_ = st.models;'''),
('''    // Spawn: a selected spawn entity (its origin and yaw), else below the camera.
    Vec3 from = ctx_->cam.pos;
    double yaw = ctx_->cam.yaw;
    for (const auto& r : ctx_->doc.selection()) {''', '''    // Spawn: a selected spawn entity (its origin and yaw), else below the camera; when the camera is outside the map
    // (or above all of it, as after opening a map), the spawn nearest the camera.
    Vec3 from = ctx_->cam.pos;
    double yaw = ctx_->cam.yaw;
    bool chosen = false;
    for (const auto& r : ctx_->doc.selection()) {'''),
('''            if (const std::string* a = e->value("angles")) {
                double p = 0, y = 0, rr = 0;
                if (std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &rr) >= 2) yaw = y;
            }
            break;
        }
    }
    player_ = PlayerState{};
    const bool floor = placePlayer(world_, player_, from, params_);''', '''            if (const std::string* a = e->value("angles")) {
                double p = 0, y = 0, rr = 0;
                if (std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &rr) >= 2) yaw = y;
            }
            chosen = true;
            break;
        }
    }
    player_ = PlayerState{};
    bool floor = false;
    if (!chosen) {
        const AABB wb = world_.bounds();
        // Inside the map's footprint and not above all of it (a camera under the ceiling of a closed room counts).
        const bool inside = wb.valid() && from.x > wb.mins.x && from.x < wb.maxs.x && from.y > wb.mins.y && from.y < wb.maxs.y &&
                            from.z <= wb.maxs.z + 1;
        floor = inside && placePlayer(world_, player_, from, params_);
        if (!floor) {
            // The nearest spawn: the document's spawn entities, then a stock map's compiled spawns.
            double best = 1e300;
            std::optional<std::pair<Vec3, double>> spawn;
            const auto consider = [&](const std::string& cls, const Vec3& o, double y) {
                if (!(cls.rfind("mp_", 0) == 0 && cls.find("spawn") != std::string::npos) && cls != "info_player_start") return;
                const double d = length(o - ctx_->cam.pos);
                if (d < best) best = d, spawn = std::pair<Vec3, double>{o, y};
            };
            for (const auto& e : ctx_->doc.map().entities)
                if (auto o = e.origin()) {
                    double p = 0, y = 0, rr = 0;
                    if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &rr);
                    consider(e.classname(), *o, y);
                }
            for (const auto& s : ctx_->scene.spawns) consider(s.name, s.origin, s.angles.y);
            if (spawn) {
                from = spawn->first + Vec3{0, 0, 8};
                yaw = spawn->second;
                player_ = PlayerState{};
                floor = placePlayer(world_, player_, from, params_);
            }
        }
        if (!floor && !inside) player_ = PlayerState{}, floor = placePlayer(world_, player_, ctx_->cam.pos, params_);
    } else {
        floor = placePlayer(world_, player_, from, params_);
    }'''),
])
print('ok')
