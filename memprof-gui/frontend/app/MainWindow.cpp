#include "MainWindow.h"
#include <QTabWidget>
#include <QStatusBar>
#include <QThread>
#include <QHostAddress>

#include "frontend/tabs/GeneralTab.h"
#include "frontend/tabs/MapTab.h"
#include "frontend/tabs/PerFileTab.h"
#include "frontend/tabs/LeaksTab.h"
#include "frontend/net/ServerWorker.h"
#include "memprof/proto/MetricsSnapshot.h"

#include <QMetaType>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Registrar el metatipo de QSharedPointer<MetricsSnapshot>
    qRegisterMetaType<QSharedPointer<const MetricsSnapshot>>("QSharedPointer<const MetricsSnapshot>");

    // Pestañas
    tabs_    = new QTabWidget(this);
    general_ = new GeneralTab(this);
    map_     = new MapTab(this);
    perFile_ = new PerFileTab(this);
    leaks_   = new LeaksTab(this);

    tabs_->addTab(general_, "General");
    tabs_->addTab(map_,     "Mapa");
    tabs_->addTab(perFile_, "Por archivo");
    tabs_->addTab(leaks_,   "Leaks");
    setCentralWidget(tabs_);
    statusBar()->showMessage("Listo");

    // Hilo + worker (worker sin padre para moveToThread)
    thread_ = new QThread(this);
    worker_ = new ServerWorker();
    worker_->moveToThread(thread_);

    // Arrancar escucha en el hilo del worker (encolado)
    connect(thread_, &QThread::started, worker_,
            [w = worker_]{ w->listen(QHostAddress::LocalHost, 7070); },
            Qt::QueuedConnection);

    // Estado a la barra de estado (encolado)
    connect(worker_, &ServerWorker::status,
            this,     &MainWindow::onStatus,
            Qt::QueuedConnection);

    // Guardar snapshot (no pintar aquí)
    connect(worker_, &ServerWorker::snapshotReady,
            this,     &MainWindow::onSnapshot,
            Qt::QueuedConnection);

    // Limpieza segura
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

    // Timer de UI: pinta el último snapshot guardado a ~12.5 FPS
    uiTimer_ = new QTimer(this);
    uiTimer_->setTimerType(Qt::CoarseTimer);
    uiTimer_->setInterval(80);
    connect(uiTimer_, &QTimer::timeout, this, &MainWindow::uiTick);
    uiTimer_->start();

    thread_->start();
}

MainWindow::~MainWindow() {
    if (thread_) {
        QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
        thread_->quit();
        thread_->wait();
    }
}

void MainWindow::onSnapshot(QSharedPointer<const MetricsSnapshot> s) {
    // Solo guardar el último; si llegan muchos, se descartan los intermedios
    pending_.swap(s);
}

void MainWindow::uiTick() {
    if (!pending_) return;
    auto s = pending_;     // copia barata del shared_ptr
    pending_.reset();

    // Pinta SOLO la pestaña visible (reduce trabajo)
    const int idx = tabs_->currentIndex();
    if      (idx == 0) general_->updateSnapshot(*s);
    else if (idx == 1) map_->updateSnapshot(*s);
    else if (idx == 2) perFile_->updateSnapshot(*s);
    else if (idx == 3) leaks_->updateSnapshot(*s);
}

void MainWindow::onStatus(const QString& st) {
    statusBar()->showMessage(st, 3000);
}
