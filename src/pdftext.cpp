#include "pdftext.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace {

// ==================== PDF 底层：拆对象、解压流 ====================

// 把 "12 0 obj … endobj" 切成 objNum → 对象体。
// 用 Latin-1 转成 QString 再正则是安全的：Latin-1 是单字节映射，
// 字符下标和字节下标一一对应，不会把二进制内容搞坏。
QHash<int, QByteArray> splitObjects(const QByteArray &pdf)
{
    QHash<int, QByteArray> objs;
    const QString text = QString::fromLatin1(pdf);

    static const QRegularExpression objRe(QStringLiteral("(\\d+)\\s+0\\s+obj\\b"));
    static const QRegularExpression endRe(QStringLiteral("\\bendobj\\b"));

    auto it = objRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int num = m.captured(1).toInt();
        const int from = m.capturedEnd();
        const auto end = endRe.match(text, from);
        const int to = end.hasMatch() ? end.capturedStart() : text.size();
        objs.insert(num, pdf.mid(from, to - from));
    }
    return objs;
}

// 取对象里 stream … endstream 之间的原始字节。
QByteArray streamData(const QByteArray &body)
{
    const int at = body.indexOf("stream");
    if (at < 0)
        return {};

    int from = at + 6; // 跳过 "stream"
    if (from < body.size() && body[from] == '\r')
        ++from;
    if (from < body.size() && body[from] == '\n')
        ++from;

    int to = body.indexOf("endstream", from);
    if (to < 0)
        return {};
    while (to > from && (body[to - 1] == '\n' || body[to - 1] == '\r'))
        --to; // endstream 前面那个换行不属于数据

    return body.mid(from, to - from);
}

// FlateDecode。PDF 里存的是裸 zlib 流，而 qUncompress 要求前面多 4 个字节
// 的大端「原始长度」，所以得手动包一层。
QByteArray inflate(const QByteArray &raw)
{
    if (raw.isEmpty())
        return {};

    QByteArray wrapped;
    wrapped.resize(4);
    const quint32 n = quint32(raw.size());
    wrapped[0] = char((n >> 24) & 0xff);
    wrapped[1] = char((n >> 16) & 0xff);
    wrapped[2] = char((n >> 8) & 0xff);
    wrapped[3] = char(n & 0xff);
    wrapped += raw;

    return qUncompress(wrapped);
}

// ==================== 内容流扫描 ====================

bool isDigitChar(char c) { return c >= '0' && c <= '9'; }
bool isAlphaChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int readNumber(const QByteArray &b, int i, double *out)
{
    const int start = i;
    const int n = b.size();
    while (i < n && (isDigitChar(b[i]) || b[i] == '.' || b[i] == '-'
                     || b[i] == '+')) {
        if ((b[i] == '-' || b[i] == '+') && i > start)
            break;
        ++i;
    }
    bool ok = false;
    *out = b.mid(start, i - start).toDouble(&ok);
    if (!ok)
        *out = 0;
    return i;
}

// b[i] 是 '('，读到配对的 ')' 为止，处理转义和嵌套括号。
int readString(const QByteArray &b, int i, QByteArray *out)
{
    ++i; // 跳过 '('
    int depth = 1;
    const int n = b.size();

    while (i < n) {
        const char c = b[i];

        if (c == '\\') {
            ++i;
            if (i >= n)
                break;
            const char e = b[i];
            switch (e) {
            case 'n': out->append('\n'); ++i; break;
            case 'r': out->append('\r'); ++i; break;
            case 't': out->append('\t'); ++i; break;
            case 'b': out->append('\b'); ++i; break;
            case 'f': out->append('\f'); ++i; break;
            case '(': out->append('('); ++i; break;
            case ')': out->append(')'); ++i; break;
            case '\\': out->append('\\'); ++i; break;
            default:
                if (e >= '0' && e <= '7') {
                    int v = 0;
                    int k = 0;
                    while (k < 3 && i < n && b[i] >= '0' && b[i] <= '7') {
                        v = v * 8 + (b[i] - '0');
                        ++i;
                        ++k;
                    }
                    out->append(char(v & 0xff));
                } else {
                    out->append(e); // 不认识的转义，原样留着
                    ++i;
                }
                break;
            }
            continue;
        }

        if (c == '(') {
            ++depth;
            out->append(c);
            ++i;
            continue;
        }
        if (c == ')') {
            --depth;
            ++i;
            if (depth == 0)
                break;
            out->append(c);
            continue;
        }

        out->append(c);
        ++i;
    }
    return i;
}

// STSong-Light-UniGB-UCS2-H：字节就是 UCS-2 大端，两个一组直接转。
QString decodeBytes(const QByteArray &b)
{
    QString out;
    out.reserve(b.size() / 2);
    for (int i = 0; i + 1 < b.size(); i += 2) {
        const ushort hi = uchar(b[i]);
        const ushort lo = uchar(b[i + 1]);
        out.append(QChar((hi << 8) | lo));
    }
    return out;
}

