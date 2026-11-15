import io

# Bench camera "mat <material part> N [distance] [yaw] [fov]": looks at the Nth stock world batch whose material holds
# the part (the camera keeps each main reference batch's bounds), from `distance` (default 400) along `yaw`, from
# above the batch. Used to frame the Shipment water (research 133 section 5.3) and any other material offline.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


CH = 'src/qtapp/camera_view.h'
patch(CH, '''    size_t referenceTriangles() const { return refDirty_ ? pendingRef_.size() / 3 : renderer_.referenceVertexCount() / 3; }''',
      '''    size_t referenceTriangles() const { return refDirty_ ? pendingRef_.size() / 3 : renderer_.referenceVertexCount() / 3; }
    // The bounds of the Nth main stock reference batch whose material name holds `part` (bench cameras); none: invalid.
    iw8r::AABB referenceMaterialBounds(const std::string& part, int n, std::string* material = nullptr) const {
        int seen = 0;
        for (size_t k = 0; k < refTexMaterials_.size() && k < refTexBounds_.size(); ++k)
            if (refTexMaterials_[k].find(part) != std::string::npos && seen++ == n) {
                if (material) *material = refTexMaterials_[k];
                return refTexBounds_[k];
            }
        return iw8r::AABB{};
    }''')
patch(CH, '''    std::vector<std::string> refTexMaterials_;''', '''    std::vector<std::string> refTexMaterials_;
    std::vector<iw8r::AABB> refTexBounds_;  // per main reference batch (referenceMaterialBounds)''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''        refTexLayer_.clear();
        refTexVolume_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);''', '''        refTexLayer_.clear();
        refTexVolume_.clear();
        refTexBounds_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            AABB bb;
            for (const auto& v : b.tris) bb.add(Vec3{v.x, v.y, v.z});
            refTexBounds_.push_back(bb);''')
M = 'src/qtapp/main.cpp'
patch(M, '''    // IW8R_BENCH_NEAR=<radius>: the scene items near the bench camera (kind, label, bounds), to name what a shot shows.''',
      '''    // IW8R_BENCH_CAM="mat <material part> N [distance] [yaw] [fov]": look down at the Nth stock world batch whose material
    // holds the part, from `distance` (default 400) units back along `yaw` (default 0) and half that above its centre.
    if (camParts.size() >= 3 && camParts[0] == "mat") {
        std::string material;
        const AABB bb = cam->referenceMaterialBounds(camParts[1].toStdString(), camParts[2].toInt(), &material);
        if (bb.valid()) {
            const double dist = camParts.size() >= 4 ? camParts[3].toDouble() : 400.0, yaw = camParts.size() >= 5 ? camParts[4].toDouble() : 0.0;
            const Vec3 c = bb.center();
            const double r = yaw * 3.14159265358979 / 180.0;
            benchCam[0] = c.x - std::cos(r) * dist, benchCam[1] = c.y - std::sin(r) * dist, benchCam[2] = c.z + dist * 0.5;
            benchCam[3] = yaw, benchCam[4] = -26.5;  // looking at the centre
            benchCam[5] = camParts.size() >= 6 ? camParts[5].toDouble() : 70;
            fixedCam = true;
            report << QString("bench_cam mat %1 at %2 %3 %4 size %5 %6 %7").arg(QString::fromStdString(material)).arg(c.x).arg(c.y).arg(c.z)
                          .arg(bb.maxs.x - bb.mins.x).arg(bb.maxs.y - bb.mins.y).arg(bb.maxs.z - bb.mins.z);
        } else {
            report << "bench_cam mat: no batch with " + camParts[1];
        }
    }
    // IW8R_BENCH_NEAR=<radius>: the scene items near the bench camera (kind, label, bounds), to name what a shot shows.''')
print('ok')
