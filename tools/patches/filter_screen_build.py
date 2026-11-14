import io

# Stock rebuild: the map shader's iw8rFilterScreen is compiled next to iw8rFilter and handed to the converter as
# editzone lit_filter "screen_program"; --shader-filter-program writes it too (<out>.screen.cso) for scripted builds.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/stock_zone_edit.h', '''std::string stockLitFilterJson(const std::string& programPath);''',
      '''// `screenProgramPath`: the compiled iw8rFilterScreen program ("" = none), used for every lit shader that has the
// screen position and a slot for the scene depth ("screen_program"; the converter re-points that slot).
std::string stockLitFilterJson(const std::string& programPath, const std::string& screenProgramPath = {});''')
patch('src/core/stock_zone_edit.cpp', '''std::string stockLitFilterJson(const std::string& programPath) {
    std::string path;
    for (const char ch : programPath) {
        if (ch == '\\\\' || ch == '"') path += '\\\\';
        path += ch;
    }''', '''std::string stockLitFilterJson(const std::string& programPath, const std::string& screenProgramPath) {
    const auto escaped = [](const std::string& text) {
        std::string out;
        for (const char ch : text) {
            if (ch == '\\\\' || ch == '"') out += '\\\\';
            out += ch;
        }
        return out;
    };
    const std::string path = escaped(programPath) + (screenProgramPath.empty() ? std::string() : "\\",\\"screen_program\\":\\"" + escaped(screenProgramPath));''')

M = 'src/qtapp/main_window_stock_edit.cpp'
patch(M, '''        pf.close();
        litFilter = stockLitFilterJson(QDir::toNativeSeparators(programPath).toStdString());
        if (notes)
            *notes << "the map shader " + qs(shader->name) + " filters the map's lit surfaces (world and static props; characters, weapons and "
                      "effects use shared shaders and keep their look); needs a converter with editzone \\"lit_filter\\"";''',
      '''        pf.close();
        // The filter with the screen (iw8rFilterScreen: the toon shader's ink lines from the scene depth), when the
        // shader has one.
        QString screenPath;
        if (customShaderHasScreenFilter(shader->code)) {
            std::vector<uint8_t> screen;
            screenPath = work + "/lit_filter_screen.cso";
            QFile sf(screenPath);
            if (!iw8r::app::SceneRenderer::compileProgram(customShaderScreenFilterProgramSource(*shader), kIw8rFilterEntry, screen, &cerr)) {
                if (err) *err = "The shader's screen filter (iw8rFilterScreen) does not compile: " + qs(cerr);
                return false;
            }
            if (!sf.open(QIODevice::WriteOnly) || sf.write(reinterpret_cast<const char*>(screen.data()), qint64(screen.size())) != qint64(screen.size())) {
                if (err) *err = "Cannot write " + screenPath;
                return false;
            }
        }
        litFilter = stockLitFilterJson(QDir::toNativeSeparators(programPath).toStdString(), QDir::toNativeSeparators(screenPath).toStdString());
        if (notes)
            *notes << "the map shader " + qs(shader->name) + " filters the map's lit surfaces (world and static props; characters, weapons and "
                      "effects use shared shaders and keep their look)" +
                          (screenPath.isEmpty() ? QString() : QString(", with the screen where a shader can take it (lines from the scene depth)")) +
                          "; needs a converter with editzone \\"lit_filter\\"";''')
print('ok')
