#include "TableModels.h"
#include <QString>

// ==================== LeaksModel ====================
LeaksModel::LeaksModel(QObject* parent) : QAbstractTableModel(parent) {}

int LeaksModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int LeaksModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return 6; // ptr, size, file, line, type, ts_ns
}

QVariant LeaksModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case 0: return "Ptr";
            case 1: return "Size (B)";
            case 2: return "File";
            case 3: return "Line";
            case 4: return "Type";
            case 5: return "ts_ns";
        }
    }
    return {};
}

QVariant LeaksModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) return {};
    const auto& it = rows_[index.row()];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return QString("0x%1").arg(QString::number(it.ptr, 16));
            case 1: return it.size;
            case 2: return it.file;
            case 3: return it.line;
            case 4: return it.type;
            case 5: return QString::number(it.ts_ns);
        }
    }
    return {};
}

void LeaksModel::setDataSet(const QVector<LeakItem>& v) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(v.size());
    for (const auto& it : v) {
        if (it.isLeak) rows_.push_back(it); // <-- SOLO fugas reales
    }
    endResetModel();
}

LeakItem LeaksModel::itemAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_[row] : LeakItem{};
}

// ==================== PerFileModel ====================
PerFileModel::PerFileModel(QObject* parent) : QAbstractTableModel(parent) {}

int PerFileModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int PerFileModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    // ↓↓↓ Solo 3 columnas: Archivo | Total [MB] | Allocs
    return 3;
}

QVariant PerFileModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case 0: return "Archivo";
            case 1: return "Total [MB]";
            case 2: return "Allocs";
        }
    }
    return {};
}

QVariant PerFileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) return {};
    const auto& it = rows_[index.row()];

    // Para ordenamiento correcto: devolver valores numéricos en UserRole
    if (role == Qt::UserRole) {
        switch (index.column()) {
            case 1: { // Total [MB] numérico
                const double mb = static_cast<double>(it.totalBytes) / (1024.0 * 1024.0);
                return mb;
            }
            case 2: { // Allocs
                return it.allocs;
            }
            default: break;
        }
    }

    // Alineación de numéricos
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == 1 || index.column() == 2)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return it.file;
            case 1: {
                const double mb = static_cast<double>(it.totalBytes) / (1024.0 * 1024.0);
                return QString::number(mb, 'f', 2);
            }
            case 2: return it.allocs;
        }
    }
    return {};
}

void PerFileModel::setDataSet(const QVector<FileStat>& v) {
    beginResetModel();
    rows_ = v;
    endResetModel();
}

const QVector<FileStat>& PerFileModel::items() const {
    return rows_;
}