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
#include "ui_Defect_Data_Display.h"

class DataLoaderThread;
class TabDataLoaderThread;

// CachedTabData structure must be defined before Defect_Data_Display class
struct CachedTabData {
    QList<QPair<int, int>> positions;
    QStringList types;
    QMap<QString, QPair<int, int>> trendData;
    QMap<QString, double> defectRates;
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

private slots:
    void onRefreshClicked();
    void onTimeRangeChanged(int index);
    void updateDateTime();
    void onMinimizeClicked();
    void onCloseClicked();
    void onTabChanged(int index);
    void onDateChanged(const QDate& date);

    void onDataLoaded_Aoi(const QMap<QString, QList<QPair<QString, int>>>& defectByType, int totalDefects);
    void onDataLoaded_Inspection(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod,
                                 int totalInspect, int passCount, int failCount, double passRate);
    void onDataLoaded_Platform(const QMap<int, QPair<int, int>>& platformStats);
    void onDataLoaded_DefectMapping(const QList<QPair<int, int>>& positions, const QStringList& types);
    void onDataLoaded_Trend(const QMap<QString, QPair<int, int>>& trendData, const QMap<QString, double>& defectRates);
    void onDataLoaded_Detail(const QList<QVariantList>& defectDetails);
    void onLoadFinished(int loadId);

    // New slots for time-based trend data
    void onDataLoaded_PlatformTrend(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData, const QString& timeRange);
    void onDataLoaded_DefectTrend(const QMap<QString, QMap<QString, int>>& defectTrendData, const QString& timeRange);
    void onDataLoaded_InspectionTrend(const QMap<QString, QPair<int, int>>& inspectionTrendData, const QString& timeRange);

private:
    Ui::Defect_Data_DisplayClass ui;
    QSqlDatabase m_db;

    void* m_chartViewAoi;
    void* m_chartViewInspectionPass;
    void* m_chartViewInspectionFail;
    void* m_chartViewDefectMapping;
    void* m_chartViewTrend;
    void* m_chartViewDefectRate;
    QChartView* m_chartViewPlatform0;
    QChartView* m_chartViewPlatform1;
    QChartView* m_chartViewPlatform2;
    QChartView* m_chartViewPlatform3;
    void* m_chartViewDetail;
    QTimer* m_timer;
    QDate m_selectedDate;

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

    // New member variables for time-based trend data
    QMap<QString, QMap<int, QPair<int, int>>> m_platformTrendData;  // time_period -> platform_id -> (pass, fail)
    QMap<QString, QMap<QString, int>> m_defectTrendData;            // time_period -> (defect_type -> count)
    QMap<QString, QPair<int, int>> m_inspectionTrendData;            // time_period -> (pass, fail)
    QString m_currentTimeFormat;  // Current time format for display

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
    void updateTrendChart(const QMap<QString, QPair<int, int>>& trendData, const QMap<QString, double>& defectRates);
    void updateDetailTable(const QList<QVariantList>& defectDetails);
    void updateStats(int totalInspect, int passCount, int failCount, double passRate, int totalDefects);
    QString getTimeFilterClause(const QString& timeRange);
    QString getDateTimeRange(const QString& timeRange);
    void setupCharts();
    void clearAllCharts();

    // New functions for time-based trend display
    void updatePlatformTrendChart(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData);
    void updateDefectTrendChart(const QMap<QString, QMap<QString, int>>& defectTrendData);
    void updateInspectionTrendChart(const QMap<QString, QPair<int, int>>& inspectionTrendData);
    void loadMainData(const QString& timeRange);
};

class DataLoaderThread : public QThread
{
    Q_OBJECT

public:
    DataLoaderThread(int loadId, const QString& timeRange, const QString& dateRange, QObject* parent = nullptr);
    int getLoadId() const { return m_loadId; }
    void run() override;

signals:
    void aoiDataLoaded(const QMap<QString, QList<QPair<QString, int>>>& defectByType, int totalDefects);
    void inspectionDataLoaded(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod,
                             int totalInspect, int passCount, int failCount, double passRate);
    void platformDataLoaded(const QMap<int, QPair<int, int>>& platformStats);
    void defectMappingLoaded(const QList<QPair<int, int>>& positions, const QStringList& types);
    void trendDataLoaded(const QMap<QString, QPair<int, int>>& trendData, const QMap<QString, double>& defectRates);
    void detailDataLoaded(const QList<QVariantList>& defectDetails);
    void finished(int loadId);

    // New signals for time-based trend data
    void platformTrendLoaded(const QMap<QString, QMap<int, QPair<int, int>>>& platformTrendData, const QString& timeRange);
    void defectTrendLoaded(const QMap<QString, QMap<QString, int>>& defectTrendData, const QString& timeRange);
    void inspectionTrendLoaded(const QMap<QString, QPair<int, int>>& inspectionTrendData, const QString& timeRange);

protected:
    int m_loadId;
    QString m_timeRange;
    QString m_dateRange;
};

class TabDataLoaderThread : public QThread
{
    Q_OBJECT

public:
    TabDataLoaderThread(int loadId, int tabIndex, const QString& timeRange, const QString& dateRange,
                       const QDate& date, QObject* parent = nullptr);
    int getLoadId() const { return m_loadId; }
    int getTabIndex() const { return m_tabIndex; }
    void run() override;

signals:
    void defectMappingDataLoaded(const QList<QPair<int, int>>& positions, const QStringList& types);
    void trendDataLoaded(const QMap<QString, QPair<int, int>>& trendData, const QMap<QString, double>& defectRates);
    void detailDataLoaded(const QList<QVariantList>& defectDetails);
    void finished(int loadId, int tabIndex);

protected:
    int m_loadId;
    int m_tabIndex;
    QString m_timeRange;
    QString m_dateRange;
    QDate m_date;
};
