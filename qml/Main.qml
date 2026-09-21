import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 400
    height: 780
    visible: true
    title: qsTr("课表")

    Material.theme: Material.Light
    Material.accent: "#3F51B5"
    Material.primary: "#3F51B5"

    property int currentWeek: 1
    property int totalWeeks: 24

    // 一天 5 个大节（一次课 = 两小节），行数由模型给，别在这儿写死
    readonly property int maxSections: courseModel.maxSections

    // 「改课」模式：打开后点格子才能改课 / 加课
    property bool editing: false

    // 学期起始日一变就 +1，用来把表头日期、年月重算一遍
    property int dateRevision: 0

    readonly property color barColor: "#3F51B5"

    Connections {
        target: calendarData
        function onFirstMondayChanged() { root.dateRevision++ }
    }

    component NavButton: Rectangle {
        id: nav

        property string label: ""
        property bool dimmed: false
        property bool active: false
        property int labelSize: 26
        signal tapped()

        implicitWidth: 44
        implicitHeight: 32
        radius: 16
        color: nav.active ? "white" : "#33FFFFFF"
        border.width: 1
        border.color: nav.active ? "transparent" : "#80FFFFFF"

        Text {
            anchors.centerIn: parent
            text: nav.label
            color: nav.active ? root.barColor : (nav.dimmed ? "#99FFFFFF" : "white")
            font.pixelSize: nav.labelSize
            font.bold: true
            // 背景是一张图，描个边保证白字在亮处也看得清
            style: Text.Outline
            styleColor: "#66000000"
        }

        TapHandler {
            onTapped: nav.tapped()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ========== 顶部：横幅 + 周次 / 年月 / 改课开关 ==========
        Item {
            id: topBar

            Layout.fillWidth: true
            // 高度照 topbar.jpg 的原始比例走（1920 × 803），
            // 窗口多宽，横幅就多高，图永远不会被拉变形。
            implicitHeight: Math.round(width * 803 / 1920)

            Image {
                anchors.fill: parent
                source: "qrc:/assets/topbar.jpg"
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }

            // 左边压一层深色渐变：白字落在人物那半边要有底，
            // 右边留给天空，压得太狠图就脏了。
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#59000000" }
                    GradientStop { position: 0.5; color: "#26000000" }
                    GradientStop { position: 1.0; color: "#1A000000" }
                }
            }

            // 周次导航 + 年月：贴横幅左下，排成一行
            RowLayout {
                id: weekRow

                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.bottomMargin: 8
                spacing: 4

                NavButton {
                    label: "\u2039"
                    dimmed: root.currentWeek <= 1
                    onTapped: {
                        if (root.currentWeek > 1)
                            root.currentWeek--
                    }
                }

                Label {
                    text: qsTr("第 %1 周").arg(root.currentWeek)
                    color: "white"
                    font.pixelSize: 17
                    font.bold: true
                    style: Text.Outline
                    styleColor: "#80000000"
                }

                NavButton {
                    label: "\u203A"
                    dimmed: root.currentWeek >= root.totalWeeks
                    onTapped: {
                        if (root.currentWeek < root.totalWeeks)
                            root.currentWeek++
                    }
                }

                // 导课：挑一个课表 PDF
                NavButton {
                    label: "->"
                    labelSize: 16
                    Layout.leftMargin: 14
                    onTapped: importDialog.open()
                }
            }

            // 年月压在周次头上：年一行、月一行
            // （学期起始日是从导入的 PDF 里读的，这里只显示）
            Column {
                id: monthBlock

                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.bottom: weekRow.top
                anchors.bottomMargin: 4
                spacing: 0

                Text {
                    text: {
                        root.dateRevision
                        return calendarData.yearLabel(root.currentWeek)
                    }
                    color: "white"
                    font.pixelSize: 17 // 跟「第 N 周」一样大
                    font.bold: true
                    style: Text.Outline
                    styleColor: "#80000000"
                }

                Text {
                    text: {
                        root.dateRevision
                        return calendarData.monthLabel(root.currentWeek)
                    }
                    color: "#E6FFFFFF"
                    font.pixelSize: 12 // 月小一号
                    style: Text.Outline
                    styleColor: "#80000000"
                }
            }

            // 改课开关：贴横幅右下
            NavButton {
                label: "+/-"
                labelSize: 14
                active: root.editing
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 8
                anchors.bottomMargin: 8
                onTapped: root.editing = !root.editing
            }
        }

        // ========== 课表网格 ==========
        CourseGrid {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentWeek: root.currentWeek
            maxSections: root.maxSections
            editable: root.editing
            revision: root.dateRevision

            onCourseTapped: function (courseId) { editor.openEdit(courseId) }
            onEmptyCellTapped: function (day, section) { editor.openNew(day, section) }
        }
    }

    CourseEditor {
        id: editor
        objectName: "courseEditor"
    }

    // 「->」导课：先挑文件。真正的解析还没接上（Qt PDF 模块没装），
    // 选完先给个明确回执，别让按钮看着像坏的。
    FileDialog {
        id: importDialog
        objectName: "importDialog"
        title: qsTr("选择课表 PDF")
        // 不用系统原生对话框：系统那个是独立的顶层窗口，样式跟界面两张皮，
        // 也没法跟着主窗口一起截下来。用内置的，风格统一、也看得见。
        options: FileDialog.DontUseNativeDialog | FileDialog.ReadOnly
        fileMode: FileDialog.OpenFile
        // 别一开就落在程序自己的 build 目录里，从「主文件夹」起步，
        // 用户放课表 PDF 的地方八成在下载或文档里。
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        nameFilters: [qsTr("PDF 文件 (*.pdf)"), qsTr("所有文件 (*)")]
        onAccepted: {
            const parts = selectedFile.toString().split("/")
            importNote.fileName = parts[parts.length - 1]
            importNote.open()
        }
    }

    MessageDialog {
        id: importNote

        property string fileName: ""

        title: qsTr("导课")
        text: qsTr("已选中：%1").arg(importNote.fileName)
        informativeText: qsTr("PDF 解析还没接上——得先装 Qt PDF 模块，再照着你这张课表的版式写解析。")
    }
}
