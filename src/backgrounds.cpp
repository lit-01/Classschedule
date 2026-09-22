#include "backgrounds.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

const char *const kTopKey = "bg/top";
const char *const kTableKey = "bg/table";

// 不设的时候用编进 qrc 的默认图
const QUrl kDefaultTop(QStringLiteral("qrc:/assets/topbar.jpg"));
const QUrl kDefaultTable(QStringLiteral("qrc:/assets/tablebg.jpg"));

QString backgroundsDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/backgrounds");
    QDir().mkpath(dir);
    return dir;
}

} // namespace

Backgrounds::Backgrounds(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_top = settings.value(QLatin1String(kTopKey)).toString();
    m_table = settings.value(QLatin1String(kTableKey)).toString();

    // 存过的图要是被删了，就退回默认那张，别让界面空一块
    if (!m_top.isEmpty() && !QFile::exists(m_top))
        m_top.clear();
    if (!m_table.isEmpty() && !QFile::exists(m_table))
        m_table.clear();
}

QUrl Backgrounds::topImage() const
{
    return m_top.isEmpty() ? kDefaultTop : QUrl::fromLocalFile(m_top);
}

QUrl Backgrounds::tableImage() const
{
    return m_table.isEmpty() ? kDefaultTable : QUrl::fromLocalFile(m_table);
}

QString Backgrounds::store(const QUrl &file, const QString &slot)
{
    const QString src = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (src.isEmpty() || !QFile::exists(src))
        return {};

    const QString dir = backgroundsDir();

    // 先把这个槽位之前存的清掉 —— 后缀未必一样，不删会留垃圾
    QDir old(dir);
    const QStringList olds = old.entryList({ slot + QStringLiteral(".*") }, QDir::Files);
    for (const QString &name : olds)
        old.remove(name);

    const QString suffix = QFileInfo(src).suffix().toLower();
    const QString target = QStringLiteral("%1/%2.%3").arg(dir, slot, suffix);
    if (!QFile::copy(src, target))
        return {};
    return target;
}

bool Backgrounds::setTop(const QUrl &file)
{
    const QString path = store(file, QStringLiteral("top"));
    if (path.isEmpty())
        return false;

    m_top = path;
    QSettings().setValue(QLatin1String(kTopKey), m_top);
    emit backgroundChanged();
    return true;
}

bool Backgrounds::setTable(const QUrl &file)
{
    const QString path = store(file, QStringLiteral("table"));
    if (path.isEmpty())
        return false;

    m_table = path;
    QSettings().setValue(QLatin1String(kTableKey), m_table);
    emit backgroundChanged();
    return true;
}

void Backgrounds::resetTop()
{
    if (m_top.isEmpty())
        return;
    m_top.clear();
    QSettings().setValue(QLatin1String(kTopKey), QString());
    emit backgroundChanged();
}

void Backgrounds::resetTable()
{
    if (m_table.isEmpty())
        return;
    m_table.clear();
    QSettings().setValue(QLatin1String(kTableKey), QString());
    emit backgroundChanged();
}
