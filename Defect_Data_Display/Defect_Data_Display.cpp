#include "Defect_Data_Display.h"
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLineSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QVBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QMouseEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <QHeaderView>
#include <QToolTip>
#include <QGraphicsSimpleTextItem>
#include <algorithm>

Defect_Data_Display::Defect_Data_Display(QWidget *parent)
    : QMainWindow(parent)
    , m_chartViewAoi(nullptr)
    , m_chartViewInspectionPass(nullptr)
    , m_chartViewInspectionFail(nullptr)
    , m_chartViewPlatform0(nullptr)
    , m_chartViewPlatform1(nullptr)
    , m_chartViewPlatform2(nullptr)
    , m_chartViewPlatform3(nullptr)
    , m_chartViewDefectMapping(nullptr)
    , m_chartViewTrend(nullptr)
    , m_chartViewDefectRate(nullptr)
    , m_chartViewDetail(nullptr)
    , m_chartViewPieDetail(nullptr)
    , m_chartViewLocationAbnormal(nullptr)
    , m_timer(nullptr)
    , m_workerThread(nullptr)
    , m_tabWorkerThread(nullptr)
    , m_currentLoadId(0)
    , m_selectedDate(QDate::currentDate())
    , m_isDragging(false)
    , m_isLoading(false)
    , m_isTabLoading(false)
    , m_lastMainLoadTime(0)
    , m_tooltipLabel(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    ui.setupUi(this);

    ui.dateEdit->setDate(m_selectedDate);
    ui.dateEdit->setDisplayFormat("yyyy-MM-dd");

    setupCharts();

    // Install event filter for chart tooltips
    void* platformCharts[] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
    for (int i = 0; i < 4; ++i) {
        QChartView* cv = (QChartView*)platformCharts[i];
        cv->setMouseTracking(true);
        cv->installEventFilter(this);
        cv->viewport()->installEventFilter(this);
    }

    connect(ui.btnRefresh, &QPushButton::clicked, this, &Defect_Data_Display::onRefreshClicked);
    connect(ui.comboTimeRange, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Defect_Data_Display::onTimeRangeChanged);
    connect(ui.dateEdit, &QDateEdit::dateChanged, this, &Defect_Data_Display::onDateChanged);
    connect(ui.btnMinimize, &QPushButton::clicked, this, &Defect_Data_Display::onMinimizeClicked);
    connect(ui.btnClose, &QPushButton::clicked, this, &Defect_Data_Display::onCloseClicked);
    connect(ui.tabWidget, &QTabWidget::currentChanged, this, &Defect_Data_Display::onTabChanged);
    connect(ui.searchEdit, &QLineEdit::returnPressed, this, &Defect_Data_Display::onSearchClicked);
    connect(ui.btnSearch, &QPushButton::clicked, this, &Defect_Data_Display::onSearchClicked);

    // Search box styling
    ui.searchEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: rgba(30, 40, 60, 200);
            border: 1px solid rgba(0, 217, 255, 80);
            border-radius: 6px;
            padding: 4px 10px;
            color: #e0f0ff;
            font-size: 13px;
            selection-background-color: #00d9ff;
            selection-color: #000;
        }
        QLineEdit:focus {
            border: 1px solid #00d9ff;
            background-color: rgba(20, 35, 55, 220);
        }
        QLineEdit:!enabled {
            background-color: rgba(20, 30, 45, 150);
            color: #607080;
            border: 1px solid rgba(0, 217, 255, 40);
        }
    )");

    ui.btnSearch->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 150, 200, 180),
                stop:1 rgba(0, 100, 160, 180));
            color: #ffffff;
            border: 1px solid rgba(0, 217, 255, 100);
            border-radius: 6px;
            padding: 4px 12px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 190, 250, 220),
                stop:1 rgba(0, 130, 200, 220));
            border: 1px solid #00d9ff;
            color: #ffffff;
        }
        QPushButton:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 100, 160, 220),
                stop:1 rgba(0, 70, 120, 220));
            border: 1px solid rgba(0, 217, 255, 150);
            padding-top: 5px;
            padding-bottom: 3px;
        }
        QPushButton:!enabled {
            background-color: rgba(40, 55, 70, 150);
            color: #607080;
            border: 1px solid rgba(0, 217, 255, 40);
        }
    )");

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Defect_Data_Display::updateDateTime);
    m_timer->start(1000);
    updateDateTime();

    if (!connectToDatabase()) {
        ui.labelStatus->setText("Status: DB Disconnected");
        ui.labelStatus->setStyleSheet("color: #ff4444;");
    } else {
        ui.labelStatus->setText("Status: Connected - Loading...");
        ui.labelStatus->setStyleSheet("color: #00ff88;");
        QTimer::singleShot(500, this, [this]() {
            onRefreshClicked();
        });
    }
}

Defect_Data_Display::~Defect_Data_Display()
{
    if (m_timer) {
        m_timer->stop();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(100);
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
        }
        delete m_workerThread;
    }
    if (m_tabWorkerThread) {
        m_tabWorkerThread->quit();
        m_tabWorkerThread->wait(100);
        if (m_tabWorkerThread->isRunning()) {
            m_tabWorkerThread->terminate();
        }
        delete m_tabWorkerThread;
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

bool Defect_Data_Display::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        bool matched = false;
        for (int p = 0; p < 4; ++p) {
            void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
            QChartView* cv = (QChartView*)chartViewPtrs[p];
            if (cv && (cv == watched || cv->viewport() == watched)) {
                QChart* chart = cv->chart();
                QList<QAbstractSeries*> seriesList = chart->series();
                if (!seriesList.isEmpty()) {
                    QPointF chartPos = chart->mapToValue(QPointF(me->pos()), seriesList.first());
                    showPlatformChartTooltip(cv, p, me->pos(), chartPos);
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            m_tooltipLabel->hide();
        }
    } else if (event->type() == QEvent::Leave) {
        for (int p = 0; p < 4; ++p) {
            void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
            QChartView* cv = (QChartView*)chartViewPtrs[p];
            if (cv && (cv == watched || cv->viewport() == watched)) {
                m_tooltipLabel->hide();
                break;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void Defect_Data_Display::showPlatformChartTooltip(QChartView* chartView, int platformIdx, const QPoint& viewportPos, const QPointF& chartPos)
{
    QChart* chart = chartView->chart();
    QList<QAbstractSeries*> allSeries = chart->series();
    if (allSeries.isEmpty()) return;

    QAbstractBarSeries* barSeries = qobject_cast<QAbstractBarSeries*>(allSeries.first());
    if (!barSeries) return;

    QRectF plotArea = chart->plotArea();
    qreal relX = (viewportPos.x() - plotArea.left()) / plotArea.width();
    qreal relY = (viewportPos.y() - plotArea.top()) / plotArea.height();

    if (relX < 0 || relX > 1 || relY < 0 || relY > 1) return;

    QList<QAbstractAxis*> axesX = chart->axes(Qt::Horizontal);
    if (axesX.isEmpty()) return;
    QBarCategoryAxis* axisX = qobject_cast<QBarCategoryAxis*>(axesX.first());
    if (!axisX) return;
    QStringList categories = axisX->categories();
    if (categories.isEmpty()) return;

    int numCategories = categories.size();
    int numBarSets = barSeries->count();
    int totalBars = numCategories * numBarSets;
    if (totalBars == 0) return;

    int barIndex = static_cast<int>(relX * totalBars);
    int categoryIndex = barIndex / qMax(numBarSets, 1);
    if (categoryIndex < 0 || categoryIndex >= categories.size()) return;

    QString timeKey = categories[categoryIndex];

    QString timeRange = ui.comboTimeRange->currentText();
    QString originalKey;
    
    // Check if we have stacked AOIResult data
    bool hasStackedData = !m_platformAoiResultData.isEmpty();
    
    if (hasStackedData) {
        // Find original key from m_platformAoiResultData
        for (auto it = m_platformAoiResultData.constBegin(); it != m_platformAoiResultData.constEnd(); ++it) {
            QString label = it.key();
            if (timeRange == "按小时" && label.contains(" ")) {
                if (label.split(" ").at(1).left(5) == timeKey) { originalKey = it.key(); break; }
            } else if (timeRange == "按天" && label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3 && parts.at(2) == timeKey) { originalKey = it.key(); break; }
            } else if (timeRange == "按月" && label == timeKey) {
                originalKey = it.key(); break;
            }
        }
    } else {
        // Fallback to m_platformTrendData
        for (auto it = m_platformTrendData.constBegin(); it != m_platformTrendData.constEnd(); ++it) {
            QString label = it.key();
            if (timeRange == "按小时" && label.contains(" ")) {
                if (label.split(" ").at(1).left(5) == timeKey) { originalKey = it.key(); break; }
            } else if (timeRange == "按天" && label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3 && parts.at(2) == timeKey) { originalKey = it.key(); break; }
            } else if (timeRange == "按月" && label == timeKey) {
                originalKey = it.key(); break;
            }
        }
    }

    if (originalKey.isEmpty()) return;
    
    // Build tooltip content based on data type
    QString tipPassFail;
    int total = 0;
    
    if (hasStackedData) {
        // Stacked bar - show AOIResult breakdown
        if (!m_platformAoiResultData.contains(originalKey) || !m_platformAoiResultData[originalKey].contains(platformIdx)) return;
        
        const QMap<QString, int>& aoiMap = m_platformAoiResultData[originalKey][platformIdx];
        
        // Calculate total from AOIResult map
        for (auto ait = aoiMap.constBegin(); ait != aoiMap.constEnd(); ++ait) {
            total += ait.value();
        }
        
        // Build AOIResult breakdown HTML
        QString section1;
        QList<QColor> resultColors;
        resultColors << QColor(0, 255, 136) << QColor(255, 80, 80) << QColor(255, 200, 0)
                    << QColor(0, 150, 255) << QColor(200, 100, 255) << QColor(255, 150, 150)
                    << QColor(150, 200, 255) << QColor(200, 255, 150);
        
        int colorIdx = 0;
        for (auto ait = aoiMap.constBegin(); ait != aoiMap.constEnd(); ++ait) {
            QString colorHex = resultColors[colorIdx % resultColors.size()].name();
            section1 += QString("<div style='font-size:15px'><span style='color:%1'>%2 : %3</span></div>")
                .arg(colorHex).arg(ait.key()).arg(ait.value());
            colorIdx++;
        }
        
        QString tipBody1;
        if (!section1.isEmpty()) {
            tipBody1 = QString("<div style='margin-top:10px;line-height:1.6'>"
                              "<div style='color:#00e5ff;font-size:15px;font-weight:bold;margin-bottom:8px'>AOI Result</div>"
                              "%1"
                              "</div>").arg(section1);
        }
        
        tipPassFail = tipBody1;
    } else {
        // Non-stacked bar - show Pass/Fail
        if (!m_platformTrendData.contains(originalKey) || !m_platformTrendData[originalKey].contains(platformIdx)) return;
        
        int pass = m_platformTrendData[originalKey][platformIdx].first;
        int fail = m_platformTrendData[originalKey][platformIdx].second;
        total = pass + fail;
        
        tipPassFail = QString(
            "<div style='margin-bottom:12px;line-height:1.8'>"
            "<div style='font-size:17px'><span style='color:#4ade80'>Pass : %1</span></div>"
            "<div style='font-size:17px'><span style='color:#f87171'>Fail : %2</span></div>"
            "</div>").arg(pass).arg(fail);
    }

    QString tip = QString(
        "<div style='background:#1e2a3a;color:#fff;padding:20px 24px;border-radius:14px;"
        "border:1px solid #3a4a5a;min-width:320px;font-family:Arial,sans-serif'>"
        "<div style='border-bottom:1px solid #3a4a5a;padding-bottom:12px;margin-bottom:12px'>"
        "<span style='font-size:16px;color:#aaaaaa'>Total : </span>"
        "<span style='font-size:26px;font-weight:bold;color:#ffffff'>%1</span>"
        "</div>"
        "%2"
        "</div>"
    ).arg(total).arg(tipPassFail);

    m_tooltipLabel->setText(tip);

    // Position tooltip near the mouse, but keep it within the screen bounds
    QPoint globalPos = chartView->viewport()->mapToGlobal(viewportPos);
    int x = globalPos.x() + 16;
    int y = globalPos.y() - 20;
    QScreen* screen = QApplication::screenAt(globalPos);
    if (screen) {
        QRect screenGeo = screen->availableGeometry();
        QSize tipSize = m_tooltipLabel->sizeHint();
        if (x + tipSize.width() > screenGeo.right()) x = globalPos.x() - tipSize.width() - 16;
        if (y < screenGeo.top()) y = screenGeo.top() + 4;
        if (y + tipSize.height() > screenGeo.bottom()) y = screenGeo.bottom() - tipSize.height() - 4;
    }
    m_tooltipLabel->move(x, y);
    m_tooltipLabel->show();
}

void Defect_Data_Display::onMinimizeClicked()
{
    showMinimized();
}

void Defect_Data_Display::onCloseClicked()
{
    close();
}

void Defect_Data_Display::onSearchClicked()
{
    QString screenId = ui.searchEdit->text().trimmed();
    m_searchScreenId = screenId;
    qDebug() << "=== onSearchClicked called ===" << "ScreenID:" << screenId;

    if (!screenId.isEmpty()) {
        ui.labelStatus->setText("Searching...");
        ui.labelStatus->setStyleSheet("color: #ffaa00;");
        performQrCodeSearch(screenId);
    }
}

void Defect_Data_Display::performQrCodeSearch(const QString& screenId)
{
    if (screenId.isEmpty())
        return;

    // Update search result tab header
    ui.labelSearchId->setText(QString("搜索ID: %1").arg(screenId));

    // Switch to search result tab
    ui.tabWidget->setCurrentIndex(TAB_SEARCH);

    // Setup search result table style
    ui.searchResultTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #16213e;
            color: #ee2033;
            gridline-color: rgba(0, 217, 255, 60);
            font-size: 13px;
            border: none;
            border-radius: 8px;
            padding: 4px;
        }
        QTableWidget::item {
            padding: 6px 8px;
        }
        QTableWidget::item:selected {
            background-color: rgba(0, 217, 255, 80);
            color: #ff3326;
        }
        QHeaderView::section {
            background-color: rgba(0, 150, 200, 150);
            color: #ffffff;
            font-weight: bold;
            font-size: 13px;
            padding: 6px 8px;
            border: none;
            border-bottom: 1px solid rgba(0, 217, 255, 100);
            border-right: 1px solid rgba(0, 217, 255, 60);
        }
        QTableWidget QTableCornerButton::section {
            background-color: rgba(0, 150, 200, 150);
            border: none;
        }
    )");
    ui.searchResultTable->setAlternatingRowColors(true);
    ui.searchResultTable->verticalHeader()->setVisible(false);
    ui.searchResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui.searchResultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.searchResultTable->horizontalHeader()->setStretchLastSection(true);
    for (int i = 0; i < 5; ++i)
        ui.searchResultTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);

    // Set column headers
    QStringList headers = {"Code_AOI", "Grade_AOI", "Defect_Type", "Station", "Inspect_Time", "Result"};
    for (int i = 0; i < headers.size() && i < ui.searchResultTable->columnCount(); ++i)
        ui.searchResultTable->setHorizontalHeaderItem(i, new QTableWidgetItem(headers[i]));

    // Query database for matching records (no time limit for QR search)
    if (!m_db.isOpen()) {
        ui.labelStatus->setText("Database not connected");
        ui.labelStatus->setStyleSheet("color: #ff4444;");
        return;
    }

    QString queryStr = QString(R"(
        SELECT
            ir.Code_AOI,
            ir.Grade_AOI,
            COUNT(*) as cnt,
            ir.PlatformID,
            ir.StartTime,
            ir.AOIResult
        FROM ivs_lcd_inspectionresult ir
        WHERE ir.ScreenID = '%1'
        GROUP BY ir.Code_AOI, ir.Grade_AOI, ir.PlatformID, ir.AOIResult, DATE(ir.StartTime), HOUR(ir.StartTime)
        ORDER BY ir.StartTime DESC
        LIMIT 500
    )").arg(screenId);

    qDebug() << "Executing QR search query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (!query.exec(queryStr)) {
        qDebug() << "QR search query failed:" << query.lastError().text();
        ui.labelStatus->setText("Query failed");
        ui.labelStatus->setStyleSheet("color: #ff4444;");
        return;
    }

    int row = 0;
    ui.searchResultTable->setRowCount(0);

    while (query.next()) {
        ui.searchResultTable->insertRow(row);

        QString codeAoi = query.value(0).toString();
        QString gradeAoi = query.value(1).toString();
        int cnt = query.value(2).toInt();
        int platformId = query.value(3).toInt();
        QDateTime startTime = query.value(4).toDateTime();
        QString aoiResult = query.value(5).toString();

        ui.searchResultTable->setItem(row, 0, new QTableWidgetItem(codeAoi));
        ui.searchResultTable->setItem(row, 1, new QTableWidgetItem(gradeAoi));
        ui.searchResultTable->setItem(row, 2, new QTableWidgetItem(QString::number(cnt)));
        ui.searchResultTable->setItem(row, 3, new QTableWidgetItem(QString("P%1").arg(platformId)));

        QTableWidgetItem* timeItem = new QTableWidgetItem(startTime.toString("yyyy-MM-dd HH:mm:ss"));
        timeItem->setForeground(QBrush(QColor(100, 180, 220)));
        timeItem->setFont(QFont(ui.searchResultTable->font().family(), 12));
        ui.searchResultTable->setItem(row, 4, timeItem);

        QTableWidgetItem* resultItem = new QTableWidgetItem(aoiResult);
        if (aoiResult == "OK") {
            resultItem->setForeground(QBrush(QColor(0, 255, 136)));
        } else {
            resultItem->setForeground(QBrush(QColor(128, 100, 100)));
        }
        ui.searchResultTable->setItem(row, 5, resultItem);

        ++row;
    }

    ui.searchResultTable->setRowCount(row);
    ui.labelStatus->setText(QString("Found %1 records for %2").arg(row).arg(screenId));
    ui.labelStatus->setStyleSheet(row > 0 ? "color: #00ff88;" : "color: #ffaa00;");

    qDebug() << "QR search completed. Rows:" << row;
}

