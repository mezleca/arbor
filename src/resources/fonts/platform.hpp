#pragma once

#include <imgui-ui/resources/font-registry.hpp>

namespace ui {
    std::vector<FontFace> enumerate_system_fonts();
    std::vector<std::string> system_font_preferences(std::string_view language);
    FontFamilyId font_family_id(std::string_view name);
    /// rewrites variable-font face indices to the closest named instance for the requested style
    void
    resolve_font_variants(std::vector<FontSource>& sources, std::optional<FontWeight> weight, std::optional<FontSlant> slant);
} // namespace ui
