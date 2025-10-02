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
#include <QDateTime>
#include <QValueAxis>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>

LeaksTab::LeaksTab(QWidget* parent): QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // --- Resumen KPIs ---
    auto* kpiRow = new QHBoxLayout();
    leakTotalLbl_ = new QLabel("Total fugado: 0 MB");
    largestLbl_   = new QLabel("Leak mayor: —");
    topFileLbl_   = new QLabel("Archivo top: —");
    leakRateLbl_  = new QLabel("Leak rate: 0.0%");
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
    model_->setDataSet(s.leaks);

    // ----- Preferir KPIs del snapshot (runtime); fallback si no vienen -----
    double leak_mb = s.leakBytes / (1024.0*1024.0);
    leakTotalLbl_->setText(QString("Total fugado: %1 MB").arg(leak_mb, 0, 'f', 2));

    if (s.largestLeakSz > 0) {
        largestLbl_->setText(QString("Leak mayor: %1 (%2 B)").arg(s.largestLeakFile).arg(s.largestLeakSz));
    } else {
        // fallback (cálculo local)
        qint64 max_sz = 0; QString max_file = "—";
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        for (const auto& L : s.leaks) {
            const qint64 age_ms = now_ms - (qint64)(L.ts_ns / 1000000ULL);
            if (age_ms >= 3000 && L.size > max_sz) { max_sz = L.size; max_file = L.file; }
        }
        largestLbl_->setText(QString("Leak mayor: %1 (%2 B)").arg(max_file).arg(max_sz));
    }

    if (!s.topLeakFile.isEmpty()) {
        topFileLbl_->setText(QString("Archivo top: %1 (%2 leaks, %3 B)")
                             .arg(s.topLeakFile).arg(s.topLeakCount).arg(s.topLeakBytes));
    } else {
        // fallback local por frecuencia
        QHash<QString, QPair<qint64,int>> byFile;
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        for (const auto& L : s.leaks) {
            const qint64 age_ms = now_ms - (qint64)(L.ts_ns / 1000000ULL);
            if (age_ms >= 3000) {
                auto& ref = byFile[L.file]; ref.first += L.size; ref.second += 1;
            }
        }
        QString topf="—"; int cnt=0; qint64 bytes=0;
        for (auto it = byFile.begin(); it != byFile.end(); ++it) {
            if (it.value().second > cnt || (it.value().second == cnt && it.value().first > bytes)) {
                topf = it.key(); cnt = it.value().second; bytes = it.value().first;
            }
        }
        topFileLbl_->setText(QString("Archivo top: %1 (%2 leaks, %3 B)").arg(topf).arg(cnt).arg(bytes));
    }

    const double rate = (s.leakRate > 0.0) ? (100.0 * s.leakRate) : 0.0;
    leakRateLbl_->setText(QString("Leak rate: %1%").arg(rate, 0, 'f', 2));

    // ----- Gráficas -----
    rebuildCharts(s);
}

