include(FetchContent)

if(ARBOR_IMGUI_DIR)
    get_filename_component(ARBOR_IMGUI_DIR "${ARBOR_IMGUI_DIR}" ABSOLUTE)
    if(NOT EXISTS "${ARBOR_IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR "ARBOR_IMGUI_DIR must point to an imgui source directory containing imgui.h")
    endif()
else()
    set(ARBOR_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")
endif()

# core compiles the native file dialog adapter even when both render backends are disabled.
if(NOT TARGET arbor::nfd)
    if(NOT TARGET nfd AND NOT TARGET nfd::nfd)
        find_package(nfd QUIET CONFIG)
        if(NOT TARGET nfd::nfd)
            set(NFD_BUILD_TESTS OFF CACHE BOOL "build nfd tests")
            set(NFD_INSTALL OFF CACHE BOOL "install nfd")

            # nfd's wayland build reads protocol xml from its nested submodule, absent from source archives.
            FetchContent_Declare(nfd
                GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
                GIT_TAG 7bbbd9fe6b1d1549b41df138f614d1a44df9ba08
                GIT_SUBMODULES 3ps/wayland-protocols
            )
            FetchContent_MakeAvailable(nfd)
        endif()
    endif()

    if(TARGET nfd)
        add_library(arbor::nfd ALIAS nfd)
    else()
        add_library(arbor::nfd ALIAS nfd::nfd)
    endif()
endif()

# building imgui here includes its freetype renderer, which requires the freetype headers and library.
if(NOT TARGET arbor::imgui)
    if(NOT TARGET arbor-imgui)
        if(NOT TARGET Freetype::Freetype AND NOT TARGET freetype)
            find_package(Freetype QUIET)
            if(NOT TARGET Freetype::Freetype)
                FetchContent_Declare(freetype
                    URL https://codeload.github.com/freetype/freetype/tar.gz/0a0221a1347e2f1e07c395263540026e9a0aa7c7
                    URL_HASH SHA256=11cd478953fc1d382f20a233b8e2aed6c31b47cbf0568c4bdb3334bcc1550698
                )
                FetchContent_MakeAvailable(freetype)
            endif()
        endif()

        if(NOT TARGET Freetype::Freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        endif()

        add_library(arbor-imgui STATIC
            "${ARBOR_IMGUI_DIR}/imgui.cpp"
            "${ARBOR_IMGUI_DIR}/imgui_draw.cpp"
            "${ARBOR_IMGUI_DIR}/imgui_tables.cpp"
            "${ARBOR_IMGUI_DIR}/imgui_widgets.cpp"
            "${ARBOR_IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp"
            "${ARBOR_IMGUI_DIR}/misc/freetype/imgui_freetype.cpp"
        )

        target_include_directories(arbor-imgui PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include/arbor/imgui"
            "${ARBOR_IMGUI_DIR}"
            "${ARBOR_IMGUI_DIR}/misc/cpp"
        )

        target_compile_definitions(arbor-imgui PUBLIC IMGUI_ENABLE_FREETYPE PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
        target_link_libraries(arbor-imgui PUBLIC Freetype::Freetype)
        target_precompile_headers(arbor-imgui PRIVATE
            "${ARBOR_IMGUI_DIR}/imgui_internal.h"
        )
    endif()

    add_library(arbor::imgui ALIAS arbor-imgui)
endif()

# core does not compile the opengl effects or texture loaders below.
if(NOT ARBOR_BUILD_SDL AND NOT ARBOR_BUILD_RAYLIB)
    return()
endif()

if(NOT TARGET arbor::glad)
    if(NOT TARGET glad)
        FetchContent_Declare(glad
            URL https://codeload.github.com/mezleca/glad/tar.gz/987653e300a5575e063983bf63ad938285684ac9
            URL_HASH SHA256=c7f3ec28b4ca5a4f456651bdfff9f2a380c60c5596e4084e80007712597230cb
        )
        FetchContent_MakeAvailable(glad)
    endif()

    add_library(arbor::glad ALIAS glad)
endif()

find_package(OpenGL REQUIRED)

# libnsgif has no cmake target. compile its decoder from gif.c and lzw.c.
if(NOT TARGET nsgif)
    add_library(nsgif STATIC
        vendor/libnsgif/src/gif.c
        vendor/libnsgif/src/lzw.c
    )
    target_include_directories(nsgif PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libnsgif/include")
endif()

if(NOT TARGET arbor::nsgif)
    add_library(arbor::nsgif ALIAS nsgif)
endif()

if(NOT TARGET arbor::lunasvg)
    if(NOT TARGET lunasvg AND NOT TARGET lunasvg::lunasvg)
        find_package(lunasvg 3.5 QUIET CONFIG)
        if(NOT TARGET lunasvg::lunasvg)
            set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "build lunasvg examples")

            # mark plutovg as found so lunasvg does not recreate the parent's target.
            if(TARGET plutovg::plutovg)
                set(plutovg_FOUND TRUE)
            endif()

            FetchContent_Declare(lunasvg
                URL https://codeload.github.com/sammycage/lunasvg/tar.gz/2872affa1027cad92a05408a7e6f2547efa7f364
                URL_HASH SHA256=a1e110c2e7cf798a3ceccde8eb70e35accab4ef306d2a1d611ca1ee75e101eaa
            )
            FetchContent_MakeAvailable(lunasvg)
        endif()
    endif()

    if(TARGET lunasvg)
        add_library(arbor::lunasvg ALIAS lunasvg)
    else()
        add_library(arbor::lunasvg ALIAS lunasvg::lunasvg)
    endif()
endif()

# the raster loader calls plutovg directly, even when lunasvg comes from the parent.
if(NOT TARGET plutovg::plutovg)
    find_package(plutovg 1.3 REQUIRED CONFIG)
endif()

# downloading sdl is the fallback after both cmake packages and pkg-config miss the system installation.
if(ARBOR_BUILD_SDL AND NOT TARGET SDL3::SDL3)
    find_package(SDL3 3.2 QUIET CONFIG)

    if(NOT TARGET SDL3::SDL3)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL3_SYSTEM QUIET IMPORTED_TARGET sdl3>=3.2)

            if(SDL3_SYSTEM_FOUND)
                add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3_SYSTEM)
            endif()
        endif()
    endif()

    if(NOT TARGET SDL3::SDL3)
        set(SDL_SHARED OFF CACHE BOOL "build sdl shared library")
        set(SDL_STATIC ON CACHE BOOL "build sdl static library")
        set(SDL_VULKAN OFF CACHE BOOL "enable sdl vulkan support")
        set(SDL_EXAMPLES OFF CACHE BOOL "build sdl examples")
        set(SDL_TESTS OFF CACHE BOOL "build sdl tests")
        FetchContent_Declare(SDL3
            URL https://codeload.github.com/libsdl-org/SDL/tar.gz/452c3634d4cda3dfaf4e8e419da65bc5d7b78959
            URL_HASH SHA256=8ac8aceeae819af403f805ae27ef08ea2c4cc49b987008ef5d23202238da80ec
        )
        FetchContent_MakeAvailable(SDL3)
    endif()
endif()

if(ARBOR_BUILD_RAYLIB AND NOT TARGET raylib)
    find_package(raylib 6.0 QUIET CONFIG)
    if(NOT TARGET raylib)
        if(UNIX AND NOT APPLE)
            set(GLFW_BUILD_WAYLAND ON CACHE BOOL "build glfw wayland support")
        endif()

        set(BUILD_EXAMPLES OFF CACHE BOOL "build raylib examples")
        FetchContent_Declare(raylib
            URL https://codeload.github.com/raysan5/raylib/tar.gz/dbc56a87da87d973a9c5baa4e7438a9d20121d28
            URL_HASH SHA256=81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126
        )
        FetchContent_MakeAvailable(raylib)
    endif()
endif()
