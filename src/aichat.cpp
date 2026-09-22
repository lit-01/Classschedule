#include "aichat.h"

#include "aiimporter.h"
#include "calendardata.h"
#include "coursemodel.h"
#include "filebridge.h"
#include "soul.h"
#include "pdftext.h"
#include "timetableimport.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace {

// [1,2,3,5,6] → "1-3周,5-6周"。（aiimporter.cpp 里还有一份一样的，
// 那边是解析 PDF 用的；抽成公共工具反而更绕，就先各留一份。）
QString labelFromWeeks(const QVariantList &weeks)
{
    QList<int> ws;
    for (const QVariant &v : weeks) {
        const int w = v.toInt();
        if (w > 0)
            ws.append(w);
    }
    std::sort(ws.begin(), ws.end());
    ws.erase(std::unique(ws.begin(), ws.end()), ws.end());

    QStringList parts;
    int i = 0;
    while (i < ws.size()) {
        int j = i;
        while (j + 1 < ws.size() && ws.at(j + 1) == ws.at(j) + 1)
            ++j;
        parts << (i == j ? QString::number(ws.at(i))
                         : QStringLiteral("%1-%2").arg(ws.at(i)).arg(ws.at(j)));
        i = j + 1;
    }
    return parts.join(QLatin1Char(',')) + QStringLiteral("周");
}

// 多模态消息里的一个「文本块」
QVariantMap textPart(const QString &text)
{
    QVariantMap part;
    part.insert(QStringLiteral("type"), QStringLiteral("text"));
    part.insert(QStringLiteral("text"), text);
    return part;
}

// 把整个文件读进字节（附件用）
QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// 支持发的图片格式
bool isImageFile(const QString &lowerPath)
{
    return lowerPath.endsWith(QStringLiteral(".png"))
           || lowerPath.endsWith(QStringLiteral(".jpg"))
           || lowerPath.endsWith(QStringLiteral(".jpeg"))
           || lowerPath.endsWith(QStringLiteral(".bmp"))
           || lowerPath.endsWith(QStringLiteral(".webp"))
           || lowerPath.endsWith(QStringLiteral(".gif"));
}

struct OpsBlock
{
    QJsonObject ops;
    int start = -1;
    int length = 0;
};

// 找回复里那段改课 JSON —— 裹在 ```json 里也行，光秃秃摆在结尾也行。
// 找到之后把位置也记下来，等下从气泡正文里剪掉。
OpsBlock findOps(const QString &reply)
{
    OpsBlock found;

    static const QRegularExpression fenced(
        QStringLiteral("```(?:json)?\\s*(\\{.*?\\})\\s*```"),
        QRegularExpression::DotMatchesEverythingOption);
    const auto match = fenced.match(reply);
    if (match.hasMatch()) {
        found.ops = QJsonDocument::fromJson(match.captured(1).toUtf8()).object();
        if (!found.ops.isEmpty()) {
            found.start = match.capturedStart();
            found.length = match.capturedLength();
            return found;
        }
    }

    // 没裹代码块就自己数括号找结尾
    const int at = reply.indexOf(QStringLiteral("{\"ops\""));
    if (at < 0)
        return found;

    int depth = 0;
    for (int i = at; i < reply.size(); ++i) {
        if (reply.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (reply.at(i) == QLatin1Char('}')) {
            --depth;
            if (depth == 0) {
                const QString text = reply.mid(at, i - at + 1);
                found.ops = QJsonDocument::fromJson(text.toUtf8()).object();
                if (!found.ops.isEmpty()) {
                    found.start = at;
                    found.length = text.size();
                }
                break;
            }
        }
    }
    return found;
}

} // namespace

