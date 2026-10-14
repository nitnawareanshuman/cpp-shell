
cmake_minimum_required(VERSION 3.20)

project(shell-starter-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ftxui CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(shell
    src/main.cpp
    src/ui/desktop_ui.cpp
)

target_include_directories(shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(shell PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    Qt6::Widgets
)
