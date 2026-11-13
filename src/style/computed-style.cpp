#include <arbor/style/computed-style.hpp>
#include <arbor/style/theme.hpp>

#include <imgui.h>

using namespace ui;

BoxInsets ComputedStyle::border_widths() const {
    return {
        m_border_types.left == BorderType::None ? 0.0F : m_border_left_size.value,
        m_border_types.top == BorderType::None ? 0.0F : m_border_top_size.value,
        m_border_types.right == BorderType::None ? 0.0F : m_border_right_size.value,
        m_border_types.bottom == BorderType::None ? 0.0F : m_border_bottom_size.value,
    };
}

uint8_t ComputedStyle::border() const {
    const BoxInsets widths = border_widths();
    return (widths.left > 0.0F ? BORDER_LEFT : 0) | (widths.top > 0.0F ? BORDER_TOP : 0) |
           (widths.right > 0.0F ? BORDER_RIGHT : 0) | (widths.bottom > 0.0F ? BORDER_BOTTOM : 0);
}

bool ComputedStyle::uniform_border() const {
    const BoxInsets widths = border_widths();
    return widths.left > 0.0F && widths.left == widths.top && widths.left == widths.right && widths.left == widths.bottom &&
           m_border_types.left == m_border_types.top && m_border_types.left == m_border_types.right &&
           m_border_types.left == m_border_types.bottom;
}

ComputedStyle::ComputedStyle() {
    const Theme theme{};
    m_color.set(theme.text_color);
    m_border_color.set(theme.border_color);
    m_background_color.set(theme.transparent);
    m_scrollbar_background_color.set(theme.scrollbar.background_color);
    m_scrollbar_grab_color.set(theme.scrollbar.grab_color);
    m_scrollbar_grab_hovered_color.set(theme.scrollbar.grab_hovered_color);
    m_scrollbar_grab_active_color.set(theme.scrollbar.grab_active_color);
}

PushState ComputedStyle::push(float opacity, ImFont* effective_font, float effective_font_size) const {
    ImGuiStyle& current = ImGui::GetStyle();
    const float scale = dpi_scale();
    PushState state;

    state.font_pushed = effective_font != ImGui::GetFont() || effective_font_size != current.FontSizeBase;
    if (state.font_pushed) ImGui::PushFont(effective_font, effective_font_size);

    const auto push_var = [&state](ImGuiStyleVar variable, auto value) {
        ImGui::PushStyleVar(variable, value);
        ++state.variables;
    };

    const auto push_color = [&state](ImGuiCol color, ImVec4 value) {
        ImGui::PushStyleColor(color, value);
        ++state.colors;
    };

    const ImVec2 padding = (m_padding.value * scale).window_padding();
    if (current.FramePadding.x != padding.x || current.FramePadding.y != padding.y) {
        push_var(ImGuiStyleVar_FramePadding, padding);
    }

    if (current.FrameRounding != m_border_radius * scale) push_var(ImGuiStyleVar_FrameRounding, m_border_radius * scale);
    if (current.FrameBorderSize != 0.0F) push_var(ImGuiStyleVar_FrameBorderSize, 0.0F);
    if (current.ScrollbarSize != m_scrollbar_size * scale) push_var(ImGuiStyleVar_ScrollbarSize, m_scrollbar_size * scale);
    if (current.ScrollbarRounding != m_scrollbar_rounding * scale) {
        push_var(ImGuiStyleVar_ScrollbarRounding, m_scrollbar_rounding * scale);
    }
    if (current.GrabMinSize != m_scrollbar_minimum_grab_size * scale) {
        push_var(ImGuiStyleVar_GrabMinSize, m_scrollbar_minimum_grab_size * scale);
    }
    if (current.GrabRounding != m_scrollbar_grab_rounding * scale) {
        push_var(ImGuiStyleVar_GrabRounding, m_scrollbar_grab_rounding * scale);
    }

    const float alpha = current.Alpha * m_alpha * opacity;
    if (current.Alpha != alpha) push_var(ImGuiStyleVar_Alpha, alpha);

    const ImVec4 background = m_background_color.get();
    push_color(ImGuiCol_Text, m_color.get());
    push_color(ImGuiCol_Border, m_border_color.get());
    push_color(ImGuiCol_FrameBg, background);
    push_color(ImGuiCol_FrameBgHovered, background);
    push_color(ImGuiCol_FrameBgActive, background);
    push_color(ImGuiCol_Button, background);
    push_color(ImGuiCol_ButtonHovered, background);
    push_color(ImGuiCol_ButtonActive, background);
    push_color(ImGuiCol_ScrollbarBg, m_scrollbar_background_color.get());
    push_color(ImGuiCol_ScrollbarGrab, m_scrollbar_grab_color.get());
    push_color(ImGuiCol_ScrollbarGrabHovered, m_scrollbar_grab_hovered_color.get());
    push_color(ImGuiCol_ScrollbarGrabActive, m_scrollbar_grab_active_color.get());
    return state;
}

void ComputedStyle::pop(PushState state) {
    // unchanged values were never pushed. restore only the entries recorded by this style pass.
    ImGui::PopStyleColor(state.colors);
    ImGui::PopStyleVar(state.variables);
    if (state.font_pushed) ImGui::PopFont();
}