void Defect_Data_Display::onDateChanged(const QDate& date)
{
    qDebug() << "=== onDateChanged called ===" << date.toString();
    m_selectedDate = date;

    // Invalidate location abnormal cache when date changes
    m_locationAbnormalCache.timestamp = 0;

    // If currently on location abnormal tab (index 6 or 7), reload data immediately
    int currentTabIndex = ui.tabWidget->currentIndex();
    if (currentTabIndex == 6 || currentTabIndex == 7) {
        QString timeRange = ui.comboTimeRange->currentText();
        loadLocationAbnormalDataAsync(timeRange);
    }

    // Always refresh main page data when date changes
    onRefreshClicked();
}

void Defect_Data_Display::onTabChanged(int index)
{
    qDebug() << "=== onTabChanged called with index:" << index << "===";
    QString timeRange = ui.comboTimeRange->currentText();

    qDebug() << "onTabChanged: about to call QTimer::singleShot for index" << index;
    QTimer::singleShot(50, this, [this, index, timeRange]() {
        qDebug() << "=== Timer fired, processing index:" << index << "===";
        qDebug() << "Timer lambda: starting switch";
        switch (index) {
        case 0:
        case 1:
        case 2:
            qDebug() << "Index 0-2: doing nothing";
            break;
        case 3: {
            qDebug() << "Index 3: Defect Mapping";
            CachedTabData* cache = &m_defectMappingCache;
            if (isCacheValid(cache, timeRange, m_selectedDate)) {
                updateDefectMappingChart(cache->positions, cache->types);
            } else {
                loadDefectMappingAsync(timeRange);
            }
            break;
        }
        case 4: {
            qDebug() << "Index 4: Trend";
            CachedTabData* cache = &m_trendCache;
            if (isCacheValid(cache, timeRange, m_selectedDate)) {
                updateTrendChart(cache->trendData, cache->defectRates);
            } else {
                loadTrendDataAsync(timeRange);
            }
            break;
        }
        case 5: {
            qDebug() << "Index 5: Detail";
            CachedTabData* cache = &m_detailCache;
            if (isCacheValid(cache, timeRange, m_selectedDate)) {
                qDebug() << "Using cache, calling updateDetailTable from tab changed";
                updateDetailTable(cache->defectDetails);
                qDebug() << "updateDetailTable from cache returned, about to exit timer lambda";
            } else {
                loadDetailDataAsync(timeRange);
            }
            break;
        }
        case 6: {  // TAB_LOCATION_ABNORMAL (some configurations use 6 instead of 7)
            qDebug() << "=== Index 6: Location Abnormal - LOADING DATA ===";
            CachedTabData* cache = &m_locationAbnormalCache;
            if (isCacheValid(cache, timeRange, m_selectedDate)) {
                qDebug() << "Using cache, calling updateLocationAbnormalChart";
                updateLocationAbnormalChart(m_locationAbnormalData);
            } else {
                qDebug() << "Cache invalid, calling loadLocationAbnormalDataAsync";
                loadLocationAbnormalDataAsync(timeRange);
            }
            break;
        }
        case 7: {  // TAB_LOCATION_ABNORMAL
            qDebug() << "=== Index 7: Location Abnormal - LOADING DATA ===";
            CachedTabData* cache = &m_locationAbnormalCache;
            if (isCacheValid(cache, timeRange, m_selectedDate)) {
                qDebug() << "Using cache, calling updateLocationAbnormalChart";
                updateLocationAbnormalChart(m_locationAbnormalData);
            } else {
                qDebug() << "Cache invalid, calling loadLocationAbnormalDataAsync";
                loadLocationAbnormalDataAsync(timeRange);
            }
            break;
        }
        default:
            qDebug() << "Unknown index:" << index;
            break;
        }
        qDebug() << "Timer lambda: about to exit switch";
    });
    qDebug() << "Timer::singleShot call completed for onTabChanged";
}

void Defect_Data_Display::updateDateTime()
{
    ui.dateTimeLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  HH:mm:ss"));
}

void Defect_Data_Display::setupCharts()
{
    // Create 4 separate charts for each platform
    QStringList platformNames = {"工位一", "工位二", "工位三", "工位四"};
    QList<QColor> platformColors = {
        QColor(0, 255, 136),    // P0 - Green
        QColor(255, 200, 0),    // P1 - Yellow
        QColor(0, 150, 255),    // P2 - Blue
        QColor(255, 100, 100)   // P3 - Red
    };

    // Create P0 chart
    QChart* chart0 = new QChart();
    chart0->setTitle(platformNames[0] + " Trend");
    chart0->setAnimationOptions(QChart::NoAnimation);
    chart0->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart0->setTitleBrush(QBrush(platformColors[0]));
    chart0->legend()->setLabelColor(QColor(234, 234, 234));
    chart0->legend()->setAlignment(Qt::AlignTop);
    chart0->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart0->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewPlatform0 = new QChartView(chart0);
    ((QChartView*)m_chartViewPlatform0)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewPlatform0)->setMinimumHeight(600);
    ((QChartView*)m_chartViewPlatform0)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewPlatform0)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layout0 = new QVBoxLayout(ui.chartPlatform0);
    layout0->setSpacing(0);
    layout0->setContentsMargins(0, 0, 0, 0);
    layout0->addWidget((QChartView*)m_chartViewPlatform0);

    // Create P1 chart
    QChart* chart1 = new QChart();
    chart1->setTitle(platformNames[1] + " Trend");
    chart1->setAnimationOptions(QChart::NoAnimation);
    chart1->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart1->setTitleBrush(QBrush(platformColors[1]));
    chart1->legend()->setLabelColor(QColor(234, 234, 234));
    chart1->legend()->setAlignment(Qt::AlignTop);
    chart1->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart1->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewPlatform1 = new QChartView(chart1);
    ((QChartView*)m_chartViewPlatform1)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewPlatform1)->setMinimumHeight(600);
    ((QChartView*)m_chartViewPlatform1)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewPlatform1)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layout1 = new QVBoxLayout(ui.chartPlatform1);
    layout1->setSpacing(0);
    layout1->setContentsMargins(0, 0, 0, 0);
    layout1->addWidget((QChartView*)m_chartViewPlatform1);

    // Create P2 chart
    QChart* chart2 = new QChart();
    chart2->setTitle(platformNames[2] + " Trend");
    chart2->setAnimationOptions(QChart::NoAnimation);
    chart2->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart2->setTitleBrush(QBrush(platformColors[2]));
    chart2->legend()->setLabelColor(QColor(234, 234, 234));
    chart2->legend()->setAlignment(Qt::AlignTop);
    chart2->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart2->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewPlatform2 = new QChartView(chart2);
    ((QChartView*)m_chartViewPlatform2)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewPlatform2)->setMinimumHeight(600);
    ((QChartView*)m_chartViewPlatform2)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewPlatform2)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layout2 = new QVBoxLayout(ui.chartPlatform2);
    layout2->setSpacing(0);
    layout2->setContentsMargins(0, 0, 0, 0);
    layout2->addWidget((QChartView*)m_chartViewPlatform2);

    // Create P3 chart
    QChart* chart3 = new QChart();
    chart3->setTitle(platformNames[3] + " Trend");
    chart3->setAnimationOptions(QChart::NoAnimation);
    chart3->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart3->setTitleBrush(QBrush(platformColors[3]));
    chart3->legend()->setLabelColor(QColor(234, 234, 234));
    chart3->legend()->setAlignment(Qt::AlignTop);
    chart3->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart3->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewPlatform3 = new QChartView(chart3);
    ((QChartView*)m_chartViewPlatform3)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewPlatform3)->setMinimumHeight(600);
    ((QChartView*)m_chartViewPlatform3)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewPlatform3)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layout3 = new QVBoxLayout(ui.chartPlatform3);
    layout3->setSpacing(0);
    layout3->setContentsMargins(0, 0, 0, 0);
    layout3->addWidget((QChartView*)m_chartViewPlatform3);

    QChart* chartAoi = new QChart();
    chartAoi->setTitle("AOI Defect Analysis");
    chartAoi->setAnimationOptions(QChart::NoAnimation);
    chartAoi->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartAoi->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartAoi->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewAoi = new QChartView(chartAoi);
    ((QChartView*)m_chartViewAoi)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewAoi)->setMinimumHeight(280);
    ((QChartView*)m_chartViewAoi)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutAoi = new QVBoxLayout(ui.chartAoiDefect);
    layoutAoi->setContentsMargins(0, 0, 0, 0);
    layoutAoi->addWidget((QChartView*)m_chartViewAoi);

    // Create Inspection Pass chart
    QChart* chartPass = new QChart();
    chartPass->setTitle("Pass Count Trend");
    chartPass->setAnimationOptions(QChart::NoAnimation);
    chartPass->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartPass->setTitleBrush(QBrush(QColor(0, 255, 136)));
    chartPass->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartPass->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewInspectionPass = new QChartView(chartPass);
    ((QChartView*)m_chartViewInspectionPass)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewInspectionPass)->setMinimumHeight(250);
    ((QChartView*)m_chartViewInspectionPass)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewInspectionPass)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layoutPass = new QVBoxLayout(ui.chartInspectionPass);
    layoutPass->setSpacing(0);
    layoutPass->setContentsMargins(0, 0, 0, 0);
    layoutPass->addWidget((QChartView*)m_chartViewInspectionPass);

    // Create Inspection Fail chart
    QChart* chartFail = new QChart();
    chartFail->setTitle("Fail Count Trend");
    chartFail->setAnimationOptions(QChart::NoAnimation);
    chartFail->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartFail->setTitleBrush(QBrush(QColor(255, 68, 68)));
    chartFail->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartFail->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewInspectionFail = new QChartView(chartFail);
    ((QChartView*)m_chartViewInspectionFail)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewInspectionFail)->setMinimumHeight(250);
    ((QChartView*)m_chartViewInspectionFail)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewInspectionFail)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layoutFail = new QVBoxLayout(ui.chartInspectionFail);
    layoutFail->setSpacing(0);
    layoutFail->setContentsMargins(0, 0, 0, 0);
    layoutFail->addWidget((QChartView*)m_chartViewInspectionFail);

    QChart* chartMapping = new QChart();
    chartMapping->setTitle("Defect Position Map");
    chartMapping->setAnimationOptions(QChart::NoAnimation);
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

    QChart* chartTrend = new QChart();
    chartTrend->setTitle("Defect Count Trend");
    chartTrend->setAnimationOptions(QChart::NoAnimation);
    chartTrend->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartTrend->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartTrend->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewTrend = new QChartView(chartTrend);
    ((QChartView*)m_chartViewTrend)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewTrend)->setMinimumHeight(280);
    ((QChartView*)m_chartViewTrend)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutTrend = new QVBoxLayout(ui.chartTrend);
    layoutTrend->setContentsMargins(0, 0, 0, 0);
    layoutTrend->addWidget((QChartView*)m_chartViewTrend);

    QChart* chartDefectRate = new QChart();
    chartDefectRate->setTitle("Defect Rate Trend");
    chartDefectRate->setAnimationOptions(QChart::NoAnimation);
    chartDefectRate->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartDefectRate->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartDefectRate->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewDefectRate = new QChartView(chartDefectRate);
    ((QChartView*)m_chartViewDefectRate)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewDefectRate)->setMinimumHeight(280);
    ((QChartView*)m_chartViewDefectRate)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutDefectRate = new QVBoxLayout(ui.chartDefectRate);
    layoutDefectRate->setContentsMargins(0, 0, 0, 0);
    layoutDefectRate->addWidget((QChartView*)m_chartViewDefectRate);

    // Create custom floating tooltip label for platform charts
    m_tooltipLabel = new QLabel(this);
    m_tooltipLabel->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    m_tooltipLabel->setAlignment(Qt::AlignLeft);
    m_tooltipLabel->setTextFormat(Qt::RichText);
    m_tooltipLabel->setMargin(0);
    m_tooltipLabel->setIndent(0);
    m_tooltipLabel->setStyleSheet(R"(
        background: rgba(20, 30, 50, 255);
        border: 2px solid rgba(0, 200, 255, 200);
        border-radius: 12px;
        padding: 0px;
    )");
    m_tooltipLabel->hide();
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

