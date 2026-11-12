#include "upnp.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace iw8r::qt {

namespace {
constexpr int kLeaseSeconds = 3600;  // renewed at half-life; routers that refuse leases get 0 (permanent until removed)

QString tag(const QString& xml, const QString& name, int from = 0) {
    const QRegularExpression re("<(?:\\w+:)?" + QRegularExpression::escape(name) + ">([^<]*)</(?:\\w+:)?" + QRegularExpression::escape(name) + ">");
    const auto m = re.match(xml, from);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}
}  // namespace

UpnpPortMapper::UpnpPortMapper(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {
    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] {
        if (searching_) {
            searching_ = false;
            setStatus("UPnP: no router answered (is UPnP enabled on it?)");
        }
    });
    renew_ = new QTimer(this);
    renew_->setInterval(kLeaseSeconds * 1000 / 2);
    connect(renew_, &QTimer::timeout, this, [this] {
        if (mapped_) addMapping();
    });
}

UpnpPortMapper::~UpnpPortMapper() { remove(); }

void UpnpPortMapper::setStatus(const QString& s) {
    status_ = s;
    emit changed();
}

void UpnpPortMapper::map(quint16 port, const QString& description) {
    port_ = port;
    description_ = description;
    if (control_.isValid()) return addMapping();
    discover();
}

void UpnpPortMapper::discover() {
    delete udp_;
    udp_ = new QUdpSocket(this);
    if (!udp_->bind(QHostAddress::AnyIPv4, 0)) return setStatus("UPnP: cannot open a UDP socket");
    connect(udp_, &QUdpSocket::readyRead, this, &UpnpPortMapper::onSsdp);
    searching_ = true;
    setStatus("UPnP: looking for the router...");
    for (const char* st : {"urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANIPConnection:2",
                           "urn:schemas-upnp-org:service:WANPPPConnection:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:1"}) {
        const QByteArray msg = QByteArray("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st +
                               "\r\n\r\n";
        udp_->writeDatagram(msg, QHostAddress("239.255.255.250"), 1900);
    }
    timeout_->start(5000);
}

void UpnpPortMapper::onSsdp() {
    while (udp_ && udp_->hasPendingDatagrams()) {
        QByteArray data(int(udp_->pendingDatagramSize()), 0);
        udp_->readDatagram(data.data(), data.size());
        if (!searching_) continue;
        const QRegularExpression re("(?im)^location:\\s*(\\S+)");
        const auto m = re.match(QString::fromUtf8(data));
        if (!m.hasMatch()) continue;
        searching_ = false;
        timeout_->stop();
        fetchDescription(QUrl(m.captured(1)));
    }
}

void UpnpPortMapper::fetchDescription(const QUrl& location) {
    QNetworkReply* r = net_->get(QNetworkRequest(location));
    connect(r, &QNetworkReply::finished, this, [this, r, location] {
        r->deleteLater();
        const QString xml = QString::fromUtf8(r->readAll());
        if (r->error() != QNetworkReply::NoError) return setStatus("UPnP: cannot read the router description: " + r->errorString());
        // The first WAN connection service with its control URL.
        for (const char* type : {"WANIPConnection:2", "WANIPConnection:1", "WANPPPConnection:1"}) {
            const int at = xml.indexOf(QString("urn:schemas-upnp-org:service:") + type);
            if (at < 0) continue;
            const QString ctrl = tag(xml, "controlURL", at);
            if (ctrl.isEmpty()) continue;
            const QString base = tag(xml, "URLBase");
            control_ = (base.isEmpty() ? location : QUrl(base)).resolved(QUrl(ctrl));
            serviceType_ = QString("urn:schemas-upnp-org:service:") + type;
            localIp_ = localAddressFor(location);
            return addMapping();
        }
        setStatus("UPnP: the router has no WAN connection service");
    });
}

QString UpnpPortMapper::localAddressFor(const QUrl& router) const {
    // The address this machine uses to reach the router (a socket to it tells).
    QTcpSocket probe;
    probe.connectToHost(router.host(), quint16(router.port(80)));
    if (probe.waitForConnected(1500)) {
        const QString ip = probe.localAddress().toString();
        probe.abort();
        if (!ip.isEmpty()) return ip;
    }
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) return a.toString();
    return {};
}

void UpnpPortMapper::soap(const QString& action, const QString& args, std::function<void(bool, const QByteArray&)> done) {
    QNetworkRequest req(control_);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
    req.setRawHeader("SOAPAction", ("\"" + serviceType_ + "#" + action + "\"").toUtf8());
    const QString body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                         "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
                         action + " xmlns:u=\"" + serviceType_ + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
    QNetworkReply* r = net_->post(req, body.toUtf8());
    connect(r, &QNetworkReply::finished, this, [r, done] {
        r->deleteLater();
        done(r->error() == QNetworkReply::NoError, r->readAll());
    });
}

void UpnpPortMapper::addMapping() {
    if (!control_.isValid() || localIp_.isEmpty()) return setStatus("UPnP: no router or local address");
    const auto args = [this](int lease) {
        return QString("<NewRemoteHost></NewRemoteHost><NewExternalPort>%1</NewExternalPort><NewProtocol>TCP</NewProtocol>"
                       "<NewInternalPort>%1</NewInternalPort><NewInternalClient>%2</NewInternalClient><NewEnabled>1</NewEnabled>"
                       "<NewPortMappingDescription>%3</NewPortMappingDescription><NewLeaseDuration>%4</NewLeaseDuration>")
            .arg(port_)
            .arg(localIp_, description_.toHtmlEscaped())
            .arg(lease);
    };
    soap("AddPortMapping", args(kLeaseSeconds), [this, args](bool ok, const QByteArray& reply) {
        const auto finish = [this] {
            mapped_ = true;
            renew_->start();
            soap("GetExternalIPAddress", {}, [this](bool ok2, const QByteArray& r2) {
                externalIp_ = ok2 ? tag(QString::fromUtf8(r2), "NewExternalIPAddress") : QString();
                setStatus(QString("UPnP: router forwards port %1 to %2%3").arg(port_).arg(localIp_)
                              .arg(externalIp_.isEmpty() ? QString() : " (public address " + externalIp_ + ")"));
            });
        };
        if (ok) return finish();
        // Some routers only take permanent mappings (error 725 OnlyPermanentLeasesSupported).
        if (QString::fromUtf8(reply).contains("725"))
            return soap("AddPortMapping", args(0), [this, finish](bool ok2, const QByteArray& r2) {
                if (ok2) return finish();
                setStatus("UPnP: the router refused the mapping: " + tag(QString::fromUtf8(r2), "errorDescription"));
            });
        setStatus("UPnP: the router refused the mapping: " + tag(QString::fromUtf8(reply), "errorDescription"));
    });
}

void UpnpPortMapper::remove() {
    renew_->stop();
    if (!mapped_ || !control_.isValid()) return;
    mapped_ = false;
    soap("DeletePortMapping",
         QString("<NewRemoteHost></NewRemoteHost><NewExternalPort>%1</NewExternalPort><NewProtocol>TCP</NewProtocol>").arg(port_),
         [](bool, const QByteArray&) {});
}

}  // namespace iw8r::qt
