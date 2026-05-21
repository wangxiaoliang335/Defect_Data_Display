#include "Defect_Data_Display.h"
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>
#include <QVBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QMouseEvent>

Defect_Data_Display::Defect_Data_Display(QWidget *parent)
    : QMainWindow(parent)
    , m_chartViewAoi(nullptr)
    , m_chartViewInspection(nullptr)
    , m_chartViewPlatform(nullptr)
    , m_timer(nullptr)
    , m_isDragging(false)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    ui.setupUi(this);
    setupCharts();

    connect(ui.btnRefresh, &QPushButton::clicked, this, &Defect_Data_Display::onRefreshClicked);
    connect(ui.comboTimeRange, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Defect_Data_Display::onTimeRangeChanged);
    connect(ui.btnMinimize, &QPushButton::clicked, this, &Defect_Data_Display::onMinimizeClicked);
    connect(ui.btnClose, &QPushButton::clicked, this, &Defect_Data_Display::onCloseClicked);
    connect(ui.tabWidget, &QTabWidget::currentChanged, this, &Defect_Data_Display::onTabChanged);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Defect_Data_Display::updateDateTime);
    m_timer->start(1000);
    updateDateTime();

    if (!connectToDatabase()) {
        ui.labelStatus->setText("状态: 数据库断开");
        ui.labelStatus->setStyleSheet("color: #ff4444;");
    } else {
        ui.labelStatus->setText("状态: 已连接");
        ui.labelStatus->setStyleSheet("color: #00ff88;");
        onRefreshClicked();
    }
}

Defect_Data_Display::~Defect_Data_Display()
{
    if (m_timer) {
        m_timer->stop();
    }
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void Defect_Data_Display::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_isDragging = true;
    }
    QMainWindow::mousePressEvent(event);
}

void Defect_Data_Display::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton && m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
    }
    QMainWindow::mouseMoveEvent(event);
}

void Defect_Data_Display::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
    QMainWindow::mouseReleaseEvent(event);
}

void Defect_Data_Display::onMinimizeClicked()
{
    showMinimized();
}

void Defect_Data_Display::onCloseClicked()
{
    close();
}

void Defect_Data_Display::onTabChanged(int index)
{
    QTimer::singleShot(100, this, [this, index]() {
        if (index == 1) {
            QString timeRange = ui.comboTimeRange->currentText();
            loadDefectMapping(timeRange);
        }
    });
}

void Defect_Data_Display::updateDateTime()
{
    ui.dateTimeLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  HH:mm:ss"));
}

void Defect_Data_Display::setupCharts()
{
    QChart* chartPlatform = new QChart();
    chartPlatform->setTitle("各工位检测统计");
    chartPlatform->setAnimationOptions(QChart::SeriesAnimations);
    chartPlatform->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartPlatform->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartPlatform->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewPlatform = new QChartView(chartPlatform);
    ((QChartView*)m_chartViewPlatform)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewPlatform)->setMinimumHeight(200);
    ((QChartView*)m_chartViewPlatform)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutPlatform = new QVBoxLayout(ui.chartPlatform);
    layoutPlatform->setContentsMargins(0, 0, 0, 0);
    layoutPlatform->addWidget((QChartView*)m_chartViewPlatform);

    QChart* chartAoi = new QChart();
    chartAoi->setTitle("AOI 缺陷分析");
    chartAoi->setAnimationOptions(QChart::SeriesAnimations);
    chartAoi->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartAoi->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartAoi->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewAoi = new QChartView(chartAoi);
    ((QChartView*)m_chartViewAoi)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewAoi)->setMinimumHeight(250);
    ((QChartView*)m_chartViewAoi)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutAoi = new QVBoxLayout(ui.chartAoiDefect);
    layoutAoi->setContentsMargins(0, 0, 0, 0);
    layoutAoi->addWidget((QChartView*)m_chartViewAoi);

    QChart* chartInspection = new QChart();
    chartInspection->setTitle("检测结果统计");
    chartInspection->setAnimationOptions(QChart::SeriesAnimations);
    chartInspection->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartInspection->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartInspection->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewInspection = new QChartView(chartInspection);
    ((QChartView*)m_chartViewInspection)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewInspection)->setMinimumHeight(250);
    ((QChartView*)m_chartViewInspection)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutInspection = new QVBoxLayout(ui.chartInspectionResult);
    layoutInspection->setContentsMargins(0, 0, 0, 0);
    layoutInspection->addWidget((QChartView*)m_chartViewInspection);

    QChart* chartMapping = new QChart();
    chartMapping->setTitle("缺陷位置分布");
    chartMapping->setAnimationOptions(QChart::SeriesAnimations);
    chartMapping->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartMapping->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartMapping->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewDefectMapping = new QChartView(chartMapping);
    ((QChartView*)m_chartViewDefectMapping)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewDefectMapping)->setMinimumHeight(400);
    ((QChartView*)m_chartViewDefectMapping)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutMapping = new QVBoxLayout(ui.chartDefectMapping);
    layoutMapping->setContentsMargins(0, 0, 0, 0);
    layoutMapping->addWidget((QChartView*)m_chartViewDefectMapping);
}

