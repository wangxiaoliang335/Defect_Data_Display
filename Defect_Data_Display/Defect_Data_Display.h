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
#include "ui_Defect_Data_Display.h"

class QChartView;

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

private:
    Ui::Defect_Data_DisplayClass ui;
    QSqlDatabase m_db;

    void* m_chartViewAoi;
    void* m_chartViewInspection;
    void* m_chartViewPlatform;
    void* m_chartViewDefectMapping;
    QTimer* m_timer;

    QPoint m_dragPosition;
    bool m_isDragging;

    bool connectToDatabase();
    void loadAoiDefectData(const QString& timeRange);
    void loadInspectionResultData(const QString& timeRange);
    void loadPlatformStats(const QString& timeRange);
    void loadDefectMapping(const QString& timeRange);
    void updateAoiDefectChart(const QMap<QString, QList<QPair<QString, int>>>& defectByType);
    void updateInspectionResultChart(const QMap<QString, int>& passByPeriod, const QMap<QString, int>& failByPeriod);
    void updatePlatformChart(const QMap<int, QPair<int, int>>& platformStats);
    void updateDefectMappingChart(const QList<QPair<int, int>>& defectPositions, const QStringList& defectTypes);
    void updateStats(int totalInspect, int passCount, int failCount, double passRate, int totalDefects);
    QString getTimeFilterClause(const QString& timeRange);
    QString getDateTimeRange(const QString& timeRange);
    void setupCharts();
};
