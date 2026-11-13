function(arbor_add_backend name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBS" ${ARGN})

    add_library(arbor-${name} STATIC ${ARG_SOURCES})
    add_library(arbor::${name} ALIAS arbor-${name})
    target_precompile_headers(arbor-${name} PRIVATE <imgui.h>)

    target_include_directories(arbor-${name}
        PRIVATE "${ARBOR_IMGUI_DIR}/backends"
        PUBLIC "$<TARGET_PROPERTY:arbor::glad,INTERFACE_INCLUDE_DIRECTORIES>"
    )
    target_link_libraries(arbor-${name} PUBLIC
        arbor-core
        arbor::lunasvg
        arbor::nsgif
        plutovg::plutovg
        OpenGL::GL
        ${ARG_LINK_LIBS}
    )
endfunction()

# core must build without opengl. compile these effect implementations into each enabled backend instead.
file(GLOB_RECURSE ARBOR_CORE_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER ARBOR_CORE_SOURCES EXCLUDE REGEX "/backends/")
list(FILTER ARBOR_CORE_SOURCES EXCLUDE REGEX "/resources/fonts/(linux|windows|unsupported)\\.cpp$")
list(APPEND ARBOR_CORE_SOURCES "resources/fonts/${ARBOR_FONT_PLATFORM}.cpp")

file(GLOB_RECURSE ARBOR_OPENGL_EFFECT_SOURCES CONFIGURE_DEPENDS "imgui/effects/opengl*.cpp")
list(REMOVE_ITEM ARBOR_CORE_SOURCES ${ARBOR_OPENGL_EFFECT_SOURCES})

set(ARBOR_OPENGL_BACKEND_SOURCES
    ${ARBOR_OPENGL_EFFECT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/texture-loader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/gpu-timer.cpp"
    "${ARBOR_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

add_library(arbor-core STATIC ${ARBOR_CORE_SOURCES})
add_library(arbor::core ALIAS arbor-core)
if(ARBOR_ENABLE_SANITIZERS)
    arbor_enable_sanitizers(arbor-core)
endif()
target_precompile_headers(arbor-core PRIVATE <imgui.h>)

target_compile_features(arbor-core PUBLIC cxx_std_23)
target_include_directories(arbor-core
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(arbor-core PUBLIC arbor::imgui)
target_link_libraries(arbor-core PRIVATE arbor::nfd)

if(MSVC)
    target_compile_options(arbor-core PRIVATE /utf-8)
endif()

if(ARBOR_FONT_PLATFORM STREQUAL "linux")
    find_package(Fontconfig 2.12 REQUIRED)
    target_link_libraries(arbor-core PRIVATE Fontconfig::Fontconfig)
elseif(ARBOR_FONT_PLATFORM STREQUAL "windows")
    target_link_libraries(arbor-core PRIVATE dwrite user32)
endif()

if(WIN32)
    target_compile_definitions(arbor-core PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        VC_EXTRALEAN
    )
endif()

if(ARBOR_BUILD_SDL)
    file(GLOB ARBOR_SDL_SOURCES CONFIGURE_DEPENDS "backends/sdl/*.cpp")

    arbor_add_backend(sdl
        SOURCES
            ${ARBOR_SDL_SOURCES}
            ${ARBOR_OPENGL_BACKEND_SOURCES}
            "${ARBOR_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        LINK_LIBS SDL3::SDL3 arbor::glad
    )
endif()

if(ARBOR_BUILD_RAYLIB)
    file(GLOB ARBOR_RAYLIB_SOURCES CONFIGURE_DEPENDS "backends/raylib/*.cpp")

    # raylib defines the glad symbols. linking glad here would define them twice.
    arbor_add_backend(raylib
        SOURCES ${ARBOR_RAYLIB_SOURCES} ${ARBOR_OPENGL_BACKEND_SOURCES}
        LINK_LIBS raylib
    )
endif()
