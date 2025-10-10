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

namespace {
// Helper: convierte bytes → kB con 2 decimales y devuelve solo el número (sin sufijo)
static inline QString bytesToKB(qint64 bytes) {
    const double kb = bytes / 1024.0;
    return QString::number(kb, 'f', 2);
}

// Helper: intenta obtener un basename legible desde rutas, percent-encoding o cadenas “aplanadas”
static inline QString niceBaseName(const QString& raw) {
    // 1) Decodifica percent-encoding
    QString dec = QUrl::fromPercentEncoding(raw.toUtf8());

    // 2) Normaliza separadores
    QString norm = dec;
    norm.replace("\\", "/");
    norm.replace("%2F", "/");
    norm.replace("%5C", "/");

    // 3) Prueba basename directo
    QString base = QFileInfo(norm).fileName();
    auto looksLikeFile = [](const QString& s) {
        return !s.isEmpty() && s.contains('.');
    };
    if (looksLikeFile(base)) return base;

    // 4) Recuperación best-effort si vino “aplanado” sin separadores
    static const QStringList exts = {"cpp","cxx","cc","c","hpp","hh","h"};
    int bestPos = -1;
    QString bestExt;
    bool bestHadDot = false;

    for (const auto& ext : exts) {
        const QString dotExt = "." + ext;

        int pDot = norm.lastIndexOf(dotExt, -1, Qt::CaseInsensitive);
        if (pDot >= 0 && pDot > bestPos) {
            bestPos = pDot; bestExt = ext; bestHadDot = true;
        }
        int pPlain = norm.lastIndexOf(ext, -1, Qt::CaseInsensitive);
        if (pPlain >= 0 && pPlain > bestPos) {
            bestPos = pPlain; bestExt = ext; bestHadDot = false;
        }
    }

    if (bestPos >= 0) {
        int end = bestPos + (bestHadDot ? 1 : 0) + bestExt.size();
        int start = bestPos - 1;
        auto okCh = [](QChar ch) {
            return ch.isLetterOrNumber() || ch == '_' || ch == '-' || ch == '.';
        };
        while (start >= 0 && okCh(norm[start])) --start;
        ++start;

        QString name = norm.mid(start, end - start);
        if (!bestHadDot && name.size() > bestExt.size()
            && name.right(bestExt.size()) == bestExt) {
            name.insert(name.size() - bestExt.size(), ".");
        }
        if (looksLikeFile(name)) return name;
    }

    // 5) Último recurso: devuelve una versión acortada
    if (norm.size() > 40) return norm.left(20) + "…" + norm.right(15);
    return norm;
}
} // namespace

LeaksTab::LeaksTab(QWidget* parent): QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // --- Resumen KPIs ---
    auto* kpiRow = new QHBoxLayout();
    leakTotalLbl_ = new QLabel("Total fugado: 0.00 kB");
    largestLbl_   = new QLabel("Leak mayor: —");
    topFileLbl_   = new QLabel("Archivo con mayor frecuencia de leaks: —");
    leakRateLbl_  = new QLabel("Tasa de leaks: 0.00%");
    kpiRow->addWidget(leakTotalLbl_);
    kpiRow->addWidget(largestLbl_);
    kpiRow->addWidget(topFileLbl_);
    kpiRow->addWidget(leakRateLbl_);
    kpiRow->addStretch(1);
    root->addLayout(kpiRow);

    // --- Filtro + copiar ---
    auto* row = new QHBoxLayout(); root->addLayout(row);
    filterEdit_ = new QLineEdit(); filterEdit_->setPlaceholderText("Filtrar por archivo/tipo…"); row->addWidget(filterEdit_);
    copyBtn_ = new QPushButton("Copiar selección"); row->addWidget(copyBtn_);

    // --- Tabla ---
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

    // --- Charts: barras, pastel, temporal ---
    barsView_ = new QChartView(new QChart());
    pieView_  = new QChartView(new QChart());
    timeView_ = new QChartView(new QChart());
    barsView_->setRenderHint(QPainter::Antialiasing);
    pieView_->setRenderHint(QPainter::Antialiasing);
    timeView_->setRenderHint(QPainter::Antialiasing);

    auto* chartsRow = new QHBoxLayout();
    chartsRow->addWidget(barsView_, 2);
    chartsRow->addWidget(pieView_, 2);
    chartsRow->addWidget(timeView_, 3);
    root->addLayout(chartsRow);
}