void Defect_Data_Display::onRefreshClicked()
{
    qDebug() << "=== onRefreshClicked called ===";
    qDebug() << "m_isLoading:" << m_isLoading;

    // Clear all charts to show empty state
    clearAllCharts();

    // Disable refresh button and time controls while loading
    ui.btnRefresh->setEnabled(false);
    ui.comboTimeRange->setEnabled(false);
    ui.dateEdit->setEnabled(false);

    // Cancel any existing loading
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(200);
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
        }
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    // Reset loading state to allow new load
    m_isLoading = false;

    QString timeRange = ui.comboTimeRange->currentText();
    qDebug() << "Time range:" << timeRange;
    qDebug() << "Selected date:" << m_selectedDate;

    ui.labelStatus->setText("Loading...");
    ui.labelStatus->setStyleSheet("color: #ffaa00;");

    m_isLoading = true;
    ui.btnRefresh->setEnabled(false);
    ++m_currentLoadId;
    int thisLoadId = m_currentLoadId;

    qDebug() << "Creating new worker thread with loadId:" << thisLoadId;
    m_workerThread = new DataLoaderThread(thisLoadId, timeRange, getDateTimeRange(timeRange), m_searchScreenId, this);

    connect(m_workerThread, &DataLoaderThread::aoiDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_Aoi, Qt::QueuedConnection);
    connect(m_workerThread, &DataLoaderThread::inspectionDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_Inspection, Qt::QueuedConnection);
    connect(m_workerThread, &DataLoaderThread::platformDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_Platform, Qt::QueuedConnection);
    connect(m_workerThread, &DataLoaderThread::finished,
            this, [this](int loadId) { onLoadFinished(loadId); }, Qt::QueuedConnection);

    // Connect new trend data signals
    connect(m_workerThread, &DataLoaderThread::platformTrendLoaded,
            this, &Defect_Data_Display::onDataLoaded_PlatformTrend, Qt::QueuedConnection);
    connect(m_workerThread, &DataLoaderThread::platformAoiResultLoaded,
            this, &Defect_Data_Display::onDataLoaded_PlatformAoiResult, Qt::QueuedConnection);
    connect(m_workerThread, &DataLoaderThread::defectTrendLoaded,
            this, &Defect_Data_Display::onDataLoaded_DefectTrend, Qt::QueuedConnection);
    connect(m_workerThread, &DataLoaderThread::inspectionTrendLoaded,
            this, &Defect_Data_Display::onDataLoaded_InspectionTrend, Qt::QueuedConnection);

    qDebug() << "Starting worker thread...";
    m_workerThread->start();
    qDebug() << "Worker thread started";

    // Also load Tab 4 trend data
    loadTrendDataAsync(timeRange);

    // Also load detail data for the detail tab
    loadDetailDataAsync(timeRange);
}

void Defect_Data_Display::clearAllCharts()
{
    // Clear platform charts
    void* platformCharts[] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
    for (int i = 0; i < 4; ++i) {
        QChart* chart = ((QChartView*)platformCharts[i])->chart();
        chart->removeAllSeries();
        for (auto axis : chart->axes()) {
            chart->removeAxis(axis);
        }
    }

    // Clear AOI chart
    QChart* aoiChart = ((QChartView*)m_chartViewAoi)->chart();
    aoiChart->removeAllSeries();
    for (auto axis : aoiChart->axes()) {
        aoiChart->removeAxis(axis);
    }

    // Clear inspection charts
    QChart* passChart = ((QChartView*)m_chartViewInspectionPass)->chart();
    passChart->removeAllSeries();
    for (auto axis : passChart->axes()) {
        passChart->removeAxis(axis);
    }

    QChart* failChart = ((QChartView*)m_chartViewInspectionFail)->chart();
    failChart->removeAllSeries();
    for (auto axis : failChart->axes()) {
        failChart->removeAxis(axis);
    }

    // Clear defect mapping chart
    QChart* mappingChart = ((QChartView*)m_chartViewDefectMapping)->chart();
    mappingChart->removeAllSeries();
    for (auto axis : mappingChart->axes()) {
        mappingChart->removeAxis(axis);
    }

    // Clear trend chart
    QChart* trendChart = ((QChartView*)m_chartViewTrend)->chart();
    trendChart->removeAllSeries();
    for (auto axis : trendChart->axes()) {
        trendChart->removeAxis(axis);
    }
}

void Defect_Data_Display::onTimeRangeChanged(int index)
{
    Q_UNUSED(index);

    // Invalidate location abnormal cache when time range changes
    m_locationAbnormalCache.timestamp = 0;

    // If currently on location abnormal tab (index 6 or 7), reload data immediately
    int currentTabIndex = ui.tabWidget->currentIndex();
    if (currentTabIndex == 6 || currentTabIndex == 7) {
        QString timeRange = ui.comboTimeRange->currentText();
        loadLocationAbnormalDataAsync(timeRange);
    }

    // Always refresh main page data when time range changes
    onRefreshClicked();
}

void Defect_Data_Display::onLoadFinished(int loadId)
{
    qDebug() << "=== onLoadFinished called ===" << "loadId:" << loadId << "currentLoadId:" << m_currentLoadId;
    if (loadId != m_currentLoadId) {
        qDebug() << "Stale load, ignoring";
    }
    m_isLoading = false;
    ui.btnRefresh->setEnabled(true);
    ui.comboTimeRange->setEnabled(true);
    ui.dateEdit->setEnabled(true);
    ui.labelStatus->setText("Updated " + QDateTime::currentDateTime().toString("HH:mm:ss"));
    ui.labelStatus->setStyleSheet("color: #00ff88;");
}

void Defect_Data_Display::onDataLoaded_Aoi(const QMap<QString, QList<QPair<QString, int>>>& defectByType, int totalDefects)
{
    qDebug() << "=== onDataLoaded_Aoi called ===" << "defect types:" << defectByType.size() << "total:" << totalDefects;
    // Only update statistics, not chart (chart updated by trend data)
    ui.statValue5->setText(QString::number(totalDefects));
}

void Defect_Data_Display::onDataLoaded_Inspection(const QMap<QString, int>& passByPeriod,
                                                      const QMap<QString, int>& failByPeriod,
                                                      int totalInspect, int passCount,
                                                      int failCount, double passRate)
{
    qDebug() << "=== onDataLoaded_Inspection called ===" << "total:" << totalInspect << "pass:" << passCount << "fail:" << failCount;
    // Only update statistics, not chart (chart updated by trend data)
    updateStats(totalInspect, passCount, failCount, passRate, 0);
}

void Defect_Data_Display::onDataLoaded_Platform(const QMap<int, QPair<int, int>>& platformStats)
{
    qDebug() << "=== onDataLoaded_Platform called ===" << "platforms:" << platformStats.size();
    // Only store data, don't update chart (chart updated by trend data)
    Q_UNUSED(platformStats);
}

void Defect_Data_Display::onDataLoaded_DefectMapping(const QList<QPair<int, int>>& positions, const QStringList& types)
{
    qDebug() << "=== onDataLoaded_DefectMapping called ===" << "positions:" << positions.size();

    m_defectMappingCache.positions = positions;
    m_defectMappingCache.types = types;
    m_defectMappingCache.timeRange = ui.comboTimeRange->currentText();
    m_defectMappingCache.date = m_selectedDate;
    m_defectMappingCache.timestamp = QDateTime::currentMSecsSinceEpoch();

    updateDefectMappingChart(positions, types);
}

void Defect_Data_Display::onDataLoaded_Trend(const QMap<QString, QPair<int, int>>& trendData, const QMap<QString, double>& defectRates)
{
    qDebug() << "=== onDataLoaded_Trend called ===" << "data points:" << trendData.size();

    m_trendCache.trendData = trendData;
    m_trendCache.defectRates = defectRates;
    m_trendCache.timeRange = ui.comboTimeRange->currentText();
    m_trendCache.date = m_selectedDate;
    m_trendCache.timestamp = QDateTime::currentMSecsSinceEpoch();

    updateTrendChart(trendData, defectRates);
}

void Defect_Data_Display::onDataLoaded_Detail(const QList<QVariantList>& defectDetails)
{
    qDebug() << "=== onDataLoaded_Detail called ===" << "records:" << defectDetails.size();

    m_detailCache.defectDetails = defectDetails;
    m_detailCache.timeRange = ui.comboTimeRange->currentText();
    m_detailCache.date = m_selectedDate;
    m_detailCache.timestamp = QDateTime::currentMSecsSinceEpoch();

    qDebug() << "onDataLoaded_Detail: about to call updateDetailTable";
    updateDetailTable(defectDetails);
    qDebug() << "onDataLoaded_Detail: updateDetailTable returned";
}

void Defect_Data_Display::onDataLoaded_PlatformTrend(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData, const QString& timeRange)
{
    qDebug() << "=== onDataLoaded_PlatformTrend called ===" << "time periods:" << platformTrendData.size();

    m_platformTrendData = platformTrendData;
    m_currentTimeFormat = timeRange;

    updatePlatformTrendChart(platformTrendData);
}

void Defect_Data_Display::onDataLoaded_DefectTrend(const QMap<QString, QMap<QString, int>>& defectTrendData, const QString& timeRange)
{
    qDebug() << "=== onDataLoaded_DefectTrend called ===" << "data points:" << defectTrendData.size();

    m_defectTrendData = defectTrendData;
    m_currentTimeFormat = timeRange;

    updateDefectTrendChart(defectTrendData);
}

void Defect_Data_Display::onDataLoaded_InspectionTrend(const QMap<QString, QPair<int, int>>& inspectionTrendData, const QString& timeRange)
{
    qDebug() << "=== onDataLoaded_InspectionTrend called ===" << "data points:" << inspectionTrendData.size();

    m_inspectionTrendData = inspectionTrendData;
    m_currentTimeFormat = timeRange;

    updateInspectionTrendChart(inspectionTrendData);
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
        return QString("StartTime >= '%1 00:00:00' AND StartTime <= '%1 23:59:59'")
            .arg(m_selectedDate.toString("yyyy-MM-dd"));
    } else if (timeRange == "按天") {
        int year = m_selectedDate.year();
        int month = m_selectedDate.month();
        return QString("StartTime >= '%1-%2-01' AND StartTime < DATE_ADD('%1-%2-01', INTERVAL 1 MONTH)")
            .arg(year).arg(month, 2, 10, QChar('0'));
    } else if (timeRange == "按月") {
        int year = m_selectedDate.year();
        return QString("StartTime >= '%1-01-01' AND StartTime < '%2-01-01'")
            .arg(year).arg(year + 1);
    }
    return QString("StartTime >= '%1 00:00:00' AND StartTime <= '%1 23:59:59'")
        .arg(m_selectedDate.toString("yyyy-MM-dd"));
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

    //ui.statValue5->setText(QString::number(totalDefects));
}

void Defect_Data_Display::updateAoiDefectChart(const QMap<QString, QList<QPair<QString, int>>>& defectByType)
{
    QChart* chart = ((QChartView*)m_chartViewAoi)->chart();
    chart->removeAllSeries();

    for (auto axis : chart->axes()) {
        chart->removeAxis(axis);
    }

    QMap<QString, QColor> defectColors;
    defectColors["BlackDot"] = QColor(100, 100, 100);
    defectColors["BrightDot"] = QColor(255, 180, 0);
    defectColors["Line"] = QColor(0, 150, 255);
    defectColors["Mura"] = QColor(255, 80, 80);
    defectColors["Block"] = QColor(80, 200, 120);
    defectColors["Bubble"] = QColor(180, 100, 255);
    defectColors["Dent"] = QColor(255, 200, 150);
    defectColors["Scratch"] = QColor(100, 255, 255);

    QBarSeries* series = new QBarSeries();
    series->setLabelsVisible(true);
    series->setLabelsFormat("@value");
    series->setLabelsPosition(QBarSeries::LabelsOutsideEnd);

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
    chart->setTitle("AOI Defect Analysis");

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(QStringList() << "");
    axisX->setLabelsColor(QColor(234, 234, 234));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Defect Count");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    int maxDefect = 0;
    for (auto it = defectByType.constBegin(); it != defectByType.constEnd(); ++it) {
        int total = 0;
        for (const auto& pair : it.value()) {
            total += pair.second;
        }
        if (total > maxDefect) maxDefect = total;
    }
    axisY->setRange(0, maxDefect > 0 ? maxDefect + maxDefect * 0.2 : 10);
    chart->addAxis(axisY, Qt::AlignLeft);

    series->attachAxis(axisX);
    series->attachAxis(axisY);
}

