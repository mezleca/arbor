#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace ui {
    struct StringHash {
        /// enables heterogeneous lookup so find(string_view) avoids a temporary string.
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };
} // namespace ui
