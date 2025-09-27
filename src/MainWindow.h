// src/MainWindow.h
#pragma once
#include <QMainWindow>
class QTabWidget;
class GeneralTab; class MapTab; class PerFileTab; class LeaksTab;
class MetricsWorker; struct MetricsSnapshot;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent=nullptr);
    ~MainWindow();
private slots:
  void onSnapshot(const MetricsSnapshot& s);
    void onStatus(const QString& st);
private:
    QTabWidget* tabs_ = nullptr;
    GeneralTab* general_ = nullptr;
    MapTab* map_ = nullptr;
    PerFileTab* perFile_ = nullptr;
    LeaksTab* leaks_ = nullptr;
    MetricsWorker* worker_ = nullptr;
};
