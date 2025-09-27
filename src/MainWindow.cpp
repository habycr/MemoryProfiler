// src/MainWindow.cpp
#include "MainWindow.h"
#include <QTabWidget>
#include <QStatusBar>
#include <QThread>
#include "tabs/GeneralTab.h"
#include "tabs/MapTab.h"
#include "tabs/PerFileTab.h"
#include "tabs/LeaksTab.h"
#include "net/MetricsWorker.h"
#include "model/MetricsSnapshot.h"

MainWindow::MainWindow(QWidget* parent): QMainWindow(parent) {
    tabs_ = new QTabWidget(this);
    general_ = new GeneralTab(this);
    map_ = new MapTab(this);
    perFile_ = new PerFileTab(this);
    leaks_ = new LeaksTab(this);
    tabs_->addTab(general_, "General");
    tabs_->addTab(map_, "Mapa");
    tabs_->addTab(perFile_, "Por archivo");
    tabs_->addTab(leaks_, "Leaks");
    setCentralWidget(tabs_);
    statusBar()->showMessage("Listo");

    // Hilo de red
    auto* thread = new QThread(this);
    worker_ = new MetricsWorker();
    worker_->moveToThread(thread);
    connect(thread, &QThread::started, [this]{ worker_->start("127.0.0.1", 7070); });
    connect(worker_, &MetricsWorker::snapshotReady, this, &MainWindow::onSnapshot);
    connect(worker_, &MetricsWorker::statusChanged, this, &MainWindow::onStatus);
    connect(this, &QObject::destroyed, worker_, &MetricsWorker::stop);
    thread->start();
}

MainWindow::~MainWindow() {}

void MainWindow::onSnapshot(const MetricsSnapshot& s) {
    general_->updateSnapshot(s);
    map_->updateSnapshot(s);
    perFile_->updateSnapshot(s);
    leaks_->updateSnapshot(s);
}

void MainWindow::onStatus(const QString& st) { statusBar()->showMessage(st); }
