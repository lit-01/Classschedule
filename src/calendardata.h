#pragma once

#include <QDate>
#include <QObject>
#include <QSet>
#include <QVariantMap>

// 管两件事：
//   1. 学期第 1 周的周一是哪天（把「第 N 周 + 星期几」换算成真实日期）。
//      这个值不由用户手填 —— 等导入 PDF 课表时，从表里的周次标注推出来。
//   2. 哪些日子放假、哪些是调休补班。来源是手机自带的日历：
//      一进 app 就去遍历一次（calendar_type / 名字里带「假」「holiday」的日历），
//      把结果存到本地 calendar.json，下次启动直接用缓存。
//      桌面上没有系统日历可读，就用内置的兜底表，方便调界面。
class CalendarData : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QDate firstMonday READ firstMonday WRITE setFirstMonday NOTIFY firstMondayChanged)
    Q_PROPERTY(QString deviceCalendarNote READ deviceCalendarNote NOTIFY holidaysChanged)

public:
    explicit CalendarData(QObject *parent = nullptr);

    QDate firstMonday() const { return m_firstMonday; }
    void setFirstMonday(const QDate &date);

    // 给人看的一句话：这次节假日是从哪来的、捞到几条
    QString deviceCalendarNote() const { return m_deviceCalendarNote; }

    // 一次把某格需要的日期信息都取回来（QML 一次调用就够）
    Q_INVOKABLE QVariantMap dayInfo(int week, int day) const;

    // 这天放不放假
    Q_INVOKABLE bool isHoliday(const QString &isoDate) const;

    // 顶部那块「年 / 月」分两行显示，所以拆开给。
    // 跨月会写成 9月-10月，跨年会写成 26/27
    Q_INVOKABLE QString yearLabel(int week) const;
    Q_INVOKABLE QString monthLabel(int week) const;

    // 去翻手机自带的日历，把法定节假日 / 调休补班捞出来存下来
    Q_INVOKABLE void refreshFromDeviceCalendar();

    void load();
    void save();

signals:
    void firstMondayChanged();
    void holidaysChanged();

private:
    void readDeviceCalendar();
    // 解析 Java 那边回来的 "假日1,假日2|调休1,调休2"
    void applyParsed(const QString &raw);
    // 内置兜底表（只在日历读不到时才用）
    void loadBuiltinFallback();

    QDate m_firstMonday;
    QSet<QDate> m_holidays; // 放假日（不上课）
    QSet<QDate> m_workdays; // 调休补班日
    QString m_deviceCalendarNote;
};
