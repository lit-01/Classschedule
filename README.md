# 课程表 · CourseTable

一个 Qt 6 / QML 写的大学课程表 app。现在在桌面上跑，最终目标是打包成 Android 应用。

![主界面](shots/a-normal.png)

## 功能

- 一周 7 天 × **5 个大节**。按国内大学的习惯，一堂课分两小节，所以一节占一行，
  左侧节次列直接标出每大节的起止时间（`8:00-9:40`、`10:00-11:40` …）。
- 顶部一张横幅，年月（`2026` / `9月`）分两行压在周次导航头上，周次两边是 `‹ ›` 翻页。
- 周一到周日的日期按学期起始周自动推算，今天那列标蓝，周六周日标红。
- **法定节假日和调休补班日**从手机自带的日历里读（Android 走 JNI 读 `CalendarContract`），
  读不到的日期用内置兜底表。放假日的课整块变灰。
- 「改课」模式：点右上角 `+/-` 打开，然后点空格子加课、点已有的课改课。
- **导课解析**（当前没有界面入口）：挑一个课表 PDF，解析出来直接铺到表上。
  周次行末尾那个 `->` 按钮已经撤了，但解析器和 FileDialog 都留着 ——
  用 `COURSETABLE_AUTOIMPORT` 还能触发，以后给 AI 对话加个「传课表」的入口
  就能接上。见下面「导课是怎么做的」。
- 顶栏的 `AI` 按钮：**点一下切到 AI 对话页**，**长按进设置**（混元 / DeepSeek
  二选一，填个 Key 就能用）。导课时本地解析器认不出来的课表，
  也会自动交给它再试一次。
- **AI 能直接改课表**：对话里说「把周三的编译原理删了」「周六加一门 XX」，
  它会带着改动回来，App 直接落到课表上。一张课都没有的时候进对话页，
  它会自己先开口要 PDF、问开学时间。
- 卡片上显示**课程类别**（理论 / 实验 / 上机 / 实践，从课表 PDF 里那些
  ★ ☆ ◆ ■ 来的）。底色是白 / 青绿 / **樱花粉**三色轮换。
- `IMG` 按钮（在 `+/-` 上面）能换主页那两张背景图 —— **顶部横幅**和**课表底图**，
  各支持「换一张」和「恢复默认」。选完图会先进一个**裁剪层**：拖动选框、拉右下角圆点
  调大小（比例锁死显示区的比例，裁下来不会变形），确认才生效。
- **对话里能给 AI 发附件**（PDF / 图片，可多选）：PDF 本地抠文字、图片原图走视觉模型。
- AI 知道**今天是几号、第几周、今天有哪几门课**（每次请求都随提示词带过去），
  你告诉它开学第一周的周一，它会用 `set_first_monday` 存下来。
- 装好后**默认是空的**：角色（人格设定）空白、课表也没有示例课 —— 想让它像个助手的样，
  自己在「角色」里写，或者让 AI 提议一份。

## 编译

依赖 Qt 6.5+（开发时用的是 **6.11.2 mingw_64**）、CMake 3.21+、Ninja、MinGW 13。

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="<Qt>/6.11.2/mingw_64" \
  -DCMAKE_MAKE_PROGRAM="<Qt>/Tools/Ninja/ninja.exe" \
  -DCMAKE_C_COMPILER="<Qt>/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="<Qt>/Tools/mingw1310_64/bin/g++.exe"

cmake --build build

