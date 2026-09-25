# Focused tests of production parsers, OAuth, and lot metadata. No network or keys.
cmake_minimum_required(VERSION 3.19)
project(BrickOwlTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Test)
find_package(QCoro6 REQUIRED COMPONENTS Core Network)
add_executable(brickowl-tests
    tst_brickowl.cpp
    ../../src/common/brickowl.cpp
    ../../src/common/bricklinkstoreapi.cpp
    ../../src/bricklink/lot.cpp
    ../../src/utility/exception.cpp)
target_compile_definitions(brickowl-tests PRIVATE QT_USE_QSTRINGBUILDER QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_include_directories(brickowl-tests PRIVATE ../../src)
target_link_libraries(brickowl-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Test QCoro6::Core QCoro6::Network)
# These focused Linux tests do not link unused application/catalogue/network paths.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_compile_options(brickowl-tests PRIVATE -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
    target_link_options(brickowl-tests PRIVATE -Wl,--gc-sections)
endif()
enable_testing()
add_test(NAME brickowl-fixtures COMMAND brickowl-tests)