bool Defect_Data_Display::connectToDatabase()
{
    QString connectionString = "DRIVER={MySQL ODBC 5.3 ANSI Driver};"
                               "SERVER=localhost;"
                               "PORT=3306;"
                               "DATABASE=ivs_lcd;"
                               "UID=root;"
                               "PWD=123456;"
                               "OPTION=8;";

    m_db = QSqlDatabase::addDatabase("QODBC");
    m_db.setDatabaseName(connectionString);
    m_db.setConnectOptions("SQL_ATTR_CONNECTION_TIMEOUT=30000");

    if (!m_db.open()) {
        qDebug() << "Database connection failed:" << m_db.lastError().text();
        return false;
    }

    qDebug() << "Database connected successfully!";
    return true;
}

QString Defect_Data_Display::getTimeFilterClause(const QString& timeRange)
{
    if (timeRange == "按小时") {
        return "DATE_FORMAT(StartTime, '%Y-%m-%d %H:00')";
    } else if (timeRange == "按天") {
        return "DATE_FORMAT(StartTime, '%Y-%m-%d')";
    } else if (timeRange == "按月") {
        return "DATE_FORMAT(StartTime, '%Y-%m')";
    }
    return "DATE_FORMAT(StartTime, '%Y-%m-%d')";
}

QString Defect_Data_Display::getDateTimeRange(const QString& timeRange)
{
    if (timeRange == "按小时") {
        return " DATE_SUB(NOW(), INTERVAL 24 HOUR)";
    } else if (timeRange == "按天") {
        return " DATE_SUB(NOW(), INTERVAL 7 DAY)";
    } else if (timeRange == "按月") {
        return " DATE_SUB(NOW(), INTERVAL 30 DAY)";
    }
    return " DATE_SUB(NOW(), INTERVAL 7 DAY)";
}

void Defect_Data_Display::updateStats(int totalInspect, int passCount, int failCount, double passRate, int totalDefects)
{
    ui.statValue1->setText(QString::number(totalInspect));
    ui.statValue2->setText(QString::number(passCount));
    ui.statValue3->setText(QString::number(failCount));

    QString passRateStr = QString::number(passRate, 'f', 2) + "%";
    ui.statValue4->setText(passRateStr);

    if (passRate >= 95) {
        ui.statValue4->setStyleSheet("color: #00ff88; font-size: 32px; font-weight: bold;");
    } else if (passRate >= 90) {
        ui.statValue4->setStyleSheet("color: #ffaa00; font-size: 32px; font-weight: bold;");
    } else {
        ui.statValue4->setStyleSheet("color: #ff4444; font-size: 32px; font-weight: bold;");
    }

    ui.statValue5->setText(QString::number(totalDefects));
}

