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

    // 全窗底图：手机上开了边到边之后，状态栏 / 导航栏底下那块不再
    // 是系统给的死白，透出来的是这张淡彩渐变；窗口里任何没被内容
    // 盖住的角落也都由它兜底。
    background: Image {
        source: "qrc:/assets/bgfill.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    property int currentWeek: 1
    property int totalWeeks: 24

    // 一天 5 个大节（一次课 = 两小节），行数由模型给，别在这儿写死
    readonly property int maxSections: courseModel.maxSections

    // 「改课」模式：打开后点格子才能改课 / 加课
    property bool editing: false

    // 选文件用哪套对话框：手机上必须用系统的（Qt 自绘那个在安卓上又慢又难翻，
    // 而且藏不到文件管理器深处）；桌面继续用内置的，截图脚本才抓得到它。
    readonly property bool nativeFileDialog: Qt.platform.os === "android"

    // 点 AI 按钮切到对话页，再点切回课表
    property bool showChat: false

    function sendChat() {
        const text = chatInput.text
        if (text.trim().length === 0)
            return
        chatInput.text = ""
        aiChat.send(text)
    }

    // 一张课都没有的时候进对话页，先把话头递给它 —— 让它要 PDF、问开学时间
    Component.onCompleted: {
        // 手机上别锁桌面的 400×780，让它自己撑满屏幕
        if (Qt.platform.os === "android")
            root.showMaximized()

        // 一进来就停在「今天所在的周」—— 不用自己从第 1 周滑过来
        root.gotoCurrentWeek()
    }

    // 停在今天那一周（周次夹在 1..总周数 之间）
    function gotoCurrentWeek() {
        root.currentWeek = Math.max(1, Math.min(calendarData.currentWeek(),
                                                root.totalWeeks))
    }

    onShowChatChanged: {
        if (showChat && courseModel.empty && aiChat.messages.length === 0)
            aiChat.send(qsTr("把课表 PDF 或图片发给我（点左边「附件」），我帮你识别并导进去；如果里面没有开学时间，也告诉我。"))
    }

    // 学期起始日一变就 +1，用来把表头日期、年月重算一遍
    property int dateRevision: 0

    readonly property color barColor: "#3F51B5"

    Connections {
        target: calendarData
        function onFirstMondayChanged() {
            root.dateRevision++
            // 开学日期变了，之前停的周次就不对了，重新落到本周
            root.gotoCurrentWeek()
        }
    }

    component NavButton: Rectangle {
        id: nav

        property string label: ""
        property bool dimmed: false
        property bool active: false
        property int labelSize: 26
        signal tapped()
        signal longPressed()

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
            onLongPressed: nav.longPressed()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        // 手机边到边之后，状态栏 / 导航栏会压在窗口上。
        // 给整块界面在安卓上加安全区内边距，把内容（含 AI 对话页底部
        // 输入框）顶到系统栏之上，不会又被导航栏盖住。Windows 上不加。
        anchors.topMargin: Qt.platform.os === "android" ? 26 : 0
        anchors.bottomMargin: Qt.platform.os === "android" ? 48 : 0
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
                source: backgrounds.topImage
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
                    // 手机小屏下粗体笔画会糊在一起，改成常规字重更清爽
                    font.bold: false
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

                // 导课那个「->」按钮已经撤了。解析能力（courseImporter）和
                // 这个 FileDialog 都留着 —— 以后要给 AI 对话加个「传课表」的
                // 入口，接上就能用。
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

            // AI 解析设置。填了 Key 之后，本地解析器认不出来的课表会交给大模型再试一次。
            NavButton {
                label: "AI"
                labelSize: 12
                active: root.showChat
                anchors.right: plusMinus.left
                anchors.rightMargin: 6
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                // 点一下切到对话页（再点切回课表），长按进设置
                onTapped: root.showChat = !root.showChat
                onLongPressed: aiSettings.open()
            }

            // 换主页背景图：贴 +/- 上面
            NavButton {
                label: "IMG"
                labelSize: 10
                anchors.right: plusMinus.right
                anchors.bottom: plusMinus.top
                anchors.bottomMargin: 6
                onTapped: bgSettings.open()
            }

            // 改课开关：贴横幅右下
            NavButton {
                id: plusMinus
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
            id: courseGrid

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.showChat
            currentWeek: root.currentWeek
            maxSections: root.maxSections
            editable: root.editing
            revision: root.dateRevision

            onCourseTapped: function (courseId) { editor.openEdit(courseId) }
            onEmptyCellTapped: function (day, section) { editor.openNew(day, section) }
        }

        // ========== AI 对话页 ==========
        // 背景跟课表用同一张图。ColumnLayout 里不可见的项不占位置，
        // 所以直接跟 CourseGrid 互斥显示就行。
        Item {
            id: chatPage

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showChat

            Image {
                anchors.fill: parent
                source: backgrounds.tableImage
                fillMode: Image.PreserveAspectCrop
            }

            Text {
                anchors.centerIn: parent
                visible: aiChat.messages.length === 0
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("长按 AI 按钮可以换模型、填 Key\n然后在这儿跟它聊")
                color: "#B0BEC5"
                font.pixelSize: 12
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                ListView {
                    id: chatList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: aiChat.messages
                    onCountChanged: positionViewAtEnd()

                    delegate: Item {
                        id: chatRow

                        readonly property var msg: modelData
                        readonly property string reasoning: msg.reasoning === undefined
                                ? "" : String(msg.reasoning)
                        readonly property bool hasReasoning: !msg.isUser
                                && reasoning.length > 0

                        // 思考过程默认收着，点一下展开
                        property bool showReasoning: false

                        width: chatList.width
                        height: column.height

                        Column {
                            id: column

                            width: parent.width
                            spacing: 4

                            // ---- 思考过程 ----
                            Column {
                                width: parent.width
                                spacing: 2
                                visible: chatRow.hasReasoning

                                Rectangle {
                                    width: parent.width
                                    height: 18
                                    color: "transparent"

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: chatRow.showReasoning
                                              ? qsTr("思考过程（收起）")
                                              : qsTr("思考过程（展开）")
                                        color: "#78909C"
                                        font.pixelSize: 10
                                    }

                                    TapHandler {
                                        onTapped: chatRow.showReasoning = !chatRow.showReasoning
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: reasoningText.implicitHeight + 14
                                    radius: 6
                                    color: "#38FFFFFF"
                                    border.width: 1
                                    border.color: "#2690A4AE"
                                    visible: chatRow.showReasoning

                                    Text {
                                        id: reasoningText

                                        anchors.centerIn: parent
                                        width: parent.width - 14
                                        text: chatRow.reasoning
                                        color: "#78909C"
                                        font.pixelSize: 10
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            // ---- 正文气泡 ----
                            Item {
                                width: parent.width
                                height: bubble.height

                                Rectangle {
                                    id: bubble

                                    // 自己说的靠右，AI 说的靠左
                                    anchors.right: chatRow.msg.isUser ? parent.right : undefined
                                    anchors.left: chatRow.msg.isUser ? undefined : parent.left
                                    width: Math.min(chatList.width * 0.8,
                                                    label.implicitWidth + 22)
                                    height: label.implicitHeight + 16
                                    radius: 10
                                    // 自己的话：淡蓝底配深蓝字
                                    color: chatRow.msg.isUser ? "#BBDEFB" : "#F2FFFFFF"
                                    border.width: chatRow.msg.isUser ? 0 : 1
                                    border.color: "#4D90A4AE"

                                    Text {
                                        id: label

                                        anchors.centerIn: parent
                                        width: bubble.width - 22
                                        text: chatRow.msg.content
                                        color: chatRow.msg.isUser ? "#0D3A6B" : "#263238"
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    // 输入框本体：圆角胶囊，附件按钮嵌在右下角
                    Item {
                        id: inputBox

                        Layout.fillWidth: true
                        implicitHeight: 36

                        // 圆角背景（原来 TextField 自带的背景挪到这儿，
                        // TextField 自己设透明，附件按钮才能叠在里面）
                        Rectangle {
                            anchors.fill: parent
                            radius: height / 2
                            color: "#F2FFFFFF"
                            border.width: 1
                            border.color: "#4D90A4AE"
                        }

                        TextField {
                            id: chatInput

                            anchors.fill: parent
                            verticalAlignment: TextInput.AlignVCenter
                            leftPadding: 40   // 给左上角的「+」留位置
                            rightPadding: 14
                            placeholderText: aiChat.busy ? qsTr("正在回…") : ""
                            enabled: !aiChat.busy
                            onAccepted: root.sendChat()
                            background: null
                        }

                        // 附件（PDF / 图片）：嵌在输入框内左上角，点一下挑文件
                        Button {
                            id: attachBtn

                            width: 30
                            height: 30
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.leftMargin: 4
                            anchors.topMargin: 3
                            text: "+"
                            enabled: !aiChat.busy
                            onClicked: root.pickFiles("attach", false, true, chatAttachDialog)

                            background: Rectangle {
                                radius: height / 2
                                color: enabled ? "#C0FFFFFF" : "#80FFFFFF"
                            }
                            contentItem: Text {
                                text: "+"
                                color: "#263238"
                                font.pixelSize: 16
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Button {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 36
                        text: "->"
                        enabled: !aiChat.busy && chatInput.text.trim().length > 0
                        onClicked: root.sendChat()

                        background: Rectangle {
                            radius: height / 2
                            color: enabled ? "#C03F51B5" : "#80000000"
                        }
                        contentItem: Text {
                            text: "->"
                            color: "white"
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
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
        options: root.nativeFileDialog ? FileDialog.ReadOnly
                                       : (FileDialog.DontUseNativeDialog | FileDialog.ReadOnly)
        fileMode: FileDialog.OpenFile
        // 别一开就落在程序自己的 build 目录里，从「主文件夹」起步，
        // 用户放课表 PDF 的地方八成在下载或文档里。
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        nameFilters: [qsTr("PDF / 图片 (*.pdf *.png *.jpg *.jpeg *.bmp *.webp)"),
                      qsTr("所有文件 (*)")]
        onAccepted: courseImporter.importFile(selectedFile)
    }

    // 对话里传附件（PDF / 图片）给 AI。选完把当前输入框的文字一起发过去。
    FileDialog {
        id: chatAttachDialog
        objectName: "chatAttachDialog"

        title: qsTr("选课表或图片")
        options: root.nativeFileDialog ? FileDialog.ReadOnly
                                       : (FileDialog.DontUseNativeDialog | FileDialog.ReadOnly)
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        nameFilters: [qsTr("课表 (*.pdf *.png *.jpg *.jpeg *.bmp *.webp)"),
                      qsTr("所有文件 (*)")]
        onAccepted: {
            // 系统文件管理器给的是 content://，先过 fileBridge 变成真能读的路径
            const paths = fileBridge.toLocalFiles(selectedFiles)
            if (paths.length === 0) {
                root.showFileNote(selectedFiles.length > 0 ? selectedFiles[0] : "")
                return
            }
            aiChat.send(chatInput.text, paths)
            chatInput.text = ""
            if (paths.length < selectedFiles.length)
                root.showFileNote(selectedFiles[0])
        }
    }

    // 只有导课失败才弹（导成了不打扰）
    MessageDialog {
        id: importNote
        objectName: "importNote"

        title: qsTr("导课没成")
        text: qsTr("这份课表没解析出来")
        informativeText: courseImporter.lastError
    }

    // 选文件读不出来时的一句人话（附件 / 换背景都用它）
    // text 是**赋值**不是绑定：QML 绑定的方法调用会被缓存住，
    // 那样永远只会显示第一次的空值，看不到真实原因
    MessageDialog {
        id: fileNote
        objectName: "fileNote"

        title: qsTr("文件没读到")
        text: qsTr("系统没把这个文件交给程序，换一个试试")
    }

    // 安卓上 Qt 自己的 FileDialog 结果回不来，走 Picker：
    // 发系统选择器 → 选完 app 回到前台 → 这里收到路径清单
    Connections {
        target: fileBridge

        function onFilesPicked(purpose, paths) {
            if (purpose === "attach") {
                aiChat.send(chatInput.text, paths)
                chatInput.text = ""
                return
            }
            cropDialog.kind = purpose
            cropDialog.aspect = (purpose === "top") ? 1920 / 803
                                                    : courseGrid.width / Math.max(1, courseGrid.height)
            cropDialog.source = "file:///" + paths[0]
            cropDialog.open()
        }
    }

    // 选文件：安卓走系统选择器，桌面走 Qt 的对话框
    function pickFiles(purpose, image, multiple, dialog) {
        if (root.nativeFileDialog)
            fileBridge.startPick(purpose, image, multiple)
        else
            dialog.open()
    }
    // 把真实原因 + 系统给过来的原始路径一起摆出来
    function showFileNote(rawPath) {
        const why = fileBridge.lastError
        fileNote.text = (why && why.length > 0 ? why : qsTr("没说原因"))
                        + qsTr("\n拿到的路径：") + (rawPath ? rawPath : qsTr("（空）"))
        fileNote.open()
    }

    Connections {
        target: courseImporter

        function onFinished(ok) {
            // 学期一共多少周，按导进来的课自动定
            if (ok && courseImporter.lastMaxWeek > 0) {
                root.totalWeeks = courseImporter.lastMaxWeek
                // 总周数定了，重新落到今天那一周
                root.gotoCurrentWeek()
            }

            // 导成了不打扰 —— 课表变了本身就是反馈。只有没导成才需要说一句。
            if (!ok)
                importNote.open()
        }
    }

    // AI 设置：长按 AI 按钮进来。只有「用哪家」和「API Key」两项 ——
    // 接口地址和模型跟着预设走，不用用户操心。
    Dialog {
        id: aiSettings
        objectName: "aiSettings"

        title: qsTr("AI")
        modal: true
        anchors.centerIn: parent
        width: 300

        // 默认的 header 是一条不透明的白条，跟半透明背景拼一起很割裂，
        // 换成光秃秃一个 Label
        header: Label {
            text: qsTr("AI")
            font.pixelSize: 16
            padding: 12
        }

        // 背景压成半透明的，能透出后面的底图
        background: Rectangle {
            color: "#C0FFFFFF"
            radius: 8
            border.width: 1
            border.color: "#4DFFFFFF"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            ComboBox {
                id: providerBox
                objectName: "providerBox"
                Layout.fillWidth: true
                model: [qsTr("混元"), "DeepSeek"]

                Component.onCompleted: currentIndex = aiImporter.preset
                onActivated: aiImporter.applyPreset(currentIndex)
            }

            Label {
                Layout.topMargin: 4
                text: qsTr("API Key")
                font.pixelSize: 12
                color: "#546E7A"
            }
            TextField {
                id: apiKeyField
                Layout.fillWidth: true
                placeholderText: "sk-..."
                echoMode: TextInput.Password
                text: aiImporter.apiKey
            }

            // 角色不是「用哪家」，是 AI 的性格设定，所以单独一个入口
            Button {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: qsTr("角色")
                onClicked: {
                    soulArea.text = soul.text
                    aiSettings.close()
                    soulDialog.open()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("保存")
                    onClicked: {
                        // 把当前服务商（混元 / DeepSeek）的 Key 显式存下来
                        aiImporter.apiKey = apiKeyField.text
                    }
                }

                Button {
                    text: qsTr("关闭")
                    onClicked: {
                        // 关的时候也顺手存，别指望失焦触发
                        aiImporter.apiKey = apiKeyField.text
                        aiSettings.close()
                    }
                }
            }
        }
    }

    // 换主页那两张背景图
    Dialog {
        id: bgSettings
        objectName: "bgSettings"

        title: qsTr("背景图")
        modal: true
        anchors.centerIn: parent
        width: 300

        header: Label {
            text: qsTr("背景图")
            font.pixelSize: 16
            padding: 12
        }

        // 跟 AI 那个设置框保持一致
        background: Rectangle {
            color: "#C0FFFFFF"
            radius: 8
            border.width: 1
            border.color: "#4DFFFFFF"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Label {
                text: qsTr("顶部横幅")
                font.pixelSize: 12
                color: "#546E7A"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button { text: qsTr("换一张"); onClicked: root.pickFiles("top", true, false, topPicker) }
                Button { text: qsTr("恢复默认"); onClicked: backgrounds.resetTop() }
                Item { Layout.fillWidth: true }
            }

            Label {
                Layout.topMargin: 6
                text: qsTr("课表背景")
                font.pixelSize: 12
                color: "#546E7A"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button { text: qsTr("换一张"); onClicked: root.pickFiles("table", true, false, tablePicker) }
                Button { text: qsTr("恢复默认"); onClicked: backgrounds.resetTable() }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Item { Layout.fillWidth: true }
                Button { text: qsTr("关闭"); onClicked: bgSettings.close() }
            }
        }
    }

    FileDialog {
        id: topPicker
        objectName: "topPicker"

        title: qsTr("选一张顶部横幅")
        options: root.nativeFileDialog ? FileDialog.ReadOnly
                                       : (FileDialog.DontUseNativeDialog | FileDialog.ReadOnly)
        fileMode: FileDialog.OpenFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"), qsTr("所有文件 (*)")]
        // 选完不直接用：先把系统给的路径（安卓上是 content://）换成真能读的
        // 本地文件，再进裁剪层框一块
        onAccepted: {
            const p = fileBridge.toLocalFile(selectedFile)
            if (p.length === 0) {
                root.showFileNote(selectedFile)
                return
            }
            cropDialog.kind = "top"
            cropDialog.aspect = 1920 / 803 // 顶部横幅的比例，跟显示区一致
            cropDialog.source = "file:///" + p
            cropDialog.open()
        }
    }

    FileDialog {
        id: tablePicker
        objectName: "tablePicker"

        title: qsTr("选一张课表背景")
        options: root.nativeFileDialog ? FileDialog.ReadOnly
                                       : (FileDialog.DontUseNativeDialog | FileDialog.ReadOnly)
        fileMode: FileDialog.OpenFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"), qsTr("所有文件 (*)")]
        onAccepted: {
            const p = fileBridge.toLocalFile(selectedFile)
            if (p.length === 0) {
                root.showFileNote(selectedFile)
                return
            }
            cropDialog.kind = "table"
            cropDialog.aspect = courseGrid.width / Math.max(1, courseGrid.height)
            cropDialog.source = "file:///" + p
            cropDialog.open()
        }
    }

    // 换背景的裁剪层（背景设置里两个「换一张」都接到它）
    CropDialog {
        id: cropDialog
        width: root.width
        height: root.height
    }

    // 角色：AI 的性格设定，跟 WorkBuddy 那份 SOUL.md 一个意思
    Dialog {
        id: soulDialog
        objectName: "soulDialog"

        title: qsTr("角色")
        modal: true
        anchors.centerIn: parent
        width: 340
        height: 420

        header: Label {
            text: qsTr("角色")
            font.pixelSize: 16
            padding: 12
        }

        // 所有设置栏统一 75% 不透明，能透出后面的背景图
        background: Rectangle {
            color: "#C0FFFFFF"
            radius: 8
            border.width: 1
            border.color: "#4DFFFFFF"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: "#546E7A"
                text: qsTr("这段文字会作为系统提示词发给 AI，决定它说话什么调调 —— 跟用哪家模型没关系。")
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextArea {
                    id: soulArea
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 12
                    text: soul.text
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: qsTr("恢复默认")
                    onClicked: {
                        soul.reset()
                        soulArea.text = soul.text
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("保存")
                    onClicked: {
                        soul.setText(soulArea.text)
                        soulDialog.close()
                    }
                }
            }
        }
    }

    // AI 想改角色设定时弹这个 —— 它没权限自己改，必须用户点头
    Dialog {
        id: roleConfirm
        objectName: "roleConfirm"

        title: qsTr("角色")
        modal: true
        anchors.centerIn: parent
        width: 340
        height: 380

        header: Label {
            text: qsTr("角色")
            font.pixelSize: 16
            padding: 12
        }

        background: Rectangle {
            color: "#C0FFFFFF"
            radius: 8
            border.width: 1
            border.color: "#4DFFFFFF"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: "#546E7A"
                text: qsTr("AI 想把「角色设定」整个换成下面这样。同意吗？（不同意就保持原样）")
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextArea {
                    text: aiChat.pendingRole
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 12
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: qsTr("不同意")
                    onClicked: {
                        aiChat.rejectRoleChange()
                        roleConfirm.close()
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("同意")
                    onClicked: {
                        aiChat.acceptRoleChange()
                        roleConfirm.close()
                    }
                }
            }
        }
    }

    Connections {
        target: aiChat

        function onRoleChangeRequested() {
            roleConfirm.open()
        }
    }
}
