// IW8 Radiant Suite - UPnP IGD port mapping for the web server (roadmap 10.1, owner request 2026-11-10).
//
// Discovers the router with SSDP (M-SEARCH for WANIPConnection / WANPPPConnection on 239.255.255.250:1900), reads its
// device description, then asks it over SOAP to forward an external TCP port to this machine (AddPortMapping, renewed
// before its lease runs out) and for the public address (GetExternalIPAddress). remove() deletes the mapping.
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QTimer;
class QUdpSocket;

namespace iw8r::qt {

class UpnpPortMapper : public QObject {
    Q_OBJECT
public:
    explicit UpnpPortMapper(QObject* parent = nullptr);
    ~UpnpPortMapper() override;

    // Maps external `port` to this machine's `port` (TCP). Async: emits changed() as it progresses.
    void map(quint16 port, const QString& description);
    void remove();  // deletes the mapping (best effort)

    bool mapped() const { return mapped_; }
    QString externalAddress() const { return externalIp_; }
    QString status() const { return status_; }

signals:
    void changed();

private:
    void discover();
    void onSsdp();
    void fetchDescription(const QUrl& location);
    void soap(const QString& action, const QString& args, std::function<void(bool ok, const QByteArray& reply)> done);
    void addMapping();
    void setStatus(const QString& s);
    QString localAddressFor(const QUrl& router) const;

    QNetworkAccessManager* net_;
    QUdpSocket* udp_ = nullptr;
    QTimer* timeout_;
    QTimer* renew_;
    QUrl control_;
    QString serviceType_;
    QString localIp_, externalIp_, description_, status_;
    quint16 port_ = 0;
    bool mapped_ = false;
    bool searching_ = false;
};

}  // namespace iw8r::qt
