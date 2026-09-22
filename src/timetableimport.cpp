#include "timetableimport.h"

#include "pdftext.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace {

constexpr int kDayCount = 7;

struct Column
{
    double left = 0;
    double right = 0;
};

// 课程名两头挂的 ★ ☆ ◆ ■ 其实是课程类别标记（PDF 页脚有图例）：
// 一边把它们抠出来当类别，一边把名字清理干净。
QString takeCategory(const QString &raw, QString *cleanName)
{
    QString category;
    QString name;
    for (const QChar &ch : raw) {
        if (ch == QChar(0x2605))      // ★
            category = QStringLiteral("理论");
        else if (ch == QChar(0x2606)) // ☆
            category = QStringLiteral("实验");
        else if (ch == QChar(0x25C6)) // ◆
            category = QStringLiteral("上机");
        else if (ch == QChar(0x25A0)) // ■
            category = QStringLiteral("实践");
        else
            name.append(ch);
    }
    *cleanName = name.trimmed();
    return category;
}

// "1-16周" / "2-16周(双)" / "9周" / "4-8周,10-16周" → 具体周次
QList<int> expandWeeks(const QString &label)
{
    static const QRegularExpression rangeRe(QStringLiteral(
        "(\\d+)(?:\\s*-\\s*(\\d+))?\\s*周\\s*(?:\\(?\\s*([双单])\\s*\\)?)?"));

    QList<int> weeks;
    auto it = rangeRe.globalMatch(label);
    while (it.hasNext()) {
        const auto m = it.next();
        int a = m.captured(1).toInt();
        int b = m.captured(2).isEmpty() ? a : m.captured(2).toInt();
        if (b < a)
            std::swap(a, b);

        const QString parity = m.captured(3);
        for (int w = a; w <= b; ++w) {
            if (parity == QStringLiteral("双") && w % 2 != 0)
                continue;
            if (parity == QStringLiteral("单") && w % 2 != 1)
                continue;
            if (!weeks.contains(w))
                weeks.append(w);
        }
    }
    std::sort(weeks.begin(), weeks.end());
    return weeks;
}

// 从表格的单元格矩形里推出 7 个数据列的左右边界。
//
// 表格里每条线都是个 `x y w h re` 矩形。宽度出现次数最多的那一档
// （这里是 103.85）就是每天的数据列；「时间段」列（46.73）和「节次」列
// （36.35）比它窄，会被 `width > 50` 这条件自然筛掉。
bool computeColumns(const QVector<PdfPageText> &pages, QList<Column> *cols,
                    QString *error)
{
    QHash<int, int> widthCount;
    for (const PdfPageText &page : pages) {
        for (const QRectF &r : page.cellRects) {
            if (r.width() > 50.0)
                widthCount[qRound(r.width())] += 1;
        }
    }
    if (widthCount.isEmpty()) {
        *error = QStringLiteral("PDF 里没找到表格线，认不出课表的列。");
        return false;
    }

    int bestWidth = 0;
    int bestCount = 0;
    for (auto it = widthCount.constBegin(); it != widthCount.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            bestWidth = it.key();
        }
    }

    QList<double> xs;
    for (const PdfPageText &page : pages) {
        for (const QRectF &r : page.cellRects) {
            if (qAbs(r.width() - bestWidth) > 1.0)
                continue;
            xs.append(r.x());
        }
    }
    std::sort(xs.begin(), xs.end());

    QList<double> uniq;
    for (double x : xs) {
        if (uniq.isEmpty() || x - uniq.last() > 5.0)
            uniq.append(x);
    }

    if (uniq.size() < kDayCount) {
        *error = QStringLiteral("只认出 %1 列，课表该有 7 天。").arg(uniq.size());
        return false;
    }
    if (uniq.size() > kDayCount)
        uniq = uniq.mid(uniq.size() - kDayCount); // 多了就取最右边 7 列

    for (int i = 0; i < kDayCount; ++i)
        cols->append({uniq.at(i), uniq.at(i) + bestWidth});
    return true;
}

