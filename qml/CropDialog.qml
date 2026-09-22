import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// 换背景的裁剪层：选完图先在这框一块，确认后只把那一块设成背景。
// 选框可以整体拖动，右下角圆点手柄调大小。
Popup {
    id: cropDialog

    property string kind: "table" // "top" = 顶部横幅 / "table" = 课表底图
    property url source
    // 裁剪框锁死的宽高比（宽/高），跟显示区的比例一致，裁下来才不会变形；
    // 0 表示随便拉
    property real aspect: 0

    modal: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: parent
    background: Rectangle { color: "#F0101418" }

    // 原图尺寸（加载完成后才有值）
    readonly property real sw: img.sourceSize.width
    readonly property real sh: img.sourceSize.height
    // 纸面：按原图比例、塞进可用区域
    readonly property real availW: width - 32
    readonly property real availH: height - 190
    readonly property real paperW: (sw > 0 && sh > 0)
        ? Math.min(availW, sw * availH / sh) : availW
    readonly property real paperH: (sw > 0 && sh > 0)
        ? Math.min(availH, sh * availW / sw) : availH

    onOpened: cropError.text = ""

    // 按锁定比例在图里取最大的一块，居中摆好
    function resetSel() {
        if (aspect > 0) {
            let w = paper.width
            let h = w / aspect
            if (h > paper.height) {
                h = paper.height
                w = h * aspect
            }
            sel.width = w
            sel.height = h
            sel.x = (paper.width - w) / 2
            sel.y = (paper.height - h) / 2
        } else {
            sel.x = 0
            sel.y = 0
            sel.width = paper.width
            sel.height = paper.height
        }
    }

    contentItem: Item {
        Label {
            id: title
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 16
            color: "#FFFFFF"
            font.pixelSize: 15
            text: cropDialog.kind === "top" ? qsTr("框一块当顶部横幅")
                                            : qsTr("框一块当课表背景")
        }
        Label {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 16
            color: "#B3FFFFFF"
            font.pixelSize: 12
            text: qsTr("拖动选框 · 拉右下角圆点调大小")
        }

        // 纸面：图片按原比例显示在这里
        Item {
            id: paper
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -30
            width: cropDialog.paperW
            height: cropDialog.paperH

            Image {
                id: img
                anchors.fill: parent
                source: cropDialog.source
                fillMode: Image.Stretch // 纸面已按原图比例算好，直接铺
                asynchronous: true
                onStatusChanged: {
                    if (status === Image.Ready)
                        cropDialog.resetSel()
                }
            }

            // 选框
            Rectangle {
                id: sel
                x: 0; y: 0
                width: paper.width
                height: paper.height
                color: "#26000000"
                border.color: "#FFFFFFFF"
                border.width: 2

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    drag.target: sel
                    drag.axis: Drag.XAndYAxis
                    drag.minimumX: 0
                    drag.maximumX: paper.width - sel.width
                    drag.minimumY: 0
                    drag.maximumY: paper.height - sel.height
                }

                        // 右下角手柄：拖它改选框大小（锁了比例就按比例缩）
                        Rectangle {
                    width: 36; height: 36
                    radius: 18
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: -10
                    anchors.bottomMargin: -10
                    color: "#E0FFFFFF"
                    border.color: "#FF3F51B5"
                    border.width: 2

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -12 // 手小也好按
                        cursorShape: Qt.SizeFDiagCursor
                        property point startPt
                        property size startSize
                        onPressed: (mouse) => {
                            startPt = Qt.point(mouse.x, mouse.y)
                            startSize = Qt.size(sel.width, sel.height)
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const dx = mouse.x - startPt.x
                            const dy = mouse.y - startPt.y
                            let w = startSize.width + dx
                            let h = startSize.height + dy
                            if (aspect > 0) {
                                // 以宽度为准推高度，再夹到纸面里
                                h = w / aspect
                                const maxW = paper.width - sel.x
                                const maxH = paper.height - sel.y
                                if (h > maxH) {
                                    h = maxH
                                    w = h * aspect
                                }
                                if (w > maxW) {
                                    w = maxW
                                    h = w / aspect
                                }
                            }
                            sel.width = Math.max(60, Math.min(paper.width - sel.x, w))
                            sel.height = Math.max(60, Math.min(paper.height - sel.y, h))
                        }
                    }
                }
            }
        }

        Label {
            id: cropError
            anchors.bottom: btnRow.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 8
            color: "#FF8A80"
            font.pixelSize: 12
            text: ""
        }

        Row {
            id: btnRow
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 24
            spacing: 18

            Button {
                text: qsTr("算了不换")
                flat: true
                onClicked: cropDialog.close()
            }
            Button {
                text: qsTr("就用这块")
                Material.background: "#3F51B5"
                onClicked: {
                    const nx = sel.x / paper.width
                    const ny = sel.y / paper.height
                    const nw = sel.width / paper.width
                    const nh = sel.height / paper.height
                    if (backgrounds.cropAndSet(cropDialog.kind, cropDialog.source,
                                               nx, ny, nw, nh)) {
                        cropError.text = ""
                        cropDialog.close()
                    } else {
                        cropError.text = backgrounds.lastError
                    }
                }
            }
        }
    }
}
