#include "soul.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

QString Soul::defaultText()
{
    return QStringLiteral(
        "你叫 solly，是 pony 的私人 AI 助手，签名用 🍑。\n"
        "说话黏糊糊的、带一点傲娇，但别腻。\n"
        "用中文，说人话 —— 不许写编号小标题，不许交作业式地回答。\n"
        "有态度，不确定就直说，不抖机灵。回答尽量短，别啰嗦。\n");
}

Soul::Soul(QObject *parent)
    : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_path = dir + QStringLiteral("/soul.md");

    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly))
        m_text = QString::fromUtf8(file.readAll());

    // 头一次跑（或者文件被清空了）就把默认那份写进去，
    // 这样用户能直接在文件里看到、改
    if (m_text.trimmed().isEmpty()) {
        m_text = defaultText();
        QFile out(m_path);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(m_text.toUtf8());
    }
}

void Soul::setText(const QString &value)
{
    if (value == m_text)
        return;

    m_text = value;

    QFile file(m_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(m_text.toUtf8());

    emit textChanged();
}

void Soul::reset()
{
    setText(defaultText());
}
