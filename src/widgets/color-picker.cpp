#include <imgui-ui/widgets/color-picker.hpp>

#include <imgui-ui/imgui/painter.hpp>
#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/text-input.hpp>
#include <imgui-ui/widgets/text.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <imgui.h>
#include <string>
#include <utility>

using namespace ui;

static float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

static bool active_area(const char* id, Rect rect) {
    ImGui::SetCursorScreenPos(rect.min);
    ImGui::InvisibleButton(id, rect.size());
    return ImGui::IsItemActive();
}

static void draw_picker_gradient(
    ImDrawList& draw_list, Rect rect, ImVec4 top_left, ImVec4 top_right, ImVec4 bottom_right, ImVec4 bottom_left
) {
    draw_list.AddRectFilledMultiColor(
        rect.min, rect.max, ImGui::GetColorU32(top_left), ImGui::GetColorU32(top_right), ImGui::GetColorU32(bottom_right),
        ImGui::GetColorU32(bottom_left)
    );
}

static std::string format_hex(ImColor color, bool alpha = true) {
    std::string result = std::format(
        "#{:02X}{:02X}{:02X}{:02X}", static_cast<unsigned int>(saturate(color.Value.x) * 255.0F),
        static_cast<unsigned int>(saturate(color.Value.y) * 255.0F), static_cast<unsigned int>(saturate(color.Value.z) * 255.0F),
        static_cast<unsigned int>(saturate(color.Value.w) * 255.0F)
    );
    if (!alpha) result.resize(7);
    return result;
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool parse_hex(const std::string& text, ImColor& color) {
    const std::size_t offset = !text.empty() && text[0] == '#' ? 1 : 0;
    const std::size_t length = text.size() - offset;
    if (length != 6 && length != 8) {
        return false;
    }

    std::array<unsigned char, 4> channels{0, 0, 0, 255};
    for (std::size_t index = 0; index < length / 2; ++index) {
        const int high = hex_digit(text[offset + (index * 2)]);
        const int low = hex_digit(text[offset + (index * 2) + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        channels[index] = static_cast<unsigned char>((high * 16) + low);
    }

    color = ImColor(channels[0], channels[1], channels[2], channels[3]);
    return true;
}

static void draw_checkerboard(const Painter& paint, Rect rect, float cell_size, const Color& light, const Color& dark) {
    cell_size = std::max(1.0F, cell_size);
    const int rows = std::max(0, static_cast<int>(std::ceil((rect.max.y - rect.min.y) / cell_size)));
    const int columns = std::max(0, static_cast<int>(std::ceil((rect.max.x - rect.min.x) / cell_size)));

    for (int row = 0; row < rows; ++row) {
        const float y = rect.min.y + (static_cast<float>(row) * cell_size);
        for (int column = 0; column < columns; ++column) {
            const float x = rect.min.x + (static_cast<float>(column) * cell_size);
            const Rect cell =
                Rect::from_position_size({x, y}, {std::min(cell_size, rect.max.x - x), std::min(cell_size, rect.max.y - y)});
            paint.rect_filled(cell, (row + column) % 2 == 0 ? light : dark);
        }
    }
}

static ImColor hsv_color(float hue, float saturation, float value, float alpha = 1.0F) {
    ImColor color;
    ImGui::ColorConvertHSVtoRGB(hue, saturation, value, color.Value.x, color.Value.y, color.Value.z);
    color.Value.w = alpha;
    return color;
}

class ui::ColorPickerPreviewNode final : public Widget {
public:
    explicit ColorPickerPreviewNode(ColorPickerWidget& owner) : Widget("preview", "ColorPickerPreview"), m_owner(owner) {}

private:
    void paint_surface(const PaintContext& context) const override {
        context.painter.frame_surface(context.rect, context.style, m_owner.m_value.value());
    }

    void click_event(UiEvent& event) override {
        if (event.button != PointerButton::Left) {
            return;
        }

        if (m_owner.is_open()) {
            m_owner.close();
            if (m_owner.native_popup_open()) return;
        }
        m_owner.open();
    }

    void input_state_changed() override {
        StyledNode::input_state_changed();
        if (m_owner.is_open()) set_visual_style(StyleType::ACTIVE);
    }

    ColorPickerWidget& m_owner;
};

class ui::ColorPickerControlsNode final : public Widget {
public:
    explicit ColorPickerControlsNode(ColorPickerWidget& owner)
        : Widget("controls", "ColorPickerControls", InputMode::None), m_owner(owner) {
        set_size({grow(), grow()});
    }

private:
    void on_measure() override {
        const Theme& theme = surface().theme();
        const float bar_size = std::max(1.0F, theme.controls.thumb_size);
        const float size = 196.0F + theme.metrics.item_spacing.x + bar_size;
        set_measured_content_size({size, size});
    }

    void paint_content(const PaintContext& context) override;
    void set_hsv(float hue, float saturation, float value, float alpha);

    ColorPickerWidget& m_owner;
    float m_hue = 0.0f;
    float m_saturation = 0.0f;
};

class ui::ColorPickerPopup final : public LayerContainer {
public:
    explicit ColorPickerPopup(ColorPickerWidget& owner)
        : LayerContainer("popup", LayerMode::Popup, "ColorPickerPopup"), m_owner(owner) {
        set_id(std::format("##color-picker-{}", identity()));

        m_controls = &add<ColorPickerControlsNode>(m_owner);
        m_hex_input = &add<TextInputWidget>();
        m_hex_input->set_id("hex");
        m_hex_input->on_change([this] {
            if (m_editing_hex) return;
            ImColor parsed;
            if (parse_hex(m_hex_input->value(), parsed)) {
                m_editing_hex = true;
                m_owner.set_color(parsed);
                m_editing_hex = false;
            }
        });
    }

    void sync_hex() {
        if (m_editing_hex) return;
        m_editing_hex = true;
        m_hex_input->set_value(format_hex(m_owner.color().rgba(), m_owner.m_alpha_visible));
        m_editing_hex = false;
    }

private:
    void apply_theme_defaults(const Theme& theme) override {
        set_size({fit(), fit()});
        set_spacing(theme.metrics.item_spacing.y);

        configure_all_styles([&theme](Style& style) {
            style.color(theme.text_color)
                .background_color(theme.controls.background_color)
                .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
                .border_color(theme.controls.border_color)
                .border_radius(theme.metrics.popup_rounding)
                .padding({12.0F, 12.0F});
        });

        m_hex_input->configure_all_styles([&theme](Style& style) {
            style.color(theme.text_color)
                .background_color(theme.background_tertiary_color)
                .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
                .border_color(theme.controls.border_color)
                .border_radius(theme.controls.rounding)
                .padding(BoxInsets{theme.metrics.item_inner_spacing.x, theme.metrics.item_inner_spacing.y});
        });
        m_hex_input->style(StyleType::HOVER).border_color(theme.accent_hover_color);
        m_hex_input->style(StyleType::ACTIVE).border_color(theme.accent_color);
        m_hex_input->style(StyleType::FOCUS).border_color(theme.accent_color);
        m_controls->configure_all_styles([](Style& style) { style.padding({}).border(BorderEdge{}); });
    }

    bool paint() override {
        if (!m_owner.is_open()) return LayerContainer::paint();

        const Rect preview_rect = m_owner.preview().layout().visual_rect();
        set_popup_position({preview_rect.min.x, preview_rect.max.y + surface().theme().metrics.item_spacing.y});
        if (!LayerContainer::paint()) {
            m_owner.close();
            return false;
        }

        const Rect popup_rect = layout().visual_rect();

        surface().input_router().register_target(*this, popup_rect);
        surface().input_router().register_blocker(*this, popup_rect);

        return true;
    }

    ColorPickerWidget& m_owner;
    ColorPickerControlsNode* m_controls = nullptr;
    TextInputWidget* m_hex_input = nullptr;
    bool m_editing_hex = false;
};

void ColorPickerControlsNode::paint_content(const PaintContext& context) {
    const Rect content = context.content_rect;
    const Theme& theme = surface().theme();
    const ImVec2 spacing = theme.metrics.item_spacing;
    const float bar_size = std::max(1.0F, theme.controls.thumb_size);

    ImColor color = m_owner.m_value.value().rgba();
    float hue = 0.0F;
    float saturation = 0.0F;
    float value = 0.0F;

    ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, hue, saturation, value);
    if (saturation > 0.0f) m_hue = hue;
    if (value > 0.0f) m_saturation = saturation;
    if (saturation == 0.0f) hue = m_hue;
    if (value == 0.0f) saturation = m_saturation;

    const ImVec2 origin = content.min;
    const float selector_size =
        std::max(1.0F, std::min(content.size().x - spacing.x - bar_size, content.size().y - spacing.y - bar_size));
    const Rect selector = Rect::from_position_size(origin, {selector_size, selector_size});
    const Rect hue_bar = Rect::from_position_size({selector.max.x + spacing.x, selector.min.y}, {bar_size, selector_size});
    const Rect alpha_bar =
        Rect::from_position_size({selector.min.x, selector.max.y + spacing.y}, {hue_bar.max.x - selector.min.x, bar_size});

    // invisible controls update hsv before the surfaces below read the new color for this frame.
    if (active_area("selector", selector)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        saturation = saturate((mouse.x - selector.min.x) / selector.size().x);
        value = 1.0F - saturate((mouse.y - selector.min.y) / selector.size().y);
        set_hsv(hue, saturation, value, color.Value.w);
        color = m_owner.m_value.value().rgba();
    }

    if (active_area("hue", hue_bar)) {
        hue = saturate((ImGui::GetIO().MousePos.y - hue_bar.min.y) / hue_bar.size().y);
        set_hsv(hue, saturation, value, color.Value.w);
        color = m_owner.m_value.value().rgba();
    }

    if (m_owner.m_alpha_visible && active_area("alpha", alpha_bar)) {
        color.Value.w = saturate((ImGui::GetIO().MousePos.x - alpha_bar.min.x) / alpha_bar.size().x);
        m_owner.set_color(color);
    }

    const Painter paint = painter();
    ImDrawList& draw_list = paint.draw_list();
    const ImColor hue_color = hsv_color(hue, 1.0F, 1.0F);
    draw_picker_gradient(draw_list, selector, rgb(255, 255, 255), hue_color, hue_color, rgb(255, 255, 255));
    draw_picker_gradient(draw_list, selector, rgba(0, 0, 0, 0), rgba(0, 0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0));
    paint.rect_outline(selector, theme.controls.border_color);

    for (int index = 0; index < 6; ++index) {
        const float top = hue_bar.min.y + (hue_bar.size().y * static_cast<float>(index) / 6.0F);
        const float bottom = hue_bar.min.y + (hue_bar.size().y * static_cast<float>(index + 1) / 6.0F);
        const ImColor first = hsv_color(static_cast<float>(index) / 6.0F, 1.0F, 1.0F);
        const ImColor second = hsv_color(static_cast<float>(index + 1) / 6.0F, 1.0F, 1.0F);
        draw_picker_gradient(draw_list, {{hue_bar.min.x, top}, {hue_bar.max.x, bottom}}, first, first, second, second);
    }
    paint.rect_outline(hue_bar, theme.controls.border_color);

    if (m_owner.m_alpha_visible) {
        draw_checkerboard(paint, alpha_bar, bar_size * 0.5f, theme.background_secondary_color, theme.background_tertiary_color);

        const ImColor opaque = ImColor(color.Value.x, color.Value.y, color.Value.z, 1.0f);
        const ImColor transparent = ImColor(color.Value.x, color.Value.y, color.Value.z, 0.0f);

        draw_picker_gradient(draw_list, alpha_bar, transparent, opaque, opaque, transparent);
        paint.rect_outline(alpha_bar, theme.controls.border_color);
    }

    const ImVec2 selector_cursor = {
        selector.min.x + (saturation * selector.size().x),
        selector.min.y + ((1.0F - value) * selector.size().y),
    };

    paint.circle(selector_cursor, 5.0F, theme.background_color);
    paint.circle_outline(selector_cursor, 5.0F, theme.text_color, 1.5F);

    const float hue_cursor_y = hue_bar.min.y + (hue * hue_bar.size().y);

    paint.line({hue_bar.min.x - 2.0F, hue_cursor_y}, {hue_bar.max.x + 2.0F, hue_cursor_y}, theme.text_color, 2.0F);
    paint.line(
        {hue_bar.min.x - 2.0F, hue_cursor_y + 1.0F}, {hue_bar.max.x + 2.0F, hue_cursor_y + 1.0F}, theme.background_color, 1.0F
    );

    if (m_owner.m_alpha_visible) {
        const float alpha_cursor_x = alpha_bar.min.x + (color.Value.w * alpha_bar.size().x);

        paint.line({alpha_cursor_x, alpha_bar.min.y - 2.0f}, {alpha_cursor_x, alpha_bar.max.y + 2.0f}, theme.text_color, 2.0f);

        paint.line(
            {alpha_cursor_x + 1.0f, alpha_bar.min.y - 2.0f}, {alpha_cursor_x + 1.0f, alpha_bar.max.y + 2.0f},
            theme.background_color, 1.0f
        );
    }
}

void ColorPickerControlsNode::set_hsv(float hue, float saturation, float value, float alpha) {
    m_hue = hue;
    m_saturation = saturation;
    m_owner.set_color(hsv_color(hue, saturation, value, alpha));
}

ColorPickerWidget::ColorPickerWidget(WidgetValue<Color> value, std::string label, LabelPlacement placement, std::string id)
    : Widget(std::move(id), StackDirection::Vertical, "ColorPicker"), m_value(std::move(value)) {
    m_label_node = &add<TextWidget>("");
    m_label_node->set_visible(!m_label_node->empty());
    m_preview = &add<ColorPickerPreviewNode>(*this);
    m_popup = &add<ColorPickerPopup>(*this);
    set_label(std::move(label), placement);
}

ColorPickerWidget& ColorPickerWidget::set_label(std::string label, LabelPlacement placement) {
    m_label_node->set_text(std::move(label));
    m_label_node->set_visible(!m_label_node->empty());
    m_label_placement = placement;
    set_label_layout(m_label_placement, m_label_spacing);
    return *this;
}

void ColorPickerWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    m_label_spacing = {theme.metrics.item_inner_spacing.x, theme.metrics.item_spacing.y};
    set_label_layout(m_label_placement, m_label_spacing);

    const ImVec2 preview_size = {
        (theme.controls.thumb_size * 2.0F) + (theme.metrics.frame_padding.x * 2.0F),
        theme.controls.thumb_size + (theme.metrics.frame_padding.y * 2.0F),
    };

    set_size({fit(), fit()});
    m_preview->set_size({px(preview_size.x), px(preview_size.y)});

    configure_all_styles([&theme](Style& style) { style.font_size(theme.controls.font_size).padding({}); });

    m_label_node->configure_all_styles([&theme](Style& style) { style.color(theme.text_color); });
    m_preview->configure_all_styles([&theme](Style& style) {
        style.background_color(theme.controls.background_color)
            .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
            .border_color(theme.controls.border_color)
            .border_radius(theme.controls.rounding)
            .padding({})
            .cursor(ImGuiMouseCursor_Hand);
    });

    m_preview->style(StyleType::HOVER).border_color(theme.accent_hover_color);
    m_preview->style(StyleType::ACTIVE).border_color(theme.accent_color);
    m_preview->style(StyleType::DISABLED).border_color(theme.border_color).cursor(ImGuiMouseCursor_NotAllowed);
    m_label_node->style(StyleType::DISABLED).color(theme.text_secondary_color);
    style(StyleType::DISABLED).cursor(ImGuiMouseCursor_NotAllowed);
}

bool ColorPickerWidget::set_color(const Color& color) {
    if (!m_value.set(color)) {
        return false;
    }

    m_popup->sync_hex();
    notify_change();
    return true;
}

void ColorPickerWidget::open() {
    if (m_open) {
        return;
    }

    m_open = true;
    m_popup->sync_hex();
    m_popup->open_popup();
    m_preview->set_open_style(true);
}

void ColorPickerWidget::close() {
    m_open = false;
    m_popup->close_popup();
    m_preview->set_open_style(false);
}

bool ColorPickerWidget::native_popup_open() const {
    return m_popup->native_popup_open();
}

TextWidget& ColorPickerWidget::label() {
    return *m_label_node;
}

Widget& ColorPickerWidget::preview() {
    return *m_preview;
}

Container& ColorPickerWidget::popup() {
    return *m_popup;
}

const TextWidget& ColorPickerWidget::label() const {
    return *m_label_node;
}

const Widget& ColorPickerWidget::preview() const {
    return *m_preview;
}

const Container& ColorPickerWidget::popup() const {
    return *m_popup;
}

ColorPickerWidget& ColorPickerWidget::set_alpha_visible(bool visible) {
    m_alpha_visible = visible;
    m_popup->sync_hex();
    return *this;
}
