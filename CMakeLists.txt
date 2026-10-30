cmake_minimum_required(VERSION 3.20)

project(cpp-shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(shell_engine STATIC
    src/core/lexer.cpp
    src/core/parser.cpp
    src/core/expander.cpp
    src/core/path.cpp
    src/core/state.cpp
    src/core/executor.cpp
    src/core/line_editor.cpp
    src/jobs/jobs.cpp
    src/history/history.cpp
    src/completion/completion.cpp
)

target_include_directories(shell_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
    target_compile_options(shell_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cpp-shell
    src/main.cpp
    src/ui/dashboard.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
    target_compile_options(cpp-shell PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(cpp-shell PRIVATE
    shell_engine
    Qt6::Widgets
)

enable_testing()

add_executable(shell_unit_tests
    tests/unit/unit_tests.cpp
)
target_include_directories(shell_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(shell_unit_tests PRIVATE shell_engine)
add_test(NAME shell_unit_tests COMMAND shell_unit_tests)

add_executable(shell_integration_tests
    tests/integration/integration_tests.cpp
)
target_include_directories(shell_integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(shell_integration_tests PRIVATE shell_engine)
target_compile_definitions(shell_integration_tests PRIVATE CPP_SHELL_BIN="$<TARGET_FILE:cpp-shell>")
add_dependencies(shell_integration_tests cpp-shell)
add_test(NAME shell_integration_tests COMMAND shell_integration_tests)