AiChat::AiChat(AiImporter *config, CourseModel *courses, Soul *soul,
               CalendarData *calendar, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_courses(courses)
    , m_soul(soul)
    , m_calendar(calendar)
    , m_net(new QNetworkAccessManager(this))
{
}

void AiChat::append(const QString &role, const QString &content,
                    const QString &reasoning, const QVariant &parts)
{
    QVariantMap message;
    message.insert(QStringLiteral("role"), role);
    message.insert(QStringLiteral("content"), content);
    // 发给模型的实际内容：普通消息是字符串，带附件的是多模态数组。
    // 界面显示只看 content（纯文字），parts 只在组请求时用。
    if (parts.isValid())
        message.insert(QStringLiteral("parts"), parts);
    // 思考过程：有思考模式的模型会单独回一段，界面上折叠着显示
    message.insert(QStringLiteral("reasoning"), reasoning);
    message.insert(QStringLiteral("isUser"), role == QLatin1String("user"));
    m_messages.append(message);
    emit messagesChanged();
}

void AiChat::clear()
{
    if (m_messages.isEmpty())
        return;
    m_messages.clear();
    emit messagesChanged();
}

void AiChat::acceptRoleChange()
{
    if (m_soul && !m_pendingRole.isEmpty())
        m_soul->setText(m_pendingRole);
    m_pendingRole.clear();
}

void AiChat::rejectRoleChange()
{
    m_pendingRole.clear();
}

// 课表摘要。模型得先看见现在有什么课，改的时候才知道动哪一门。
QString AiChat::timetableContext() const
{
    if (!m_courses || m_courses->empty())
        return QStringLiteral("当前课表是空的。");

    static const char *const kDays[] = {"", "周一", "周二", "周三", "周四",
                                        "周五", "周六", "周日"};

    QString text = QStringLiteral("当前课表（改课要用方括号里的 id）：\n");
    for (const Course &c : m_courses->allCourses()) {
        text += QStringLiteral("[%1] %2 / 老师:%3 / 地点:%4 / 类别:%5 / %6 第%7大节 / %8\n")
                    .arg(c.id)
                    .arg(c.name)
                    .arg(c.teacher.isEmpty() ? QStringLiteral("—") : c.teacher)
                    .arg(c.room.isEmpty() ? QStringLiteral("—") : c.room)
                    .arg(c.category.isEmpty() ? QStringLiteral("—") : c.category)
                    .arg(QString::fromUtf8(kDays[qBound(1, c.day, 7)]))
                    .arg(c.startSection)
                    .arg(c.weeksText());
    }
    return text;
}

QString AiChat::dateContext() const
{
    static const char *const kWeekDays[] = {"", "星期一", "星期二", "星期三", "星期四",
                                            "星期五", "星期六", "星期日"};
    const QDate today = QDate::currentDate();

    QString s = QStringLiteral("\n【今天的日期】%1 %2。")
                    .arg(today.toString(QStringLiteral("yyyy-MM-dd")),
                         QString::fromUtf8(kWeekDays[qBound(1, today.dayOfWeek(), 7)]));

    if (!m_calendar) {
        s += QStringLiteral("（学期起始日还没定，问用户开学第一天是几号）\n");
        return s;
    }

    const QDate first = m_calendar->firstMonday();
    int week = 0;
    if (first.isValid()) {
        week = first.daysTo(today) / 7 + 1;
        s += QStringLiteral("第 1 周的周一是 %1，所以今天是第 %2 周。")
                 .arg(first.toString(QStringLiteral("yyyy-MM-dd")))
                 .arg(week);
        if (week < 1)
            s += QStringLiteral("（今天还在开学前）");
        if (m_calendar->isHoliday(today.toString(QStringLiteral("yyyy-MM-dd"))))
            s += QStringLiteral("今天是法定放假日。");
    } else {
        s += QStringLiteral("学期起始日还没定，需要时问用户开学第一天是几号。");
    }

    // 顺手把「今天有什么课」算出来，省得模型自己数错
    if (m_courses && !m_courses->empty() && week >= 1) {
        QStringList todayCourses;
        for (const Course &c : m_courses->allCourses()) {
            if (c.day == today.dayOfWeek() && m_courses->isActive(c.id, week))
                todayCourses.append(c.name);
        }
        s += todayCourses.isEmpty()
                 ? QStringLiteral("今天没有课。")
                 : QStringLiteral("今天有课：%1。").arg(todayCourses.join(QStringLiteral("、")));
    }

    s += QStringLiteral("\n用户说的「今天 / 明天 / 这周」都按这个日期算；"
                        "涉及具体哪一天上课，先把「第几周 + 星期几」对上再回答。\n");
    return s;
}

QString AiChat::systemPrompt() const
{
    // 人格那部分来自 Soul —— 一份用户可以自己改的文件，跟用哪家模型无关
    QString prompt = m_soul ? m_soul->text() : QString();

    prompt += QStringLiteral(
        "\n\n你可以直接改用户的课表。用户让你加课 / 删课 / 改课时，"
        "除了正常回复，还要在回复最末尾附一个 JSON 代码块，格式：\n"
        "```json\n"
        "{\"ops\":[{\"op\":\"add\",\"course\":{\"name\":\"高等数学\",\"teacher\":\"张三\","
        "\"room\":\"博慧楼101\",\"category\":\"理论\",\"day\":1,"
        "\"startSection\":1,\"endSection\":1,\"weeks\":[1,2,3]}}]}\n"
        "```\n"
        "op 可以是 add / update / remove；update 和 remove 要带上 id。\n"
        "另外你**没有权限直接改「角色设定」**（决定你说话调调的那段文字）。"
        "想改就提一个 {\"op\":\"set_role\",\"text\":\"完整的角色设定\"} —— "
        "这只是提议，界面会弹框让用户点头；正文里也要说清楚你想改成什么、为什么。\n"
        "day 是 1-7（周一到周日）；startSection / endSection 是**大节号**，"
        "取 1-5（一天 5 个大节，一个大节 = 两小节）；weeks 是具体第几周的数组。\n"
        "不需要改课的时候就别输出这段 JSON。\n\n");

    prompt += QStringLiteral(
        "\n如果用户发来的是**课表（PDF 或图片）**，请直接识别里面的每一门课，"
        "在回复末尾用改课 JSON 把课程 add 进去（字段同上：name / teacher / room / "
        "category（可留空）/ day / startSection / endSection / weeks）。"
        "只 add 新课，不要删掉现有课表。识别不出周次时先问用户开学时间。\n");

    prompt += QStringLiteral(
        "\n课表 PDF 里一般没有「开学日期」，但它决定了日期显示对不对。"
        "用户告诉你这学期第一周的周一（开学那天）时，用这个 op 存下来：\n"
        "```json\n"
        "{\"ops\":[{\"op\":\"set_first_monday\",\"date\":\"2026-09-07\"}]}\n"
        "```\n"
        "拿不准就先问清楚，别自己猜。\n");

    prompt += dateContext();
    prompt += timetableContext();
    return prompt;
}

QString AiChat::applyOps(const QString &reply)
{
    const OpsBlock found = findOps(reply);
    if (found.ops.isEmpty() || !m_courses)
        return reply;

    const QJsonArray ops = found.ops.value(QStringLiteral("ops")).toArray();
    for (const QJsonValue &value : ops) {
        const QJsonObject item = value.toObject();
        const QString kind = item.value(QStringLiteral("op")).toString();

        if (kind == QLatin1String("set_role")) {
            // 只是提议，不落地 —— 等用户在界面上点同意
            const QString text =
                item.value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty()) {
                m_pendingRole = text;
                emit roleChangeRequested();
            }
            continue;
        }

        if (kind == QLatin1String("set_first_monday")) {
            // 用户说了开学第一周的周一，存下来，日期显示就对了
            const QString date = item.value(QStringLiteral("date")).toString().trimmed();
            const QDate parsed = QDate::fromString(date, QStringLiteral("yyyy-MM-dd"));
            if (m_calendar && parsed.isValid())
                m_calendar->setFirstMonday(parsed);
            continue;
        }

        if (kind == QLatin1String("remove")) {
            const int id = item.value(QStringLiteral("id")).toInt();
            if (id > 0)
                m_courses->removeCourse(id);
            continue;
        }

        const QJsonObject data = item.value(QStringLiteral("course")).toObject();

        QVariantMap course;
        course.insert(QStringLiteral("name"),
                      data.value(QStringLiteral("name")).toString());
        course.insert(QStringLiteral("teacher"),
                      data.value(QStringLiteral("teacher")).toString());
        course.insert(QStringLiteral("room"),
                      data.value(QStringLiteral("room")).toString());
        course.insert(QStringLiteral("category"),
                      data.value(QStringLiteral("category")).toString());
        course.insert(QStringLiteral("day"),
                      qBound(1, data.value(QStringLiteral("day")).toInt(1), 7));

        const int start = qMax(1, data.value(QStringLiteral("startSection")).toInt(1));
        const int end =
            qMax(start, data.value(QStringLiteral("endSection")).toInt(start));
        course.insert(QStringLiteral("startSection"),
                      qMin(start, CourseModel::kMaxSections));
        course.insert(QStringLiteral("endSection"),
                      qMin(end, CourseModel::kMaxSections));

        QVariantList weeks;
        for (const QJsonValue &w : data.value(QStringLiteral("weeks")).toArray()) {
            const int week = w.toInt();
            if (week > 0)
                weeks.append(week);
        }
        std::sort(weeks.begin(), weeks.end(),
                  [](const QVariant &a, const QVariant &b) {
                      return a.toInt() < b.toInt();
                  });
        course.insert(QStringLiteral("weeks"), weeks);
        course.insert(QStringLiteral("weeksLabel"),
                      weeks.isEmpty() ? QString() : labelFromWeeks(weeks));
        course.insert(QStringLiteral("startWeek"),
                      weeks.isEmpty() ? 0 : weeks.first().toInt());
        course.insert(QStringLiteral("endWeek"),
                      weeks.isEmpty() ? 0 : weeks.last().toInt());

        if (kind == QLatin1String("update")) {
            const int id = item.value(QStringLiteral("id")).toInt();
            if (id > 0)
                m_courses->updateCourse(id, course);
        } else if (kind == QLatin1String("add")) {
            if (!course.value(QStringLiteral("name")).toString().isEmpty())
                m_courses->addCourse(course);
        }
    }

    // 正文里不要那段 JSON，气泡里看着太脏
    QString clean = reply;
    clean.remove(found.start, found.length);
    return clean.trimmed();
}

