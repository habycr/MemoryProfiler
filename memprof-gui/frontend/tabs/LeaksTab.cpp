#include "LeaksTab.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QClipboard>
#include <QApplication>
#include <QLabel>
#include <QValueAxis>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>
#include <QtMath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsView>
#include <QHash>
#include <QPair>

#include "frontend/model/TableModels.h"
#include "memprof/proto/MetricsSnapshot.h"

// =============================================
//  Clase auxiliar: ChartView con Zoom y Pan
// =============================================
class ZoomableChartView : public QChartView {
public:
    explicit ZoomableChartView(QChart* chart, QWidget* parent = nullptr)
        : QChartView(chart, parent) {
        setRubberBand(QChartView::RectangleRubberBand);
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setMouseTracking(true);
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        constexpr double zoomFactor = 1.15;
        if (event->angleDelta().y() > 0)
            chart()->zoom(zoomFactor);
        else
            chart()->zoom(1.0 / zoomFactor);
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        chart()->zoomReset();
        event->accept();
    }
};

// =============================================
//  Funciones auxiliares
// =============================================
namespace {
static inline double bytesToMB(qint64 bytes) {
    return bytes / (1024.0 * 1024.0);
}
static inline QString formatMB(qint64 bytes) {
    return QString::number(bytesToMB(bytes), 'f', 2);
}
static inline QString niceBaseName(const QString& raw) {
    // decodifica %20 etc, normaliza separadores, y devuelve nombre o ruta compacta
    QString dec = QUrl::fromPercentEncoding(raw.toUtf8());
    QString norm = dec; norm.replace("\\", "/");
    QString base = QFileInfo(norm).fileName();
    if (!base.isEmpty() && base.contains('.')) return base;
    if (norm.size() > 40) return norm.left(20) + "…" + norm.right(15);
    return norm;
}
} // namespace

// =============================================
//  Clase principal LeaksTab
// =============================================
LeaksTab::LeaksTab(QWidget* parent): QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // --- KPIs ---
    auto* kpiRow = new QHBoxLayout();
    leakTotalLbl_ = new QLabel("Total fugado: 0.00 MB");
    largestLbl_   = new QLabel("Leak mayor: —");
    topFileLbl_   = new QLabel("Archivo top leaks: —");
    leakRateLbl_  = new QLabel("Tasa de leaks: 0.00%");
    kpiRow->addWidget(leakTotalLbl_);
    kpiRow->addWidget(largestLbl_);
    kpiRow->addWidget(topFileLbl_);
    kpiRow->addWidget(leakRateLbl_);
    kpiRow->addStretch(1);
    root->addLayout(kpiRow);

    // --- Filtro + copiar ---
    auto* row = new QHBoxLayout(); root->addLayout(row);
    filterEdit_ = new QLineEdit(); filterEdit_->setPlaceholderText("Filtrar por archivo/tipo…");
    copyBtn_ = new QPushButton("Copiar selección");
    row->addWidget(filterEdit_); row->addWidget(copyBtn_);

    // --- Tabla ---
    model_ = new LeaksModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1);
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

    // --- Charts ---
    barsView_ = new QChartView(new QChart());
    pieView_  = new QChartView(new QChart());
    timeView_ = new ZoomableChartView(new QChart()); // ✅ zoom interactivo

    barsView_->setRenderHint(QPainter::Antialiasing);
    pieView_->setRenderHint(QPainter::Antialiasing);

    auto* chartsRow = new QHBoxLayout();
    chartsRow->addWidget(barsView_, 2);
    chartsRow->addWidget(pieView_, 2);
    chartsRow->addWidget(timeView_, 3);
    root->addLayout(chartsRow);
}

void LeaksTab::updateSnapshot(const MetricsSnapshot& s) {
    model_->setDataSet(s.leaks);

    // KPIs de la cabecera (valores vienen del runtime)
    leakTotalLbl_->setText(QString("Total fugado: %1 MB").arg(formatMB(s.leakBytes)));
    if (s.largestLeakSz > 0)
        largestLbl_->setText(QString("Leak mayor: %1 (%2 MB)")
                             .arg(niceBaseName(s.largestLeakFile))
                             .arg(formatMB(s.largestLeakSz)));
    else
        largestLbl_->setText("Leak mayor: —");

    if (!s.topLeakFile.isEmpty())
        topFileLbl_->setText(QString("Archivo con mayor frecuencia de leaks: %1 (%2 leaks, %3 MB)")
                             .arg(niceBaseName(s.topLeakFile))
                             .arg(s.topLeakCount)
                             .arg(formatMB(s.topLeakBytes)));
    else
        topFileLbl_->setText("Archivo top leaks: —");

    leakRateLbl_->setText(QString("Tasa de leaks: %1%").arg(s.leakRate * 100.0, 0, 'f', 2));

    rebuildCharts(s);
}

