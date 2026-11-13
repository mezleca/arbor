#pragma once

#include <cstdint>

namespace ui {
    enum class BoxSizing : uint8_t {
        /// fixed and percentage sizes apply to the content before padding and borders.
        ContentBox,

        /// fixed and percentage sizes include padding and borders in the final box.
        BorderBox,
    };
} // namespace ui
