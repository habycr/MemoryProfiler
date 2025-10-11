#include "ServerWorker.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>

namespace {
// --- util: convierte QJsonValue a quint64 soportando string (hex/dec) o número ---
static quint64 toU64(const QJsonValue& v, quint64 def = 0) {
    if (v.isUndefined() || v.isNull()) return def;

    if (v.isString()) {
        QString s = v.toString().trimmed();
        if (s.startsWith("0x", Qt::CaseInsensitive)) {
            bool ok = false;
            quint64 x = s.mid(2).toULongLong(&ok, 16);
            return ok ? x : def;
        } else {
            bool ok = false;
            quint64 x = s.toULongLong(&ok, 10);
            return ok ? x : def;
        }
    }

    // Si es número JSON, suele ser double y puede perder precisión > 2^53.
    // Pero intentamos igual:
    if (v.isDouble()) {
        // vía QVariant suele devolver 0 si es muy grande; probamos cast directo:
        const double d = v.toDouble();
        if (d < 0) return def;
        // cuidado con overflow; clamp
        const long double ld = static_cast<long double>(d);
        if (ld > static_cast<long double>(std::numeric_limits<quint64>::max())) return def;
        return static_cast<quint64>(ld);
    }

    // vía QVariant como fallback
    bool ok = false;
    quint64 x = v.toVariant().toULongLong(&ok);
    return ok ? x : def;
}

static qlonglong toI64(const QJsonValue& v, qlonglong def = 0) {
    if (v.isUndefined() || v.isNull()) return def;
    if (v.isString()) {
        bool ok = false;
        qlonglong x = v.toString().toLongLong(&ok, 10);
        return ok ? x : def;
    }
    if (v.isDouble()) {
        const double d = v.toDouble();
        const long double ld = static_cast<long double>(d);
        if (ld > static_cast<long double>(std::numeric_limits<qlonglong>::max())) return def;
        if (ld < static_cast<long double>(std::numeric_limits<qlonglong>::min())) return def;
        return static_cast<qlonglong>(ld);
    }
    bool ok = false;
    qlonglong x = v.toVariant().toLongLong(&ok);
    return ok ? x : def;
}

static int toInt(const QJsonValue& v, int def = 0) {
    if (v.isString()) {
        bool ok = false;
        int x = v.toString().toInt(&ok, 10);
        return ok ? x : def;
    }
    if (v.isDouble()) return v.toInt(def);
    bool ok = false;
    int x = v.toVariant().toInt(&ok);
    return ok ? x : def;
}
} // namespace

ServerWorker::ServerWorker(QObject* parent) : QObject(parent) {
    flushTimer_ = new QTimer(this);
    flushTimer_->setTimerType(Qt::CoarseTimer);
    flushTimer_->setInterval(kFlushMs);
    connect(flushTimer_, &QTimer::timeout, this, &ServerWorker::flushCoalesced);
}

void ServerWorker::listen(const QHostAddress& addr, quint16 port) {
    if (server_) { emit status(QStringLiteral("Server already listening")); return; }

    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &ServerWorker::onNewConnection);

    if (!server_->listen(addr, port)) {
        emit status(QStringLiteral("Listen failed: %1").arg(server_->errorString()));
        return;
    }
    emit status(QStringLiteral("Listening on %1:%2").arg(addr.toString()).arg(port));
    flushTimer_->start();
}