void Defect_Data_Display::updateDefectMappingChart(const QList<QPair<int, int>>& defectPositions, const QStringList& defectTypes)
{
    qDebug() << "updateDefectMappingChart called with" << defectPositions.size() << "positions";

    QChart* chart = ((QChartView*)m_chartViewDefectMapping)->chart();
    chart->removeAllSeries();

    for (auto axis : chart->axes()) {
        chart->removeAxis(axis);
    }

    if (defectPositions.isEmpty()) {
        qDebug() << "No positions, returning early";
        return;
    }

    QMap<QString, QColor> defectColors;
    defectColors["BlackDot"] = QColor(100, 100, 100);
    defectColors["BrightDot"] = QColor(255, 180, 0);
    defectColors["Line"] = QColor(0, 150, 255);
    defectColors["Mura"] = QColor(255, 80, 80);
    defectColors["Block"] = QColor(80, 200, 120);
    defectColors["Bubble"] = QColor(180, 100, 255);
    defectColors["Dent"] = QColor(255, 200, 150);
    defectColors["Scratch"] = QColor(100, 255, 255);

    QMap<QString, QScatterSeries*> seriesMap;

    int maxPoints = qMin(defectPositions.size(), 5000);
    for (int i = 0; i < maxPoints; ++i) {
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

    for (auto series : seriesMap.values()) {
        chart->addSeries(series);
    }

    chart->setTitle("Defect Position Map");

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

    QValueAxis* axisX = new QValueAxis();
    axisX->setTitleText("X");
    axisX->setRange(minX - 50, maxX + 50);
    axisX->setLabelsColor(QColor(234, 234, 234));
    axisX->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Y");
    axisY->setRange(minY - 50, maxY + 50);
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->addAxis(axisY, Qt::AlignLeft);

    for (auto series : seriesMap.values()) {
        series->attachAxis(axisX);
        series->attachAxis(axisY);
    }
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

    QString dateRangeClause = getDateTimeRange(timeRange);

    QString queryStr = QString(R"(
        SELECT Pos_x, Pos_y, Type
        FROM ivs_lcd_aoidefect FORCE INDEX (IDX_StartTime)
        WHERE %1
        ORDER BY StartTime DESC
    )").arg(dateRangeClause);

    qDebug() << "Executing Defect Mapping query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

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

    updateDefectMappingChart(positions, types);
}

void Defect_Data_Display::loadTrendData(const QString& timeRange)
{
    if (!m_db.isOpen() || !m_db.isValid()) {
        m_db.close();
        if (!m_db.open() || !m_db.isOpen()) {
            qDebug() << "Database not open";
            return;
        }
    }

    QString dateRangeClause = getDateTimeRange(timeRange);
    QString timeFormat = getTimeFilterClause(timeRange);

    qDebug() << "Executing optimized trend query...";

    QString combinedTrendQuery = QString(R"(
        SELECT
            defect_counts.time_period,
            COALESCE(defect_counts.defect_count, 0) as defect_count,
            COALESCE(total_counts.total_count, 0) as total_count
        FROM (
            SELECT %1 as time_period, COUNT(*) as defect_count
            FROM ivs_lcd_aoidefect FORCE INDEX (IDX_StartTime)
            WHERE %2
            GROUP BY time_period
        ) defect_counts
        LEFT JOIN (
            SELECT %1 as time_period, COUNT(*) as total_count
            FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
            WHERE %2
            GROUP BY time_period
        ) total_counts ON defect_counts.time_period = total_counts.time_period
        ORDER BY defect_counts.time_period
    )").arg(timeFormat).arg(dateRangeClause);

    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (!query.exec(combinedTrendQuery)) {
        qDebug() << "Trend query failed:" << query.lastError().text();
        return;
    }

    QMap<QString, QPair<int, int>> trendData;
    QMap<QString, double> defectRates;

    while (query.next()) {
        QString period = query.value(0).toString();
        int defectCount = query.value(1).toInt();
        int totalCount = query.value(2).toInt();
        double rate = (totalCount > 0) ? (defectCount * 100.0 / totalCount) : 0;

        trendData[period] = qMakePair(defectCount, totalCount);
        defectRates[period] = rate;
    }

    qDebug() << "Trend query completed, periods:" << trendData.size();
    updateTrendChart(trendData, defectRates);
}

void Defect_Data_Display::updateTrendChart(const QMap<QString, QPair<int, int>>& trendData, const QMap<QString, double>& defectRates)
{
    qDebug() << "updateTrendChart called with" << trendData.size() << "data points";

    // Check if chart views are initialized
    if (!m_chartViewTrend || !m_chartViewDefectRate) {
        qDebug() << "Chart views not initialized, returning early";
        return;
    }

    QChart* chartTrend = ((QChartView*)m_chartViewTrend)->chart();
    chartTrend->removeAllSeries();

    for (auto axis : chartTrend->axes()) {
        chartTrend->removeAxis(axis);
    }

    QChart* chartRate = ((QChartView*)m_chartViewDefectRate)->chart();
    chartRate->removeAllSeries();

    for (auto axis : chartRate->axes()) {
        chartRate->removeAxis(axis);
    }

    if (trendData.isEmpty()) {
        qDebug() << "No trend data, returning early";
        return;
    }

    // Determine time format based on current selection
    QString timeRange = ui.comboTimeRange->currentText();

    QLineSeries* defectSeries = new QLineSeries();
    defectSeries->setName("Defect Count");
    defectSeries->setColor(QColor(255, 100, 100));
    defectSeries->setPointLabelsVisible(true);
    defectSeries->setPointLabelsFormat("@yPoint");
    defectSeries->setPointLabelsColor(QColor(255, 200, 100));
    defectSeries->setPointLabelsFont(QFont("Arial", 9, QFont::Bold));

    QStringList categories;
    int index = 0;
    for (auto it = trendData.constBegin(); it != trendData.constEnd(); ++it) {
        QString label = it.key();
        // Format label based on time range
        if (timeRange == "按小时") {
            // Extract hour from "2026-05-22 00:00"
            if (label.contains(" ")) {
                QString timePart = label.split(" ").at(1);
                label = timePart.left(5);  // Get "HH:00"
            }
        } else if (timeRange == "按天") {
            // Show only day "2026-05-22" -> "22"
            if (label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) {
                    label = parts.at(2);  // Get day number
                }
            }
        }
        // 按月: keep as "2026-05"
        categories.append(label);
        defectSeries->append(index++, it.value().first);
    }

    chartTrend->addSeries(defectSeries);
    chartTrend->setTitle("Defect Count Trend");

    QBarCategoryAxis* axisXTrend = new QBarCategoryAxis();
    axisXTrend->append(categories);
    axisXTrend->setLabelsColor(QColor(234, 234, 234));
    chartTrend->addAxis(axisXTrend, Qt::AlignBottom);

    QValueAxis* axisYTrend = new QValueAxis();
    axisYTrend->setTitleText("Defect Count");
    axisYTrend->setLabelFormat("%d");
    axisYTrend->setLabelsColor(QColor(234, 234, 234));
    axisYTrend->setTitleBrush(QBrush(QColor(0, 217, 255)));
    int maxDefect = 1;
    for (auto it = trendData.constBegin(); it != trendData.constEnd(); ++it) {
        if (it.value().first > maxDefect) maxDefect = it.value().first;
    }
    axisYTrend->setRange(0, maxDefect + maxDefect * 0.25);
    chartTrend->addAxis(axisYTrend, Qt::AlignLeft);

    defectSeries->attachAxis(axisXTrend);
    defectSeries->attachAxis(axisYTrend);

    QLineSeries* rateSeries = new QLineSeries();
    rateSeries->setName("Defect Rate (%)");
    rateSeries->setColor(QColor(0, 217, 255));
    rateSeries->setPointLabelsVisible(true);
    rateSeries->setPointLabelsFormat("@yPoint");
    rateSeries->setPointLabelsColor(QColor(100, 220, 255));
    rateSeries->setPointLabelsFont(QFont("Arial", 9, QFont::Bold));

    // Reuse the same categories from defect series for rate chart
    index = 0;
    for (auto it = defectRates.constBegin(); it != defectRates.constEnd(); ++it) {
        rateSeries->append(index++, it.value());
    }

    chartRate->addSeries(rateSeries);
    chartRate->setTitle("Defect Rate Trend");

    // Use the same formatted categories as defect chart
    QBarCategoryAxis* axisXRate = new QBarCategoryAxis();
    axisXRate->append(categories);
    axisXRate->setLabelsColor(QColor(234, 234, 234));
    chartRate->addAxis(axisXRate, Qt::AlignBottom);

    QValueAxis* axisYRate = new QValueAxis();
    axisYRate->setTitleText("Defect Rate (%)");
    axisYRate->setLabelFormat("%.2f");
    axisYRate->setLabelsColor(QColor(234, 234, 234));
    axisYRate->setTitleBrush(QBrush(QColor(0, 217, 255)));
    // Set Y-axis range dynamically based on max defect rate
    double maxRate = 10.0;
    for (auto it = defectRates.constBegin(); it != defectRates.constEnd(); ++it) {
        if (it.value() > maxRate) maxRate = it.value();
    }
    axisYRate->setRange(0, maxRate * 1.3);  // Add 30% padding for labels
    chartRate->addAxis(axisYRate, Qt::AlignLeft);

    rateSeries->attachAxis(axisXRate);
    rateSeries->attachAxis(axisYRate);
}

void Defect_Data_Display::loadDetailData(const QString& timeRange)
{
    if (!m_db.isOpen() || !m_db.isValid()) {
        m_db.close();
        if (!m_db.open() || !m_db.isOpen()) {
            qDebug() << "Database not open";
            return;
        }
    }

    QString dateRangeClause = getDateTimeRange(timeRange);

    QString queryStr = QString(R"(
        SELECT Code_AOI, Grade_AOI, COUNT(*) as cnt
        FROM ivs_lcd_inspectionresult
        WHERE %1
        GROUP BY Code_AOI, Grade_AOI
        ORDER BY Code_AOI, Grade_AOI
    )").arg(dateRangeClause);

    qDebug() << "Executing Detail query:" << queryStr;

    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (!query.exec(queryStr)) {
        qDebug() << "Detail query failed:" << query.lastError().text();
        return;
    }

    QList<QVariantList> defectDetails;

    while (query.next()) {
        QVariantList row;
        row.append(query.value(0).toString());  // Code_AOI
        row.append(query.value(1).toString());  // Grade_AOI
        row.append(query.value(2).toInt());     // count
        defectDetails.append(row);
    }

    updateDetailTable(defectDetails);
}

void Defect_Data_Display::updateDetailTable(const QList<QVariantList>& defectDetails)
{
    qDebug() << "updateDetailTable called with" << defectDetails.size() << "records";

    if (defectDetails.isEmpty()) {
        qDebug() << "No detail data, returning early";
        return;
    }

    qDebug() << "Step 1: Counting defects by type";

    // Count by Code_AOI (row[0]) using actual count from row[2]
    QMap<QString, int> defectCountByType;
    for (const QVariantList& row : defectDetails) {
        QString codeAoi = row[0].toString();
        int cnt = row[2].toInt();
        defectCountByType[codeAoi] = defectCountByType.value(codeAoi, 0) + cnt;
    }

    qDebug() << "Step 2: Defect types count =" << defectCountByType.size();

    if (defectCountByType.isEmpty()) {
        qDebug() << "No defect types found, returning early";
        return;
    }

    qDebug() << "Step 3: Creating pie series";

    QPieSeries* pieSeries = new QPieSeries();
    pieSeries->setLabelsVisible(true);

    // Define a palette of bright and distinguishable colors
    QList<QColor> colorPalette;
    colorPalette << QColor(255, 80, 80)   // Bright Red
                << QColor(255, 220, 0)    // Bright Yellow
                << QColor(0, 200, 255)   // Cyan Blue
                << QColor(200, 100, 255)  // Purple
                << QColor(80, 255, 120)   // Green
                << QColor(255, 140, 200)  // Pink
                << QColor(255, 160, 60)   // Orange
                << QColor(100, 180, 255)  // Light Blue
                << QColor(180, 255, 180)  // Light Green
                << QColor(255, 200, 100)  // Light Orange
                << QColor(150, 150, 255)  // Lavender
                << QColor(255, 150, 150); // Salmon

    int colorIndex = 0;
    for (auto it = defectCountByType.constBegin(); it != defectCountByType.constEnd(); ++it) {
        QString defectType = it.key();
        int count = it.value();
        QPieSlice* slice = pieSeries->append(defectType, count);
        slice->setColor(colorPalette[colorIndex % colorPalette.size()]);
        slice->setLabelBrush(QBrush(QColor(234, 234, 234)));
        slice->setLabelFont(QFont("Arial", 10, QFont::Bold));
        colorIndex++;
    }

    // Force set colors after adding all slices
    int idx = 0;
    for (QPieSlice* slice : pieSeries->slices()) {
        slice->setColor(colorPalette[idx % colorPalette.size()]);
        idx++;
    }

    qDebug() << "Step 4: Creating pie chart";

    QChart* pieChart = new QChart();
    pieChart->setTitle("缺陷类型分布");
    pieChart->setAnimationOptions(QChart::NoAnimation);
    pieChart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    pieChart->setTitleBrush(QBrush(QColor(0, 217, 255)));
    pieChart->legend()->setLabelColor(QColor(234, 234, 234));
    pieChart->legend()->setFont(QFont("Arial", 9));
    pieChart->addSeries(pieSeries);

    qDebug() << "Step 5: Creating pie chart view";

    QChartView* newPieChartView = new QChartView(pieChart);
    newPieChartView->setRenderHint(QPainter::Antialiasing);
    newPieChartView->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    qDebug() << "Step 6: Adding pie chart to layout";

    // Only create layout if one doesn't exist
    if (ui.chartPieDetail->layout() == nullptr) {
        QVBoxLayout* layoutPie = new QVBoxLayout(ui.chartPieDetail);
        layoutPie->setContentsMargins(0, 0, 0, 0);
    }
    if (ui.chartPieDetail->layout() != nullptr) {
        ui.chartPieDetail->layout()->addWidget(newPieChartView);
    }

    // Schedule old chart view for deletion using deleteLater to avoid Qt internal access issues
    if (m_chartViewPieDetail) {
        ((QWidget*)m_chartViewPieDetail)->deleteLater();
    }
    m_chartViewPieDetail = newPieChartView;

    qDebug() << "Step 7: Creating bar series";
    QBarSeries* series = new QBarSeries();
    series->setLabelsVisible(true);
    series->setLabelsFormat("@value");
    series->setLabelsPosition(QBarSeries::LabelsOutsideEnd);
    QBarSet* set = new QBarSet("Count");
    set->setColor(QColor(0, 217, 255));

    QStringList categories;
    for (auto it = defectCountByType.constBegin(); it != defectCountByType.constEnd(); ++it) {
        categories.append(it.key());
        *set << it.value();
    }

    series->append(set);

    qDebug() << "Step 8: Creating bar chart";

    QChart* chart = new QChart();
    chart->setTitle("Code_AOI Distribution");
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->legend()->setLabelColor(QColor(234, 234, 234));
    chart->legend()->hide();

    QChartView* newDetailChartView = new QChartView(chart);
    newDetailChartView->setRenderHint(QPainter::Antialiasing);
    newDetailChartView->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    qDebug() << "Step 9: Adding bar chart to layout";

    // Only create layout if one doesn't exist
    if (ui.chartDetail->layout() == nullptr) {
        QVBoxLayout* layout = new QVBoxLayout(ui.chartDetail);
        layout->setContentsMargins(0, 0, 0, 0);
    }
    if (ui.chartDetail->layout() != nullptr) {
        ui.chartDetail->layout()->addWidget(newDetailChartView);
    }

    // Schedule old chart view for deletion using deleteLater to avoid Qt internal access issues
    if (m_chartViewDetail) {
        ((QWidget*)m_chartViewDetail)->deleteLater();
    }
    m_chartViewDetail = newDetailChartView;

    qDebug() << "Step 10: Adding series to chart";

    chart->addSeries(series);

    qDebug() << "Step 11: Creating axes";

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    axisX->setLabelsColor(QColor(234, 234, 234));
    QFont axisXFont = axisX->labelsFont();
    axisXFont.setPointSize(10);
    axisX->setLabelsFont(axisXFont);
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Count");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    QFont axisYFont = axisY->labelsFont();
    axisYFont.setPointSize(10);
    axisY->setLabelsFont(axisYFont);
    // Set minimum to 0 and add top padding for labels
    int maxVal = 1;
    for (auto it = defectCountByType.constBegin(); it != defectCountByType.constEnd(); ++it) {
        if (it.value() > maxVal) maxVal = it.value();
    }
    axisY->setRange(0, maxVal + maxVal * 0.2);  // Add 20% top padding for labels
    chart->addAxis(axisY, Qt::AlignLeft);

    series->attachAxis(axisX);
    series->attachAxis(axisY);

    qDebug() << "Checking chart view validity after attachAxis: pieDetail =" << (m_chartViewPieDetail != nullptr) << "chartDetail =" << (m_chartViewDetail != nullptr);

    qDebug() << "updateDetailTable completed successfully - about to return";
    qDebug() << "Exiting updateDetailTable";
}

