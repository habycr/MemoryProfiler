// src/net/MetricsWorker.h
#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>              // <-- agrega esto
#include "model/MetricsSnapshot.h"

class MetricsWorker : public QObject {
    Q_OBJECT
  public:
    explicit MetricsWorker(QObject* parent=nullptr);
    void start(const QString& host, quint16 port);
    signals:
      void snapshotReady(const MetricsSnapshot& snap);
    void statusChanged(const QString& status);
public slots:
  void stop();
private slots:
  void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError);
    void doReconnect();
private:
    void tryConnect();
    void applyEventLine(const QByteArray& line);   // <-- declara aquí

    QTcpSocket* sock_ = nullptr;
    QByteArray buf_;
    MetricsSnapshot current_;
    QTimer* reconnectTimer_ = nullptr;
    QString host_;
    quint16 port_ = 0;
};

