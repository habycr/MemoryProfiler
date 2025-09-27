#pragma once
#include <QAbstractTableModel>
#include "MetricsSnapshot.h"


class PerFileModel : public QAbstractTableModel {
    Q_OBJECT
    public:
    enum Col { File=0, TotalBytes, Allocs, Frees, NetBytes, NCols };
    explicit PerFileModel(QObject* parent=nullptr);
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setDataSet(const QVector<FileStat>& v);
private:
    QVector<FileStat> rows_;
};


class LeaksModel : public QAbstractTableModel {
    Q_OBJECT
    public:
    enum Col { Ptr=0, Size, File, Line, Type, TsNs, NCols };
    explicit LeaksModel(QObject* parent=nullptr);
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setDataSet(const QVector<LeakItem>& v);
    LeakItem itemAt(int row) const;
private:
    QVector<LeakItem> rows_;
};