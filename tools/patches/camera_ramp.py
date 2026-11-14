import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/editor_context.h'
patch(H, '''    QSet<int> heldKeys;   // camera keys currently down (Qt::Key values)''', '''    QSet<int> heldKeys;   // camera keys currently down (Qt::Key values)
    // View > Smooth Camera Movement (view/cameraRamp): the fly camera eases up to its speed and back down to rest
    // over about `cameraRamp` seconds (0 = the old instant start and stop). Owner 2026-11-13.
    double cameraRamp = 0.2;
    Vec3 camVelocity;              // units per second, eased toward the keys' target
    double camYawRate = 0, camPitchRate = 0;  // degrees per second (keyboard turning)''')
C = 'src/qtapp/editor_context.cpp'
patch(C, '''void EditorContext::tickCamera(double dt, bool shiftHeld) {
    if (heldKeys.isEmpty()) return;
    const double speed = cam.speed * (shiftHeld ? 3.0 : 1.0) * dt;
    const double turn = 90.0 * dt;
    Vec3 move;
    bool turned = false;''', '''void EditorContext::tickCamera(double dt, bool shiftHeld) {
    const bool resting = length(camVelocity) < 1e-3 && std::fabs(camYawRate) < 1e-3 && std::fabs(camPitchRate) < 1e-3;
    if (heldKeys.isEmpty() && resting) return;
    const double fullSpeed = cam.speed * (shiftHeld ? 3.0 : 1.0);  // units per second
    const double turn = 90.0;                                      // degrees per second
    Vec3 move;
    bool turned = false;
    double yawWant = 0, pitchWant = 0;''')
patch(C, '''        if (down(Qt::Key_Left)) { cam.yaw += turn; turned = true; }
        if (down(Qt::Key_Right)) { cam.yaw -= turn; turned = true; }
        if (down(Qt::Key_A)) { cam.pitch = std::min(89.0, cam.pitch + turn); turned = true; }
        if (down(Qt::Key_Z)) { cam.pitch = std::max(-89.0, cam.pitch - turn); turned = true; }
    }
    if (length(move) > 0) cam.pos += normalize(move) * speed;
    if (length(move) > 0 || turned) emit cameraChanged();
}''', '''        if (down(Qt::Key_Left)) yawWant += turn;
        if (down(Qt::Key_Right)) yawWant -= turn;
        if (down(Qt::Key_A)) pitchWant += turn;
        if (down(Qt::Key_Z)) pitchWant -= turn;
    }
    const Vec3 wantVel = length(move) > 0 ? normalize(move) * fullSpeed : Vec3{};
    if (cameraRamp <= 0) {
        camVelocity = wantVel, camYawRate = yawWant, camPitchRate = pitchWant;
    } else {
        // Exponential easing: about 95 % of the way in cameraRamp seconds, starting and stopping alike.
        const double k = 1.0 - std::exp(-3.0 * dt / cameraRamp);
        camVelocity = camVelocity + (wantVel - camVelocity) * k;
        camYawRate += (yawWant - camYawRate) * k;
        camPitchRate += (pitchWant - camPitchRate) * k;
        // Settle exactly at rest (no endless creep).
        if (length(wantVel) == 0 && length(camVelocity) < fullSpeed * 0.01) camVelocity = Vec3{};
        if (yawWant == 0 && std::fabs(camYawRate) < 0.5) camYawRate = 0;
        if (pitchWant == 0 && std::fabs(camPitchRate) < 0.5) camPitchRate = 0;
    }
    const bool moved = length(camVelocity) > 0;
    if (moved) cam.pos += camVelocity * dt;
    if (camYawRate != 0) cam.yaw += camYawRate * dt, turned = true;
    if (camPitchRate != 0) cam.pitch = std::clamp(cam.pitch + camPitchRate * dt, -89.0, 89.0), turned = true;
    if (moved || turned) emit cameraChanged();
}''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''    QAction* spawnsToggle = addCommand(view, "ToggleCompiledSpawns", "Show IW8 Compiled Spawns", QKeySequence(), [this] {''',
      '''    {
        QAction* smooth = addCommand(view, "SmoothCameraMovement", "Smooth Camera Movement", QKeySequence(), [this] {
            const bool on = action("SmoothCameraMovement")->isChecked();
            Settings::instance().setValue("view/cameraRamp", on ? 0.2 : 0.0);
            ctx_.cameraRamp = on ? 0.2 : 0.0;
        });
        smooth->setCheckable(true);
        ctx_.cameraRamp = std::max(0.0, Settings::instance().value("view/cameraRamp", 0.2).toDouble());
        smooth->setChecked(ctx_.cameraRamp > 0);
    }
    QAction* spawnsToggle = addCommand(view, "ToggleCompiledSpawns", "Show IW8 Compiled Spawns", QKeySequence(), [this] {''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''    r.section("game mode view (View > Game Mode, research 40)");''', '''    r.section("smooth camera movement (View > Smooth Camera Movement)");
    {
        // Holding W eases up to the camera speed; letting go eases back to rest instead of stopping dead.
        const Vec3 p0 = ctx.cam.pos;
        const double ramp0 = ctx.cameraRamp;
        ctx.cameraRamp = 0.2;
        ctx.camVelocity = Vec3{};
        ctx.looking = true;
        ctx.heldKeys.insert(Qt::Key_W);
        ctx.tickCamera(1.0 / 60.0, false);
        const double first = length(ctx.cam.pos - p0);
        for (int i = 0; i < 60; ++i) ctx.tickCamera(1.0 / 60.0, false);
        const Vec3 p1 = ctx.cam.pos;
        ctx.tickCamera(1.0 / 60.0, false);
        const double cruise = length(ctx.cam.pos - p1);
        r.check(first > 0 && first < cruise * 0.5 && std::fabs(cruise - ctx.cam.speed / 60.0) < ctx.cam.speed / 60.0 * 0.05,
                QString("the camera eases into its speed (first step %1, cruising %2 a frame)").arg(first).arg(cruise));
        ctx.heldKeys.clear();
        const Vec3 p2 = ctx.cam.pos;
        ctx.tickCamera(1.0 / 60.0, false);
        const double coast = length(ctx.cam.pos - p2);
        for (int i = 0; i < 120; ++i) ctx.tickCamera(1.0 / 60.0, false);
        r.check(coast > 0 && coast < cruise && length(ctx.camVelocity) == 0, "letting go eases to a stop");
        ctx.cameraRamp = ramp0;
        ctx.looking = false;
        ctx.cam.pos = p0;
        r.check(w.action("SmoothCameraMovement") && w.action("SmoothCameraMovement")->isCheckable(), "View > Smooth Camera Movement is a toggle");
    }

    r.section("game mode view (View > Game Mode, research 40)");''')
print('ok')
