import io

# Plan 3.2, the two open points: the log backlog with filters, and an undo history list.
# - GET /api/log takes level (warning | error = at least that), source and q (text), and every line carries its level;
#   the page loads the backlog when it connects and filters what it shows.
# - Web form "history" (Edit > Undo History): the document's steps; choosing one undoes or redoes to it.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


S = 'src/qtapp/web_server.cpp'
patch(S, '''        broadcast("log", json({{"seq", double(t.next - 1)}, {"source", e.source}, {"text", e.text}}));''',
      '''        broadcast("log", json({{"seq", double(t.next - 1)}, {"source", e.source}, {"text", e.text}, {"level", int(e.level)}}));''')
patch(S, '''            const qint64 since = q.queryItemValue("since").toLongLong();
            QJsonArray lines;
            for (const auto& [seq, e] : logTail().lines)
                if (seq >= since) lines.append(QJsonObject{{"seq", double(seq)}, {"source", e.source}, {"text", e.text}});''',
      '''            // The kept lines from `since` on; level = warning | error (at least that), source = one source, q = text.
            const qint64 since = q.queryItemValue("since").toLongLong();
            const QString lv = q.queryItemValue("level"), src = q.queryItemValue("source"), needle = q.queryItemValue("q", QUrl::FullyDecoded);
            const int minLevel = lv == "error" ? int(LogLevel::Error) : lv == "warning" ? int(LogLevel::Warning) : int(LogLevel::Info);
            QJsonArray lines;
            for (const auto& [seq, e] : logTail().lines) {
                if (seq < since || int(e.level) < minLevel) continue;
                if (!src.isEmpty() && e.source != src) continue;
                if (!needle.isEmpty() && !e.text.contains(needle, Qt::CaseInsensitive)) continue;
                lines.append(QJsonObject{{"seq", double(seq)}, {"source", e.source}, {"text", e.text}, {"level", int(e.level)}});
            }''')

W = 'src/qtapp/main_window_web.cpp'
patch(W, '''                      QJsonObject{{"id", "surface"}, {"title", "Surface (texture of the selection)"}, {"command", ""}, {"menu", "Edit"}},''',
      '''                      QJsonObject{{"id", "history"}, {"title", "Undo History"}, {"command", ""}, {"menu", "Edit"}},
                      QJsonObject{{"id", "surface"}, {"title", "Surface (texture of the selection)"}, {"command", ""}, {"menu", "Edit"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
    if (id == "history") {
        // The document's steps, the newest 200 at most: choosing one undoes or redoes to it.
        const auto& h = ctx_.doc.history();
        const size_t pos = ctx_.doc.undoPosition(), first = h.size() > 200 ? h.size() - 200 : 0;
        QJsonArray steps, labels;
        for (size_t i = first; i <= h.size(); ++i) {
            steps.append(QString::number(i));
            labels.append(i == 0 ? QString("0: (opened document)") : QString("%1: %2%3").arg(i).arg(qs(h[i - 1].name), i > pos ? "  (undone)" : ""));
        }
        QJsonArray f{QJsonObject{{"key", "step"}, {"label", QString("Go to step (now at %1 of %2)").arg(pos).arg(h.size())}, {"type", "choice"},
                                 {"value", QString::number(std::max(pos, first))}, {"choices", steps}, {"choiceLabels", labels},
                                 {"help", "Steps after the chosen one are undone; they stay in the list until a new edit replaces them"}}};
        return QJsonObject{{"id", id}, {"title", "Undo History"}, {"fields", f}, {"reload", true}};
    }
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "history") {
        if (!values.contains("step")) return true;
        bool ok = false;
        const qlonglong want = values.value("step").toString().toLongLong(&ok);
        if (!ok || want < 0 || size_t(want) > ctx_.doc.history().size()) return no("there is no such step");
        size_t guard = ctx_.doc.history().size() + 1;
        while (ctx_.doc.undoPosition() > size_t(want) && guard-- > 0 && ctx_.doc.undo()) {}
        while (ctx_.doc.undoPosition() < size_t(want) && guard-- > 0 && ctx_.doc.redo()) {}
        ctx_.documentEdited(QString("History: step %1").arg(ctx_.doc.undoPosition()));
        return ctx_.doc.undoPosition() == size_t(want) ? true : no("the document could not go to that step");
    }
''')

# ---- the page
patch('data/web/index.html', '''      <h2>Log</h2>''', '''      <h2>Log
        <select id="logLevel" title="Which lines the log shows"><option value="0">All</option><option value="1">Warnings and errors</option><option value="2">Errors</option></select>
        <input id="logFilter" placeholder="filter" title="Show only the lines that hold this text"></h2>''')
J = 'data/web/app.js'
patch(J, '''  es.addEventListener("log", (ev) => {
    try {
      const l = JSON.parse(ev.data), pre = $("log");
      pre.textContent += `[${l.source}] ${l.text}\\n`;
      if (pre.textContent.length > 20000) pre.textContent = pre.textContent.slice(-15000);
      pre.scrollTop = pre.scrollHeight;
    } catch (_) {}
  });''', '''  es.addEventListener("log", (ev) => { try { addLogLine(JSON.parse(ev.data)); } catch (_) {} });
  // The lines from before this page was opened (the editor keeps the last ones).
  get("/api/log?since=0").then((j) => { for (const l of j.lines) addLogLine(l, true); renderLog(); }).catch(() => {});''')
