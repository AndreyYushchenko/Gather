cmake_minimum_required(VERSION 3.21)
project(Gather LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Svg Multimedia MultimediaWidgets)

qt_standard_project_setup()

qt_add_executable(Gather
    src/main.cpp
    src/core/ContentItem.h
    src/core/ContentItem.cpp
    src/core/Database.h
    src/core/Database.cpp
    src/core/ContentRepository.h
    src/core/ContentRepository.cpp
    src/core/BibleRepository.h
    src/core/BibleRepository.cpp
    src/core/PlaylistRepository.h
    src/core/PlaylistRepository.cpp
    src/display/SlideContent.h
    src/display/SlideContentBuilder.h
    src/display/SlideContentBuilder.cpp
    src/display/SlideRenderWidget.h
    src/display/SlideRenderWidget.cpp
    src/display/DisplayWindow.h
    src/display/DisplayWindow.cpp
    src/display/DisplayServer.h
    src/display/DisplayServer.cpp
    src/display/PresentationController.h
    src/display/PresentationController.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/DisplaySettings.h
    src/ui/DisplaySettings.cpp
    src/ui/IconProvider.h
    src/ui/IconProvider.cpp
    src/ui/Sidebar.h
    src/ui/Sidebar.cpp
    src/ui/LibraryListPanel.h
    src/ui/LibraryListPanel.cpp
    src/ui/DetailPanel.h
    src/ui/DetailPanel.cpp
    src/ui/BiblePanel.h
    src/ui/BiblePanel.cpp
    src/ui/SettingsPanel.h
    src/ui/SettingsPanel.cpp
    src/ui/PlaylistListPanel.h
    src/ui/PlaylistListPanel.cpp
    src/ui/PlaylistDetailPanel.h
    src/ui/PlaylistDetailPanel.cpp
    src/ui/VideoListPanel.h
    src/ui/VideoListPanel.cpp
    src/ui/VideoDetailPanel.h
    src/ui/VideoDetailPanel.cpp
    src/ui/ImportExportPanel.h
    src/ui/ImportExportPanel.cpp
    src/ui/HelpPanel.h
    src/ui/HelpPanel.cpp
    src/ui/DisplayControlPanel.h
    src/ui/DisplayControlPanel.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ItemEditDialog.h
    src/ui/ItemEditDialog.cpp
)

if(WIN32)
    target_sources(Gather PRIVATE src/app.rc)
endif()

target_include_directories(Gather PRIVATE src)

qt_add_resources(Gather "icons"
    PREFIX "/icons"
    BASE "resources/icons"
    FILES
        resources/icons/church.svg
        resources/icons/plus.svg
        resources/icons/music.svg
        resources/icons/book-open.svg
        resources/icons/megaphone.svg
        resources/icons/image.svg
        resources/icons/list-music.svg
        resources/icons/arrow-left-right.svg
        resources/icons/settings.svg
        resources/icons/info.svg
        resources/icons/search.svg
        resources/icons/list-filter.svg
        resources/icons/chevron-down.svg
        resources/icons/star.svg
        resources/icons/pencil.svg
        resources/icons/ellipsis-vertical.svg
        resources/icons/copy.svg
        resources/icons/trash-2.svg
        resources/icons/eye.svg
        resources/icons/monitor.svg
        resources/icons/arrow-left.svg
        resources/icons/arrow-right.svg
        resources/icons/monitor-off.svg
        resources/icons/pause.svg
        resources/icons/external-link.svg
        resources/icons/qr-code.svg
        resources/icons/link.svg
        resources/icons/check.svg
        resources/icons/flame.svg
        resources/icons/history.svg
        resources/icons/database.svg
        resources/icons/keyboard.svg
        resources/icons/wifi.svg
        resources/icons/cloud.svg
        resources/icons/palette.svg
        resources/icons/rotate-ccw.svg
        resources/icons/save.svg
        resources/icons/cloud-upload.svg
        resources/icons/file-text.svg
        resources/icons/download.svg
        resources/icons/rocket.svg
        resources/icons/mail.svg
        resources/icons/video.svg
        resources/icons/upload.svg
        resources/icons/youtube.svg
        resources/icons/play.svg
        resources/icons/repeat.svg
        resources/icons/app-256.png
)

target_link_libraries(Gather PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Svg
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

set_target_properties(Gather PROPERTIES
    WIN32_EXECUTABLE ON
)
