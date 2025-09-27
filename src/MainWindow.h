#pragma once
#include <QMainWindow>
#include <QString>
#include "model/MetricsSnapshot.h"

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
    void onSnapshot(const MetricsSnapshot& s);
    void onStatus(const QString& st);

private:
    QTabWidget* tabs_ = nullptr;
    GeneralTab* general_ = nullptr;
    MapTab*     map_ = nullptr;
    PerFileTab* perFile_ = nullptr;
    LeaksTab*   leaks_ = nullptr;

    QThread*     thread_ = nullptr;   // <-- faltaba
    ServerWorker* worker_ = nullptr;  // <-- por claridad
};