void Defect_Data_Display::loadAoiDefectData(const QString& timeRange)
{
    if (!m_db.isOpen() || !m_db.isValid()) {
        m_db.close();
        if (!m_db.open() || !m_db.isOpen()) {
            ui.labelStatus->setText("状态: 连接丢失");
            ui.labelStatus->setStyleSheet("color: #ff4444;");
            return;
        }
    }

    QString dateRange = getDateTimeRange(timeRange);

    QString queryStr = QString(R"(
        SELECT
            Type as defect_type,
            COUNT(*) as defect_count
        FROM ivs_lcd_aoidefect
        WHERE StartTime >= %1
        GROUP BY Type
        ORDER BY defect_count DESC
    )").arg(dateRange);

    qDebug() << "Executing AOI query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);

    if (!query.exec(queryStr)) {
        qDebug() << "Query failed:" << query.lastError().text();
        return;
    }

    int totalDefects = 0;
    QMap<QString, QList<QPair<QString, int>>> defectByType;

    while (query.next()) {
        QString defectType = query.value(0).toString();
        int count = query.value(1).toInt();
        totalDefects += count;

        defectByType[defectType].append(qMakePair("All", count));
    }

    updateAoiDefectChart(defectByType);
    ui.statValue5->setText(QString::number(totalDefects));
}

void Defect_Data_Display::updateAoiDefectChart(const QMap<QString, QList<QPair<QString, int>>>& defectByType)
{
    QChart* chart = ((QChartView*)m_chartViewAoi)->chart();
    chart->removeAllSeries();

    QMap<QString, QColor> defectColors;
    defectColors["BlackDot"] = Qt::darkGray;
    defectColors["BrightDot"] = QColor(255, 200, 0);
    defectColors["Line"] = Qt::blue;
    defectColors["Mura"] = Qt::red;
    defectColors["Block"] = Qt::green;

    QBarSeries* series = new QBarSeries();

    for (auto it = defectByType.constBegin(); it != defectByType.constEnd(); ++it) {
        QString defectType = it.key();
        QBarSet* barSet = new QBarSet(defectType);
        barSet->setColor(defectColors.value(defectType, Qt::gray));
        barSet->setLabelColor(QColor(234, 234, 234));

        int total = 0;
        for (const auto& pair : it.value()) {
            total += pair.second;
        }
        *barSet << total;

        series->append(barSet);
    }

    chart->addSeries(series);
    chart->setTitle("AOI 缺陷分析");

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(QStringList() << "");
    axisX->setLabelsColor(QColor(234, 234, 234));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("缺陷数量");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->addAxis(axisY, Qt::AlignLeft);

    series->attachAxis(axisX);
    series->attachAxis(axisY);
}

void Defect_Data_Display::loadInspectionResultData(const QString& timeRange)
{
    if (!m_db.isOpen() || !m_db.isValid()) {
        return;
    }

    QString dateRange = getDateTimeRange(timeRange);

    QString queryStr = QString(R"(
        SELECT
            SUM(CASE WHEN AOIResult = 'OK' THEN 1 ELSE 0 END) as pass_count,
            SUM(CASE WHEN AOIResult != 'OK' THEN 1 ELSE 0 END) as fail_count,
            COUNT(*) as total_count
        FROM ivs_lcd_inspectionresult
        WHERE StartTime >= %1
    )").arg(dateRange);

    qDebug() << "Executing Inspection query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);

    if (!query.exec(queryStr)) {
        qDebug() << "Query failed:" << query.lastError().text();
        return;
    }

    int totalInspect = 0;
    int passCount = 0;
    int failCount = 0;
    QMap<QString, int> passByPeriod;
    QMap<QString, int> failByPeriod;

    if (query.next()) {
        passCount = query.value(0).toInt();
        failCount = query.value(1).toInt();
        totalInspect = query.value(2).toInt();

        passByPeriod["总计"] = passCount;
        failByPeriod["总计"] = failCount;
    }

    updateInspectionResultChart(passByPeriod, failByPeriod);

    double passRate = (totalInspect > 0) ? (passCount * 100.0 / totalInspect) : 0;
    updateStats(totalInspect, passCount, failCount, passRate, 0);
}

