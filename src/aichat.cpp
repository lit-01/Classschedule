#include "aichat.h"

#include "aiimporter.h"
#include "coursemodel.h"
#include "soul.h"

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
               QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_courses(courses)
    , m_soul(soul)
    , m_net(new QNetworkAccessManager(this))
{
}

void AiChat::append(const QString &role, const QString &content,
                    const QString &reasoning)
{
    QVariantMap message;
    message.insert(QStringLiteral("role"), role);
    message.insert(QStringLiteral("content"), content);
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
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !m_config)
        return;

    if (!m_config->configured()) {
        append(QStringLiteral("user"), trimmed);
        append(QStringLiteral("assistant"),
               tr("还没配 API。长按上面那个 AI 按钮，把 Key 填上就能聊了。"));
        return;
    }

    append(QStringLiteral("user"), trimmed);

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
