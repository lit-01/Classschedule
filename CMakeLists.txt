cmake_minimum_required(VERSION 3.21)

project(CourseTable VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 的 qrc 资源必须真的被编进二进制。AUTORCC 默认是关的，
# 而且必须写在 qt_add_executable 之前才生效。
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(CourseTable
    main.cpp
    src/coursemodel.h
    src/coursemodel.cpp
    src/calendardata.h
    src/calendardata.cpp
)

set(qml_sources
    qml/Main.qml
    qml/CourseGrid.qml
    qml/CourseCard.qml
    qml/CourseEditor.qml
)

# 让 QML 文件落在 QML 模块的根目录下，
# 这样 engine.loadFromModule("CourseTable", "Main") 才能按类型名找到它。
foreach(qml_file ${qml_sources})
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_qml_module(CourseTable
    URI CourseTable
    VERSION 1.0
    QML_FILES ${qml_sources}
)

target_link_libraries(CourseTable PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2 # 导课按钮要用系统文件选择器
)

# 顶部横幅 + 课表区背景，编进 qrc，QML 里用 qrc:/assets/xxx.jpg 取。
# -no-compress 是明说「别动这个文件」：rcc 默认会拿 zlib 过一遍，
# 对 JPEG 来说压不动、纯属白跑，直接原样存还能零拷贝加载。
qt_add_resources(CourseTable "app_assets"
    PREFIX "/"
    FILES
        assets/topbar.jpg
        assets/tablebg.jpg
    OPTIONS -no-compress
)

set_target_properties(CourseTable PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# ---------- Android ----------
if(ANDROID)
    # android/ 下是手写的打包模板：
    #   AndroidManifest.xml                        —— 在 Qt 官方模板基础上加了 READ_CALENDAR
    #   src/com/pony/coursetable/HolidayReader.java —— 读系统日历里的法定节假日 / 调休
    set_property(TARGET CourseTable PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set_property(TARGET CourseTable PROPERTY QT_ANDROID_PACKAGE_NAME
        "com.pony.coursetable")
    set_property(TARGET CourseTable PROPERTY QT_ANDROID_VERSION_NAME "0.1")
    set_property(TARGET CourseTable PROPERTY QT_ANDROID_VERSION_CODE 1)
endif()