void AiChat::send(const QString &text)
{
    send(text, {});
}

void AiChat::send(const QString &text, const QStringList &filePaths)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() && filePaths.isEmpty())
        return;
    if (!m_config)
        return;

    // 拼出「界面显示用的纯文字」和「发给模型的多模态内容」两部分
    QString display = trimmed;
    QVariantList parts;
    if (!trimmed.isEmpty())
        parts.append(textPart(trimmed));

    for (const QString &fp : filePaths) {
        // 系统文件选择器给的不一定是本地路径（安卓上是 content://），
        // 统一过 FileBridge：拿显示名 + 变成真读得到的本地文件
        const QUrl url(fp);
        QString name = fileBridgeDisplayName(url);
        QString resolveErr;
        const QString local = fileBridgeResolve(url, &resolveErr);

        if (local.isEmpty()) {
            display += QStringLiteral("\n[附件] %1（读不了：%2）")
                           .arg(name.isEmpty() ? QStringLiteral("附件") : name,
                                resolveErr.isEmpty() ? QStringLiteral("未知原因") : resolveErr);
            continue;
        }
        if (name.isEmpty())
            name = QFileInfo(local).fileName();
        const QString lower = name.toLower();

        if (lower.endsWith(QStringLiteral(".pdf"))) {
            // PDF 没有通用视觉接口，先本地抠文字；抠不出来就如实告诉模型
            QString err;
            const QByteArray bytes = readFile(local);
            QString pdfText;
            if (!bytes.isEmpty()) {
                const QVector<PdfPageText> pages =
                    PdfTextExtractor::extract(bytes, &err);
                if (!pages.isEmpty())
                    pdfText = TimetableImporter::extractTimetableText(bytes, &err);
            }
            if (!pdfText.isEmpty()) {
                parts.append(textPart(QStringLiteral("（课表 PDF 内容）\n") + pdfText));
                display += QStringLiteral("\n[附件] %1").arg(name);
            } else {
                display += QStringLiteral("\n[附件] %1（PDF 没读到文字，可能是扫描件）")
                               .arg(name);
            }
        } else if (isImageFile(lower)) {
            // 图片：原图 base64 发给支持视觉的模型直接看
            const QString dataUrl = AiImporter::imageToDataUrl(local);
            if (!dataUrl.isEmpty()) {
                QVariantMap img;
                img.insert(QStringLiteral("type"), QStringLiteral("image_url"));
                QVariantMap u;
                u.insert(QStringLiteral("url"), dataUrl);
                img.insert(QStringLiteral("image_url"), u);
                parts.append(img);
                display += QStringLiteral("\n[附件] %1").arg(name);
            } else {
                display += QStringLiteral("\n[附件] %1（图片读不出来）").arg(name);
            }
        } else {
            display += QStringLiteral("\n[附件] %1（不支持的格式）").arg(name);
        }
    }

    if (!m_config->configured()) {
        append(QStringLiteral("user"), display);
        append(QStringLiteral("assistant"),
               tr("还没配 API。长按上面那个 AI 按钮，把 Key 填上就能聊了。"));
        return;
    }

    append(QStringLiteral("user"), display, QString(),
           parts.isEmpty() ? QVariant() : QVariant(parts));
    postRequest();
}