patch(J, '''function connect() {''', '''// The log (plan 3.2): the kept lines, shown through the level and text filters of the Log panel.
const logLines = [];
let logSeen = -1;
function logShown(l) {
  const f = $("logFilter").value.trim().toLowerCase();
  return (l.level || 0) >= Number($("logLevel").value) && (!f || (l.source + " " + l.text).toLowerCase().includes(f));
}
const logText = (l) => `${l.level === 2 ? "ERROR " : l.level === 1 ? "warning " : ""}[${l.source}] ${l.text}\\n`;
function renderLog() {
  const pre = $("log");
  pre.textContent = logLines.filter(logShown).slice(-400).map(logText).join("");
  pre.scrollTop = pre.scrollHeight;
}
function addLogLine(l, quiet) {
  if (typeof l.seq === "number") { if (l.seq <= logSeen) return; logSeen = l.seq; }   // the backlog and the stream overlap
  logLines.push(l);
  if (logLines.length > 3000) logLines.splice(0, 1000);
  if (quiet || !logShown(l)) return;
  const pre = $("log");
  pre.textContent += logText(l);
  if (pre.textContent.length > 40000) renderLog();
  pre.scrollTop = pre.scrollHeight;
}
$("logFilter").oninput = renderLog;
$("logLevel").onchange = renderLog;

function connect() {''')
patch('data/web/style.css', '''.log pre {''', '''.log h2 select, .log h2 input { font-size: 11px; margin-left: 6px; }
.log h2 input { width: 90px; }
.log pre {''')

# ---- GUI test
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 10 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 11 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Compile options: the dock's map id and switches, and the Build Budget as a report.''', '''                {
                    // Undo History: the steps as a choice; choosing one undoes or redoes to it. And the log's filters.
                    const size_t undoH = ctx.doc.undoPosition();
                    fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"History One\\"}}", true, {});
                    fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"History Two\\"}}", true, {});
                    const bool two = ctx.doc.undoPosition() == undoH + 2 && key("iw8r_title") == "History Two";
                    const QJsonObject hf = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=history", {}, true, {}).body).object();
                    const QJsonObject stepField = hf.value("fields").toArray().first().toObject();
                    const bool listed = stepField.value("value").toString() == QString::number(undoH + 2) &&
                                        stepField.value("choices").toArray().size() >= 3;
                    const auto back = fweb.handle("POST", "/api/form", QString("{\\"id\\":\\"history\\",\\"values\\":{\\"step\\":\\"%1\\"}}").arg(undoH + 1).toUtf8(), true, {});
                    const bool one = ctx.doc.undoPosition() == undoH + 1 && key("iw8r_title") == "History One";
                    const auto fwd = fweb.handle("POST", "/api/form", QString("{\\"id\\":\\"history\\",\\"values\\":{\\"step\\":\\"%1\\"}}").arg(undoH + 2).toUtf8(), true, {});
                    const bool again = ctx.doc.undoPosition() == undoH + 2 && key("iw8r_title") == "History Two";
                    const auto far = fweb.handle("POST", "/api/form", "{\\"id\\":\\"history\\",\\"values\\":{\\"step\\":\\"999999\\"}}", true, {});
                    while (ctx.doc.undoPosition() > undoH) ctx.doc.undo();
                    ctx.documentEdited();
                    EditorLog::instance().add(LogLevel::Warning, "webtest", "a warning line for the web log filter");
                    EditorLog::instance().add(LogLevel::Info, "webtest", "an info line for the web log filter");
                    QApplication::processEvents();
                    const auto count = [&](const char* query) {
                        int n = 0;
                        for (const QJsonValue& l : QJsonDocument::fromJson(fweb.handle("GET", query, {}, true, {}).body).object().value("lines").toArray())
                            n += l.toObject().value("source").toString() == "webtest" ? 1 : 0;
                        return n;
                    };
                    const int all = count("/api/log?since=0"), warns = count("/api/log?since=0&level=warning"), errs = count("/api/log?since=0&level=error");
                    const int text = count("/api/log?since=0&q=INFO%20line"), other = count("/api/log?since=0&source=compile");
                    r.check(two && listed && back.status == 200 && one && fwd.status == 200 && again && far.status == 400 && all == 2 && warns == 1 &&
                                errs == 0 && text == 1 && other == 0,
                            QString("the Undo History form goes back and forward by step; the log is filtered by level, source and text (%1 %2 %3 %4 %5)")
                                .arg(all).arg(warns).arg(errs).arg(text).arg(other));
                }
                {
                    // Compile options: the dock's map id and switches, and the Build Budget as a report.''')
print('ok')

# The kept log lines are filled once per process, not once per server object (the GUI test runs several servers and
# every line was kept that many times).
patch(S, '''    connect(&EditorLog::instance(), &EditorLog::added, this, [this](const LogEntry& e) {
        LogTail& t = logTail();
        t.lines.append({t.next++, e});
        while (t.lines.size() > 2000) t.lines.removeFirst();
        broadcast("log", json({{"seq", double(t.next - 1)}, {"source", e.source}, {"text", e.text}, {"level", int(e.level)}}));
    });''', '''    static bool tailHooked = false;
    if (!tailHooked) {
        // One keeper for the process, connected before any server's own slot: the line is kept when a server sends it.
        tailHooked = true;
        QObject::connect(&EditorLog::instance(), &EditorLog::added, &EditorLog::instance(), [](const LogEntry& e) {
            LogTail& t = logTail();
            t.lines.append({t.next++, e});
            while (t.lines.size() > 2000) t.lines.removeFirst();
        });
    }
    connect(&EditorLog::instance(), &EditorLog::added, this, [this](const LogEntry& e) {
        broadcast("log", json({{"seq", double(logTail().next - 1)}, {"source", e.source}, {"text", e.text}, {"level", int(e.level)}}));
    });''')
print('ok 2')
