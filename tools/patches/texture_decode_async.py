import io

# "Preview decode still runs on the GUI thread (bounded per frame)" (PROGRESS, perf notes of 2026-11-11): the camera
# view read and decoded every texture file while it built a frame, 12 ms of it per frame. The files are now read and
# decoded by worker threads (core keyed_job_pool.h); the frame goes on without a texture that is not ready and finds
# it a few frames later, and only the upload to the GPU is left for the GUI thread. Off-screen renders (screenshots,
# tests, the bench, the build's probe captures) still wait for each file, as they draw a fixed number of frames.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/camera_view.h'
patch(H, '''    void setFilmGrainEnabled(bool on) { renderer_.setFilmGrainEnabled(on); update(); }''',
      '''    void setFilmGrainEnabled(bool on) { renderer_.setFilmGrainEnabled(on); update(); }
    // Texture files are read and decoded by worker threads while the view is on screen (decodedTexture). Runs that
    // draw a fixed number of frames (tests, the bench, the start-up check, feature pictures) turn that off for the
    // whole process: every file is then decoded where it is asked for, as before.
    static void setBackgroundTextureDecode(bool on);
    static bool backgroundTextureDecode();
    // Files a worker decoded and the view then used (the status line and tests).
    int backgroundDecodes() const { return backgroundDecodes_; }''')
patch(H, '''    int captureW_ = 0, captureH_ = 0;  // renderImage: target size instead of the widget's''',
      '''    int captureW_ = 0, captureH_ = 0;  // renderImage: target size instead of the widget's
    // A texture file as a worker decoded it (decodedTexture). mode 0: block-compressed levels, else the preview image;
    // 1: the preview image; 2: an image file (PNG / TGA / JPG / BMP).
    struct DecodedTexture {
        bool blocksOk = false, imageOk = false;
        app::DdsBlockLevels blocks;
        app::Rgba8Image image;
        int fullW = 0, fullH = 0;
    };
    // True: `out` holds the decoded file (which may have failed: neither flag set). False: a worker is on it; the frame
    // goes on without this texture and asks again (moreToLoad_). Off-screen renders always get true.
    bool decodedTexture(const std::string& path, int side, int mode, DecodedTexture& out);
    std::unique_ptr<KeyedJobPool<DecodedTexture>> decodePool_;
    int backgroundDecodes_ = 0;''')
