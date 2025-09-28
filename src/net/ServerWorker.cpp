// src/net/ServerWorker.cpp
#include "net/ServerWorker.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonArray>
#include <QAbstractSocket>
#include <QDebug>

ServerWorker::ServerWorker(QObject* p)
    : QObject(p),
      srv_(new QTcpServer(this)) {
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
                ? QString("Servidor escuchando en %1:%2")
                      .arg(srv_->serverAddress().toString())
                      .arg(srv_->serverPort())
                : QString("Fallo al escuchar: %1").arg(srv_->errorString()));
    qDebug() << "[ServerWorker] listen:" << ok
             << "addr=" << addr.toString()
             << "port=" << port;
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
    qDebug() << "[ServerWorker] client connected"
             << cli_->peerAddress().toString()
             << cli_->peerPort();
}

void ServerWorker::onReadyRead() {
    if (!cli_) return;
    buf_.append(cli_->readAll());

    for (;;) {
        const int idx = buf_.indexOf('\n');
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

// -------------------------------
// JSON → MetricsSnapshot
// -------------------------------
namespace {
static bool parseHexPtrToU64(const QString& s, quint64& out) {
    bool ok = false;
    const QString t = s.trimmed();
    if (t.startsWith("0x", Qt::CaseInsensitive)) {
        out = t.mid(2).toULongLong(&ok, 16);
    } else {
        out = t.toULongLong(&ok, 10);
    }
    return ok;
}
} // namespace

void ServerWorker::parseSnapshotJson(const QJsonObject& o) {
    // 1) GENERAL
    const QJsonObject g = o.value("general").toObject();
    if (g.isEmpty()) {
        qDebug() << "[ServerWorker] JSON sin 'general'";
        return;
    }

    MetricsSnapshot s{};
    s.heapCurrent = static_cast<qulonglong>(g.value("heap_current").toDouble(0));
    s.heapPeak    = static_cast<qulonglong>(g.value("heap_peak").toDouble(0));
    s.allocRate   = g.value("alloc_rate").toDouble(0.0);
    s.freeRate    = g.value("free_rate").toDouble(0.0);
    s.uptimeMs    = static_cast<qulonglong>(g.value("uptime_ms").toDouble(0));
    s.activeAllocs= static_cast<qulonglong>(g.value("active_allocs").toDouble(0));
    s.totalAllocs = static_cast<qulonglong>(g.value("total_allocs").toDouble(0));
    s.leakBytes   = static_cast<qulonglong>(g.value("leak_bytes").toDouble(0));

    // 2) BINS
    s.bins.clear();
    const QJsonArray bins = o.value("bins").toArray();
    s.bins.reserve(bins.size());
    for (const QJsonValue& v : bins) {
        const QJsonObject b = v.toObject();
        BinRange br;
        br.lo          = static_cast<qulonglong>(b.value("lo").toDouble(0));
        br.hi          = static_cast<qulonglong>(b.value("hi").toDouble(0));
        br.bytes       = static_cast<qlonglong>(b.value("bytes").toDouble(0));
        br.allocations = b.value("allocations").toInt(0);
        s.bins.push_back(br);
    }

    // 3) PER FILE
    s.perFile.clear();
    const QJsonArray perFile = o.value("per_file").toArray();
    s.perFile.reserve(perFile.size());
    for (const QJsonValue& v : perFile) {
        const QJsonObject pf = v.toObject();
        FileStat fs;
        fs.file       = pf.value("file").toString();
        fs.totalBytes = static_cast<qlonglong>(pf.value("totalBytes").toDouble(0));
        fs.allocs     = pf.value("allocs").toInt(0);
        fs.frees      = pf.value("frees").toInt(0);
        fs.netBytes   = static_cast<qlonglong>(pf.value("netBytes").toDouble(0));
        s.perFile.push_back(fs);
    }

    // 4) LEAKS (bloques vivos)
    s.leaks.clear();
    const QJsonArray leaks = o.value("leaks").toArray();
    s.leaks.reserve(leaks.size());
    for (const QJsonValue& v : leaks) {
        const QJsonObject L = v.toObject();
        LeakItem li;
        const QJsonValue jp = L.value("ptr");
        if (jp.isString()) {
            quint64 ptrVal = 0;
            if (parseHexPtrToU64(jp.toString(), ptrVal)) li.ptr = ptrVal;
        } else if (jp.isDouble()) {
            li.ptr = static_cast<qulonglong>(jp.toDouble(0));
        }
        li.size  = static_cast<qlonglong>(L.value("size").toDouble(0));
        li.file  = L.value("file").toString();
        li.line  = L.value("line").toInt(0);
        li.type  = L.value("type").toString();
        li.ts_ns = static_cast<qulonglong>(L.value("ts_ns").toDouble(0));
        s.leaks.push_back(li);
    }

    qDebug() << "[ServerWorker] snapshot:"
             << "cur=" << s.heapCurrent
             << "peak=" << s.heapPeak
             << "active=" << s.activeAllocs
             << "total=" << s.totalAllocs
             << "leakB=" << s.leakBytes
             << "bins=" << s.bins.size()
             << "perFile=" << s.perFile.size()
             << "leaks=" << s.leaks.size();

    emit snapshotReady(s);
}
