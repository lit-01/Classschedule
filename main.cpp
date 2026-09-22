#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>
#include <QUrl>

#include "src/aichat.h"
#include "src/aiimporter.h"
#include "src/backgrounds.h"
#include "src/calendardata.h"
#include "src/courseimporter.h"
#include "src/coursemodel.h"
#include "src/filebridge.h"
#include "src/soul.h"
#include "src/timetableimport.h"

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

    // 调 PDF 解析用的后门：设了 COURSETABLE_PDF 就只跑解析、把提取结果写进
    // COURSETABLE_PDF_OUT 指定的文件，然后退出。不用点界面也能核对解析对不对。
    const QByteArray pdfPath = qgetenv("COURSETABLE_PDF");
    if (!pdfPath.isEmpty()) {
        QString report;
        QFile in(QString::fromLocal8Bit(pdfPath));
        if (!in.open(QIODevice::ReadOnly)) {
            report = QStringLiteral("打不开 %1\n").arg(QString::fromLocal8Bit(pdfPath));
        } else {
            const ImportResult r = TimetableImporter::importPdf(in.readAll());
            if (!r.ok()) {
                report += QStringLiteral("错误：%1\n").arg(r.error);
            } else {
                static const char *const kDayNames[] = {
                    "", "周一", "周二", "周三", "周四", "周五", "周六", "周日"};
                report += QStringLiteral("共 %1 门/次，最大周次 %2\n\n")
                              .arg(r.courses.size())
                              .arg(r.maxWeek);
                for (const ImportedCourse &c : r.courses) {
                    report += QStringLiteral(
                                  "%1  第%2大节(%3-%4节)  %5  %6  %7  %8\n")
                                  .arg(QString::fromUtf8(kDayNames[c.day]))
                                  .arg(c.startBlock())
                                  .arg(c.startSection)
                                  .arg(c.endSection)
                                  .arg(c.weeksLabel, -14)
                                  .arg(c.name, -18)
                                  .arg(c.room, -24)
                                  .arg(c.teacher);
                }
            }
        }

        const QByteArray outPath = qgetenv("COURSETABLE_PDF_OUT");
        QFile out(outPath.isEmpty() ? QByteArray("pdf_probe.txt") : outPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(report.toUtf8());
        return 0;
    }

    CourseModel courseModel;
    CalendarData calendarData;
    Backgrounds backgrounds;
    FileBridge fileBridge;
    AiImporter aiImporter;
    Soul soul;
    AiChat aiChat(&aiImporter, &courseModel, &soul, &calendarData);
    CourseImporter courseImporter(&courseModel, &aiImporter);

    // 调试用：设了 COURSETABLE_AUTOIMPORT 就直接把这份 PDF 导进来，省得每次
    // 点界面选文件。配合 COURSETABLE_SHOT 就能直接截到「导入后」的课表。
    const QByteArray autoImport = qgetenv("COURSETABLE_AUTOIMPORT");
    if (!autoImport.isEmpty()) {
        courseImporter.importFile(
            QUrl::fromLocalFile(QString::fromLocal8Bit(autoImport)));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("courseModel"), &courseModel);
    engine.rootContext()->setContextProperty(QStringLiteral("calendarData"), &calendarData);
    engine.rootContext()->setContextProperty(QStringLiteral("courseImporter"),
                                             &courseImporter);
    engine.rootContext()->setContextProperty(QStringLiteral("aiImporter"), &aiImporter);
    engine.rootContext()->setContextProperty(QStringLiteral("aiChat"), &aiChat);
    engine.rootContext()->setContextProperty(QStringLiteral("backgrounds"), &backgrounds);
    engine.rootContext()->setContextProperty(QStringLiteral("soul"), &soul);
    // 安卓的系统文件管理器给的是 content://，QML 的 Image 也读不了，
    // 所以 QML 那边拿到文件先过它一下变成真本地路径
    engine.rootContext()->setContextProperty(QStringLiteral("fileBridge"), &fileBridge);

    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &fileBridge, [&fileBridge](Qt::ApplicationState state) {
                         // 从系统文件选择器回来时，把选好的文件收进来
                         if (state == Qt::ApplicationActive)
                             fileBridge.checkPicked();
                     });

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
            // 支持逗号分隔的多个 objectName，前一个留 350ms 落定再动下一个，
            // 比如 "aiSettings,providerBox" = 先开设置弹窗、再展开它的下拉
            const QStringList names = QString::fromLocal8Bit(shotAction)
                                          .split(QLatin1Char(','), Qt::SkipEmptyParts);
            int when = 1100;
            for (const QString &name : names) {
                QTimer::singleShot(when, &app, [&engine, name]() {
                    const QList<QObject *> roots = engine.rootObjects();
                    for (QObject *obj : roots) {
                        QObject *target = obj->findChild<QObject *>(name);
                        if (!target) {
                            qWarning() << "shot action: 找不到控件" << name;
                            continue;
                        }
                        // ComboBox 这类控件自己没有 open()，直接调会刷一条
                        // "No such method" 警告，所以先问一下有没有这个方法
                        bool ok = target->metaObject()->indexOfMethod("open()") >= 0
                                  && QMetaObject::invokeMethod(target, "open");
                        if (!ok) {
                            if (QObject *popup =
                                    target->property("popup").value<QObject *>()) {
                                ok = QMetaObject::invokeMethod(popup, "open");
                            }
                        }
                        const bool vis = target->setProperty("visible", true);
                        qWarning() << "shot action: 调用" << name
                                   << "open() ->" << ok << "visible ->" << vis;
                    }
                });
                when += 350;
            }
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

    // 调试：直接切到对话页、自动发一句，用来验「发送 → 收回复 → 渲染气泡」
    // 这条链路（配 COURSETABLE_SHOT 就能把结果截下来）
    const QByteArray chatTest = qgetenv("COURSETABLE_CHAT_TEST");
    if (!chatTest.isEmpty()) {
        QTimer::singleShot(1300, &app, [&engine, &aiChat, chatTest]() {
            for (QObject *obj : engine.rootObjects())
                obj->setProperty("showChat", true);
            aiChat.send(QString::fromLocal8Bit(chatTest));
        });
    }

    return app.exec();
}
