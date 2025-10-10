#include "PerFileTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QAbstractItemView>
#include <QSortFilterProxyModel>

PerFileTab::PerFileTab(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // Fila superior: solo contador de archivos
    auto* top = new QHBoxLayout();
    totalRows_ = new QLabel("0 archivos", this);
    top->addStretch(1);
    top->addWidget(totalRows_);
    root->addLayout(top);

    // Modelo base y proxy solo para ordenamiento (sin filtrado)
    model_ = new PerFileModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setDynamicSortFilter(true);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

    // Tabla principal
    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);

    // Configuración de headers
    auto* hh = table_->horizontalHeader();
    hh->setSectionsClickable(true);
    hh->setSortIndicatorShown(true);
    hh->setSectionResizeMode(0, QHeaderView::Stretch);          // Archivo
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents); // Total [MB]
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Allocs
    table_->verticalHeader()->setVisible(false);

    root->addWidget(table_);
}

void PerFileTab::updateSnapshot(const MetricsSnapshot& s) {
    model_->setDataSet(s.perFile);
    totalRows_->setText(QString("%1 archivos").arg(s.perFile.size()));

    // Orden inicial: por uso total (columna 1)
    table_->sortByColumn(1, Qt::DescendingOrder);
}