void LeaksTab::updateSnapshot(const MetricsSnapshot& s) {
    // El modelo interno ya filtrará solo isLeak==true (ver TableModels.cpp)
    model_->setDataSet(s.leaks);

    // ----- KPIs con preferencia a los del snapshot; fallback local coherente -----

    // Total fugado (kB)
    leakTotalLbl_->setText(QString("Total fugado: %1 kB").arg(bytesToKB(s.leakBytes)));

    // Leak mayor (basename + tamaño en kB)
    if (s.largestLeakSz > 0) {
        const QString base = s.largestLeakFile.isEmpty() ? "—" : niceBaseName(s.largestLeakFile);
        largestLbl_->setText(QString("Leak mayor: %1 (%2 kB)")
                             .arg(base)
                             .arg(bytesToKB(static_cast<qint64>(s.largestLeakSz))));
    } else {
        // fallback local: buscar mayor entre los que son leak
        qint64 max_sz = 0; QString max_file = "—";
        for (const auto& L : s.leaks) {
            if (!L.isLeak) continue;
            if (L.size > max_sz) { max_sz = L.size; max_file = L.file; }
        }
        largestLbl_->setText(QString("Leak mayor: %1 (%2 kB)")
                             .arg(niceBaseName(max_file))
                             .arg(bytesToKB(max_sz)));
    }

    // Archivo con mayor frecuencia de leaks (basename + conteo + total kB)
    if (!s.topLeakFile.isEmpty()) {
        topFileLbl_->setText(QString("Archivo con mayor frecuencia de leaks: %1 (%2 leaks, %3 kB)")
                             .arg(niceBaseName(s.topLeakFile))
                             .arg(s.topLeakCount)
                             .arg(bytesToKB(static_cast<qint64>(s.topLeakBytes))));
    } else {
        // fallback local por frecuencia (solo fugas)
        QHash<QString, QPair<qint64,int>> byFile; // bytes, count
        for (const auto& L : s.leaks) {
            if (!L.isLeak) continue;
            auto& ref = byFile[L.file]; ref.first += L.size; ref.second += 1;
        }
        QString topf="—"; int cnt=0; qint64 bytes=0;
        for (auto it = byFile.begin(); it != byFile.end(); ++it) {
            if (it.value().second > cnt || (it.value().second == cnt && it.value().first > bytes)) {
                topf = it.key(); cnt = it.value().second; bytes = it.value().first;
            }
        }
        topFileLbl_->setText(QString("Archivo con mayor frecuencia de leaks: %1 (%2 leaks, %3 kB)")
                             .arg(niceBaseName(topf))
                             .arg(cnt)
                             .arg(bytesToKB(bytes)));
    }

    // Tasa de leaks (%)
    const double ratePct = (s.leakRate > 0.0) ? (100.0 * s.leakRate) : 0.0;
    leakRateLbl_->setText(QString("Tasa de leaks: %1%").arg(ratePct, 0, 'f', 2));

    // ----- Gráficas (las dejamos para la siguiente iteración) -----
    rebuildCharts(s);
}