// 一列文本 → 若干课程。每个课程块都以「学分:x.x」收尾，拿它当分隔符最省事。
QList<ImportedCourse> parseColumn(const QString &text, int day)
{
    static const QRegularExpression blockRe(
        QStringLiteral("(.*?学分:[\\d.]+)"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression sectionRe(
        QStringLiteral("\\((\\d+)-(\\d+)节\\)"));
    static const QRegularExpression weeksRe(QStringLiteral("节\\)([^/]*)"));
    static const QRegularExpression campusRe(QStringLiteral("/校区:([^/]*)"));
    static const QRegularExpression roomRe(QStringLiteral("/场地:([^/]*)"));
    static const QRegularExpression teacherRe(QStringLiteral("/教师:([^/]*)"));

    auto field = [](const QString &chunk, const QRegularExpression &rx) {
        const auto m = rx.match(chunk);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };

    QList<ImportedCourse> out;
    auto it = blockRe.globalMatch(text);
    while (it.hasNext()) {
        const QString chunk = it.next().captured(1);

        const auto sec = sectionRe.match(chunk);
        if (!sec.hasMatch())
            continue; // 不是课（比如页脚那行图例）

        ImportedCourse c;
        c.category = takeCategory(chunk.left(sec.capturedStart()), &c.name);
        if (c.name.isEmpty())
            continue;

        c.day = day;
        c.startSection = sec.captured(1).toInt();
        c.endSection = sec.captured(2).toInt();

        c.weeksLabel = field(chunk, weeksRe);
        c.weeks = expandWeeks(c.weeksLabel);

        c.campus = field(chunk, campusRe);
        c.room = field(chunk, roomRe);
        c.teacher = field(chunk, teacherRe);

        out.append(c);
    }
    return out;
}

QString dayTitle(int index)
{
    static const char *const names[] = {"星期一", "星期二", "星期三", "星期四",
                                        "星期五", "星期六", "星期日"};
    return QString::fromUtf8(names[index]);
}

// 把每页的文字按星期分列、跨页接起来，得到 7 段文本。
bool buildColumns(const QVector<PdfPageText> &pages, QList<QString> *bucket,
                  QString *error)
{
    QList<Column> cols;
    if (!computeColumns(pages, &cols, error))
        return false;

    // 表头和标题只在第一页出现（后面几页是表格续页，顶格就接着写）。
    // 记一下「星期一」那一行的 y，把比它高的文本全丢掉 —— 不然标题和表头
    // 会粘到第一个课程名前面，变成「星期一概率论与数理统计」这种。
    double headerY = -1;
    for (const PdfPageText &page : pages) {
        for (const PdfTextItem &item : page.items) {
            if (item.text == QStringLiteral("星期一"))
                headerY = qMax(headerY, item.y);
        }
    }

    bucket->clear();
    bucket->resize(kDayCount);

    for (const PdfPageText &page : pages) {
        bool hasHeader = false;
        for (const PdfTextItem &item : page.items) {
            if (item.text == QStringLiteral("星期一")) {
                hasHeader = true;
                break;
            }
        }

        QList<QList<PdfTextItem>> perCol(kDayCount);
        for (const PdfTextItem &item : page.items) {
            if (hasHeader && headerY > 0 && item.y >= headerY - 2.0)
                continue; // 标题 / 表头
            // 页脚那行「打印时间:…」的 x 正好落在星期日那一列里，清掉
            if (item.text.contains(QStringLiteral("打印时间")))
                continue;
            for (int i = 0; i < kDayCount; ++i) {
                if (item.x >= cols.at(i).left - 3.0
                    && item.x < cols.at(i).right - 1.0) {
                    perCol[i].append(item);
                    break;
                }
            }
        }

        for (int i = 0; i < kDayCount; ++i) {
            QList<PdfTextItem> &list = perCol[i];
            // 从页面上方往下排；同一行里再按 x 从左到右
            std::sort(list.begin(), list.end(),
                      [](const PdfTextItem &a, const PdfTextItem &b) {
                          if (qAbs(a.y - b.y) > 0.5)
                              return a.y > b.y;
                          return a.x < b.x;
                      });
            // 中文直接连着拼 —— 单元格里的换行本来就是把一个词切断的
            for (const PdfTextItem &t : list)
                (*bucket)[i] += t.text;
        }
    }
    return true;
}

} // namespace

ImportResult TimetableImporter::importPdf(const QByteArray &pdfBytes)
{
    ImportResult result;

    QString err;
    const QVector<PdfPageText> pages = PdfTextExtractor::extract(pdfBytes, &err);
    if (!err.isEmpty()) {
        result.error = err;
        return result;
    }
    if (pages.isEmpty()) {
        result.error = QStringLiteral("PDF 里没读到任何内容。");
        return result;
    }

    QList<QString> bucket;
    if (!buildColumns(pages, &bucket, &err)) {
        result.error = err;
        return result;
    }

    for (int i = 0; i < kDayCount; ++i) {
        const QList<ImportedCourse> found = parseColumn(bucket.at(i), i + 1);
        for (const ImportedCourse &c : found) {
            result.courses.append(c);
            for (int w : c.weeks)
                result.maxWeek = qMax(result.maxWeek, w);
        }
    }

    if (result.courses.isEmpty())
        result.error = QStringLiteral("没解析出任何课程，这份课表的版式可能不认识。");

    return result;
}

QString TimetableImporter::extractTimetableText(const QByteArray &pdfBytes, QString *error)
{
    if (error)
        error->clear();

    QString err;
    const QVector<PdfPageText> pages = PdfTextExtractor::extract(pdfBytes, &err);
    if (!err.isEmpty()) {
        if (error)
            *error = err;
        return {};
    }

    QList<QString> bucket;
    if (!buildColumns(pages, &bucket, &err)) {
        if (error)
            *error = err;
        return {};
    }

    QString out;
    for (int i = 0; i < kDayCount; ++i) {
        out += QStringLiteral("【%1】\n").arg(dayTitle(i));
        out += bucket.at(i);
        out += QLatin1Char('\n');
    }
    return out;
}
