import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'
MC = 'src/qtapp/main_window.cpp'
M = 'src/qtapp/main.cpp'

patch(CH, '''    void setStockLocalLights(std::shared_ptr<const std::vector<StockGameLight>> lights, std::shared_ptr<const std::vector<uint8_t>> ies) {
        pendingLights_ = lights ? std::move(lights) : std::make_shared<const std::vector<StockGameLight>>();
        pendingIes_ = std::move(ies);
        update();
    }''', '''    // shadows: each light's shadow camera (same order; research 106 section 4), null = none shadowed.
    void setStockLocalLights(std::shared_ptr<const std::vector<StockGameLight>> lights, std::shared_ptr<const std::vector<uint8_t>> ies,
                             std::shared_ptr<const std::vector<StockLightShadow>> shadows = nullptr) {
        pendingLights_ = lights ? std::move(lights) : std::make_shared<const std::vector<StockGameLight>>();
        pendingIes_ = std::move(ies);
        pendingShadows_ = std::move(shadows);
        update();
    }
    size_t stockLightShadowsDrawn() const { return renderer_.stockLightShadowsDrawn(); }''')
patch(CH, '''    std::shared_ptr<const std::vector<uint8_t>> pendingIes_;''', '''    std::shared_ptr<const std::vector<uint8_t>> pendingIes_;
    std::shared_ptr<const std::vector<StockLightShadow>> pendingShadows_;''')
patch(CC, '''            renderer_.setStockLocalLights(*pendingLights_);''',
      '''            renderer_.setStockLocalLights(*pendingLights_, pendingShadows_ ? *pendingShadows_ : std::vector<StockLightShadow>{});''')
patch(CC, '''            pendingLights_.reset();
            pendingIes_.reset();''', '''            pendingLights_.reset();
            pendingIes_.reset();
            pendingShadows_.reset();''')
patch(MC, '''        auto localLights = std::make_shared<std::vector<StockGameLight>>();''', '''        auto localLights = std::make_shared<std::vector<StockGameLight>>();
        auto lightShadows = std::make_shared<std::vector<StockLightShadow>>();''')
patch(MC, '''                    if (packStockLight(l, d != defs.end() && !iesStrip->empty() ? &d->second : nullptr, g)) localLights->push_back(g);''',
      '''                    if (packStockLight(l, d != defs.end() && !iesStrip->empty() ? &d->second : nullptr, g)) {
                        localLights->push_back(g);
                        lightShadows->push_back(stockLightShadowCamera(l, g));  // research 106 section 4
                    }''')
patch(MC, '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade, localLights, iesStrip] {
            if (!self || self->referenceZone_ != zone) return;
            self->camera_->setStockLocalLights(localLights, iesStrip);''', '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade, localLights, iesStrip,
                                         lightShadows] {
            if (!self || self->referenceZone_ != zone) return;
            self->camera_->setStockLocalLights(localLights, iesStrip, lightShadows);''')
patch(M, '''                report << QString("stock_lights %1 in the map, %2 drawn").arg(cam->stockLocalLightCount()).arg(cam->stockLocalLightsDrawn());''',
      '''                report << QString("stock_lights %1 in the map, %2 drawn, %3 shadow maps").arg(cam->stockLocalLightCount()).arg(cam->stockLocalLightsDrawn())
                              .arg(cam->stockLightShadowsDrawn());''')
print('ok')
