import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/devmap_launch.cpp', [
(r'''    sock.write("GET " + path + " HTTP/1.0\r\nHost: " + url.host().toLatin1() + "\r\nConnection: close\r\n\r\n");''',
 r'''    // HTTP/1.1 with a Host header: the real host answers an HTTP/1.0 request with "400 invalid_request" (owner
    // report 2026-11-11: F5 never started the game, because the host that was running was never recognised).
    sock.write("GET " + path + " HTTP/1.1\r\nHost: " + url.host().toLatin1() + ":" + QByteArray::number(url.port(80)) +
               "\r\nUser-Agent: IW8Radiant\r\nAccept: */*\r\nConnection: close\r\n\r\n");'''),
])

edit('src/qtapp/qt_gui_test.cpp', [
(r'''            c->waitForReadyRead(2000);
            c->write("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"server\":\"iw8-dw\",\"local_qos_enabled\":true}");''',
 r'''            c->waitForReadyRead(2000);
            // Like the real host: only an HTTP/1.1 request with a Host header gets the health answer.
            const QByteArray request = c->readAll();
            if (request.startsWith("GET /health HTTP/1.1\r\n") && request.contains("\r\nHost: 127.0.0.1:"))
                c->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"server\":\"iw8-dw\",\"local_qos_enabled\":true}");
            else
                c->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"invalid_request\"}");'''),
(r'''        r.check(probed, "the host probe accepts a health answer whose server is iw8-dw");''',
 r'''        r.check(probed, "the host probe sends a request the real host accepts (HTTP/1.1 with Host) and reads its health answer");'''),
])
print('ok')
