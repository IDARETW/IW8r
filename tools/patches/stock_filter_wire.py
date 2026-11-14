import io

# Custom shader filter on stock maps, wiring: the stock reference draws with the filter variant, the camera keeps it
# up to date with the map's shader, File > Rebuild Stock Zone hands the compiled filter to the converter.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RC = 'E:/IW8-Radiant/src/viewport/render.cpp'
patch(RC, '''        IW8R_PROF("d.ref");
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psTex_, nullptr, 0);''', '''        IW8R_PROF("d.ref");
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(refPs, nullptr, 0);''')

CH = 'E:/IW8-Radiant/src/qtapp/camera_view.h'
patch(CH, '''    // Cull by tome (View > Umbra Visibility)''', '''    // The map shader's filter for the stock reference (core custom_shader iw8rFilter): the lookup gives the shader
    // whose filter applies, or null. Checked when the files are polled; the reference then draws through it.
    void setReferenceFilterLookup(std::function<std::shared_ptr<const CustomShader>()> lookup) { filterLookup_ = std::move(lookup); }
    bool referenceFilterOn() const { return renderer_.referenceFilterShader(); }
    // Cull by tome (View > Umbra Visibility)''')
patch(CH, '''    QString skyAutoKey_;''', '''    std::function<std::shared_ptr<const CustomShader>()> filterLookup_;
    std::string filterKey_;  // path + code of the filter the renderer holds ("" none)
    QString skyAutoKey_;''')
CC = 'E:/IW8-Radiant/src/qtapp/camera_view.cpp'
patch(CC, '''    // Sky preview: (re)load the face textures when the sky image changes.
    if (ctx_->texturedView && skySource_ && pollFiles_) {''', '''    // The map shader's filter on the stock reference: compiled when the shader (or its file) changes.
    if (filterLookup_ && rendererOk_ && pollFiles_) {
        const std::shared_ptr<const CustomShader> fs = filterLookup_();
        const std::string key = fs ? fs->path + '\\n' + fs->code : std::string();
        if (key != filterKey_) {
            filterKey_ = key;
            std::string err;
            const std::string src = fs ? customShaderFilterViewportSource(app::SceneRenderer::texShaderSource(), "float4 ps_tex(PSIn i) : SV_TARGET {", *fs)
                                       : std::string();
            if (!renderer_.setReferenceFilterShader(src, &err)) {
                renderer_.setReferenceFilterShader({}, nullptr);
                if (ctx_ && fs) emit ctx_->statusMessage(QString::fromStdString("Shader filter " + fs->name + ": " + err));
            }
        }
    }
    // Sky preview: (re)load the face textures when the sky image changes.
    if (ctx_->texturedView && skySource_ && pollFiles_) {''')
patch(CC, '''    const std::shared_ptr<const CustomShader> shader = shaderLookup_(material);
    if (!shader) return -1;''', '''    const std::shared_ptr<const CustomShader> shader = shaderLookup_(material);
    if (!shader || !customShaderHasSurface(shader->code)) return -1;  // a filter alone lights nothing itself''')

M = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
patch(M, '''    // Brush faces carry edge data (core face_edges) while a shader in use reads the edge inputs''', '''    // A stock map with a map shader that has a filter: the reference world and props draw through it (what File >
    // Rebuild Stock Zone builds into the map's lit shaders).
    camera_->setReferenceFilterLookup([this]() -> std::shared_ptr<const CustomShader> {
        if (referenceZone_.isEmpty()) return nullptr;
        const auto s = mapSurfaceShader();
        return s && customShaderHasFilter(s->code) ? s : nullptr;
    });
    // Brush faces carry edge data (core face_edges) while a shader in use reads the edge inputs''')

E = 'E:/IW8-Radiant/src/qtapp/main_window_stock_edit.cpp'
patch(E, '''    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr,
                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun, &replaceMaterials,
                                           &removeWorld);''', '''    // The map shader's filter (worldspawn iw8r_surface_shader, a file with iw8rFilter): compiled here and spliced by the
    // converter into every lit pixel shader of the map's techset zone (editzone "lit_filter"), so the world and the
    // static props keep the game's lighting and get the filter's look.
    std::string litFilter;
    if (const auto shader = mapSurfaceShader(); shader && customShaderHasFilter(shader->code)) {
        std::vector<uint8_t> program;
        std::string cerr;
        const QString programPath = work + "/lit_filter.cso";
        QFile pf(programPath);
        if (!iw8r::app::SceneRenderer::compileProgram(customShaderFilterProgramSource(*shader), kIw8rFilterEntry, program, &cerr)) {
            if (err) *err = "The shader's filter does not compile: " + qs(cerr);
            return false;
        }
        if (!pf.open(QIODevice::WriteOnly) || pf.write(reinterpret_cast<const char*>(program.data()), qint64(program.size())) != qint64(program.size())) {
            if (err) *err = "Cannot write " + programPath;
            return false;
        }
        pf.close();
        litFilter = stockLitFilterJson(QDir::toNativeSeparators(programPath).toStdString());
        if (notes)
            *notes << "the map shader " + qs(shader->name) + " filters the map's lit surfaces (world and static props; characters, weapons and "
                      "effects use shared shaders and keep their look); needs a converter with editzone \\"lit_filter\\"";
    } else if (const auto any = mapSurfaceShader(); any && notes) {
        *notes << "the map shader " + qs(any->name) + " has no iw8rFilter function: a stock map's surfaces keep the game's lighting, "
                  "so only a filter can change their look";
    }

    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr,
                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun, &replaceMaterials,
                                           &removeWorld, &litFilter);''')
s = io.open(E, encoding='utf-8', newline='').read()
for inc in ('#include "custom_shader.h"', '#include "render.h"'):
    if inc not in s:
        i = s.index('#include "main_window.h"')
        j = s.index('\n', i) + 1
        s = s[:j] + inc + '\n' + s[j:]
io.open(E, 'w', encoding='utf-8', newline='').write(s)
print('ok')