# 拷 Qt 运行库
<Qt>/6.11.2/mingw_64/bin/windeployqt.exe --qmldir qml build/CourseTable.exe
```

**`windeployqt` 这步别省。** 新加一个 QML 模块（`QtQuick.Dialogs`、`QtCore` 之类）的时候，
CMake 那边配好了、编译一路绿灯，但 `build/` 里缺 DLL 和 QML 模块，**运行到那一步才炸**。
每次动了 `import` 就重跑一遍。

## Android 打包

交叉编译靠 Qt 自带的 `qt-cmake` + `androiddeployqt`，需要 **JDK 17**、**Android SDK**
（platform / build-tools）、**NDK**：

- `android/` 下是手写打包模板：`AndroidManifest.xml`（带 `READ_CALENDAR` 权限、
  `androidx.core.content.FileProvider`）+ `src/com/pony/coursetable/HolidayReader.java`
  （走 JNI 读系统日历的 `CalendarContract` 拿节假日 / 调休）。
- **NDK 坑**：NDK r27c / r28 的 toolchain 最高只支持 **API 35**，`ANDROID_PLATFORM`
  别填 `android-36`，否则 configure 直接报 `above the maximum supported version 35`。
  Qt 6.11.2 模板 `build.gradle` 里 `androidx.core:core:1.17.0` 又要求 compileSdk ≥ 36，
  所以把那份依赖降到 `1.13.1`（只要求 ≥ 34）就能和 API 35 和平共处。
- 权限 API 漂移：Qt 6.11 里 `QCoreApplication::requestPermission` 不再是静态函数，
  改成 `QCoreApplication::instance()->requestPermission(...)`，状态判定用
  `Qt::PermissionStatus::Granted`。
- **HTTPS 必须自带 OpenSSL**：Qt 只带 `qopensslbackend` 插件，`libcrypto_3.so` /
  `libssl_3.so` 得自己塞进 APK，否则安卓上一切网络请求都失败（桌面走系统 Schannel，
  所以「桌面正常、手机发不出请求」就是缺这两个库）。库可以取 KDAB 的
  `android_openssl`（预编译），在 `CMakeLists.txt` 里用 `QT_ANDROID_EXTRA_LIBS` 带进去。
- **文件选择器不要用 Qt 的 `FileDialog`**：安卓上它能弹出选择界面，但 `selectedFile`
  永远是空的（结果回不来）。本项目自己实现了一份：`android/.../Picker.java` 发
  `ACTION_OPEN_DOCUMENT`，`MainActivity.onActivityResult` 接结果，文件拷进 cache 后
  把路径清单写 `cache/picked.txt`，Qt 侧回到前台时读取（见 `src/filebridge.*`）。

```bash
# 用 Qt 安卓套件里的 qt-cmake，别直接用 cmake
<Qt>/6.11.2/android_arm64_v8a/bin/qt-cmake.bat -S . -B build-android -G Ninja \
  -DQT_HOST_PATH=<Qt>/6.11.2/mingw_64 \
  -DANDROID_SDK_ROOT=<Android SDK 根目录> \
  -DANDROID_NDK_ROOT=<Android SDK 根目录>/ndk/<版本> \
  -DANDROID_PLATFORM=android-35

cmake --build build-android

<Qt>/6.11.2/mingw_64/bin/androiddeployqt.exe \
  --input build-android/android-CourseTable-deployment-settings.json \
  --output build-android/android-build --android-platform android-35 --gradle
```

产物在 `build-android/android-build/build/outputs/apk/`（debug 签名版可直接
`adb install`；要分发就自己拿 keystore 签 release）。`build-android/` 已在
`.gitignore` 里，不会进仓库。

## 目录

```
CourseTable/
├─ main.cpp              入口；还带一套调界面用的「截图后门」（见下）
├─ CMakeLists.txt
├─ src/
│  ├─ coursemodel.*      课程数据：QAbstractListModel + JSON 持久化
│  ├─ calendardata.*     学期起始周、节假日、日期换算
│  ├─ pdftext.*          最小 PDF 文本提取器，不依赖 QtPdf
│  ├─ timetableimport.*  把提取出来的文字解析成课程
│  ├─ courseimporter.*   给 QML 调的导课入口（本地优先、AI 兜底）
│  ├─ aiimporter.*       AI 配置（混元 / DeepSeek 两家）+ 走 AI 解析课表
│  ├─ aichat.*           AI 对话（带日期/课表上下文，还能直接改课表）
│  ├─ backgrounds.*      主页背景图：顶部横幅 / 课表底图（含裁剪落盘）
│  ├─ filebridge.*       把选择器给的路径（安卓是 content://）变成能读的本地文件
│  └─ soul.*             AI 性格设定（AppData/soul.md）
├─ qml/
│  ├─ Main.qml           顶栏（横幅 / 周次 / 年月 / 导课 / 改课开关）
│  ├─ CourseGrid.qml     课表网格 + 左侧节次列 + 星期表头
│  ├─ CourseCard.qml     一格课程卡片
│  ├─ CourseEditor.qml   加课 / 改课弹窗
│  └─ CropDialog.qml     换背景前的裁剪层（锁显示区比例）
├─ assets/               横幅和表格底图（编进 qrc，`-no-compress` 原样存）
├─ android/              Android 打包模板
│  ├─ AndroidManifest.xml
│  ├─ res/mipmap-*/      启动器图标（自适应 + 各密度）
│  └─ src/com/pony/coursetable/
│     ├─ HolidayReader.java   读系统日历里的节假日
│     ├─ FileBridge.java      content:// 的显示名 + 字节读取
│     ├─ Picker.java          自己发的系统文件选择器（Qt 那个在安卓上拿不到结果）
│     └─ MainActivity.java    边到边 + 转发 Picker 的 onActivityResult
├─ tools/                Python 辅助脚本
└─ shots/                界面截图
```

## 调界面用的截图后门

`main.cpp` 里留了几个环境变量，不用手点就能把某个状态渲染成 PNG 然后退出：

```bash
COURSETABLE_SHOT=out.png              # 输出路径，设了就进截图模式
COURSETABLE_SHOT_EDIT=1               # 先切到「改课」模式
COURSETABLE_SHOT_DIALOG=1             # 先打开「加课」弹窗
COURSETABLE_SHOT_ACTION=importDialog  # 按 objectName 找控件、调它的 open()
COURSETABLE_SHOT_DELAY=9000           # 抓图前等多久（毫秒），默认 2400

