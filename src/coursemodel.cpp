#include "coursemodel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace {

// 卡片底色：白色和青绿交替
const QColor kCardWhite(QStringLiteral("#FFFFFF"));
const QColor kCardGreen(QStringLiteral("#87F7EB"));

QString storageFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/courses.json");
}

// 周次：startWeek / endWeek 都 <= 0 表示「表里没给周次」，
// 这种课每周都上，卡片上也不显示周次那一行。
Course courseFromMap(const QVariantMap &m)
{
    Course c;
    c.name = m.value(QStringLiteral("name")).toString();
    c.teacher = m.value(QStringLiteral("teacher")).toString();
    c.room = m.value(QStringLiteral("room")).toString();
    c.day = qBound(1, m.value(QStringLiteral("day"), 1).toInt(), 7);
    c.startSection = qBound(1, m.value(QStringLiteral("startSection"), 1).toInt(),
                            CourseModel::kMaxSections);
    c.endSection = qBound(c.startSection,
                          m.value(QStringLiteral("endSection"), c.startSection).toInt(),
                          CourseModel::kMaxSections);

    const int startWeek = m.value(QStringLiteral("startWeek"), 0).toInt();
    const int endWeek = m.value(QStringLiteral("endWeek"), 0).toInt();
    if (startWeek > 0 && endWeek > 0) {
        c.startWeek = startWeek;
        c.endWeek = qMax(startWeek, endWeek);
    } else {
        c.startWeek = 0;
        c.endWeek = 0;
    }

    c.weekType = qBound(0, m.value(QStringLiteral("weekType"), 0).toInt(), 2);
    return c;
}

QVariantMap courseToMap(const Course &c)
{
    QVariantMap m;
    m.insert(QStringLiteral("courseId"), c.id);
    m.insert(QStringLiteral("name"), c.name);
    m.insert(QStringLiteral("teacher"), c.teacher);
    m.insert(QStringLiteral("room"), c.room);
    m.insert(QStringLiteral("day"), c.day);
    m.insert(QStringLiteral("startSection"), c.startSection);
    m.insert(QStringLiteral("endSection"), c.endSection);
    m.insert(QStringLiteral("startWeek"), c.startWeek);
    m.insert(QStringLiteral("endWeek"), c.endWeek);
    m.insert(QStringLiteral("weekType"), c.weekType);
    m.insert(QStringLiteral("color"), c.color.name(QColor::HexRgb));
    return m;
}

} // namespace

bool Course::activeInWeek(int week) const
{
    if (startWeek <= 0 || endWeek <= 0)
        return true; // 没有周次信息 → 每周都上

    if (week < startWeek || week > endWeek)
        return false;
    if (weekType == 1 && week % 2 == 0) // 单周
        return false;
    if (weekType == 2 && week % 2 == 1) // 双周
        return false;
    return true;
}

CourseModel::CourseModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();

    // 第一次跑，给几门示例课，方便先看到课表长什么样
    if (m_courses.isEmpty())
        seedDemo();
}

int CourseModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_courses.size());
}

QVariant CourseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_courses.size())
        return {};

    const Course &c = m_courses.at(index.row());
    switch (role) {
    case IdRole:           return c.id;
    case NameRole:         return c.name;
    case TeacherRole:      return c.teacher;
    case RoomRole:         return c.room;
    case DayRole:          return c.day;
    case StartSectionRole: return c.startSection;
    case EndSectionRole:   return c.endSection;
    case StartWeekRole:    return c.startWeek;
    case EndWeekRole:      return c.endWeek;
    case WeekTypeRole:     return c.weekType;
    case ColorRole:        return c.color.isValid() ? c.color : kCardWhite;
    default:               return {};
    }
}

QHash<int, QByteArray> CourseModel::roleNames() const
{
    return {
        { IdRole,           "courseId" },
        { NameRole,         "name" },
        { TeacherRole,      "teacher" },
        { RoomRole,         "room" },
        { DayRole,          "day" },
        { StartSectionRole, "startSection" },
        { EndSectionRole,   "endSection" },
        { StartWeekRole,    "startWeek" },
        { EndWeekRole,      "endWeek" },
        { WeekTypeRole,     "weekType" },
        { ColorRole,        "color" },
    };
}

int CourseModel::indexOfId(int id) const
{
    for (int i = 0; i < m_courses.size(); ++i) {
        if (m_courses.at(i).id == id)
            return i;
    }
    return -1;
}

void CourseModel::reassignColors()
{
    // 先按「星期 → 起始节次」把课程排一遍，再整体白 / 淡绿交替。
    // 这样一眼扫过去是相间的，同一天里的课往往也能岔开色。
    QList<int> order;
    order.reserve(m_courses.size());
    for (int i = 0; i < m_courses.size(); ++i)
        order.append(i);

    std::sort(order.begin(), order.end(), [this](int lhs, int rhs) {
        const Course &a = m_courses.at(lhs);
        const Course &b = m_courses.at(rhs);
        if (a.day != b.day)
            return a.day < b.day;
        return a.startSection < b.startSection;
    });

    for (int k = 0; k < order.size(); ++k)
        m_courses[order.at(k)].color = (k % 2 == 0) ? kCardWhite : kCardGreen;
}

