cmake_minimum_required(VERSION 3.16)

project(SHS_Logistics VERSION 0.8 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network PrintSupport)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network PrintSupport)

set(PROJECT_SOURCES
    main.cpp
    mainwindowshslogistics.cpp
    mainwindowshslogistics.h
    mainwindowshslogistics.ui
    railmapwidget.cpp
    railmapwidget.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SHS_Logistics
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ../SHSIcon.png
        ../SHS_Logo.png
        SHSRes.qrc
    )
else()
    add_executable(SHS_Logistics
        ${PROJECT_SOURCES}
        SHSRes.qrc
    )
endif()

target_link_libraries(SHS_Logistics PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
target_include_directories(SHS_Logistics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Zentrale Versionsnummer (Splashscreen, Titelleiste, Info-Dialog)
target_compile_definitions(SHS_Logistics PRIVATE SHS_APP_VERSION="${PROJECT_VERSION}")

set_target_properties(SHS_Logistics PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS SHS_Logistics
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SHS_Logistics)
endif()