void LeaksTab::rebuildCharts(const MetricsSnapshot& s) {
    // ======= 1) Barras (MB por archivo) =======
    QHash<QString, double> mbByFile;
    for (const auto& L : s.leaks)
        if (L.isLeak) mbByFile[niceBaseName(L.file)] += bytesToMB(L.size);

    QList<QPair<QString,double>> items;
    items.reserve(mbByFile.size());
    for (auto it = mbByFile.begin(); it != mbByFile.end(); ++it)
        items.append({it.key(), it.value()});
    std::sort(items.begin(), items.end(), [](auto& a, auto& b){ return a.second > b.second; });

    const int TOPN = 8;
    QStringList cats;
    auto* set = new QBarSet("MB");
    double others = 0.0;
    for (int i=0; i<items.size(); ++i) {
        if (i<TOPN) { cats<<items[i].first; *set<<items[i].second; }
        else others += items[i].second;
    }
    if (others>0){ cats<<"Otros"; *set<<others; }

    auto* barChart = new QChart();
    barChart->setTitle("Leaks por archivo (MB)");
    auto* series = new QBarSeries(); series->append(set);
    barChart->addSeries(series);
    auto* axisX = new QBarCategoryAxis(); axisX->append(cats);
    auto* axisY = new QValueAxis(); axisY->setTitleText("MB");
    barChart->addAxis(axisX, Qt::AlignBottom);
    barChart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX); series->attachAxis(axisY);
    barChart->legend()->hide();
    barsView_->setChart(barChart);

    // ======= 2) Pie (% MB por archivo) =======
    auto* pieChart = new QChart(); auto* pie = new QPieSeries();
    int upTo = qMin(items.size(), TOPN);
    for (int i=0;i<upTo;++i)
        if (items[i].second>0) pie->append(items[i].first, items[i].second);
    if (others>0) pie->append("Otros", others);
    pie->setLabelsVisible(true);
    for (auto* sl : pie->slices())
        sl->setLabel(QString("%1 (%2%)").arg(sl->label())
                     .arg(sl->percentage()*100.0,0,'f',1));
    pieChart->addSeries(pie);
    pieChart->setTitle("Distribución de leaks (MB)");
    pieView_->setChart(pieChart);

    // ======= 3) Temporal (curva MB vs tiempo con zoom) =======
    QVector<QPointF> pts;
    qulonglong tmin = std::numeric_limits<qulonglong>::max(), tmax = 0;
    for (const auto& L : s.leaks)
        if (L.isLeak) { tmin = qMin(tmin,L.ts_ns); tmax = qMax(tmax,L.ts_ns); }
    if (tmin==std::numeric_limits<qulonglong>::max()) tmin=tmax=0;

    for (const auto& L : s.leaks)
        if (L.isLeak)
            pts.append(QPointF(double(L.ts_ns - tmin)/1e9, bytesToMB(L.size)));
    std::sort(pts.begin(), pts.end(), [](auto&a,auto&b){return a.x()<b.x();});

    auto* line = new QLineSeries();
    for (auto& p: pts) line->append(p);
    line->setName("Fugas detectadas (MB)");
    line->setPointsVisible(false);

    auto* timeChart = new QChart();
    timeChart->addSeries(line);
    timeChart->setTitle("Curva temporal de fugas (MB vs s)");

    auto* axX = new QValueAxis();
    axX->setTitleText("Tiempo (s)");
    axX->setRange(0.0, std::max(1.0, double(tmax - tmin)/1e9));

    auto* axY = new QValueAxis();
    axY->setTitleText("Tamaño de fuga (MB)");
    double maxMB = 0.0;
    for (auto& p: pts) maxMB = std::max(maxMB, p.y());
    axY->setRange(0.0, std::max(0.01, maxMB*1.2)); // 20% margen

    timeChart->addAxis(axX, Qt::AlignBottom);
    timeChart->addAxis(axY, Qt::AlignLeft);
    line->attachAxis(axX); line->attachAxis(axY);
    timeChart->legend()->hide();

    timeView_->setChart(timeChart); // mantiene zoom interactivo
}

void LeaksTab::onCopySelected() {
    auto idx = table_->currentIndex(); if (!idx.isValid()) return;
    int row = proxy_->mapToSource(idx).row();
    LeakItem item = model_->itemAt(row);
    QString text = QString("ptr=0x%1 size=%2 file=%3 line=%4 type=%5 ts_ns=%6")
        .arg(QString::number(item.ptr,16)).arg(item.size)
        .arg(item.file).arg(item.line).arg(item.type).arg(item.ts_ns);
    QApplication::clipboard()->setText(text);
}
