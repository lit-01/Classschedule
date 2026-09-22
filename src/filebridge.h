#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

// 把 QML 文件选择器给来的 URL 变成「C++ 真正读得到的本地路径」。
//
// 桌面 / file:// 直接转本地路径；安卓上系统文件管理器（SAF）给的是 content://，
// QFileInfo 拿不到文件名、QFile 读不了字节 —— 得走 JNI 让 Java 层
// （FileBridge.java）用 ContentResolver 读出来，落到 AppData/attachments/ 里。
class FileBridge : public QObject
{
    Q_OBJECT

public:
    explicit FileBridge(QObject *parent = nullptr) : QObject(parent) {}

    // 给 QML 用：把选到的文件变成真能读的本地路径，失败返回空串
    Q_INVOKABLE QString toLocalFile(const QUrl &url);
    Q_INVOKABLE QStringList toLocalFiles(const QVariantList &urls);

    // 安卓上 Qt 的 FileDialog 结果回不来，改成自己发系统选择器。
    // purpose 只用来区分这次是给谁选的（"attach" / "top" / "table"），
    // 选完会原样跟着 filesPicked 信号回来。
    Q_INVOKABLE void startPick(const QString &purpose, bool image, bool multiple);

    // 回到前台时调一下：有选好的文件就发 filesPicked（然后清掉清单）
    Q_INVOKABLE void checkPicked();

    // 最近一次解析失败的说明（成功会清空）—— 直接显示给用户，省得瞎猜
    Q_INVOKABLE QString lastError() const;

signals:
    void filesPicked(const QString &purpose, const QStringList &paths);

private:
    QString m_purpose;
};

// 这两个是纯工具函数，故意放在类外面：静态成员函数不能当信号用，
// moc 会给它们生成用到 this 的调用代码，编不过。
QString fileBridgeDisplayName(const QUrl &url);
QString fileBridgeResolve(const QUrl &url, QString *err = nullptr);