void LeaksTab::rebuildCharts(const MetricsSnapshot& s) {
    // (sin cambios por ahora; lo ajustaremos en el siguiente paso si hace falta)
    // --- Helper: obtener una "clave" legible y consistente por archivo ---
    auto toNiceFileKey = [](const QString& raw) -> QString {
        QString dec = QUrl::fromPercentEncoding(raw.toUtf8());
        QString norm = dec;
        norm.replace("\\", "/");
        norm.replace("%2F", "/");
        norm.replace("%5C", "/");
        QString base = QFileInfo(norm).fileName();
        auto isGood = [](const QString& s) { return !s.isEmpty() && s.contains('.'); };
        if (isGood(base)) return base;

        static const QStringList exts = {"cpp","cxx","cc","c","hpp","hh","h"};
        int bestPos = -1; QString bestExt; bool bestHadDot = false;
        for (const auto& ext : exts) {
            const QString dotExt = "." + ext;
            int pDot = norm.lastIndexOf(dotExt, -1, Qt::CaseInsensitive);
            if (pDot >= 0 && pDot > bestPos) { bestPos = pDot; bestExt = ext; bestHadDot = true; }
            int pPlain = norm.lastIndexOf(ext, -1, Qt::CaseInsensitive);
            if (pPlain >= 0 && pPlain > bestPos) { bestPos = pPlain; bestExt = ext; bestHadDot = false; }
        }
        if (bestPos >= 0) {
            int end = bestPos + (bestHadDot ? 1 : 0) + bestExt.size();
            int start = bestPos - 1;
            auto okCh = [](QChar ch) { return ch.isLetterOrNumber() || ch == '_' || ch == '-' || ch == '.'; };
            while (start >= 0 && okCh(norm[start])) --start;
            ++start;
            QString name = norm.mid(start, end - start);
            if (!bestHadDot && name.size() > bestExt.size()
                && name.right(bestExt.size()) == bestExt) {
                name.insert(name.size() - bestExt.size(), ".");
            }
            if (isGood(name)) return name;
        }
        if (norm.size() > 40) return norm.left(20) + "…" + norm.right(15);
        return norm;
    };

    // --------- 1) Barras por archivo (bytes de fugas) ---------
    QHash<QString, qint64> bytesByKey;
    for (const auto& L : s.leaks) {
        if (!L.isLeak) continue;
        bytesByKey[toNiceFileKey(L.file)] += L.size;
    }

    QList<QPair<QString, qint64>> items;
    items.reserve(bytesByKey.size());
    for (auto it = bytesByKey.begin(); it != bytesByKey.end(); ++it) {
        items.append({it.key(), it.value()});
    }
    std::sort(items.begin(), items.end(),
              [](const QPair<QString,qint64>& a, const QPair<QString,qint64>& b){
                  return a.second > b.second;
              });

    const int TOPN = 8;
    QStringList cats;
    auto* set = new QBarSet("bytes");
    qint64 othersBytes = 0;

    for (int i = 0; i < items.size(); ++i) {
        if (i < TOPN) {
            cats << items[i].first;
            *set << static_cast<double>(items[i].second);
        } else {
            othersBytes += items[i].second;
        }
    }
    if (othersBytes > 0) {
        cats << "Otros";
        *set << static_cast<double>(othersBytes);
    }

    auto* barChart = new QChart();
    barChart->setTitle("Leaks por archivo (bytes)");
    auto* series = new QBarSeries();
    series->append(set);
    barChart->addSeries(series);

    auto* axisX = new QBarCategoryAxis();
    axisX->append(cats);
    auto* axisY = new QValueAxis();
    axisY->setTitleText("Bytes");

    barChart->addAxis(axisX, Qt::AlignBottom);
    barChart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    barChart->legend()->hide();
    barsView_->setChart(barChart);

    // --------- 2) Pie distribución por archivo (%) ---------
    auto* pieChart = new QChart();
    pieChart->setTitle("Distribución de leaks (%)");
    auto* pie = new QPieSeries();

    const int upTo = qMin(items.size(), TOPN);
    for (int i = 0; i < upTo; ++i) {
        if (items[i].second > 0)
            pie->append(items[i].first, static_cast<double>(items[i].second));
    }
    if (othersBytes > 0) {
        pie->append("Otros", static_cast<double>(othersBytes));
    }

    pie->setLabelsVisible(true);
    pieChart->addSeries(pie);
    pieChart->legend()->setVisible(true);

    for (auto* slice : pie->slices()) {
        slice->setLabel(QString("%1 (%2%)")
                            .arg(slice->label())
                            .arg(slice->percentage() * 100.0, 0, 'f', 1));
    }
    pieView_->setChart(pieChart);

    // --------- 3) Temporal: usar ts_ns relativo entre fugas ---------
    QVector<QPair<double,double>> timePoints; // (x=time[s], y=size[B])
    qulonglong tmin_ns = std::numeric_limits<qulonglong>::max();
    qulonglong tmax_ns = 0;

    for (const auto& L : s.leaks) {
        if (!L.isLeak) continue;
        if (L.ts_ns < tmin_ns) tmin_ns = L.ts_ns;
        if (L.ts_ns > tmax_ns) tmax_ns = L.ts_ns;
    }
    if (tmin_ns == std::numeric_limits<qulonglong>::max()) {
        tmin_ns = 0; tmax_ns = 0;
    }
    if (tmax_ns < tmin_ns) tmax_ns = tmin_ns + 1;

    auto* timeChart = new QChart();
    timeChart->setTitle("Temporal de asignaciones con fuga");
    auto* scat = new QScatterSeries();
    scat->setMarkerSize(6.0);

    for (const auto& L : s.leaks) {
        if (!L.isLeak) continue;
        const double x = (tmin_ns == 0) ? 0.0
                                        : double(L.ts_ns - tmin_ns) / 1e9; // segundos desde tmin
        scat->append(x, static_cast<double>(L.size));
    }

    timeChart->addSeries(scat);
    auto* axX = new QValueAxis(); axX->setTitleText("t (s)");
    axX->setRange(0.0, std::max(1.0, double(tmax_ns - tmin_ns) / 1e9));
    auto* axY = new QValueAxis(); axY->setTitleText("Tamaño (B)");
    timeChart->addAxis(axX, Qt::AlignBottom);
    timeChart->addAxis(axY, Qt::AlignLeft);
    scat->attachAxis(axX);
    scat->attachAxis(axY);
    timeChart->legend()->hide();
    timeView_->setChart(timeChart);
}

void LeaksTab::onCopySelected() {
    auto idx = table_->currentIndex(); if (!idx.isValid()) return;
    int row = proxy_->mapToSource(idx).row();
    LeakItem item = model_->itemAt(row);
    QString text = QString("ptr=0x%1 size=%2 file=%3 line=%4 type=%5 ts_ns=%6")
        .arg(QString::number(item.ptr,16)).arg(item.size).arg(item.file).arg(item.line).arg(item.type).arg(item.ts_ns);
    QApplication::clipboard()->setText(text);
}