#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

// 主页那两张背景图（顶部横幅、课表底图）的管理。
//
// 用户选了新图会**先拷一份到 AppData 再记路径** —— 不直接引用原文件，
// 免得他哪天把原图挪走或者删了，界面就白了。不设就用编进 qrc 的默认图。
class Backgrounds : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl topImage READ topImage NOTIFY backgroundChanged)
    Q_PROPERTY(QUrl tableImage READ tableImage NOTIFY backgroundChanged)

public:
    explicit Backgrounds(QObject *parent = nullptr);

    QUrl topImage() const;
    QUrl tableImage() const;

    // 挑一张换上去，成功返回 true
    Q_INVOKABLE bool setTop(const QUrl &file);
    Q_INVOKABLE bool setTable(const QUrl &file);

    Q_INVOKABLE void resetTop();
    Q_INVOKABLE void resetTable();

signals:
    // 名字不能叫 changed() —— QObject 里已经有一个了（信号重载会打架）
    void backgroundChanged();

private:
    QString store(const QUrl &file, const QString &slot);

    QString m_top;
    QString m_table;
};
