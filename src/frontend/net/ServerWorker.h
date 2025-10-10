#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "../../../include/memprof/proto/MetricsSnapshot.h"

class ServerWorker : public QObject {
    Q_OBJECT
public:
    explicit ServerWorker(QObject* parent = nullptr);
    ~ServerWorker() override;

    bool listen(const QHostAddress& addr, quint16 port);
    void stop();

    signals:
        void status(const QString& text);
    void snapshotReady(const MetricsSnapshot& s);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void processLine(const QByteArray& line);
    void parseSnapshotJson(const QJsonObject& o);

private:
    QTcpServer*  srv_ = nullptr;
    QTcpSocket*  cli_ = nullptr;
    QByteArray   buf_;
};
