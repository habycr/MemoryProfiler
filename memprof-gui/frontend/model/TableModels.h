#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include "memprof/proto/MetricsSnapshot.h"

// -------------------- LeaksModel --------------------
class LeaksModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LeaksModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setDataSet(const QVector<LeakItem>& v);
    LeakItem itemAt(int row) const;

private:
    QVector<LeakItem> rows_;
};

// -------------------- PerFileModel --------------------
class PerFileModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit PerFileModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override; // Archivo | Total [MB] | Allocs
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setDataSet(const QVector<FileStat>& v);
    const QVector<FileStat>& items() const;

private:
    QVector<FileStat> rows_;
};
