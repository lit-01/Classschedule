#pragma once

#include <QObject>
#include <QString>

// AI 的性格设定。跟 WorkBuddy 那份 `SOUL.md` 是一个意思 ——
// 它就是一段提示词，决定 AI 说话什么调调，跟用哪家模型无关。
//
// 存在 `AppData/soul.md`，用户可以直接改（设置里有个编辑框）；
// 改坏了可以一键恢复默认。第一次跑会写一份默认的进去。
class Soul : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    explicit Soul(QObject *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &value);

    // 文件在哪（给「想知道存哪儿」的人看）
    QString path() const { return m_path; }

    // 内置的默认人格
    static QString defaultText();

    Q_INVOKABLE void reset();

signals:
    void textChanged();

private:
    QString m_path;
    QString m_text;
};
