import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/main_window.h'
patch(H, '''    std::array<size_t, 2> propLightStats() const { return {refPropLit_, refPropLitOf_}; }''',
      '''    std::array<size_t, 2> propLightStats() const { return {refPropLit_, refPropLitOf_}; }
    // The stock reference's prop placements (null before a stock map is shown): the bench's "prop" camera.
    std::shared_ptr<const std::vector<StockModelInstance>> referencePropPlacements() const { return refProps_; }''')

M = 'src/qtapp/main.cpp'
patch(M, '''    // IW8R_BENCH_NEAR=<radius>: the scene items near the bench camera (kind, label, bounds), to name what a shot shows.''',
      '''    // IW8R_BENCH_CAM="prop <model name part> N [distance] [yaw] [fov]": look at the Nth stock prop placement whose model
    // name holds the part, from `distance` (default 160) units away along `yaw` (default 0), 50 units above its origin.
    if (camParts.size() >= 3 && camParts[0] == "prop") {
        const std::string part = camParts[1].toStdString();
        const int n = camParts[2].toInt();
        const double dist = camParts.size() >= 4 ? camParts[3].toDouble() : 160.0, yaw = camParts.size() >= 5 ? camParts[4].toDouble() : 0.0;
        int seen = 0;
        if (const auto props = w.referencePropPlacements())
            for (const auto& in : *props) {
                if (in.model.find(part) == std::string::npos || seen++ < n) continue;
                const double r = yaw * 3.14159265358979 / 180.0;
                benchCam[0] = in.origin.x - std::cos(r) * dist, benchCam[1] = in.origin.y - std::sin(r) * dist;
                benchCam[2] = in.origin.z + 50, benchCam[3] = yaw, benchCam[4] = -8;
                benchCam[5] = camParts.size() >= 6 ? camParts[5].toDouble() : 70;
                fixedCam = true;
                report << QString("bench_cam prop %1 %2 at %3 %4 %5").arg(QString::fromStdString(in.model)).arg(n).arg(in.origin.x)
                              .arg(in.origin.y).arg(in.origin.z);
                break;
            }
    }
    // IW8R_BENCH_NEAR=<radius>: the scene items near the bench camera (kind, label, bounds), to name what a shot shows.''')
print('ok')