COURSETABLE_AUTOIMPORT=课表.pdf       # 启动就直接导入这份课表，跳过点界面
COURSETABLE_FORCE_AI=1                # 跳过本地解析、直接走 AI（验证那条通道用）
COURSETABLE_CHAT_TEST=你好            # 切到对话页并自动发一句（验证对话链路用）
COURSETABLE_PDF=课表.pdf              # 只跑解析，结果写 COURSETABLE_PDF_OUT，然后退出
COURSETABLE_PDF_OUT=result.txt
```

`tools/` 下的脚本（跑它们要用隔离 venv 里的 Python：
`~/.workbuddy/binaries/python/envs/default/Scripts/python.exe`）：

- `screenshot.py` —— 批量生成 `shots/a-normal.png`（普通）、`b-edit.png`（改课）、
  `c-dialog.png`（加课弹窗）。
- `shot_dialog.py` —— 单独抓「导课」的文件选择对话框。它是**独立顶层窗口**，
  `QQuickWindow::grabWindow()` 只抓得到主窗口，所以要 `EnumWindows` 找到它的 HWND、
  取真实可见边框（用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`，
  别用 `GetWindowRect`，那个把透明阴影也算进去了），再从外面按屏幕坐标抓。
- `pick_color.py` —— 纯 Python（不依赖 Pillow）从 PNG 里取某个像素的颜色。
- `inspect_pdf.py` —— 把一份 PDF 的底细翻出来：页数、字体、有没有表格线、
  `extract_tables()` 能不能识别出表格、文字块都落在哪。
- `parse_timetable.py` —— 导课解析的 **Python 原型**。先在 Python 里把算法跑通、
  拿到「标准答案」，再照着写了 C++ 版；两边结果可以互相验证。

## 导课是怎么做的

> 入口（周次行那个 `->` 按钮）已经撤了，解析能力保留着：
> `COURSETABLE_AUTOIMPORT=课表.pdf` 会走同一条路。

解析一份课表 PDF，结果直接替换整张课表。

**没有用 QtPdf。** 教务系统导出的课表是 iText 生成的，字体是
`STSong-Light-UniGB-UCS2-H`，**不带 ToUnicode 表** —— 内容流里的字节本身
就是 UCS-2 大端编码的 Unicode。所以自己拆 PDF 就够：`src/pdftext.cpp` 里
解 Flate + 扫内容流 + 解码一共三百来行，Android 上也不用另配组件。

解析思路（和 `tools/parse_timetable.py` 完全一致）：

1. 表格线是矢量矩形，**宽度众数**那一档就是 7 个数据列；`width > 50`
   自然筛掉「时间段」和「节次」两列。
2. 按列把文字捞出来，**跨页拼接** —— 格子内容会被分页切断，得接回去。
3. 每个格子用「学分:数字」当结束标志切块，再用正则抠出节次 / 周次 / 场地 / 教师。

