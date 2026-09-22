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
    // 设置失败时给一句人话（读不了图 / 选区太小 / 落盘失败），成功后清空
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit Backgrounds(QObject *parent = nullptr);

    QUrl topImage() const;
    QUrl tableImage() const;
    QString lastError() const { return m_lastError; }

    // 挑一张换上去，成功返回 true
    Q_INVOKABLE bool setTop(const QUrl &file);
    Q_INVOKABLE bool setTable(const QUrl &file);

    // 选图 → 用户在裁剪层框一块 → 把那块裁下来设为背景。
    // 归一化坐标（0~1，相对原图）。kind: "top" 顶部横幅 / "table" 课表底图
    Q_INVOKABLE bool cropAndSet(const QString &kind, const QUrl &source,
                                qreal nx, qreal ny, qreal nw, qreal nh);

    Q_INVOKABLE void resetTop();
    Q_INVOKABLE void resetTable();

signals:
    // 名字不能叫 changed() —— QObject 里已经有一个了（信号重载会打架）
    void backgroundChanged();
    void lastErrorChanged();

private:
    QString store(const QUrl &file, const QString &slot);

    QString m_top;
    QString m_table;
    QString m_lastError;
};
