import io

# Custom shader filter on stock maps, tools: `IW8Radiant.exe --shader-filter-program <shader file> --out <file.cso>`
# compiles a shader file's iw8rFilter as the editor's stock rebuild does; the bench's IW8R_BENCH_SHADER=<name> sets the
# map shader for the shots; SceneRenderer::pixelShaderAccepted asks the device whether it takes a program (GUI test).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'E:/IW8-Radiant/src/viewport/render.h'
patch(RH, '''    bool referenceFilterShader() const { return filterPs_ != nullptr; }
''', '''    bool referenceFilterShader() const { return filterPs_ != nullptr; }
    // Does the device take this compiled pixel shader (checksum and bytecode valid)? For checks of spliced programs.
    bool pixelShaderAccepted(const std::vector<uint8_t>& program);
''')
RC = 'E:/IW8-Radiant/src/viewport/render.cpp'
patch(RC, '''bool SceneRenderer::setReferenceFilterShader(const std::string& source, std::string* err) {''', '''bool SceneRenderer::pixelShaderAccepted(const std::vector<uint8_t>& program) {
    if (!dev_ || program.empty()) return false;
    ID3D11PixelShader* ps = nullptr;
    const bool ok = SUCCEEDED(dev_->CreatePixelShader(program.data(), program.size(), nullptr, &ps)) && ps;
    safeRelease(ps);
    return ok;
}

bool SceneRenderer::setReferenceFilterShader(const std::string& source, std::string* err) {''')
CH = 'E:/IW8-Radiant/src/qtapp/camera_view.h'
patch(CH, '''    bool referenceFilterOn() const { return renderer_.referenceFilterShader(); }''', '''    bool referenceFilterOn() const { return renderer_.referenceFilterShader(); }
    bool pixelShaderAccepted(const std::vector<uint8_t>& program) { return renderer_.pixelShaderAccepted(program); }''')

M = 'E:/IW8-Radiant/src/qtapp/main.cpp'
patch(M, '''        else if (a == "--safe-mode") continue;
''', '''        else if (a == "--shader-filter-program" && i + 1 < argc) filterShaderFile = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--safe-mode") continue;
''')
patch(M, '''    QString compileFile, openFile, keysPath,''', '''    QString filterShaderFile;  // --shader-filter-program <file.iw8shader.hlsl> --out <file.cso>
    QString compileFile, openFile, keysPath,''')
patch(M, '''    if (!materialTest.isEmpty()) {''', '''    if (!filterShaderFile.isEmpty()) {
        // A shader file's iw8rFilter as the compiled program File > Rebuild Stock Zone hands the converter (editzone
        // "lit_filter"). No window, no device: the compiler only.
        iw8r::CustomShader shader;
        std::string err;
        std::vector<uint8_t> program;
        bool ok = iw8r::readCustomShader(filterShaderFile.toStdString(), shader, &err);
        if (ok && !iw8r::customShaderHasFilter(shader.code)) ok = false, err = "the shader has no float3 iw8rFilter(float3 colour)";
        if (ok) ok = iw8r::app::SceneRenderer::compileProgram(iw8r::customShaderFilterProgramSource(shader), iw8r::kIw8rFilterEntry, program, &err);
        QFile out(outDir);
        if (ok && (outDir.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char*>(program.data()), qint64(program.size())) != qint64(program.size())))
            ok = false, err = "cannot write --out " + outDir.toStdString();
        std::printf("%s%s\\n", ok ? "shader filter program written: " : "shader filter program FAILED: ", ok ? outDir.toStdString().c_str() : err.c_str());
        std::fflush(stdout);
        return ok ? 0 : 1;
    }
    if (!materialTest.isEmpty()) {''')
patch(M, '''            const QString um = qEnvironmentVariable("IW8R_UMBRA");''', '''            // IW8R_BENCH_SHADER=<name>: the map's surface shader for the shots (a stock map: its filter on the reference).
            if (const QString sh = qEnvironmentVariable("IW8R_BENCH_SHADER"); !sh.isEmpty()) {
                if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {
                    ctx.doc.setKeyValue(ws->id, "iw8r_surface_shader", sh.toStdString());
                    ctx.documentEdited();
                }
                QElapsedTimer st;
                st.start();
                while (st.elapsed() < 4000) {
                    frame(nullptr);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                settle();
                report << QString("shader %1 reference_filter %2").arg(sh).arg(cam->referenceFilterOn() ? 1 : 0);
            }
            const QString um = qEnvironmentVariable("IW8R_UMBRA");''')