void scanContent(const QByteArray &content, PdfPageText *page)
{
    const int n = content.size();
    QVector<double> nums;
    QByteArray pending;

    double curX = 0;
    double curY = 0;

    auto flush = [&]() {
        if (pending.isEmpty())
            return;
        const QString text = decodeBytes(pending);
        pending.clear();
        if (text.trimmed().isEmpty())
            return;
        page->items.append({curX, curY, text});
    };

    int i = 0;
    while (i < n) {
        const char c = content[i];

        if (c == '(') {
            i = readString(content, i, &pending);
            continue;
        }

        if (c == '[' || c == ']') { // TJ 的数组壳，里面的字符串已经攒在 pending 里
            ++i;
            continue;
        }

        if (isDigitChar(c) || c == '-' || c == '+' || c == '.') {
            double v = 0;
            i = readNumber(content, i, &v);
            nums.append(v);
            if (nums.size() > 8)
                nums.removeFirst();
            continue;
        }

        if (isAlphaChar(c)) {
            const int start = i;
            while (i < n && (isAlphaChar(content[i]) || content[i] == '*'))
                ++i;
            const QByteArray op = content.mid(start, i - start);

            if (op == "Tm" && nums.size() >= 6) {
                curX = nums[nums.size() - 2];
                curY = nums[nums.size() - 1];
            } else if ((op == "Td" || op == "TD") && nums.size() >= 2) {
                curX += nums[nums.size() - 2];
                curY += nums[nums.size() - 1];
            } else if (op == "re" && nums.size() >= 4) {
                const int k = nums.size();
                page->cellRects.append(
                    QRectF(nums[k - 4], nums[k - 3], nums[k - 2], nums[k - 1]));
            } else if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
                flush();
            }

            nums.clear(); // 操作数被这个操作符消费掉了
            continue;
        }

        ++i;
    }
}

} // namespace

// ==================== 对外入口 ====================

QVector<PdfPageText> PdfTextExtractor::extract(const QByteArray &pdf, QString *error)
{
    QVector<PdfPageText> pages;
    if (error)
        error->clear();

    const auto objs = splitObjects(pdf);
    if (objs.isEmpty()) {
        if (error)
            *error = QStringLiteral("这不是一个有效的 PDF（找不到任何对象）");
        return pages;
    }

    // 只认 UCS-2 那一类中文编码。别的字体（比如嵌入了 TrueType 子集、
    // 或者带 ToUnicode 表的）这里解出来会是乱码，不如直接说清楚。
    const QString whole = QString::fromLatin1(pdf);
    const bool ucs2 = whole.contains(QLatin1String("UniGB-UCS2"))
                      || whole.contains(QLatin1String("UniCNS-UCS2"))
                      || whole.contains(QLatin1String("UniJIS-UCS2"))
                      || whole.contains(QLatin1String("UniKS-UCS2"));
    if (!ucs2) {
        if (error)
            *error = QStringLiteral(
                "这份 PDF 的中文不是 UCS-2 编码，暂时认不出来。\n"
                "（现在只支持教务系统用 iText 导出的那种课表）");
        return pages;
    }

    // 找 /Type /Page 的对象，取出它引用的内容流对象号。
    // 按页对象号排序当阅读顺序 —— 这份 PDF 正好是递增的。
    static const QRegularExpression typePage(
        QStringLiteral("/Type\\s*/Page(?![a-zA-Z])"));
    static const QRegularExpression contentsRe(
        QStringLiteral("/Contents\\s+(\\d+)\\s+0\\s+R"));

    QVector<QPair<int, int>> pageList; // <页对象号, 内容流对象号>
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it) {
        const QString body = QString::fromLatin1(it.value());
        if (!typePage.match(body).hasMatch())
            continue;
        const auto m = contentsRe.match(body);
        if (!m.hasMatch())
            continue;
        pageList.append({it.key(), m.captured(1).toInt()});
    }
    std::sort(pageList.begin(), pageList.end(),
              [](const QPair<int, int> &a, const QPair<int, int> &b) {
                  return a.first < b.first;
              });

    if (pageList.isEmpty()) {
        if (error)
            *error = QStringLiteral("PDF 里没有找到页面");
        return pages;
    }

    for (const auto &entry : pageList) {
        const auto it = objs.constFind(entry.second);
        if (it == objs.constEnd())
            continue;

        const QByteArray content = inflate(streamData(it.value()));
        if (content.isEmpty())
            continue;

        PdfPageText page;
        scanContent(content, &page);
        if (page.items.isEmpty() && page.cellRects.isEmpty())
            continue;
        pages.append(page);
    }

    if (pages.isEmpty() && error)
        *error = QStringLiteral("PDF 里的文字没能提取出来");

    return pages;
}
