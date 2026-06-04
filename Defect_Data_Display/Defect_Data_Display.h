#pragma once

#include <QtWidgets/QMainWindow>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVariant>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QSet>
#include <QTimer>
#include <QMouseEvent>
#include <QTabWidget>
#include <QDateEdit>
#include <QDate>
#include <QTableWidget>
#include <QThread>
#include <QMutex>
#include <QChartView>
#include <QTableWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include "ui_Defect_Data_Display.h"

class DataLoaderThread;
class TabDataLoaderThread;

// CachedTabData structure must be defined before Defect_Data_Display class
struct CachedTabData {
    QList<QPair<int, int>> positions;
    QStringList types;
    QMap<QString, QMap<QString, int>> trendDataByGrade;
    QMap<QString, QMap<QString, double>> defectRatesByGrade;
    QStringList allGrades;
    QList<QVariantList> defectDetails;
    QString timeRange;
    QDate date;
    qint64 timestamp;
};

class Defect_Data_Display : public QMainWindow
{
    Q_OBJECT

public:
    Defect_Data_Display(QWidget *parent = nullptr);
    ~Defect_Data_Display();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showPlatformChartTooltip(QChartView* chartView, int platformIdx, const QPoint& viewportPos, const QPointF& chartPos);
    void showByTimeChartTooltip(const QPoint& viewportPos, const QPointF& chartPos);

private slots:
    void onRefreshClicked();
    void onTimeRangeChanged(int index);
    void updateDateTime();
    void onMinimizeClicked();
    void onCloseClicked();
    void onTabChanged(int index);
    void onPlatformTabChanged(int index);
    void onDateChanged(const QDate& date);
    void onSearchClicked();
    void performQrCodeSearch(const QString& screenId);
    void onTimeRangeChangedForSearch(int index);

    void onDataLoaded_Aoi(const QMap<QString, QList<QPair<QString, int>>>& defectByType, int totalDefects);
    void onDataLoaded_Inspection(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod,
                                 int totalInspect, int passCount, int failCount, double passRate);
    void onDataLoaded_Platform(const QMap<int, QPair<int, int>>& platformStats);
    void onDataLoaded_DefectMapping(const QList<QPair<int, int>>& positions, const QStringList& types);
    void onDataLoaded_Trend(const QMap<QString, QMap<QString, int>>& trendData, const QMap<QString, QMap<QString, double>>& defectRates, const QStringList& allGrades);
    void onDataLoaded_Detail(const QList<QVariantList>& defectDetails);
    void onLoadFinished(int loadId);

    // New slots for time-based trend data
    void onDataLoaded_PlatformTrend(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData, const QString& timeRange);
    void onDataLoaded_PlatformAoiResult(const QMap<QString, QMap<int, QMap<QString, int>>>& platformAoiResultData, const QStringList& aoiResultCategories, const QString& timeRange);
    void onDataLoaded_DefectTrend(const QMap<QString, QMap<QString, int>>& defectTrendData, const QString& timeRange);
    void onDataLoaded_InspectionTrend(const QMap<QString, QPair<int, int>>& inspectionTrendData, const QString& timeRange);
    void onDataLoaded_PlatformGradeTrend(const QMap<QString, QMap<QString, int>>& gradeTrendData, const QStringList& allGrades, const QString& timeRange);

private:
    Ui::Defect_Data_DisplayClass ui;
    QSqlDatabase m_db;

    void* m_chartViewAoi;
    void* m_chartViewInspectionPass;
    void* m_chartViewInspectionFail;
    void* m_chartViewDefectMapping;
    void* m_chartViewTrend;
    void* m_chartViewDefectRate;
    void* m_chartViewTrendY2;
    void* m_chartViewDefectRateY2;
    QMap<QString, QPair<int, int>> m_trendDataY2;
    QChartView* m_chartViewPlatform0;
    QChartView* m_chartViewPlatform1;
    QChartView* m_chartViewPlatform2;
    QChartView* m_chartViewPlatform3;
    QChartView* m_chartViewPlatformByTime;
    void* m_chartViewDetail;
    void* m_chartViewPieDetail;
    void* m_chartViewLocationAbnormal;
    QTimer* m_timer;
    QDate m_selectedDate;
    int m_searchStartHour;   // 开始小时 for hourly search (0-23)
    int m_searchEndHour;     // 结束小时 for hourly search (0-23)

