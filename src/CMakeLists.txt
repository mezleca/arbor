function(imgui_ui_add_backend name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBS" ${ARGN})

    add_library(imgui-ui-${name} STATIC ${ARG_SOURCES})
    add_library(imgui-ui::${name} ALIAS imgui-ui-${name})
    target_precompile_headers(imgui-ui-${name} PRIVATE <imgui.h>)

    target_include_directories(imgui-ui-${name}
        PRIVATE "${IMGUI_UI_IMGUI_DIR}/backends"
        PUBLIC "$<TARGET_PROPERTY:imgui-ui::glad,INTERFACE_INCLUDE_DIRECTORIES>"
    )
    target_link_libraries(imgui-ui-${name} PUBLIC
        imgui-ui-core
        imgui-ui::lunasvg
        imgui-ui::nsgif
        plutovg::plutovg
        OpenGL::GL
        ${ARG_LINK_LIBS}
    )
endfunction()

# core must build without opengl. compile these effect implementations into each enabled backend instead.
file(GLOB_RECURSE IMGUI_UI_CORE_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER IMGUI_UI_CORE_SOURCES EXCLUDE REGEX "/backends/")
list(FILTER IMGUI_UI_CORE_SOURCES EXCLUDE REGEX "/resources/fonts/(linux|windows|unsupported)\\.cpp$")
list(APPEND IMGUI_UI_CORE_SOURCES "resources/fonts/${IMGUI_UI_FONT_PLATFORM}.cpp")

file(GLOB_RECURSE IMGUI_UI_OPENGL_EFFECT_SOURCES CONFIGURE_DEPENDS "imgui/effects/opengl*.cpp")
list(REMOVE_ITEM IMGUI_UI_CORE_SOURCES ${IMGUI_UI_OPENGL_EFFECT_SOURCES})

set(IMGUI_UI_OPENGL_BACKEND_SOURCES
    ${IMGUI_UI_OPENGL_EFFECT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/texture-loader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/gpu-timer.cpp"
    "${IMGUI_UI_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

add_library(imgui-ui-core STATIC ${IMGUI_UI_CORE_SOURCES})
add_library(imgui-ui::core ALIAS imgui-ui-core)
if(IMGUI_UI_ENABLE_SANITIZERS)
    imgui_ui_enable_sanitizers(imgui-ui-core)
endif()
target_precompile_headers(imgui-ui-core PRIVATE <imgui.h>)

target_compile_features(imgui-ui-core PUBLIC cxx_std_23)
target_include_directories(imgui-ui-core
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(imgui-ui-core PUBLIC imgui-ui::imgui)
target_link_libraries(imgui-ui-core PRIVATE imgui-ui::nfd)

if(MSVC)
    target_compile_options(imgui-ui-core PRIVATE /utf-8)
endif()

if(IMGUI_UI_FONT_PLATFORM STREQUAL "linux")
    find_package(Fontconfig 2.12 REQUIRED)
    target_link_libraries(imgui-ui-core PRIVATE Fontconfig::Fontconfig)
elseif(IMGUI_UI_FONT_PLATFORM STREQUAL "windows")
    target_link_libraries(imgui-ui-core PRIVATE dwrite user32)
endif()

if(WIN32)
    target_compile_definitions(imgui-ui-core PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        VC_EXTRALEAN
    )
endif()

if(IMGUI_UI_BUILD_SDL)
    file(GLOB IMGUI_UI_SDL_SOURCES CONFIGURE_DEPENDS "backends/sdl/*.cpp")

    imgui_ui_add_backend(sdl
        SOURCES
            ${IMGUI_UI_SDL_SOURCES}
            ${IMGUI_UI_OPENGL_BACKEND_SOURCES}
            "${IMGUI_UI_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        LINK_LIBS SDL3::SDL3 imgui-ui::glad
    )
endif()

if(IMGUI_UI_BUILD_RAYLIB)
    file(GLOB IMGUI_UI_RAYLIB_SOURCES CONFIGURE_DEPENDS "backends/raylib/*.cpp")

    # raylib defines the glad symbols. linking glad here would define them twice.
    imgui_ui_add_backend(raylib
        SOURCES ${IMGUI_UI_RAYLIB_SOURCES} ${IMGUI_UI_OPENGL_BACKEND_SOURCES}
        LINK_LIBS raylib
    )
endif()
