#pragma once
#include <QMainWindow>
#include <QString>
#include <QSharedPointer>
#include <QTimer>

#include "memprof/proto/MetricsSnapshot.h"

class QTabWidget;
class QThread;
class ServerWorker;
class GeneralTab;
class MapTab;
class PerFileTab;
class LeaksTab;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSnapshot(QSharedPointer<const MetricsSnapshot> s); // recibe puntero compartido
    void onStatus(const QString& st);
    void uiTick(); // pinta a ~12.5 FPS el último snapshot guardado

private:
    QTabWidget* tabs_ = nullptr;
    GeneralTab* general_ = nullptr;
    MapTab*     map_ = nullptr;
    PerFileTab* perFile_ = nullptr;
    LeaksTab*   leaks_ = nullptr;

    QThread*      thread_  = nullptr;
    ServerWorker* worker_  = nullptr;

    QTimer* uiTimer_ = nullptr;
    QSharedPointer<const MetricsSnapshot> pending_; // último snapshot recibido
};
