// src/net/MetricsWorker.cpp
#include "MetricsWorker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

MetricsWorker::MetricsWorker(QObject* p): QObject(p) {
  reconnectTimer_ = new QTimer(this);
  reconnectTimer_->setSingleShot(true);
  connect(reconnectTimer_, &QTimer::timeout, this, &MetricsWorker::doReconnect);
}

void MetricsWorker::start(const QString& host, quint16 port) {
  if (sock_) return;
  host_ = host; port_ = port;
  sock_ = new QTcpSocket(this);
  connect(sock_, &QTcpSocket::connected,    this, &MetricsWorker::onConnected);
  connect(sock_, &QTcpSocket::readyRead,    this, &MetricsWorker::onReadyRead);
  connect(sock_, &QTcpSocket::disconnected, this, &MetricsWorker::onDisconnected);
  connect(sock_, &QTcpSocket::errorOccurred,this, &MetricsWorker::onError);
  emit statusChanged(QStringLiteral("Conectando a %1:%2...").arg(host_).arg(port_));
  tryConnect();
}

void MetricsWorker::tryConnect() {
  if (!sock_) return;
  if (sock_->state() != QAbstractSocket::UnconnectedState) sock_->abort();
  sock_->connectToHost(host_, port_);
}

void MetricsWorker::doReconnect() { emit statusChanged("Reintentando..."); tryConnect(); }

void MetricsWorker::stop() {
  if (sock_) sock_->disconnectFromHost();
}

void MetricsWorker::onConnected() {
  emit statusChanged("Conectado");
  reconnectTimer_->stop();
}

void MetricsWorker::onDisconnected() {
  emit statusChanged("Desconectado; reintento en 1s");
  reconnectTimer_->start(1000);
}

void MetricsWorker::onError(QAbstractSocket::SocketError) {
  emit statusChanged(QStringLiteral("Error: %1; reintento en 1s").arg(sock_->errorString()));
  reconnectTimer_->start(1000);
}

void MetricsWorker::onReadyRead() {
  buf_ += sock_->readAll();
  int idx;
  while ((idx = buf_.indexOf('\n')) >= 0) {
    QByteArray line = buf_.left(idx); buf_.remove(0, idx+1);
    if (line.trimmed().isEmpty()) continue;

    auto doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) continue;
    QJsonObject o = doc.object();
    if (o.value("kind").toString() != "SNAPSHOT") continue;

    MetricsSnapshot s;
    auto g = o.value("general").toObject();
    s.heapCurrent = (qint64)g.value("heap_current").toDouble();
    s.heapPeak    = (qint64)g.value("heap_peak").toDouble();
    s.allocRate   = g.value("alloc_rate").toDouble();
    s.freeRate    = g.value("free_rate").toDouble();
    s.uptimeMs    = (qint64)g.value("uptime_ms").toDouble();

    for (auto v : o.value("bins").toArray()) {
      auto b = v.toObject(); BinRange br;
      br.lo = (quint64)b.value("addr_lo").toDouble();
      br.hi = (quint64)b.value("addr_hi").toDouble();
      br.bytes = (qint64)b.value("bytes").toDouble();
      br.allocations = b.value("allocations").toInt();
      s.bins.push_back(br);
    }
    for (auto v : o.value("per_file").toArray()) {
      auto f = v.toObject(); FileStat fs;
      fs.file = f.value("file").toString();
      fs.totalBytes = (qint64)f.value("total_bytes").toDouble();
      fs.allocs = f.value("allocs").toInt();
      fs.frees  = f.value("frees").toInt();
      fs.netBytes = (qint64)f.value("net_bytes").toDouble();
      s.perFile.push_back(fs);
    }
    for (auto v : o.value("leaks").toArray()) {
      auto L = v.toObject(); LeakItem li;
      li.ptr = (quint64)L.value("ptr").toDouble();
      li.size = (qint64)L.value("size").toDouble();
      li.file = L.value("file").toString();
      li.line = L.value("line").toInt();
      li.type = L.value("type").toString();
      li.ts_ns = (qint64)L.value("ts_ns").toDouble();
      s.leaks.push_back(li);
    }
    s.capturedAt = QDateTime::currentDateTime();
    emit snapshotReady(s);
  }
}


void MetricsWorker::applyEventLine(const QByteArray& line) {
  auto doc = QJsonDocument::fromJson(line);
  if (!doc.isObject()) return;
  QJsonObject o = doc.object();
  const QString kind = o.value("kind").toString();

  if (kind == "SNAPSHOT") {
    MetricsSnapshot s;
    auto g = o.value("general").toObject();
    s.heapCurrent = (qint64)g.value("heap_current").toDouble();
    s.heapPeak    = (qint64)g.value("heap_peak").toDouble();
    s.allocRate   = g.value("alloc_rate").toDouble();
    s.freeRate    = g.value("free_rate").toDouble();
    s.uptimeMs    = (qint64)g.value("uptime_ms").toDouble();

    for (auto v : o.value("bins").toArray()) {
      auto b = v.toObject(); BinRange br;
      br.lo = (quint64)b.value("addr_lo").toDouble();
      br.hi = (quint64)b.value("addr_hi").toDouble();
      br.bytes = (qint64)b.value("bytes").toDouble();
      br.allocations = b.value("allocations").toInt();
      s.bins.push_back(br);
    }
    for (auto v : o.value("per_file").toArray()) {
      auto f = v.toObject(); FileStat fs;
      fs.file = f.value("file").toString();
      fs.totalBytes = (qint64)f.value("total_bytes").toDouble();
      fs.allocs = f.value("allocs").toInt();
      fs.frees  = f.value("frees").toInt();
      fs.netBytes = (qint64)f.value("net_bytes").toDouble();
      s.perFile.push_back(fs);
    }
    for (auto v : o.value("leaks").toArray()) {
      auto L = v.toObject(); LeakItem li;
      li.ptr = (quint64)L.value("ptr").toDouble();
      li.size = (qint64)L.value("size").toDouble();
      li.file = L.value("file").toString();
      li.line = L.value("line").toInt();
      li.type = L.value("type").toString();
      li.ts_ns = (qint64)L.value("ts_ns").toDouble();
      s.leaks.push_back(li);
    }
    s.capturedAt = QDateTime::currentDateTime();
    emit snapshotReady(s);
  }
  // Si tu broker manda ALLOC/FREE incrementales, aquí acumulas y emites snapshot periódico
}
