#include "aiimporter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace {

const char *const kApiBaseKey = "ai/apiBase";
const char *const kApiKeyKey = "ai/apiKey";
const char *const kModelKey = "ai/model";

// key 从 ai/preset 换成 ai/provider：预设列表改过几回，索引容易错位，
// 换个 key 让老存档自然失效，省得写迁移
const char *const kPresetKey = "ai/provider";

// 默认走 DeepSeek。deepseek-chat / deepseek-reasoner 这两个旧名字已经在
// 2026-07 退役了，现在只有 flash 和 pro 两个。
const QString kDefaultApiBase = QStringLiteral("https://api.deepseek.com");
const QString kDefaultModel = QStringLiteral("deepseek-flash");

QString buildPrompt(const QString &timetableText)
{
    return QStringLiteral(
               "下面是一份大学课表，已经按星期几分好组了。请把它解析成 JSON 数组。\n"
               "\n"
               "每门课一个对象，字段如下：\n"
               "  name          课程名（去掉 ★ ☆ ◆ ■ 这些标记）\n"
               "  day           1=周一 … 7=周日\n"
               "  startSection  起始小节号，照抄括号里的数字（\"(3-4节)\" 就是 3）\n"
               "  endSection    结束小节号（\"(3-4节)\" 就是 4）\n"
               "  weeks         整数数组，把周次全部展开成具体周。规则：\n"
               "                  \"1-16周\"        → 1 到 16 全部\n"
               "                  \"2-16周(双)\"    → 其中偶数周\n"
               "                  \"1-15周(单)\"    → 其中奇数周\n"
               "                  \"9周\"           → 只有第 9 周\n"
               "                  \"4-8周,10-16周\" → 两段都要，中间第 9 周不要\n"
               "  category      课程类别，只能填这四个之一：理论 / 实验 / 上机 / 实践\n"
               "                （课表里用 ★=理论 ☆=实验 ◆=上机 ■=实践 标在课程名两头，\n"
               "                 没有标记就留空字符串）\n"
               "  room          场地\n"
               "  teacher       教师\n"
               "\n"
               "课程名、课程类别、教师、场地这四样都不能漏。\n"
               "只输出 JSON 数组本身，不要解释，不要 markdown 代码块。\n"
               "示例：\n"
               "[{\"name\":\"高等数学\",\"day\":1,\"startSection\":1,\"endSection\":2,"
               "\"weeks\":[1,2,3],\"room\":\"博慧楼101\",\"teacher\":\"张三\","
               "\"category\":\"理论\"}]\n"
               "\n"
               "课表内容：\n")
           + timetableText;
}

// [1,2,3,5,6,7,10] → "1-3周,5-7周,10周"
QString labelFromWeeks(const QVariantList &weeks)
{
    QList<int> ws;
    ws.reserve(weeks.size());
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
        if (i == j)
            parts << QString::number(ws.at(i));
        else
            parts << QStringLiteral("%1-%2").arg(ws.at(i)).arg(ws.at(j));
        i = j + 1;
    }
    return parts.join(QLatin1Char(',')) + QStringLiteral("周");
}

} // namespace

AiImporter::AiImporter(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    QSettings settings;
    m_apiBase = settings.value(QLatin1String(kApiBaseKey), kDefaultApiBase).toString();
    m_apiKey = settings.value(QLatin1String(kApiKeyKey)).toString();
    m_model = settings.value(QLatin1String(kModelKey), kDefaultModel).toString();
    m_preset = settings.value(QLatin1String(kPresetKey), int(PresetDeepSeek)).toInt();
}

void AiImporter::store()
{
    QSettings settings;
    settings.setValue(QLatin1String(kApiBaseKey), m_apiBase);
    settings.setValue(QLatin1String(kApiKeyKey), m_apiKey);
    settings.setValue(QLatin1String(kModelKey), m_model);
}

void AiImporter::applyPreset(int index)
{
    if (index < PresetHunyuan || index > PresetDeepSeek)
        return;

    m_preset = index;
    switch (index) {
    case PresetHunyuan:
        // 腾讯云 TokenHub 的混元接入点。Hy4 是 2026-08 发布的旗舰（770B MoE），
        // 在混元那边的模型名就是 hy4-preview。
        setApiBase(QStringLiteral("https://tokenhub.tencentmaas.com/v1"));
        setModel(QStringLiteral("hy4-preview"));
        break;

    case PresetDeepSeek:
        setApiBase(QStringLiteral("https://api.deepseek.com"));
        setModel(QStringLiteral("deepseek-flash"));
        break;

    default:
        break;
    }

    QSettings settings;
    settings.setValue(QLatin1String(kPresetKey), m_preset);
    emit configChanged();
}