    DataLoaderThread* m_workerThread;
    TabDataLoaderThread* m_tabWorkerThread;
    QMutex m_queryMutex;
    int m_currentLoadId;

    QPoint m_dragPosition;
    bool m_isDragging;
    bool m_isLoading;
    bool m_isTabLoading;

    CachedTabData m_defectMappingCache;
    CachedTabData m_trendCache;
    CachedTabData m_detailCache;
    qint64 m_lastMainLoadTime;
    static constexpr int TAB_SEARCH = 3;
    static constexpr int TAB_LOCATION_ABNORMAL = 4;
    QString m_searchScreenId;  // ScreenID for search filtering

    // New member variables for time-based trend data
    QMap<QString, QMap<int, QPair<int, int>>> m_platformTrendData;  // time_period -> platform_id -> (pass, fail)
    QMap<QString, QMap<int, QMap<QString, int>>> m_platformAoiResultData;  // time_period -> platform_id -> (AOIResult -> count) for stacked chart
    QMap<QString, QMap<QString, int>> m_defectTrendData;            // time_period -> (defect_type -> count)
    QMap<QString, QPair<int, int>> m_inspectionTrendData;            // time_period -> (pass, fail)
    QString m_currentTimeFormat;  // Current time format for display
    QStringList m_aoiResultCategories;  // AOIResult categories for stacked chart (e.g., "OK", "NG", "Rework")
    QMap<QString, QMap<QString, int>> m_platformGradeTrendData;  // time_period -> grade -> count

    // Member variables for by-time chart tooltip support
    QMap<QString, QString> m_byTimeCategoryMap;  // Display category -> original key
    QList<QList<int>> m_byTimePlatformTotals;  // Platform totals for tooltip

    bool connectToDatabase();
    void startLoading(const QString& timeRange);
    void loadAoiDefectData(const QString& timeRange);
    void loadInspectionResultData(const QString& timeRange);
    void loadPlatformStats(const QString& timeRange);
    void loadDefectMapping(const QString& timeRange);
    void loadDefectMappingAsync(const QString& timeRange);
    void loadTrendData(const QString& timeRange);
    void loadTrendDataAsync(const QString& timeRange);
    void loadDetailData(const QString& timeRange);
    void loadDetailDataAsync(const QString& timeRange);
    bool isCacheValid(CachedTabData* cache, const QString& timeRange, const QDate& date);
    void updateAoiDefectChart(const QMap<QString, QList<QPair<QString, int>>>& defectByType);
    void updateInspectionResultChart(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod);
    void updateDefectMappingChart(const QList<QPair<int, int>>& defectPositions, const QStringList& defectTypes);
    void updateTrendChart(const QMap<QString, QMap<QString, int>>& trendData, const QMap<QString, QMap<QString, double>>& defectRates, const QStringList& allGrades, const QString& timeRange);
    void updateDetailTable(const QList<QVariantList>& defectDetails);
    void updateStats(int totalInspect, int passCount, int failCount, double passRate, int totalDefects);
    QString getTimeFilterClause(const QString& timeRange);
    QString getDateTimeRange(const QString& timeRange);
    void setupCharts();
    void clearAllCharts();

    // New functions for time-based trend display
    void updatePlatformTrendChart(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData);
    void updatePlatformTrendChartStacked(const QMap<QString, QMap<int, QMap<QString, int>>>& platformAoiResultData, const QStringList& aoiResultCategories);
    void updateDefectTrendChart(const QMap<QString, QMap<QString, int>>& defectTrendData);
    void updateInspectionTrendChart(const QMap<QString, QPair<int, int>>& inspectionTrendData);
    void updatePlatformGradeTrendChart(const QMap<QString, QMap<QString, int>>& gradeTrendData, const QStringList& allGrades, const QString& timeRange);
    void updatePlatformByTimeChart();
    void loadMainData(const QString& timeRange);

