cmake_minimum_required(VERSION 3.21)

project(qtinkup VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)

if(APPLE)
    set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/icons/qtinkup.icns")
    set_source_files_properties(${app_icon_macos} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()

qt_add_executable(qtinkup
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/tink.cpp
    src/tink.h
    ${app_icon_macos}
)

target_link_libraries(qtinkup PRIVATE Qt6::Widgets Qt6::SerialPort)

set_target_properties(qtinkup PROPERTIES
    MACOSX_BUNDLE ON
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE_ICON_FILE qtinkup.icns
)

enable_testing()
add_subdirectory(tests)
