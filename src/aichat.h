#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

class AiImporter;
class CalendarData;
class CourseModel;
class Soul;
class QNetworkAccessManager;
class QNetworkReply;

// AI 对话。用的就是「AI 设置」里那一套配置，不另存一份 ——
// 所以选了 Soul，对话就带 solly 的人格；选别的就是普通助手。
class AiChat : public QObject
{
    Q_OBJECT
    // [{role: "user"/"assistant", content: "…", isUser: bool}, …]
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY messagesChanged)
    // AI 提议改「角色」时先把新内容放这儿，等用户在界面上点同意
    Q_PROPERTY(QString pendingRole READ pendingRole NOTIFY roleChangeRequested)

public:
    AiChat(AiImporter *config, CourseModel *courses, Soul *soul,
           CalendarData *calendar = nullptr, QObject *parent = nullptr);

    QVariantList messages() const { return m_messages; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void send(const QString &text);
    // 带附件发：PDF 先本地提取文字、图片按 base64 原图发给模型识别
    Q_INVOKABLE void send(const QString &text, const QStringList &filePaths);
    Q_INVOKABLE void clear();

    QString pendingRole() const { return m_pendingRole; }

    // AI 没权限自己改角色设定 —— 它只能提议，用户点了同意才落地
    Q_INVOKABLE void acceptRoleChange();
    Q_INVOKABLE void rejectRoleChange();

signals:
    void messagesChanged();
    // AI 提了改角色的想法，界面该弹框问用户了
    void roleChangeRequested();

private:
    void append(const QString &role, const QString &content,
                const QString &reasoning = QString(),
                const QVariant &parts = QVariant());
    void handleReply(QNetworkReply *reply);
    // 把 m_messages 组装成请求发出去（send 的两个重载都靠它）
    void postRequest();
    QString systemPrompt() const;
    QString timetableContext() const;
    // 今天几号、星期几、现在第几周 —— 不然 AI 根本不知道「今天」是哪天
    QString dateContext() const;
    // 把回复里的改课 / 改角色 JSON 抠出来处理，返回「去掉那段 JSON」的正文
    QString applyOps(const QString &reply);

    AiImporter *m_config = nullptr;
    CourseModel *m_courses = nullptr;
    Soul *m_soul = nullptr;
    CalendarData *m_calendar = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QVariantList m_messages;
    QString m_pendingRole;
    bool m_busy = false;
};