void Defect_Data_Display::updatePlatformTrendChart(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData)
{
    qDebug() << "updatePlatformTrendChart called with" << platformTrendData.size() << "time periods";

    if (platformTrendData.isEmpty()) {
        qDebug() << "No platform trend data";
        return;
    }

    QString timeRange = ui.comboTimeRange->currentText();

    // Get all time periods sorted
    QStringList timeCategories;
    for (auto it = platformTrendData.constBegin(); it != platformTrendData.constEnd(); ++it) {
        QString label = it.key();
        // Format label based on time range
        if (timeRange == "按小时") {
            if (label.contains(" ")) {
                QString timePart = label.split(" ").at(1);
                label = timePart.left(5);
            }
        } else if (timeRange == "按天") {
            if (label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) {
                    label = parts.at(2);
                }
            }
        }
        // 按月: keep as is
        timeCategories.append(label);
    }

    // Sort time categories properly
    if (timeRange == "按小时" || timeRange == "按天") {
        std::sort(timeCategories.begin(), timeCategories.end(), [](const QString& a, const QString& b) {
            return a.toInt() < b.toInt();
        });
    }

    // Platform colors
    QList<QColor> platformColors = {
        QColor(0, 255, 136),    // P0 - Green
        QColor(255, 200, 0),    // P1 - Yellow
        QColor(0, 150, 255),    // P2 - Blue
        QColor(255, 100, 100)   // P3 - Red
    };

    QStringList platformNames = {"工位一", "工位二", "工位三", "工位四"};

    // Create chart for each platform
    void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
    for (int p = 0; p < 4; ++p) {
        QChartView* chartView = (QChartView*)chartViewPtrs[p];
        QChart* chart = chartView->chart();
        chart->removeAllSeries();
        for (auto axis : chart->axes()) {
            chart->removeAxis(axis);
        }

        QBarSeries* series = new QBarSeries();
        QBarSet* failSet = new QBarSet("Fail");
        failSet->setColor(platformColors[p]);
        failSet->setLabelColor(QColor(234, 234, 234));

        // Populate data for this platform
        for (const QString& timeKey : timeCategories) {
            // Find original key
            QString originalKey;
            for (auto it = platformTrendData.constBegin(); it != platformTrendData.constEnd(); ++it) {
                QString label = it.key();
                if (timeRange == "按小时" && label.contains(" ")) {
                    QString timePart = label.split(" ").at(1);
                    if (timePart.left(5) == timeKey) {
                        originalKey = it.key();
                        break;
                    }
                } else if (timeRange == "按天" && label.contains("-")) {
                    QStringList parts = label.split("-");
                    if (parts.size() >= 3 && parts.at(2) == timeKey) {
                        originalKey = it.key();
                        break;
                    }
                } else if (timeRange == "按月" && label == timeKey) {
                    originalKey = it.key();
                    break;
                }
            }

            if (!originalKey.isEmpty() && platformTrendData.contains(originalKey)) {
                const QMap<int, QPair<int, int>>& platformData = platformTrendData[originalKey];
                if (platformData.contains(p)) {
                    *failSet << (platformData[p].first + platformData[p].second);
                } else {
                    *failSet << 0;
                }
            } else {
                *failSet << 0;
            }
        }

        series->append(failSet);
        series->setLabelsVisible(true);
        series->setLabelsFormat("@value");
        series->setLabelsPosition(QBarSeries::LabelsOutsideEnd);
        chart->addSeries(series);
        chart->setTitle(platformNames[p] + " (" + timeRange + ") - Total");
        chart->legend()->hide();

        QBarCategoryAxis* axisX = new QBarCategoryAxis();
        axisX->append(timeCategories);
        axisX->setLabelsColor(QColor(234, 234, 234));
        QFont axisXFont = axisX->labelsFont();
        axisXFont.setPointSize(10);
        axisX->setLabelsFont(axisXFont);
        chart->addAxis(axisX, Qt::AlignBottom);

        QValueAxis* axisY = new QValueAxis();
        axisY->setTitleText("Total");
        axisY->setLabelFormat("%d");
        axisY->setLabelsColor(QColor(234, 234, 234));
        QFont axisYFont = axisY->labelsFont();
        axisYFont.setPointSize(10);
        axisY->setLabelsFont(axisYFont);
        axisY->setTitleBrush(QBrush(platformColors[p]));
        int maxVal = 1;
        for (const QString& timeKey : timeCategories) {
            QString originalKey;
            for (auto it = platformTrendData.constBegin(); it != platformTrendData.constEnd(); ++it) {
                QString label = it.key();
                if (timeRange == "按小时" && label.contains(" ")) {
                    QString timePart = label.split(" ").at(1);
                    if (timePart.left(5) == timeKey) {
                        originalKey = it.key();
                        break;
                    }
                } else if (timeRange == "按天" && label.contains("-")) {
                    QStringList parts = label.split("-");
                    if (parts.size() >= 3 && parts.at(2) == timeKey) {
                        originalKey = it.key();
                        break;
                    }
                } else if (timeRange == "按月" && label == timeKey) {
                    originalKey = it.key();
                    break;
                }
            }
            if (!originalKey.isEmpty() && platformTrendData.contains(originalKey)) {
                const QMap<int, QPair<int, int>>& platformData = platformTrendData[originalKey];
                if (platformData.contains(p)) {
                    int val = platformData[p].first + platformData[p].second;
                    if (val > maxVal) maxVal = val;
                }
            }
        }
        axisY->setRange(0, maxVal + maxVal * 0.2);
        chart->addAxis(axisY, Qt::AlignLeft);

        series->attachAxis(axisX);
        series->attachAxis(axisY);
    }
}

void Defect_Data_Display::updatePlatformTrendChartStacked(const QMap<QString, QMap<int, QMap<QString, int>>>& platformAoiResultData, const QStringList& aoiResultCategories)
{
    if (platformAoiResultData.isEmpty() || aoiResultCategories.isEmpty()) {
        if (!m_platformTrendData.isEmpty()) {
            updatePlatformTrendChart(m_platformTrendData);
        }
        return;
    }

    QString timeRange = ui.comboTimeRange->currentText();

    QStringList timeCategories;
    for (auto it = platformAoiResultData.constBegin(); it != platformAoiResultData.constEnd(); ++it) {
        QString label = it.key();
        if (timeRange == "按小时" && label.contains(" ")) {
            label = label.split(" ").at(1).left(5);
        } else if (timeRange == "按天" && label.contains("-")) {
            QStringList parts = label.split("-");
            if (parts.size() >= 3) label = parts.at(2);
        }
        if (!timeCategories.contains(label)) timeCategories.append(label);
    }
    if (timeRange == "按小时" || timeRange == "按天") {
        std::sort(timeCategories.begin(), timeCategories.end(), [](const QString& a, const QString& b) {
            return a.toInt() < b.toInt();
        });
    }

    QList<QColor> platformColors = {
        QColor(0, 255, 136), QColor(255, 200, 0), QColor(0, 150, 255), QColor(255, 100, 100)
    };
    QList<QColor> resultColors;
    resultColors << QColor(0, 255, 136) << QColor(255, 80, 80) << QColor(255, 200, 0)
                << QColor(0, 150, 255) << QColor(200, 100, 255) << QColor(255, 150, 150)
                << QColor(150, 200, 255) << QColor(200, 255, 150);
    QStringList platformNames = {"工位一", "工位二", "工位三", "工位四"};

    void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};

    for (int p = 0; p < 4; ++p) {
        QChartView* chartView = (QChartView*)chartViewPtrs[p];
        QChart* chart = chartView->chart();
        // Clear old text items from scene
        if (chart->scene()) {
            QList<QGraphicsItem*> items = chart->scene()->items();
            for (QGraphicsItem* item : items) {
                if (qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item)) {
                    chart->scene()->removeItem(item);
                    delete item;
                }
            }
        }

        chart->removeAllSeries();
        for (auto axis : chart->axes()) chart->removeAxis(axis);

        QStackedBarSeries* stackedSeries = new QStackedBarSeries();
        stackedSeries->setLabelsVisible(false);

        QList<QBarSet*> barSets;
        for (int i = 0; i < aoiResultCategories.size() && i < resultColors.size(); ++i) {
            QBarSet* set = new QBarSet(aoiResultCategories[i]);
            set->setColor(resultColors[i]);
            set->setLabelColor(QColor(234, 234, 234));
            barSets.append(set);
        }

        QList<int> columnTotals(timeCategories.size(), 0);

        for (int ti = 0; ti < timeCategories.size(); ++ti) {
            const QString& timeKey = timeCategories[ti];
            QString originalKey;
            for (auto it2 = platformAoiResultData.constBegin(); it2 != platformAoiResultData.constEnd(); ++it2) {
                QString label = it2.key();
                if (timeRange == "按小时" && label.contains(" ")) {
                    if (label.split(" ").at(1).left(5) == timeKey) { originalKey = it2.key(); break; }
                } else if (timeRange == "按天" && label.contains("-")) {
                    QStringList parts = label.split("-");
                    if (parts.size() >= 3 && parts.at(2) == timeKey) { originalKey = it2.key(); break; }
                } else if (timeRange == "按月" && label == timeKey) {
                    originalKey = it2.key(); break;
                }
            }

            for (int i = 0; i < aoiResultCategories.size() && i < barSets.size(); ++i) {
                int val = 0;
                if (!originalKey.isEmpty() && platformAoiResultData.contains(originalKey)) {
                    const QMap<QString, int>& resMap = platformAoiResultData[originalKey].value(p);
                    val = resMap.value(aoiResultCategories[i], 0);
                }
                *barSets[i] << val;
                columnTotals[ti] += val;
            }
        }

        for (QBarSet* bs : barSets) stackedSeries->append(bs);
        chart->addSeries(stackedSeries);
        chart->setTitle(platformNames[p] + " (" + timeRange + ") - Total");
        chart->legend()->hide();

        QBarCategoryAxis* axisX = new QBarCategoryAxis();
        axisX->append(timeCategories);
        axisX->setLabelsColor(QColor(234, 234, 234));
        QFont axisXFont = axisX->labelsFont();
        axisXFont.setPointSize(10);
        axisX->setLabelsFont(axisXFont);
        chart->addAxis(axisX, Qt::AlignBottom);

        int maxVal = 1;
        for (int v : columnTotals) if (v > maxVal) maxVal = v;

        QValueAxis* axisY = new QValueAxis();
        axisY->setTitleText("Total");
        axisY->setLabelFormat("%d");
        axisY->setLabelsColor(QColor(234, 234, 234));
        QFont axisYFont = axisY->labelsFont();
        axisYFont.setPointSize(10);
        axisY->setLabelsFont(axisYFont);
        axisY->setTitleBrush(QBrush(platformColors[p]));
        axisY->setRange(0, maxVal + maxVal * 0.2);
        chart->addAxis(axisY, Qt::AlignLeft);

        stackedSeries->attachAxis(axisX);
        stackedSeries->attachAxis(axisY);

        // Add total value text above each bar
        if (!chart->scene()) continue;
        for (int ti = 0; ti < timeCategories.size(); ++ti) {
            if (columnTotals[ti] <= 0) continue;
            int chartWidth = chartView->viewport()->width();
            if (chartWidth <= 0) chartWidth = 400;
            int categoryWidth = chartWidth / qMax(timeCategories.size(), 1);
            qreal barCenterX = (ti + 0.5) * categoryWidth;
            qreal barTopY = columnTotals[ti];

            QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(QString::number(columnTotals[ti]));
            textItem->setFont(QFont("Arial", 9, QFont::Bold));
            textItem->setBrush(QBrush(QColor(255, 255, 255)));
            textItem->setZValue(100);
            chart->scene()->addItem(textItem);
            QPointF scenePoint = chart->mapToPosition(QPointF(barCenterX, barTopY));
            textItem->setPos(scenePoint.x() - textItem->boundingRect().width() / 2, scenePoint.y() - 18);
        }
    }
}


void Defect_Data_Display::onDataLoaded_PlatformAoiResult(const QMap<QString, QMap<int, QMap<QString, int>>>& platformAoiResultData, const QStringList& aoiResultCategories, const QString& timeRange)
{
    // Save the data to member variable for tooltip usage
    m_platformAoiResultData = platformAoiResultData;
    m_aoiResultCategories = aoiResultCategories;
    qDebug() << "[PlatformAoiResult] Saved" << m_platformAoiResultData.size() << "time periods";

    // Use stacked bar chart to show different AOIResult types
    if (!platformAoiResultData.isEmpty() && !aoiResultCategories.isEmpty()) {
        updatePlatformTrendChartStacked(platformAoiResultData, aoiResultCategories);
    } else if (!m_platformTrendData.isEmpty()) {
        // Fallback to non-stacked chart if no AOIResult detail
        updatePlatformTrendChart(m_platformTrendData);
    }
}

