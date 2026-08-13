find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_tink
    test_tink.cpp
    ${CMAKE_SOURCE_DIR}/src/tink.cpp
    ${CMAKE_SOURCE_DIR}/src/tink.h
)
target_include_directories(test_tink PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tink PRIVATE Qt6::Test Qt6::SerialPort)
add_test(NAME test_tink COMMAND test_tink)
