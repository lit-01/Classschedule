#include <QDebug>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>

#include "src/calendardata.h"
#include "src/coursemodel.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("pony"));
    QCoreApplication::setApplicationName(QStringLiteral("CourseTable"));

    // Qt 自带的控件文案（文件选择器的「主文件夹 / 打开 / 取消」这些）默认是英文，
    // 装了 qt_zh_CN.qm 就整成中文，跟界面是一套语言。
    QTranslator qtTr;
    if (qtTr.load(QLocale(QLocale::Chinese, QLocale::China), QStringLiteral("qt"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath))
        || qtTr.load(QStringLiteral("qt_zh_CN"),
                     QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
        QCoreApplication::installTranslator(&qtTr);

    QQuickStyle::setStyle(QStringLiteral("Material"));

    CourseModel courseModel;
    CalendarData calendarData;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("courseModel"), &courseModel);
    engine.rootContext()->setContextProperty(QStringLiteral("calendarData"), &calendarData);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("CourseTable", "Main");

    // 调界面用的后门：设了 COURSETABLE_SHOT 就渲染一张图然后退出，
    // 方便不开窗口也能看 UI 长什么样。
    //   COURSETABLE_SHOT_EDIT=1        → 先切到「改课」模式
    //   COURSETABLE_SHOT_DIALOG=1      → 先打开「添加课程」弹窗
    //   COURSETABLE_SHOT_ACTION=<id>   → 先按 objectName 找控件、调它的 open()
    const QByteArray shotPath = qgetenv("COURSETABLE_SHOT");
    if (!shotPath.isEmpty()) {
        const QByteArray shotEdit = qgetenv("COURSETABLE_SHOT_EDIT");
        const QByteArray shotDialog = qgetenv("COURSETABLE_SHOT_DIALOG");
        const QByteArray shotAction = qgetenv("COURSETABLE_SHOT_ACTION");

        // 截图模式下把窗口摆到屏幕靠中偏左：系统默认位置有可能跑到屏幕外面，
        // 那样 grabWindow() 还正常，但用外部工具按屏幕坐标截就什么都抓不到。
        // 往右挪一点是给「->」那个文件对话框留地方——它是按主窗口居中的，
        // 主窗口贴太左，对话框就会有一截挂在屏幕外，截出来缺一块。
        for (QObject *obj : engine.rootObjects()) {
            if (auto *win = qobject_cast<QQuickWindow *>(obj))
                win->setPosition(420, 60);
        }

        if (!shotEdit.isEmpty()) {
            QTimer::singleShot(900, &app, [&engine]() {
                const QList<QObject *> roots = engine.rootObjects();
                for (QObject *obj : roots)
                    obj->setProperty("editing", true);
            });
        }

        if (!shotDialog.isEmpty()) {
            QTimer::singleShot(1100, &app, [&engine]() {
                const QList<QObject *> roots = engine.rootObjects();
                for (QObject *obj : roots) {
                    QObject *editor = obj->findChild<QObject *>(QStringLiteral("courseEditor"));
                    if (!editor)
                        continue;
                    QMetaObject::invokeMethod(editor, "openNew",
                                              Q_ARG(QVariant, QVariant(1)),
                                              Q_ARG(QVariant, QVariant(1)));
                    break;
                }
            });
        }

        if (!shotAction.isEmpty()) {
            QTimer::singleShot(1100, &app, [&engine, shotAction]() {
                const QString name = QString::fromLocal8Bit(shotAction);
                const QList<QObject *> roots = engine.rootObjects();
                for (QObject *obj : roots) {
                    QObject *target = obj->findChild<QObject *>(name);
                    if (!target) {
                        qWarning() << "shot action: 找不到控件" << name;
                        continue;
                    }
                    const bool ok = QMetaObject::invokeMethod(target, "open");
                    const bool vis = target->setProperty("visible", true);
                    qWarning() << "shot action: 调用" << name
                               << "open() ->" << ok << "visible ->" << vis;
                }
            });
        }

        // 抓图前等多久。默认 2400ms；外部工具要在旁边同时按屏幕坐标截别的窗口时，
        // 用 COURSETABLE_SHOT_DELAY 把它拉长，留出操作余地。
        int shotDelay = QByteArray(qgetenv("COURSETABLE_SHOT_DELAY")).toInt();
        if (shotDelay <= 0)
            shotDelay = 2400;

        QTimer::singleShot(shotDelay, &app, [&engine, shotPath]() {
            const QList<QObject *> roots = engine.rootObjects();
            for (QObject *obj : roots) {
                if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
                    win->grabWindow().save(QString::fromLocal8Bit(shotPath));
                    break;
                }
            }
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
