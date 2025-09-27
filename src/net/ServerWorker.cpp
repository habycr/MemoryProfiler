#include "net/ServerWorker.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QAbstractSocket>
#include <QDebug>

ServerWorker::ServerWorker(QObject* p) : QObject(p), srv_(new QTcpServer(this)) {
    connect(srv_, &QTcpServer::newConnection, this, &ServerWorker::onNewConnection);
}

ServerWorker::~ServerWorker() {
    stop();
}

bool ServerWorker::listen(const QHostAddress& addr, quint16 port) {
    if (srv_->isListening())
        srv_->close();
    const bool ok = srv_->listen(addr, port);
    emit status(ok
                ? QString("Servidor escuchando en %1:%2").arg(srv_->serverAddress().toString()).arg(srv_->serverPort())
                : QString("Fallo al escuchar: %1").arg(srv_->errorString()));
    qDebug() << "[ServerWorker] listen:" << ok << "addr=" << addr.toString() << "port=" << port;
    return ok;
}

void ServerWorker::stop() {
    if (cli_) {
        cli_->disconnect(this);
        cli_->disconnectFromHost();
        cli_->deleteLater();
        cli_ = nullptr;
    }
    if (srv_->isListening())
        srv_->close();
}

void ServerWorker::onNewConnection() {
    QTcpSocket* s = srv_->nextPendingConnection();
    if (!s) return;

    if (cli_) {
        disconnect(cli_, nullptr, this, nullptr);
        cli_->disconnectFromHost();
        cli_->deleteLater();
        cli_ = nullptr;
    }

    cli_ = s;
    connect(cli_, &QTcpSocket::readyRead,    this, &ServerWorker::onReadyRead);
    connect(cli_, &QTcpSocket::disconnected, this, &ServerWorker::onDisconnected);

    emit status(QString("Cliente conectado desde %1:%2")
                .arg(cli_->peerAddress().toString())
                .arg(cli_->peerPort()));
    qDebug() << "[ServerWorker] client connected" << cli_->peerAddress().toString() << cli_->peerPort();
}

void ServerWorker::onReadyRead() {
    if (!cli_) return;
    buf_.append(cli_->readAll());

    for (;;) {
        int idx = buf_.indexOf('\n');
        if (idx < 0) break;
        QByteArray line = buf_.left(idx);
        buf_.remove(0, idx + 1);
        processLine(line.trimmed());
    }
}

void ServerWorker::onDisconnected() {
    emit status("Cliente desconectado");
    qDebug() << "[ServerWorker] client disconnected";
    if (cli_) {
        cli_->deleteLater();
        cli_ = nullptr;
    }
}

void ServerWorker::processLine(const QByteArray& line) {
    if (line.isEmpty()) return;

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "[ServerWorker] JSON inválido:" << pe.errorString()
                 << "first120=" << QString::fromUtf8(line.left(120));
        return;
    }
    parseSnapshotJson(doc.object());
}

void ServerWorker::parseSnapshotJson(const QJsonObject& o) {
    const QJsonObject g = o.value("general").toObject();
    if (g.isEmpty()) {
        qDebug() << "[ServerWorker] JSON sin 'general'";
        return;
    }

    MetricsSnapshot s{};
    s.heapCurrent = static_cast<qulonglong>(g.value("heap_current").toDouble());
    s.heapPeak    = static_cast<qulonglong>(g.value("heap_peak").toDouble());
    s.allocRate   = g.value("alloc_rate").toDouble();
    s.freeRate    = g.value("free_rate").toDouble();
    s.uptimeMs    = static_cast<qulonglong>(g.value("uptime_ms").toDouble());
    s.activeAllocs= static_cast<qulonglong>(g.value("active_allocs").toDouble(0));
    s.totalAllocs = static_cast<qulonglong>(g.value("total_allocs").toDouble(0));
    s.leakBytes   = static_cast<qulonglong>(g.value("leak_bytes").toDouble(0));

    qDebug() << "[ServerWorker] snapshot:"
             << "cur=" << s.heapCurrent
             << "peak=" << s.heapPeak
             << "active=" << s.activeAllocs
             << "total=" << s.totalAllocs
             << "leakB=" << s.leakBytes;

    emit snapshotReady(s);
}
