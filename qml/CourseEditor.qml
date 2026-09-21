import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// 添加 / 编辑 / 删除课程
Dialog {
    id: editor

    property int editId: -1

    // 一天 5 大节（一次课 = 两小节）
    readonly property var sectionOptions: [1, 2, 3, 4, 5]

    // 下拉里顺手把时间写出来，省得记第几大节是几点
    readonly property var sectionLabels: [
        "1  8:00-9:40",
        "2  10:00-11:40",
        "3  14:00-15:40",
        "4  16:00-17:40",
        "5  19:00-20:40"
    ]

    // 0 = 表里没给周次（卡片不显示那一行，每周都上）
    readonly property var weekOptions: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]

    modal: true
    anchors.centerIn: parent
    width: 340
    padding: 16
    title: editor.editId > 0 ? qsTr("编辑课程") : qsTr("添加课程")

    function openNew(day, section) {
        editor.editId = -1
        nameField.text = ""
        teacherField.text = ""
        roomField.text = ""
        dayBox.currentIndex = (day >= 1 && day <= 7) ? day - 1 : 0
        const s = (section >= 1 && section <= editor.sectionOptions.length) ? section - 1 : 0
        startSectionBox.currentIndex = s
        endSectionBox.currentIndex = s
        startWeekBox.currentIndex = 1
        endWeekBox.currentIndex = 20
        weekTypeBox.currentIndex = 0
        editor.open()
    }

    function openEdit(courseId) {
        const c = courseModel.getCourse(courseId)
        if (!c || c.name === undefined)
            return

        editor.editId = courseId
        nameField.text = c.name
        teacherField.text = c.teacher
        roomField.text = c.room
        dayBox.currentIndex = c.day - 1
        startSectionBox.currentIndex = c.startSection - 1
        endSectionBox.currentIndex = c.endSection - 1
        startWeekBox.currentIndex = c.startWeek
        endWeekBox.currentIndex = c.endWeek
        weekTypeBox.currentIndex = c.weekType
        editor.open()
    }

    function submit() {
        if (nameField.text.trim().length === 0) {
            nameField.placeholderText = qsTr("课程名不能为空")
            return
        }

        // 任意一头选了「—」就整门课按「没有周次」处理
        let startWeek = editor.weekOptions[startWeekBox.currentIndex]
        let endWeek = editor.weekOptions[endWeekBox.currentIndex]
        if (startWeek <= 0 || endWeek <= 0) {
            startWeek = 0
            endWeek = 0
        }

        const data = {
            "name": nameField.text.trim(),
            "teacher": teacherField.text.trim(),
            "room": roomField.text.trim(),
            "day": dayBox.currentIndex + 1,
            "startSection": editor.sectionOptions[startSectionBox.currentIndex],
            "endSection": editor.sectionOptions[endSectionBox.currentIndex],
            "startWeek": startWeek,
            "endWeek": endWeek,
            "weekType": weekTypeBox.currentIndex
        }

        if (editor.editId > 0)
            courseModel.updateCourse(editor.editId, data)
        else
            courseModel.addCourse(data)

        editor.close()
    }

    contentItem: GridLayout {
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        width: editor.availableWidth

        Label {
            text: qsTr("课程名")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("例如：高等数学")
        }

        Label {
            text: qsTr("老师")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        TextField {
            id: teacherField
            Layout.fillWidth: true
            placeholderText: qsTr("选填")
        }

        Label {
            text: qsTr("地点")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        TextField {
            id: roomField
            Layout.fillWidth: true
            placeholderText: qsTr("选填")
        }

        Label {
            text: qsTr("星期")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        ComboBox {
            id: dayBox
            Layout.fillWidth: true
            model: [qsTr("周一"), qsTr("周二"), qsTr("周三"), qsTr("周四"),
                    qsTr("周五"), qsTr("周六"), qsTr("周日")]
        }

        Label {
            text: qsTr("节次")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            // 下拉列表里带上时间，省得记第几大节是几点；
            // 收起时只显示序号，不然 74 px 放不下整行
            ComboBox {
                id: startSectionBox
                Layout.preferredWidth: 74
                model: editor.sectionLabels
                displayText: String(editor.sectionOptions[currentIndex])
                popup.width: 170
            }
            Label {
                text: "—"
                color: "#90A4AE"
            }
            ComboBox {
                id: endSectionBox
                Layout.preferredWidth: 74
                model: editor.sectionLabels
                displayText: String(editor.sectionOptions[currentIndex])
                popup.width: 170
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: qsTr("周次")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            ComboBox {
                id: startWeekBox
                Layout.preferredWidth: 74
                model: editor.weekOptions
                displayText: editor.weekOptions[currentIndex] === 0
                             ? "—" : String(editor.weekOptions[currentIndex])
            }
            Label {
                text: "—"
                color: "#90A4AE"
            }
            ComboBox {
                id: endWeekBox
                Layout.preferredWidth: 74
                model: editor.weekOptions
                displayText: editor.weekOptions[currentIndex] === 0
                             ? "—" : String(editor.weekOptions[currentIndex])
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: qsTr("单双周")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        ComboBox {
            id: weekTypeBox
            Layout.fillWidth: true
            model: [qsTr("每周"), qsTr("单周"), qsTr("双周")]
        }
    }

    footer: RowLayout {
        spacing: 8

        Button {
            text: qsTr("删除")
            visible: editor.editId > 0
            flat: true
            Material.foreground: "#E53935"
            onClicked: {
                courseModel.removeCourse(editor.editId)
                editor.close()
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            text: qsTr("取消")
            flat: true
            onClicked: editor.close()
        }

        Button {
            text: qsTr("保存")
            highlighted: true
            onClicked: editor.submit()
        }
    }
}