void LeaksTab::rebuildCharts(const MetricsSnapshot& s) {
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const qint64 THR_MS = 3000;

    // --- Helper: obtener una "clave" legible y consistente por archivo ---
        // --- Helper: obtener una "clave" legible y consistente por archivo ---
    auto toNiceFileKey = [](const QString& raw) -> QString {
        // 1) Decodificar percent-encoding
        QString dec = QUrl::fromPercentEncoding(raw.toUtf8());

        // 2) Normalizar separadores
        QString norm = dec;
        norm.replace("\\", "/");
        norm.replace("%2F", "/");
        norm.replace("%5C", "/");

        // 3) Intentar basename convencional
        QString base = QFileInfo(norm).fileName();
        auto isGood = [](const QString& s) {
            return !s.isEmpty() && s.contains('.');
        };
        if (isGood(base)) return base;

        // 4) Recuperar basename desde una cadena "aplanada"
        //    (sin separadores) buscando la extensión al final.
        static const QStringList exts = {"cpp","cxx","cc","c","hpp","hh","h"};
        int bestPos = -1;
        QString bestExt;
        bool bestHadDot = false;

        for (const auto& ext : exts) {
            const QString dotExt = "." + ext;

            int pDot = norm.lastIndexOf(dotExt, -1, Qt::CaseInsensitive); // "...alloc_a.cpp"
            if (pDot >= 0 && pDot > bestPos) {
                bestPos = pDot;
                bestExt = ext;
                bestHadDot = true;
            }
            int pPlain = norm.lastIndexOf(ext, -1, Qt::CaseInsensitive);  // "...alloc_acpp" (aplanado)
            if (pPlain >= 0 && pPlain > bestPos) {
                bestPos = pPlain;
                bestExt = ext;
                bestHadDot = false;
            }
        }

        if (bestPos >= 0) {
            // end: posición tras la extensión
            int end = bestPos + (bestHadDot ? 1 : 0) + bestExt.size();

            // start: retroceder hasta el primer no [A-Za-z0-9._-]
            int start = bestPos - 1;
            auto isOkCh = [](QChar ch) {
                return ch.isLetterOrNumber() || ch == '_' || ch == '-' || ch == '.';
            };
            while (start >= 0 && isOkCh(norm[start])) --start;
            ++start;

            QString name = norm.mid(start, end - start);

            // Si venía sin punto antes de la extensión, insértalo para mostrarse bien.
            if (!bestHadDot && name.size() > bestExt.size()
                && name.right(bestExt.size()) == bestExt) {
                name.insert(name.size() - bestExt.size(), ".");
            }

            if (isGood(name)) return name;
        }

        // 5) Último recurso: devolver una versión acortada para que no rompa la UI
        if (norm.size() > 40) return norm.left(20) + "…" + norm.right(15);
        return norm;
    };

    // --- Agrupar por archivo (basename) solo para fugas "confirmadas" (edad >= THR_MS) ---
    QHash<QString, qint64> bytesByKey;
    qint64 tmin_detect = LLONG_MAX, tmax_detect = 0;

    for (const auto& L : s.leaks) {
        const qint64 ts_ms = static_cast<qint64>(L.ts_ns / 1000000ULL);
        if (now_ms - ts_ms >= THR_MS) {
            const QString key = toNiceFileKey(L.file);
            bytesByKey[key] += L.size;

            const qint64 det_ms = ts_ms + THR_MS;
            if (det_ms < tmin_detect) tmin_detect = det_ms;
            if (det_ms > tmax_detect) tmax_detect = det_ms;
        }
    }
    if (tmin_detect == LLONG_MAX) tmin_detect = now_ms;
    if (tmax_detect < tmin_detect) tmax_detect = tmin_detect + 1;

    // --- Ordenar por bytes y construir Top-N + "Otros" ---
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

    // --- Barras por archivo (bytes) ---
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

    // --- Pie distribución por archivo (%) ---
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

    // Etiquetas con porcentaje
    for (auto* slice : pie->slices()) {
        slice->setLabel(QString("%1 (%2%)")
                            .arg(slice->label())
                            .arg(slice->percentage() * 100.0, 0, 'f', 1));
    }
    pieView_->setChart(pieChart);

    // --- Temporal de detección (igual) ---
    auto* timeChart = new QChart();
    timeChart->setTitle("Temporal de detección de leaks");
    auto* scat = new QScatterSeries();
    scat->setMarkerSize(6.0);

    for (const auto& L : s.leaks) {
        const qint64 ts_ms = static_cast<qint64>(L.ts_ns / 1000000ULL);
        if (now_ms - ts_ms >= THR_MS) {
            const double x = double(ts_ms + THR_MS - tmin_detect) / 1000.0;
            scat->append(x, static_cast<double>(L.size));
        }
    }

    timeChart->addSeries(scat);
    auto* axX = new QValueAxis(); axX->setTitleText("t (s)");
    axX->setRange(0.0, std::max(1.0, double(tmax_detect - tmin_detect) / 1000.0));
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
