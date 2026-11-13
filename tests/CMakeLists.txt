add_executable(arbor-tests
    core.cpp
    diagnostics.cpp
    file-dialog.cpp
    input.cpp
    layout.cpp
    widgets.cpp
    texture-cache.cpp
    fonts.cpp
    imgui-context.hpp
)

target_link_libraries(arbor-tests PRIVATE arbor::core Catch2::Catch2WithMain)
target_compile_definitions(arbor-tests PRIVATE ARBOR_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets")
target_precompile_headers(arbor-tests PRIVATE <catch2/catch_test_macros.hpp> <imgui.h>)

if(MSVC)
    target_compile_options(arbor-tests PRIVATE /utf-8)
endif()

if(NOT ARBOR_FONT_PLATFORM STREQUAL "unsupported")
    target_compile_definitions(arbor-tests PRIVATE ARBOR_TEST_NATIVE_FONTS)
endif()

include(Catch)
catch_discover_tests(arbor-tests DISCOVERY_MODE PRE_TEST)

if(ARBOR_BUILD_SDL)
    add_executable(arbor-sdl-tests
        sdl-input.cpp
    )

    target_link_libraries(arbor-sdl-tests PRIVATE arbor::sdl Catch2::Catch2WithMain)
    target_compile_definitions(arbor-sdl-tests PRIVATE ARBOR_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets")
    target_include_directories(arbor-sdl-tests PRIVATE "${ARBOR_IMGUI_DIR}/backends")
    target_precompile_headers(arbor-sdl-tests PRIVATE <catch2/catch_test_macros.hpp> <imgui.h>)

    catch_discover_tests(
        arbor-sdl-tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endif()
