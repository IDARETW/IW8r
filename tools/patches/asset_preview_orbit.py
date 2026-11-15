import io

# The asset preview window (owner request 2026-11-14): CameraView gets an orbit mode (left / right drag orbit, middle
# or Shift+left pan, wheel zoom, F frame, R reset) and copyHooksFrom (a second view with the main view's material,
# image, model and effect hooks).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/camera_view.h'
patch(H, '''    CameraView(EditorContext* ctx, ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent = nullptr);
    ~CameraView() override;''', '''    CameraView(EditorContext* ctx, ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent = nullptr);
    ~CameraView() override;
    // A second view (the asset preview window) draws with the same material, image, normal, specular, emissive, mask,
    // layer, shader, tint, effect and tool-path hooks as `other`.
    void copyHooksFrom(const CameraView& other);
    // Orbit mode (the asset preview): the left or right button orbits about a target, the middle button or Shift+left
    // pans, the wheel zooms, F asks for framing (orbitFrameRequested), R returns to the last setOrbit.
    void setOrbitMode(bool on) { orbit_ = on; }
    bool orbitMode() const { return orbit_; }
    void setOrbit(const Vec3& target, double distance, double yawDeg, double pitchDeg);
    Vec3 orbitTarget() const { return orbitTarget_; }
    double orbitDistance() const { return orbitDist_; }''')
patch(H, '''signals:''', '''signals:
    void orbitFrameRequested();''')
patch(H, '''    MaterialStateLookup stateLookup_;''', '''    MaterialStateLookup stateLookup_;
    // Orbit mode (setOrbitMode).
    bool orbit_ = false;
    Vec3 orbitTarget_;
    double orbitDist_ = 256, orbitYaw_ = 45, orbitPitch_ = -25;
    Vec3 orbitHomeTarget_;
    double orbitHomeDist_ = 256, orbitHomeYaw_ = 45, orbitHomePitch_ = -25;
    QPoint orbitLast_;
    void applyOrbit();''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''void CameraView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);''', '''void CameraView::copyHooksFrom(const CameraView& o) {
    shaderLookup_ = o.shaderLookup_;
    layerLookup_ = o.layerLookup_;
    factsLookup_ = o.factsLookup_;
    maskLookup_ = o.maskLookup_;
    normalLookup_ = o.normalLookup_;
    emissiveLookup_ = o.emissiveLookup_;
    specLookup_ = o.specLookup_;
    stateLookup_ = o.stateLookup_;
    effectSource_ = o.effectSource_;
    toolPathFn_ = o.toolPathFn_;
    customTint_ = o.customTint_;
    imageLookup_ = o.imageLookup_;
    imageRequest_ = o.imageRequest_;
    materialImagesChanged();
}

void CameraView::setOrbit(const Vec3& target, double distance, double yawDeg, double pitchDeg) {
    orbitTarget_ = orbitHomeTarget_ = target;
    orbitDist_ = orbitHomeDist_ = std::max(1.0, distance);
    orbitYaw_ = orbitHomeYaw_ = yawDeg;
    orbitPitch_ = orbitHomePitch_ = std::clamp(pitchDeg, -89.0, 89.0);
    applyOrbit();
}

void CameraView::applyOrbit() {
    ctx_->cam.yaw = orbitYaw_;
    ctx_->cam.pitch = orbitPitch_;
    ctx_->cam.pos = orbitTarget_ - ctx_->cam.forward() * orbitDist_;
    ctx_->cameraMoved();
    update();
}

void CameraView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    if (orbit_) {
        orbitLast_ = e->position().toPoint();
        return;
    }''')
patch(C, '''void CameraView::mouseMoveEvent(QMouseEvent* e) {''', '''void CameraView::mouseMoveEvent(QMouseEvent* e) {
    if (orbit_) {
        const QPoint p = e->position().toPoint();
        const QPoint d = p - orbitLast_;
        orbitLast_ = p;
        const bool pan = (e->buttons() & Qt::MiddleButton) || ((e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ShiftModifier));
        if (pan) {
            const double k = orbitDist_ * 0.0018;
            orbitTarget_ = orbitTarget_ - ctx_->cam.right() * (d.x() * k) + ctx_->cam.up() * (d.y() * k);
            applyOrbit();
        } else if (e->buttons() & (Qt::LeftButton | Qt::RightButton)) {
            orbitYaw_ = std::fmod(orbitYaw_ - d.x() * 0.4, 360.0);
            orbitPitch_ = std::clamp(orbitPitch_ - d.y() * 0.4, -89.0, 89.0);
            applyOrbit();
        }
        return;
    }''')
patch(C, '''void CameraView::mouseReleaseEvent(QMouseEvent* e) {''', '''void CameraView::mouseReleaseEvent(QMouseEvent* e) {
    if (orbit_) return;''')
patch(C, '''void CameraView::wheelEvent(QWheelEvent* e) {''', '''void CameraView::wheelEvent(QWheelEvent* e) {
    if (orbit_) {
        orbitDist_ = std::clamp(orbitDist_ * std::pow(0.87, e->angleDelta().y() / 120.0), 1.0, 1e6);
        applyOrbit();
        return;
    }''')
patch(C, '''void CameraView::keyPressEvent(QKeyEvent* e) {''', '''void CameraView::keyPressEvent(QKeyEvent* e) {
    if (orbit_) {
        if (e->key() == Qt::Key_F) emit orbitFrameRequested();
        else if (e->key() == Qt::Key_R) setOrbit(orbitHomeTarget_, orbitHomeDist_, orbitHomeYaw_, orbitHomePitch_);
        return;
    }''')
print('ok')
