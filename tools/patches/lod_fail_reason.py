import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'src/qtapp/model_cache.cpp'
patch(C, '''                emit progress(QString("Prop LODs from %1: %2 of %3 (exit %4)").arg(zone).arg(ok).arg(part.size()).arg(result.exitCode));''',
      '''                // A run that failed as a whole says why (the server's error, else the last lines of the tool's output).
                QString why;
                if (ok == 0 && result.exitCode != 0 && result.exitCode != 127) {
                    why = result.error.trimmed();
                    if (why.isEmpty()) {
                        const QList<QByteArray> lines = result.log.trimmed().split('\\n');
                        for (qsizetype k = std::max<qsizetype>(0, lines.size() - 2); k < lines.size(); ++k)
                            why += (why.isEmpty() ? "" : " | ") + QString::fromUtf8(lines[k]).trimmed();
                    }
                    if (!result.started) why = "acts.exe could not be started";
                    if (!why.isEmpty()) why = ": " + why.left(300);
                }
                emit progress(QString("Prop LODs from %1: %2 of %3 (exit %4)%5").arg(zone).arg(ok).arg(part.size()).arg(result.exitCode).arg(why));''')
print('ok')
