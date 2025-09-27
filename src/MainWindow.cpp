// src/MainWindow.cpp
#include "MainWindow.h"
#include <QTabWidget>
#include <QStatusBar>
#include <QThread>
#include <QHostAddress>

#include "tabs/GeneralTab.h"
#include "tabs/MapTab.h"
#include "tabs/PerFileTab.h"
#include "tabs/LeaksTab.h"
#include "net/ServerWorker.h"
#include "model/MetricsSnapshot.h"   // o "MetricsSnapshot.h" según tu árbol

#include <QMetaType>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Pestañas
    tabs_ = new QTabWidget(this);
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

    // --- Registro de tipo para señales entre hilos ---
    qRegisterMetaType<MetricsSnapshot>("MetricsSnapshot");

    // --- Hilo + worker (sin padre) ---
    thread_ = new QThread(this);
    worker_ = new ServerWorker();     // <-- SIN padre (clave para poder moveToThread)
    worker_->moveToThread(thread_);

    // Arrancar escucha cuando el hilo esté listo (la lambda corre en el hilo del worker)
    connect(thread_, &QThread::started, worker_, [this]{
        // LocalHost si solo vas a conectar desde la misma máquina.
        // Usa QHostAddress::Any si quieres permitir conexiones remotas.
        worker_->listen(QHostAddress::LocalHost, 7070);
    });

    // Estado a la barra de estado (ajusta el nombre de la señal a tu header)
    // Si tu ServerWorker tiene 'statusChanged', deja esa línea; si usa 'status', cambia aquí:
    // connect(worker_, &ServerWorker::status, this, &MainWindow::onStatus);
    connect(worker_, &ServerWorker::status, this, &MainWindow::onStatus);

    // Snapshot -> actualiza todas las pestañas
    connect(worker_, &ServerWorker::snapshotReady, this, &MainWindow::onSnapshot);

    // Limpieza segura del worker cuando el hilo termine
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

    // No necesitamos llamar a worker_->stop() en destroyed; vamos a parar el hilo en el dtor
    // connect(this, &QObject::destroyed, worker_, &ServerWorker::stop);

    thread_->start();
}

MainWindow::~MainWindow() {
    if (thread_) {
        thread_->quit();
        thread_->wait();
    }
}

void MainWindow::onSnapshot(const MetricsSnapshot& s) {
    // Si prefieres, puedes mandar solo a General y luego ir habilitando el resto
    general_->updateSnapshot(s);
    map_->updateSnapshot(s);
    perFile_->updateSnapshot(s);
    leaks_->updateSnapshot(s);
}

void MainWindow::onStatus(const QString& st) {
    statusBar()->showMessage(st, 3000);
}