void Defect_Data_Display::updateDefectTrendChart(const QMap<QString, QMap<QString, int>>& defectTrendData)
{
    qDebug() << "updateDefectTrendChart called with" << defectTrendData.size() << "time periods";

    if (!m_chartViewAoi) {
        qDebug() << "m_chartViewAoi not initialized, returning early";
        return;
    }

    QChart* chart = ((QChartView*)m_chartViewAoi)->chart();
    chart->removeAllSeries();

    for (auto axis : chart->axes()) {
        chart->removeAxis(axis);
    }

    if (defectTrendData.isEmpty()) {
        qDebug() << "No defect trend data";
        return;
    }

    // Get all unique defect types
    QSet<QString> allDefectTypes;
    for (auto it = defectTrendData.constBegin(); it != defectTrendData.constEnd(); ++it) {
        for (auto typeIt = it.value().constBegin(); typeIt != it.value().constEnd(); ++typeIt) {
            allDefectTypes.insert(typeIt.key());
        }
    }

    QString timeRange = ui.comboTimeRange->currentText();
    QStringList timeCategories;
    QMap<QString, QColor> defectColors;
    defectColors["BlackDot"] = QColor(100, 100, 100);
    defectColors["BrightDot"] = QColor(255, 180, 0);
    defectColors["Line"] = QColor(0, 150, 255);
    defectColors["Mura"] = QColor(255, 80, 80);
    defectColors["Block"] = QColor(80, 200, 120);
    defectColors["Bubble"] = QColor(180, 100, 255);
    defectColors["Dent"] = QColor(255, 200, 150);
    defectColors["Scratch"] = QColor(100, 255, 255);

    // Create a line series for each defect type
    QMap<QString, QLineSeries*> seriesMap;
    for (const QString& defectType : allDefectTypes) {
        QLineSeries* series = new QLineSeries();
        series->setName(defectType);
        series->setColor(defectColors.value(defectType, Qt::gray));
        series->setPointLabelsVisible(true);
        series->setPointLabelsFormat("@yPoint");
        series->setPointLabelsColor(QColor(255, 255, 255));
        series->setPointLabelsFont(QFont("Arial", 8, QFont::Bold));
        seriesMap[defectType] = series;
    }

    // Populate data
    int index = 0;
    for (auto it = defectTrendData.constBegin(); it != defectTrendData.constEnd(); ++it) {
        QString label = it.key();
        // Format label
        if (timeRange == "按小时") {
            if (label.contains(" ")) {
                QString timePart = label.split(" ").at(1);
                label = timePart.left(5);
            }
        } else if (timeRange == "按天") {
            if (label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) {
                    label = parts.at(2);
                }
            }
        }
        timeCategories.append(label);

        for (const QString& defectType : allDefectTypes) {
            int count = it.value().value(defectType, 0);
            seriesMap[defectType]->append(index, count);
        }
        index++;
    }

    // Add series to chart
    for (auto series : seriesMap.values()) {
        chart->addSeries(series);
    }
    chart->setTitle("Defect Analysis Trend (" + timeRange + ")");

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(timeCategories);
    axisX->setLabelsColor(QColor(234, 234, 234));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Defect Count");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    int maxCount = 1;
    for (auto it = defectTrendData.constBegin(); it != defectTrendData.constEnd(); ++it) {
        for (auto typeIt = it.value().constBegin(); typeIt != it.value().constEnd(); ++typeIt) {
            if (typeIt.value() > maxCount) maxCount = typeIt.value();
        }
    }
    axisY->setRange(0, maxCount + maxCount * 0.25);
    chart->addAxis(axisY, Qt::AlignLeft);

    for (auto series : seriesMap.values()) {
        series->attachAxis(axisX);
        series->attachAxis(axisY);
    }
}

void Defect_Data_Display::updateInspectionTrendChart(const QMap<QString, QPair<int, int>>& inspectionTrendData)
{
    qDebug() << "updateInspectionTrendChart called with" << inspectionTrendData.size() << "data points";

    if (inspectionTrendData.isEmpty()) {
        qDebug() << "No inspection trend data";
        return;
    }

    QString timeRange = ui.comboTimeRange->currentText();
    QStringList timeCategories;

    // Pass chart
    QChart* passChart = ((QChartView*)m_chartViewInspectionPass)->chart();
    passChart->removeAllSeries();
    for (auto axis : passChart->axes()) {
        passChart->removeAxis(axis);
    }

    // Fail chart
    QChart* failChart = ((QChartView*)m_chartViewInspectionFail)->chart();
    failChart->removeAllSeries();
    for (auto axis : failChart->axes()) {
        failChart->removeAxis(axis);
    }

    // Create Pass series
    QBarSeries* passBarSeries = new QBarSeries();
    QBarSet* passSet = new QBarSet("Pass");
    passSet->setColor(QColor(0, 255, 136));
    passSet->setLabelColor(QColor(234, 234, 234));

    // Create Fail series
    QBarSeries* failBarSeries = new QBarSeries();
    QBarSet* failSet = new QBarSet("Fail");
    failSet->setColor(QColor(255, 68, 68));
    failSet->setLabelColor(QColor(234, 234, 234));

    int index = 0;
    for (auto it = inspectionTrendData.constBegin(); it != inspectionTrendData.constEnd(); ++it) {
        QString label = it.key();
        // Format label
        if (timeRange == "按小时") {
            if (label.contains(" ")) {
                QString timePart = label.split(" ").at(1);
                label = timePart.left(5);
            }
        } else if (timeRange == "按天") {
            if (label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) {
                    label = parts.at(2);
                }
            }
        }
        // 按月: keep as is
        timeCategories.append(label);

        *passSet << it.value().first;
        *failSet << it.value().second;
        index++;
    }

    passBarSeries->append(passSet);
    passBarSeries->setLabelsVisible(true);
    passBarSeries->setLabelsFormat("@value");
    passBarSeries->setLabelsPosition(QBarSeries::LabelsOutsideEnd);
    passChart->addSeries(passBarSeries);
    passChart->setTitle("Pass Count (" + timeRange + ")");

    failBarSeries->append(failSet);
    failBarSeries->setLabelsVisible(true);
    failBarSeries->setLabelsFormat("@value");
    failBarSeries->setLabelsPosition(QBarSeries::LabelsOutsideEnd);
    failChart->addSeries(failBarSeries);
    failChart->setTitle("Fail Count (" + timeRange + ")");

    // Pass chart axes
    QBarCategoryAxis* passAxisX = new QBarCategoryAxis();
    passAxisX->append(timeCategories);
    passAxisX->setLabelsColor(QColor(234, 234, 234));
    passChart->addAxis(passAxisX, Qt::AlignBottom);

    QValueAxis* passAxisY = new QValueAxis();
    passAxisY->setTitleText("Count");
    passAxisY->setLabelFormat("%d");
    passAxisY->setLabelsColor(QColor(234, 234, 234));
    passAxisY->setTitleBrush(QBrush(QColor(0, 255, 136)));
    int maxPass = 1;
    for (auto it = inspectionTrendData.constBegin(); it != inspectionTrendData.constEnd(); ++it) {
        if (it.value().first > maxPass) maxPass = it.value().first;
    }
    passAxisY->setRange(0, maxPass + maxPass * 0.2);
    passChart->addAxis(passAxisY, Qt::AlignLeft);

    passBarSeries->attachAxis(passAxisX);
    passBarSeries->attachAxis(passAxisY);

    // Fail chart axes
    QBarCategoryAxis* failAxisX = new QBarCategoryAxis();
    failAxisX->append(timeCategories);
    failAxisX->setLabelsColor(QColor(234, 234, 234));
    failChart->addAxis(failAxisX, Qt::AlignBottom);

    QValueAxis* failAxisY = new QValueAxis();
    failAxisY->setTitleText("Count");
    failAxisY->setLabelFormat("%d");
    failAxisY->setLabelsColor(QColor(234, 234, 234));
    failAxisY->setTitleBrush(QBrush(QColor(255, 68, 68)));
    int maxFail = 1;
    for (auto it = inspectionTrendData.constBegin(); it != inspectionTrendData.constEnd(); ++it) {
        if (it.value().second > maxFail) maxFail = it.value().second;
    }
    failAxisY->setRange(0, maxFail + maxFail * 0.2);
    failChart->addAxis(failAxisY, Qt::AlignLeft);

    failBarSeries->attachAxis(failAxisX);
    failBarSeries->attachAxis(failAxisY);
}

DataLoaderThread::DataLoaderThread(int loadId, const QString& timeRange, const QString& dateRange,
                                   const QString& searchScreenId, QObject* parent)
    : QThread(parent)
    , m_loadId(loadId)
    , m_timeRange(timeRange)
    , m_dateRange(dateRange)
    , m_searchScreenId(searchScreenId)
{
}

void DataLoaderThread::run()
{
    qDebug() << "=== Worker thread started ===";

    QString connectionString = "DRIVER={MySQL ODBC 5.3 ANSI Driver};"
                              "SERVER=localhost;"
                              "PORT=3306;"
                              "DATABASE=ivs_lcd;"
                              "UID=root;"
                              "PWD=123456;"
                              "OPTION=8;";

    qDebug() << "Creating database connection...";
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "worker_connection");
    db.setDatabaseName(connectionString);
    db.setConnectOptions("SQL_ATTR_CONNECTION_TIMEOUT=30000");

    if (!db.open()) {
        qDebug() << "Worker thread: database connection failed:" << db.lastError().text();
        return;
    }

    qDebug() << "Worker thread: database connected";
    qDebug() << "Time range:" << m_timeRange;
    qDebug() << "Date range:" << m_dateRange;

    // Build query condition with optional ScreenID filter
    QString queryCondition = m_dateRange;
    //if (!m_searchScreenId.isEmpty()) {
    //    queryCondition += QString(" AND ScreenID = '%1'").arg(m_searchScreenId);
    //    qDebug() << "ScreenID filter:" << m_searchScreenId;
    //}

    // Get time format based on time range
    QString timeFormat;
    if (m_timeRange == "按小时") {
        timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d %H:00')";
    } else if (m_timeRange == "按天") {
        timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d')";
    } else {
        timeFormat = "DATE_FORMAT(StartTime, '%Y-%m')";
    }

    // Query 1: Platform trend by time period (per platform)
    qDebug() << "Querying platform trend...";
    QString platformTrendQuery = QString(R"(
        SELECT %1 as time_period, PlatformID,
               SUM(IF(AOIResult = 'OK', 1, 0)) as pass_cnt,
               SUM(IF(AOIResult != 'OK', 1, 0)) as fail_cnt
        FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
        WHERE %2
        GROUP BY time_period, PlatformID
        ORDER BY time_period, PlatformID
    )").arg(timeFormat).arg(queryCondition);

    QSqlQuery platformTrendQ(db);
    platformTrendQ.setForwardOnly(true);
    QMap<QString, QMap<int, QPair<int, int>>> platformTrendData;

    if (platformTrendQ.exec(platformTrendQuery)) {
        while (platformTrendQ.next()) {
            QString period = platformTrendQ.value(0).toString();
            int platformId = platformTrendQ.value(1).toInt();
            int pass = platformTrendQ.value(2).toInt();
            int fail = platformTrendQ.value(3).toInt();
            // Store data per platform
            platformTrendData[period][platformId] = qMakePair(pass, fail);
        }
    } else {
        qDebug() << "Platform trend query failed:" << platformTrendQ.lastError().text();
    }

    // Query 2: Defect trend by time period
    qDebug() << "Querying defect trend...";
    QString defectTrendQuery = QString(R"(
        SELECT %1 as time_period, Type, COUNT(*) as cnt
        FROM ivs_lcd_aoidefect FORCE INDEX (IDX_StartTime)
        WHERE %2
        GROUP BY time_period, Type
        ORDER BY time_period
    )").arg(timeFormat).arg(queryCondition);

    QSqlQuery defectTrendQ(db);
    defectTrendQ.setForwardOnly(true);
    QMap<QString, QMap<QString, int>> defectTrendData;

    if (defectTrendQ.exec(defectTrendQuery)) {
        while (defectTrendQ.next()) {
            QString period = defectTrendQ.value(0).toString();
            QString defectType = defectTrendQ.value(1).toString();
            int cnt = defectTrendQ.value(2).toInt();
            defectTrendData[period][defectType] = cnt;
        }
    } else {
        qDebug() << "Defect trend query failed:" << defectTrendQ.lastError().text();
    }

    // Query 3: Inspection trend by time period
    qDebug() << "Querying inspection trend...";
    QString inspectionTrendQuery = QString(R"(
        SELECT %1 as time_period,
               SUM(IF(AOIResult = 'OK', 1, 0)) as pass_cnt,
               SUM(IF(AOIResult != 'OK', 1, 0)) as fail_cnt
        FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
        WHERE %2
        GROUP BY time_period
        ORDER BY time_period
    )").arg(timeFormat).arg(queryCondition);

    QSqlQuery inspectionTrendQ(db);
    inspectionTrendQ.setForwardOnly(true);
    QMap<QString, QPair<int, int>> inspectionTrendData;

    if (inspectionTrendQ.exec(inspectionTrendQuery)) {
        while (inspectionTrendQ.next()) {
            QString period = inspectionTrendQ.value(0).toString();
            int pass = inspectionTrendQ.value(1).toInt();
            int fail = inspectionTrendQ.value(2).toInt();
            inspectionTrendData[period] = qMakePair(pass, fail);
        }
    } else {
        qDebug() << "Inspection trend query failed:" << inspectionTrendQ.lastError().text();
    }

    // Query 1b: Platform AOIResult detail by time period (per platform, per AOIResult)
    qDebug() << "Querying platform AOIResult detail...";
    QString platformAoiResultQuery = QString(R"(
        SELECT %1 as time_period, PlatformID, AOIResult, COUNT(*) as cnt
        FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
        WHERE %2
        GROUP BY time_period, PlatformID, AOIResult
        ORDER BY time_period, PlatformID, AOIResult
    )").arg(timeFormat).arg(queryCondition);

    QSqlQuery platformAoiResultQ(db);
    platformAoiResultQ.setForwardOnly(true);
    QMap<QString, QMap<int, QMap<QString, int>>> platformAoiResultData;
    QStringList aoiResultCategories;

    if (platformAoiResultQ.exec(platformAoiResultQuery)) {
        while (platformAoiResultQ.next()) {
            QString period = platformAoiResultQ.value(0).toString();
            int platformId = platformAoiResultQ.value(1).toInt();
            QString aoiResult = platformAoiResultQ.value(2).toString();
            int cnt = platformAoiResultQ.value(3).toInt();
            platformAoiResultData[period][platformId][aoiResult] = cnt;
            if (!aoiResultCategories.contains(aoiResult)) {
                aoiResultCategories.append(aoiResult);
            }
        }
    } else {
        qDebug() << "Platform AOIResult query failed:" << platformAoiResultQ.lastError().text();
    }

    // Emit trend data signals
    emit platformTrendLoaded(platformTrendData, m_timeRange);
    emit platformAoiResultLoaded(platformAoiResultData, aoiResultCategories, m_timeRange);
    emit defectTrendLoaded(defectTrendData, m_timeRange);
    emit inspectionTrendLoaded(inspectionTrendData, m_timeRange);

    // Original combined query for totals
    qDebug() << "Executing combined optimized query for totals...";

    QString combinedQueryStr = QString(R"(
        SELECT 'aoi' as query_type, Type as defect_type, COUNT(*) as cnt, 0 as platform_id, 0 as pass_cnt, 0 as fail_cnt
        FROM ivs_lcd_aoidefect FORCE INDEX (IDX_StartTime)
        WHERE %1
        GROUP BY Type
        UNION ALL
        SELECT 'insp_total', '', COUNT(*), 0, SUM(IF(AOIResult = 'OK', 1, 0)), SUM(IF(AOIResult != 'OK', 1, 0))
        FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
        WHERE %1
        UNION ALL
        SELECT 'insp_platform', '', PlatformID, PlatformID, SUM(IF(AOIResult = 'OK', 1, 0)), SUM(IF(AOIResult != 'OK', 1, 0))
        FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
        WHERE %1
        GROUP BY PlatformID
    )").arg(queryCondition);

    QSqlQuery combinedQuery(db);
    combinedQuery.setForwardOnly(true);
    combinedQuery.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (!combinedQuery.exec(combinedQueryStr)) {
        qDebug() << "Combined query failed:" << combinedQuery.lastError().text();
        db.close();
        QSqlDatabase::removeDatabase("worker_connection");
        emit finished(m_loadId);
        return;
    }

    QMap<QString, QList<QPair<QString, int>>> defectByType;
    QMap<QString, int> passByPeriod;
    QMap<QString, int> failByPeriod;
    QMap<int, QPair<int, int>> platformStats;
    int totalDefects = 0;
    int totalInspect = 0, passCount = 0, failCount = 0;

    while (combinedQuery.next()) {
        QString queryType = combinedQuery.value(0).toString();
        QString defectType = combinedQuery.value(1).toString();
        int cnt = combinedQuery.value(2).toInt();
        int platformId = combinedQuery.value(3).toInt();
        int passCnt = combinedQuery.value(4).toInt();
        int failCnt = combinedQuery.value(5).toInt();

        if (queryType == "aoi") {
            totalDefects += cnt;
            defectByType[defectType].append(qMakePair("All", cnt));
        } else if (queryType == "insp_total") {
            totalInspect = cnt;
            passCount = passCnt;
            failCount = failCnt;
            passByPeriod["Total"] = passCount;
            failByPeriod["Total"] = failCount;
        } else if (queryType == "insp_platform") {
            platformStats[platformId] = qMakePair(passCnt, failCnt);
        }
    }

    qDebug() << "Combined query results - AOI types:" << defectByType.size()
             << "defects:" << totalDefects << "inspect:" << totalInspect
             << "pass:" << passCount << "fail:" << failCount;

    double passRate = (totalInspect > 0) ? (passCount * 100.0 / totalInspect) : 0;

    emit aoiDataLoaded(defectByType, totalDefects);
    emit inspectionDataLoaded(passByPeriod, failByPeriod, totalInspect, passCount, failCount, passRate);
    emit platformDataLoaded(platformStats);

    db.close();
    QSqlDatabase::removeDatabase("worker_connection");

    qDebug() << "Worker thread: all queries completed";
    emit finished(m_loadId);
}

