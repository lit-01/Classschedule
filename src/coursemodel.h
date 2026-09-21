#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

struct Course
{
    int id = 0;
    QString name;
    QString teacher;
    QString room;
    int day = 1;          // 1 = 周一 …… 7 = 周日
    int startSection = 1; // 起始大节
    int endSection = 1;   // 结束大节
    int startWeek = 1;    // 起始周
    int endWeek = 20;     // 结束周
    int weekType = 0;     // 0 = 每周, 1 = 单周, 2 = 双周
    QColor color;

    bool activeInWeek(int week) const;
};

class CourseModel : public QAbstractListModel
{
    Q_OBJECT
    // 网格一共几行，QML 那边直接读，别在两处硬编码
    Q_PROPERTY(int maxSections READ maxSections CONSTANT)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TeacherRole,
        RoomRole,
        DayRole,
        StartSectionRole,
        EndSectionRole,
        StartWeekRole,
        EndWeekRole,
        WeekTypeRole,
        ColorRole
    };

    // 一次课 = 两小节，所以一天按大节算只有 5 行
    static constexpr int kMaxSections = 5;

    explicit CourseModel(QObject *parent = nullptr);

    int maxSections() const { return kMaxSections; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 给 QML 用的接口
    Q_INVOKABLE int addCourse(const QVariantMap &data);
    Q_INVOKABLE void updateCourse(int id, const QVariantMap &data);
    Q_INVOKABLE void removeCourse(int id);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QVariantMap getCourse(int id) const;
    Q_INVOKABLE void seedDemo();

    void load();
    void save();

private:
    int indexOfId(int id) const;
    void reassignColors(); // 按天分组、同列按节次排序，白 / 淡绿交替
    void refreshColors();

    QList<Course> m_courses;
    int m_nextId = 1;
};
