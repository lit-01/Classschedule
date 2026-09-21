import QtQuick

// 一周课表网格：左边节次栏 + 7 列星期，课程卡片按 day / 节次范围绝对定位
Item {
    id: grid

    property int currentWeek: 1
    property int maxSections: 5

    // 只有处于「改课」模式时，点格子才有反应
    property bool editable: false

    // 学期起始日改了之后，靠它把表头日期重算一遍
    property int revision: 0

    // 一次课 = 两小节，所以一天只有 5 大节。
    // 每行左边标上这堂课的起止时间。**跟你们学校不一样就改这里**。
    readonly property var sectionTimes: [
        "8:00-9:40",
        "10:00-11:40",
        "14:00-15:40",
        "16:00-17:40",
        "19:00-20:40"
    ]

    readonly property int sectionColWidth: 52
    readonly property int headerHeight: 46

    // 行高跟着可视高度走：5 行正好铺满一屏，屏幕太矮时才滚动
    readonly property real rowHeight: Math.max(92, flick.height / maxSections)

    readonly property real colWidth: (width - sectionColWidth) / 7

    readonly property var dayNames: ["一", "二", "三", "四", "五", "六", "日"]

    // 今天是周几：1 = 周一 …… 7 = 周日
    readonly property int todayIndex: {
        const d = new Date().getDay()
        return d === 0 ? 7 : d
    }

    signal courseTapped(int courseId)
    signal emptyCellTapped(int day, int section)

    // ---------- 整块课表的底图（星期表头那一行也盖到） ----------
    // 原图是竖构图，按高度铺满、左右各裁掉一点点
    Image {
        anchors.fill: parent
        source: "qrc:/assets/tablebg.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    // ---------- 星期表头：星期几 + 当天日期 ----------
    Row {
        id: headerRow
        x: grid.sectionColWidth
        y: 0

        Repeater {
            model: grid.dayNames

            delegate: Item {
                id: headCell

                width: grid.colWidth
                height: grid.headerHeight

                readonly property var info: {
                    grid.revision
                    return calendarData.dayInfo(grid.currentWeek, index + 1)
                }
                readonly property bool isToday: (index + 1) === grid.todayIndex
                readonly property bool isHoliday: info.holiday === true
                // 调休补班那天也算特殊日子，但颜色跟真放假区分开
                readonly property bool isWorkday: info.workday === true

                Column {
                    anchors.centerIn: parent
                    spacing: 0

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("周") + modelData
                        font.pixelSize: 10
                        font.bold: headCell.isToday
                        color: headCell.isHoliday ? "#E53935"
                               : (headCell.isWorkday ? "#F57C00"
                                                     : (headCell.isToday ? "#3F51B5" : "#78909C"))
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: headCell.info.day
                        font.pixelSize: 14
                        font.bold: true
                        color: headCell.isHoliday ? "#E53935"
                               : (headCell.isWorkday ? "#F57C00"
                                                     : (headCell.isToday ? "#3F51B5" : "#37474F"))
                    }
                }
            }
        }
    }

    // ---------- 可滚动区域 ----------
    Flickable {
        id: flick

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: grid.headerHeight
        anchors.bottom: parent.bottom

        contentWidth: width
        contentHeight: grid.maxSections * grid.rowHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            // 左侧节次：第 N 大节 + 这堂课的起止时间
            Column {
                Repeater {
                    model: grid.maxSections

                    delegate: Item {
                        width: grid.sectionColWidth
                        height: grid.rowHeight

                        Column {
                            anchors.centerIn: parent
                            spacing: 2

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: index + 1
                                font.pixelSize: 12
                                font.bold: true
                                color: "#546E7A"
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: grid.sectionTimes[index] !== undefined
                                      ? grid.sectionTimes[index] : ""
                                font.pixelSize: 8
                                color: "#78909C"
                            }
                        }
                    }
                }
            }

            // 课程区
            Item {
                id: board

                width: flick.width - grid.sectionColWidth
                height: grid.maxSections * grid.rowHeight

                // 今天那一列的淡底色（QML 的 8 位颜色是 #AARRGGBB）
                Rectangle {
                    x: (grid.todayIndex - 1) * grid.colWidth
                    width: grid.colWidth
                    height: board.height
                    color: "#143F51B5"
                }

                // 横向网格线（底图是彩色的，线条用半透明白更服帖）
                Repeater {
                    model: grid.maxSections + 1

                    delegate: Rectangle {
                        width: board.width
                        height: 1
                        y: index * grid.rowHeight
                        color: index === 0 ? "transparent" : "#4DFFFFFF"
                    }
                }

                // 纵向网格线
                Repeater {
                    model: 8

                    delegate: Rectangle {
                        width: 1
                        height: board.height
                        x: index * grid.colWidth
                        color: index === 0 ? "transparent" : "#4DFFFFFF"
                    }
                }

                // 点空格子 → 直接在这个位置加课
                // 用 TapHandler 而不是 MouseArea：MouseArea 会把 Flickable
                // 的滑动一并吃掉，课表就滚不动了。
                TapHandler {
                    enabled: grid.editable
                    onTapped: function (eventPoint) {
                        const day = Math.floor(eventPoint.position.x / grid.colWidth) + 1
                        const section = Math.floor(eventPoint.position.y / grid.rowHeight) + 1
                        grid.emptyCellTapped(Math.min(Math.max(day, 1), 7),
                                             Math.min(Math.max(section, 1), grid.maxSections))
                    }
                }

                // 课程卡片
                Repeater {
                    model: courseModel

                    delegate: Item {
                        id: cell

                        // 没有周次信息（startWeek <= 0）的课每周都上
                        readonly property bool inThisWeek: {
                            if (model.startWeek <= 0 || model.endWeek <= 0)
                                return true
                            if (grid.currentWeek < model.startWeek || grid.currentWeek > model.endWeek)
                                return false
                            if (model.weekType === 1 && grid.currentWeek % 2 === 0)
                                return false
                            if (model.weekType === 2 && grid.currentWeek % 2 === 1)
                                return false
                            return true
                        }

                        readonly property string weekLabel: {
                            if (model.startWeek <= 0 || model.endWeek <= 0)
                                return ""
                            let t = model.startWeek + "-" + model.endWeek + "周"
                            if (model.weekType === 1)
                                t += "单"
                            else if (model.weekType === 2)
                                t += "双"
                            return t
                        }

                        // 这天是不是放假 / 调休 —— 是的话整块课变灰
                        readonly property bool grayDay: {
                            grid.revision
                            return calendarData.dayInfo(grid.currentWeek, model.day).gray === true
                        }

                        visible: inThisWeek
                        x: (model.day - 1) * grid.colWidth
                        y: (model.startSection - 1) * grid.rowHeight
                        width: grid.colWidth
                        height: (model.endSection - model.startSection + 1) * grid.rowHeight

                        CourseCard {
                            anchors.fill: parent
                            anchors.margins: 1

                            courseName: model.name
                            courseTeacher: model.teacher
                            courseRoom: model.room
                            courseWeek: cell.weekLabel
                            cardColor: model.color
                            editable: grid.editable
                            grayDay: cell.grayDay

                            onClicked: grid.courseTapped(model.courseId)
                        }
                    }
                }
            }
        }
    }
}
