cmake_minimum_required(VERSION 3.21)

project(qtinkup VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)

qt_add_executable(qtinkup
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/tink.cpp
    src/tink.h
)

target_link_libraries(qtinkup PRIVATE Qt6::Widgets Qt6::SerialPort)

set_target_properties(qtinkup PROPERTIES
    MACOSX_BUNDLE ON
    WIN32_EXECUTABLE ON
)

enable_testing()
add_subdirectory(tests)
