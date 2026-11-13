cmake_minimum_required(VERSION 3.25)
project(arbor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ARBOR_BUILD_SDL "build the SDL3/OpenGL backend" OFF)
option(ARBOR_BUILD_RAYLIB "build the raylib backend" OFF)
option(ARBOR_BUILD_TESTS "build the framework tests" ${PROJECT_IS_TOP_LEVEL})
option(ARBOR_BUILD_EXAMPLES "build the selected backend examples" OFF)
option(ARBOR_ENABLE_SANITIZERS "enable sanitizers in debug builds" OFF)

if(WIN32)
    set(ARBOR_FONT_PLATFORM windows)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    set(ARBOR_FONT_PLATFORM linux)
else()
    set(ARBOR_FONT_PLATFORM unsupported)
endif()

set(ARBOR_IMGUI_DIR "" CACHE PATH "imgui source directory, empty uses vendor/imgui")

include(cmake/sanitizers.cmake)
include(cmake/dependencies.cmake)
add_subdirectory(src)

if(ARBOR_BUILD_TESTS)
    if(NOT TARGET Catch2::Catch2WithMain)
        find_package(Catch2 3 QUIET CONFIG)
        if(NOT TARGET Catch2::Catch2WithMain)
            FetchContent_Declare(Catch2
                URL https://codeload.github.com/catchorg/Catch2/tar.gz/69e0473f6e98d47c93518424c08ee69ee632c0f0
                URL_HASH SHA256=1f3d8a4cadff975bb7f33ce71fad2f4b308d1d4a72273d29ef0abb2e42c5456a
            )
            FetchContent_MakeAvailable(Catch2)
        endif()
    endif()

    include(CTest)
    add_subdirectory(tests)
endif()

if(ARBOR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
