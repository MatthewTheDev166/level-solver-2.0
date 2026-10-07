cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(level-solver-2.0 VERSION 2.0.0)

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

# Find Geode SDK
find_package(Geode REQUIRED)

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.hpp")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE imgui-cocos)

setup_geode_mod(${PROJECT_NAME})

