add_executable(imgui-ui-tests
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

target_link_libraries(imgui-ui-tests PRIVATE imgui-ui::core Catch2::Catch2WithMain)
target_compile_definitions(imgui-ui-tests PRIVATE IMGUI_UI_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets")
target_precompile_headers(imgui-ui-tests PRIVATE <catch2/catch_test_macros.hpp> <imgui.h>)

if(MSVC)
    target_compile_options(imgui-ui-tests PRIVATE /utf-8)
endif()

if(NOT IMGUI_UI_FONT_PLATFORM STREQUAL "unsupported")
    target_compile_definitions(imgui-ui-tests PRIVATE IMGUI_UI_TEST_NATIVE_FONTS)
endif()

include(Catch)
catch_discover_tests(imgui-ui-tests DISCOVERY_MODE PRE_TEST)

if(IMGUI_UI_BUILD_SDL)
    add_executable(imgui-ui-sdl-tests
        sdl-input.cpp
    )

    target_link_libraries(imgui-ui-sdl-tests PRIVATE imgui-ui::sdl Catch2::Catch2WithMain)
    target_compile_definitions(imgui-ui-sdl-tests PRIVATE IMGUI_UI_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets")
    target_include_directories(imgui-ui-sdl-tests PRIVATE "${IMGUI_UI_IMGUI_DIR}/backends")
    target_precompile_headers(imgui-ui-sdl-tests PRIVATE <catch2/catch_test_macros.hpp> <imgui.h>)

    catch_discover_tests(
        imgui-ui-sdl-tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endif()
