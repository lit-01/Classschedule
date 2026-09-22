#include "calendardata.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QPermissions>
#endif

namespace {

QString storageFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/calendar.json");
}

QJsonArray toStringArray(const QSet<QDate> &dates)
{
    QStringList list;
    list.reserve(dates.size());
    for (const QDate &d : dates)
        list.append(d.toString(Qt::ISODate));
    list.sort();

    QJsonArray arr;
    for (const QString &s : list)
        arr.append(s);
    return arr;
}

QSet<QDate> fromJsonArray(const QJsonArray &arr)
{
    QSet<QDate> out;
    for (const QJsonValue &v : arr) {
        const QDate d = QDate::fromString(v.toString(), Qt::ISODate);
        if (d.isValid())
            out.insert(d);
    }
    return out;
}

QSet<QDate> fromCommaList(const QString &text)
{
    QSet<QDate> out;
    for (const QString &piece : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QDate d = QDate::fromString(piece.trimmed(), Qt::ISODate);
        if (d.isValid())
            out.insert(d);
    }
    return out;
}

} // namespace

CalendarData::CalendarData(QObject *parent)
    : QObject(parent)
{
    load();
}

void CalendarData::setFirstMonday(const QDate &date)
{
    if (!date.isValid() || date == m_firstMonday)
        return;

    // 不管传进来的是周几，都归到那一周的周一
    m_firstMonday = date.addDays(1 - date.dayOfWeek());
    save();
    emit firstMondayChanged();
}

QVariantMap CalendarData::dayInfo(int week, int day) const
{
    const QDate date = m_firstMonday.addDays((week - 1) * 7 + (day - 1));

    const bool holiday = m_holidays.contains(date);
    const bool workday = m_workdays.contains(date);

    QVariantMap info;
    info.insert(QStringLiteral("valid"), date.isValid());
    info.insert(QStringLiteral("year"), date.year());
    info.insert(QStringLiteral("month"), date.month());
    info.insert(QStringLiteral("day"), date.day());
    info.insert(QStringLiteral("iso"), date.toString(Qt::ISODate));
    info.insert(QStringLiteral("holiday"), holiday);
    info.insert(QStringLiteral("workday"), workday);
    // 放假和调休，课都画成灰的
    info.insert(QStringLiteral("gray"), holiday || workday);
    return info;
}

bool CalendarData::isHoliday(const QString &isoDate) const
{
    const QDate d = QDate::fromString(isoDate, Qt::ISODate);
    return m_holidays.contains(d) || m_workdays.contains(d);
}

QString CalendarData::yearLabel(int week) const
{
    const QDate monday = m_firstMonday.addDays((week - 1) * 7);
    const QDate sunday = monday.addDays(6);
    if (!monday.isValid())
        return {};

    if (monday.year() == sunday.year())
        return QString::number(monday.year());

    // 一周跨年（比如 12/28 - 1/3），写成 26/27 省地方
    return QStringLiteral("%1/%2").arg(monday.year() % 100).arg(sunday.year() % 100);
}

QString CalendarData::monthLabel(int week) const
{
    const QDate monday = m_firstMonday.addDays((week - 1) * 7);
    const QDate sunday = monday.addDays(6);
    if (!monday.isValid())
        return {};

    if (monday.month() == sunday.month())
        return QStringLiteral("%1月").arg(monday.month());

    return QStringLiteral("%1月-%2月").arg(monday.month()).arg(sunday.month());
}

// ---------------------------------------------------------------- 手机日历

void CalendarData::refreshFromDeviceCalendar()
{
#ifdef Q_OS_ANDROID
    QCalendarPermission permission;
    permission.setAccessMode(QCalendarPermission::ReadOnly);

    QCoreApplication::instance()->requestPermission(permission, this, [this](const QPermission &p) {
        if (p.status() == Qt::PermissionStatus::Granted) {
            readDeviceCalendar();
        } else {
            m_deviceCalendarNote = QStringLiteral("没给日历权限，节假日先用内置表顶着");
            loadBuiltinFallback();
            emit holidaysChanged();
        }
    });
#else
    // 桌面上没有系统日历可翻，走内置兜底，先把界面调通
    m_deviceCalendarNote = QStringLiteral("桌面预览：节假日用内置表");
    loadBuiltinFallback();
    emit holidaysChanged();
#endif
}

#ifdef Q_OS_ANDROID

