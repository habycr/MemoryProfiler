// src/MainWindow.cpp
#include "MainWindow.h"
#include <QTabWidget>
#include <QStatusBar>
#include <QThread>
#include <QHostAddress>

#include "../../frontend/tabs/GeneralTab.h"
#include "../../frontend/tabs/MapTab.h"
#include "../../frontend/tabs/PerFileTab.h"
#include "../../frontend/tabs/LeaksTab.h"
#include "../../frontend/net/ServerWorker.h"
#include "../../../include/memprof/proto/MetricsSnapshot.h"

#include <QMetaType>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
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

    // Registro de tipo para señales entre hilos
    qRegisterMetaType<MetricsSnapshot>("MetricsSnapshot");

    // Hilo + worker (worker sin padre, para moveToThread)
    thread_ = new QThread(this);
    worker_ = new ServerWorker();
    worker_->moveToThread(thread_);

    // Arrancar escucha en el hilo del worker
    connect(thread_, &QThread::started, worker_, [this]{
        worker_->listen(QHostAddress::LocalHost, 7070);
    });

    // Estado a la barra de estado
    connect(worker_, &ServerWorker::status, this, &MainWindow::onStatus);

    // Snapshot -> actualizar pestañas
    connect(worker_, &ServerWorker::snapshotReady, this, &MainWindow::onSnapshot);

    // Limpieza segura
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

    thread_->start();
}

MainWindow::~MainWindow() {
    if (thread_) {
        thread_->quit();
        thread_->wait();
    }
}

void MainWindow::onSnapshot(const MetricsSnapshot& s) {
    general_->updateSnapshot(s);
    map_->updateSnapshot(s);
    perFile_->updateSnapshot(s);
    leaks_->updateSnapshot(s);
}

void MainWindow::onStatus(const QString& st) {
    statusBar()->showMessage(st, 3000);
}
