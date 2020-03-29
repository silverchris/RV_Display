cmake_minimum_required(VERSION 3.10)
project(lvgl)
set(CMAKE_C_STANDARD 11)#C11
set(CMAKE_CXX_STANDARD 17)#C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

file(GLOB_RECURSE INCLUDES "lv_drivers/*.h" "lvgl/*.h" "gui/*.h" "websocket.h" "ha/*.hpp" "secrets.h")
file(GLOB_RECURSE SOURCES "lv_drivers/*.c" "lvgl/*.c" "gui/*.cpp" "websocket.cpp" "ha/*.cpp")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

find_package(SDL2 REQUIRED SDL2)
include_directories(${SDL2_INCLUDE_DIRS})
find_package(Libwebsockets CONFIG REQUIRED)
include_directories(${LIB_WEBSOCKETS_INCLUDE_DIRS})

include_directories(ArduinoJson/src)


add_executable(main main.c mouse_cursor_icon.c Symbols.c ${SOURCES} ${INCLUDES})
target_link_libraries(main PRIVATE SDL2 websockets)
add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main)
