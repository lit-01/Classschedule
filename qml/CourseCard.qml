import QtQuick

// 一格课程卡片：课程名 / 老师 / 地点 / 周次
// 底色由外面传进来（白 / 明绿交替）；放假日和调休日的课整块变灰。
Rectangle {
    id: card

    property string courseName: ""
    property string courseTeacher: ""
    property string courseRoom: ""
    property string courseWeek: "" // 空字符串 = 表里没给周次，不显示这一行
    property color cardColor: "#FFFFFF"
    property bool editable: false
    property bool grayDay: false

    signal clicked()

    // 绿卡和白卡深浅差得多，同一个字色压上去会糊。
    // 所以按底色深浅自动切字色：浅底用灰黑，深底用同色系的墨绿。
    readonly property bool deepBg: cardColor.hslLightness < 0.6

    readonly property color nameColor: grayDay ? "#6B6B6B"
                                                : (deepBg ? "#0D2B24" : "#263238")
    readonly property color subColor: grayDay ? "#7C7C7C"
                                               : (deepBg ? "#173D32" : "#455A64")
    readonly property color weekColor: grayDay ? "#868686"
                                                : (deepBg ? "#1B463A" : "#546E7A")

    radius: 6
    color: grayDay ? "#C7C7C7" : cardColor
    border.width: 1
    // 平时不画边框。底图是彩色的，再勒一圈线格子就太死了。
    // 只有「改课」模式留一圈蓝框——那是提示这格能点，是功能不是装饰。
    border.color: editable ? "#3F51B5" : "transparent"
    clip: true

    // 一格大节有 113 px 高，内容只有四五短行；
    // 稍微偏上放，比正中好看一点
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -12
        width: card.width - 6
        spacing: 1

        Text {
            width: parent.width
            text: card.courseName
            color: card.nameColor
            font.pixelSize: 10
            font.bold: true
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 3
        }

        Text {
            width: parent.width
            visible: card.courseTeacher.length > 0
            text: card.courseTeacher
            color: card.subColor
            font.pixelSize: 8
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            width: parent.width
            visible: card.courseRoom.length > 0
            text: card.courseRoom
            color: card.subColor
            font.pixelSize: 8
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            width: parent.width
            visible: card.courseWeek.length > 0
            text: card.courseWeek
            color: card.weekColor
            font.pixelSize: 8
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    TapHandler {
        enabled: card.editable
        onTapped: card.clicked()
    }
}
