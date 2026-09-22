#pragma once

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

// 从 PDF 里抠出来的一小段文字，外加它在「内容流坐标系」里的位置。
//
// 坐标系说明：这份 PDF 的内容流开头有 `0 1 -1 0 595 0 cm`，把内容流的
// (x, y) 映射成页面上的 (595 - y, x)。也就是整个页面是**转着画**的。
// 但好在做「按列分组」根本不需要转到页面坐标 —— 表格竖线的 x 和文字基线的
// x 本来就在同一个内容流坐标系里，直接拿来比大小就行：
//   · item.x  → 决定这个字属于星期几那一列（越大越靠右）
//   · item.y  → 决定它在第几行（越大越靠页面上方）
struct PdfTextItem
{
    double x = 0;
    double y = 0;
    QString text;
};

struct PdfPageText
{
    QVector<PdfTextItem> items;
    // 表格里所有单元格边框（`x y w h re`）。用来算星期列的位置：
    // 宽度是「众数」的那一档就是数据列，它的 x 起点就是每天的左边界。
    QVector<QRectF> cellRects;
};

// 一个够用就好的 PDF 文字提取器。
//
// 为什么自己写而不是用 QtPdf：
//   1. 教务系统导出的课表是 iText 生成的，结构规整，不是扫描件；
//   2. 这份 PDF 用的 STSong-Light-UniGB-UCS2-H 字体**没有 ToUnicode 表**，
//      内容流里的字节直接就是 UCS-2 大端，省掉了整个字体编码映射的坑；
//   3. 不依赖 QtPdf，Android 上也不用额外配组件。
//
// 它**只认识这一种编码**。碰到别的字体编码会在 error 里说明白，不会瞎猜。
class PdfTextExtractor
{
public:
    // pdf：整个 PDF 文件的字节。失败返回空，理由写进 error。
    static QVector<PdfPageText> extract(const QByteArray &pdf, QString *error);
};
