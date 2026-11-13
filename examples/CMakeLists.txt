function(arbor_add_example name)
    add_executable(arbor-example-${name} ${name}/main.cpp demo.cpp demo.hpp)
    target_link_libraries(arbor-example-${name} PRIVATE arbor::${name})
    target_compile_definitions(arbor-example-${name} PRIVATE
        ARBOR_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    )
endfunction()

if(ARBOR_BUILD_SDL)
    arbor_add_example(sdl)
endif()

if(ARBOR_BUILD_RAYLIB)
    arbor_add_example(raylib)
endif()