void AiChat::postRequest()
{
    QString base = m_config->apiBase();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (!base.endsWith(QLatin1String("/chat/completions")))
        base += QStringLiteral("/chat/completions");

    QNetworkRequest request{QUrl(base)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_config->apiKey().toUtf8());

    QJsonArray messages;
    const QString system = systemPrompt();
    if (!system.isEmpty()) {
        QJsonObject item;
        item.insert(QStringLiteral("role"), QStringLiteral("system"));
        item.insert(QStringLiteral("content"), system);
        messages.append(item);
    }
    for (const QVariant &value : m_messages) {
        const QVariantMap m = value.toMap();
        QJsonObject item;
        item.insert(QStringLiteral("role"),
                    m.value(QStringLiteral("role")).toString());
        const QVariant parts = m.value(QStringLiteral("parts"));
        if (parts.isValid() && parts.canConvert<QVariantList>())
            item.insert(QStringLiteral("content"),
                        QJsonArray::fromVariantList(parts.toList()));
        else
            item.insert(QStringLiteral("content"),
                        m.value(QStringLiteral("content")).toString());
        messages.append(item);
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_config->model());
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), false);

    m_busy = true;
    emit messagesChanged();

    QNetworkReply *reply =
        m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        handleReply(reply);
    });
}

void AiChat::handleReply(QNetworkReply *reply)
{
    m_busy = false;

    const QByteArray raw = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        append(QStringLiteral("assistant"),
               tr("请求没发出去：%1\n%2")
                   .arg(reply->errorString(), QString::fromUtf8(raw.left(300))));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    if (root.contains(QStringLiteral("error"))) {
        const QString detail = root.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString();
        append(QStringLiteral("assistant"),
               tr("接口报错：%1")
                   .arg(detail.isEmpty() ? QString::fromUtf8(raw.left(300)) : detail));
        return;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        append(QStringLiteral("assistant"),
               tr("接口没返回内容：%1").arg(QString::fromUtf8(raw.left(300))));
        return;
    }

    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    QString content = message.value(QStringLiteral("content")).toString();

    // 思考内容各家叫法不太一样，常见这两种都认一下
    QString reasoning = message.value(QStringLiteral("reasoning_content")).toString();
    if (reasoning.isEmpty())
        reasoning = message.value(QStringLiteral("reasoning")).toString();

    if (content.isEmpty() && reasoning.isEmpty())
        content = tr("（这次没返回文字）");

    // 模型要是答应了改课 / 改角色，顺手处理掉，气泡里只留正文
    content = applyOps(content);
    if (content.isEmpty())
        content = tr("（处理好了）");

    append(QStringLiteral("assistant"), content, reasoning);
}
