import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/asset_browser.h'
T = 'src/qtapp/qt_gui_test.cpp'
patch(H, '''    void setStatusSource(StatusSource fn) { statusSource_ = std::move(fn); refreshThumbnails(); }''',
      '''    void setStatusSource(StatusSource fn) { statusSource_ = std::move(fn); refreshThumbnails(); }
    StatusSource statusSource() const { return statusSource_; }  // tests''')
patch(T, '''        // tmo/uk_portable_radio_01: its images are only referenced from techsets_mp_donetsk (no data in Replay).
        AssetBrowser* mb = w.materialBrowser();
        mb->setFilter("tmo/uk_portable_radio_01");
        for (int i = 0; i < 1800 && mb->unavailableMarked() == 0; ++i) {
            if (i % 20 == 0) mb->refreshThumbnails();
            QTest::qWait(50);
        }
        r.check(mb->unavailableMarked() > 0, "a material Replay has no images for is greyed as not available");
        mb->setFilter("");''', '''        // A material the availability source reports as missing is greyed. Which stock materials lack their images
        // depends on the install and on which zones were catalogued (tmo/uk_portable_radio_01 used to be one until its
        // pixels were found in a streamed Verdansk tile, 2026-11-13), so the source stands in for the cache here.
        AssetBrowser* mb = w.materialBrowser();
        const AssetBrowser::StatusSource real = mb->statusSource();
        mb->setStatusSource([real](const QString& m) -> QString {
            if (m == "tmo/uk_portable_radio_01") return "test: no images in this install";
            return real ? real(m) : QString();
        });
        mb->setFilter("tmo/uk_portable_radio_01");
        for (int i = 0; i < 1800 && mb->unavailableMarked() == 0; ++i) {
            if (i % 20 == 0) mb->refreshThumbnails();
            QTest::qWait(50);
        }
        r.check(mb->unavailableMarked() > 0, "a material Replay has no images for is greyed as not available");
        mb->setFilter("");
        mb->setStatusSource(real);''')
print('ok')
