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
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
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
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QPixmap>
#include <QTextStream>
#include <algorithm>

#define DEFAULT_MANUAL_MAIN_AOI_IMAGE_ROOT "D:\\MEMS_DFS_Data\\"

Defect_Data_Display::Defect_Data_Display(QWidget *parent)
    : QMainWindow(parent)
    , m_chartViewAoi(nullptr)
    , m_chartViewInspectionPass(nullptr)
    , m_chartViewInspectionFail(nullptr)
    , m_chartViewPlatform0(nullptr)
    , m_chartViewPlatform1(nullptr)
    , m_chartViewPlatform2(nullptr)
    , m_chartViewPlatform3(nullptr)
    , m_chartViewPlatformByTime(nullptr)
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
    , m_searchStartHour(0)    // 默认 00:00
    , m_searchEndHour(23)     // 默认 23:59
    , m_isDragging(false)
    , m_fullScreenInitialized(false)
    , m_isMaximized(false)
    , m_isLoading(false)
    , m_isTabLoading(false)
    , m_lastMainLoadTime(0)
    , m_tooltipLabel(nullptr)
    , m_barClickDialog(nullptr)
    , m_detailPieTitle()
    , m_detailPieData()
{
    ui.setupUi(this);

    ui.dateEdit->setDate(m_selectedDate);
    ui.dateEdit->setDisplayFormat("yyyy-MM-dd");
    // Set default selection for hourly time range (default: 0-23)
    ui.timeEditStart->setCurrentIndex(0);   // 00:00
    ui.timeEditEnd->setCurrentIndex(23);     // 23:00
    ui.timeEditStart->setEnabled(false);
    ui.timeEditEnd->setEnabled(false);
    ui.labelStartTime->setEnabled(false);
    ui.labelEndTime->setEnabled(false);

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
    connect(ui.comboTimeRange, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Defect_Data_Display::onTimeRangeChangedForSearch);
    connect(ui.timeEditStart, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_searchStartHour = ui.timeEditStart->currentIndex();
        if (ui.comboTimeRange->currentText() == "按小时") {
            int currentTab = ui.tabWidget->currentIndex();
            if (currentTab == 1) {
                loadTrendDataAsync(ui.comboTimeRange->currentText());
            } else if (currentTab == 2) {
                loadDetailDataAsync(ui.comboTimeRange->currentText());
            } else if (currentTab == 4) {
                loadLocationAbnormalDataAsync(ui.comboTimeRange->currentText());
            }
        }
    });
    connect(ui.timeEditEnd, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_searchEndHour = ui.timeEditEnd->currentIndex();
        if (ui.comboTimeRange->currentText() == "按小时") {
            int currentTab = ui.tabWidget->currentIndex();
            if (currentTab == 1) {
                loadTrendDataAsync(ui.comboTimeRange->currentText());
            } else if (currentTab == 2) {
                loadDetailDataAsync(ui.comboTimeRange->currentText());
            } else if (currentTab == 4) {
                loadLocationAbnormalDataAsync(ui.comboTimeRange->currentText());
            }
        }
    });
    connect(ui.btnMinimize, &QPushButton::clicked, this, &Defect_Data_Display::onMinimizeClicked);
    connect(ui.btnMaximize, &QPushButton::clicked, this, &Defect_Data_Display::onMaximizeClicked);
    connect(ui.btnClose, &QPushButton::clicked, this, &Defect_Data_Display::onCloseClicked);
    connect(ui.tabWidget, &QTabWidget::currentChanged, this, &Defect_Data_Display::onTabChanged);
    connect(ui.tabPlatformPages, &QTabWidget::currentChanged, this, &Defect_Data_Display::onPlatformTabChanged);
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

    // Time filter combo box styling
    ui.timeEditStart->setStyleSheet(R"(
        QComboBox {
            background-color: rgba(30, 40, 60, 200);
            border: 1px solid rgba(0, 217, 255, 80);
            border-radius: 4px;
            padding: 2px 5px;
            color: #e0f0ff;
        }
        QComboBox:disabled {
            background-color: rgba(20, 30, 45, 150);
            color: #607080;
            border: 1px solid rgba(0, 217, 255, 40);
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 6px solid #00d9ff;
        }
        QComboBox QAbstractItemView {
            background-color: rgba(20, 35, 55, 240);
            border: 1px solid rgba(0, 217, 255, 80);
            color: #e0f0ff;
            selection-background-color: rgba(0, 150, 200, 180);
        }
    )");
    ui.timeEditEnd->setStyleSheet(R"(
        QComboBox {
            background-color: rgba(30, 40, 60, 200);
            border: 1px solid rgba(0, 217, 255, 80);
            border-radius: 4px;
            padding: 2px 5px;
            color: #e0f0ff;
        }
        QComboBox:disabled {
            background-color: rgba(20, 30, 45, 150);
            color: #607080;
            border: 1px solid rgba(0, 217, 255, 40);
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 6px solid #00d9ff;
        }
        QComboBox QAbstractItemView {
            background-color: rgba(20, 35, 55, 240);
            border: 1px solid rgba(0, 217, 255, 80);
            color: #e0f0ff;
            selection-background-color: rgba(0, 150, 200, 180);
        }
    )");

    // Initially disable time edits (only enabled for "按小时" mode)
    ui.timeEditStart->setEnabled(false);
    ui.timeEditEnd->setEnabled(false);
    ui.labelStartTime->setEnabled(false);
    ui.labelEndTime->setEnabled(false);

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
    //ui.tabDefect->hide();
    //ui.tabInspection->hide();
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
    if (m_barClickDialog) {
        if (m_barClickDialog->isVisible()) {
            m_barClickDialog->close();
        }
        delete m_barClickDialog;
        m_barClickDialog = nullptr;
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

void Defect_Data_Display::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_fullScreenInitialized) {
        m_fullScreenInitialized = true;
        QTimer::singleShot(0, this, [this]() {
            setWindowFlags(Qt::FramelessWindowHint);
            showFullScreen();
        });
    }
}