void AiImporter::setApiBase(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed == m_apiBase)
        return;
    m_apiBase = trimmed;
    store();
    emit configChanged();
}

void AiImporter::setApiKey(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed == m_apiKey)
        return;
    m_apiKey = trimmed;
    store();
    emit configChanged();
}

void AiImporter::setModel(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed == m_model)
        return;
    m_model = trimmed;
    store();
    emit configChanged();
}

void AiImporter::parse(const QString &timetableText)
{
    if (!configured()) {
        emit failed(tr("还没配接口。点右上角那个齿轮，把接口地址和 API Key 填上。"));
        return;
    }

    QString base = m_apiBase;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (!base.endsWith(QLatin1String("/chat/completions")))
        base += QStringLiteral("/chat/completions");

    QNetworkRequest request{QUrl(base)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), buildPrompt(timetableText));

    QJsonArray messages;
    messages.append(message);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0);
    body.insert(QStringLiteral("stream"), false);

    QNetworkReply *reply =
        m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        handleReply(reply);
    });
}

void AiImporter::handleReply(QNetworkReply *reply)
{
    const QByteArray raw = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(tr("请求没发出去：%1\n%2")
                        .arg(reply->errorString(),
                             QString::fromUtf8(raw.left(400))));
        return;
    }

    QJsonParseError parseError{};
    const QJsonObject root = QJsonDocument::fromJson(raw, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        emit failed(tr("接口返回的不是 JSON：\n%1")
                        .arg(QString::fromUtf8(raw.left(400))));
        return;
    }

    // 有的服务出错时也回 200，错误塞在 body 里
    if (root.contains(QStringLiteral("error"))) {
        const QString detail =
            root.value(QStringLiteral("error")).toObject()
                .value(QStringLiteral("message")).toString();
        emit failed(tr("接口报错：%1")
                        .arg(detail.isEmpty() ? QString::fromUtf8(raw.left(300))
                                              : detail));
        return;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit failed(tr("接口没返回内容：\n%1").arg(QString::fromUtf8(raw.left(400))));
        return;
    }

    const QString content =
        choices.first().toObject().value(QStringLiteral("message")).toObject()
            .value(QStringLiteral("content")).toString();

    // 模型可能把 JSON 裹在 ```json 里，或者前后多几句废话 ——
    // 直接把方括号那一坨抠出来，别指望它听话。
    const int begin = content.indexOf(QLatin1Char('['));
    const int end = content.lastIndexOf(QLatin1Char(']'));
    if (begin < 0 || end <= begin) {
        emit failed(tr("模型没按格式返回课程列表：\n%1").arg(content.left(400)));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(
        content.mid(begin, end - begin + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        emit failed(tr("模型返回的 JSON 读不了：%1").arg(parseError.errorString()));
        return;
    }

    QVariantList courses;
    int maxWeek = 0;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;

        QVariantList weeks;
        for (const QJsonValue &w : item.value(QStringLiteral("weeks")).toArray()) {
            const int n = w.toInt();
            if (n > 0) {
                weeks.append(n);
                maxWeek = qMax(maxWeek, n);
            }
        }
        if (weeks.isEmpty())
            continue;
        std::sort(weeks.begin(), weeks.end(),
                  [](const QVariant &a, const QVariant &b) {
                      return a.toInt() < b.toInt();
                  });

        const int start = qMax(1, item.value(QStringLiteral("startSection")).toInt(1));
        const int end =
            qMax(start, item.value(QStringLiteral("endSection")).toInt(start));

        QVariantMap course;
        course.insert(QStringLiteral("name"), name);
        course.insert(QStringLiteral("teacher"),
                      item.value(QStringLiteral("teacher")).toString());
        course.insert(QStringLiteral("room"),
                      item.value(QStringLiteral("room")).toString());
        course.insert(QStringLiteral("category"),
                      item.value(QStringLiteral("category")).toString());
        course.insert(QStringLiteral("day"),
                      qBound(1, item.value(QStringLiteral("day")).toInt(1), 7));
        // 模型给的是小节号（和 PDF 里一致），界面上一堂大课 = 两小节
        course.insert(QStringLiteral("startSection"), (start + 1) / 2);
        course.insert(QStringLiteral("endSection"), (end + 1) / 2);
        course.insert(QStringLiteral("weeks"), weeks);
        course.insert(QStringLiteral("weeksLabel"), labelFromWeeks(weeks));
        course.insert(QStringLiteral("startWeek"), weeks.first().toInt());
        course.insert(QStringLiteral("endWeek"), weeks.last().toInt());
        courses.append(course);
    }

    if (courses.isEmpty()) {
        emit failed(tr("模型返回的课程列表是空的。"));
        return;
    }

    emit succeeded(courses, maxWeek);
}
