// src/tabs/MapTab.cpp
#include "MapTab.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QToolTip>
#include <QMouseEvent>
#include <QTableView>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <algorithm>

// ---- Modelo interno para bloques (Ptr, Size, File, Line, Type, Edad(ms), Estado) ----
#include <QAbstractTableModel>

class BlocksModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit BlocksModel(QObject* p=nullptr) : QAbstractTableModel(p) {}

    void setDataSet(const QVector<LeakItem>& v) {
        beginResetModel();
        rows_ = v;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid()? 0 : rows_.size();
    }

    int columnCount(const QModelIndex& parent = {}) const override {
        Q_UNUSED(parent);
        return 7;
    }

    QVariant headerData(int s, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole) return {};
        if (o == Qt::Horizontal) {
            switch (s) {
                case 0: return "Ptr";
                case 1: return "Size (B)";
                case 2: return "File";
                case 3: return "Line";
                case 4: return "Type";       // <--- NUEVO
                case 5: return "Edad (ms)";
                case 6: return "Estado";
            }
        }
        return {};
    }

    QVariant data(const QModelIndex& i, int role) const override {
        if (!i.isValid() || i.row() >= rows_.size()) return {};
        const auto& L = rows_[i.row()];
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        const qint64 age_ms = now_ms - static_cast<qint64>(L.ts_ns / 1000000ULL);
        const bool isLeak = (age_ms >= 3000);

        // Valor crudo para ordenar correctamente por puntero (numérico)
        if (role == Qt::UserRole && i.column() == 0) {
            return static_cast<qulonglong>(L.ptr);
        }

        if (role == Qt::TextAlignmentRole) {
            // Alinea numéricos a la derecha para lectura
            if (i.column() == 1 || i.column() == 3 || i.column() == 5) {
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            }
        }

        if (role == Qt::ForegroundRole) {
            return isLeak ? QColor(200,40,40) : QColor(40,140,60);
        }

        if (role == Qt::DisplayRole) {
            switch (i.column()) {
                case 0: return QString("0x%1").arg(QString::number(static_cast<qulonglong>(L.ptr), 16));
                case 1: return L.size;
                case 2: return L.file;
                case 3: return L.line;
                case 4: return L.type;          // <--- NUEVO
                case 5: return age_ms;
                case 6: return isLeak ? "LEAK" : "Activo";
            }
        }
        return {};
    }

private:
    QVector<LeakItem> rows_;
};

class MapBinsCanvas : public QWidget {
    Q_OBJECT
public:
    explicit MapBinsCanvas(QWidget* p=nullptr) : QWidget(p) {
        setMouseTracking(true);
    }
    void setBins(const QVector<BinRange>& v) { bins_ = v; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), palette().base());
        if (bins_.isEmpty()) { p.drawText(rect(), Qt::AlignCenter, "Sin datos de bins"); return; }

        const int L=40, R=20, T=20, B=30;
        QRect area(L, T, width()-L-R, height()-T-B);
        p.drawRect(area);

        qint64 maxBytes = 0; for (const auto& b : bins_) maxBytes = std::max(maxBytes, b.bytes);
        if (maxBytes==0) maxBytes = 1;

        const int n = bins_.size();
        const double w = static_cast<double>(area.width())/n;

        for (int i=0;i<n;++i) {
            const auto& b = bins_[i];
            double h = static_cast<double>(b.bytes) / static_cast<double>(maxBytes) * area.height();
            QRectF bar(area.left()+i*w+1, area.bottom()-h, w-2, h);
            p.fillRect(bar, QColor(80,140,220));
        }

        // ejes simples
        p.drawText(5, T-4, "bytes");
        p.drawText(area.right()-40, area.bottom()+20, "addr →");
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (bins_.isEmpty()) return;
        const int L=40, R=20, T=20, B=30; QRect area(L, T, width()-L-R, height()-T-B);
        if (!area.contains(e->pos())) { QToolTip::hideText(); return; }
        const int n = bins_.size();
        double w = static_cast<double>(area.width())/n;
        int idx = std::clamp(static_cast<int>((e->pos().x()-area.left())/w), 0, n-1);
        const auto& b = bins_[idx];
        QString txt = QString("[%1 - %2)\nbytes=%3\nallocs=%4")
        .arg(QString("0x%1").arg(QString::number(b.lo,16)))
        .arg(QString("0x%1").arg(QString::number(b.hi,16)))
        .arg(static_cast<qlonglong>(b.bytes))
        .arg(b.allocations);
        QToolTip::showText(e->globalPosition().toPoint(), txt, this);
    }

private:
    QVector<BinRange> bins_;
};

MapTab::MapTab(QWidget* parent): QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // Canvas superior de bins
    binsCanvas_ = new MapBinsCanvas(this);
    binsCanvas_->setMinimumHeight(180);
    root->addWidget(binsCanvas_);

    // Tabla de bloques individuales
    table_ = new QTableView(this);

    // Modelo crudo + proxy para ordenar por puntero numérico (UserRole)
    auto* rawModel = new BlocksModel(this);
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(rawModel);
    proxy->setSortRole(Qt::UserRole);

    table_->setModel(proxy);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    root->addWidget(table_);
}

void MapTab::updateSnapshot(const MetricsSnapshot& s) {
    bins_ = s.bins;
    blocks_ = s.leaks; // bloques vivos
    repaintCanvas();

    // Soportar tanto modelo crudo como proxy (por si cambia en el futuro)
    BlocksModel* m = nullptr;
    if (auto* proxy = qobject_cast<QSortFilterProxyModel*>(table_->model())) {
        m = qobject_cast<BlocksModel*>(proxy->sourceModel());
    } else {
        m = qobject_cast<BlocksModel*>(table_->model());
    }
    if (m) m->setDataSet(blocks_);
}

void MapTab::repaintCanvas() {
    if (auto* c = qobject_cast<MapBinsCanvas*>(binsCanvas_)) {
        c->setBins(bins_);
    }
}

#include "MapTab.moc"
