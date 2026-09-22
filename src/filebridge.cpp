#include "filebridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace {

// 最近一次解析失败的原因，QML 那边直接显示它，省得瞎猜
QString g_lastError;

#ifdef Q_OS_ANDROID
QString attachmentsDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/attachments");
    QDir().mkpath(dir);
    return dir;
}
#endif

} // namespace

QString FileBridge::toLocalFile(const QUrl &url)
{
    return fileBridgeResolve(url);
}

// 最近一次失败的说明（成功会清空）
QString FileBridge::lastError() const
{
    return g_lastError;
}

void FileBridge::startPick(const QString &purpose, bool image, bool multiple)
{
    m_purpose = purpose;
#ifdef Q_OS_ANDROID
    auto ctx = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "com/pony/coursetable/Picker",
        "start",
        "(Landroid/app/Activity;ZZ)V",
        ctx.object<jobject>(),
        static_cast<jboolean>(image),
        static_cast<jboolean>(multiple));
#endif
}

void FileBridge::checkPicked()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty())
        return;
    QFile file(dir + QStringLiteral("/picked.txt"));
    if (!file.exists())
        return;

    QStringList paths;
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.isEmpty())
                paths.append(line);
        }
    }
    file.remove();

    const QString purpose = m_purpose;
    m_purpose.clear();
    if (!paths.isEmpty()) {
        g_lastError.clear();
        emit filesPicked(purpose, paths);
    }
}

QStringList FileBridge::toLocalFiles(const QVariantList &urls)
{
    QStringList out;
    for (const QVariant &v : urls) {
        const QString local = fileBridgeResolve(QUrl(v.toString()));
        if (!local.isEmpty())
            out.append(local);
    }
    return out;
}

QString fileBridgeDisplayName(const QUrl &url)
{
    if (url.isLocalFile())
        return QFileInfo(url.toLocalFile()).fileName();

#ifdef Q_OS_ANDROID
    if (url.scheme() == QLatin1String("content")) {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        const QJniObject name = QJniObject::callStaticObjectMethod(
            "com/pony/coursetable/FileBridge",
            "displayName",
            "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            ctx.object<jobject>(),
            QJniObject::fromString(url.toString()).object<jstring>());
        return name.isValid() ? name.toString() : QString();
    }
#endif

    // file:///somewhere/a.png 这类，直接从 URL 里抠
    return QFileInfo(url.toString()).fileName();
}

QString fileBridgeResolve(const QUrl &url, QString *err)
{
    if (err)
        err->clear();

    if (url.isLocalFile()) {
        const QString p = url.toLocalFile();
        if (!QFile::exists(p)) {
            const QString why = QStringLiteral("文件不存在：%1").arg(p);
            g_lastError = why;
            if (err)
                *err = why;
            return {};
        }
        g_lastError.clear();
        return p;
    }

    const QString raw = url.toString();
    if (url.scheme() != QLatin1String("content")) {
        if (QFile::exists(raw)) {
            g_lastError.clear();
            return raw;
        }
        const QString why = QStringLiteral("读不了这个路径（%1）").arg(raw.left(90));
        g_lastError = why;
        if (err)
            *err = why;
        return {};
    }

    // content:// 是安卓系统文件管理器给的，Qt 和 Java 两条路都试一遍
    QByteArray bytes;

    // ① Qt 自己带没带 content 文件引擎？有的话直接读
    {
        QFile f(raw);
        if (f.open(QIODevice::ReadOnly))
            bytes = f.readAll();
    }

    // ② 不行就走 JNI，让 Java 的 ContentResolver 读
#ifdef Q_OS_ANDROID
    if (bytes.isEmpty()) {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        const QJniObject b64 = QJniObject::callStaticObjectMethod(
            "com/pony/coursetable/FileBridge",
            "readBase64",
            "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            ctx.object<jobject>(),
            QJniObject::fromString(raw).object<jstring>());
        const QString got = b64.isValid() ? b64.toString() : QString();
        if (got.startsWith(QLatin1String("ERR:"))) {
            // Java 那边把为什么失败写回来了
            const QString why = QStringLiteral("（%1）").arg(got.mid(4));
            g_lastError = why;
            if (err)
                *err = why;
            return {};
        }
        if (!got.isEmpty())
            bytes = QByteArray::fromBase64(got.toUtf8());
    }
#endif

    if (bytes.isEmpty()) {
        const QString why = QStringLiteral("系统没把文件给过来（%1）").arg(raw.left(90));
        g_lastError = why;
        if (err)
            *err = why;
        return {};
    }

    QString fname = fileBridgeDisplayName(url);
    if (fname.isEmpty())
        fname = QUrl(raw).fileName();
    if (fname.isEmpty())
        fname = QStringLiteral("attachment");
    static const QRegularExpression bad("[\\\\/:*?\"<>|]");
    fname.remove(bad);

#ifndef Q_OS_ANDROID
    // 非安卓平台没有 content 文件引擎，落盘那步也用不了
    Q_UNUSED(bytes)
    const QString why = QStringLiteral("这个平台读不了 content:// 路径");
    g_lastError = why;
    if (err)
        *err = why;
    return {};
#else
    const QString target = attachmentsDir() + QLatin1Char('/') + fname;
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || f.write(bytes) != bytes.size()) {
        const QString why = QStringLiteral("附件没存下来");
        g_lastError = why;
        if (err)
            *err = why;
        return {};
    }
    g_lastError.clear();
    return target;
#endif
}