s = io.open(M, encoding='utf-8', newline='').read()
if '#include "custom_shader.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "custom_shader.h"\n' + s[i:]
    io.open(M, 'w', encoding='utf-8', newline='').write(s)

T = 'E:/IW8-Radiant/src/qtapp/qt_gui_test.cpp'
patch(T, '''    r.section("umbra visibility");''', '''    r.section("shader filter on stock shaders");
    {
        // A shader file's iw8rFilter compiled as the stock rebuild does, spliced into a lit pixel shader (core
        // dxbc_splice), and the result offered to the device: Direct3D checks the checksum and the bytecode.
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("data/shaders")); ++up) d.cdUp();
        iw8r::CustomShader toon;
        std::string err;
        const bool read = iw8r::readCustomShader(d.filePath("data/shaders/toon.iw8shader.hlsl").toStdString(), toon, &err);
        r.check(read && iw8r::customShaderHasFilter(toon.code) && iw8r::customShaderHasSurface(toon.code), "the toon shader has a surface function and a filter");
        std::vector<uint8_t> filter, host, spliced;
        const bool fc = read && iw8r::app::SceneRenderer::compileProgram(iw8r::customShaderFilterProgramSource(toon), iw8r::kIw8rFilterEntry, filter, &err);
        r.check(fc && iw8r::dxbcChecksumValid(filter), "its filter compiles to a program with a valid checksum " + QString::fromStdString(fc ? "" : err));
        const char* hostSource =
            "Texture2D colorMap : register(t0); SamplerState colorSampler : register(s0); cbuffer Frame : register(b0) { float4 exposure; };\\n"
            "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float3 normal : TEXCOORD1) : SV_Target {\\n"
            "  float4 texel = colorMap.Sample(colorSampler, uv);\\n"
            "  float light = saturate(dot(normalize(normal), float3(0.3, 0.5, 0.8))) + 0.2;\\n"
            "  return min(float4(texel.rgb * light * exposure.x, 1.0), 32255.0); }\\n";
        const bool hc = iw8r::app::SceneRenderer::compileProgram(hostSource, "main", host, &err);
        r.check(hc, "a lit pixel shader compiles " + QString::fromStdString(hc ? "" : err));
        const bool sp = fc && hc && iw8r::dxbcSpliceFilter(host, filter, spliced, &err);
        r.check(sp && iw8r::dxbcChecksumValid(spliced), "the filter is spliced into it " + QString::fromStdString(sp ? "" : err));
        CameraView* cv = w.cameraView();
        r.check(sp && cv->pixelShaderAccepted(host) && cv->pixelShaderAccepted(spliced), "the device accepts the spliced shader");
        if (sp) {
            std::vector<uint8_t> broken = spliced;
            broken[broken.size() - 9] ^= 0x40;  // a changed byte without a new checksum: refused
            r.check(!cv->pixelShaderAccepted(broken), "the device refuses a program whose checksum is stale");
        }
        // The game's own lit shaders with the filter, when a stock map's edited techset zone was exported here
        // (out/agent/toon_ship/v_pixelshader: the project's offline proof; skipped elsewhere).
        QDir real(d.filePath("out/agent/toon_ship/v_pixelshader/mw19replay/techsets_mp_shipment/assets/pixelshader"));
        const QStringList blobs = real.entryList({"iw8rf_*.cso"}, QDir::Files);
        if (!blobs.isEmpty()) {
            int accepted = 0;
            for (const QString& b : blobs) {
                QFile f(real.filePath(b));
                if (!f.open(QIODevice::ReadOnly)) continue;
                const QByteArray bytes = f.readAll();
                accepted += cv->pixelShaderAccepted(std::vector<uint8_t>(bytes.begin(), bytes.end())) ? 1 : 0;
            }
            r.check(accepted == blobs.size(), QString("the device accepts every filtered stock lit shader of Shipment (%1 of %2)").arg(accepted).arg(blobs.size()));
        }
    }

    r.section("umbra visibility");''')
s = io.open(T, encoding='utf-8', newline='').read()
for inc in ('#include "dxbc_splice.h"', '#include "custom_shader.h"'):
    if inc not in s:
        i = s.index('#include "')
        s = s[:i] + inc + '\n' + s[i:]
io.open(T, 'w', encoding='utf-8', newline='').write(s)
print('ok')