void ServerWorker::stop() {
    flushTimer_->stop();
    {
        QMutexLocker lk(&m_);
        buffer_.clear();
    }
    if (sock_) {
        disconnect(sock_, nullptr, this, nullptr);
        sock_->close();
        sock_->deleteLater();
        sock_ = nullptr;
    }
    if (server_) {
        disconnect(server_, nullptr, this, nullptr);
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    emit status(QStringLiteral("Stopped"));
}

void ServerWorker::onNewConnection() {
    // Solo un cliente; cierra extras.
    if (sock_) {
        if (auto extra = server_->nextPendingConnection()) {
            extra->close();
            extra->deleteLater();
        }
        return;
    }
    sock_ = server_->nextPendingConnection();
    sock_->setReadBufferSize(1 * 1024 * 1024); // 1 MB para evitar explosiones
    connect(sock_, &QTcpSocket::readyRead,    this, &ServerWorker::onReadyRead);
    connect(sock_, &QTcpSocket::disconnected, this, &ServerWorker::onDisconnected);
    emit status(QStringLiteral("Client connected"));
}

void ServerWorker::onDisconnected() {
    if (sock_) { sock_->deleteLater(); sock_ = nullptr; }
    emit status(QStringLiteral("Client disconnected"));
}

void ServerWorker::onReadyRead() {
    if (!sock_) return;
    QByteArray chunk = sock_->readAll();
    QMutexLocker lk(&m_);
    buffer_.append(chunk);

    // Tope duro: nos quedamos con el final del buffer
    if (buffer_.size() > kMaxBuf) {
        buffer_ = buffer_.right(kMaxBuf);
        // Alinear a inicio de línea para no partir JSON
        int nl = buffer_.indexOf('\n');
        if (nl > 0) buffer_ = buffer_.mid(nl + 1);
    }
}

void ServerWorker::flushCoalesced() {
    QByteArray chunk;
    { QMutexLocker lk(&m_); if (buffer_.isEmpty()) return; chunk.swap(buffer_); }

    // Tomar la ÚLTIMA línea completa no vacía
    int end = chunk.size() - 1;
    while (end >= 0 && (chunk[end] == '\n' || chunk[end] == '\r')) --end;
    if (end < 0) return;
    int start = chunk.lastIndexOf('\n', end);
    QByteArray lastLine = chunk.mid(start + 1, end - start);

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(lastLine, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    MetricsSnapshot tmp = parseSnapshotJson(doc.object());
    auto sp = QSharedPointer<const MetricsSnapshot>::create(std::move(tmp)); // sin copias grandes
    emit snapshotReady(sp);
}

// --- Parser robusto: acepta números o strings (hex/dec) ---
MetricsSnapshot ServerWorker::parseSnapshotJson(const QJsonObject& obj) const {
    MetricsSnapshot out;

    // ----- general -----
    if (obj.contains("general") && obj["general"].isObject()) {
        const QJsonObject g = obj["general"].toObject();
        out.heapCurrent  = toU64(g.value("heap_current"));
        out.heapPeak     = toU64(g.value("heap_peak"));
        out.activeAllocs = toU64(g.value("active_allocs"));
        out.totalAllocs  = toU64(g.value("total_allocs"));
        out.leakBytes    = toU64(g.value("leak_bytes"));
        out.allocRate    = g.value("alloc_rate").toDouble();
        out.freeRate     = g.value("free_rate").toDouble();
        out.uptimeMs     = toU64(g.value("uptime_ms"));

        out.leakRate        = g.value("leak_rate").toDouble();
        out.largestLeakSz   = toU64(g.value("largest_size"));
        out.largestLeakFile = g.value("largest_file").toString();
        out.topLeakFile     = g.value("top_file").toString();
        out.topLeakCount    = toInt(g.value("top_file_count"));
        out.topLeakBytes    = toI64(g.value("top_file_bytes"));
    }

    // ----- per_file -----
    out.perFile.clear();
    if (obj.contains("per_file") && obj["per_file"].isArray()) {
        const QJsonArray arr = obj["per_file"].toArray();
        out.perFile.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            if (!v.isObject()) continue;
            const QJsonObject o = v.toObject();
            FileStat fs;
            fs.file       = o.value("file").toString();
            // Aceptar numérico o string
            fs.totalBytes = toI64(o.value("totalBytes"));
            fs.allocs     = toInt(o.value("allocs"));
            fs.frees      = toInt(o.value("frees"));
            fs.netBytes   = toI64(o.value("netBytes"));
            out.perFile.push_back(fs);
        }
    }

    // ----- bins -----
    out.bins.clear();
    if (obj.contains("bins") && obj["bins"].isArray()) {
        const QJsonArray arr = obj["bins"].toArray();
        out.bins.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            if (!v.isObject()) continue;
            const QJsonObject o = v.toObject();
            BinRange b;
            b.lo          = toU64(o.value("lo"));
            b.hi          = toU64(o.value("hi"));
            b.bytes       = toI64(o.value("bytes"));
            b.allocations = toInt(o.value("allocations"));
            out.bins.push_back(b);
        }
    }

    // ----- leaks (bloques vivos) -----
    out.leaks.clear();
    if (obj.contains("leaks") && obj["leaks"].isArray()) {
        const QJsonArray arr = obj["leaks"].toArray();
        out.leaks.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            if (!v.isObject()) continue;
            const QJsonObject o = v.toObject();
            LeakItem li;

            // Acepta "ptr" o "addr" (string hex/dec o número)
            if (o.contains("ptr"))  li.ptr = toU64(o.value("ptr"));
            else if (o.contains("addr")) li.ptr = toU64(o.value("addr"));
            else li.ptr = 0;

            li.size  = toI64(o.value("size"));
            li.file  = o.value("file").toString();
            li.line  = toInt(o.value("line"));
            li.type  = o.value("type").toString();

            // Acepta "ts_ns" o "t_ns" y string/num
            if (o.contains("ts_ns"))      li.ts_ns = toU64(o.value("ts_ns"));
            else if (o.contains("t_ns"))  li.ts_ns = toU64(o.value("t_ns"));
            else                          li.ts_ns = 0;

            li.isLeak = o.value("is_leak").toBool(false);
            out.leaks.push_back(li);
        }
    }

    return out;
}