bool Defect_Data_Display::isCacheValid(CachedTabData* cache, const QString& timeRange, const QDate& date)
{
    if (cache->timeRange.isEmpty() || cache->date != date) {
        return false;
    }
    if (cache->timeRange != timeRange) {
        return false;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - cache->timestamp > 60000) {
        return false;
    }
    return true;
}

void Defect_Data_Display::loadDefectMappingAsync(const QString& timeRange)
{
    if (m_isTabLoading) {
        if (m_tabWorkerThread) {
            m_tabWorkerThread->quit();
            m_tabWorkerThread->wait(500);
            if (m_tabWorkerThread->isFinished()) {
                m_tabWorkerThread->deleteLater();
            }
        }
        m_isTabLoading = false;
    }

    ++m_currentLoadId;
    int thisLoadId = m_currentLoadId;

    m_tabWorkerThread = new TabDataLoaderThread(thisLoadId, 3, timeRange,
        getDateTimeRange(timeRange), m_selectedDate, "", this);
    m_isTabLoading = true;

    connect(m_tabWorkerThread, &TabDataLoaderThread::defectMappingDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_DefectMapping, Qt::QueuedConnection);
    connect(m_tabWorkerThread, &TabDataLoaderThread::finished,
            this, [this](int loadId, int tabIndex) {
                if (loadId == m_currentLoadId) {
                    m_isTabLoading = false;
                    qDebug() << "Tab" << tabIndex << "load finished";
                }
            }, Qt::QueuedConnection);

    m_isTabLoading = true;
    m_tabWorkerThread->start();
}

void Defect_Data_Display::loadTrendDataAsync(const QString& timeRange)
{
    if (m_isTabLoading) {
        if (m_tabWorkerThread) {
            m_tabWorkerThread->quit();
            m_tabWorkerThread->wait(500);
            if (m_tabWorkerThread->isFinished()) {
                m_tabWorkerThread->deleteLater();
            }
        }
        m_isTabLoading = false;
    }

    ++m_currentLoadId;
    int thisLoadId = m_currentLoadId;

    m_tabWorkerThread = new TabDataLoaderThread(thisLoadId, 4, timeRange,
        getDateTimeRange(timeRange), m_selectedDate, "", this);
    m_isTabLoading = true;

    connect(m_tabWorkerThread, &TabDataLoaderThread::trendDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_Trend, Qt::QueuedConnection);
    connect(m_tabWorkerThread, &TabDataLoaderThread::finished,
            this, [this](int loadId, int tabIndex) {
                if (loadId == m_currentLoadId) {
                    m_isTabLoading = false;
                    qDebug() << "Tab" << tabIndex << "load finished";
                }
            }, Qt::QueuedConnection);

    m_tabWorkerThread->start();
}

void Defect_Data_Display::loadDetailDataAsync(const QString& timeRange)
{
    if (m_isTabLoading) {
        if (m_tabWorkerThread) {
            m_tabWorkerThread->quit();
            m_tabWorkerThread->wait(500);
            if (m_tabWorkerThread->isFinished()) {
                m_tabWorkerThread->deleteLater();
            }
        }
        m_isTabLoading = false;
    }

    ++m_currentLoadId;
    int thisLoadId = m_currentLoadId;

    m_tabWorkerThread = new TabDataLoaderThread(thisLoadId, 5, timeRange,
        getDateTimeRange(timeRange), m_selectedDate, "", this);
    m_isTabLoading = true;

    connect(m_tabWorkerThread, &TabDataLoaderThread::detailDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_Detail, Qt::QueuedConnection);
    connect(m_tabWorkerThread, &TabDataLoaderThread::finished,
            this, [this](int loadId, int tabIndex) {
                if (loadId == m_currentLoadId) {
                    m_isTabLoading = false;
                    qDebug() << "Tab" << tabIndex << "load finished";
                }
            }, Qt::QueuedConnection);

    m_tabWorkerThread->start();
}

TabDataLoaderThread::TabDataLoaderThread(int loadId, int tabIndex, const QString& timeRange,
                                       const QString& dateRange, const QDate& date,
                                       const QString& searchScreenId, QObject* parent)
    : QThread(parent)
    , m_loadId(loadId)
    , m_tabIndex(tabIndex)
    , m_timeRange(timeRange)
    , m_dateRange(dateRange)
    , m_date(date)
    , m_searchScreenId(searchScreenId)
{
}

void TabDataLoaderThread::run()
{
    qDebug() << "=== Tab worker thread started ===" << m_tabIndex;

    QString connectionString = "DRIVER={MySQL ODBC 5.3 ANSI Driver};"
                              "SERVER=localhost;"
                              "PORT=3306;"
                              "DATABASE=ivs_lcd;"
                              "UID=root;"
                              "PWD=123456;"
                              "OPTION=8;";

    QString connectionName = QString("tab_worker_connection_%1").arg((quint64)this);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connectionName);
    db.setDatabaseName(connectionString);
    db.setConnectOptions("SQL_ATTR_CONNECTION_TIMEOUT=30000");

    if (!db.open()) {
        qDebug() << "Tab worker: database connection failed:" << db.lastError().text();
        emit finished(m_loadId, m_tabIndex);
        return;
    }

    if (m_tabIndex == 3) {
        QString queryStr = QString(R"(
            SELECT Pos_x, Pos_y, Type
            FROM ivs_lcd_aoidefect FORCE INDEX (IDX_StartTime)
            WHERE %1
            ORDER BY StartTime DESC
        )").arg(m_dateRange);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

        if (query.exec(queryStr)) {
            QList<QPair<int, int>> positions;
            QStringList types;

            while (query.next()) {
                positions.append(qMakePair(query.value(0).toInt(), query.value(1).toInt()));
                types.append(query.value(2).toString());
            }
            emit defectMappingDataLoaded(positions, types);
        } else {
            qDebug() << "Defect mapping query failed:" << query.lastError().text();
        }
    }
    else if (m_tabIndex == 4) {
        QString timeFormat;
        if (m_timeRange == "按小时") {
            timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d %H:00')";
        } else if (m_timeRange == "按天") {
            timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d')";
        } else {
            timeFormat = "DATE_FORMAT(StartTime, '%Y-%m')";
        }

        QString combinedTrendQuery = QString(R"(
            SELECT
                defect_counts.time_period,
                COALESCE(defect_counts.defect_count, 0) as defect_count,
                COALESCE(total_counts.total_count, 0) as total_count
            FROM (
                SELECT %1 as time_period, COUNT(*) as defect_count
                FROM ivs_lcd_aoidefect FORCE INDEX (IDX_StartTime)
                WHERE %2
                GROUP BY time_period
            ) defect_counts
            LEFT JOIN (
                SELECT %1 as time_period, COUNT(*) as total_count
                FROM ivs_lcd_inspectionresult FORCE INDEX (IDX_StartTime)
                WHERE %2
                GROUP BY time_period
            ) total_counts ON defect_counts.time_period = total_counts.time_period
            ORDER BY defect_counts.time_period
        )").arg(timeFormat).arg(m_dateRange);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

        if (query.exec(combinedTrendQuery)) {
            QMap<QString, QPair<int, int>> trendData;
            QMap<QString, double> defectRates;

            while (query.next()) {
                QString period = query.value(0).toString();
                int defectCount = query.value(1).toInt();
                int totalCount = query.value(2).toInt();
                double rate = (totalCount > 0) ? (defectCount * 100.0 / totalCount) : 0;

                trendData[period] = qMakePair(defectCount, totalCount);
                defectRates[period] = rate;
            }
            emit trendDataLoaded(trendData, defectRates);
        } else {
            qDebug() << "Trend query failed:" << query.lastError().text();
        }
    }
    else if (m_tabIndex == 5) {
        QString queryStr = QString(R"(
            SELECT Code_AOI, Grade_AOI, COUNT(*) as cnt
            FROM ivs_lcd_inspectionresult
            WHERE %1
            GROUP BY Code_AOI, Grade_AOI
            ORDER BY Code_AOI, Grade_AOI
        )").arg(m_dateRange);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

        if (query.exec(queryStr)) {
            QList<QVariantList> defectDetails;

            while (query.next()) {
                QVariantList row;
                row.append(query.value(0).toString());
                row.append(query.value(1).toString());
                row.append(query.value(2).toInt());
                defectDetails.append(row);
            }
            emit detailDataLoaded(defectDetails);
        } else {
            qDebug() << "Detail query failed:" << query.lastError().text();
        }
    }
    else if (m_tabIndex == 7) {  // TAB_LOCATION_ABNORMAL
        QString timeFormat;
        if (m_timeRange == "按小时") {
            timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d %H:00')";
        } else if (m_timeRange == "按天") {
            timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d')";
        } else {
            timeFormat = "DATE_FORMAT(StartTime, '%Y-%m')";
        }

        // Query for "定位异常" status grouped by time period and MarkID (1-16)
        QString queryStr = QString(R"(
            SELECT
                %1 as time_period,
                MarkID,
                COUNT(*) as abnormal_count
            FROM ivs_lcd_inspectionresult
            WHERE %2
              AND Status LIKE '%%定位异常%%'
              AND MarkID >= 1 AND MarkID <= 16
            GROUP BY time_period, MarkID
            ORDER BY time_period, MarkID
        )").arg(timeFormat).arg(m_dateRange);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

        if (query.exec(queryStr)) {
            QMap<QString, QMap<int, int>> abnormalByPeriod;

            while (query.next()) {
                QString period = query.value(0).toString();
                int markId = query.value(1).toInt();
                int count = query.value(2).toInt();
                abnormalByPeriod[period][markId] = count;
            }
            emit locationAbnormalDataLoaded(abnormalByPeriod);
        } else {
            qDebug() << "Location abnormal query failed:" << query.lastError().text();
        }
    }

    db.close();
    QSqlDatabase::removeDatabase(connectionName);

    qDebug() << "Tab worker thread finished" << m_tabIndex;
    emit finished(m_loadId, m_tabIndex);
}

