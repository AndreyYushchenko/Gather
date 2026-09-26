cmake_minimum_required(VERSION 3.21)
project(Sermon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Svg Multimedia MultimediaWidgets LinguistTools)

qt_standard_project_setup()

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.h")

qt_add_executable(Sermon ${SRC_FILES})

if(WIN32)
    target_sources(Sermon PRIVATE src/app.rc)
endif()

target_include_directories(Sermon PRIVATE src)

# Globbed (not listed by hand): a hand-kept list silently dropped icons
# added later (timer, x, grip, volume, …) and they drew as empty space.
file(GLOB ICON_FILES CONFIGURE_DEPENDS "resources/icons/*.svg" "resources/icons/*.png")
qt_add_resources(Sermon "icons"
    PREFIX "/icons"
    BASE "resources/icons"
    FILES ${ICON_FILES}
)

# Built-in slide/timer backgrounds (":/backgrounds/…", "builtin:<name>").
file(GLOB BACKGROUND_FILES CONFIGURE_DEPENDS "resources/backgrounds/*")
qt_add_resources(Sermon "backgrounds"
    PREFIX "/backgrounds"
    BASE "resources/backgrounds"
    FILES ${BACKGROUND_FILES}
)

# Interface languages (Настройки → Язык): Ukrainian and English from the
# .ts sources, plus Qt's own dialog strings.
qt_add_lrelease(TS_FILES translations/sermon_uk.ts translations/sermon_en.ts
    QM_FILES_OUTPUT_VARIABLE SERMON_QM_FILES)
qt_add_resources(Sermon "i18n"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${SERMON_QM_FILES}
)
qt_add_resources(Sermon "i18n_qt"
    PREFIX "/i18n"
    BASE "translations"
    FILES translations/qtbase_uk.qm translations/qtbase_ru.qm translations/qtbase_en.qm
)

target_link_libraries(Sermon PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Svg
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)


set_target_properties(Sermon PROPERTIES
    WIN32_EXECUTABLE ON
)
