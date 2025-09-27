// src/main.cpp
#include <QApplication>
#include "MainWindow.h"
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow w; w.resize(1100, 720); w.show();
    return app.exec();
}
