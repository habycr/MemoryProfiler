#include "PerFileTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>


PerFileTab::PerFileTab(QWidget* parent): QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout(); root->addLayout(row);
    filterEdit_ = new QLineEdit(); filterEdit_->setPlaceholderText("Filtrar por nombre de archivo…"); row->addWidget(filterEdit_);


    model_ = new PerFileModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(PerFileModel::File);
    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    root->addWidget(table_);


    connect(filterEdit_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
}


void PerFileTab::updateSnapshot(const MetricsSnapshot& s) {
    model_->setDataSet(s.perFile);
}