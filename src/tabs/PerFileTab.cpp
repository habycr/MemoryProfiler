#include "PerFileTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QAbstractItemView>

PerFileTab::PerFileTab(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // Fila superior: filtro + contador
    auto* top = new QHBoxLayout();
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText("Filtrar por nombre de archivo…");
    totalRows_ = new QLabel("0 items", this);
    top->addWidget(filterEdit_, /*stretch*/ 1);
    top->addWidget(totalRows_);
    root->addLayout(top);

    // Modelo base y proxy para filtrado/ordenamiento
    model_ = new PerFileModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setDynamicSortFilter(true);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(0); // columna Archivo

    // Tabla
    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);

    // Ajustes de headers
    auto* hh = table_->horizontalHeader();
    hh->setStretchLastSection(true);
    hh->setSectionsClickable(true);
    hh->setSortIndicatorShown(true);
    // Opcional: ajusta automáticamente ancho de columnas 0-1 (archivo y conteo)
    // hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    // hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    table_->verticalHeader()->setVisible(false);
    root->addWidget(table_);

    // Filtro: texto -> proxy
    connect(filterEdit_, &QLineEdit::textChanged,
            proxy_,       &QSortFilterProxyModel::setFilterFixedString);
}

void PerFileTab::updateSnapshot(const MetricsSnapshot& s) {
    // Se asume que PerFileModel expone setDataSet(...) como en tu proyecto
    model_->setDataSet(s.perFile);

    // Actualizar contador visible (tras filtrado mostramos total bruto para consistencia con otras pestañas)
    totalRows_->setText(QString("%1 items").arg(s.perFile.size()));

    // Mantener orden por defecto (por ejemplo, Bytes desc) si el modelo expone esa columna
    // Nota: esto solo aplica si el modelo devuelve tipos numéricos reales en esas columnas.
    // table_->sortByColumn(2, Qt::DescendingOrder); // Descomenta si la col 2 es "Bytes"
}
