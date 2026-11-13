if(NOT ARBOR_ENABLE_SANITIZERS)
    return()
endif()

# msvc address sanitizer cannot be combined with /rtc or /zi edit-and-continue flags.
if(MSVC)
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REGEX REPLACE "/RTC[1su]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

function(arbor_enable_sanitizers target)
    if(MSVC)
        target_compile_options(${target} PUBLIC "$<$<CONFIG:Debug>:/fsanitize=address>")
        # dependencies are not built with asan. disable stl container annotations across their link boundary.
        target_compile_definitions(${target} PUBLIC
            "$<$<CONFIG:Debug>:_DISABLE_STRING_ANNOTATION>"
            "$<$<CONFIG:Debug>:_DISABLE_VECTOR_ANNOTATION>"
            "$<$<CONFIG:Debug>:_DISABLE_OPTIONAL_ANNOTATION>"
        )
        target_link_options(${target} PUBLIC "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    else()
        target_compile_options(${target} PUBLIC
            "$<$<CONFIG:Debug>:-fsanitize=address,undefined>"
            "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
        )
        target_link_options(${target} PUBLIC "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
    endif()
endfunction()
