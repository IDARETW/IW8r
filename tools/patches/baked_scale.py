import io
def patch(p, pairs):
    s=io.open(p,encoding='utf-8',newline='').read(); nl='\r\n' if '\r\n' in s else '\n'
    for a,b in pairs:
        a=a.replace('\n',nl); b=b.replace('\n',nl); assert s.count(a)==1,(p,a[:80]); s=s.replace(a,b)
    io.open(p,'w',encoding='utf-8',newline='').write(s)
patch('src/core/stock_world.h', [
 ('''    size_t layeredSurfaces = 0;  // surfaces with layerCount >= 2''', '''    size_t layeredSurfaces = 0;  // surfaces with layerCount >= 2
    // GfxWorldDraw.bakedLightScale: the factor the game applies to the baked light (lightmaps; research 95). 1 on most
    // maps, 1/64 on mp_m_king (an indoor map whose lightmaps were stored 6 EV brighter).
    double bakedLightScale = 1.0;'''),
])
patch('src/core/stock_world.cpp', [
 ('''    const Value* f = fieldsOf(*root);
    const Value* surfs = f && f->get("surfaces") && f->get("surfaces")->get("surfaces")''', '''    const Value* f = fieldsOf(*root);
    // bakedLightScale lives in the draw sub-object (its path differs between exports): the first one found by name,
    // through objects only (the big arrays hold none).
    {
        std::vector<const Value*> stack{f};
        while (!stack.empty()) {
            const Value* v = stack.back();
            stack.pop_back();
            if (!v || v->type != Value::Type::Object) continue;
            if (const Value* b = v->get("bakedLightScale"); b && b->number() && *b->number() > 0) {
                out.bakedLightScale = *b->number();
                break;
            }
            for (const auto& [k, child] : v->obj)
                if (child.type == Value::Type::Object) stack.push_back(&child);
        }
    }
    const Value* surfs = f && f->get("surfaces") && f->get("surfaces")->get("surfaces")'''),
])
patch('src/viewport/render.h', [
 ('''        float skyScale = 1.0f;''', '''        float skyScale = 1.0f;
        float bakedScale = 1.0f;  // the stock map's bakedLightScale, on its lightmap'''),
])
patch('src/viewport/render.cpp', [
 ('''    g[40] = game_.skyScale;''', '''    g[40] = game_.skyScale;
    g[41] = game_.bakedScale;'''),
 ('''            if (stockRef) {
                // Stock: light = f C + (1 - f) C / D.z x sat(N . Dw)''', '''            if (stockRef) {
                a.rgb *= gameSkyScale.y, b *= gameSkyScale.y;  // the map's bakedLightScale
                // Stock: light = f C + (1 - f) C / D.z x sat(N . Dw)'''),
])
patch('src/qtapp/camera_view.h', [
 ('''    // The stock sky material's intensity (its first pixel constant): the sky image times this is the drawn sky.''',
  '''    // The stock map's bakedLightScale (StockWorld): scales its lightmap and the light derived from it.
    void setGameStockBakedScale(double s) {
        stockBakedScale_ = s > 0 ? s : 1.0;
        update();
    }
    // The stock sky material's intensity (its first pixel constant): the sky image times this is the drawn sky.'''),
 ('''    double stockSkyIntensity_ = 0;''', '''    double stockSkyIntensity_ = 0;
    double stockBakedScale_ = 1.0;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''            for (int k = 0; k < 3; ++k) g.sky[k] = stockMeanLight_[size_t(k)];''', '''            for (int k = 0; k < 3; ++k) g.sky[k] = float(stockMeanLight_[size_t(k)] * stockBakedScale_);'''),
 ('''        if (stock && skyExposure_ > 0) g.skyScale =''', '''        if (stock) g.bakedScale = float(stockBakedScale_);
        if (stock && skyExposure_ > 0) g.skyScale ='''),
])
patch('src/qtapp/main_window.cpp', [
 ('''            self->stockWorld_ = world;''', '''            self->stockWorld_ = world;
            if (world) self->camera_->setGameStockBakedScale(world->bakedLightScale);'''),
])
print('ok')