周次认得 `1-16周`、`2-16周(双)`、`9周`、`4-8周,10-16周` 这几种写法。
「星期几」只能从列的位置推 —— 格子里没写。

**只认这一种版式。** 别家教务系统（或者 Word 直接导出的）字体编码不一样，
会明确报「认不出来」，而不是硬凑一堆错数据出来。

## AI

顶栏那个 `AI` 按钮有两种按法：

- **点一下** → 切到 AI 对话页（背景跟课表用同一张图），再点切回课表
- **长按** → 打开设置

设置里是**选一家 + 填 API Key + Soul**：

| 选哪家 | 接口地址 | 模型 |
|---|---|---|
| 混元 | `https://tokenhub.tencentmaas.com/v1` | `hy4-preview` |
| DeepSeek | `https://api.deepseek.com` | `deepseek-flash` |

几点说明：

- **第一项是混元不是 WorkBuddy**：WorkBuddy 自己不对外提供 API（它在本机开的
  那几个 `127.0.0.1` 端口只是自己的内部通信），它背后用的就是腾讯混元，
  Hy4 也是先在 WorkBuddy 上线的，所以直接接混元的接入点。
- **DeepSeek 现在只剩 `flash` 和 `v4-pro`**，旧的 `deepseek-chat` /
  `deepseek-reasoner` 已经在 2026-07 退役。

## 角色

设置里那个「角色」**不是「用哪家」**，跟模型无关 —— 它是一段**系统提示词**，
决定 AI 说话什么调调，跟 WorkBuddy 那份 `SOUL.md` 是一个意思。

存在 `AppData/soul.md`（第一次跑会写一份默认的进去），设置里有个编辑框能直接改，
改坏了点「恢复默认」。对话时它作为 system 消息发给模型。

**AI 没权限自己改它。** 它只能提一个 `{"op":"set_role","text":"…"}` 的提议，
界面会把新内容整段摆出来问用户，点「同意」才落地，点「不同意」就保持原样。

### 思考过程

带思考模式的模型（DeepSeek、混元都支持）会在 `reasoning_content` 里单独回一段
思维链。这段显示在气泡上方，**默认收起**，点一下「思考过程（展开）」才铺开。

**导课是「本地优先、AI 兜底」**：先跑本地解析器（快、准、不花钱），只有它
认不出来（比如换成别家教务系统的版式），才把按星期整理好的文字丢给模型，
让它返回 JSON。

提示词里写死了输出格式（字段名、周次怎么展开），解析响应时会**把方括号那一坨
抠出来**再读 —— 模型爱裹 ` ```json ` 或者多几句废话都无所谓。
回来的是小节号，跟 PDF 里一致，换算成大节由代码做。

### 它还能直接改你的课表

对话时会把**当前课表的摘要**（每门课带 id、老师、地点、类别、星期几、第几大节、
周次）一起塞进系统提示词，并告诉模型改课的格式。它要动手时会在回复末尾附一段
`{"ops": [...]}` 的 JSON，App 解析出来直接执行（`add` / `update` / `remove`），
**然后把那段 JSON 从气泡正文里剪掉** —— 你只会看到一句人话。

几个约定：`day` 是 1-7；`startSection` / `endSection` 填的是**大节号**（1-5，
一个大节 = 两小节）；`weeks` 是具体第几周的数组；`update` / `remove` 要带上 id。
找不到 JSON 就当普通聊天，什么都不动。

配置走 `QSettings`（Windows 上是 `HKCU\Software\pony\CourseTable`），
**不写进项目目录**，所以 Key 不会跟着代码推上 GitHub。

## 还没做的

- **学期起始日**。课表 PDF 里只有「2026-2027学年第1学期」和打印时间，
  没说第 1 周是哪天，所以现在拿「今天所在的周一」兜底，表头日期会对不上。
  等定下规则（比如「该学年 9 月第一个周一」）再自动推。
- **Android 真机验证**。APK 已经能正常编出来（见上「Android 打包」），但读系统日历那段
  只在 Windows 上跑过逻辑，没在真机上完整验证过；不同厂商日历的 `CalendarContract`
  字段可能有差异。
- Material 换肤、深色模式都没做。