// Location Abnormal Tab Functions
void Defect_Data_Display::loadLocationAbnormalData(const QString& timeRange)
{
    QString dateRange = getDateTimeRange(timeRange);
    QString connectionString = "DRIVER={MySQL ODBC 5.3 ANSI Driver};"
                              "SERVER=localhost;"
                              "PORT=3306;"
                              "DATABASE=ivs_lcd;"
                              "UID=root;"
                              "PWD=123456;"
                              "OPTION=8;";

    QString connectionName = QString("location_abnormal_%1").arg((quint64)this);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connectionName);
    db.setDatabaseName(connectionString);
    db.setConnectOptions("SQL_ATTR_CONNECTION_TIMEOUT=30000");

    if (!db.open()) {
        qDebug() << "Location abnormal: database connection failed:" << db.lastError().text();
        return;
    }

    QString timeFormat;
    if (timeRange == "按小时") {
        timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d %H:00')";
    } else if (timeRange == "按天") {
        timeFormat = "DATE_FORMAT(StartTime, '%Y-%m-%d')";
    } else {
        timeFormat = "DATE_FORMAT(StartTime, '%Y-%m')";
    }

    QString queryStr = QString(R"(
        SELECT
            %1 as time_period,
            MarkID,
            COUNT(*) as abnormal_count
        FROM ivs_lcd_inspectionresult
        WHERE %2
          AND Status LIKE '%%定位异常%%'
          AND MarkID >= 1 AND MarkID <= 16
        GROUP BY time_period, MarkID
        ORDER BY time_period, MarkID
    )").arg(timeFormat).arg(dateRange);

    QSqlQuery query(db);
    query.setForwardOnly(true);
    query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (query.exec(queryStr)) {
        QMap<QString, QMap<int, int>> abnormalByPeriod;

        while (query.next()) {
            QString period = query.value(0).toString();
            int markId = query.value(1).toInt();
            int count = query.value(2).toInt();
            abnormalByPeriod[period][markId] = count;
        }
        onDataLoaded_LocationAbnormal(abnormalByPeriod);
    } else {
        qDebug() << "Location abnormal query failed:" << query.lastError().text();
    }

    db.close();
    QSqlDatabase::removeDatabase(connectionName);
}

void Defect_Data_Display::loadLocationAbnormalDataAsync(const QString& timeRange)
{
    if (m_isTabLoading) {
        if (m_tabWorkerThread) {
            m_tabWorkerThread->quit();
            m_tabWorkerThread->wait(500);
            if (m_tabWorkerThread->isFinished()) {
                m_tabWorkerThread->deleteLater();
            }
        }
        m_isTabLoading = false;
    }

    ++m_currentLoadId;
    int thisLoadId = m_currentLoadId;

    m_tabWorkerThread = new TabDataLoaderThread(thisLoadId, 7, timeRange,  // TAB_LOCATION_ABNORMAL
        getDateTimeRange(timeRange), m_selectedDate, "", this);
    m_isTabLoading = true;

    connect(m_tabWorkerThread, &TabDataLoaderThread::locationAbnormalDataLoaded,
            this, &Defect_Data_Display::onDataLoaded_LocationAbnormal, Qt::QueuedConnection);
    connect(m_tabWorkerThread, &TabDataLoaderThread::finished,
            this, [this](int loadId, int tabIndex) {
                if (loadId == m_currentLoadId) {
                    m_isTabLoading = false;
                }
            }, Qt::QueuedConnection);

    m_tabWorkerThread->start();
}

void Defect_Data_Display::onDataLoaded_LocationAbnormal(const QMap<QString, QMap<int, int>>& abnormalByPeriod)
{
    m_locationAbnormalData = abnormalByPeriod;

    // Update cache
    m_locationAbnormalCache.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_locationAbnormalCache.timeRange = ui.comboTimeRange->currentText();
    m_locationAbnormalCache.date = m_selectedDate;

    updateLocationAbnormalChart(abnormalByPeriod);
}

void Defect_Data_Display::updateLocationAbnormalChart(const QMap<QString, QMap<int, int>>& abnormalByPeriod)
{
    // Clear existing content in all frames
    QList<QFrame*> frames = {
        ui.chartLocationAbnormalFrame0,
        ui.chartLocationAbnormalFrame1,
        ui.chartLocationAbnormalFrame2,
        ui.chartLocationAbnormalFrame3,
        ui.chartLocationAbnormalFrame4
    };
    for (QFrame* frame : frames) {
        QLayout* existingLayout = frame->layout();
        if (existingLayout) {
            QLayoutItem* item;
            while ((item = existingLayout->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
            delete existingLayout;
        }
    }

    QString timeRange = ui.comboTimeRange->currentText();

    // Generate time periods based on time range type
    QList<QString> periods;
    if (timeRange == "按小时") {
        for (int h = 0; h < 24; ++h) {
            periods << QString("%1 %2:00").arg(m_selectedDate.toString("yyyy-MM-dd")).arg(h, 2, 10, QChar('0'));
        }
    } else if (timeRange == "按天") {
        int daysInMonth = m_selectedDate.daysInMonth();
        for (int d = 1; d <= daysInMonth; ++d) {
            periods << QString("%1-%2").arg(m_selectedDate.toString("yyyy-MM")).arg(d, 2, 10, QChar('0'));
        }
    } else if (timeRange == "按月") {
        for (int m = 1; m <= 12; ++m) {
            periods << QString("%1-%2").arg(m_selectedDate.year()).arg(m, 2, 10, QChar('0'));
        }
    } else {
        periods = abnormalByPeriod.keys();
        std::sort(periods.begin(), periods.end());
    }

    // Page 0: 按时间统计（总数，不区分工位）
    {
        QFrame* frame = frames[0];

        QWidget* container = new QWidget();
        container->setStyleSheet("background-color: #16213e; border-radius: 8px;");
        QVBoxLayout* mainLayout = new QVBoxLayout(container);
        mainLayout->setSpacing(10);
        mainLayout->setContentsMargins(10, 10, 10, 10);

        // Create single bar series for total count by time
        QBarSeries* barSeries = new QBarSeries();
        barSeries->setLabelsVisible(true);
        barSeries->setLabelsFormat("@value");
        barSeries->setLabelsPosition(QBarSeries::LabelsOutsideEnd);

        qDebug() << "=== Building summary chart ===";
        qDebug() << "Total periods to process:" << periods.size();
        qDebug() << "abnormalByPeriod keys:" << abnormalByPeriod.keys();

        QStringList categories;
        int maxCategories;
        if (timeRange == "按小时") {
            maxCategories = 24;
        } else if (timeRange == "按天") {
            maxCategories = 31;  // 最多显示31天
        } else {
            maxCategories = 12;  // 按月
        }

        QBarSet* barSet = new QBarSet("");
        for (int j = 0; j < periods.size() && j < maxCategories; ++j) {
            QString period = periods[j];
            int totalCount = 0;
            QMap<int, int> markData = abnormalByPeriod.value(period);
            for (auto it = markData.constBegin(); it != markData.constEnd(); ++it) {
                totalCount += it.value();
            }

            QString shortLabel;
            if (period.contains(":")) {
                shortLabel = period.mid(period.lastIndexOf(" ") + 1, 2);
            } else if (period.contains("-")) {
                QStringList parts = period.split("-");
                if (parts.size() >= 2) {
                    if (parts.size() >= 3) {
                        shortLabel = parts[2];
                    } else {
                        shortLabel = parts[1];
                    }
                }
            }

            qDebug() << "Period:" << period << "totalCount:" << totalCount << "shortLabel:" << shortLabel;

            //if (totalCount > 0) {
                *barSet << totalCount;
                barSet->setColor(QColor(0, 200, 255));
                barSet->setLabelColor(QColor(234, 234, 234));
            //}
            categories << shortLabel;
        }

        barSeries->append(barSet);
        // Create chart
        QChart* chart = new QChart();
        chart->addSeries(barSeries);
        chart->setTitle("定位异常总数趋势");
        chart->setAnimationOptions(QChart::NoAnimation);
        chart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
        chart->setTitleBrush(QBrush(QColor(0, 217, 255)));
        chart->legend()->setVisible(false);

        QBarCategoryAxis* axisX = new QBarCategoryAxis();
        axisX->append(categories);
        axisX->setLabelsColor(QColor(234, 234, 234));
        axisX->setLabelsFont(QFont("Arial", 9));
        chart->addAxis(axisX, Qt::AlignBottom);

        int maxValue = 0;
        for (int j = 0; j < periods.size() && j < maxCategories; ++j) {
            int totalCount = 0;
            QMap<int, int> markData = abnormalByPeriod.value(periods[j]);
            for (auto it = markData.constBegin(); it != markData.constEnd(); ++it) {
                totalCount += it.value();
            }
            if (totalCount > maxValue) maxValue = totalCount;
        }

        QValueAxis* axisY = new QValueAxis();
        axisY->setLabelFormat("%d");
        axisY->setLabelsColor(QColor(234, 234, 234));
        axisY->setLabelsFont(QFont("Arial", 9));
        axisY->setGridLineVisible(true);
        axisY->setMinorGridLineVisible(false);
        axisY->setRange(0, maxValue == 0 ? 10 : maxValue * 1.2);
        chart->addAxis(axisY, Qt::AlignLeft);
        barSeries->attachAxis(axisY);

        QChartView* chartView = new QChartView(chart);
        chartView->setRenderHint(QPainter::Antialiasing);
        chartView->setMinimumHeight(300);

        mainLayout->addWidget(chartView);

        QVBoxLayout* wrapperLayout = new QVBoxLayout(frame);
        wrapperLayout->setContentsMargins(0, 0, 0, 0);
        wrapperLayout->addWidget(container);
    }

    // Colors for bars - distinct colors for each time period
    QList<QColor> barColors;
    barColors << QColor(0, 200, 255) << QColor(255, 100, 100) << QColor(100, 255, 150)
              << QColor(255, 200, 0) << QColor(200, 100, 255) << QColor(100, 200, 200)
              << QColor(255, 150, 100) << QColor(150, 100, 255) << QColor(80, 255, 200)
              << QColor(255, 80, 150) << QColor(100, 150, 255) << QColor(200, 255, 80)
              << QColor(255, 180, 80) << QColor(80, 180, 255) << QColor(180, 80, 180)
              << QColor(180, 255, 80) << QColor(255, 80, 180) << QColor(80, 255, 180)
              << QColor(150, 150, 255) << QColor(255, 150, 150) << QColor(150, 255, 150)
              << QColor(255, 255, 100) << QColor(255, 150, 255) << QColor(150, 255, 255);

    // Process each page (4 platforms per page)
    for (int page = 0; page < 4; ++page) {
        QFrame* frame = frames[page + 1];
        int startMarkId = page * 4 + 1;  // 1, 5, 9, 13

        // Create container with vertical layout
        QWidget* container = new QWidget();
        container->setStyleSheet("background-color: #16213e; border-radius: 8px;");
        QVBoxLayout* mainLayout = new QVBoxLayout(container);
        mainLayout->setSpacing(10);
        mainLayout->setContentsMargins(10, 10, 10, 10);

        // Create 4 charts in vertical layout (4x1)
        QVBoxLayout* vLayout = new QVBoxLayout();
        vLayout->setSpacing(10);

        for (int i = 0; i < 4; ++i) {
            int markId = startMarkId + i;

            // Create bar series for this platform
            QBarSeries* barSeries = new QBarSeries();
            barSeries->setLabelsVisible(true);
            barSeries->setLabelsFormat("@value");
            barSeries->setLabelsPosition(QBarSeries::LabelsOutsideEnd);

            QStringList categories;
            int maxCategories;
            if (timeRange == "按小时") {
                maxCategories = 24;
            } else if (timeRange == "按天") {
                maxCategories = 31;
            } else {
                maxCategories = 12;
            }

            QBarSet* barSet = new QBarSet("");
            for (int j = 0; j < periods.size() && j < maxCategories; ++j) {
                QString period = periods[j];
                int count = abnormalByPeriod.value(period).value(markId, 0);

                *barSet << count;
                barSet->setColor(barColors.value(j, Qt::gray));
                barSet->setLabelColor(QColor(234, 234, 234));

                QString shortLabel;
                if (period.contains(":")) {
                    shortLabel = period.mid(period.lastIndexOf(" ") + 1, 2);
                } else if (period.contains("-")) {
                    QStringList parts = period.split("-");
                    if (parts.size() >= 2) {
                        if (parts.size() >= 3) {
                            QString month = parts[1];
                            QString day = parts[2];
                            if (period.length() <= 7) {
                                shortLabel = month + "/" + day;
                            } else {
                                shortLabel = day;
                            }
                        } else {
                            shortLabel = parts[1];
                        }
                    }
                }
                categories << shortLabel;
            }

            barSeries->append(barSet);

            // Create chart
            QChart* chart = new QChart();
            chart->addSeries(barSeries);
            chart->setTitle(QString("工位 %1").arg(markId));
            chart->setAnimationOptions(QChart::NoAnimation);
            chart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
            chart->setTitleBrush(QBrush(QColor(0, 217, 255)));
            chart->legend()->setVisible(false);

            // Add axes
            QBarCategoryAxis* axisX = new QBarCategoryAxis();
            axisX->append(categories);
            axisX->setLabelsColor(QColor(234, 234, 234));
            axisX->setLabelsFont(QFont("Arial", 7));
            chart->addAxis(axisX, Qt::AlignBottom);

            int maxValue = 0;
            for (int j = 0; j < periods.size() && j < maxCategories; ++j) {
                int count = abnormalByPeriod.value(periods[j]).value(markId, 0);
                if (count > maxValue) maxValue = count;
            }

            QValueAxis* axisY = new QValueAxis();
            axisY->setLabelFormat("%d");
            axisY->setLabelsColor(QColor(234, 234, 234));
            axisY->setLabelsFont(QFont("Arial", 8));
            axisY->setGridLineVisible(true);
            axisY->setMinorGridLineVisible(false);
            if (maxValue == 0) {
                axisY->setRange(0, 10);
            } else {
                axisY->setRange(0, maxValue * 3.0);
            }
            axisY->applyNiceNumbers();
            chart->addAxis(axisY, Qt::AlignLeft);
            barSeries->attachAxis(axisY);

            // Create chart view
            QChartView* chartView = new QChartView(chart);
            chartView->setRenderHint(QPainter::Antialiasing);
            chartView->setMinimumHeight(120);
            chartView->setMaximumHeight(150);

            vLayout->addWidget(chartView);
        }

        mainLayout->addLayout(vLayout);

        // Set layout to frame
        QVBoxLayout* wrapperLayout = new QVBoxLayout(frame);
        wrapperLayout->setContentsMargins(0, 0, 0, 0);
        wrapperLayout->addWidget(container);
    }

    // Update cache
    m_locationAbnormalCache.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_locationAbnormalCache.timeRange = ui.comboTimeRange->currentText();
    m_locationAbnormalCache.date = m_selectedDate;
}