void Defect_Data_Display::updateInspectionResultChart(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod)
{
    QChart* chart = ((QChartView*)m_chartViewInspection)->chart();
    chart->removeAllSeries();

    QBarSet* passSet = new QBarSet("通过数");
    passSet->setColor(QColor(0, 255, 136));
    passSet->setLabelColor(QColor(234, 234, 234));
    QBarSet* failSet = new QBarSet("失败数");
    failSet->setColor(QColor(255, 68, 68));
    failSet->setLabelColor(QColor(234, 234, 234));

    for (const QString& period : passByPeriod.keys()) {
        *passSet << passByPeriod.value(period, 0);
        *failSet << failByPeriod.value(period, 0);
    }

    QBarSeries* series = new QBarSeries();
    series->append(passSet);
    series->append(failSet);

    chart->addSeries(series);
    chart->setTitle("检测结果统计");

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(QStringList() << "");
    axisX->setLabelsColor(QColor(234, 234, 234));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("数量");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->addAxis(axisY, Qt::AlignLeft);

    series->attachAxis(axisX);
    series->attachAxis(axisY);
}

void Defect_Data_Display::loadPlatformStats(const QString& timeRange)
{
    if (!m_db.isOpen() || !m_db.isValid()) {
        return;
    }

    QString dateRange = getDateTimeRange(timeRange);

    QString queryStr = QString(R"(
        SELECT
            PlatformID,
            SUM(CASE WHEN AOIResult = 'OK' THEN 1 ELSE 0 END) as pass_count,
            SUM(CASE WHEN AOIResult != 'OK' THEN 1 ELSE 0 END) as fail_count
        FROM ivs_lcd_inspectionresult
        WHERE StartTime >= %1
        GROUP BY PlatformID
        ORDER BY PlatformID
    )").arg(dateRange);

    qDebug() << "Executing Platform query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);

    if (!query.exec(queryStr)) {
        qDebug() << "Platform query failed:" << query.lastError().text();
        return;
    }

    QMap<int, QPair<int, int>> platformStats;

    while (query.next()) {
        int platformId = query.value(0).toInt();
        int passCount = query.value(1).toInt();
        int failCount = query.value(2).toInt();
        platformStats[platformId] = qMakePair(passCount, failCount);
    }

    updatePlatformChart(platformStats);
}

void Defect_Data_Display::updatePlatformChart(const QMap<int, QPair<int, int>>& platformStats)
{
    QChart* chart = ((QChartView*)m_chartViewPlatform)->chart();
    chart->removeAllSeries();

    if (platformStats.isEmpty()) {
        return;
    }

    QStringList categories;
    QBarSet* passSet = new QBarSet("通过数");
    passSet->setColor(QColor(0, 255, 136));
    passSet->setLabelColor(QColor(234, 234, 234));

    QBarSet* failSet = new QBarSet("失败数");
    failSet->setColor(QColor(255, 68, 68));
    failSet->setLabelColor(QColor(234, 234, 234));

    QMap<int, QPair<int, int>>::const_iterator it;
    for (it = platformStats.constBegin(); it != platformStats.constEnd(); ++it) {
        categories.append(QString("工位 %1").arg(it.key()));
        *passSet << it.value().first;
        *failSet << it.value().second;
    }

    QBarSeries* series = new QBarSeries();
    series->append(passSet);
    series->append(failSet);

    chart->addSeries(series);
    chart->setTitle("各工位检测统计");

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    axisX->setLabelsColor(QColor(234, 234, 234));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("数量");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->addAxis(axisY, Qt::AlignLeft);

    series->attachAxis(axisX);
    series->attachAxis(axisY);
}

void Defect_Data_Display::loadDefectMapping(const QString& timeRange)
{
    if (!m_db.isOpen() || !m_db.isValid()) {
        m_db.close();
        if (!m_db.open() || !m_db.isOpen()) {
            qDebug() << "Database not open";
            return;
        }
    }

    QString dateRange = getDateTimeRange(timeRange);

    QString queryStr = QString(R"(
        SELECT
            Pos_x, Pos_y, Type
        FROM ivs_lcd_aoidefect
        WHERE StartTime >= %1
        ORDER BY StartTime DESC
        LIMIT 5000
    )").arg(dateRange);

    qDebug() << "Executing Defect Mapping query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);

    if (!query.exec(queryStr)) {
        qDebug() << "Defect Mapping query failed:" << query.lastError().text();
        return;
    }

    QList<QPair<int, int>> positions;
    QStringList types;

    while (query.next()) {
        int posX = query.value(0).toInt();
        int posY = query.value(1).toInt();
        QString type = query.value(2).toString();

        positions.append(qMakePair(posX, posY));
        types.append(type);
    }

    qDebug() << "Loaded" << positions.size() << "defect positions";
    updateDefectMappingChart(positions, types);
}

