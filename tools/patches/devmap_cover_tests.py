import io
p = 'tests/devmap_plan_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
add = r'''
TEST(devmap_cover_rules) {
    // A game the editor did not start is never covered.
    CHECK(!coverWanted(false, -1, GameSnapshot{}));
    // From the launch until the start request: covered, whatever the game shows (boot, mode select, menu, lobby).
    CHECK(coverWanted(true, -1, GameSnapshot{}));
    CHECK(coverWanted(true, -1, frontEnd()));
    CHECK(coverWanted(true, -1, lobbyHost()));
    // After the start request: only while the front end is still on screen, and never longer than the limit.
    CHECK(coverWanted(true, 100, lobbyHost()));
    CHECK(!coverWanted(true, kCoverMaxAfterStartMs, lobbyHost()));
    GameSnapshot loading = lobbyHost();
    loading.frontEndState = 0;
    CHECK(!coverWanted(true, 100, loading));
    CHECK(!coverWanted(true, 100, GameSnapshot{}));  // unreadable while the map loads
    // In a match the card is gone even if no start request was seen (the client started the map by itself).
    CHECK(!coverWanted(true, -1, matchHost("mp_iw8r_test")));

    CHECK(clientLineIsStart("start requested mp_iw8r_test war"));
    CHECK(clientLineIsStart("loading mp_iw8r_test"));
    CHECK(!clientLineIsStart("request mp_iw8r_test war") && !clientLineIsStart("hosting") && !clientLineIsStart(""));
}
'''
s = s.rstrip('\r\n') + nl + add.replace('\n', nl)
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a = r'''        r.check(!CompilePanel::gameRunningFrom(pick), "the fake exe was not started");
    }'''
b = r'''        r.check(!CompilePanel::gameRunningFrom(pick), "the fake exe was not started");

        // The cover card of a cold start: over a given rectangle, topmost, never focused, clicks pass through.
        {
            DevmapCover cover;
            cover.setMap("mp_iw8r_test", "war");
            for (int i = 0; i < DevmapCover::kMaxLines + 3; ++i) cover.addLine(QString("+%1.0 s: line %1").arg(i));
            cover.setElapsed(12.3);
            const QRect want(120, 90, 800, 450);
            QWidget* focusBefore = QApplication::focusWidget();
            cover.coverRect(want);
            QApplication::processEvents();
            r.check(cover.isVisible() && cover.coveredRect() == want, "the cover card shows over the rectangle it is given");
            r.check(cover.lines().size() == DevmapCover::kMaxLines && cover.lines().last().contains("line 16"),
                    "the cover card keeps the latest timeline lines");
            const Qt::WindowFlags f = cover.windowFlags();
            r.check((f & Qt::WindowStaysOnTopHint) && (f & Qt::WindowDoesNotAcceptFocus) && (f & Qt::WindowTransparentForInput) &&
                        cover.testAttribute(Qt::WA_ShowWithoutActivating),
                    "the cover card is topmost, never takes focus and passes clicks through");
            r.check(QApplication::focusWidget() == focusBefore && !cover.isActiveWindow(), "showing the cover card does not move the focus");
            const QImage shot = cover.grab().toImage();
            int bright = 0;
            for (int y = 0; y < shot.height(); y += 3)
                for (int x = 0; x < shot.width(); x += 3) bright += qGray(shot.pixel(x, y)) > 120;
            r.check(shot.width() > 100 && bright > 50 && qGray(shot.pixel(2, 2)) < 40, "the cover card is a dark card with the map name and the timeline");
            shot.save(outDir + "/devmap_cover.png");
            cover.hide();
        }
        w.show();
        QApplication::processEvents();
        r.check(DevmapCover::gameWindowRect(quint32(QCoreApplication::applicationPid())).isValid() &&
                    DevmapCover::gameWindowRect(0xFFFFFFF0u).isNull(),
                "a process's main window rectangle is found; a process without a window gives none");
    }'''
assert s.count(a) == 1
s = s.replace(a, b)
a = '#include "devmap_launch.h"'
assert s.count(a) == 1
s = s.replace(a, '#include "devmap_cover.h"\n' + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
print('ok')
