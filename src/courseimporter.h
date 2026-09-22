#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

class AiImporter;
class CourseModel;

// 导课：把选中的课表 PDF 解析出来塞进模型。
//
// 单独拎成一个对象是为了让 QML 直接调：
//     FileDialog { onAccepted: courseImporter.importFile(selectedFile) }
// 结果通过 finished 信号回。
//
// 两条通道：**本地解析器优先**（快、准、不花钱），本地认不出来才把文字丢给 AI
// 再试一次（前提是用户配了 API Key）。AI 那步是异步的，所以 importFile 返回
// true 只表示「受理了」，最终成没成看 finished。
class CourseImporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY finished)
    Q_PROPERTY(int lastCount READ lastCount NOTIFY finished)
    Q_PROPERTY(int lastMaxWeek READ lastMaxWeek NOTIFY finished)
    Q_PROPERTY(bool lastUsedAi READ lastUsedAi NOTIFY finished)

public:
    explicit CourseImporter(CourseModel *model, AiImporter *ai,
                            QObject *parent = nullptr);

    Q_INVOKABLE bool importFile(const QUrl &fileUrl);

    QString lastError() const { return m_lastError; }
    int lastCount() const { return m_lastCount; }
    int lastMaxWeek() const { return m_lastMaxWeek; }
    bool lastUsedAi() const { return m_lastUsedAi; }

signals:
    void finished(bool ok);

private:
    void applyCourses(const QVariantList &courses, int maxWeek, bool fromAi);

    CourseModel *m_model = nullptr;
    AiImporter *m_ai = nullptr;
    QString m_lastError;
    QString m_localError; // 只让本地解析器报的错，配上 AI 的失败原因一起说
    int m_lastCount = 0;
    int m_lastMaxWeek = 0;
    bool m_lastUsedAi = false;
};
