#include "backgrounds.h"

#include "filebridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
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

bool Backgrounds::cropAndSet(const QString &kind, const QUrl &source,
                             qreal nx, qreal ny, qreal nw, qreal nh)
{
    const auto fail = [this](const QString &why) {
        m_lastError = why;
        emit lastErrorChanged();
        return false;
    };

    QString err;
    const QString src = fileBridgeResolve(source, &err);
    if (src.isEmpty())
        return fail(err.isEmpty() ? QStringLiteral("这个文件读不了") : err);

    QImage img(src);
    if (img.isNull())
        return fail(QStringLiteral("图片读不出来"));

    // 选区是相对原图的归一化坐标，换算回像素并夹到图内
    QRect r(qRound(nx * img.width()), qRound(ny * img.height()),
            qRound(nw * img.width()), qRound(nh * img.height()));
    r = r.intersected(img.rect());
    if (r.width() < 8 || r.height() < 8)
        return fail(QStringLiteral("选的区域太小了"));

    const QImage cropped = (r == img.rect()) ? img : img.copy(r);

    // 先落一份临时文件，再走 store() 归档（它按槽位清旧图、按后缀存新图）
    const QString tmp = backgroundsDir() + QStringLiteral("/_crop_tmp.png");
    if (!cropped.save(tmp, "PNG"))
        return fail(QStringLiteral("裁剪结果没存下来"));

    const QString slot = (kind == QLatin1String("top")) ? QStringLiteral("top")
                                                        : QStringLiteral("table");
    const QString path = store(QUrl::fromLocalFile(tmp), slot);
    QFile::remove(tmp);
    if (path.isEmpty())
        return fail(QStringLiteral("背景图没存下来"));

    if (slot == QLatin1String("top")) {
        m_top = path;
        QSettings().setValue(QLatin1String(kTopKey), m_top);
    } else {
        m_table = path;
        QSettings().setValue(QLatin1String(kTableKey), m_table);
    }
    m_lastError.clear();
    emit lastErrorChanged();
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
