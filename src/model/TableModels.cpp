#include "TableModels.h"
#include <QDateTime>
#include <QString>


// --- PerFileModel ---
PerFileModel::PerFileModel(QObject* p): QAbstractTableModel(p) {}
int PerFileModel::rowCount(const QModelIndex&) const { return rows_.size(); }
int PerFileModel::columnCount(const QModelIndex&) const { return NCols; }
QVariant PerFileModel::headerData(int s, Qt::Orientation o, int r) const {
if (r!=Qt::DisplayRole) return {};
if (o==Qt::Horizontal) {
switch (s){
case File: return "Archivo"; case TotalBytes: return "Total [B]"; case Allocs: return "Allocs"; case Frees: return "Frees"; case NetBytes: return "Net [B]";
}
}
return {};
}
QVariant PerFileModel::data(const QModelIndex& i, int role) const {
if (!i.isValid() || i.row()<0 || i.row()>=rows_.size()) return {};
const auto& r = rows_[i.row()];
if (role==Qt::DisplayRole) {
switch (i.column()){
case File: return r.file; case TotalBytes: return (qlonglong)r.totalBytes; case Allocs: return r.allocs; case Frees: return r.frees; case NetBytes: return (qlonglong)r.netBytes;
}
}
return {};
}
void PerFileModel::setDataSet(const QVector<FileStat>& v){ beginResetModel(); rows_ = v; endResetModel(); }


// --- LeaksModel ---
LeaksModel::LeaksModel(QObject* p): QAbstractTableModel(p) {}
int LeaksModel::rowCount(const QModelIndex&) const { return rows_.size(); }
int LeaksModel::columnCount(const QModelIndex&) const { return NCols; }
QVariant LeaksModel::headerData(int s, Qt::Orientation o, int r) const {
if (r!=Qt::DisplayRole) return {};
if (o==Qt::Horizontal) {
switch (s){
case Ptr: return "ptr"; case Size: return "size"; case File: return "file"; case Line: return "line"; case Type: return "type"; case TsNs: return "ts_ns";
}
}
return {};
}
QVariant LeaksModel::data(const QModelIndex& i, int role) const {
if (!i.isValid() || i.row()<0 || i.row()>=rows_.size()) return {};
const auto& r = rows_[i.row()];
if (role==Qt::DisplayRole) {
switch (i.column()){
case Ptr: return QString("0x%1").arg(QString::number(r.ptr,16));
case Size: return (qlonglong)r.size;
case File: return r.file;
case Line: return r.line;
case Type: return r.type;
case TsNs: return (qlonglong)r.ts_ns;
}
}
return {};
}
void LeaksModel::setDataSet(const QVector<LeakItem>& v){ beginResetModel(); rows_ = v; endResetModel(); }
LeakItem LeaksModel::itemAt(int row) const { return (row>=0 && row<rows_.size()) ? rows_[row] : LeakItem{}; }