bool Defect_Data_Display::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        bool matched = false;

        // Check if it's the by-time chart
        if (m_chartViewPlatformByTime && (m_chartViewPlatformByTime == watched || m_chartViewPlatformByTime->viewport() == watched)) {
            QChart* chart = ((QChartView*)m_chartViewPlatformByTime)->chart();
            QList<QAbstractSeries*> seriesList = chart->series();
            if (!seriesList.isEmpty()) {
                QPointF chartPos = chart->mapToValue(QPointF(me->pos()), seriesList.first());
                showByTimeChartTooltip(me->pos(), chartPos);
            }
            matched = true;
        }

        if (!matched) {
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
        }
        if (!matched) {
            m_tooltipLabel->hide();
        }
    } else if (event->type() == QEvent::Leave) {
        // Check if it's the by-time chart
        if (m_chartViewPlatformByTime && (m_chartViewPlatformByTime == watched || m_chartViewPlatformByTime->viewport() == watched)) {
            m_tooltipLabel->hide();
        }

        for (int p = 0; p < 4; ++p) {
            void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
            QChartView* cv = (QChartView*)chartViewPtrs[p];
            if (cv && (cv == watched || cv->viewport() == watched)) {
                m_tooltipLabel->hide();
                break;
            }
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            if (m_chartViewPlatformByTime && (m_chartViewPlatformByTime == watched || m_chartViewPlatformByTime->viewport() == watched)) {
                QChart* chart = m_chartViewPlatformByTime->chart();
                QList<QAbstractSeries*> seriesList = chart->series();
                if (!seriesList.isEmpty()) {
                    QAbstractBarSeries* barSeries = qobject_cast<QAbstractBarSeries*>(seriesList.first());
                    if (barSeries) {
                        QRectF plotArea = chart->plotArea();
                        qreal relX = (me->pos().x() - plotArea.left()) / plotArea.width();

                        QList<QAbstractAxis*> axesX = chart->axes(Qt::Horizontal);
                        if (!axesX.isEmpty()) {
                            QBarCategoryAxis* axisX = qobject_cast<QBarCategoryAxis*>(axesX.first());
                            if (axisX) {
                                QStringList categories = axisX->categories();
                                if (!categories.isEmpty()) {
                                    int numCategories = categories.size();
                                    int numBarSets = barSeries->count();
                                    int totalBars = numCategories * numBarSets;
                                    if (totalBars > 0) {
                                        int barIndex = static_cast<int>(relX * totalBars);
                                        barIndex = qBound(0, barIndex, totalBars - 1);
                                        int categoryIndex = barIndex / qMax(numBarSets, 1);
                                        int platformIdx = barIndex % qMax(numBarSets, 1);
                                        if (categoryIndex >= 0 && categoryIndex < categories.size() && platformIdx >= 0 && platformIdx < 4) {
                                            QString timeKey = categories[categoryIndex];
                                            Q_UNUSED(platformIdx);
                                            showBarClickDialog(-1, timeKey);
                                            return true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (int p = 0; p < 4; ++p) {
                void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
                QChartView* cv = (QChartView*)chartViewPtrs[p];
                if (cv && (cv == watched || cv->viewport() == watched)) {
                    QChart* chart = cv->chart();
                    QList<QAbstractSeries*> seriesList = chart->series();
                    if (!seriesList.isEmpty()) {
                        QPointF chartPos = chart->mapToValue(QPointF(me->pos()), seriesList.first());

                        QAbstractBarSeries* barSeries = qobject_cast<QAbstractBarSeries*>(seriesList.first());
                        if (barSeries) {
                            QRectF plotArea = chart->plotArea();
                            qreal relX = (me->pos().x() - plotArea.left()) / plotArea.width();

                            QList<QAbstractAxis*> axesX = chart->axes(Qt::Horizontal);
                            if (!axesX.isEmpty()) {
                                QBarCategoryAxis* axisX = qobject_cast<QBarCategoryAxis*>(axesX.first());
                                if (axisX) {
                                    QStringList categories = axisX->categories();
                                    if (!categories.isEmpty()) {
                                        int numCategories = categories.size();
                                        int numBarSets = barSeries->count();
                                        int totalBars = numCategories * numBarSets;
                                        if (totalBars > 0) {
                                            int barIndex = static_cast<int>(relX * totalBars);
                                            int categoryIndex = barIndex / qMax(numBarSets, 1);
                                            if (categoryIndex >= 0 && categoryIndex < categories.size()) {
                                                QString timeKey = categories[categoryIndex];
                                                showBarClickDialog(p, timeKey);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return QMainWindow::eventFilter(watched, event);
                }
            }

            QChartView* pieView = static_cast<QChartView*>(m_chartViewPieDetail);
            if (pieView && (pieView == watched || pieView->viewport() == watched)) {
                return true;
            }

            // Handle chartDetail bar chart click (newDetailChartView)
            QChartView* detailView = static_cast<QChartView*>(m_chartViewDetail);
            if (detailView && (detailView == watched || detailView->viewport() == watched)) {
                QChart* chart = detailView->chart();
                QList<QAbstractSeries*> seriesList = chart->series();
                if (!seriesList.isEmpty()) {
                    QPointF chartPos = chart->mapToValue(QPointF(me->pos()), seriesList.first());

                    QAbstractBarSeries* barSeries = qobject_cast<QAbstractBarSeries*>(seriesList.first());
                    if (barSeries) {
                        QRectF plotArea = chart->plotArea();
                        qreal relX = (me->pos().x() - plotArea.left()) / plotArea.width();

                        QList<QAbstractAxis*> axesX = chart->axes(Qt::Horizontal);
                        if (!axesX.isEmpty()) {
                            QBarCategoryAxis* axisX = qobject_cast<QBarCategoryAxis*>(axesX.first());
                            if (axisX) {
                                QStringList categories = axisX->categories();
                                if (!categories.isEmpty()) {
                                    int numCategories = categories.size();
                                    int numBarSets = barSeries->count();
                                    int totalBars = numCategories * numBarSets;
                                    if (totalBars > 0) {
                                        int barIndex = static_cast<int>(relX * totalBars);
                                        int categoryIndex = barIndex / qMax(numBarSets, 1);
                                        if (categoryIndex >= 0 && categoryIndex < categories.size()) {
                                            QString gradeName = categories[categoryIndex];
                                            qDebug() << "[DetailChart] Clicked at pos:" << me->pos() << "chartPos:" << chartPos << "bar index:" << barIndex << "categoryIndex:" << categoryIndex << "grade:" << gradeName;
                                            //showGradeTypeDialog(gradeName);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return true;  // Consume the event
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void Defect_Data_Display::showDetailPieDialog()
{
    if (m_detailPieData.isEmpty()) {
        QMessageBox::information(this, "提示", "当前没有可显示的饼图数据。");
        return;
    }

    int total = 0;
    for (auto it = m_detailPieData.constBegin(); it != m_detailPieData.constEnd(); ++it) {
        total += it.value();
    }

    QDialog dialog(this);
    dialog.setWindowTitle(m_detailPieTitle.isEmpty() ? "缺陷类型分布" : m_detailPieTitle);
    dialog.setModal(true);

    // Calculate dialog size based on row count
    const int rowCount = m_detailPieData.size();
    const int tableRowHeight = 40;
    const int headerHeight = 40;
    const int tableMinHeight = qMin(rowCount * tableRowHeight + headerHeight, 800); // Max 800px, or calculated height
    const int dialogWidth = 1500;
    const int dialogHeight = 200 + tableMinHeight; // 200 for title, pie chart, stats, button
    dialog.resize(dialogWidth, dialogHeight);
    dialog.setStyleSheet(R"(
        QDialog {
            background-color: #1a1a2e;
        }
        QLabel#titleLabel {
            color: #ffffff;
            font-size: 18px;
            font-weight: bold;
            padding: 8px 0;
        }
        QLabel#subtitleLabel {
            color: #8892b0;
            font-size: 12px;
            padding-bottom: 10px;
        }
        QTableWidget {
            background-color: #16213e;
            border: 1px solid #0f3460;
            border-radius: 8px;
            padding: 5px;
            gridline-color: #0f3460;
            color: #e0e0e0;
            font-size: 13px;
        }
        QTableWidget::item {
            padding: 8px 12px;
            border-bottom: 1px solid #0f3460;
        }
        QTableWidget::item:selected {
            background-color: #0f3460;
            color: #00d9ff;
        }
        QHeaderView::section {
            background-color: #0f3460;
            color: #00d9ff;
            font-weight: bold;
            font-size: 13px;
            padding: 10px;
            border: none;
            border-bottom: 2px solid #00d9ff;
        }
        QPushButton {
            background-color: #0f3460;
            color: #00d9ff;
            border: 1px solid #00d9ff;
            border-radius: 6px;
            padding: 10px 30px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #00d9ff;
            color: #16213e;
        }
        QFrame#chartFrame {
            background-color: #16213e;
            border: 1px solid #0f3460;
            border-radius: 8px;
            padding: 5px;
        }
        QFrame#statCard {
            background-color: #16213e;
            border: 1px solid #0f3460;
            border-radius: 8px;
            padding: 10px;
        }
    )");

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title section
    QLabel* titleLabel = new QLabel(m_detailPieTitle.isEmpty() ? "缺陷类型分布" : m_detailPieTitle, &dialog);
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel, 0);

    QLabel* subtitleLabel = new QLabel("点击下方表格查看详细分布信息", &dialog);
    subtitleLabel->setObjectName("subtitleLabel");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitleLabel, 0);

    // Content layout: pie chart on left, all controls on right
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(20);

    // Left side: pie chart only
    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(10);

    // Mini pie chart frame
    QFrame* chartFrame = new QFrame(&dialog);
    chartFrame->setObjectName("chartFrame");
    chartFrame->setFixedWidth(320);
    chartFrame->setMinimumHeight(320);
    QVBoxLayout* chartLayout = new QVBoxLayout(chartFrame);
    chartLayout->setContentsMargins(10, 10, 10, 10);
    chartLayout->setSpacing(10);

    QLabel* chartTitle = new QLabel("分布图", chartFrame);
    chartTitle->setStyleSheet("color: #00d9ff; font-size: 14px; font-weight: bold;");
    chartTitle->setAlignment(Qt::AlignCenter);
    chartLayout->addWidget(chartTitle);

    // Create mini pie chart
    QPieSeries* miniSeries = new QPieSeries();
    QList<QString> colors = {"#00d9ff", "#ff6b6b", "#4ecdc4", "#ffe66d", "#a855f7", "#f97316", "#84cc16", "#ec4899"};
    int colorIdx = 0;
    for (auto it = m_detailPieData.constBegin(); it != m_detailPieData.constEnd(); ++it) {
        QPieSlice* slice = miniSeries->append(it.key(), it.value());
        slice->setColor(QColor(colors[colorIdx % colors.size()]));
        slice->setLabelVisible(false);
        colorIdx++;
    }
    miniSeries->setHoleSize(0.35);

    QChart* miniChart = new QChart();
    miniChart->addSeries(miniSeries);
    miniChart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    miniChart->setAnimationOptions(QChart::NoAnimation);
    miniChart->legend()->setVisible(false);

    QChartView* miniChartView = new QChartView(miniChart);
    miniChartView->setRenderHint(QPainter::Antialiasing);
    miniChartView->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    miniChartView->setMinimumSize(280, 260);
    chartLayout->addWidget(miniChartView, 1);

    leftLayout->addWidget(chartFrame);
    leftLayout->addStretch(1);
    contentLayout->addLayout(leftLayout, 1);

    // Right side: stats + data table
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(12);

    // Stats cards
    QHBoxLayout* statsLayout = new QHBoxLayout();
    statsLayout->setSpacing(10);

    // Total stat card
    QFrame* totalCard = new QFrame(&dialog);
    totalCard->setObjectName("statCard");
    QVBoxLayout* totalCardLayout = new QVBoxLayout(totalCard);
    totalCardLayout->setContentsMargins(8, 8, 8, 8);
    QLabel* totalTitle = new QLabel("总数", totalCard);
    totalTitle->setStyleSheet("color: #8892b0; font-size: 11px;");
    totalTitle->setAlignment(Qt::AlignCenter);
    QLabel* totalValue = new QLabel(QString::number(total), totalCard);
    totalValue->setStyleSheet("color: #00d9ff; font-size: 22px; font-weight: bold;");
    totalValue->setAlignment(Qt::AlignCenter);
    totalCardLayout->addWidget(totalTitle);
    totalCardLayout->addWidget(totalValue);
    statsLayout->addWidget(totalCard);

    // Type count stat card
    QFrame* typeCard = new QFrame(&dialog);
    typeCard->setObjectName("statCard");
    QVBoxLayout* typeCardLayout = new QVBoxLayout(typeCard);
    typeCardLayout->setContentsMargins(8, 8, 8, 8);
    QLabel* typeTitle = new QLabel("类型数", typeCard);
    typeTitle->setStyleSheet("color: #8892b0; font-size: 11px;");
    typeTitle->setAlignment(Qt::AlignCenter);
    QLabel* typeValue = new QLabel(QString::number(m_detailPieData.size()), typeCard);
    typeValue->setStyleSheet("color: #ff6b6b; font-size: 22px; font-weight: bold;");
    typeValue->setAlignment(Qt::AlignCenter);
    typeCardLayout->addWidget(typeTitle);
    typeCardLayout->addWidget(typeValue);
    statsLayout->addWidget(typeCard);

    rightLayout->addLayout(statsLayout);

    QLabel* tableTitle = new QLabel("详细数据", &dialog);
    tableTitle->setStyleSheet("color: #00d9ff; font-size: 14px; font-weight: bold;");
    tableTitle->setFixedHeight(20);
    rightLayout->addWidget(tableTitle);
    //rightLayout->addWidget(table, 1);

    QTableWidget* table = new QTableWidget(&dialog);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"缺陷类型", "数量", "比例", "占比"});
    table->setRowCount(m_detailPieData.size());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(false);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    table->verticalHeader()->setDefaultSectionSize(36);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Calculate table height based on row count
    const int rowHeight = 40; //36;
    const int hdrHeight = 38;
    const int tableHeight = rowCount * rowHeight + hdrHeight;
    table->setFixedHeight(tableHeight);

    // Set uniform background for all rows
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < 4; ++c) {
            QTableWidgetItem* item = table->item(r, c);
            if (item) {
                item->setBackground(QBrush(QColor("#16213e")));
            }
        }
    }

    // Set row colors for the mini pie chart legend
    colorIdx = 0;
    int row = 0;
    for (auto it = m_detailPieData.constBegin(); it != m_detailPieData.constEnd(); ++it, ++row) {
        const int count = it.value();
        const double ratio = total > 0 ? static_cast<double>(count) / static_cast<double>(total) : 0.0;
        const QString colorHex = colors[colorIdx % colors.size()];

        // Type name with color indicator
        QTableWidgetItem* nameItem = new QTableWidgetItem("● " + it.key());
        nameItem->setForeground(QBrush(QColor(colorHex)));
        nameItem->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
        table->setItem(row, 0, nameItem);

        QTableWidgetItem* countItem = new QTableWidgetItem(QString::number(count));
        countItem->setFont(QFont("Microsoft YaHei", 12));
        countItem->setForeground(QBrush(QColor("#e0e0e0")));
        countItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 1, countItem);

        QTableWidgetItem* ratioItem = new QTableWidgetItem(QString::number(ratio, 'f', 4));
        ratioItem->setFont(QFont("Microsoft YaHei", 12));
        ratioItem->setForeground(QBrush(QColor("#e0e0e0")));
        ratioItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 2, ratioItem);

        QTableWidgetItem* percentItem = new QTableWidgetItem(QString::number(ratio * 100.0, 'f', 1) + "%");
        percentItem->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
        percentItem->setForeground(QBrush(QColor(colorHex)));
        percentItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 3, percentItem);

        colorIdx++;
    }

    // Adjust column widths for wider dialog
    table->setColumnWidth(0, 250);
    table->setColumnWidth(1, 150);
    table->setColumnWidth(2, 150);
    rightLayout->addWidget(table, 6);
    contentLayout->addLayout(rightLayout, 3);

    // Connect table cell click to show defect type detail dialog
    QObject::connect(table, &QTableWidget::cellClicked, this, [=](int row, int /*column*/) {
        if (row >= 0 && row < m_detailPieData.size()) {
            QString defectType = m_detailPieData.keys().at(row);
            qDebug() << "[PieDetailTable] Clicked row:" << row << "defect type:" << defectType;
            showGradeTypeDialog(defectType);
        }
    });

    mainLayout->addLayout(contentLayout, 1);

    // Bottom button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    QPushButton* okBtn = new QPushButton("确 定", &dialog);
    okBtn->setFixedWidth(120);
    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonLayout->addWidget(okBtn);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout, 0);

    dialog.exec();
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
        
        // Build OK/NG breakdown HTML
        QString section1;
        int okCount = 0, ngCount = 0;
        for (auto ait = aoiMap.constBegin(); ait != aoiMap.constEnd(); ++ait) {
            if (ait.key() == "OK") okCount = ait.value();
            else if (ait.key() == "NG") ngCount = ait.value();
        }
        // Display OK first (green), then NG (red)
        if (okCount > 0) {
            section1 += QString("<div style='font-size:15px'><span style='color:%1'>OK : %2</span></div>")
                .arg(QColor(0, 255, 136).name()).arg(okCount);
        }
        if (ngCount > 0) {
            section1 += QString("<div style='font-size:15px'><span style='color:%1'>NG : %2</span></div>")
                .arg(QColor(255, 80, 80).name()).arg(ngCount);
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
        // Non-stacked bar - show OK/NG (derived from Pass/Fail)
        if (!m_platformTrendData.contains(originalKey) || !m_platformTrendData[originalKey].contains(platformIdx)) return;

        int pass = m_platformTrendData[originalKey][platformIdx].first;
        int fail = m_platformTrendData[originalKey][platformIdx].second;
        total = pass + fail;

        tipPassFail = QString(
            "<div style='margin-bottom:12px;line-height:1.8'>"
            "<div style='font-size:17px'><span style='color:#4ade80'>OK : %1</span></div>"
            "<div style='font-size:17px'><span style='color:#f87171'>NG : %2</span></div>"
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

void Defect_Data_Display::showByTimeChartTooltip(const QPoint& viewportPos, const QPointF& chartPos)
{
    if (!m_chartViewPlatformByTime) return;
    QChart* chart = ((QChartView*)m_chartViewPlatformByTime)->chart();
    QList<QAbstractSeries*> allSeries = chart->series();
    if (allSeries.isEmpty()) return;

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

    // AOI result colors (OK=green, NG=red)
    QList<QColor> resultColors;
    resultColors << QColor(0, 255, 136) << QColor(255, 80, 80);

    // Get time category from X position
    int numTimeCategories = categories.size();
    int timeCategoryIndex = static_cast<int>(relX * numTimeCategories);
    if (timeCategoryIndex < 0 || timeCategoryIndex >= categories.size()) return;
    QString timeKey = categories[timeCategoryIndex];

    // Get original key for this time category
    QString originalKey = m_byTimeCategoryMap.value(timeKey);
    if (originalKey.isEmpty()) return;

    // Build aggregated tooltip content
    QString tooltipContent;
    int grandTotal = 0;
    QStringList aoiBreakdown;

    // Aggregate data across all 4 platforms
    for (int p = 0; p < 4; ++p) {
        if (!m_platformAoiResultData.isEmpty()) {
            if (m_platformAoiResultData.contains(originalKey) && m_platformAoiResultData[originalKey].contains(p)) {
                const QMap<QString, int>& aoiMap = m_platformAoiResultData[originalKey][p];
                for (auto ait = aoiMap.constBegin(); ait != aoiMap.constEnd(); ++ait) {
                    if (ait.value() > 0) {
                        // Find or create entry for this AOI result type
                        QString resultKey = ait.key();
                        bool found = false;
                        for (int i = 0; i < aoiBreakdown.size(); ++i) {
                            QString existing = aoiBreakdown[i];
                            if (existing.startsWith(resultKey + "::")) {
                                // Parse existing count and add to it
                                QStringList parts = existing.split("::");
                                if (parts.size() >= 2) {
                                    int existingCount = parts[1].toInt();
                                    int newCount = existingCount + ait.value();
                                    aoiBreakdown[i] = resultKey + "::" + QString::number(newCount);
                                }
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            aoiBreakdown.append(resultKey + "::" + QString::number(ait.value()));
                        }
                        grandTotal += ait.value();
                    }
                }
            }
        } else if (!m_platformTrendData.isEmpty()) {
            if (m_platformTrendData.contains(originalKey) && m_platformTrendData[originalKey].contains(p)) {
                QPair<int, int> passFail = m_platformTrendData[originalKey][p];
                // Aggregate Pass -> OK
                if (passFail.first > 0) {
                    bool found = false;
                    for (int i = 0; i < aoiBreakdown.size(); ++i) {
                        if (aoiBreakdown[i].startsWith("OK::")) {
                            QStringList parts = aoiBreakdown[i].split("::");
                            if (parts.size() >= 2) {
                                int existingCount = parts[1].toInt();
                                aoiBreakdown[i] = "OK::" + QString::number(existingCount + passFail.first);
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        aoiBreakdown.append("OK::" + QString::number(passFail.first));
                    }
                    grandTotal += passFail.first;
                }
                // Aggregate Fail -> NG
                if (passFail.second > 0) {
                    bool found = false;
                    for (int i = 0; i < aoiBreakdown.size(); ++i) {
                        if (aoiBreakdown[i].startsWith("NG::")) {
                            QStringList parts = aoiBreakdown[i].split("::");
                            if (parts.size() >= 2) {
                                int existingCount = parts[1].toInt();
                                aoiBreakdown[i] = "NG::" + QString::number(existingCount + passFail.second);
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        aoiBreakdown.append("NG::" + QString::number(passFail.second));
                    }
                    grandTotal += passFail.second;
                }
            }
        }
    }

    // Build the AOI breakdown HTML (OK/NG display with fixed colors)
    QString aoiHtml;
    int okCount = 0, ngCount = 0;
    for (int i = 0; i < aoiBreakdown.size(); ++i) {
        QStringList parts = aoiBreakdown[i].split("::");
        if (parts.size() >= 2) {
            QString resultName = parts[0];
            int count = parts[1].toInt();
            if (resultName == "OK") okCount = count;
            else if (resultName == "NG") ngCount = count;
        }
    }
    // Display OK first (green), then NG (red)
    if (okCount > 0) {
        aoiHtml += QString("<div style='font-size:14px;margin-left:15px'><span style='color:%1'>OK : %2</span></div>")
            .arg(QColor(0, 255, 136).name()).arg(okCount);
    }
    if (ngCount > 0) {
        aoiHtml += QString("<div style='font-size:14px;margin-left:15px'><span style='color:%1'>NG : %2</span></div>")
            .arg(QColor(255, 80, 80).name()).arg(ngCount);
    }

    QString tip = QString(
        "<div style='background:#1e2a3a;color:#fff;padding:16px 20px;border-radius:12px;"
        "border:1px solid #3a4a5a;min-width:300px;font-family:Arial,sans-serif'>"
        "<div style='border-bottom:1px solid #3a4a5a;padding-bottom:10px;margin-bottom:10px'>"
        "<div style='color:#00d9ff;font-size:14px'>Time: %1</div>"
        "<span style='font-size:14px;color:#aaaaaa'>Total : </span>"
        "<span style='font-size:22px;font-weight:bold;color:#ffffff'>%2</span>"
        "</div>"
        "%3"
        "</div>"
    ).arg(timeKey).arg(grandTotal).arg(aoiHtml);

    m_tooltipLabel->setText(tip);

    // Position tooltip near the mouse
    QPoint globalPos = m_chartViewPlatformByTime->viewport()->mapToGlobal(viewportPos);
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

void Defect_Data_Display::onMaximizeClicked()
{
    if (m_isMaximized) {
        showNormal();
        m_isMaximized = false;
        ui.btnMaximize->setText(QStringLiteral("\u25A1")); // □
    } else {
        showMaximized();
        m_isMaximized = true;
        ui.btnMaximize->setText(QStringLiteral("\u2750")); // ❐
    }
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
    ui.searchResultTable->setColumnCount(8);
    ui.searchResultTable->setAlternatingRowColors(true);
    ui.searchResultTable->verticalHeader()->setVisible(false);
    ui.searchResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui.searchResultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.searchResultTable->horizontalHeader()->setStretchLastSection(true);
    for (int i = 0; i < 8; ++i)
        ui.searchResultTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);

    // Set column headers
    QStringList headers = {"Code_AOI", "Grade_AOI", "Defect_Type", "PlatformID", "Inspect_Time", "Result", "MarkID", "StopTime"};
    for (int i = 0; i < headers.size() && i < ui.searchResultTable->columnCount(); ++i)
        ui.searchResultTable->setHorizontalHeaderItem(i, new QTableWidgetItem(headers[i]));

    // Connect table cell click to open image preview
    disconnect(ui.searchResultTable, nullptr, this, nullptr);
    connect(ui.searchResultTable, &QTableWidget::cellClicked, this, [=](int row, int /*col*/) {
        QTableWidgetItem* timeItem = ui.searchResultTable->item(row, 4);
        if (!timeItem) {
            return;
        }

        QString selectedScreenId = timeItem->data(Qt::UserRole).toString();
        QString localIp = timeItem->data(Qt::UserRole + 1).toString();
        int platformId = timeItem->data(Qt::UserRole + 2).toInt();
        QDateTime startTime = timeItem->data(Qt::UserRole + 3).toDateTime();
        showInspectionImageDialog(selectedScreenId, platformId, localIp, startTime);
    });

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
            ir.AOIResult,
            ir.MarkID,
            ir.StopTime,
            ir.LocalIP
        FROM ivs_lcd_inspectionresult ir
        WHERE ir.ScreenID = '%1'
        GROUP BY ir.Code_AOI, ir.Grade_AOI, ir.PlatformID, ir.AOIResult, ir.MarkID, ir.StopTime, ir.LocalIP, DATE(ir.StartTime), HOUR(ir.StartTime)
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
        int markId = query.value(6).toInt();
        QDateTime stopTime = query.value(7).toDateTime();
        QString localIp = query.value(8).toString();

        ui.searchResultTable->setItem(row, 0, new QTableWidgetItem(codeAoi));
        ui.searchResultTable->setItem(row, 1, new QTableWidgetItem(gradeAoi));
        ui.searchResultTable->setItem(row, 2, new QTableWidgetItem(QString::number(cnt)));
        ui.searchResultTable->setItem(row, 3, new QTableWidgetItem(QString::number(platformId + 1)));

        QTableWidgetItem* timeItem = new QTableWidgetItem(startTime.toString("yyyy-MM-dd HH:mm:ss"));
        timeItem->setForeground(QBrush(QColor(100, 180, 220)));
        timeItem->setFont(QFont(ui.searchResultTable->font().family(), 12));
        timeItem->setData(Qt::UserRole, screenId);
        timeItem->setData(Qt::UserRole + 1, localIp);
        timeItem->setData(Qt::UserRole + 2, platformId);
        timeItem->setData(Qt::UserRole + 3, startTime);
        ui.searchResultTable->setItem(row, 4, timeItem);

        QTableWidgetItem* resultItem = new QTableWidgetItem(aoiResult);
        if (aoiResult == "OK") {
            resultItem->setForeground(QBrush(QColor(0, 255, 136)));
        } else {
            resultItem->setForeground(QBrush(QColor(128, 100, 100)));
        }
        ui.searchResultTable->setItem(row, 5, resultItem);

        ui.searchResultTable->setItem(row, 6, new QTableWidgetItem(markId > 0 ? QString::number(markId) : ""));

        QTableWidgetItem* stopTimeItem = new QTableWidgetItem(stopTime.isValid() ? stopTime.toString("yyyy-MM-dd HH:mm:ss") : "");
        stopTimeItem->setForeground(QBrush(QColor(255, 180, 100)));
        stopTimeItem->setFont(QFont(ui.searchResultTable->font().family(), 12));
        ui.searchResultTable->setItem(row, 7, stopTimeItem);

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

    // If currently on location abnormal tab (index 4), reload data immediately
    int currentTabIndex = ui.tabWidget->currentIndex();
    if (currentTabIndex == 4) {
        QString timeRange = ui.comboTimeRange->currentText();
        loadLocationAbnormalDataAsync(timeRange);
    }

    // If currently on "按时间" (by-time) sub-tab, update the by-time chart
    if (currentTabIndex == 0 && ui.tabPlatformPages->currentIndex() == 0) {
        qDebug() << "Date changed while on by-time tab, updating chart";
        if (m_chartViewPlatformByTime) {
            updatePlatformByTimeChart();
        }
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
        case 0: {
            // Platform chart tab (index 0)
            // Check if "按时间" sub-tab (index 0) is selected
            int rawPlatformTabIndex = ui.tabPlatformPages->currentIndex();
            qInfo() << "onTabChanged platform page" << "rawIndex=" << rawPlatformTabIndex;

            if (rawPlatformTabIndex == 0) {
                // "按时间" tab selected - call updatePlatformByTimeChart
                qDebug() << "Index 0: By-time tab selected, scheduling chart update";
                QTimer::singleShot(50, this, [this]() {
                    if (!m_chartViewPlatformByTime) {
                        qWarning() << "By-time chart view is null during delayed update";
                        return;
                    }
                    qInfo() << "Updating by-time chart from onTabChanged delayed task";
                    updatePlatformByTimeChart();
                });
                break;
            }

            int platformArrayIndex = rawPlatformTabIndex - 1;
            if (platformArrayIndex < 0 || platformArrayIndex >= 4) {
                qWarning() << "Invalid platform tab index in onTabChanged"
                           << "rawIndex=" << rawPlatformTabIndex
                           << "mappedIndex=" << platformArrayIndex;
                break;
            }

            // For platform tabs 1-4, update the platform-specific charts
            qDebug() << "Index 0: Platform" << rawPlatformTabIndex << "tab switched to, scheduling label update";
            qInfo() << "Scheduling platform label redraw"
                    << "rawIndex=" << rawPlatformTabIndex
                    << "mappedIndex=" << platformArrayIndex;

            // Get the chart for this platform
            void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
            QChartView* chartView = (QChartView*)chartViewPtrs[platformArrayIndex];
            if (!chartView) {
                qWarning() << "Platform chart view is null in onTabChanged" << "mappedIndex=" << platformArrayIndex;
                break;
            }
            QChart* chart = chartView->chart();
            if (!chart) {
                qWarning() << "Platform chart is null in onTabChanged" << "mappedIndex=" << platformArrayIndex;
                break;
            }

            // Clear old text items
            if (chart->scene()) {
                QList<QGraphicsItem*> items = chart->scene()->items();
                for (QGraphicsItem* item : items) {
                    if (qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item)) {
                        chart->scene()->removeItem(item);
                        delete item;
                    }
                }
            }

            QPointer<QChartView> safeChartView(chartView);
            QPointer<QChart> safeChart(chart);

            // Wait for layout and redraw labels
            QTimer::singleShot(100, this, [this, safeChartView, safeChart, rawPlatformTabIndex, platformArrayIndex]() {
                if (!safeChartView) {
                    qWarning() << "Delayed platform label redraw skipped: chartView destroyed" << "mappedIndex=" << platformArrayIndex;
                    return;
                }
                if (!safeChart) {
                    qWarning() << "Delayed platform label redraw skipped: chart destroyed" << "mappedIndex=" << platformArrayIndex;
                    return;
                }
                QChart* chart = safeChart.data();
                if (!chart->scene()) {
                    qWarning() << "Delayed platform label redraw skipped: chart scene null" << "mappedIndex=" << platformArrayIndex;
                    return;
                }
                QRectF plotArea = chart->plotArea();
                qDebug() << "[TabSwitch] Platform" << rawPlatformTabIndex << "plotArea after switch:" << plotArea;
                qInfo() << "Delayed platform label redraw running"
                        << "rawIndex=" << rawPlatformTabIndex
                        << "mappedIndex=" << platformArrayIndex
                        << "plotArea=" << plotArea;

                if (plotArea.width() < 200 || plotArea.height() < 50) {
                    qDebug() << "[TabSwitch] Platform" << rawPlatformTabIndex << "plotArea still invalid, skipping labels";
                    return;
                }

                // Get time categories from axis
                QList<QAbstractAxis*> axesX = chart->axes(Qt::Horizontal);
                QStringList timeCategories;
                if (!axesX.isEmpty()) {
                    QBarCategoryAxis* axisX = qobject_cast<QBarCategoryAxis*>(axesX.first());
                    if (axisX) timeCategories = axisX->categories();
                }

                // Get column totals from the stacked series
                QList<QAbstractSeries*> seriesList = chart->series();
                if (seriesList.isEmpty()) {
                    qWarning() << "Delayed platform label redraw skipped: no series" << "mappedIndex=" << platformArrayIndex;
                    return;
                }
                QStackedBarSeries* stackedSeries = qobject_cast<QStackedBarSeries*>(seriesList.first());
                if (!stackedSeries) {
                    qWarning() << "Delayed platform label redraw skipped: first series is not stacked bar" << "mappedIndex=" << platformArrayIndex;
                    return;
                }

                // Calculate totals from the bar sets
                QList<int> columnTotals(timeCategories.size(), 0);
                QList<QBarSet*> barSets = stackedSeries->barSets();
                for (QBarSet* barSet : barSets) {
                    for (int ti = 0; ti < timeCategories.size() && ti < barSet->count(); ++ti) {
                        columnTotals[ti] += (int)barSet->at(ti);
                    }
                }

                // Get Y axis
                QList<QAbstractAxis*> axesY = chart->axes(Qt::Vertical);
                if (axesY.isEmpty()) {
                    qWarning() << "Delayed platform label redraw skipped: no Y axis" << "mappedIndex=" << platformArrayIndex;
                    return;
                }
                QValueAxis* axisY = qobject_cast<QValueAxis*>(axesY.first());
                if (!axisY) {
                    qWarning() << "Delayed platform label redraw skipped: Y axis cast failed" << "mappedIndex=" << platformArrayIndex;
                    return;
                }

                // Draw labels
                qDebug() << "[TabSwitch] Platform" << rawPlatformTabIndex << "Drawing" << columnTotals.size() << "labels";
                int numBars = timeCategories.size();
                if (numBars <= 0) {
                    qWarning() << "Delayed platform label redraw skipped: no categories" << "mappedIndex=" << platformArrayIndex;
                    return;
                }
                qreal barGroupWidth = plotArea.width() / numBars;
                qreal barWidth = barGroupWidth * 0.7;
                qreal barLeftMargin = barGroupWidth * 0.15;
                qreal yMin = axisY->min();
                qreal yMax = axisY->max();
                qreal yRange = yMax - yMin;
                if (qFuzzyIsNull(yRange)) {
                    qWarning() << "Delayed platform label redraw skipped: yRange is zero" << "mappedIndex=" << platformArrayIndex;
                    return;
                }

                for (int ti = 0; ti < numBars; ++ti) {
                    if (columnTotals[ti] <= 0) continue;

                    qreal barCenterX = plotArea.left() + barLeftMargin + ti * barGroupWidth + barWidth / 2;
                    qreal normalizedValue = (columnTotals[ti] - yMin) / yRange;
                    qreal barTopY = plotArea.bottom() - normalizedValue * plotArea.height();

                    QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(QString::number(columnTotals[ti]));
                    textItem->setFont(QFont("Arial", 9, QFont::Bold));
                    textItem->setBrush(QBrush(QColor(255, 255, 255)));
                    textItem->setZValue(100);
                    chart->scene()->addItem(textItem);

                    qreal textX = barCenterX - textItem->boundingRect().width() / 2;
                    qreal textY = barTopY - textItem->boundingRect().height() - 3;
                    textItem->setPos(textX, textY);
                }
            });
            break;
        }
        case 1: {
            qDebug() << "Index 1: Trend";
            CachedTabData* cache = &m_trendCache;
            if (isCacheValid(cache, timeRange, m_selectedDate)) {
                updateTrendChart(cache->trendDataByGrade, cache->defectRatesByGrade, cache->allGrades, timeRange);
            } else {
                loadTrendDataAsync(timeRange);
            }
            break;
        }
        case 2: {
            qDebug() << "Index 2: Detail";
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
        case 3: {
            // Search results tab
            // No additional loading needed, data is searched on demand
            qDebug() << "Index 3: Search results (no auto-load)";
            break;
        }
        case 4: {  // TAB_LOCATION_ABNORMAL
            qDebug() << "=== Index 4: Location Abnormal - LOADING DATA ===";
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

void Defect_Data_Display::onPlatformTabChanged(int index)
{
    qInfo() << "onPlatformTabChanged" << "rawIndex=" << index;
    qDebug() << "=== onPlatformTabChanged called with index:" << index << "===";

    // Index 0 = "按时间" (aggregate view)
    if (index == 0) {
        if (!m_chartViewPlatformByTime) return;
        updatePlatformByTimeChart();
        return;
    }

    // Platform tab (index 1-4 corresponds to platform 0-3)
    if (index < 1 || index > 4) return;

    int platformIdx = index - 1;
    qInfo() << "Resolved platform tab index" << "rawIndex=" << index << "mappedIndex=" << platformIdx;

    // Get the chart for this platform
    void* chartViewPtrs[4] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
    QChartView* chartView = (QChartView*)chartViewPtrs[platformIdx];
    if (!chartView) {
        qWarning() << "Platform chart view is null in onPlatformTabChanged" << "mappedIndex=" << platformIdx;
        return;
    }
    QChart* chart = chartView->chart();
    if (!chart) {
        qWarning() << "Platform chart is null in onPlatformTabChanged" << "mappedIndex=" << platformIdx;
        return;
    }

    // Clear old text items
    if (chart->scene()) {
        QList<QGraphicsItem*> items = chart->scene()->items();
        for (QGraphicsItem* item : items) {
            if (qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item)) {
                chart->scene()->removeItem(item);
                delete item;
            }
        }
    }

    QPointer<QChartView> safeChartView(chartView);
    QPointer<QChart> safeChart(chart);

    // Wait for layout and redraw labels
    QTimer::singleShot(100, this, [this, safeChartView, safeChart, index, platformIdx]() {
        if (!safeChartView) {
            qWarning() << "Platform tab delayed redraw skipped: chartView destroyed" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }
        if (!safeChart) {
            qWarning() << "Platform tab delayed redraw skipped: chart destroyed" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }
        QChart* chart = safeChart.data();
        if (!chart->scene()) {
            qWarning() << "Platform tab delayed redraw skipped: chart scene null" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }
        QRectF plotArea = chart->plotArea();
        qDebug() << "[PlatformTab] Platform" << index << "plotArea after switch:" << plotArea;

        if (plotArea.width() < 200 || plotArea.height() < 50) {
            qDebug() << "[PlatformTab] Platform" << index << "plotArea still invalid, skipping labels";
            return;
        }

        // Get time categories from axis
        QList<QAbstractAxis*> axesX = chart->axes(Qt::Horizontal);
        QStringList timeCategories;
        if (!axesX.isEmpty()) {
            QBarCategoryAxis* axisX = qobject_cast<QBarCategoryAxis*>(axesX.first());
            if (axisX) timeCategories = axisX->categories();
        }

        // Get column totals from the stacked series
        QList<QAbstractSeries*> seriesList = chart->series();
        if (seriesList.isEmpty()) {
            qWarning() << "Platform tab delayed redraw skipped: no series" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }
        QStackedBarSeries* stackedSeries = qobject_cast<QStackedBarSeries*>(seriesList.first());
        if (!stackedSeries) {
            qWarning() << "Platform tab delayed redraw skipped: first series is not stacked bar" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }

        // Calculate totals from the bar sets
        QList<int> columnTotals(timeCategories.size(), 0);
        QList<QBarSet*> barSets = stackedSeries->barSets();
        for (QBarSet* barSet : barSets) {
            for (int ti = 0; ti < timeCategories.size() && ti < barSet->count(); ++ti) {
                columnTotals[ti] += (int)barSet->at(ti);
            }
        }

        // Get Y axis
        QList<QAbstractAxis*> axesY = chart->axes(Qt::Vertical);
        if (axesY.isEmpty()) {
            qWarning() << "Platform tab delayed redraw skipped: no Y axis" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }
        QValueAxis* axisY = qobject_cast<QValueAxis*>(axesY.first());
        if (!axisY) {
            qWarning() << "Platform tab delayed redraw skipped: Y axis cast failed" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }

        // Draw labels
        qDebug() << "[PlatformTab] Platform" << index << "Drawing" << columnTotals.size() << "labels";
        int numBars = timeCategories.size();
        if (numBars <= 0) {
            qWarning() << "Platform tab delayed redraw skipped: no categories" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }
        qreal barGroupWidth = plotArea.width() / numBars;
        qreal barWidth = barGroupWidth * 0.7;
        qreal barLeftMargin = barGroupWidth * 0.15;
        qreal yMin = axisY->min();
        qreal yMax = axisY->max();
        qreal yRange = yMax - yMin;
        if (qFuzzyIsNull(yRange)) {
            qWarning() << "Platform tab delayed redraw skipped: yRange is zero" << "rawIndex=" << index << "mappedIndex=" << platformIdx;
            return;
        }

        for (int ti = 0; ti < numBars; ++ti) {
            if (columnTotals[ti] <= 0) continue;

            qreal barCenterX = plotArea.left() + barLeftMargin + ti * barGroupWidth + barWidth / 2;
            qreal normalizedValue = (columnTotals[ti] - yMin) / yRange;
            qreal barTopY = plotArea.bottom() - normalizedValue * plotArea.height();

            QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(QString::number(columnTotals[ti]));
            textItem->setFont(QFont("Arial", 9, QFont::Bold));
            textItem->setBrush(QBrush(QColor(255, 255, 255)));
            textItem->setZValue(100);
            chart->scene()->addItem(textItem);

            qreal textX = barCenterX - textItem->boundingRect().width() / 2;
            qreal textY = barTopY - textItem->boundingRect().height() - 3;
            textItem->setPos(textX, textY);
        }
    });
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

    // Create "按时间" chart - aggregate of all platforms by time
    QChart* chartByTime = new QChart();
    chartByTime->setTitle("缺陷统计 (按时间)");
    chartByTime->setAnimationOptions(QChart::NoAnimation);
    chartByTime->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartByTime->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartByTime->legend()->setLabelColor(QColor(234, 234, 234));
    chartByTime->legend()->setAlignment(Qt::AlignTop);
    chartByTime->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartByTime->setMargins(QMargins(0, 0, 0, 0));
    m_chartViewPlatformByTime = new QChartView(chartByTime);
    ((QChartView*)m_chartViewPlatformByTime)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewPlatformByTime)->setMinimumHeight(600);
    ((QChartView*)m_chartViewPlatformByTime)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    ((QChartView*)m_chartViewPlatformByTime)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layoutByTime = new QVBoxLayout(ui.chartPlatformByTime);
    layoutByTime->setSpacing(0);
    layoutByTime->setContentsMargins(0, 0, 0, 0);
    layoutByTime->addWidget((QChartView*)m_chartViewPlatformByTime);

    // Hidden: tabDefect - AOI Defect Analysis chart
    /*
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
    */

    // Hidden: tabInspection - Inspection Pass/Fail charts
    /*
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
    */

    // Hidden: tabMapping - Defect Position Map chart
    /*
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
    */

    QChart* chartTrend = new QChart();
    chartTrend->setTitle("Defect Count Trend");
    chartTrend->setAnimationOptions(QChart::NoAnimation);
    chartTrend->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartTrend->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartTrend->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewTrend = new QChartView(chartTrend);
    ((QChartView*)m_chartViewTrend)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewTrend)->setMinimumHeight(200);
    ((QChartView*)m_chartViewTrend)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutTrend = new QVBoxLayout(ui.chartTrend);
    layoutTrend->setContentsMargins(0, 0, 0, 2);
    layoutTrend->setSpacing(2);
    layoutTrend->addWidget((QChartView*)m_chartViewTrend, 2);

    QChart* chartDefectRate = new QChart();
    chartDefectRate->setTitle("Defect Rate Trend");
    chartDefectRate->setAnimationOptions(QChart::NoAnimation);
    chartDefectRate->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartDefectRate->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chartDefectRate->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewDefectRate = new QChartView(chartDefectRate);
    ((QChartView*)m_chartViewDefectRate)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewDefectRate)->setMinimumHeight(200);
    ((QChartView*)m_chartViewDefectRate)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutDefectRate = new QVBoxLayout(ui.chartDefectRate);
    layoutDefectRate->setContentsMargins(0, 2, 0, 2);
    layoutDefectRate->setSpacing(2);
    layoutDefectRate->addWidget((QChartView*)m_chartViewDefectRate, 2);

    // Y2 Count Trend Chart
    QChart* chartTrendY2 = new QChart();
    chartTrendY2->setTitle("Y2 Count Trend");
    chartTrendY2->setAnimationOptions(QChart::NoAnimation);
    chartTrendY2->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartTrendY2->setTitleBrush(QBrush(QColor(255, 105, 180)));
    chartTrendY2->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewTrendY2 = new QChartView(chartTrendY2);
    ((QChartView*)m_chartViewTrendY2)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewTrendY2)->setMinimumHeight(235);
    ((QChartView*)m_chartViewTrendY2)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutTrendY2 = new QVBoxLayout(ui.chartTrendY2);
    layoutTrendY2->setContentsMargins(0, 2, 0, 2);
    layoutTrendY2->setSpacing(2);
    layoutTrendY2->addWidget((QChartView*)m_chartViewTrendY2, 2);

    // Y2 Rate Trend Chart
    QChart* chartDefectRateY2 = new QChart();
    chartDefectRateY2->setTitle("Y2 Rate Trend (%)");
    chartDefectRateY2->setAnimationOptions(QChart::NoAnimation);
    chartDefectRateY2->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartDefectRateY2->setTitleBrush(QBrush(QColor(255, 105, 180)));
    chartDefectRateY2->legend()->setLabelColor(QColor(234, 234, 234));

    m_chartViewDefectRateY2 = new QChartView(chartDefectRateY2);
    ((QChartView*)m_chartViewDefectRateY2)->setRenderHint(QPainter::Antialiasing);
    ((QChartView*)m_chartViewDefectRateY2)->setMinimumHeight(235);
    ((QChartView*)m_chartViewDefectRateY2)->setBackgroundBrush(QBrush(QColor(22, 33, 62)));

    QVBoxLayout* layoutDefectRateY2 = new QVBoxLayout(ui.chartDefectRateY2);
    layoutDefectRateY2->setContentsMargins(0, 2, 0, 0);
    layoutDefectRateY2->setSpacing(2);
    layoutDefectRateY2->addWidget((QChartView*)m_chartViewDefectRateY2, 2);

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

    // Clear cached time-based chart data so a no-data refresh cannot reuse previous date results
    m_platformTrendData.clear();
    m_platformAoiResultData.clear();
    m_defectTrendData.clear();
    m_inspectionTrendData.clear();
    m_trendDataY2.clear();
    m_trendCache = CachedTabData();
    m_detailCache = CachedTabData();
    clearPlatformStatsView();
    clearDetailView();

    // Disable refresh button and time controls while loading
    ui.btnRefresh->setEnabled(false);
    ui.comboTimeRange->setEnabled(false);
    ui.dateEdit->setEnabled(false);
    ui.timeEditStart->setEnabled(false);
    ui.timeEditEnd->setEnabled(false);

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
    // Pass time range filter for hourly mode
    int startHour = (timeRange == "按小时") ? ui.timeEditStart->currentIndex() : -1;
    int endHour = (timeRange == "按小时") ? ui.timeEditEnd->currentIndex() : -1;
    m_workerThread = new DataLoaderThread(thisLoadId, timeRange, getDateTimeRange(timeRange), m_searchScreenId, this, startHour, endHour);

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
    connect(m_workerThread, &DataLoaderThread::platformGradeTrendLoaded,
            this, &Defect_Data_Display::onDataLoaded_PlatformGradeTrend, Qt::QueuedConnection);

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
    auto clearChart = [](QChart* chart) {
        if (!chart) return;
        chart->removeAllSeries();
        for (QAbstractAxis* axis : chart->axes()) {
            chart->removeAxis(axis);
            axis->deleteLater();
        }
        if (chart->scene()) {
            const QList<QGraphicsItem*> items = chart->scene()->items();
            for (QGraphicsItem* item : items) {
                if (!item) continue;
                const QVariant itemTag = item->data(0);
                if (itemTag == QStringLiteral("customBarValueLabel")
                    || itemTag == QStringLiteral("customLinePointLabel")) {
                    delete item;
                }
            }
        }
    };

    // Clear platform charts
    void* platformCharts[] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
    for (int i = 0; i < 4; ++i) {
        clearChart(((QChartView*)platformCharts[i])->chart());
    }

    // Clear AOI chart (hidden tab)
    if (m_chartViewAoi) {
        clearChart(((QChartView*)m_chartViewAoi)->chart());
    }

    // Clear inspection charts (hidden tab)
    if (m_chartViewInspectionPass) {
        clearChart(((QChartView*)m_chartViewInspectionPass)->chart());
    }

    if (m_chartViewInspectionFail) {
        clearChart(((QChartView*)m_chartViewInspectionFail)->chart());
    }

    // Clear defect mapping chart (hidden tab)
    if (m_chartViewDefectMapping) {
        clearChart(((QChartView*)m_chartViewDefectMapping)->chart());
    }

    // Clear trend charts
    clearChart(((QChartView*)m_chartViewTrend)->chart());
    clearChart(((QChartView*)m_chartViewDefectRate)->chart());
    if (m_chartViewTrendY2) {
        clearChart(((QChartView*)m_chartViewTrendY2)->chart());
    }
    if (m_chartViewDefectRateY2) {
        clearChart(((QChartView*)m_chartViewDefectRateY2)->chart());
    }
}

void Defect_Data_Display::clearDetailView()
{
    m_detailPieData.clear();
    m_detailPieTitle.clear();

    if (ui.chartPieDetail->layout()) {
        while (QLayoutItem* item = ui.chartPieDetail->layout()->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    if (ui.chartDetail->layout()) {
        while (QLayoutItem* item = ui.chartDetail->layout()->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    m_chartViewPieDetail = nullptr;
    m_chartViewDetail = nullptr;
}

void Defect_Data_Display::clearPlatformStatsView()
{
    m_platformTrendData.clear();
    m_platformAoiResultData.clear();
    m_platformGradeTrendData.clear();
    m_aoiResultCategories.clear();
    m_byTimeCategoryMap.clear();
    m_byTimePlatformTotals.clear();

    auto clearPlatformChart = [](QChartView* chartView) {
        if (!chartView || !chartView->chart()) return;
        QChart* chart = chartView->chart();
        if (chart->scene()) {
            const QList<QGraphicsItem*> items = chart->scene()->items();
            for (QGraphicsItem* item : items) {
                if (qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item)) {
                    chart->scene()->removeItem(item);
                    delete item;
                }
            }
        }
        chart->removeAllSeries();
        for (QAbstractAxis* axis : chart->axes()) {
            chart->removeAxis(axis);
            axis->deleteLater();
        }
    };

    QChartView* platformCharts[] = {m_chartViewPlatform0, m_chartViewPlatform1, m_chartViewPlatform2, m_chartViewPlatform3};
    for (QChartView* chartView : platformCharts) {
        clearPlatformChart(chartView);
    }
    clearPlatformChart(m_chartViewPlatformByTime);

    if (m_tooltipLabel) {
        m_tooltipLabel->hide();
    }
}

void Defect_Data_Display::onTimeRangeChanged(int index)
{
    Q_UNUSED(index);

    // Invalidate location abnormal cache when time range changes
    m_locationAbnormalCache.timestamp = 0;

    // If currently on location abnormal tab (index 4), reload data immediately
    int currentTabIndex = ui.tabWidget->currentIndex();
    if (currentTabIndex == 4) {
        QString timeRange = ui.comboTimeRange->currentText();
        loadLocationAbnormalDataAsync(timeRange);
    }

    // Always refresh main page data when time range changes
    onRefreshClicked();
}

void Defect_Data_Display::onTimeRangeChangedForSearch(int index)
{
    // Only enable time filters for "按小时" mode (index 0)
    bool enableTimeFilter = (index == 0);
    ui.timeEditStart->setEnabled(enableTimeFilter);
    ui.timeEditEnd->setEnabled(enableTimeFilter);
    ui.labelStartTime->setEnabled(enableTimeFilter);
    ui.labelEndTime->setEnabled(enableTimeFilter);
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
    // Re-enable time edits only if in hourly mode
    QString timeRange = ui.comboTimeRange->currentText();
    bool enableTimeFilter = (timeRange == "按小时");
    ui.timeEditStart->setEnabled(enableTimeFilter);
    ui.timeEditEnd->setEnabled(enableTimeFilter);
    ui.labelStartTime->setEnabled(enableTimeFilter);
    ui.labelEndTime->setEnabled(enableTimeFilter);
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
    if (platformStats.isEmpty()) {
        clearPlatformStatsView();
        return;
    }
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

void Defect_Data_Display::onDataLoaded_Trend(const QMap<QString, QMap<QString, int>>& trendData, const QMap<QString, QMap<QString, double>>& defectRates, const QStringList& allGrades)
    {

    // Build m_trendDataY2 from the received trendData
    m_trendDataY2.clear();
    QMap<QString, int> totalPerPeriod;
    for (auto periodIt = trendData.constBegin(); periodIt != trendData.constEnd(); ++periodIt) {
        int periodTotal = 0;
        for (auto gradeIt = periodIt.value().constBegin(); gradeIt != periodIt.value().constEnd(); ++gradeIt) {
            periodTotal += gradeIt.value();
        }
        totalPerPeriod[periodIt.key()] = periodTotal;
    }
    for (auto periodIt = trendData.constBegin(); periodIt != trendData.constEnd(); ++periodIt) {
        QString period = periodIt.key();
        int y2Count = periodIt.value().value("Y2", 0);
        int totalCount = totalPerPeriod.value(period, 0);
        if (y2Count > 0 || totalCount > 0) {
            m_trendDataY2[period] = qMakePair(y2Count, totalCount);
        }
    }
    qDebug() << "[onDataLoaded_Trend] Built m_trendDataY2, entries:" << m_trendDataY2.size();
    if (!m_trendDataY2.isEmpty()) {
        for (auto it = m_trendDataY2.constBegin(); it != m_trendDataY2.constEnd(); ++it) {
            qDebug() << "  Y2 period:" << it.key() << "y2:" << it.value().first << "total:" << it.value().second;
        }
    }

    qDebug() << "=== onDataLoaded_Trend called ===" << "data points:" << trendData.size() << "grades:" << allGrades;

    m_trendCache.trendDataByGrade = trendData;
    m_trendCache.defectRatesByGrade = defectRates;
    m_trendCache.allGrades = allGrades;
    m_trendCache.timeRange = ui.comboTimeRange->currentText();
    m_trendCache.date = m_selectedDate;
    m_trendCache.startHour = (ui.comboTimeRange->currentText() == "按小时") ? qMin(m_searchStartHour, m_searchEndHour) : -1;
    m_trendCache.endHour = (ui.comboTimeRange->currentText() == "按小时") ? qMax(m_searchStartHour, m_searchEndHour) : -1;
    m_trendCache.timestamp = QDateTime::currentMSecsSinceEpoch();

    updateTrendChart(trendData, defectRates, allGrades, ui.comboTimeRange->currentText());
}

void Defect_Data_Display::onDataLoaded_Detail(const QList<QVariantList>& defectDetails)
{
    qDebug() << "=== onDataLoaded_Detail called ===" << "records:" << defectDetails.size();

    if (defectDetails.isEmpty()) {
        m_detailCache = CachedTabData();
        clearDetailView();
        return;
    }

    m_detailCache.defectDetails = defectDetails;
    m_detailCache.timeRange = ui.comboTimeRange->currentText();
    m_detailCache.date = m_selectedDate;
    m_detailCache.startHour = (ui.comboTimeRange->currentText() == "按小时") ? qMin(m_searchStartHour, m_searchEndHour) : -1;
    m_detailCache.endHour = (ui.comboTimeRange->currentText() == "按小时") ? qMax(m_searchStartHour, m_searchEndHour) : -1;
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

    // Only draw chart here if we don't have AOIResult data
    // Otherwise, wait for onDataLoaded_PlatformAoiResult to draw the stacked chart
    if (m_platformAoiResultData.isEmpty()) {
        updatePlatformTrendChart(platformTrendData);
    } else {
        qDebug() << "Waiting for AOIResult data to draw stacked chart...";
    }

    // If user is on the by-time tab, also update the by-time chart
    if (ui.tabPlatformPages->currentIndex() == 0 && m_chartViewPlatformByTime) {
        updatePlatformByTimeChart();
    }
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

void Defect_Data_Display::onDataLoaded_PlatformGradeTrend(const QMap<QString, QMap<QString, int>>& gradeTrendData, const QStringList& allGrades, const QString& timeRange)
{
    qDebug() << "[GradeTrend] Received grade trend data:" << gradeTrendData.size() << "time periods, grades:" << allGrades;
    m_platformGradeTrendData = gradeTrendData;
    updatePlatformGradeTrendChart(gradeTrendData, allGrades, timeRange);
}

void Defect_Data_Display::updatePlatformGradeTrendChart(const QMap<QString, QMap<QString, int>>& gradeTrendData, const QStringList& allGrades, const QString& timeRange)
{
    qDebug() << "updatePlatformGradeTrendChart called with" << gradeTrendData.size() << "time periods, grades:" << allGrades;

    if (gradeTrendData.isEmpty()) {
        qDebug() << "No grade trend data";
        ((QChartView*)m_chartViewTrend)->chart()->removeAllSeries();
        return;
    }

    auto isY2Grade = [](const QString& gradeName) {
        return gradeName.trimmed().compare("Y2", Qt::CaseInsensitive) == 0;
    };

    // Sort grades: OK first, then R1-R5, then others alphabetically
    QStringList sortedGrades;
    for (const QString& grade : allGrades) {
        if (!isY2Grade(grade)) {
            sortedGrades.append(grade);
        }
    }
    std::sort(sortedGrades.begin(), sortedGrades.end(), [](const QString& a, const QString& b) {
        if (a == "OK") return true;
        if (b == "OK") return false;
        if (a.startsWith("R") && b.startsWith("R")) {
            bool aOk, bOk;
            int aNum = a.mid(1).toInt(&aOk);
            int bNum = b.mid(1).toInt(&bOk);
            if (aOk && bOk) return aNum < bNum;
        }
        return a < b;
    });

    // Sort time periods
    QStringList sortedTimes = gradeTrendData.keys();
    if (timeRange == "按小时" || timeRange == "按天") {
        std::sort(sortedTimes.begin(), sortedTimes.end(), [](const QString& a, const QString& b) {
            return a < b;
        });
    }

    // Format time labels for display
    QStringList timeLabels;
    for (const QString& timeKey : sortedTimes) {
        QString label = timeKey;
        if (timeRange == "按小时") {
            if (label.contains(" ")) {
                label = label.split(" ").at(1).left(5);
            }
        } else if (timeRange == "按天") {
            if (label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) {
                    label = parts.at(2);
                }
            }
        }
        timeLabels.append(label);
    }

    // Grade colors (same as pie/bar charts)
    QMap<QString, QColor> gradeColors;
    gradeColors["OK"] = QColor(0, 255, 136);
    gradeColors["R1"] = QColor(255, 68, 68);
    gradeColors["R2"] = QColor(255, 165, 0);
    gradeColors["R3"] = QColor(255, 215, 0);
    gradeColors["R4"] = QColor(50, 205, 50);
    gradeColors["R5"] = QColor(0, 191, 255);
    gradeColors["NG"] = QColor(255, 0, 0);
    QList<QColor> otherColors = {
        QColor(138, 43, 226), QColor(255, 20, 147), QColor(64, 224, 208),
        QColor(255, 127, 80), QColor(106, 90, 205), QColor(152, 251, 152)
    };

    // Clear existing chart
    QChart* chart = ((QChartView*)m_chartViewTrend)->chart();
    qDebug() << "[GradeTrend] chart ptr:" << chart << "series:" << chart->series().size() << "axes:" << chart->axes().size();
    chart->removeAllSeries();

    QList<QAbstractAxis*> existingAxes = chart->axes();
    qDebug() << "[GradeTrend] removing axes:" << existingAxes.size();
    for (QAbstractAxis* axis : existingAxes) {
        chart->removeAxis(axis);
        axis->deleteLater();
    }

    // Create one bar set per grade type
    QBarSeries* barSeries = new QBarSeries();
    barSeries->setBarWidth(1.0);
    int colorIdx = 0;
    int maxVal = 0;
    for (const QString& grade : sortedGrades) {
        QBarSet* barSet = new QBarSet(grade);

        QColor seriesColor;
        if (gradeColors.contains(grade)) {
            seriesColor = gradeColors[grade];
        } else {
            seriesColor = otherColors[colorIdx % otherColors.size()];
            colorIdx++;
        }
        barSet->setColor(seriesColor);
        barSet->setBorderColor(seriesColor.darker(130));
        barSet->setLabelColor(QColor(234, 234, 234));

        for (int i = 0; i < sortedTimes.size(); ++i) {
            QString timeKey = sortedTimes[i];
            int count = gradeTrendData[timeKey].value(grade, 0);
            *barSet << count;
            if (count > maxVal) {
                maxVal = count;
            }
        }

        barSeries->append(barSet);
    }

    chart->addSeries(barSeries);
    barSeries->setLabelsVisible(false);
    barSeries->setBarWidth(1.0);

    // Set chart title and style
    chart->setTitle("Grade_AOI 趋势分析 (" + timeRange + ")");
    chart->setAnimationOptions(QChart::AllAnimations);
    chart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->legend()->setLabelColor(QColor(234, 234, 234));
    chart->legend()->setFont(QFont("Arial", 9));
    chart->legend()->setAlignment(Qt::AlignRight);

    // X axis (time periods)
    qDebug() << "[GradeTrend] creating axes and attaching series";
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(timeLabels);
    axisX->setLabelsColor(QColor(234, 234, 234));
    QFont axisXFont = axisX->labelsFont();
    axisXFont.setPointSize(9);
    axisX->setLabelsFont(axisXFont);
    chart->addAxis(axisX, Qt::AlignBottom);

    // Y axis (count)
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Count");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    QFont axisYFont = axisY->labelsFont();
    axisYFont.setPointSize(10);
    axisY->setLabelsFont(axisYFont);
    axisY->setRange(0, 100);  // Will auto-adjust
    chart->addAxis(axisY, Qt::AlignLeft);

    // Attach axes to all series
    for (auto series : chart->series()) {
        series->attachAxis(axisX);
        series->attachAxis(axisY);
    }
    qDebug() << "[GradeTrend] attached axes to series:" << chart->series().size();

    if (maxVal < 1) maxVal = 1;
    axisY->setRange(0, maxVal + maxVal * 0.2);

    qDebug() << "updatePlatformGradeTrendChart completed successfully";
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
        const int startHour = qMin(m_searchStartHour, m_searchEndHour);
        const int endHour = qMax(m_searchStartHour, m_searchEndHour);
        const QString dateStr = m_selectedDate.toString("yyyy-MM-dd");
        return QString("StartTime >= '%1 %2:00:00' AND StartTime <= '%1 %3:59:59'")
            .arg(dateStr)
            .arg(startHour, 2, 10, QChar('0'))
            .arg(endHour, 2, 10, QChar('0'));
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
    const QString dateStr = m_selectedDate.toString("yyyy-MM-dd");
    return QString("StartTime >= '%1 00:00:00' AND StartTime <= '%1 23:59:59'")
        .arg(dateStr);
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
    if (!m_chartViewAoi) {
        qDebug() << "m_chartViewAoi not initialized, returning early";
        return;
    }

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
    series->setBarWidth(1.0);
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

    if (!m_chartViewDefectMapping) {
        qDebug() << "m_chartViewDefectMapping not initialized, returning early";
        return;
    }

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
            series->setMarkerSize(4);
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
        FROM ivs_lcd_aoidefect
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

    qDebug() << "Executing optimized trend query with Grade_AOI grouping...";

    // Query inspection results grouped by time period AND Grade_AOI
    QString trendQuery = QString(R"(
        SELECT
            %1 as time_period,
            Grade_AOI,
            COUNT(*) as cnt
        FROM ivs_lcd_inspectionresult
        WHERE %2
        GROUP BY time_period, Grade_AOI
        ORDER BY time_period, Grade_AOI
    )").arg(timeFormat).arg(dateRangeClause);

    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (!query.exec(trendQuery)) {
        qDebug() << "Trend query failed:" << query.lastError().text();
        return;
    }

    // Data structure: time_period -> (Grade_AOI -> count)
    QMap<QString, QMap<QString, int>> trendData;
    // Also track totals per time period for rate calculation
    QMap<QString, int> totalPerPeriod;
    QStringList allGrades;

    while (query.next()) {
        QString period = query.value(0).toString();
        QString grade = query.value(1).toString();
        int cnt = query.value(2).toInt();

        trendData[period][grade] = cnt;
        totalPerPeriod[period] += cnt;
        if (!allGrades.contains(grade)) {
            allGrades.append(grade);
        }
    }

    QStringList expectedPeriods;
    if (timeRange == "按小时") {
        const int startHour = qMin(m_searchStartHour, m_searchEndHour);
        const int endHour = qMax(m_searchStartHour, m_searchEndHour);
        const QString dateStr = m_selectedDate.toString("yyyy-MM-dd");
        for (int h = startHour; h <= endHour; ++h) {
            expectedPeriods.append(QString("%1 %2:00").arg(dateStr).arg(h, 2, 10, QChar('0')));
        }
    } else if (timeRange == "按天") {
        const QString monthStr = m_selectedDate.toString("yyyy-MM");
        const int daysInMonth = m_selectedDate.daysInMonth();
        for (int d = 1; d <= daysInMonth; ++d) {
            expectedPeriods.append(QString("%1-%2").arg(monthStr).arg(d, 2, 10, QChar('0')));
        }
    } else if (timeRange == "按月") {
        const int year = m_selectedDate.year();
        for (int m = 1; m <= 12; ++m) {
            expectedPeriods.append(QString("%1-%2").arg(year).arg(m, 2, 10, QChar('0')));
        }
    }

    for (const QString& period : expectedPeriods) {
        if (!trendData.contains(period)) {
            trendData[period] = QMap<QString, int>();
        }
        if (!totalPerPeriod.contains(period)) {
            totalPerPeriod[period] = 0;
        }
    }


    // Compute defect rate per grade (grade count / total count per period * 100)
    QMap<QString, QMap<QString, double>> defectRates;
    for (auto periodIt = trendData.constBegin(); periodIt != trendData.constEnd(); ++periodIt) {
        QString period = periodIt.key();
        int total = totalPerPeriod.value(period, 0);
        for (auto gradeIt = periodIt.value().constBegin(); gradeIt != periodIt.value().constEnd(); ++gradeIt) {
            QString grade = gradeIt.key();
            int cnt = gradeIt.value();
            double rate = (total > 0) ? (cnt * 100.0 / total) : 0;
            defectRates[period][grade] = rate;
        }
    }

    // Sort grades: OK first, then R1-R5, then others alphabetically
    QStringList sortedGrades = allGrades;
    std::sort(sortedGrades.begin(), sortedGrades.end(), [](const QString& a, const QString& b) {
        if (a == "OK") return true;
        if (b == "OK") return false;
        if (a.startsWith("R") && b.startsWith("R")) {
            bool aOk, bOk;
            int aNum = a.mid(1).toInt(&aOk);
            int bNum = b.mid(1).toInt(&bOk);
            if (aOk && bOk) return aNum < bNum;
        }
        return a < b;
    });

    updateTrendChart(trendData, defectRates, sortedGrades, timeRange);
}

void Defect_Data_Display::updateTrendChart(
    const QMap<QString, QMap<QString, int>>& trendData,
    const QMap<QString, QMap<QString, double>>& defectRates,
    const QStringList& allGrades,
    const QString& timeRange)
{
    qDebug() << "updateTrendChart called with" << trendData.size() << "data points, grades:" << allGrades;

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
        if (m_chartViewTrendY2) {
            QChart* chartTrendY2 = ((QChartView*)m_chartViewTrendY2)->chart();
            chartTrendY2->removeAllSeries();
            for (QAbstractAxis* axis : chartTrendY2->axes()) {
                chartTrendY2->removeAxis(axis);
                axis->deleteLater();
            }
        }
        if (m_chartViewDefectRateY2) {
            QChart* chartRateY2 = ((QChartView*)m_chartViewDefectRateY2)->chart();
            chartRateY2->removeAllSeries();
            for (QAbstractAxis* axis : chartRateY2->axes()) {
                chartRateY2->removeAxis(axis);
                axis->deleteLater();
            }
        }
        return;
    }

    // Sort time periods
    QStringList sortedTimes = trendData.keys();
    if (timeRange == "按小时" || timeRange == "按天") {
        std::sort(sortedTimes.begin(), sortedTimes.end(), [](const QString& a, const QString& b) {
            return a < b;
        });
    }

    // Format time labels for display
    QStringList timeLabels;
    for (const QString& timeKey : sortedTimes) {
        QString label = timeKey;
        if (timeRange == "按小时") {
            if (label.contains(" ")) {
                label = label.split(" ").at(1).left(5);
            }
        } else if (timeRange == "按天") {
            if (label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) {
                    label = parts.at(2);
                }
            }
        }
        timeLabels.append(label);
    }

    // Grade colors (reuse palette from updatePlatformGradeTrendChart)
    QMap<QString, QColor> gradeColors;
    gradeColors["OK"] = QColor(0, 255, 136);
    gradeColors["R1"] = QColor(255, 68, 68);
    gradeColors["R2"] = QColor(255, 165, 0);
    gradeColors["R3"] = QColor(255, 215, 0);
    gradeColors["R4"] = QColor(50, 205, 50);
    gradeColors["R5"] = QColor(0, 191, 255);
    gradeColors["NG"] = QColor(255, 0, 0);
    QList<QColor> otherColors = {
        QColor(138, 43, 226), QColor(255, 20, 147), QColor(64, 224, 208),
        QColor(255, 127, 80), QColor(106, 90, 205), QColor(152, 251, 152)
    };

    // Chart 1: Defect Count Trend (grouped bars, excluding Y2)
    chartTrend->setTitle("Count Trend by Grade_AOI");
    chartTrend->setAnimationOptions(QChart::AllAnimations);
    chartTrend->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartTrend->setTitleBrush(QBrush(QColor(0, 217, 255)));

    auto isY2Grade = [](const QString& gradeName) {
        return gradeName.trimmed().compare("Y2", Qt::CaseInsensitive) == 0;
    };

    QStringList basicGrades;
    for (const QString& grade : allGrades) {
        if (!isY2Grade(grade)) {
            basicGrades.append(grade.trimmed());
        }
    }

    int colorIdx = 0;
    int maxCount = 1;
    QBarSeries* countSeries = new QBarSeries();
    countSeries->setBarWidth(1.0);

    for (const QString& grade : basicGrades) {
        QBarSet* barSet = new QBarSet(grade);

        QColor seriesColor = gradeColors.contains(grade)
            ? gradeColors[grade]
            : otherColors[colorIdx++ % otherColors.size()];
        barSet->setColor(seriesColor);
        barSet->setBorderColor(seriesColor.lighter(130));
        barSet->setLabelColor(seriesColor);

        for (const QString& period : sortedTimes) {
            int cnt = trendData.value(period).value(grade, 0);
            *barSet << cnt;
            if (cnt > maxCount) maxCount = cnt;
        }

        countSeries->append(barSet);
    }

    chartTrend->addSeries(countSeries);

    // X axis for count chart
    QBarCategoryAxis* axisXTrend = new QBarCategoryAxis();
    axisXTrend->append(timeLabels);
    axisXTrend->setLabelsColor(QColor(234, 234, 234));
    chartTrend->addAxis(axisXTrend, Qt::AlignBottom);

    // Y axis for count chart
    QValueAxis* axisYTrend = new QValueAxis();
    axisYTrend->setTitleText("Count");
    axisYTrend->setLabelFormat("%d");
    axisYTrend->setLabelsColor(QColor(234, 234, 234));
    axisYTrend->setTitleBrush(QBrush(QColor(0, 217, 255)));
    axisYTrend->setRange(0, maxCount + maxCount * 0.25);
    chartTrend->addAxis(axisYTrend, Qt::AlignLeft);

    countSeries->attachAxis(axisXTrend);
    countSeries->attachAxis(axisYTrend);

    // Chart 2: Defect Rate Trend (grouped bars, excluding Y2)
    chartRate->setTitle("Rate Trend by Grade_AOI (%)");
    chartRate->setAnimationOptions(QChart::AllAnimations);
    chartRate->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chartRate->setTitleBrush(QBrush(QColor(0, 217, 255)));

    colorIdx = 0;
    double maxRate = 1.0;
    QBarSeries* rateSeries = new QBarSeries();
    rateSeries->setBarWidth(1.0);

    for (const QString& grade : basicGrades) {
        QBarSet* barSet = new QBarSet(grade);

        QColor seriesColor = gradeColors.contains(grade)
            ? gradeColors[grade]
            : otherColors[colorIdx++ % otherColors.size()];
        barSet->setColor(seriesColor);
        barSet->setBorderColor(seriesColor.lighter(130));
        barSet->setLabelColor(seriesColor);

        for (const QString& period : sortedTimes) {
            double rate = defectRates.value(period).value(grade, 0);
            *barSet << rate;
            if (rate > maxRate) maxRate = rate;
        }

        rateSeries->append(barSet);
    }

    chartRate->addSeries(rateSeries);

    // X axis for rate chart
    QBarCategoryAxis* axisXRate = new QBarCategoryAxis();
    axisXRate->append(timeLabels);
    axisXRate->setLabelsColor(QColor(234, 234, 234));
    chartRate->addAxis(axisXRate, Qt::AlignBottom);

    // Y axis for rate chart
    QValueAxis* axisYRate = new QValueAxis();
    axisYRate->setTitleText("Rate (%)");
    axisYRate->setLabelFormat("%.2f");
    axisYRate->setLabelsColor(QColor(234, 234, 234));
    axisYRate->setTitleBrush(QBrush(QColor(0, 217, 255)));
    axisYRate->setRange(0, maxRate * 1.3);
    chartRate->addAxis(axisYRate, Qt::AlignLeft);

    rateSeries->attachAxis(axisXRate);
    rateSeries->attachAxis(axisYRate);

    const auto addBarValueLabels = [this](QChart* chart, QAbstractBarSeries* series, int decimals) {
        QPointer<QChart> safeChart(chart);
        QPointer<QAbstractBarSeries> safeSeries(series);

        QObject::connect(chart, &QChart::plotAreaChanged, this, [safeChart, safeSeries, decimals](const QRectF& plotArea) {
            qDebug() << "[BarValueLabels] plotAreaChanged triggered"
                     << "chart=" << safeChart.data()
                     << "series=" << safeSeries.data()
                     << "plotArea=" << plotArea;

        if (!safeChart || !safeSeries) {
            qDebug() << "[BarValueLabels] chart or series already destroyed, skip redraw";
            return;
        }

        QGraphicsScene* scene = safeChart->scene();
            if (!scene) {
                qDebug() << "[BarValueLabels] chart scene is null, skip redraw";
                return;
            }

            const QList<QGraphicsItem*> items = scene->items();
            for (QGraphicsItem* item : items) {
                if (item && item->data(0) == QStringLiteral("customBarValueLabel")) {
                    delete item;
                }
            }

            const auto barSets = safeSeries->barSets();
            const int setCount = barSets.size();
            qDebug() << "[BarValueLabels] bar set count=" << setCount;
            if (setCount == 0) {
                return;
            }

            int categoryCount = 0;
            for (QBarSet* set : barSets) {
                if (set) {
                    categoryCount = qMax(categoryCount, set->count());
                }
            }
            if (categoryCount == 0) {
                return;
            }

            QList<QAbstractAxis*> axesY = safeChart->axes(Qt::Vertical, safeSeries);
            if (axesY.isEmpty()) {
                axesY = safeChart->axes(Qt::Vertical);
            }
            QValueAxis* axisY = axesY.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(axesY.first());
            if (!axisY) {
                qDebug() << "[BarValueLabels] missing vertical value axis, skip redraw";
                return;
            }

            const qreal yMin = axisY->min();
            const qreal yMax = axisY->max();
            const qreal yRange = yMax - yMin;
            if (yRange <= 0.0 || plotArea.width() <= 0.0 || plotArea.height() <= 0.0) {
                qDebug() << "[BarValueLabels] invalid plot area or yRange, skip redraw";
                return;
            }

            const qreal categorySlotWidth = plotArea.width() / categoryCount;
            const qreal groupWidth = categorySlotWidth * safeSeries->barWidth();
            const qreal groupLeftMargin = (categorySlotWidth - groupWidth) / 2.0;
            const qreal singleBarWidth = groupWidth / setCount;

            for (int setIndex = 0; setIndex < setCount; ++setIndex) {
                QBarSet* set = barSets.at(setIndex);
                if (!set) {
                    qDebug() << "[BarValueLabels] encountered null QBarSet, skip";
                    continue;
                }

                qDebug() << "[BarValueLabels] drawing set" << set->label() << "count=" << set->count();

                for (int i = 0; i < set->count(); ++i) {
                    const qreal value = set->at(i);

                    const qreal barCenterX = plotArea.left()
                        + i * categorySlotWidth
                        + groupLeftMargin
                        + setIndex * singleBarWidth
                        + singleBarWidth / 2.0;
                    const qreal normalizedValue = (value - yMin) / yRange;
                    qreal barTopY = plotArea.bottom() - normalizedValue * plotArea.height();
                    if (qFuzzyIsNull(value)) {
                        barTopY -= 8.0;
                    }

                    QGraphicsSimpleTextItem* label = new QGraphicsSimpleTextItem(
                        QString::number(value, 'f', decimals));
                    label->setData(0, QStringLiteral("customBarValueLabel"));
                    label->setFont(QFont("Arial", 8, QFont::Bold));
                    label->setBrush(QBrush(set->labelColor()));
                    label->setZValue(100);
                    scene->addItem(label);

                    const qreal x = barCenterX - label->boundingRect().width() / 2.0;
                    const qreal y = barTopY - label->boundingRect().height() - 6.0;
                    label->setPos(x, y);
                }
            }
        });

        qDebug() << "[BarValueLabels] force initial redraw" << "chart=" << chart << "series=" << series;
        chart->plotAreaChanged(chart->plotArea());
    };

    addBarValueLabels(chartTrend, countSeries, 0);
    addBarValueLabels(chartRate, rateSeries, 2);

    // Style legend for both charts
    chartTrend->legend()->setLabelColor(QColor(234, 234, 234));
    chartTrend->legend()->setFont(QFont("Arial", 9));
    chartTrend->legend()->setAlignment(Qt::AlignRight);

    chartRate->legend()->setLabelColor(QColor(234, 234, 234));
    chartRate->legend()->setFont(QFont("Arial", 9));
    chartRate->legend()->setAlignment(Qt::AlignRight);

    // ========== Y2 Count and Rate Trend ==========

    if (m_chartViewTrendY2 && m_chartViewDefectRateY2 && !m_trendDataY2.isEmpty()) {

        // Get chart pointers

        QChart* chartTrendY2 = ((QChartView*)m_chartViewTrendY2)->chart();

        QChart* chartRateY2 = ((QChartView*)m_chartViewDefectRateY2)->chart();



        // COMPLETE cleanup: remove series, axes, AND our old custom text labels

        chartTrendY2->removeAllSeries();

        chartRateY2->removeAllSeries();

        QList<QAbstractAxis*> trendY2Axes = chartTrendY2->axes();

        QList<QAbstractAxis*> rateY2Axes = chartRateY2->axes();

        qDebug() << "[Y2 Chart] cleanup axes trend/rate:" << trendY2Axes.size() << rateY2Axes.size();

        // Remove all axes from both charts safely

        for (QAbstractAxis* axis : trendY2Axes) {

            chartTrendY2->removeAxis(axis);

            axis->deleteLater();

        }

        for (QAbstractAxis* axis : rateY2Axes) {

            chartRateY2->removeAxis(axis);

            axis->deleteLater();

        }



        chartTrendY2->setTitle("Count Trend by Grade_AOI");
        chartTrendY2->setAnimationOptions(QChart::AllAnimations);
        chartTrendY2->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
        chartTrendY2->setTitleBrush(QBrush(QColor(0, 217, 255)));

        chartRateY2->setTitle("Rate Trend by Grade_AOI (%)");
        chartRateY2->setAnimationOptions(QChart::AllAnimations);
        chartRateY2->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
        chartRateY2->setTitleBrush(QBrush(QColor(0, 217, 255)));

        // Set plot area background to match chart background

        chartTrendY2->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));

        chartTrendY2->setPlotAreaBackgroundVisible(true);

        chartRateY2->setPlotAreaBackgroundBrush(QBrush(QColor(22, 33, 62)));

        chartRateY2->setPlotAreaBackgroundVisible(true);



        qDebug() << "[Y2 Chart] Drawing Y2 charts, data points:" << m_trendDataY2.size();



        QStringList y2Categories;

        double maxY2Count = 0;

        double maxY2Rate = 0;



        for (auto it = m_trendDataY2.constBegin(); it != m_trendDataY2.constEnd(); ++it) {

            y2Categories << it.key();

            if (it.value().first > maxY2Count) maxY2Count = it.value().first;

            double rate = (it.value().second > 0) ? (it.value().first * 100.0 / it.value().second) : 0;

            if (rate > maxY2Rate) maxY2Rate = rate;

        }



        // Y2 Count Line Series

        QLineSeries* seriesY2Count = new QLineSeries();
        seriesY2Count->setName("Y2");
        seriesY2Count->setPen(QPen(QColor(255, 105, 180), 2));
        seriesY2Count->setColor(QColor(255, 105, 180));
        seriesY2Count->setPointsVisible(true);
        seriesY2Count->setMarkerSize(4.0);
        seriesY2Count->setPointLabelsVisible(true);
        seriesY2Count->setPointLabelsFormat("@yPoint");
        seriesY2Count->setPointLabelsColor(QColor(255, 105, 180));
        seriesY2Count->setPointLabelsFont(QFont("Arial", 8, QFont::Bold));

        // Y2 Rate Line Series

        QLineSeries* seriesY2Rate = new QLineSeries();
        seriesY2Rate->setName("Y2");
        seriesY2Rate->setPen(QPen(QColor(255, 105, 180), 2));
        seriesY2Rate->setColor(QColor(255, 105, 180));
        seriesY2Rate->setPointsVisible(true);
        seriesY2Rate->setMarkerSize(4.0);
        // Use custom point labels (see addLineSeriesPointLabels below) for fixed 2-decimal formatting

        int idx = 0;

        for (auto it = m_trendDataY2.constBegin(); it != m_trendDataY2.constEnd(); ++it) {

            seriesY2Count->append(idx, it.value().first);

            double rate = (it.value().second > 0) ? (it.value().first * 100.0 / it.value().second) : 0;

            seriesY2Rate->append(idx, rate);

            idx++;

        }

        chartTrendY2->addSeries(seriesY2Count);



        // X axis for Y2 count chart

        QBarCategoryAxis* axisXTrendY2 = new QBarCategoryAxis();

        axisXTrendY2->append(timeLabels);

        axisXTrendY2->setLabelsColor(QColor(234, 234, 234));

        chartTrendY2->addAxis(axisXTrendY2, Qt::AlignBottom);



        // Y axis for Y2 count chart

        QValueAxis* axisYTrendY2 = new QValueAxis();

        axisYTrendY2->setTitleText("Count");

        axisYTrendY2->setLabelFormat("%d");

        axisYTrendY2->setLabelsColor(QColor(234, 234, 234));

        axisYTrendY2->setTitleBrush(QBrush(QColor(0, 217, 255)));

        axisYTrendY2->setRange(0, maxY2Count > 0 ? maxY2Count + maxY2Count * 0.25 : 1);

        chartTrendY2->addAxis(axisYTrendY2, Qt::AlignLeft);



        seriesY2Count->attachAxis(axisXTrendY2);

        seriesY2Count->attachAxis(axisYTrendY2);



        chartRateY2->addSeries(seriesY2Rate);



        // X axis for Y2 rate chart

        QBarCategoryAxis* axisXRateY2 = new QBarCategoryAxis();

        axisXRateY2->append(timeLabels);

        axisXRateY2->setLabelsColor(QColor(234, 234, 234));

        chartRateY2->addAxis(axisXRateY2, Qt::AlignBottom);



        // Y axis for Y2 rate chart

        QValueAxis* axisYRateY2 = new QValueAxis();

        axisYRateY2->setTitleText("Rate (%)");

        axisYRateY2->setLabelFormat("%.2f");

        axisYRateY2->setLabelsColor(QColor(234, 234, 234));

        axisYRateY2->setTitleBrush(QBrush(QColor(0, 217, 255)));

        axisYRateY2->setRange(0, maxY2Rate > 0 ? maxY2Rate * 1.3 : 1);

        chartRateY2->addAxis(axisYRateY2, Qt::AlignLeft);



        seriesY2Rate->attachAxis(axisXRateY2);

        seriesY2Rate->attachAxis(axisYRateY2);

        // Custom point labels for Y2 rate chart with fixed 2-decimal format
        const auto addLineSeriesPointLabels = [this](QChart* chart, QLineSeries* series) {
            QPointer<QChart> safeChart(chart);
            QPointer<QLineSeries> safeSeries(series);

            QObject::connect(chart, &QChart::plotAreaChanged, this, [safeChart, safeSeries](const QRectF&) {
                if (!safeChart || !safeSeries) return;

                QGraphicsScene* scene = safeChart->scene();
                if (!scene) return;

                const QList<QGraphicsItem*> items = scene->items();
                for (QGraphicsItem* item : items) {
                    if (item && item->data(0) == QStringLiteral("customLinePointLabel")) {
                        delete item;
                    }
                }

                const auto points = safeSeries->points();
                for (const QPointF& pt : points) {
                    QPointF mapped = safeChart->mapToPosition(pt, safeSeries);
                    QGraphicsSimpleTextItem* label = new QGraphicsSimpleTextItem(
                        QString::number(pt.y(), 'f', 2));
                    label->setData(0, QStringLiteral("customLinePointLabel"));
                    label->setFont(QFont("Arial", 8, QFont::Bold));
                    label->setBrush(QBrush(QColor(255, 105, 180)));
                    label->setZValue(100);
                    scene->addItem(label);
                    qreal x = mapped.x() - label->boundingRect().width() / 2;
                    qreal y = mapped.y() - label->boundingRect().height() - 6;
                    label->setPos(x, y);
                }
            });

            chart->plotAreaChanged(chart->plotArea());
        };

        addLineSeriesPointLabels(chartRateY2, seriesY2Rate);

        chartTrendY2->legend()->setLabelColor(QColor(234, 234, 234));
        chartTrendY2->legend()->setFont(QFont("Arial", 9));
        chartTrendY2->legend()->setAlignment(Qt::AlignRight);

        chartRateY2->legend()->setLabelColor(QColor(234, 234, 234));
        chartRateY2->legend()->setFont(QFont("Arial", 9));
        chartRateY2->legend()->setAlignment(Qt::AlignRight);

    }
    qDebug() << "updateTrendChart completed with" << allGrades.size() << "grade series";
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
        qDebug() << "No detail data, clearing detail view";
        m_detailCache = CachedTabData();
        clearDetailView();
        return;
    }

    qDebug() << "Step 1: Processing Grade_AOI data (R1, R2, R3, etc.)";

    // Data format: row[0] = grade_result (OK/NG), row[1] = count
    // Aggregate counts by Grade_AOI value
    QMap<QString, int> gradeCount;
    for (const QVariantList& row : defectDetails) {
        QString gradeAOI = row[0].toString();  // grade_result (OK/NG)
        int cnt = row[1].toInt();              // count
        gradeCount[gradeAOI] += cnt;
    }

    m_detailPieData = gradeCount;
    m_detailPieTitle = QString("AOI Grade Types");

    qDebug() << "Step 2: Found" << gradeCount.size() << "unique Grade_AOI types:" << gradeCount.keys();

    if (gradeCount.isEmpty()) {
        qDebug() << "No Grade_AOI data found, returning early";
        return;
    }

    // Color map for Grade_AOI types
    QMap<QString, QColor> gradeColors;
    gradeColors["OK"] = QColor(0, 255, 136);      // Green
    gradeColors["R1"] = QColor(255, 68, 68);      // Red (critical)
    gradeColors["R2"] = QColor(255, 165, 0);      // Orange
    gradeColors["R3"] = QColor(255, 215, 0);      // Gold/Yellow
    gradeColors["R4"] = QColor(50, 205, 50);      // Lime Green
    gradeColors["R5"] = QColor(0, 191, 255);      // Deep Sky Blue
    gradeColors["NG"] = QColor(255, 0, 0);       // Pure Red
    // For any other grade types, generate a color
    QList<QColor> otherColors = {
        QColor(138, 43, 226),   // Blue Violet
        QColor(255, 20, 147),   // Deep Pink
        QColor(64, 224, 208),   // Turquoise
        QColor(255, 127, 80),   // Coral
        QColor(106, 90, 205),   // Slate Blue
        QColor(152, 251, 152),  // Pale Green
        QColor(255, 182, 193),  // Light Pink
        QColor(175, 238, 238)   // Pale Turquoise
    };

    qDebug() << "Step 3: Creating pie series with Grade_AOI colors";

    // Sort grades for consistent display: OK first, then R1-R5, then others alphabetically
    QStringList sortedGrades = gradeCount.keys();
    std::sort(sortedGrades.begin(), sortedGrades.end(), [](const QString& a, const QString& b) {
        if (a == "OK") return true;
        if (b == "OK") return false;
        if (a.startsWith("R") && b.startsWith("R")) {
            bool aOk, bOk;
            int aNum = a.mid(1).toInt(&aOk);
            int bNum = b.mid(1).toInt(&bOk);
            if (aOk && bOk) return aNum < bNum;
        }
        return a < b;
    });

    QMap<QString, QColor> displayGradeColors;
    int colorIdx = 0;
    for (const QString& gradeAOI : sortedGrades) {
        if (gradeColors.contains(gradeAOI)) {
            displayGradeColors[gradeAOI] = gradeColors[gradeAOI];
        } else {
            displayGradeColors[gradeAOI] = otherColors[colorIdx % otherColors.size()];
            colorIdx++;
        }
    }

    QPieSeries* pieSeries = new QPieSeries();
    pieSeries->setLabelsVisible(true);

    for (const QString& gradeAOI : sortedGrades) {
        int count = gradeCount.value(gradeAOI);
        QPieSlice* slice = pieSeries->append(gradeAOI, count);
        slice->setColor(displayGradeColors.value(gradeAOI));
        slice->setLabelBrush(QBrush(QColor(234, 234, 234)));
        slice->setLabelFont(QFont("Arial", 10, QFont::Bold));
    }

    qDebug() << "Step 4: Creating pie chart";

    QChart* pieChart = new QChart();
    pieChart->setTitle("AOI Grade Types");
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
    newPieChartView->setCursor(Qt::ArrowCursor);

    qDebug() << "Step 6: Building inline detail panel";

    while (QLayout* oldLayout = ui.chartPieDetail->layout()) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }

    QHBoxLayout* detailLayout = new QHBoxLayout(ui.chartPieDetail);
    detailLayout->setContentsMargins(12, 12, 12, 12);
    detailLayout->setSpacing(16);

    QFrame* pieFrame = new QFrame(ui.chartPieDetail);
    pieFrame->setObjectName("chartFrame");
    pieFrame->setFixedWidth(340);
    QVBoxLayout* pieFrameLayout = new QVBoxLayout(pieFrame);
    pieFrameLayout->setContentsMargins(10, 10, 10, 10);
    pieFrameLayout->setSpacing(10);

    QLabel* pieTitleLabel = new QLabel(m_detailPieTitle.isEmpty() ? "缺陷类型分布" : m_detailPieTitle, pieFrame);
    pieTitleLabel->setStyleSheet("color: #00d9ff; font-size: 14px; font-weight: bold;");
    pieTitleLabel->setAlignment(Qt::AlignCenter);
    pieFrameLayout->addWidget(pieTitleLabel);
    newPieChartView->setMinimumSize(300, 320);
    pieFrameLayout->addWidget(newPieChartView, 1);
    detailLayout->addWidget(pieFrame, 1);

    QFrame* infoFrame = new QFrame(ui.chartPieDetail);
    infoFrame->setObjectName("chartFrame");
    QVBoxLayout* infoLayout = new QVBoxLayout(infoFrame);
    infoLayout->setContentsMargins(10, 10, 10, 10);
    infoLayout->setSpacing(12);

    QHBoxLayout* statsLayout = new QHBoxLayout();
    statsLayout->setSpacing(10);

    int inlineTotal = 0;
    for (auto it = gradeCount.constBegin(); it != gradeCount.constEnd(); ++it) {
        inlineTotal += it.value();
    }

    QFrame* totalCard = new QFrame(infoFrame);
    totalCard->setObjectName("statCard");
    QVBoxLayout* totalCardLayout = new QVBoxLayout(totalCard);
    totalCardLayout->setContentsMargins(8, 8, 8, 8);
    QLabel* totalTitle = new QLabel("总数", totalCard);
    totalTitle->setStyleSheet("color: #8892b0; font-size: 11px;");
    totalTitle->setAlignment(Qt::AlignCenter);
    QLabel* totalValue = new QLabel(QString::number(inlineTotal), totalCard);
    totalValue->setStyleSheet("color: #00d9ff; font-size: 22px; font-weight: bold;");
    totalValue->setAlignment(Qt::AlignCenter);
    totalCardLayout->addWidget(totalTitle);
    totalCardLayout->addWidget(totalValue);
    statsLayout->addWidget(totalCard);

    QFrame* typeCard = new QFrame(infoFrame);
    typeCard->setObjectName("statCard");
    QVBoxLayout* typeCardLayout = new QVBoxLayout(typeCard);
    typeCardLayout->setContentsMargins(8, 8, 8, 8);
    QLabel* typeTitle = new QLabel("类型数", typeCard);
    typeTitle->setStyleSheet("color: #8892b0; font-size: 11px;");
    typeTitle->setAlignment(Qt::AlignCenter);
    QLabel* typeValue = new QLabel(QString::number(gradeCount.size()), typeCard);
    typeValue->setStyleSheet("color: #ff6b6b; font-size: 22px; font-weight: bold;");
    typeValue->setAlignment(Qt::AlignCenter);
    typeCardLayout->addWidget(typeTitle);
    typeCardLayout->addWidget(typeValue);
    statsLayout->addWidget(typeCard);

    infoLayout->addLayout(statsLayout);

    QLabel* tableTitle = new QLabel("详细数据", infoFrame);
    tableTitle->setStyleSheet("color: #00d9ff; font-size: 14px; font-weight: bold;");
    infoLayout->addWidget(tableTitle);

    QTableWidget* detailTable = new QTableWidget(infoFrame);
    detailTable->setColumnCount(4);
    detailTable->setHorizontalHeaderLabels({"缺陷类型", "数量", "比例", "占比"});
    detailTable->setRowCount(sortedGrades.size());
    detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTable->setSelectionMode(QAbstractItemView::SingleSelection);
    detailTable->setAlternatingRowColors(false);
    detailTable->horizontalHeader()->setStretchLastSection(true);
    detailTable->verticalHeader()->setVisible(false);
    detailTable->setShowGrid(false);
    detailTable->verticalHeader()->setDefaultSectionSize(36);
    detailTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    detailTable->horizontalHeader()->setHighlightSections(false);
    detailTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #16213e;
            border: 1px solid rgba(0, 217, 255, 60);
            border-radius: 10px;
            padding: 6px;
            gridline-color: rgba(15, 52, 96, 180);
            color: #e0e0e0;
            font-size: 13px;
        }
        QTableWidget::item {
            padding: 8px 12px;
            border-bottom: 1px solid rgba(15, 52, 96, 140);
            background-color: transparent;
        }
        QTableWidget::item:selected {
            background-color: rgba(15, 52, 96, 220);
            color: #00d9ff;
        }
        QHeaderView {
            background: transparent;
        }
        QHeaderView::section {
            background-color: #162131;
            color: #183622;
            font-weight: bold;
            font-size: 13px;
            padding: 10px 12px;
            border: none;
            border-right: 1px solid rgba(0, 217, 255, 35);
            border-bottom: 2px solid rgba(0, 217, 255, 170);
        }
        QHeaderView::section:first {
            border-top-left-radius: 8px;
        }
        QHeaderView::section:last {
            border-top-right-radius: 8px;
            border-right: none;
        }
        QHeaderView::up-arrow, QHeaderView::down-arrow {
            image: none;
            width: 0px;
            height: 0px;
        }
        QTableCornerButton::section {
            background-color: #16213e;
            border: none;
            border-bottom: 2px solid rgba(0, 217, 255, 170);
            border-right: 1px solid rgba(0, 217, 255, 35);
        }
    )");

    for (int col = 0; col < detailTable->columnCount(); ++col) {
        QTableWidgetItem* headerItem = detailTable->horizontalHeaderItem(col);
        if (headerItem) {
            headerItem->setTextAlignment(Qt::AlignCenter);
            headerItem->setForeground(QBrush(QColor("#28364f")));
            headerItem->setBackground(QBrush(QColor("#16213e")));
            headerItem->setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
        }
    }

    for (int row = 0; row < sortedGrades.size(); ++row) {
        const QString& grade = sortedGrades[row];
        const int count = gradeCount.value(grade, 0);
        const double ratio = inlineTotal > 0 ? static_cast<double>(count) / static_cast<double>(inlineTotal) : 0.0;
        QColor color = displayGradeColors.value(grade, QColor("#e0e0e0"));

        QTableWidgetItem* nameItem = new QTableWidgetItem("● " + grade);
        nameItem->setForeground(QBrush(color));
        nameItem->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
        detailTable->setItem(row, 0, nameItem);

        QTableWidgetItem* countItem = new QTableWidgetItem(QString::number(count));
        countItem->setFont(QFont("Microsoft YaHei", 12));
        countItem->setForeground(QBrush(QColor("#e0e0e0")));
        countItem->setTextAlignment(Qt::AlignCenter);
        detailTable->setItem(row, 1, countItem);

        QTableWidgetItem* ratioItem = new QTableWidgetItem(QString::number(ratio, 'f', 4));
        ratioItem->setFont(QFont("Microsoft YaHei", 12));
        ratioItem->setForeground(QBrush(QColor("#e0e0e0")));
        ratioItem->setTextAlignment(Qt::AlignCenter);
        detailTable->setItem(row, 2, ratioItem);

        QTableWidgetItem* percentItem = new QTableWidgetItem(QString::number(ratio * 100.0, 'f', 1) + "%");
        percentItem->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
        percentItem->setForeground(QBrush(color));
        percentItem->setTextAlignment(Qt::AlignCenter);
        detailTable->setItem(row, 3, percentItem);
    }

    detailTable->setColumnWidth(0, 220);
    detailTable->setColumnWidth(1, 120);
    detailTable->setColumnWidth(2, 120);
    QObject::connect(detailTable, &QTableWidget::cellClicked, this, [=](int row, int /*column*/) {
        if (row >= 0 && row < sortedGrades.size()) {
            showGradeTypeDialog(sortedGrades[row]);
        }
    });
    infoLayout->addWidget(detailTable, 1);
    detailLayout->addWidget(infoFrame, 2);

    m_chartViewPieDetail = newPieChartView;

    qDebug() << "Step 7: Creating bar series for Grade_AOI types";

    // Create vertical bar chart with ONE bar containing all grade counts
    QBarSeries* series = new QBarSeries();
    series->setLabelsVisible(true);
    series->setLabelsFormat("@value");

    // Create ONE BarSet containing all grade counts
    QBarSet* barSet = new QBarSet("Grade Counts");
    for (const QString& grade : sortedGrades) {
        int count = gradeCount[grade];
        *barSet << count;
    }
    // Apply a single color to all bars (setColor applies to all bars)
    barSet->setColor(QColor(0, 217, 255));
    series->append(barSet);

    // Connect bar click signal for grade type chart
    QObject::connect(series, &QBarSeries::clicked, this, [=](int index, QBarSet* barSet) {
        qDebug() << "[BarChart] Clicked bar index:" << index << "grade:" << sortedGrades.value(index, "Unknown");
        QString gradeName = sortedGrades.value(index, "Unknown");
        showGradeTypeDialog(gradeName);
    });

    qDebug() << "Step 8: Creating bar chart";

    QChart* chart = new QChart();
    chart->setTitle("AOI Grade Types");
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    chart->setTitleBrush(QBrush(QColor(0, 217, 255)));
    chart->legend()->setLabelColor(QColor(234, 234, 234));
    chart->legend()->hide();

    QChartView* newDetailChartView = new QChartView(chart);
    newDetailChartView->setRenderHint(QPainter::Antialiasing);
    newDetailChartView->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
    newDetailChartView->setCursor(Qt::PointingHandCursor);
    newDetailChartView->installEventFilter(this);
    newDetailChartView->viewport()->installEventFilter(this);

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

    // X axis = grade names (categories) ← goes at bottom
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(sortedGrades);
    axisX->setLabelsColor(QColor(234, 234, 234));
    QFont axisXFont = axisX->labelsFont();
    axisXFont.setPointSize(10);
    axisX->setLabelsFont(axisXFont);
    chart->addAxis(axisX, Qt::AlignBottom);

    // Y axis = values (counts) ← goes at left
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Count");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    QFont axisYFont = axisY->labelsFont();
    axisYFont.setPointSize(10);
    axisY->setLabelsFont(axisYFont);
    // Find max value for Y axis range
    int maxVal = 0;
    for (int val : gradeCount.values()) {
        if (val > maxVal) maxVal = val;
    }
    if (maxVal < 1) maxVal = 1;
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
        clearPlatformStatsView();
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
    Q_UNUSED(aoiResultCategories);

    if (platformAoiResultData.isEmpty()) {
        if (!m_platformTrendData.isEmpty()) {
            updatePlatformTrendChart(m_platformTrendData);
        }
        return;
    }

    const QStringList orderedAoiResultCategories = {"OK", "NG"};

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
        for (int i = 0; i < orderedAoiResultCategories.size() && i < resultColors.size(); ++i) {
            QBarSet* set = new QBarSet(orderedAoiResultCategories[i]);
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

            for (int i = 0; i < orderedAoiResultCategories.size() && i < barSets.size(); ++i) {
                int val = 0;
                if (!originalKey.isEmpty() && platformAoiResultData.contains(originalKey)) {
                    const QMap<QString, int>& resMap = platformAoiResultData[originalKey].value(p);
                    val = resMap.value(orderedAoiResultCategories[i], 0);
                }
                *barSets[i] << val;
                columnTotals[ti] += val;
            }
        }

        for (QBarSet* bs : barSets) stackedSeries->append(bs);
        chart->addSeries(stackedSeries);
        chart->setTitle(platformNames[p] + " (" + timeRange + ")");
        chart->legend()->show();
        chart->legend()->setLabelColor(QColor(234, 234, 234));
        chart->legend()->setBrush(QBrush(QColor(22, 33, 62, 200)));
        chart->legend()->setPen(QPen(QColor(60, 80, 100)));
        QFont legendFont = chart->legend()->font();
        legendFont.setPointSize(9);
        chart->legend()->setFont(legendFont);

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

        // Hide segment labels - we'll show total only above each bar
        stackedSeries->setLabelsVisible(false);
        
        // Wait for layout to complete, then add total labels
        if (!chart->scene()) continue;
        
        // Use simple single-shot with larger delay - chart should be ready after data is loaded
        QTimer::singleShot(500, this, [chart, chartView, timeCategories, columnTotals, axisY, p]() {
            QRectF plotArea = chart->plotArea();
            
            // Check if plotArea is valid (has reasonable size)
            bool validPlotArea = plotArea.width() > 200 && plotArea.height() > 50;
            
            qDebug() << "[Platform" << p << "] Delayed check, plotArea:" << plotArea << "valid:" << validPlotArea;
            
            if (!validPlotArea) {
                qDebug() << "[Platform" << p << "] plotArea still not valid, skipping labels";
                return;
            }
            
            // Draw the labels
            qDebug() << "[Platform" << p << "] Drawing labels";
            
            int numBars = timeCategories.size();
            if (numBars == 0) return;
            
            // Calculate bar positions based on plot area
            qreal barGroupWidth = plotArea.width() / numBars;
            qreal barWidth = barGroupWidth * 0.7;  // 70% of group width for the bar
            qreal barLeftMargin = barGroupWidth * 0.15; // 15% margin on each side
            
            qreal yMin = axisY->min();
            qreal yMax = axisY->max();
            qreal yRange = yMax - yMin;
            
            for (int ti = 0; ti < numBars; ++ti) {
                if (columnTotals[ti] <= 0) continue;
                
                // Calculate bar position
                qreal barCenterX = plotArea.left() + barLeftMargin + ti * barGroupWidth + barWidth / 2;
                
                // Calculate Y position for the top of the bar (value relative to axis)
                qreal normalizedValue = (columnTotals[ti] - yMin) / yRange;
                qreal barTopY = plotArea.bottom() - normalizedValue * plotArea.height();
                
                // Create text item
                QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(QString::number(columnTotals[ti]));
                textItem->setFont(QFont("Arial", 9, QFont::Bold));
                textItem->setBrush(QBrush(QColor(255, 255, 255)));
                textItem->setZValue(100);
                chart->scene()->addItem(textItem);
                
                // Position text centered above the bar
                qreal textX = barCenterX - textItem->boundingRect().width() / 2;
                qreal textY = barTopY - textItem->boundingRect().height() - 3;
                textItem->setPos(textX, textY);
            }
        });
    }
}


void Defect_Data_Display::updatePlatformByTimeChart()
{
    qDebug() << "=== updatePlatformByTimeChart ENTER ===";

    if (!m_chartViewPlatformByTime) {
        qDebug() << "[EXIT EARLY] m_chartViewPlatformByTime is null";
        return;
    }
    qDebug() << "[CHECK] m_chartViewPlatformByTime is valid";

    QChart* chart = ((QChartView*)m_chartViewPlatformByTime)->chart();

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
    for (auto axis : chart->axes()) {
        chart->removeAxis(axis);
    }
    qDebug() << "[CLEANUP] Cleared old series and axes";

    QString timeRange = ui.comboTimeRange->currentText();
    qDebug() << "[DATA] Time range:" << timeRange;
    qDebug() << "[DATA] m_platformAoiResultData.isEmpty():" << m_platformAoiResultData.isEmpty()
             << "(size:" << m_platformAoiResultData.size() << ")";
    qDebug() << "[DATA] m_platformTrendData.isEmpty():" << m_platformTrendData.isEmpty()
             << "(size:" << m_platformTrendData.size() << ")";

    // AOI result colors for stacked segments (OK=green, NG=red)
    QList<QColor> resultColors;
    resultColors << QColor(0, 255, 136) << QColor(255, 80, 80);

    // Get time categories and AOI result categories from the data
    QStringList timeCategories;
    QStringList aoiResultCategories;

    if (!m_platformAoiResultData.isEmpty()) {
        qDebug() << "[DATA BUILD] Using m_platformAoiResultData";
        // Force OK/NG categories for by-time chart
        aoiResultCategories = {"OK", "NG"};

        // Collect time categories
        for (auto it = m_platformAoiResultData.constBegin(); it != m_platformAoiResultData.constEnd(); ++it) {
            QString label = it.key();
            if (timeRange == "按小时" && label.contains(" ")) {
                label = label.split(" ").at(1).left(5);
            } else if (timeRange == "按天" && label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) label = parts.at(2);
            }
            if (!timeCategories.contains(label)) timeCategories.append(label);
        }
        qDebug() << "[DATA BUILD] Collected" << timeCategories.size() << "time categories, "
                 << aoiResultCategories.size() << "AOI result types (OK/NG)";
    } else if (!m_platformTrendData.isEmpty()) {
        qDebug() << "[DATA BUILD] Using m_platformTrendData (fallback - OK/NG from Pass/Fail)";
        // Fallback: convert Pass/Fail to OK/NG
        aoiResultCategories = {"OK", "NG"};
        for (auto it = m_platformTrendData.constBegin(); it != m_platformTrendData.constEnd(); ++it) {
            QString label = it.key();
            if (timeRange == "按小时" && label.contains(" ")) {
                label = label.split(" ").at(1).left(5);
            } else if (timeRange == "按天" && label.contains("-")) {
                QStringList parts = label.split("-");
                if (parts.size() >= 3) label = parts.at(2);
            }
            if (!timeCategories.contains(label)) timeCategories.append(label);
        }
        qDebug() << "[DATA BUILD] Collected" << timeCategories.size() << "time categories, "
                 << aoiResultCategories.size() << "AOI result types (Pass/Fail) from m_platformTrendData";
    } else {
        qDebug() << "[DATA BUILD] WARNING: BOTH m_platformAoiResultData AND m_platformTrendData are EMPTY!";
        clearPlatformStatsView();
        return;
    }

    if (timeCategories.isEmpty()) {
        qDebug() << "[EXIT EARLY] No time categories available for by-time chart (timeCategories is empty)";
        return;
    }
    qDebug() << "[DATA BUILD] Final timeCategories:" << timeCategories;
    qDebug() << "[DATA BUILD] Final aoiResultCategories:" << aoiResultCategories;

    // Sort time categories
    if (timeRange == "按小时" || timeRange == "按天") {
        std::sort(timeCategories.begin(), timeCategories.end(), [](const QString& a, const QString& b) {
            return a.toInt() < b.toInt();
        });
    }
    qDebug() << "[DATA BUILD] Sorted timeCategories:" << timeCategories;

    // Store time category mapping for tooltip
    m_byTimeCategoryMap.clear();
    qDebug() << "[TOOLTIP] Building category map...";
    for (const QString& cat : timeCategories) {
        // Find original key for this category
        if (!m_platformAoiResultData.isEmpty()) {
            for (auto it = m_platformAoiResultData.constBegin(); it != m_platformAoiResultData.constEnd(); ++it) {
                QString label = it.key();
                if (timeRange == "按小时" && label.contains(" ")) {
                    if (label.split(" ").at(1).left(5) == cat) { m_byTimeCategoryMap[cat] = it.key(); break; }
                } else if (timeRange == "按天" && label.contains("-")) {
                    QStringList parts = label.split("-");
                    if (parts.size() >= 3 && parts.at(2) == cat) { m_byTimeCategoryMap[cat] = it.key(); break; }
                } else if (timeRange == "按月" && label == cat) {
                    m_byTimeCategoryMap[cat] = it.key(); break;
                }
            }
        } else if (!m_platformTrendData.isEmpty()) {
            for (auto it = m_platformTrendData.constBegin(); it != m_platformTrendData.constEnd(); ++it) {
                QString label = it.key();
                if (timeRange == "按小时" && label.contains(" ")) {
                    if (label.split(" ").at(1).left(5) == cat) { m_byTimeCategoryMap[cat] = it.key(); break; }
                } else if (timeRange == "按天" && label.contains("-")) {
                    QStringList parts = label.split("-");
                    if (parts.size() >= 3 && parts.at(2) == cat) { m_byTimeCategoryMap[cat] = it.key(); break; }
                } else if (timeRange == "按月" && label == cat) {
                    m_byTimeCategoryMap[cat] = it.key(); break;
                }
            }
        }
    }
    qDebug() << "[TOOLTIP] Category map built, size:" << m_byTimeCategoryMap.size();

    // Create ONE stacked bar series aggregating all platforms
    // Bar sets represent AOI result types (OK, NG, Rework), each bar set contains values for each time period
    qDebug() << "[SERIES] Creating stacked bar series...";
    QStackedBarSeries* stackedSeries = new QStackedBarSeries();
    stackedSeries->setLabelsVisible(false);
    stackedSeries->setName("总计");

    // Create bar sets for each AOI result type
    qDebug() << "[SERIES] Creating" << aoiResultCategories.size() << "bar sets";
    QList<QBarSet*> barSets;
    for (int i = 0; i < aoiResultCategories.size() && i < resultColors.size(); ++i) {
        QBarSet* set = new QBarSet(aoiResultCategories[i]);
        set->setColor(resultColors[i]);
        set->setLabelColor(QColor(234, 234, 234));
        barSets.append(set);
    }

    // Store totals per time period for label display
    QList<int> timePeriodTotals(timeCategories.size(), 0);
    qDebug() << "[SERIES] Created" << barSets.size() << "bar sets, initialized" << timePeriodTotals.size() << "totals to 0";

    // Fill in aggregated data for each time category
    qDebug() << "[SERIES] Filling aggregated data for" << timeCategories.size() << "time categories...";
    for (int ti = 0; ti < timeCategories.size(); ++ti) {
        const QString& timeCat = timeCategories[ti];
        QString originalKey = m_byTimeCategoryMap.value(timeCat);

        // Aggregate data from all 4 platforms for this time period
        QMap<QString, int> aggregatedAoiData;
        qDebug() << "[AGGREGATE] Processing time category:" << timeCat << "originalKey:" << originalKey;
        for (int p = 0; p < 4; ++p) {
            if (!m_platformAoiResultData.isEmpty() && !originalKey.isEmpty()) {
                const QMap<QString, int>& aoiMap = m_platformAoiResultData.value(originalKey).value(p);
                for (auto ait = aoiMap.constBegin(); ait != aoiMap.constEnd(); ++ait) {
                    aggregatedAoiData[ait.key()] += ait.value();
                }
            }
        }

        // Fill bar sets with aggregated data
        for (int i = 0; i < aoiResultCategories.size() && i < barSets.size(); ++i) {
            int val = aggregatedAoiData.value(aoiResultCategories[i], 0);
            *barSets[i] << val;
            timePeriodTotals[ti] += val;
        }
        qDebug() << "[AGGREGATE] timePeriodTotals[" << ti << "] =" << timePeriodTotals[ti];

        // Fallback to OK/NG if no AOI result data
        if (aggregatedAoiData.isEmpty() && !m_platformTrendData.isEmpty() && !originalKey.isEmpty()) {
            int passTotal = 0, failTotal = 0;
            for (int p = 0; p < 4; ++p) {
                if (m_platformTrendData.contains(originalKey) && m_platformTrendData[originalKey].contains(p)) {
                    QPair<int, int> passFail = m_platformTrendData[originalKey][p];
                    passTotal += passFail.first;
                    failTotal += passFail.second;
                }
            }
            // Map to aoiResultCategories (OK, NG)
            for (int i = 0; i < aoiResultCategories.size() && i < barSets.size(); ++i) {
                int val = 0;
                if (aoiResultCategories[i] == "OK") val = passTotal;
                else if (aoiResultCategories[i] == "NG") val = failTotal;
                *barSets[i] << val;
                timePeriodTotals[ti] += val;
            }
            qDebug() << "[FALLBACK] OK/NG totals for category" << timeCat << ": OK=" << passTotal << "NG=" << failTotal;
        }
    }

    // Add bar sets to series
    qDebug() << "[SERIES] Adding" << barSets.size() << "bar sets to stacked series";
    for (QBarSet* bs : barSets) {
        stackedSeries->append(bs);
    }

    qDebug() << "[CHART] Adding series to chart";
    chart->addSeries(stackedSeries);

    // Set up axes
    qDebug() << "[AXIS] Setting up X axis with" << timeCategories.size() << "categories";
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(timeCategories);
    axisX->setLabelsColor(QColor(234, 234, 234));
    QFont axisXFont = axisX->labelsFont();
    axisXFont.setPointSize(10);
    axisX->setLabelsFont(axisXFont);
    chart->addAxis(axisX, Qt::AlignBottom);

    // Calculate max value for Y axis
    int maxVal = 1;
    for (int val : timePeriodTotals) {
        if (val > maxVal) maxVal = val;
    }
    qDebug() << "[AXIS] Y axis max value:" << maxVal;

    qDebug() << "[AXIS] Setting up Y axis with range [0," << (maxVal + maxVal * 0.2) << "]";
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Total");
    axisY->setLabelFormat("%d");
    axisY->setLabelsColor(QColor(234, 234, 234));
    QFont axisYFont = axisY->labelsFont();
    axisYFont.setPointSize(10);
    axisY->setLabelsFont(axisYFont);
    axisY->setTitleBrush(QBrush(QColor(0, 217, 255)));
    axisY->setRange(0, maxVal + maxVal * 0.2);
    chart->addAxis(axisY, Qt::AlignLeft);

    // Attach axes to the single series
    qDebug() << "[AXIS] Attaching axes to series";
    stackedSeries->attachAxis(axisX);
    stackedSeries->attachAxis(axisY);

    // Set chart title
    qDebug() << "[CHART] Setting title:" << ("按时间统计 (" + timeRange + ")");
    chart->setTitle("按时间统计 (" + timeRange + ")");

    // Set up legend
    qDebug() << "[LEGEND] Configuring legend";
    chart->legend()->show();
    chart->legend()->setLabelColor(QColor(234, 234, 234));
    chart->legend()->setBrush(QBrush(QColor(22, 33, 62, 200)));
    chart->legend()->setPen(QPen(QColor(60, 80, 100)));
    QFont legendFont = chart->legend()->font();
    legendFont.setPointSize(9);
    chart->legend()->setFont(legendFont);
    chart->legend()->setAlignment(Qt::AlignTop);

    // Install event filter on the by-time chart for tooltip
    qDebug() << "[TOOLTIP] Installing event filter";
    m_chartViewPlatformByTime->setMouseTracking(true);
    if (!m_chartViewPlatformByTime->property("_eventFilterInstalled").toBool()) {
        m_chartViewPlatformByTime->installEventFilter(this);
        m_chartViewPlatformByTime->viewport()->installEventFilter(this);
        m_chartViewPlatformByTime->setProperty("_eventFilterInstalled", true);
    }

    // Store time period totals for tooltip
    m_byTimePlatformTotals.clear();
    m_byTimePlatformTotals.append(timePeriodTotals);

    // Wait for layout to complete, then add total labels above each bar
    qDebug() << "[LABELS] Scheduling delayed label update (500ms)";
    QTimer::singleShot(500, this, [chart, timeCategories, timePeriodTotals, axisY]() {
        QRectF plotArea = chart->plotArea();

        bool validPlotArea = plotArea.width() > 200 && plotArea.height() > 50;
        if (!validPlotArea) {
            qDebug() << "[PlatformByTime] plotArea not valid, skipping labels";
            return;
        }

        int numTimeCategories = timeCategories.size();
        if (numTimeCategories == 0) return;

        // Calculate bar positions for single bar per time category (matching platform tab formula)
        qreal barGroupWidth = plotArea.width() / numTimeCategories;
        qreal barWidth = barGroupWidth * 0.6;
        qreal barLeftMargin = (barGroupWidth - barWidth) / 2;

        qreal yMin = axisY->min();
        qreal yMax = axisY->max();
        qreal yRange = yMax - yMin;

        for (int ti = 0; ti < numTimeCategories; ++ti) {
            if (timePeriodTotals[ti] <= 0) continue;

            // Calculate X position for the bar center (matching platform tab formula)
            qreal barCenterX = plotArea.left() + barLeftMargin + ti * barGroupWidth + barWidth / 2;

            // Calculate Y position for the top of the stacked bar
            qreal normalizedValue = (timePeriodTotals[ti] - yMin) / yRange;
            qreal barTopY = plotArea.bottom() - normalizedValue * plotArea.height();

            // Create text item for total
            QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(QString::number(timePeriodTotals[ti]));
            textItem->setFont(QFont("Arial", 9, QFont::Bold));
            textItem->setBrush(QBrush(QColor(255, 255, 255)));
            textItem->setZValue(100);
            chart->scene()->addItem(textItem);

            // Position text centered above the bar
            qreal textX = barCenterX - textItem->boundingRect().width() / 2;
            qreal textY = barTopY - textItem->boundingRect().height() - 2;
            textItem->setPos(textX, textY);
        }
        qDebug() << "[LABELS] Added total labels above bars";
    });

    qDebug() << "=== updatePlatformByTimeChart EXIT (chart update complete) ===";
}


void Defect_Data_Display::onDataLoaded_PlatformAoiResult(const QMap<QString, QMap<int, QMap<QString, int>>>& platformAoiResultData, const QStringList& aoiResultCategories, const QString& timeRange)
{
    // Save the data to member variable for tooltip usage
    m_platformAoiResultData = platformAoiResultData;
    // Force OK/NG categories (from SQL reclassification)
    m_aoiResultCategories = (aoiResultCategories.isEmpty()) ? QStringList{"OK", "NG"} : aoiResultCategories;
    qDebug() << "[PlatformAoiResult] Saved" << m_platformAoiResultData.size() << "time periods, categories:" << m_aoiResultCategories;

    // Use stacked bar chart to show different AOIResult types (OK/NG)
    if (!platformAoiResultData.isEmpty() && !m_aoiResultCategories.isEmpty()) {
        updatePlatformTrendChartStacked(platformAoiResultData, m_aoiResultCategories);
    } else if (!m_platformTrendData.isEmpty()) {
        // Fallback to non-stacked chart if no AOIResult detail
        updatePlatformTrendChart(m_platformTrendData);
    }

    // If user is on the by-time tab, also update the by-time chart
    if (ui.tabPlatformPages->currentIndex() == 0 && m_chartViewPlatformByTime) {
        updatePlatformByTimeChart();
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

    // Create a bar series for each defect type
    QMap<QString, QBarSeries*> seriesMap;
    QMap<QString, QBarSet*> barSetMap;
    for (const QString& defectType : allDefectTypes) {
        QBarSet* barSet = new QBarSet(defectType);
        barSet->setColor(defectColors.value(defectType, Qt::gray));
        barSet->setLabelColor(QColor(234, 234, 234));
        barSetMap[defectType] = barSet;

        QBarSeries* series = new QBarSeries();
        series->setLabelsVisible(true);
        series->setLabelsFormat("@value");
        series->setLabelsPosition(QBarSeries::LabelsOutsideEnd);
        series->append(barSet);
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
            *(barSetMap[defectType]) << count;
        }
        index++;
    }

    // Add series to chart
    for (auto series : seriesMap.values()) {
        chart->addSeries(series);
    }
    chart->setTitle("Defect Analysis (" + timeRange + ")");

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
        int total = 0;
        for (auto typeIt = it.value().constBegin(); typeIt != it.value().constEnd(); ++typeIt) {
            total += typeIt.value();
        }
        if (total > maxCount) maxCount = total;
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

    // Pass chart (hidden tab) - if not initialized, skip this function
    if (!m_chartViewInspectionPass || !m_chartViewInspectionFail) {
        qDebug() << "Inspection charts not initialized, skipping update";
        return;
    }

    QChart* passChart = ((QChartView*)m_chartViewInspectionPass)->chart();
    passChart->removeAllSeries();
    for (auto axis : passChart->axes()) {
        passChart->removeAxis(axis);
    }

    // Fail chart (hidden tab)
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
                                   const QString& searchScreenId, QObject* parent,
                                   int startHour, int endHour)
    : QThread(parent)
    , m_loadId(loadId)
    , m_timeRange(timeRange)
    , m_dateRange(dateRange)
    , m_searchScreenId(searchScreenId)
    , m_startHour(startHour)
    , m_endHour(endHour)
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

    // Add hourly time range filter if specified
    if (m_timeRange == "按小时" && m_startHour >= 0 && m_endHour >= 0) {
        // Add time-of-day filter: HOUR(StartTime) between start and end
        int startHour = m_startHour;
        int endHour = m_endHour;
        
        if (startHour <= endHour) {
            // Normal range: e.g., 8:00 to 17:00
            queryCondition += QString(" AND HOUR(StartTime) >= %1 AND HOUR(StartTime) <= %2")
                              .arg(startHour).arg(endHour);
        } else {
            // Overnight range: e.g., 22:00 to 06:00 (crosses midnight)
            queryCondition += QString(" AND (HOUR(StartTime) >= %1 OR HOUR(StartTime) <= %2)")
                              .arg(startHour).arg(endHour);
        }
        qDebug() << "[Chart] Hourly time filter: startHour=" << startHour << "endHour=" << endHour;
    }

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
        FROM ivs_lcd_inspectionresult
        WHERE %2
        GROUP BY time_period, PlatformID
        ORDER BY time_period, PlatformID
    )").arg(timeFormat).arg(queryCondition);
    qDebug() << "[Chart] Platform trend query:" << platformTrendQuery;

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
            qDebug() << "[Chart] Platform" << platformId << "Period" << period << "Pass:" << pass << "Fail:" << fail;
        }
    } else {
        qDebug() << "Platform trend query failed:" << platformTrendQ.lastError().text();
    }

    // Query 2: Defect trend by time period
    qDebug() << "Querying defect trend...";
    QString defectTrendQuery = QString(R"(
        SELECT %1 as time_period, Type, COUNT(*) as cnt
        FROM ivs_lcd_aoidefect
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
        FROM ivs_lcd_inspectionresult
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

    // Query 1b: Platform AOIResult detail by time period (per platform, per Grade_AOI)
    // Reclassify: R1 -> NG, all other values -> OK
    qDebug() << "Querying platform Grade_AOI detail...";
    QString platformAoiResultQuery = QString(R"(
        SELECT %1 as time_period, PlatformID,
               CASE WHEN Grade_AOI = 'R1' THEN 'NG' ELSE 'OK' END as grade_class,
               COUNT(*) as cnt
        FROM ivs_lcd_inspectionresult
        WHERE %2
        GROUP BY time_period, PlatformID, grade_class
        ORDER BY time_period, PlatformID, grade_class
    )").arg(timeFormat).arg(queryCondition);

    QSqlQuery platformAoiResultQ(db);
    platformAoiResultQ.setForwardOnly(true);
    QMap<QString, QMap<int, QMap<QString, int>>> platformAoiResultData;
    QStringList aoiResultCategories;

    if (platformAoiResultQ.exec(platformAoiResultQuery)) {
        while (platformAoiResultQ.next()) {
            QString period = platformAoiResultQ.value(0).toString();
            int platformId = platformAoiResultQ.value(1).toInt();
            QString gradeClass = platformAoiResultQ.value(2).toString();
            int cnt = platformAoiResultQ.value(3).toInt();
            platformAoiResultData[period][platformId][gradeClass] = cnt;
            if (!aoiResultCategories.contains(gradeClass)) {
                aoiResultCategories.append(gradeClass);
            }
        }
    } else {
        qDebug() << "Platform Grade_AOI query failed:" << platformAoiResultQ.lastError().text();
    }

    // Query 3: Platform trend by Grade_AOI (for line chart by grade type)
    qDebug() << "Querying platform Grade_AOI trend...";
    QStringList allGrades;
    QMap<QString, QMap<QString, int>> platformGradeTrendData;
    QString gradeTrendQuery = QString("SELECT %1 as time_period, Grade_AOI, COUNT(*) as cnt "
        "FROM ivs_lcd_inspectionresult "
        "WHERE %2 "
        "GROUP BY time_period, Grade_AOI "
        "ORDER BY time_period, Grade_AOI")
        .arg(timeFormat).arg(queryCondition);
    qDebug() << "[Chart] Grade trend query:" << gradeTrendQuery;
    QSqlQuery gradeQuery(db);
    gradeQuery.setForwardOnly(true);
    if (gradeQuery.exec(gradeTrendQuery)) {
        while (gradeQuery.next()) {
            QString timePeriod = gradeQuery.value(0).toString();
            QString grade = gradeQuery.value(1).toString();
            int cnt = gradeQuery.value(2).toInt();
            platformGradeTrendData[timePeriod][grade] = cnt;
            if (!allGrades.contains(grade)) {
                allGrades.append(grade);
            }
        }
        qDebug() << "[Chart] Grade trend data loaded:" << platformGradeTrendData.size() << "time periods, grades:" << allGrades;
    } else {
        qDebug() << "[Chart] Grade trend query failed:" << gradeQuery.lastError().text();
    }

    // Emit trend data signals
    emit platformTrendLoaded(platformTrendData, m_timeRange);
    emit platformAoiResultLoaded(platformAoiResultData, aoiResultCategories, m_timeRange);
    emit defectTrendLoaded(defectTrendData, m_timeRange);
    emit inspectionTrendLoaded(inspectionTrendData, m_timeRange);
    emit platformGradeTrendLoaded(platformGradeTrendData, allGrades, m_timeRange);

    // Original combined query for totals
    qDebug() << "Executing combined optimized query for totals...";

    QString combinedQueryStr = QString(R"(
        SELECT 'aoi' as query_type, Type as defect_type, COUNT(*) as cnt, 0 as platform_id, 0 as pass_cnt, 0 as fail_cnt
        FROM ivs_lcd_aoidefect
        WHERE %1
        GROUP BY Type
        UNION ALL
        SELECT 'insp_total', '', COUNT(*), 0, SUM(IF(AOIResult = 'OK', 1, 0)), SUM(IF(AOIResult != 'OK', 1, 0))
        FROM ivs_lcd_inspectionresult
        WHERE %1
        UNION ALL
        SELECT 'insp_platform', '', PlatformID, PlatformID, SUM(IF(AOIResult = 'OK', 1, 0)), SUM(IF(AOIResult != 'OK', 1, 0))
        FROM ivs_lcd_inspectionresult
        WHERE %1
        GROUP BY PlatformID
    )").arg(queryCondition);

    QSqlQuery combinedQuery(db);
    combinedQuery.setForwardOnly(true);
    combinedQuery.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

    if (!combinedQuery.exec(combinedQueryStr)) {
        qDebug() << "Combined query failed:" << combinedQuery.lastError().text();
        combinedQuery = QSqlQuery();
        gradeQuery = QSqlQuery();
        platformAoiResultQ = QSqlQuery();
        inspectionTrendQ = QSqlQuery();
        defectTrendQ = QSqlQuery();
        platformTrendQ = QSqlQuery();
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

    combinedQuery = QSqlQuery();
    gradeQuery = QSqlQuery();
    platformAoiResultQ = QSqlQuery();
    inspectionTrendQ = QSqlQuery();
    defectTrendQ = QSqlQuery();
    platformTrendQ = QSqlQuery();
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
    if (timeRange == "按小时") {
        const int startHour = qMin(m_searchStartHour, m_searchEndHour);
        const int endHour = qMax(m_searchStartHour, m_searchEndHour);
        if (cache->startHour != startHour || cache->endHour != endHour) {
            return false;
        }
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
    clearDetailView();

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
            FROM ivs_lcd_aoidefect
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

        // Query inspection results grouped by time period AND Grade_AOI
        QString trendQuery = QString(R"(
            SELECT
                %1 as time_period,
                Grade_AOI,
                COUNT(*) as cnt
            FROM ivs_lcd_inspectionresult
            WHERE %2
            GROUP BY time_period, Grade_AOI
            ORDER BY time_period, Grade_AOI
        )").arg(timeFormat).arg(m_dateRange);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

        if (query.exec(trendQuery)) {
            // Data structure: time_period -> (Grade_AOI -> count)
            QMap<QString, QMap<QString, int>> trendData;
            QMap<QString, int> totalPerPeriod;
            QStringList allGrades;

            while (query.next()) {
                QString period = query.value(0).toString();
                QString grade = query.value(1).toString();
                int cnt = query.value(2).toInt();

                trendData[period][grade] = cnt;
                totalPerPeriod[period] += cnt;
                if (!allGrades.contains(grade)) {
                    allGrades.append(grade);
                }
            }

            // Compute defect rate per grade
            QMap<QString, QMap<QString, double>> defectRates;
            for (auto periodIt = trendData.constBegin(); periodIt != trendData.constEnd(); ++periodIt) {
                QString period = periodIt.key();
                int total = totalPerPeriod.value(period, 0);
                for (auto gradeIt = periodIt.value().constBegin(); gradeIt != periodIt.value().constEnd(); ++gradeIt) {
                    QString grade = gradeIt.key();
                    int cnt = gradeIt.value();
                    double rate = (total > 0) ? (cnt * 100.0 / total) : 0;
                    defectRates[period][grade] = rate;
                }
            }

            // Sort grades: OK first, then R1-R5, then others alphabetically
            QStringList sortedGrades = allGrades;
            std::sort(sortedGrades.begin(), sortedGrades.end(), [](const QString& a, const QString& b) {
                if (a == "OK") return true;
                if (b == "OK") return false;
                if (a.startsWith("R") && b.startsWith("R")) {
                    bool aOk, bOk;
                    int aNum = a.mid(1).toInt(&aOk);
                    int bNum = b.mid(1).toInt(&bOk);
                    if (aOk && bOk) return aNum < bNum;
                }
                return a < b;
            });

            emit trendDataLoaded(trendData, defectRates, sortedGrades);
        } else {
            qDebug() << "Trend query failed:" << query.lastError().text();
        }
    }
    else if (m_tabIndex == 5) {
        // Query raw Grade_AOI values (R1, R2, R3, etc.)
        QString queryStr = QString(R"(
            SELECT 
                Grade_AOI as grade_result,
                COUNT(*) as cnt
            FROM ivs_lcd_inspectionresult
            WHERE %1
            GROUP BY grade_result
            ORDER BY grade_result
        )").arg(m_dateRange);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.setNumericalPrecisionPolicy(QSql::LowPrecisionDouble);

        if (query.exec(queryStr)) {
            QList<QVariantList> defectDetails;

            while (query.next()) {
                QVariantList row;
                row.append(query.value(0).toString());  // grade_result (R1, R2, R3, etc.)
                row.append(query.value(1).toInt());     // count
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
              AND Status LIKE CONVERT(0x%3 USING utf8)
              AND MarkID >= 1 AND MarkID <= 16
            GROUP BY time_period, MarkID
            ORDER BY time_period, MarkID
        )").arg(timeFormat).arg(m_dateRange).arg(QString::fromLatin1(QStringLiteral("%%定位异常%%").toUtf8().toHex()));
        qDebug().noquote() << "Location abnormal async query template:" << queryStr;

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
        query.finish();
        query.clear();
    }

    db.close();
    db = QSqlDatabase();
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
          AND Status LIKE CONVERT(0x%3 USING utf8mb4)
          AND MarkID >= 1 AND MarkID <= 16
        GROUP BY time_period, MarkID
        ORDER BY time_period, MarkID
    )").arg(timeFormat).arg(dateRange).arg(QString::fromLatin1(QStringLiteral("%%定位异常%%").toUtf8().toHex()));
    qDebug().noquote() << "Location abnormal query template:" << queryStr;

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
    query.finish();
    query.clear();

    db.close();
    db = QSqlDatabase();
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
    m_locationAbnormalCache.startHour = (ui.comboTimeRange->currentText() == "按小时") ? qMin(m_searchStartHour, m_searchEndHour) : -1;
    m_locationAbnormalCache.endHour = (ui.comboTimeRange->currentText() == "按小时") ? qMax(m_searchStartHour, m_searchEndHour) : -1;

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
        const int startHour = qMin(m_searchStartHour, m_searchEndHour);
        const int endHour = qMax(m_searchStartHour, m_searchEndHour);
        for (int h = startHour; h <= endHour; ++h) {
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

void Defect_Data_Display::showBarClickDialog(int platformIdx, const QString& timeKey)
{
    // Delete existing dialog if any
    if (m_barClickDialog) {
        m_barClickDialog->deleteLater();
        m_barClickDialog = nullptr;
    }

    // Create new dialog
    m_barClickDialog = new QDialog(this);
    m_barClickDialog->setModal(true);
    m_barClickDialog->resize(1200, 800);

    qDebug() << "[BarClickDialog] platformIdx:" << platformIdx << "timeKey:" << timeKey;

    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(m_barClickDialog);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    m_barClickDialog->installEventFilter(m_barClickDialog);

    // Table widget
    QTableWidget* tableWidget = new QTableWidget(m_barClickDialog);
    tableWidget->setColumnCount(7);
    tableWidget->setHorizontalHeaderLabels(QStringList() << "StartTime" << "ScreenID" << "MarkID" << "AOIResult" << "Grade_AOI" << "Code_AOI" << "Status");
    tableWidget->setAlternatingRowColors(true);
    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget->horizontalHeader()->setStretchLastSection(true);
    tableWidget->verticalHeader()->setVisible(false);

    // Table styling
    tableWidget->setStyleSheet(R"(
        QTableWidget {
            background-color: rgba(20, 35, 55, 220);
            alternate-background-color: rgba(30, 50, 80, 180);
            color: #e0f0ff;
            border: 1px solid rgba(0, 217, 255, 80);
            border-radius: 6px;
            gridline-color: rgba(0, 217, 255, 40);
            font-size: 13px;
        }
        QTableWidget::item {
            padding: 5px;
        }
        QTableWidget::item:selected {
            background-color: rgba(0, 150, 200, 150);
            color: #ffffff;
        }
        QHeaderView::section {
            background-color: rgba(0, 100, 150, 180);
            color: #ffffff;
            padding: 6px;
            border: 1px solid rgba(0, 217, 255, 60);
            font-weight: bold;
            font-size: 13px;
        }
        QScrollBar:vertical {
            background: rgba(20, 35, 55, 200);
            width: 12px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical {
            background: rgba(0, 150, 200, 150);
            border-radius: 5px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(0, 200, 255, 200);
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )");

    tableWidget->setMinimumHeight(550);
    tableWidget->verticalHeader()->setDefaultSectionSize(22);
    tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    mainLayout->addWidget(tableWidget);
    mainLayout->setStretchFactor(tableWidget, 10);  // Table gets 10x stretch

    // Pagination controls
    QHBoxLayout* pageLayout = new QHBoxLayout();
    pageLayout->setSpacing(10);

    QLabel* pageLabel = new QLabel("第 1 页，共 1 页", m_barClickDialog);
    pageLabel->setStyleSheet("color: #e0f0ff; font-size: 13px;");

    QPushButton* prevBtn = new QPushButton("上一页", m_barClickDialog);
    QPushButton* nextBtn = new QPushButton("下一页", m_barClickDialog);

    prevBtn->setEnabled(false);
    nextBtn->setEnabled(false);

    // Button styling
    QString btnStyle = R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 120, 170, 180),
                stop:1 rgba(0, 80, 130, 180));
            color: #ffffff;
            border: 1px solid rgba(0, 217, 255, 100);
            border-radius: 6px;
            padding: 8px 20px;
            font-size: 13px;
            font-weight: bold;
            min-width: 80px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 160, 220, 220),
                stop:1 rgba(0, 110, 170, 220));
            border: 1px solid #00d9ff;
        }
        QPushButton:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 80, 110, 180),
                stop:1 rgba(0, 60, 90, 180));
            padding-top: 9px;
            padding-bottom: 7px;
        }
        QPushButton:!enabled {
            background-color: rgba(40, 55, 70, 150);
            color: #607080;
            border: 1px solid rgba(0, 217, 255, 40);
        }
    )";
    prevBtn->setStyleSheet(btnStyle);
    nextBtn->setStyleSheet(btnStyle);

    pageLayout->addStretch();
    pageLayout->addWidget(prevBtn);
    pageLayout->addWidget(pageLabel);
    pageLayout->addWidget(nextBtn);
    pageLayout->addStretch();

    mainLayout->addLayout(pageLayout);
    mainLayout->setStretchFactor(pageLayout, 1);  // Pagination gets 1x stretch

    // Pagination state
    int currentPage = 1;
    int totalPages = 1;
    const int pageSize = 50;

    // Get the full time key from the data - need to include date
    QString fullTimeKey = timeKey;
    QString timeRange = ui.comboTimeRange->currentText();
    QDate selectedDate = m_selectedDate;

    // Build full timestamp with date
    if (timeRange == "按小时") {
        // timeKey is like "05:00", need to add date: "2026-05-05 05:00"
        fullTimeKey = selectedDate.toString("yyyy-MM-dd") + " " + timeKey;
    } else if (timeRange == "按天") {
        // timeKey is like "2026-05-05", use as is
        fullTimeKey = timeKey;
    } else if (timeRange == "按月") {
        // timeKey is like "2026-05", use as is
        fullTimeKey = timeKey;
    }

    // Now set the title and window title since we have timeRange
    // Show time range in title: "2:00" -> "2:00到3:00"
    QString timeRangeText = timeRange;
    if (timeRange == "按小时") {
        // Parse the hour and show next hour
        bool ok;
        int hour = timeKey.left(2).toInt(&ok);
        int nextHour = (hour + 1) % 24;
        timeRangeText = QString("%1:00到%2:00").arg(hour, 2, 10, QChar('0')).arg(nextHour, 2, 10, QChar('0'));
    }
    m_barClickDialog->setWindowTitle(platformIdx >= 0
        ? QString("工位%1 %2 缺陷记录").arg(platformIdx + 1).arg(timeRangeText)
        : QString("全部工位 %1 缺陷记录").arg(timeRangeText));

    // Lambda to load page data
    auto loadPageData = [&](int page) {
        // Create a completely independent database connection to the correct database
        QString connectionName = QString("barclick_%1_%2")
            .arg(reinterpret_cast<quintptr>(m_barClickDialog))
            .arg(QDateTime::currentMSecsSinceEpoch());
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connectionName);

        // Use the same connection string as worker threads (ivs_lcd database)
        QString connStr = "DRIVER={MySQL ODBC 5.3 ANSI Driver};"
                          "SERVER=localhost;"
                          "PORT=3306;"
                          "DATABASE=ivs_lcd;"
                          "UID=root;"
                          "PWD=123456;"
                          "OPTION=8;";
        db.setDatabaseName(connStr);

        if (!db.open()) {
            qDebug() << "[BarClickDialog] Failed to open database:" << db.lastError().text();
            QMessageBox::warning(m_barClickDialog, "错误", "数据库连接失败");
            return;
        }

        int offset = (page - 1) * pageSize;

        // Build the time filter based on time range
        // Use full timestamp for accurate filtering (2:00 means 02:00:00 to 02:59:59)
        QString timeFilter;
        if (timeRange == "按小时") {
            // Filter records from fullTimeKey (e.g., "2026-05-05 05:00") to the next hour
            // We use BETWEEN to get exact time range: 05:00:00 to 05:59:59
            QString startTime = fullTimeKey;
            // Calculate end time (next hour)
            QDateTime start = QDateTime::fromString(startTime, "yyyy-MM-dd HH:mm");
            QString endTime = start.addSecs(3600).toString("yyyy-MM-dd HH:mm");
            timeFilter = QString("StartTime >= '%1' AND StartTime < '%2'").arg(startTime).arg(endTime);
        } else if (timeRange == "按天") {
            // Filter for entire day
            timeFilter = QString("StartTime >= '%1 00:00:00' AND StartTime < '%2 00:00:00'")
                .arg(fullTimeKey)
                .arg(selectedDate.addDays(1).toString("yyyy-MM-dd"));
        } else if (timeRange == "按月") {
            // Filter for entire month
            QDate date = QDate::fromString(fullTimeKey + "-01", "yyyy-MM-dd");
            QDate nextMonth = date.addMonths(1);
            timeFilter = QString("StartTime >= '%1' AND StartTime < '%2'")
                .arg(fullTimeKey + "-01 00:00:00")
                .arg(nextMonth.toString("yyyy-MM-dd") + " 00:00:00");
        } else {
            timeFilter = fullTimeKey + "%";
        }

        // Use non-prepared statement to avoid ODBC issues
        // Only show non-OK records (defects)
        // Note: The table is ivs_lcd_inspectionresult, PlatformID is 0-based (0,1,2,3)
        QString sql;
        if (platformIdx >= 0) {
            sql = QString("SELECT StartTime, ScreenID, MarkID, AOIResult, Grade_AOI, Code_AOI, Status, LocalIP, PlatformID FROM ivs_lcd_inspectionresult WHERE PlatformID = %1 AND AOIResult != 'OK' AND %2 ORDER BY StartTime DESC LIMIT %3 OFFSET %4")
                .arg(platformIdx)
                .arg(timeFilter)
                .arg(pageSize)
                .arg(offset);
        } else {
            sql = QString("SELECT StartTime, ScreenID, MarkID, AOIResult, Grade_AOI, Code_AOI, Status, LocalIP, PlatformID FROM ivs_lcd_inspectionresult WHERE AOIResult != 'OK' AND %1 ORDER BY StartTime DESC LIMIT %2 OFFSET %3")
                .arg(timeFilter)
                .arg(pageSize)
                .arg(offset);
        }

        QSqlQuery query(db);
        if (!query.exec(sql)) {
            qDebug() << "Query error:" << query.lastError().text();
            QMessageBox::warning(m_barClickDialog, "错误", "查询失败: " + query.lastError().text());
            db.close();
            QSqlDatabase::removeDatabase(connectionName);
            return;
        }

        tableWidget->setRowCount(0);
        int row = 0;
        while (query.next()) {
            tableWidget->insertRow(row);
            for (int col = 0; col < 7; ++col) {
                QTableWidgetItem* item = new QTableWidgetItem(query.value(col).toString());
                item->setTextAlignment(Qt::AlignCenter);
                tableWidget->setItem(row, col, item);
            }

            tableWidget->setRowHeight(row, 24);
            tableWidget->item(row, 0)->setData(Qt::UserRole, query.value(1).toString());
            tableWidget->item(row, 0)->setData(Qt::UserRole + 1, query.value(7).toString());
            tableWidget->item(row, 0)->setData(Qt::UserRole + 2, query.value(8).toInt());
            tableWidget->item(row, 0)->setData(Qt::UserRole + 3, query.value(0).toDateTime());
            ++row;
        }

        // Update page info
        pageLabel->setText(QString("第 %1 页，共 %2 页 (共 %3 条记录)").arg(page).arg(totalPages).arg(tableWidget->rowCount()));

        // Update button states
        prevBtn->setEnabled(page > 1);
        nextBtn->setEnabled(page < totalPages);

        db.close();
        QSqlDatabase::removeDatabase(connectionName);
    };

    // Get total count first - use same time filter logic as loadPageData
    QString countTimeFilter;
    if (timeRange == "按小时") {
        QString startTime = fullTimeKey;
        QDateTime start = QDateTime::fromString(startTime, "yyyy-MM-dd HH:mm");
        QString endTime = start.addSecs(3600).toString("yyyy-MM-dd HH:mm");
        countTimeFilter = QString("StartTime >= '%1' AND StartTime < '%2'").arg(startTime).arg(endTime);
    } else if (timeRange == "按天") {
        countTimeFilter = QString("StartTime >= '%1 00:00:00' AND StartTime < '%2 00:00:00'")
            .arg(fullTimeKey)
            .arg(selectedDate.addDays(1).toString("yyyy-MM-dd"));
    } else if (timeRange == "按月") {
        QDate date = QDate::fromString(fullTimeKey + "-01", "yyyy-MM-dd");
        QDate nextMonth = date.addMonths(1);
        countTimeFilter = QString("StartTime >= '%1' AND StartTime < '%2'")
            .arg(fullTimeKey + "-01 00:00:00")
            .arg(nextMonth.toString("yyyy-MM-dd") + " 00:00:00");
    } else {
        countTimeFilter = fullTimeKey + "%";
    }

    // Get the connection string from worker threads (ivs_lcd database)
    QString connStr = "DRIVER={MySQL ODBC 5.3 ANSI Driver};"
                       "SERVER=localhost;"
                       "PORT=3306;"
                       "DATABASE=ivs_lcd;"
                       "UID=root;"
                       "PWD=123456;"
                       "OPTION=8;";

    // Create a completely independent database connection
    QString countConnectionName = QString("barclick_count_%1_%2")
        .arg(reinterpret_cast<quintptr>(m_barClickDialog))
        .arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase countDb = QSqlDatabase::addDatabase("QODBC", countConnectionName);
    countDb.setDatabaseName(connStr);

    if (!countDb.open()) {
        qDebug() << "[BarClickDialog] Failed to open count database:" << countDb.lastError().text();
        QMessageBox::warning(m_barClickDialog, "错误", "数据库连接失败");
    } else {
        // Use non-prepared statement
        // Only count non-OK records (defects)
        // Note: The table is ivs_lcd_inspectionresult, PlatformID is 0-based (0,1,2,3)
        QString sql;
        if (platformIdx >= 0) {
            sql = QString("SELECT COUNT(*) FROM ivs_lcd_inspectionresult WHERE PlatformID = %1 AND AOIResult != 'OK' AND %2")
                .arg(platformIdx).arg(countTimeFilter);
        } else {
            sql = QString("SELECT COUNT(*) FROM ivs_lcd_inspectionresult WHERE AOIResult != 'OK' AND %1")
                .arg(countTimeFilter);
        }
        qDebug() << "[BarClickDialog] Count SQL:" << sql;
        QSqlQuery countQuery(countDb);
        countQuery.exec(sql);

        if (countQuery.next()) {
            int totalCount = countQuery.value(0).toInt();
            totalPages = (totalCount + pageSize - 1) / pageSize;
            if (totalPages < 1) totalPages = 1;
            pageLabel->setText(QString("第 1 页，共 %1 页 (共 %2 条记录)").arg(totalPages).arg(totalCount));
            nextBtn->setEnabled(totalPages > 1);
            qDebug() << "[BarClickDialog] Total count:" << totalCount;
        }
        countDb.close();
        QSqlDatabase::removeDatabase(countConnectionName);
    }

    // Load first page
    loadPageData(1);

    // Connect pagination buttons
    QObject::connect(tableWidget, &QTableWidget::cellDoubleClicked, this, [=](int row, int /*column*/) {
        QTableWidgetItem* startItem = tableWidget->item(row, 0);
        if (!startItem) {
            return;
        }

        QString screenId = startItem->data(Qt::UserRole).toString();
        QString localIp = startItem->data(Qt::UserRole + 1).toString();
        int rowPlatformId = startItem->data(Qt::UserRole + 2).toInt();
        QDateTime startTime = startItem->data(Qt::UserRole + 3).toDateTime();
        showInspectionImageDialog(screenId, rowPlatformId, localIp, startTime);
    });

    // Connect pagination buttons
    QObject::connect(prevBtn, &QPushButton::clicked, this, [&]() {
        if (currentPage > 1) {
            --currentPage;
            loadPageData(currentPage);
        }
    });

    QObject::connect(nextBtn, &QPushButton::clicked, this, [&]() {
        if (currentPage < totalPages) {
            ++currentPage;
            loadPageData(currentPage);
        }
    });

    // Dialog button box
    QDialogButtonBox* buttonBox = new QDialogButtonBox(m_barClickDialog);
    
    // Add Export button
    QPushButton* exportBtn = new QPushButton("导出到CSV", m_barClickDialog);
    exportBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 150, 100, 180),
                stop:1 rgba(0, 100, 60, 180));
            color: #ffffff;
            border: 1px solid rgba(0, 217, 150, 100);
            border-radius: 6px;
            padding: 8px 30px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 190, 140, 220),
                stop:1 rgba(0, 130, 90, 220));
            border: 1px solid #00ffa0;
        }
    )");
    buttonBox->addButton(exportBtn, QDialogButtonBox::ActionRole);
    
    // Add Close button
    QPushButton* closeBtn = buttonBox->addButton("关闭", QDialogButtonBox::AcceptRole);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 150, 200, 180),
                stop:1 rgba(0, 100, 160, 180));
            color: #ffffff;
            border: 1px solid rgba(0, 217, 255, 100);
            border-radius: 6px;
            padding: 8px 30px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(0, 190, 250, 220),
                stop:1 rgba(0, 130, 200, 220));
            border: 1px solid #00d9ff;
        }
    )");
    mainLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, m_barClickDialog, &QDialog::accept);

    // Connect export button - directly export data from tableWidget (no DB needed)
    QObject::connect(exportBtn, &QPushButton::clicked, this, [=]() {
        QString safeTimeKey = timeKey;
        safeTimeKey.replace(":", "-");

        QString fileName = QFileDialog::getSaveFileName(m_barClickDialog,
                                                        "导出数据",
                                                        QString("工位%1_%2_缺陷记录.csv").arg(platformIdx + 1).arg(safeTimeKey),
                                                        "CSV文件 (*.csv)");
        if (fileName.isEmpty())
            return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(m_barClickDialog, "导出失败", "无法创建文件: " + fileName);
            return;
        }

        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);

        // BOM for Excel UTF-8 recognition
        out << "\xEF\xBB\xBF";

        // Write header from table column names
        int colCount = tableWidget->columnCount();
        QStringList headers;
        for (int c = 0; c < colCount; ++c)
            headers << tableWidget->horizontalHeaderItem(c)->text();
        out << headers.join(",") << "\n";

        // Write all rows currently in the table
        int rowCount = tableWidget->rowCount();
        for (int r = 0; r < rowCount; ++r) {
            QStringList rowData;
            for (int c = 0; c < colCount; ++c) {
                QTableWidgetItem* item = tableWidget->item(r, c);
                QString cellText = item ? item->text() : "";
                // Escape commas and quotes in cell values
                if (cellText.contains(",") || cellText.contains("\"") || cellText.contains("\n")) {
                    cellText = "\"" + cellText.replace("\"", "\"\"") + "\"";
                }
                rowData << cellText;
            }
            out << rowData.join(",") << "\n";
        }

        file.close();
        QMessageBox::information(m_barClickDialog, "导出成功",
                                 QString("成功导出 %1 条记录到:\n%2").arg(rowCount).arg(fileName));
    });

    // Set dialog background
    m_barClickDialog->setStyleSheet("QDialog { background-color: rgba(15, 25, 45, 230); }");

    // Show dialog
    m_barClickDialog->exec();
}