    // Location Abnormal functions
    void loadLocationAbnormalData(const QString& timeRange);
    void loadLocationAbnormalDataAsync(const QString& timeRange);
    void updateLocationAbnormalChart(const QMap<QString, QMap<int, int>>& abnormalByPeriod);
    void onDataLoaded_LocationAbnormal(const QMap<QString, QMap<int, int>>& abnormalByPeriod);
    void showBarClickDialog(int platformIdx, const QString& timeKey);
    void showDetailPieDialog();
    void showGradeTypeDialog(const QString& gradeName);
    QMap<QString, QMap<int, int>> m_locationAbnormalData;  // time_period -> platform_id -> count
    CachedTabData m_locationAbnormalCache;
    QLabel* m_tooltipLabel;  // custom floating tooltip for platform charts
    QDialog* m_barClickDialog;  // dialog for chart click details
    QMap<QString, int> m_detailPieData;  // detailed tab pie chart data for modal dialog
    QString m_detailPieTitle;
};

class DataLoaderThread : public QThread
{
    Q_OBJECT

public:
    DataLoaderThread(int loadId, const QString& timeRange, const QString& dateRange,
                     const QString& searchScreenId = "", QObject* parent = nullptr,
                     int startHour = -1, int endHour = -1);
    int getLoadId() const { return m_loadId; }
    void run() override;

signals:
    void aoiDataLoaded(const QMap<QString, QList<QPair<QString, int>>>& defectByType, int totalDefects);
    void inspectionDataLoaded(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod,
                             int totalInspect, int passCount, int failCount, double passRate);
    void platformDataLoaded(const QMap<int, QPair<int, int>>& platformStats);
    void defectMappingLoaded(const QList<QPair<int, int>>& positions, const QStringList& types);
    void trendDataLoaded(const QMap<QString, QMap<QString, int>>& trendData, const QMap<QString, QMap<QString, double>>& defectRates, const QStringList& allGrades);
    void detailDataLoaded(const QList<QVariantList>& defectDetails);
    void finished(int loadId);

    // New signals for time-based trend data
    void platformTrendLoaded(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData, const QString& timeRange);
    void platformAoiResultLoaded(const QMap<QString, QMap<int, QMap<QString, int>>>& platformAoiResultData, const QStringList& aoiResultCategories, const QString& timeRange);
    void defectTrendLoaded(const QMap<QString, QMap<QString, int>>& defectTrendData, const QString& timeRange);
    void inspectionTrendLoaded(const QMap<QString, QPair<int, int>>& inspectionTrendData, const QString& timeRange);
    void platformGradeTrendLoaded(const QMap<QString, QMap<QString, int>>& gradeTrendData, const QStringList& allGrades, const QString& timeRange);

protected:
    int m_loadId;
    QString m_timeRange;
    QString m_dateRange;
    QString m_searchScreenId;
    int m_startHour;  // 开始小时 for hourly search (0-23)
    int m_endHour;     // 结束小时 for hourly search (0-23)
};

class TabDataLoaderThread : public QThread
{
    Q_OBJECT

public:
    TabDataLoaderThread(int loadId, int tabIndex, const QString& timeRange, const QString& dateRange,
                       const QDate& date, const QString& searchScreenId = "", QObject* parent = nullptr);
    int getLoadId() const { return m_loadId; }
    int getTabIndex() const { return m_tabIndex; }
    void run() override;

signals:
    void defectMappingDataLoaded(const QList<QPair<int, int>>& positions, const QStringList& types);
    void trendDataLoaded(const QMap<QString, QMap<QString, int>>& trendData, const QMap<QString, QMap<QString, double>>& defectRates, const QStringList& allGrades);
    void detailDataLoaded(const QList<QVariantList>& defectDetails);
    void locationAbnormalDataLoaded(const QMap<QString, QMap<int, int>>& abnormalByPeriod);
    void finished(int loadId, int tabIndex);

protected:
    int m_loadId;
    int m_tabIndex;
    QString m_timeRange;
    QString m_dateRange;
    QDate m_date;
    QString m_searchScreenId;
    QMap<QString, QMap<int, int>> m_locationAbnormalData;  // time_period -> platform_id -> count
};
