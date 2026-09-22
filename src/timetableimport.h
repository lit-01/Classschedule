#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// 从课表 PDF 里解析出的一次课。
// 同一天同一个大节挂多门课（比如单双周分开上）就是多条记录。
struct ImportedCourse
{
    QString name;
    QString teacher;
    QString room;
    QString campus;
    QString category;   // 理论 / 实验 / 上机 / 实践 —— 从 ★ ☆ ◆ ■ 来的
    QString weeksLabel; // 原文，例如 "4-8周,10-16周" / "2-16周(双)"
    int day = 1;        // 1 = 周一 … 7 = 周日
    int startSection = 1; // 小节号。1-2 节 = 第 1 个大节，见下
    int endSection = 1;
    QList<int> weeks;   // 展开成具体周次

    // 界面用「大节」排（一堂课 = 两小节），1-2 节是第 1 大节，3-4 节是第 2 大节……
    int startBlock() const { return (startSection + 1) / 2; }
    int endBlock() const { return (endSection + 1) / 2; }
};

struct ImportResult
{
    QList<ImportedCourse> courses;
    int maxWeek = 0;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 把教务系统导出的课表 PDF 解析成课程列表。
//
// 认的是「iText 导出 + STSong-Light-UniGB-UCS2-H 字体 + 表格线是矢量矩形」
// 这一种版式（国内教务系统里很常见）。别的版式会返回带 error 的结果，
// 而不是硬凑一堆错数据出来。
class TimetableImporter
{
public:
    static ImportResult importPdf(const QByteArray &pdfBytes);

    // 只把文字按星期整理成 7 段，不解析成课程。
    // 「AI 解析」那条备用通道用它做准备（也能拿来人工看「到底读到些什么」）。
    // 失败返回空串，理由写进 error。
    static QString extractTimetableText(const QByteArray &pdfBytes, QString *error);
};
