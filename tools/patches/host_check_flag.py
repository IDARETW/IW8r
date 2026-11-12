import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/main.cpp', [
(r'''        else if (a == "--cover-check") coverCheck = true;''',
 r'''        else if (a == "--cover-check") coverCheck = true;
        else if (a == "--f5-check") f5Check = true;'''),
(r'''    bool coverCheck = false;
''', r'''    bool coverCheck = false;
    bool f5Check = false;
'''),
(r'''    if (coverCheck) {''',
 r'''    if (f5Check) {
        // What F5 would do before it starts the game, against the real install: which exe it picks, whether that exe
        // takes the start request, and whether the local host answers the health probe. Starts nothing, writes only
        // the report (--out <file>, default f5-check.txt beside the exe).
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        using iw8r::qt::DevmapRunner;
        const QString replay = iw8r::qt::Settings::instance().value("tools/replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe").toString();
        QString note;
        const QString exe = DevmapRunner::chooseLaunchExe(iw8r::qt::Settings::instance().value("tools/launch").toString(), replay, &note);
        QStringList report;
        report << "exe: " + exe << "note: " + note;
        report << QString("exe exists: %1").arg(QFileInfo::exists(exe) ? "yes" : "NO");
        report << QString("client start request (DW_DEVMAP marker): %1").arg(DevmapRunner::exeHasClientRequest(exe) ? "yes" : "no");
        const QString host = DevmapRunner::localHostOf(exe);
        report << "local host named by the exe's ini: " + (host.isEmpty() ? QString("(none)") : host);
        report << "health address: " + DevmapRunner::hostHealthSetting();
        const bool up = DevmapRunner::probeHost(DevmapRunner::hostHealthSetting());
        report << QString("host answers the probe: %1").arg(up ? "yes" : "NO");
        report << "host command: " + (DevmapRunner::hostCommandSetting().isEmpty() ? QString("(none)") : DevmapRunner::hostCommandSetting());
        const QString out = outDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/f5-check.txt" : outDir;
        QFile f(out);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(report.join(QChar(10)).toUtf8() + QByteArray(1, char(10)));
        return QFileInfo::exists(exe) && (host.isEmpty() || up) ? 0 : 1;
    }
    if (coverCheck) {'''),
])
s = io.open('src/qtapp/main.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
for inc in ('#include "devmap_launch.h"', '#include "settings.h"'):
    if inc not in s:
        a = '#include "devmap_cover.h"'
        assert s.count(a) == 1
        s = s.replace(a, a + nl + inc)
io.open('src/qtapp/main.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
