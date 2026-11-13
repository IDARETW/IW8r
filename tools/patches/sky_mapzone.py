import io
def patch(p, pairs):
    s=io.open(p,encoding='utf-8',newline='').read(); nl='\r\n' if '\r\n' in s else '\n'
    for a,b in pairs:
        a=a.replace('\n',nl); b=b.replace('\n',nl); assert s.count(a)==1,(p,a[:80]); s=s.replace(a,b)
    io.open(p,'w',encoding='utf-8',newline='').write(s)
patch('src/qtapp/stock_sky.cpp', [
 ('''            if (current_.startsWith("sky_mp_")) candidates << current_.mid(4);
        }''', '''            if (current_.startsWith("sky_mp_")) candidates << current_.mid(4);
        }
        // The stock map shown (addMapZone): its own sky (mp_m_speed's sky_mp_speed_midday) is in no traced zone.
        for (const QString& z : mapZones_) candidates << z << "techsets_" + z;'''),
 ('''        if (!missingTool.isEmpty() || currentZones_.isEmpty()) {
            const QString why = !missingTool.isEmpty() ? "missing tool " + missingTool
                                : zones_.count(current_) ? "no catalog zone of it in the Replay zone folder"''',
  '''        if (!missingTool.isEmpty() || currentZones_.isEmpty()) {
            const QString why = !missingTool.isEmpty() ? "missing tool " + missingTool
                                : zones_.count(current_) || !mapZones_.isEmpty() ? "no catalog zone of it in the Replay zone folder"'''),
])
patch('src/qtapp/stock_sky.h', [
 ('''    bool busy() const { return running_ || !pending_.isEmpty(); }''', '''    bool busy() const { return running_ || !pending_.isEmpty(); }
    // A stock map shown as the reference: images the catalog does not list are also looked for in its zones.
    void addMapZone(const QString& zone) {
        if (!zone.isEmpty() && !mapZones_.contains(zone)) mapZones_.prepend(zone);
    }'''),
 ('''    std::map<QString, QStringList> zones_;  // image -> catalog zones''', '''    std::map<QString, QStringList> zones_;  // image -> catalog zones
    QStringList mapZones_;                  // addMapZone, newest first'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''    matCache_->addMapZone(zone);  // its own terrain blends are not in the material catalog''',
  '''    matCache_->addMapZone(zone);  // its own terrain blends are not in the material catalog
    if (skyCache_) skyCache_->addMapZone(zone);  // its own sky (sky_mp_speed_midday) may be in no traced zone
    if (imageCache_) imageCache_->addMapZone(zone);  // decal volume images likewise'''),
])
print('ok')