void CourseModel::refreshColors()
{
    if (m_courses.isEmpty())
        return;

    reassignColors();
    emit dataChanged(index(0), index(static_cast<int>(m_courses.size()) - 1), { ColorRole });
}

int CourseModel::addCourse(const QVariantMap &data)
{
    Course c = courseFromMap(data);
    c.id = m_nextId++;
    c.color = kCardWhite;

    beginInsertRows(QModelIndex(), static_cast<int>(m_courses.size()),
                    static_cast<int>(m_courses.size()));
    m_courses.append(c);
    endInsertRows();

    refreshColors();
    save();
    return c.id;
}

void CourseModel::updateCourse(int id, const QVariantMap &data)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;

    Course c = courseFromMap(data);
    c.id = id;
    c.color = m_courses.at(row).color;

    m_courses[row] = c;
    refreshColors();
    save();
}

void CourseModel::removeCourse(int id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_courses.removeAt(row);
    endRemoveRows();

    refreshColors();
    save();
}

void CourseModel::clearAll()
{
    if (m_courses.isEmpty())
        return;

    beginResetModel();
    m_courses.clear();
    m_nextId = 1;
    endResetModel();
    save();
}

QVariantMap CourseModel::getCourse(int id) const
{
    const int row = indexOfId(id);
    if (row < 0)
        return {};
    return courseToMap(m_courses.at(row));
}

void CourseModel::seedDemo()
{
    const struct {
        const char *name;
        const char *teacher;
        const char *room;
        int day;
        int startSection;
        int endSection;
    } demo[] = {
        { "高等数学", "王建国", "教三 201", 1, 1, 1 },
        { "大学英语", "李梅", "文科楼 305", 2, 2, 2 },
        { "数据结构", "张伟", "信息楼 402", 3, 1, 1 },
        { "计算机网络", "刘洋", "信息楼 210", 4, 3, 3 },
        { "体育", "陈刚", "田径场", 3, 4, 4 },
        { "程序设计基础", "赵敏", "实验楼 A301", 5, 2, 2 },
    };

    beginResetModel();
    m_courses.clear();
    m_nextId = 1;
    for (const auto &d : demo) {
        Course c;
        c.id = m_nextId++;
        c.name = QString::fromUtf8(d.name);
        c.teacher = QString::fromUtf8(d.teacher);
        c.room = QString::fromUtf8(d.room);
        c.day = d.day;
        c.startSection = d.startSection;
        c.endSection = d.endSection;
        c.startWeek = 1;
        c.endWeek = 18;
        c.weekType = 0;
        m_courses.append(c);
    }
    reassignColors();
    endResetModel();
    save();
}

void CourseModel::load()
{
    QFile f(storageFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject rootObj = doc.object();
    const QJsonArray arr = rootObj.value(QStringLiteral("courses")).toArray();

    // version 1 的存档里节次是按「小节」存的（1-12），网格还画 12 行。
    // 现在一行 = 一次课 = 两小节，只有 5 行，所以老数据要折一下：
    // (n + 1) / 2 就是大节号，1-2 → 1、3-4 → 2、5-6 → 3 ……
    const int version = rootObj.value(QStringLiteral("version")).toInt(1);
    const bool needConvert = version < 2;

    m_courses.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Course c;
        c.id = o.value(QStringLiteral("id")).toInt();
        c.name = o.value(QStringLiteral("name")).toString();
        c.teacher = o.value(QStringLiteral("teacher")).toString();
        c.room = o.value(QStringLiteral("room")).toString();
        c.day = o.value(QStringLiteral("day")).toInt(1);
        c.startSection = o.value(QStringLiteral("startSection")).toInt(1);
        c.endSection = o.value(QStringLiteral("endSection")).toInt(1);
        c.startWeek = o.value(QStringLiteral("startWeek")).toInt(0);
        c.endWeek = o.value(QStringLiteral("endWeek")).toInt(0);
        c.weekType = o.value(QStringLiteral("weekType")).toInt(0);

        if (needConvert) {
            c.startSection = (c.startSection + 1) / 2;
            c.endSection = (c.endSection + 1) / 2;
        }

        c.startSection = qBound(1, c.startSection, kMaxSections);
        c.endSection = qBound(c.startSection, c.endSection, kMaxSections);
        m_courses.append(c);
    }

    m_nextId = 1;
    for (const Course &c : m_courses)
        m_nextId = qMax(m_nextId, c.id + 1);

    // 颜色每次都按当前排布重算，不依赖存盘里的旧值
    reassignColors();

    if (needConvert)
        save();
}

void CourseModel::save()
{
    QJsonArray arr;
    for (const Course &c : m_courses) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), c.id);
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("teacher"), c.teacher);
        o.insert(QStringLiteral("room"), c.room);
        o.insert(QStringLiteral("day"), c.day);
        o.insert(QStringLiteral("startSection"), c.startSection);
        o.insert(QStringLiteral("endSection"), c.endSection);
        o.insert(QStringLiteral("startWeek"), c.startWeek);
        o.insert(QStringLiteral("endWeek"), c.endWeek);
        o.insert(QStringLiteral("weekType"), c.weekType);
        o.insert(QStringLiteral("color"), c.color.name(QColor::HexRgb));
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 2); // 2 起：section 是大节号（1-5）
    root.insert(QStringLiteral("courses"), arr);

    QFile f(storageFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