s = io.open(ROOT + H, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "keyed_job_pool.h"' not in s:
    a = '#include "game_look.h"' + nl
    assert s.count(a) == 1
    s = s.replace(a, a + '#include "keyed_job_pool.h"' + nl)
    io.open(ROOT + H, 'w', encoding='utf-8', newline='').write(s)

C = 'src/qtapp/camera_view.cpp'
patch(C, '''int CameraView::textureSideNow() const { return renderer_.textureBytes() > textureBudget_ ? 128 : kViewTextureSide; }''',
      '''int CameraView::textureSideNow() const { return renderer_.textureBytes() > textureBudget_ ? 128 : kViewTextureSide; }

namespace {
std::atomic<bool> g_backgroundDecode{true};
}
void CameraView::setBackgroundTextureDecode(bool on) { g_backgroundDecode = on; }
bool CameraView::backgroundTextureDecode() { return g_backgroundDecode; }

bool CameraView::decodedTexture(const std::string& path, int side, int mode, DecodedTexture& out) {
    static const bool noBc = qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES");
    const auto job = [path, side, mode] {
        DecodedTexture d;
        if (mode == 2) {
            std::string err;
            d.imageOk = decodeImageFile(path, d.image.width, d.image.height, d.image.pixels, &err) && d.image.width > 0 && d.image.height > 0;
            d.fullW = d.image.width, d.fullH = d.image.height;
            return d;
        }
        if (mode == 0 && !noBc) d.blocksOk = app::loadDdsBlocks(path, side, d.blocks, &d.fullW, &d.fullH);
        if (!d.blocksOk) d.imageOk = app::loadDdsPreview(path, side, d.image, &d.fullW, &d.fullH) && d.image.width > 0 && d.image.height > 0;
        return d;
    };
    // A render into an image (a screenshot, a test, a probe capture) draws a fixed number of frames: it waits.
    const bool wait = !g_backgroundDecode || captureW_ > 0 || cleanCapture_ || (ctx_ && ctx_->scene.captureMode);
    if (!decodePool_) {
        if (wait) {
            out = job();
            return true;
        }
        decodePool_ = std::make_unique<KeyedJobPool<DecodedTexture>>();
        QPointer<CameraView> self(this);
        decodePool_->setReadyCallback([self] {
            QMetaObject::invokeMethod(qApp, [self] {
                if (self) self->materialImagesChanged();  // the frame that waited is built again
            }, Qt::QueuedConnection);
        });
    }
    const std::string key = std::to_string(mode) + ":" + std::to_string(side) + ":" + path;
    if (!decodePool_->take(key, job, out, wait)) return false;
    if (!wait) ++backgroundDecodes_;
    return true;
}''')

# ---- colour images
patch(C, '''    ++loadsThisFrame_;
    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    int fullW = 0, fullH = 0;  // size of the whole image (Texel Density reads it), not of the level shown
    bool decoded = false;
    if (path->endsWith(".dds", Qt::CaseInsensitive) && colourImageKeptCompressed(material)) {
        // A stock image nothing edits (no cut-out mask, not blended, no tint): uploaded block-compressed as stored,
        // its alpha the specular code (Verdansk with its streamed tiles ran past the texture budget decoded to RGBA).
        // Nothing per material changes it, so materials that use the same file share one texture.
        if (const auto it = texByPathBc_.find(path->toStdString()); it != texByPathBc_.end()) {
            --loadsThisFrame_;
            return texIds_[material] = it->second;
        }
        app::DdsBlockLevels blocks;
        if (app::loadDdsBlocks(path->toStdString(), textureSideNow(), blocks, &fullW, &fullH)) {
            id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {''', '''    ++loadsThisFrame_;
    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    int fullW = 0, fullH = 0;  // size of the whole image (Texel Density reads it), not of the level shown
    bool decoded = false;
    const bool isDds = path->endsWith(".dds", Qt::CaseInsensitive);
    const bool compressed = isDds && colourImageKeptCompressed(material);
    if (compressed)
        if (const auto it = texByPathBc_.find(path->toStdString()); it != texByPathBc_.end()) {
            --loadsThisFrame_;
            return texIds_[material] = it->second;
        }
    // The file is read and decoded by a worker (decodedTexture); this frame goes on without it until it is ready.
    DecodedTexture dec;
    if (!decodedTexture(path->toStdString(), textureSideNow(), !isDds ? 2 : compressed ? 0 : 1, dec)) {
        --loadsThisFrame_;
        moreToLoad_ = true;
        return -1;
    }
    fullW = dec.fullW, fullH = dec.fullH;
    if (compressed) {
        // A stock image nothing edits (no cut-out mask, not blended, no tint): uploaded block-compressed as stored,
        // its alpha the specular code (Verdansk with its streamed tiles ran past the texture budget decoded to RGBA).
        // Nothing per material changes it, so materials that use the same file share one texture.
        app::DdsBlockLevels& blocks = dec.blocks;
        if (dec.blocksOk) {
            id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {''')
patch(C, '''    if (path->endsWith(".dds", Qt::CaseInsensitive)) {
        // Only the mip level the viewport texture needs is read and decoded: the top level of a 2048x2048 stock
        // image is 16 MB of RGBA and cost 100 ms and more here, to be shrunk to 512 right after.
        decoded = app::loadDdsPreview(path->toStdString(), textureSideNow(), img, &fullW, &fullH);
        if (decoded && img.width > 0 && img.height > 0) {''', '''    if (isDds) {
        // Only the mip level the viewport texture needs is read and decoded: the top level of a 2048x2048 stock
        // image is 16 MB of RGBA and cost 100 ms and more here, to be shrunk to 512 right after.
        if (dec.imageOk) {
            img = std::move(dec.image);
            decoded = true;
        } else if (dec.blocksOk) {  // the compressed upload failed: decode it here after all
            decoded = app::loadDdsPreview(path->toStdString(), textureSideNow(), img, &fullW, &fullH);
        }
        if (decoded && img.width > 0 && img.height > 0) {''')
patch(C, '''        // Custom materials (doc 21): PNG / TGA / JPG / BMP through the same decoder the compiler uses.
        std::string derr;
        decoded = decodeImageFile(path->toStdString(), img.width, img.height, img.pixels, &derr);
        if (decoded && customTint_)''', '''        // Custom materials (doc 21): PNG / TGA / JPG / BMP through the same decoder the compiler uses.
        decoded = dec.imageOk;
        if (decoded) img = std::move(dec.image);
        if (decoded && customTint_)''')

# ---- emissive images
patch(C, '''    ++loadsThisFrame_;
    int id = -1;
    app::Rgba8Image img;
    // The texture budget's size rule as the other images (emissive images always loaded at full preview size).
    app::DdsBlockLevels blocks;
    if (!qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES") && app::loadDdsBlocks(path.toStdString(), textureSideNow(), blocks))
        id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
    if (id < 0 && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    return emiIds_[material] = id;''', '''    ++loadsThisFrame_;
    int id = -1;
    app::Rgba8Image img;
    // The texture budget's size rule as the other images (emissive images always loaded at full preview size).
    DecodedTexture dec;
    if (!decodedTexture(path.toStdString(), textureSideNow(), 0, dec)) {  // a worker is decoding it
        --loadsThisFrame_;
        moreToLoad_ = true;
        return -1;
    }
    if (dec.blocksOk) id = renderer_.createTextureBlocks(dec.blocks.dxgiFormat, dec.blocks.width, dec.blocks.height, dec.blocks.levels);
    if (id < 0 && dec.imageOk) id = renderer_.createTexture(dec.image.pixels.data(), dec.image.width, dec.image.height);
    if (id < 0 && dec.blocksOk && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    return emiIds_[material] = id;''')

# ---- normal maps
patch(C, '''    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    // The same size rule as the colour images (they had always loaded at full preview size, over the budget). BC1 /
    // BC3 / BC7 normal maps go up compressed (every channel as stored); BC5 and the rest are decoded.
    app::DdsBlockLevels blocks;
    if (!qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES") && app::loadDdsBlocks(path.toStdString(), textureSideNow(), blocks))
        id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
    if (id < 0 && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);''', '''    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    // The same size rule as the colour images (they had always loaded at full preview size, over the budget). BC1 /
    // BC3 / BC7 normal maps go up compressed (every channel as stored); BC5 and the rest are decoded.
    DecodedTexture dec;
    if (!decodedTexture(path.toStdString(), textureSideNow(), 0, dec)) {  // a worker is decoding it
        --loadsThisFrame_;
        moreToLoad_ = true;
        return -1;
    }
    if (dec.blocksOk) id = renderer_.createTextureBlocks(dec.blocks.dxgiFormat, dec.blocks.width, dec.blocks.height, dec.blocks.levels);
    if (id < 0 && dec.imageOk) id = renderer_.createTexture(dec.image.pixels.data(), dec.image.width, dec.image.height);
    if (id < 0 && dec.blocksOk && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);''')
print('ok')
