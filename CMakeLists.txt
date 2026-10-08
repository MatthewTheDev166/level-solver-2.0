cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(level-solver-2.0 VERSION 2.0.0)

# Geode SDK setup
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# CPM Package Manager setup
include(cmake/CPM.cmake OPTIONAL)
if (NOT COMMAND CPMAddPackage)
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.7/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    )
    include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)
endif()

# Include gd-imgui-cocos
CPMAddPackage("gh:matcool/gd-imgui-cocos#b93f08ccef778a53ebba09b20c347f6a63980119")

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.hpp")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Link dependencies
target_include_directories(${PROJECT_NAME} PRIVATE include)

target_link_libraries(${PROJECT_NAME} imgui-cocos)

setup_geode_mod(${PROJECT_NAME})


