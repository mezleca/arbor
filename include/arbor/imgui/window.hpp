#pragma once

#include "../layout/geometry.hpp"

namespace ui {
    /// call after begin with windowpadding set to insets.window_padding().
    void apply_window_insets(BoxInsets insets);
} // namespace ui
