#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

// 「AI 解析」这条备用通道：把从 PDF 里抠出来的文字丢给大模型，让它吐 JSON。
//
// 走的是 OpenAI 那套 /chat/completions 格式 —— DeepSeek、通义千问、智谱、
// Moonshot、本地 Ollama 都兼容。所以地址 / Key / 模型名全让用户自己填，
// 不绑死任何一家。
//
// 配置走 QSettings（注册表 HKCU\Software\pony\CourseTable），**不写进项目目录**，
// 所以不会跟着代码一起推上 GitHub。
class AiImporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString apiBase READ apiBase WRITE setApiBase NOTIFY configChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY configChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY configChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(int preset READ preset NOTIFY configChanged)

public:
    // 预设的服务商。
    //
    // 第一项是混元而不是 WorkBuddy —— WorkBuddy 自己不对外提供 API
    // （本机那几个 127.0.0.1 端口只是它自己的内部通信），它背后用的就是
    // 腾讯混元，Hy4 也是先在 WorkBuddy 上线的，所以直接接混元的接入点。
    enum Preset {
        PresetHunyuan = 0,  // 腾讯混元 Hy4
        PresetDeepSeek = 1, // DeepSeek Flash
    };
    Q_ENUM(Preset)

    explicit AiImporter(QObject *parent = nullptr);

    QString apiBase() const { return m_apiBase; }
    void setApiBase(const QString &value);

    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &value);

    QString model() const { return m_model; }
    void setModel(const QString &value);

    // 填了 Key 就算配好了
    bool configured() const { return !m_apiKey.trimmed().isEmpty(); }

    // 当前选中哪个预设。存下来的，不靠接口地址反推。
    int preset() const { return m_preset; }

    // 套用预设：填好接口地址和模型。Key 不动 —— 那个只能用户自己填。
    Q_INVOKABLE void applyPreset(int index);

    // 把整理好的课表文字交给模型。结果异步走 succeeded / failed。
    void parse(const QString &timetableText);

signals:
    void configChanged();
    // courses 里每个 map 的字段和 CourseModel::replaceAll 要的一致
    void succeeded(const QVariantList &courses, int maxWeek);
    void failed(const QString &reason);

private:
    void store();
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager *m_net = nullptr;
    QString m_apiBase;
    QString m_apiKey;
    QString m_model;
    int m_preset = 1; // PresetDeepSeek
};
