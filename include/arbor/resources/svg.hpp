#pragma once

#include <string_view>

inline constexpr std::string_view DEFAULT_WARN_SVG = R"(
    <svg
        xmlns="http://www.w3.org/2000/svg"
        width="800px"
        height="800px"
        viewBox="0 0 25 25"
        fill="none"
    >
        <path d="M12.5 10V14M12.5 17V15.5M14.2483 5.64697L20.8493 17.5287C21.5899 18.8618 20.6259 20.5 19.101 20.5H5.89903C4.37406 20.5 3.41013 18.8618 4.15072 17.5287L10.7517 5.64697C11.5137 4.27535 13.4863 4.27535 14.2483 5.64697Z" stroke="#ffffff" stroke-width="1.2"/>
    </svg>)";

inline constexpr std::string_view INSPECT_SVG = R"(
    <svg
        xmlns="http://www.w3.org/2000/svg"
        width="24"
        height="24"
        viewBox="0 0 24 24"
        fill="none"
        stroke="white"
        stroke-width="2"
        stroke-linecap="round"
        stroke-linejoin="round">
        <path d="M19 11V4a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2h7" />
        <path d="M12 12l4.166 10 1.48-4.355L22 16.166 12 12z" />
        <path d="M18 18l3 3" />
    </svg>)";

inline constexpr std::string_view CLOSE_SVG = R"(
    <svg
        xmlns="http://www.w3.org/2000/svg"
        width="24"
        height="24"
        viewBox="0 0 24 24"
        fill="none"
        stroke="white"
        stroke-width="2"
        stroke-linecap="round"
        stroke-linejoin="round">
        <path d="M6 6l12 12" />
        <path d="M18 6L6 18" />
    </svg>)";

inline constexpr std::string_view CONTEXT_MENU_CHEVRON_SVG = R"(
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M4 10L8 6L12 10" stroke="white" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
    </svg>)";
