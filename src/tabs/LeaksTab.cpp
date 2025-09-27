#include "LeaksTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QClipboard>
#include <QApplication>
#include <QSortFilterProxyModel>   // <-- NUEVO

LeaksTab::LeaksTab(QWidget* parent): QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout(); root->addLayout(row);
    filterEdit_ = new QLineEdit(); filterEdit_->setPlaceholderText("Filtrar por archivo/tipo…"); row->addWidget(filterEdit_);
    copyBtn_ = new QPushButton("Copiar selección"); row->addWidget(copyBtn_);

    model_ = new LeaksModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1); // todo el row
    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(table_);

    connect(filterEdit_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    connect(copyBtn_, &QPushButton::clicked, this, &LeaksTab::onCopySelected);
}

void LeaksTab::updateSnapshot(const MetricsSnapshot& s) {
    model_->setDataSet(s.leaks);
}

void LeaksTab::onCopySelected() {
    auto idx = table_->currentIndex(); if (!idx.isValid()) return;
    int row = proxy_->mapToSource(idx).row();
    LeakItem item = model_->itemAt(row);
    QString text = QString("ptr=0x%1 size=%2 file=%3 line=%4 type=%5 ts_ns=%6")
        .arg(QString::number(item.ptr,16)).arg(item.size).arg(item.file).arg(item.line).arg(item.type).arg(item.ts_ns);
    QApplication::clipboard()->setText(text);
}
