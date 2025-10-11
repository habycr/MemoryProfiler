#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>

#include "memprof/proto/MetricsSnapshot.h"

class ServerWorker : public QObject {
    Q_OBJECT
public:
    explicit ServerWorker(QObject* parent = nullptr);

public slots:
    void listen(const QHostAddress& addr = QHostAddress::LocalHost, quint16 port = 7070);
    void stop();

    signals:
        // Pasamos un puntero compartido para evitar copias grandes entre hilos
        void snapshotReady(QSharedPointer<const MetricsSnapshot> s);
    void status(const QString& s);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void flushCoalesced();   // emite el último snapshot cada ~80 ms

private:
    MetricsSnapshot parseSnapshotJson(const QJsonObject& obj) const;

    QTcpServer* server_ = nullptr;
    QTcpSocket* sock_   = nullptr;

    QMutex m_;
    QByteArray buffer_;
    QTimer* flushTimer_ = nullptr;

    static constexpr int    kFlushMs   = 80;             // ~12.5 FPS
    static constexpr int    kMaxBufMB  = 8;              // tope de buffer
    static constexpr qint64 kMaxBuf    = qint64(kMaxBufMB) * 1024 * 1024;
};

// Nota: NO usar Q_DECLARE_METATYPE para QSharedPointer en Qt6 (ya está soportado).
