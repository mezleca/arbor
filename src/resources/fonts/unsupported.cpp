#include "platform.hpp"

std::vector<ui::FontFace> ui::enumerate_system_fonts() {
    return {};
}

std::vector<std::string> ui::system_font_preferences(std::string_view) {
    return {};
}