void Defect_Data_Display::showInspectionImageDialog(const QString& screenId, int platformId, const QString& localIp, const QDateTime& startTime)
{
    QDialog dialog(this);
    dialog.setModal(true);
    dialog.resize(1100, 850);
    dialog.setWindowTitle(QString("图片预览 - %1").arg(screenId));
    dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowSystemMenuHint);
    dialog.setStyleSheet("QDialog { background-color: rgba(15, 25, 45, 235); color: #e0f0ff; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* infoLabel = new QLabel(&dialog);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #9fdcff; font-size: 13px;");

    QString platformFolder = QString::number(platformId + 1);
    QString dateFolder = startTime.isValid() ? startTime.date().toString("yyyy-MM-dd") : m_selectedDate.toString("yyyy-MM-dd");
    QString imageDir = QString::fromLocal8Bit(DEFAULT_MANUAL_MAIN_AOI_IMAGE_ROOT)
        + QString("MainAOI/%1/%2/%3/%4")
            .arg(localIp)
            .arg(platformFolder)
            .arg(dateFolder)
            .arg(screenId);
    QString imagePath = QDir::toNativeSeparators(QDir(imageDir).filePath("MarkImg.jpg"));

    infoLabel->setText(QString("ScreenID: %1\n工位: %2\n路径: %3")
        .arg(screenId)
        .arg(platformId + 1)
        .arg(imagePath));
    mainLayout->addWidget(infoLabel);

    QGraphicsView* imageView = new QGraphicsView(&dialog);
    imageView->setMinimumSize(900, 680);
    imageView->setAlignment(Qt::AlignCenter);
    imageView->setStyleSheet("QGraphicsView { background-color: #0f1a2d; border: 1px solid rgba(0, 217, 255, 80); border-radius: 6px; }");
    imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setRenderHint(QPainter::Antialiasing);
    imageView->setDragMode(QGraphicsView::NoDrag);

    QGraphicsScene* imageScene = new QGraphicsScene(imageView);
    imageView->setScene(imageScene);

    QPixmap pixmap;
    if (!QFileInfo::exists(imagePath) || !pixmap.load(imagePath)) {
        pixmap = QPixmap(900, 680);
        pixmap.fill(QColor(20, 35, 55));
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(0, 217, 255));
        painter.setFont(QFont("Microsoft YaHei", 20, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "暂无图片");
    }

    QGraphicsPixmapItem* pixmapItem = imageScene->addPixmap(pixmap);
    imageScene->setSceneRect(pixmap.rect());

    class ImageViewInteractionFilter : public QObject {
    public:
        ImageViewInteractionFilter(QGraphicsView* view, QGraphicsPixmapItem* item)
            : QObject(view), m_view(view), m_item(item), m_manualZoom(false) {}

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override {
            if (watched != m_view || !m_item || m_item->pixmap().isNull()) {
                return QObject::eventFilter(watched, event);
            }

            if (event->type() == QEvent::Show || event->type() == QEvent::Resize) {
                if (!m_manualZoom) {
                    fitToView();
                }
            } else if (event->type() == QEvent::Wheel) {
                QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
                const qreal scaleFactor = wheelEvent->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
                m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
                m_view->scale(scaleFactor, scaleFactor);
                m_manualZoom = true;
                updateDragMode();
                return true;
            } else if (event->type() == QEvent::MouseButtonDblClick) {
                fitToView();
                m_manualZoom = false;
                updateDragMode();
                return true;
            }

            return QObject::eventFilter(watched, event);
        }

    private:
        void fitToView() {
            m_view->resetTransform();
            m_view->fitInView(m_item, Qt::KeepAspectRatio);
        }

        void updateDragMode() {
            m_view->setDragMode(m_manualZoom ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
            m_view->viewport()->setCursor(m_manualZoom ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }

        QGraphicsView* m_view;
        QGraphicsPixmapItem* m_item;
        bool m_manualZoom;
    };

    imageView->installEventFilter(new ImageViewInteractionFilter(imageView, pixmapItem));
    mainLayout->addWidget(imageView, 1);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    dialog.exec();
}

void Defect_Data_Display::showGradeTypeDialog(const QString& gradeName)
{
    // Create new dialog
    QDialog dialog(this);
    dialog.setModal(true);
    dialog.resize(1200, 800);
    dialog.setWindowTitle(QString("等级类型: %1").arg(gradeName));

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(4);

    // ========== Code_AOI Distribution Pie Chart ==========
    // Query Code_AOI distribution for this grade
    QSqlDatabase chartDb = QSqlDatabase::addDatabase("QODBC", "chartDist_" + QString::number(QDateTime::currentMSecsSinceEpoch()));
    QString chartConnStr = "DRIVER={MySQL ODBC 5.3 ANSI Driver};SERVER=localhost;PORT=3306;DATABASE=ivs_lcd;UID=root;PWD=123456;OPTION=8;";
    chartDb.setDatabaseName(chartConnStr);

    if (chartDb.open()) {
        // Build time range filter
        QString timeRange = ui.comboTimeRange->currentText();
        QDate selectedDate = m_selectedDate;
        QString dateStr = selectedDate.toString("yyyy-MM-dd");
        QString timeFilter;

        if (timeRange == "按小时") {
            timeFilter = QString(" AND DATE(StartTime) = '%1' ").arg(dateStr);
        } else if (timeRange == "按天") {
            timeFilter = QString(" AND DATE(StartTime) = '%1' ").arg(dateStr);
        } else if (timeRange == "按月") {
            QString monthStr = dateStr.left(7);
            timeFilter = QString(" AND DATE_FORMAT(StartTime, '%Y-%m') = '%1' ").arg(monthStr);
        }

        // Query Code_AOI distribution
        QString distQueryStr = QString("SELECT CASE WHEN Code_AOI IS NULL OR Code_AOI = '' THEN '空' ELSE Code_AOI END AS CodeAOI, COUNT(*) AS cnt "
                                       "FROM ivs_lcd_inspectionresult WHERE Grade_AOI = '%1' %2 "
                                       "GROUP BY Code_AOI ORDER BY cnt DESC")
                                   .arg(gradeName).arg(timeFilter);

        QSqlQuery distQuery(chartDb);
        QMap<QString, qreal> pieData;
        if (distQuery.exec(distQueryStr)) {
            while (distQuery.next()) {
                QString codeAOI = distQuery.value(0).toString();
                qreal cnt = distQuery.value(1).toDouble();
                pieData[codeAOI] = cnt;
            }
        }

        // Query total count and this grade's ratio
        QString totalQueryStr = QString("SELECT COUNT(*) FROM ivs_lcd_inspectionresult WHERE 1=1 %1").arg(timeFilter);
        QSqlQuery totalQuery(chartDb);
        qreal totalAll = 0;
        if (totalQuery.exec(totalQueryStr) && totalQuery.next()) {
            totalAll = totalQuery.value(0).toDouble();
        }

        qreal gradeTotal = 0;
        for (auto it = pieData.constBegin(); it != pieData.constEnd(); ++it) {
            gradeTotal += it.value();
        }
        qreal gradeRatio = totalAll > 0 ? (gradeTotal / totalAll * 100) : 0;

            // Calculate total for percentage
            qreal total = 0;
            for (auto it = pieData.constBegin(); it != pieData.constEnd(); ++it) {
                total += it.value();
            }

            // Create pie chart if we have data
            if (!pieData.isEmpty()) {
                QLabel* chartTitle = new QLabel(QString("Code_AOI 分布 (等级占比: %1%)").arg(gradeRatio, 0, 'f', 2), &dialog);
                chartTitle->setStyleSheet("color: #00d9ff; font-size: 14px; font-weight: bold; padding: 0px; margin: 0px;");
                chartTitle->setAlignment(Qt::AlignCenter);
                mainLayout->addWidget(chartTitle, 0, Qt::AlignTop);

                QFrame* chartFrame = new QFrame(&dialog);
                chartFrame->setFixedHeight(680);
                chartFrame->setStyleSheet("QFrame { background-color: rgba(20, 35, 55, 200); border: 1px solid rgba(0, 217, 255, 60); border-radius: 6px; }");
                QVBoxLayout* chartLayout = new QVBoxLayout(chartFrame);
                chartLayout->setContentsMargins(4, 4, 4, 4);
                chartLayout->setSpacing(2);

                QPieSeries* pieSeries = new QPieSeries();
                QList<QString> colors = {"#00d9ff", "#ff6b6b", "#4ecdc4", "#ffe66d", "#a855f7", "#f97316", "#84cc16", "#ec4899"};
                int colorIdx = 0;
                for (auto it = pieData.constBegin(); it != pieData.constEnd(); ++it) {
                    qreal percentage = total > 0 ? (it.value() / total * 100) : 0;
                    qreal finalPercentage = gradeRatio * percentage / 100;
                    QPieSlice* slice = pieSeries->append(QString("%1: %2 (%3%)").arg(it.key()).arg(it.value()).arg(finalPercentage, 0, 'f', 2), it.value());
                    slice->setColor(QColor(colors[colorIdx % colors.size()]));
                    slice->setLabelVisible(true);
                    slice->setLabelColor(QColor("#e0f0ff"));
                    slice->setLabelFont(QFont("Arial", 10));
                    colorIdx++;

                    // Click slice to show Code_AOI filtered detail dialog with pagination
                    QString clickedCodeAOI = it.key();
                    connect(slice, &QPieSlice::clicked, this, [=]() {
                        QDialog filterDialog(this);
                        filterDialog.setModal(true);
                        filterDialog.resize(1200, 750);
                        filterDialog.setWindowTitle(QString("等级: %1 / Code_AOI: %2").arg(gradeName).arg(clickedCodeAOI));
                        filterDialog.setStyleSheet("QDialog { background-color: rgba(15, 25, 45, 230); color: #e0f0ff; }");

                        QVBoxLayout* fLayout = new QVBoxLayout(&filterDialog);
                        fLayout->setContentsMargins(10, 10, 10, 10);
                        fLayout->setSpacing(8);

                        // DB connection
                        QString fConnName = QString("slicefilter_%1").arg(QDateTime::currentMSecsSinceEpoch());
                        QSqlDatabase fDb = QSqlDatabase::addDatabase("QODBC", fConnName);
                        fDb.setDatabaseName(chartConnStr);
                        if (!fDb.open()) {
                            QMessageBox::warning(this, "错误", "数据库连接失败");
                            return;
                        }

                        // Time range filter
                        QString fTimeRange = ui.comboTimeRange->currentText();
                        QDate fDate = m_selectedDate;
                        QString fDateStr = fDate.toString("yyyy-MM-dd");
                        QString fTimeFilter;
                        if (fTimeRange == "按小时" || fTimeRange == "按天") {
                            fTimeFilter = QString(" AND DATE(StartTime) = '%1' ").arg(fDateStr);
                        } else if (fTimeRange == "按月") {
                            fTimeFilter = QString(" AND DATE_FORMAT(StartTime, '%Y-%m') = '%1' ").arg(fDateStr.left(7));
                        }

                        // Load ALL data into memory at once
                        struct FRowData {
                            QString startTime, screenId, MarkID, aoiResult, gradeAOI, codeAOI, status, localIP;
                            int platformId;
                        };
                        QList<FRowData> fAllRows;
                        QString fLoadAllStr = QString(
                            "SELECT StartTime, ScreenID, MarkID, AOIResult, Grade_AOI, Code_AOI, Status, LocalIP, PlatformID "
                            "FROM ivs_lcd_inspectionresult WHERE Grade_AOI = '%1' AND Code_AOI = '%2' %3 "
                            "ORDER BY StartTime DESC"
                        ).arg(gradeName).arg(clickedCodeAOI).arg(fTimeFilter);
                        QSqlQuery fLoadAllQuery(fDb);
                        if (fLoadAllQuery.exec(fLoadAllStr)) {
                            while (fLoadAllQuery.next()) {
                                FRowData row;
                                row.startTime = fLoadAllQuery.value(0).toString();
                                row.screenId = fLoadAllQuery.value(1).toString();
                                row.MarkID = fLoadAllQuery.value(2).toString();
                                row.aoiResult = fLoadAllQuery.value(3).toString();
                                row.gradeAOI = fLoadAllQuery.value(4).toString();
                                row.codeAOI = fLoadAllQuery.value(5).toString();
                                row.status = fLoadAllQuery.value(6).toString();
                                row.localIP = fLoadAllQuery.value(7).toString();
                                row.platformId = fLoadAllQuery.value(8).toInt();
                                fAllRows.append(row);
                            }
                        }
                        fDb.close();
                        QSqlDatabase::removeDatabase(fConnName);

                        int fTotalCount = fAllRows.size();
                        int fPageSize = 100;
                        int fTotalPages = qMax(1, (fTotalCount + fPageSize - 1) / fPageSize);

                        // Page info label
                        QLabel* fPageLabel = new QLabel(&filterDialog);
                        fPageLabel->setStyleSheet("color: #9fdcff; font-size: 13px;");
                        fPageLabel->setAlignment(Qt::AlignCenter);
                        fLayout->addWidget(fPageLabel);

                        // Table widget
                        QTableWidget* fTable = new QTableWidget(&filterDialog);
                        fTable->setColumnCount(7);
                        fTable->setHorizontalHeaderLabels(QStringList() << "StartTime" << "ScreenID" << "MarkID" << "AOIResult" << "Grade_AOI" << "Code_AOI" << "Status");
                        fTable->setAlternatingRowColors(true);
                        fTable->setSelectionBehavior(QAbstractItemView::SelectRows);
                        fTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
                        fTable->horizontalHeader()->setStretchLastSection(true);
                        fTable->verticalHeader()->setVisible(false);
                        fTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                        fTable->verticalHeader()->setDefaultSectionSize(22);
                        fTable->setStyleSheet(R"(
                            QTableWidget {
                                background-color: rgba(20, 35, 55, 220);
                                alternate-background-color: rgba(30, 50, 80, 180);
                                color: #e0f0ff;
                                border: 1px solid rgba(0, 217, 255, 80);
                                border-radius: 6px;
                                gridline-color: rgba(0, 217, 255, 40);
                                font-size: 13px;
                            }
                            QTableWidget::item { padding: 5px; }
                            QTableWidget::item:selected { background-color: rgba(0, 150, 200, 150); color: #ffffff; }
                            QHeaderView::section {
                                background-color: rgba(0, 100, 150, 180);
                                color: #ffffff; padding: 6px;
                                border: 1px solid rgba(0, 217, 255, 60);
                                font-weight: bold; font-size: 13px;
                            }
                        )");
                        fLayout->addWidget(fTable, 1);

                        // Pagination buttons
                        QHBoxLayout* fPageBtnLayout = new QHBoxLayout();
                        fPageBtnLayout->setSpacing(8);

                        QPushButton* fPrevBtn = new QPushButton("上一页", &filterDialog);
                        fPrevBtn->setFixedWidth(90);
                        fPrevBtn->setStyleSheet(R"(
                            QPushButton {
                                background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,120,170,180), stop:1 rgba(0,80,130,180));
                                color: #ffffff; border: 1px solid rgba(0,217,255,100);
                                border-radius: 6px; padding: 6px 16px; font-size: 12px; font-weight: bold;
                            }
                            QPushButton:hover { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,160,220,220), stop:1 rgba(0,110,170,220)); border: 1px solid #00d9ff; }
                            QPushButton:disabled { background-color: rgba(50,60,80,200); color: #607080; border: 1px solid rgba(0,217,255,40); }
                        )");

                        QPushButton* fNextBtn = new QPushButton("下一页", &filterDialog);
                        fNextBtn->setFixedWidth(90);
                        fNextBtn->setStyleSheet(R"(
                            QPushButton {
                                background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,120,170,180), stop:1 rgba(0,80,130,180));
                                color: #ffffff; border: 1px solid rgba(0,217,255,100);
                                border-radius: 6px; padding: 6px 16px; font-size: 12px; font-weight: bold;
                            }
                            QPushButton:hover { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,160,220,220), stop:1 rgba(0,110,170,220)); border: 1px solid #00d9ff; }
                            QPushButton:disabled { background-color: rgba(50,60,80,200); color: #607080; border: 1px solid rgba(0,217,255,40); }
                        )");

                        fPageBtnLayout->addStretch();
                        fPageBtnLayout->addWidget(fPrevBtn);
                        fPageBtnLayout->addWidget(fNextBtn);
                        fPageBtnLayout->addStretch();
                        fLayout->addLayout(fPageBtnLayout);

                        // Bottom buttons
                        QHBoxLayout* fBtnLayout = new QHBoxLayout();
                        fBtnLayout->setSpacing(8);
                        QPushButton* fCloseBtn = new QPushButton("关闭", &filterDialog);
                        fCloseBtn->setFixedWidth(100);
                        fCloseBtn->setStyleSheet(R"(
                            QPushButton {
                                background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,120,170,180), stop:1 rgba(0,80,130,180));
                                color: #ffffff; border: 1px solid rgba(0,217,255,100);
                                border-radius: 6px; padding: 8px 20px; font-size: 13px; font-weight: bold;
                            }
                            QPushButton:hover { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,160,220,220), stop:1 rgba(0,110,170,220)); border: 1px solid #00d9ff; }
                        )");
                        fBtnLayout->addStretch();
                        fBtnLayout->addWidget(fCloseBtn);
                        fLayout->addLayout(fBtnLayout);
                        QObject::connect(fCloseBtn, &QPushButton::clicked, &filterDialog, &QDialog::accept);

                        // Lambda to load a page from memory
                        int fCurrentPage = 1;
                        auto fLoadPage = [=](int page) {
                            int startIndex = (page - 1) * fPageSize;
                            int endIndex = qMin(startIndex + fPageSize, fTotalCount);

                            fTable->setRowCount(0);
                            for (int i = startIndex; i < endIndex; ++i) {
                                const FRowData& rowData = fAllRows.at(i);
                                int fRow = fTable->rowCount();
                                fTable->insertRow(fRow);
                                fTable->setItem(fRow, 0, new QTableWidgetItem(rowData.startTime));
                                fTable->setItem(fRow, 1, new QTableWidgetItem(rowData.screenId));
                                fTable->setItem(fRow, 2, new QTableWidgetItem(rowData.MarkID));
                                fTable->setItem(fRow, 3, new QTableWidgetItem(rowData.aoiResult));
                                fTable->setItem(fRow, 4, new QTableWidgetItem(rowData.gradeAOI));
                                fTable->setItem(fRow, 5, new QTableWidgetItem(rowData.codeAOI));
                                fTable->setItem(fRow, 6, new QTableWidgetItem(rowData.status));

                                QTableWidgetItem* fStartItem = fTable->item(fRow, 0);
                                if (fStartItem) {
                                    fStartItem->setData(Qt::UserRole, rowData.screenId);
                                    fStartItem->setData(Qt::UserRole + 1, rowData.localIP);
                                    fStartItem->setData(Qt::UserRole + 2, rowData.platformId);
                                    fStartItem->setData(Qt::UserRole + 3, QDateTime::fromString(rowData.startTime, "yyyy-MM-dd HH:mm:ss"));
                                }
                            }
                            fPageLabel->setText(QString("共 %1 条记录，第 %2/%3 页").arg(fTotalCount).arg(page).arg(fTotalPages));
                            fPrevBtn->setEnabled(page > 1);
                            fNextBtn->setEnabled(page < fTotalPages);
                        };

                        fLoadPage(1);

                        QObject::connect(fPrevBtn, &QPushButton::clicked, this, [&, fLoadPage](bool) {
                            if (fCurrentPage > 1) {
                                fCurrentPage--;
                                fLoadPage(fCurrentPage);
                            }
                        });

                        QObject::connect(fNextBtn, &QPushButton::clicked, this, [&, fLoadPage](bool) {
                            if (fCurrentPage < fTotalPages) {
                                fCurrentPage++;
                                fLoadPage(fCurrentPage);
                            }
                        });

                        QObject::connect(fTable, &QTableWidget::cellDoubleClicked, this, [=](int fRow, int /*col*/) {
                            QTableWidgetItem* fStartItem = fTable->item(fRow, 0);
                            if (!fStartItem) return;
                            QString fScreenId = fStartItem->data(Qt::UserRole).toString();
                            QString fLocalIp = fStartItem->data(Qt::UserRole + 1).toString();
                            int fPlatformId = fStartItem->data(Qt::UserRole + 2).toInt();
                            QDateTime fStartTime = fStartItem->data(Qt::UserRole + 3).toDateTime();
                            showInspectionImageDialog(fScreenId, fPlatformId, fLocalIp, fStartTime);
                        });

                        filterDialog.exec();
                    });
                }
            pieSeries->setHoleSize(0.35);

            QChart* pieChart = new QChart();
            pieChart->addSeries(pieSeries);
            pieChart->setBackgroundBrush(QBrush(QColor(22, 33, 62)));
            pieChart->setAnimationOptions(QChart::NoAnimation);
            pieChart->legend()->setVisible(true);
            pieChart->legend()->setLabelColor(QColor("#e0f0ff"));

                QChartView* chartView = new QChartView(pieChart);
                chartView->setRenderHint(QPainter::Antialiasing);
                chartView->setStyleSheet("background: transparent;");
                chartView->setContentsMargins(0, 0, 0, 0);
                chartLayout->addWidget(chartView, 1);

                mainLayout->addWidget(chartFrame, 1);
        }

        chartDb.close();
    }
    QSqlDatabase::removeDatabase("chartDist_" + QString::number(QDateTime::currentMSecsSinceEpoch()));

    // Close button
    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    closeBtn->setFixedWidth(100);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,120,170,180), stop:1 rgba(0,80,130,180));
            color: #ffffff; border: 1px solid rgba(0,217,255,100);
            border-radius: 6px; padding: 8px 20px; font-size: 13px; font-weight: bold;
        }
        QPushButton:hover { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(0,160,220,220), stop:1 rgba(0,110,170,220)); border: 1px solid #00d9ff; }
    )");
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