void Defect_Data_Display::updateDefectMappingChart(const QList<QPair<int, int>>& defectPositions, const QStringList& defectTypes)
{
    qDebug() << "updateDefectMappingChart called with" << defectPositions.size() << "positions";

    QChart* chart = ((QChartView*)m_chartViewDefectMapping)->chart();
    chart->removeAllSeries();

    if (defectPositions.isEmpty()) {
        qDebug() << "No positions, returning early";
        return;
    }

    QMap<QString, QColor> defectColors;
    defectColors["BlackDot"] = Qt::darkGray;
    defectColors["BrightDot"] = QColor(255, 200, 0);
    defectColors["Line"] = Qt::blue;
    defectColors["Mura"] = Qt::red;
    defectColors["Block"] = Qt::green;

    QMap<QString, QScatterSeries*> seriesMap;

    for (int i = 0; i < defectPositions.size() && i < 5000; ++i) {
        QString type = defectTypes.value(i, "Other");
        if (!seriesMap.contains(type)) {
            QScatterSeries* series = new QScatterSeries();
            series->setName(type);
            series->setColor(defectColors.value(type, Qt::gray));
            series->setMarkerSize(8);
            seriesMap[type] = series;
        }
        seriesMap[type]->append(defectPositions[i].first, defectPositions[i].second);
    }

    qDebug() << "Created" << seriesMap.size() << "scatter series";

    for (auto series : seriesMap.values()) {
        chart->addSeries(series);
    }

    chart->setTitle("缺陷位置分布 (散点图)");

    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    if (!defectPositions.isEmpty()) {
        minX = defectPositions.first().first;
        maxX = defectPositions.first().first;
        minY = defectPositions.first().second;
        maxY = defectPositions.first().second;
        for (const auto& pos : defectPositions) {
            minX = qMin(minX, (double)pos.first);
            maxX = qMax(maxX, (double)pos.first);
            minY = qMin(minY, (double)pos.second);
            maxY = qMax(maxY, (double)pos.second);
        }
    }

    qDebug() << "X range:" << minX << "-" << maxX << "Y range:" << minY << "-" << maxY;

    QValueAxis* axisX = new QValueAxis();
    axisX->setTitleText("X 坐标");
    axisX->setLabelFormat("%d");
    axisX->setLabelsColor(QColor(234, 234, 234));
    axisX->setTitleBrush(QBrush(QColor(0, 217, 255)));
    axisX->setRange(minX - 10, maxX + 10);
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Y 坐标");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    axisY->setRange(minY - 10, maxY + 10);
    chart->addAxis(axisY, Qt::AlignLeft);

    for (auto series : seriesMap.values()) {
        series->attachAxis(axisX);
        series->attachAxis(axisY);
    }

    qDebug() << "Chart update complete";
}

void Defect_Data_Display::onRefreshClicked()
{
    QString timeRange = ui.comboTimeRange->currentText();
    ui.labelStatus->setText("状态: 加载中...");
    ui.labelStatus->setStyleSheet("color: #ffaa00;");

    loadAoiDefectData(timeRange);
    loadInspectionResultData(timeRange);
    loadPlatformStats(timeRange);

    ui.labelStatus->setText("状态: 已更新 " + QDateTime::currentDateTime().toString("HH:mm:ss"));
    ui.labelStatus->setStyleSheet("color: #00ff88;");
}

void Defect_Data_Display::onTimeRangeChanged(int index)
{
    Q_UNUSED(index);
    onRefreshClicked();
}
