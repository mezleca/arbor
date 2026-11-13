#pragma once

#include "widget.hpp"

#include <string>
#include <utility>

namespace ui {
    class BoxWidget : public Widget {
    public:
        explicit BoxWidget(std::string id = {}, LayoutSize size = {fit(), fit()})
            : Widget(std::move(id), "Box", InputMode::None) {
            set_size(size);
        }
    };
} // namespace ui
