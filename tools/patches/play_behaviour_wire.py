import io

# The play preview runs the map's scripted objects (core play_behaviour): doors open on F (the game's Use), lifts,
# spinners and bobbers move and carry the player, teleporters and jump pads act; the viewport draws the movers where
# they are (SceneCache::playPoses).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/camera_view.h'
patch(H, '''    size_t playWorldModels_ = 0;  // placed models that collide in the play preview''', '''    size_t playWorldModels_ = 0;  // placed models that collide in the play preview
    PlayBehaviours playBehaviours_;  // the map's doors, lifts, spinners, teleporters and jump pads while playing
    bool playUseHeld_ = false;
public:
    const PlayBehaviours& playBehavioursForTest() const { return playBehaviours_; }
private:''')
s = io.open(ROOT + H, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
inc = '#include "collision_world.h"'
assert s.count(inc) == 1
s = s.replace(inc, inc + nl + '#include "play_behaviour.h"', 1)
io.open(ROOT + H, 'w', encoding='utf-8', newline='').write(s)

C = 'src/qtapp/camera_view.cpp'
patch(C, '''void CameraView::buildPlayWorld() {
    iw8r::PlayWorldOptions o;''', '''void CameraView::buildPlayWorld() {
    // The movers first: their brushes are left out of the static world and added back as moving groups.
    playBehaviours_.read(ctx_->doc.map());
    iw8r::PlayWorldOptions o;
    o.skip = [this](const Entity& e) { return playBehaviours_.moves(e); };''')
patch(C, '''    iw8r::buildPlayWorld(ctx_->doc.map(), o, world_, &st);
    playWorldModels_ = st.models;''', '''    iw8r::buildPlayWorld(ctx_->doc.map(), o, world_, &st);
    playWorldModels_ = st.models;
    playBehaviours_.attach(ctx_->doc.map(), world_);''')
patch(C, '''    ctx_->setStatus("Play preview ended");''', '''    ctx_->setStatus("Play preview ended");
    if (!ctx_->scene.playPoses.empty()) {
        ctx_->scene.playPoses.clear();  // the movers are drawn at rest again
        ctx_->scene.invalidate();
    }''')
patch(C, '''    // Fixed 1/60 s steps (the mover clamps long frames; this keeps jumps identical at any frame rate).
    double left = std::min(dt, 0.25);''', '''    // The map's scripted objects (core play_behaviour): F is the game's Use key.
    {
        const bool useDown = down(Qt::Key_F);
        const bool use = useDown && !playUseHeld_;
        playUseHeld_ = useDown;
        const auto events = playBehaviours_.tick(std::min(dt, 0.25), player_.origin, playerMins(params_), playerMaxs(params_, player_.crouched),
                                                 use, world_);
        for (const auto& ev : events) {
            if (ev.kind == PlayBehaviours::Event::Teleport) {
                player_.origin = ev.position;
                player_.velocity = Vec3{};
                player_.yaw = ctx_->cam.yaw = ev.yaw;
                ctx_->setStatus("Play: teleported by " + QString::fromStdString(ev.name));
            } else if (ev.kind == PlayBehaviours::Event::Launch) {
                player_.velocity = ev.velocity;
                player_.onGround = false;
            }
        }
        if (!playBehaviours_.poses().empty()) {
            for (const auto& p : playBehaviours_.poses()) {
                app::SceneCache::PlayPose& pp = ctx_->scene.playPoses[p.entity];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) pp.R[i][j] = p.R[i][j];
                pp.pivot = p.pivot;
                pp.move = p.move;
            }
            ctx_->scene.invalidate();
        }
    }
    // Fixed 1/60 s steps (the mover clamps long frames; this keeps jumps identical at any frame rate).
    double left = std::min(dt, 0.25);''')
patch(C, '''        ctx_->setStatus(QString("Play: %1  (WASD, Space jump/mantle, Shift sprint, C crouch/slide, Z prone, N noclip, Esc exit)").arg(mode));''',
      '''        ctx_->setStatus(QString("Play: %1  (WASD, Space jump/mantle, Shift sprint, C crouch/slide, Z prone, F use, N noclip, Esc exit)").arg(mode));''')
print('ok')