void CalendarData::readDeviceCalendar()
{
    const QDate today = QDate::currentDate();
    // 前后各留一年，够课表跨年了
    const QDateTime begin(QDate(today.year() - 1, 1, 1), QTime(0, 0), QTimeZone::UTC);
    const QDateTime end(QDate(today.year() + 2, 1, 1), QTime(0, 0), QTimeZone::UTC);

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        m_deviceCalendarNote = QStringLiteral("拿不到 Android 上下文，节假日用内置表");
        loadBuiltinFallback();
        emit holidaysChanged();
        return;
    }

    const QJniObject result = QJniObject::callStaticObjectMethod(
        "com/pony/coursetable/HolidayReader",
        "read",
        "(Landroid/content/Context;JJ)Ljava/lang/String;",
        context.object<jobject>(),
        static_cast<jlong>(begin.toMSecsSinceEpoch()),
        static_cast<jlong>(end.toMSecsSinceEpoch()));

    applyParsed(result.isValid() ? result.toString() : QString());
}

#endif // Q_OS_ANDROID

void CalendarData::applyParsed(const QString &raw)
{
    const int bar = raw.indexOf(QLatin1Char('|'));
    if (bar < 0) {
        m_deviceCalendarNote = QStringLiteral("手机日历没读出东西，节假日用内置表");
        loadBuiltinFallback();
        emit holidaysChanged();
        return;
    }

    const QSet<QDate> holidays = fromCommaList(raw.left(bar));
    const QSet<QDate> workdays = fromCommaList(raw.mid(bar + 1));

    if (holidays.isEmpty() && workdays.isEmpty()) {
        m_deviceCalendarNote = QStringLiteral("手机日历里没有节假日日历，用内置表");
        loadBuiltinFallback();
        emit holidaysChanged();
        return;
    }

    m_holidays = holidays;
    m_workdays = workdays;
    m_deviceCalendarNote = QStringLiteral("已从手机日历读取：%1 个假日、%2 个调休")
                               .arg(holidays.size())
                               .arg(workdays.size());
    save();
    emit holidaysChanged();
}

// ---------------------------------------------------------------- 读写

void CalendarData::loadBuiltinFallback()
{
    if (!m_holidays.isEmpty() || !m_workdays.isEmpty())
        return;

    // 来源：国务院办公厅《关于 2026 年部分节假日安排的通知》
    //       国办发明电〔2025〕7 号，2025-11-04 发布
    //
    // 注意：这只是「手机日历读不到」时的兜底。真机上优先用系统日历的数据，
    //      系统日历是随 OTA 更新的，比这份写死的表准。
    const char *kHolidays[] = {
        // 元旦 1/1-1/3
        "2026-01-01", "2026-01-02", "2026-01-03",
        // 春节 2/15-2/23（9 天）
        "2026-02-15", "2026-02-16", "2026-02-17", "2026-02-18",
        "2026-02-19", "2026-02-20", "2026-02-21", "2026-02-22",
        "2026-02-23",
        // 清明 4/4-4/6
        "2026-04-04", "2026-04-05", "2026-04-06",
        // 劳动节 5/1-5/5
        "2026-05-01", "2026-05-02", "2026-05-03", "2026-05-04",
        "2026-05-05",
        // 端午 6/19-6/21
        "2026-06-19", "2026-06-20", "2026-06-21",
        // 中秋 9/25-9/27
        "2026-09-25", "2026-09-26", "2026-09-27",
        // 国庆 10/1-10/7
        "2026-10-01", "2026-10-02", "2026-10-03", "2026-10-04",
        "2026-10-05", "2026-10-06", "2026-10-07",
    };

    const char *kWorkdays[] = {
        "2026-01-04",               // 元旦调休
        "2026-02-14", "2026-02-28", // 春节调休
        "2026-05-09",               // 劳动节调休
        "2026-09-20", "2026-10-10", // 国庆调休
    };

    for (const char *s : kHolidays)
        m_holidays.insert(QDate::fromString(QString::fromLatin1(s), Qt::ISODate));
    for (const char *s : kWorkdays)
        m_workdays.insert(QDate::fromString(QString::fromLatin1(s), Qt::ISODate));
}

void CalendarData::load()
{
    const QDate today = QDate::currentDate();
    // 兜底：没导入 PDF 之前，就按「本周」算第一周
    m_firstMonday = today.addDays(1 - today.dayOfWeek());

    QFile f(storageFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        loadBuiltinFallback();
        save();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();

    const QDate saved = QDate::fromString(root.value(QStringLiteral("firstMonday")).toString(),
                                          Qt::ISODate);
    if (saved.isValid())
        m_firstMonday = saved;

    m_holidays = fromJsonArray(root.value(QStringLiteral("holidays")).toArray());
    m_workdays = fromJsonArray(root.value(QStringLiteral("workdays")).toArray());

    loadBuiltinFallback();
}

void CalendarData::save()
{
    QJsonObject root;
    root.insert(QStringLiteral("firstMonday"), m_firstMonday.toString(Qt::ISODate));
    root.insert(QStringLiteral("holidays"), toStringArray(m_holidays));
    root.insert(QStringLiteral("workdays"), toStringArray(m_workdays));

    QFile f(storageFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
