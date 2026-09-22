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
    // 理论 / 实验 / 上机 / 实践 —— 从课表 PDF 里那些 ★ ☆ ◆ ■ 来的
    QString category;
    int day = 1;          // 1 = 周一 …… 7 = 周日
    int startSection = 1; // 起始大节
    int endSection = 1;   // 结束大节
    int startWeek = 1;    // 起始周
    int endWeek = 20;     // 结束周
    int weekType = 0;     // 0 = 每周, 1 = 单周, 2 = 双周
    // 从课表 PDF 导入的课，「哪些周上」经常是不连续的（比如 4-8周,10-16周），
    // startWeek/endWeek/weekType 三个字段表达不了，所以另存一份精确周次。
    // 非空时以它为准。
    QList<int> weeks;
    QString weeksLabel;   // 给人看的原文，例如 "4-8周,10-16周"
    QColor color;

    // 派生字段（不入库）：同一个 (星期, 起始大节) 格子里排第几 / 共几个。
    // 一个格子里挂多门课（单双周分开上、或者两门课排一起）时要横向排开，别叠着。
    int slotIndex = 0;
    int slotCount = 1;

    bool activeInWeek(int week) const;
    // 卡片上「周次」那一行要显示的文字
    QString weeksText() const;
};

class CourseModel : public QAbstractListModel
{
    Q_OBJECT
    // 网格一共几行，QML 那边直接读，别在两处硬编码
    Q_PROPERTY(int maxSections READ maxSections CONSTANT)
    // 一张课都没有。「没导过课表」时进对话页要自动发一句引导，QML 里靠它判断。
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TeacherRole,
        RoomRole,
        CategoryRole,
        DayRole,
        StartSectionRole,
        EndSectionRole,
        StartWeekRole,
        EndWeekRole,
        WeekTypeRole,
        WeeksLabelRole,
        SlotIndexRole, // 同一个格子里这是第几张卡
        SlotCountRole, // 这个格子里一共几张卡
        ColorRole
    };

    // 一次课 = 两小节，所以一天按大节算只有 5 行
    static constexpr int kMaxSections = 5;

    explicit CourseModel(QObject *parent = nullptr);

    int maxSections() const { return kMaxSections; }
    bool empty() const { return m_courses.isEmpty(); }
    // 给 AI 对话用：把当前课表整份读出来，好告诉模型现在有什么课
    const QList<Course> &allCourses() const { return m_courses; }

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

    // 这门课第 week 周上不上。周次判断统一放这儿，QML 别自己算 ——
    // 导入的课周次可能是一串不连续的周（4-8周,10-16周），
    // 用 startWeek/endWeek/weekType 那套算不对。
    Q_INVOKABLE bool isActive(int id, int week) const;

    // 整批换掉（导课用）：先清空再写入，成功才落盘
    Q_INVOKABLE void replaceAll(const QVariantList &courses);

    // 某一天某个大节，在第 week 周**同时**要显示的课有几门。
    // 单双周分开上的两门课占同一个格子（比如周二 5-6 节：一门 1-3 周、
    // 一门 4-16 周双），但任意一周通常只有一门有效，这时就该占满整格；
    // 真撞上了才横向排开，所以不能拿「格子里一共有几门」当宽度依据。
    Q_INVOKABLE int liveSlotCount(int day, int section, int week) const;
    Q_INVOKABLE int liveSlotIndex(int id, int week) const;

    void load();
    void save();

signals:
    // 有没有课变了（导课、清空、增删之后）
    void emptyChanged();

private:
    int indexOfId(int id) const;
    void reassignColors(); // 按天分组、同列按节次排序，白 / 淡绿交替
    void refreshColors();

    QList<Course> m_courses;
    int m_nextId = 1;
};
