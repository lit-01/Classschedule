#include "courseimporter.h"

#include "aiimporter.h"
#include "coursemodel.h"
#include "timetableimport.h"

#include <QFile>
#include <QVariantList>
#include <QVariantMap>

namespace {

// ImportedCourse → QVariantMap（CourseModel::replaceAll 认的格式）
QVariantList toVariantList(const QList<ImportedCourse> &courses)
{
    QVariantList list;
    list.reserve(courses.size());
    for (const ImportedCourse &c : courses) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), c.name);
        m.insert(QStringLiteral("teacher"), c.teacher);
        m.insert(QStringLiteral("room"), c.room);
        m.insert(QStringLiteral("category"), c.category);
        m.insert(QStringLiteral("day"), c.day);
        // PDF 里写的是小节（1-10 节），界面上一天只有 5 个大节
        m.insert(QStringLiteral("startSection"), c.startBlock());
        m.insert(QStringLiteral("endSection"), c.endBlock());

        QVariantList weeks;
        for (int w : c.weeks)
            weeks.append(w);
        m.insert(QStringLiteral("weeks"), weeks);
        m.insert(QStringLiteral("weeksLabel"), c.weeksLabel);

        // 起止周也填上，用户在「改课」里打开这门课时表单才有初始值
        m.insert(QStringLiteral("startWeek"),
                 c.weeks.isEmpty() ? 0 : c.weeks.first());
        m.insert(QStringLiteral("endWeek"),
                 c.weeks.isEmpty() ? 0 : c.weeks.last());
        list.append(m);
    }
    return list;
}

} // namespace

CourseImporter::CourseImporter(CourseModel *model, AiImporter *ai, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_ai(ai)
{
    if (!m_ai)
        return;

    connect(m_ai, &AiImporter::succeeded, this,
            [this](const QVariantList &courses, int maxWeek) {
                applyCourses(courses, maxWeek, true);
                emit finished(true);
            });

    connect(m_ai, &AiImporter::failed, this, [this](const QString &reason) {
        m_lastError = tr("这份课表本地认不出来：\n%1\n\n丢给 AI 也没成：\n%2")
                          .arg(m_localError, reason);
        emit finished(false);
    });
}

void CourseImporter::applyCourses(const QVariantList &courses, int maxWeek, bool fromAi)
{
    m_model->replaceAll(courses);
    m_lastCount = static_cast<int>(courses.size());
    m_lastMaxWeek = maxWeek;
    m_lastUsedAi = fromAi;
}

bool CourseImporter::importFile(const QUrl &fileUrl)
{
    m_lastError.clear();
    m_localError.clear();
    m_lastCount = 0;
    m_lastMaxWeek = 0;
    m_lastUsedAi = false;

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile()
                                               : fileUrl.toString();
    const QString lower = path.toLower();

    // 图片：本地没法解析，直接丢给支持视觉的模型识别
    if (lower.endsWith(QStringLiteral(".png")) || lower.endsWith(QStringLiteral(".jpg"))
        || lower.endsWith(QStringLiteral(".jpeg")) || lower.endsWith(QStringLiteral(".bmp"))
        || lower.endsWith(QStringLiteral(".webp")) || lower.endsWith(QStringLiteral(".gif"))) {
        if (!m_ai || !m_ai->configured()) {
            m_lastError =
                tr("图片导入要 AI 识别，请先长按 AI 按钮把 Key 填上。");
            emit finished(false);
            return false;
        }
        const QString dataUrl = AiImporter::imageToDataUrl(path);
        if (dataUrl.isEmpty()) {
            m_lastError = tr("这张图片读不出来（格式不支持或损坏）。");
            emit finished(false);
            return false;
        }
        m_ai->parseImage(AiImporter::importPrompt(true), {dataUrl});
        return true; // 异步，结果稍后从 finished 回来
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = tr("打不开这个文件：\n%1").arg(path);
        emit finished(false);
        return false;
    }

    const QByteArray bytes = file.readAll();

    // 先用本地解析器 —— 快、准、不花钱。
    // COURSETABLE_FORCE_AI 是调试开关：跳过本地、直接走 AI，用来验证那条链路。
    if (qgetenv("COURSETABLE_FORCE_AI").isEmpty()) {
        const ImportResult parsed = TimetableImporter::importPdf(bytes);
        if (parsed.ok()) {
            applyCourses(toVariantList(parsed.courses), parsed.maxWeek, false);
            emit finished(true);
            return true;
        }
        m_localError = parsed.error;
    } else {
        m_localError = tr("（调试开关：跳过了本地解析）");
    }

    // 本地认不出来：配了 AI 就让它再试一次。
    // 这步是异步的，结果稍后才从信号回来，所以这里先「受理」。
    if (m_ai && m_ai->configured()) {
        QString err;
        const QString text = TimetableImporter::extractTimetableText(bytes, &err);
        if (!text.isEmpty()) {
            m_ai->parse(text);
            return true;
        }
        m_localError += QLatin1Char('\n') + err;
    }

    m_lastError = m_localError;
    emit finished(false);
    return false;
}
