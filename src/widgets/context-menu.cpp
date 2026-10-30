#include <imgui-ui/widgets/context-menu.hpp>

#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/imgui/painter.hpp>
#include <imgui-ui/resources/texture-registry.hpp>
#include <imgui-ui/runtime.hpp>

#include <algorithm>
#include <utility>

using namespace ui;

class ui::ContextMenuItemNode final : public Widget {
public:
    ContextMenuItemNode(ContextMenuWidget& menu, std::string label, ContextMenuCallback callback)
        : Widget("item", "ContextMenuItem"), m_menu(menu), m_label(std::move(label)), m_callback(std::move(callback)) {}

    bool accepts_input() const override {
        return m_menu.is_open() && Widget::accepts_input();
    }

protected:
    void apply_theme_defaults(const Theme& theme) override {
        configure_all_styles([&theme](Style& style) {
            style.color(theme.text_color)
                .background_color(theme.transparent)
                .padding(BoxInsets{theme.metrics.item_inner_spacing.x, theme.metrics.item_inner_spacing.y})
                .border(BorderEdge{0.0F, BorderType::None})
                .border_radius(theme.controls.rounding)
                .cursor(ImGuiMouseCursor_Hand);
        });

        style(StyleType::HOVER).background_color(theme.controls.hover_color);
        style(StyleType::ACTIVE).background_color(theme.controls.active_color);
        style(StyleType::DISABLED).color(theme.text_secondary_color).cursor(ImGuiMouseCursor_NotAllowed);
        set_size({grow(), px(theme.controls.font_size + (theme.metrics.item_inner_spacing.y * 2.0F))});
    }

private:
    friend class ContextMenuWidget;

    void mouse_move_event(UiEvent&) override {
        if (m_submenu != nullptr) {
            m_menu.open_submenu(*this);
        }
    }

    void click_event(UiEvent& event) override {
        if (event.button != PointerButton::Left) {
            return;
        }

        m_menu.activate_item(*this);
        event.stop_propagation();
    }

    void paint_content(const PaintContext& context) override {
        const Rect content = context.content_rect;
        const ImVec2 text_size = ImGui::CalcTextSize(m_label.c_str());
        context.painter.text(
            {content.min.x, content.min.y + ((content.size().y - text_size.y) * 0.5F)}, context.style.color().value, m_label
        );

        if (m_submenu != nullptr) {
            draw_submenu_icon(context);
        }
    }

    void draw_submenu_icon(const PaintContext& context) const {
        const Rect content = context.content_rect;
        const float icon_size = std::min(13.0F, std::min(content.size().x, content.size().y));
        const Rect icon = Rect::from_position_size(
            {content.max.x - icon_size, content.min.y + ((content.size().y - icon_size) * 0.5F)}, {icon_size, icon_size}
        );

        if (m_menu.m_submenu_icon == nullptr) {
            context.painter.triangle(
                {icon.min.x + (icon_size * 0.5F), icon.min.y + (icon_size * 0.5F)}, {icon_size * 0.5F, icon_size * 0.3F},
                context.style.color().value, TriangleDirection::Right
            );
            return;
        }

        const ImTextureID texture = m_menu.m_submenu_icon->get(icon.size());
        context.painter.draw_list().AddImageQuad(
            texture, icon.min, {icon.max.x, icon.min.y}, icon.max, {icon.min.x, icon.max.y}, {0, 1}, {0, 0}, {1, 0}, {1, 1},
            context.style.color().get_col()
        );
    }

    ContextMenuWidget& m_menu;
    std::string m_label;
    ContextMenuCallback m_callback;
    ContextMenuWidget* m_submenu = nullptr;
};

ContextMenuWidget::ContextMenuWidget(ContextMenuItems items, Texture* submenu_icon)
    : Widget({}, "ContextMenu"), m_submenu_icon(submenu_icon) {
    set_layout({.in_flow = false});
    set_size({px(184.0F), fit()});
    set_visible(false);
    set_input_enabled(false);

    set_items(std::move(items));
}

void ContextMenuWidget::mouse_move_event(UiEvent& event) {
    root_menu().update_pointer_hover(event.position);
}

ContextMenuWidget::ContextMenuWidget(ContextMenuItems items, TextureHandle submenu_icon)
    : ContextMenuWidget(std::move(items), submenu_icon.get()) {
    set_submenu_icon(std::move(submenu_icon));
}

void ContextMenuWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    set_font(surface().get_primary_font(), theme.controls.font_size);
    set_spacing(0.0F);
    if (m_submenu_icon == nullptr) {
        set_submenu_icon(surface().runtime().textures().find("context-menu-chevron"));
    }
    configure_all_styles([&theme](Style& style) {
        style.padding({4.0F, 4.0F})
            .background_color(theme.background_secondary_color)
            .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
            .border_radius(theme.metrics.popup_rounding)
            .border_color(theme.border_color);
    });
}

ContextMenuWidget& ContextMenuWidget::set_items(ContextMenuItems items) {
    m_items.clear();
    clear();

    for (ContextMenuItem& item : items) {
        auto& menu_item = add<ContextMenuItemNode>(*this, std::move(item.label), std::move(item.callback));
        m_items.push_back(&menu_item);

        if (item.children.empty()) {
            continue;
        }

        auto& submenu = add<ContextMenuWidget>(std::move(item.children), m_submenu_icon);
        if (m_submenu_icon_handle) {
            submenu.set_submenu_icon(m_submenu_icon_handle);
        }
        submenu.m_parent_menu = this;
        submenu.set_hover_close_delay(m_hover_close_delay_duration);
        menu_item.m_submenu = &submenu;
    }

    return *this;
}

ContextMenuWidget& ContextMenuWidget::set_submenu_icon(Texture* icon) {
    if (m_submenu_icon == icon) {
        return *this;
    }

    m_submenu_icon = icon;
    m_submenu_icon_handle.reset();

    for (ContextMenuItemNode* item : m_items) {
        if (item->m_submenu != nullptr) {
            item->m_submenu->set_submenu_icon(icon);
        }
    }

    return *this;
}

ContextMenuWidget& ContextMenuWidget::set_submenu_icon(TextureHandle icon) {
    m_submenu_icon = icon.get();
    m_submenu_icon_handle = std::move(icon);
    for (ContextMenuItemNode* item : m_items) {
        if (item->m_submenu != nullptr) {
            item->m_submenu->set_submenu_icon(m_submenu_icon_handle);
        }
    }
    return *this;
}

ContextMenuWidget& ContextMenuWidget::set_hover_close_delay(float seconds) {
    m_hover_close_delay_duration = std::max(0.0F, seconds);

    for (ContextMenuItemNode* item : m_items) {
        if (item->m_submenu != nullptr) {
            item->m_submenu->set_hover_close_delay(m_hover_close_delay_duration);
        }
    }

    return *this;
}

void ContextMenuWidget::open() {
    if (m_parent_menu != nullptr) {
        activate();
        return;
    }

    if (ImGui::GetCurrentContext() != nullptr) {
        open_at(ImGui::GetIO().MousePos);
    }
}

void ContextMenuWidget::open_at(ImVec2 screen_position) {
    if (m_parent_menu != nullptr) {
        activate();
        return;
    }

    const Rect work_area = viewport_work_area();
    if (!work_area.valid()) {
        return;
    }
    LayoutConfig config = layout().config();
    config.placement.offset = {screen_position.x - work_area.min.x, screen_position.y - work_area.min.y};
    set_layout(config);
    invalidate_measure();
    activate();
}

void ContextMenuWidget::activate() {
    if (m_open) {
        return;
    }

    m_closing = false;
    m_open = true;
    m_hover_close_delay = m_hover_close_delay_duration;
    if (m_parent_menu == nullptr) {
        m_pointer_was_inside = false;
    }
    set_visible(true);
    set_input_enabled(true);
    fade_in();
}

void ContextMenuWidget::close() {
    if (!m_open) {
        return;
    }

    m_open = false;
    m_closing = true;
    fade_out();
    for (ContextMenuItemNode* item : m_items) {
        if (item->m_submenu != nullptr) {
            item->m_submenu->close();
        }
    }
}

void ContextMenuWidget::cancel_close() {
    ContextMenuWidget& root = root_menu();
    if (root.m_closing) {
        root.activate();
    }
}

void ContextMenuWidget::on_update(float dt) {
    m_hover_close_delay = std::max(0.0F, m_hover_close_delay - dt);

    if (m_parent_menu == nullptr && m_open && ImGui::GetCurrentContext() != nullptr) {
        update_pointer_hover(ImGui::GetIO().MousePos);
    }

    if (m_closing && opacity() <= VISIBILITY_OPACITY_THRESHOLD) {
        m_closing = false;
        set_visible(false);
        set_input_enabled(false);
        set_opacity(0.0F);
    }
}

void ContextMenuWidget::on_measure() {
    Container::on_measure();
    if (m_parent_menu != nullptr || !m_open) return;

    const Rect work_area = viewport_work_area();
    if (!work_area.valid()) return;

    LayoutConfig config = layout().config();
    const ImVec2 position = clamp_position(
        work_area, layout().intrinsic_size(),
        {work_area.min.x + config.placement.offset.x, work_area.min.y + config.placement.offset.y}
    );
    config.placement.offset = {position.x - work_area.min.x, position.y - work_area.min.y};
    set_layout(config);
}

void ContextMenuWidget::draw_children() {
    arrange_children(child_layout_size());
    for (ContextMenuItemNode* item : m_items) {
        if (!item->removal_pending()) {
            item->draw();
        }
    }
}

void ContextMenuWidget::on_draw_end() {
    Container::on_draw_end();

    // draw open submenus after their parent items so their hit regions sit above the parent menu.
    for (ContextMenuItemNode* item : m_items) {
        if (item->m_submenu == nullptr || !item->m_submenu->is_open()) {
            continue;
        }

        position_submenu(*item->m_submenu, *item);
    }

    if (m_parent_menu != nullptr || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    const Rect work_area = viewport_work_area();
    if (!work_area.valid()) {
        return;
    }

    // block the viewport outside this menu's input descendants, leaving the open menu tree eligible for routing.
    surface().input_router().register_blocker(*this, work_area, [this](UiEvent& event) {
        if (event.type == EventType::PointerMove) {
            update_pointer_hover(event.position);
            return;
        }

        if (event.type == EventType::PointerDown) {
            close();
        }
    });
}

void ContextMenuWidget::activate_item(ContextMenuItemNode& item) {
    if (item.m_submenu != nullptr) {
        open_submenu(item);
        return;
    }

    ContextMenuWidget& root = root_menu();
    root.close();
    if (item.m_callback) {
        item.m_callback(root);
    }
}

void ContextMenuWidget::open_submenu(ContextMenuItemNode& item) {
    for (ContextMenuItemNode* sibling : m_items) {
        if (sibling != &item && sibling->m_submenu != nullptr) {
            sibling->m_submenu->close();
        }
    }

    item.m_submenu->activate();
}

void ContextMenuWidget::update_pointer_hover(ImVec2 position) {
    if (contains_open_menu(position)) {
        m_pointer_was_inside = true;
        update_submenu_hover(position);
        return;
    }

    if (m_pointer_was_inside && m_hover_close_delay <= 0.0F) {
        close();
    }
}

void ContextMenuWidget::update_submenu_hover(ImVec2 position) {
    for (ContextMenuItemNode* item : m_items) {
        ContextMenuWidget* submenu = item->m_submenu;
        if (submenu == nullptr || !submenu->is_open()) {
            continue;
        }

        const Rect item_rect = item->layout().visual_rect();
        const Rect submenu_rect = submenu->layout().visual_rect();
        const Rect submenu_gap = {
            {std::min(item_rect.max.x, submenu_rect.max.x), std::max(item_rect.min.y, submenu_rect.min.y)},
            {std::max(item_rect.min.x, submenu_rect.min.x), std::min(item_rect.max.y, submenu_rect.max.y)},
        };

        // keep the submenu open while the pointer crosses the space between the item and its submenu.
        if (submenu->m_hover_close_delay <= 0.0F && !item_rect.contains(position) && !submenu_gap.contains(position) &&
            !submenu->contains_open_menu(position)) {
            submenu->close();
        }
    }
}

void ContextMenuWidget::position_submenu(ContextMenuWidget& submenu, const ContextMenuItemNode& item) {
    const Rect item_rect = item.layout().visual_rect();
    const Rect work_area = viewport_work_area();
    const ImVec2 submenu_size = submenu.layout().intrinsic_size();

    const float submenu_gap = 6.0F;
    float screen_x = item_rect.max.x + submenu_gap;
    if (screen_x + submenu_size.x > work_area.max.x) {
        screen_x = item_rect.min.x - submenu_size.x - submenu_gap;
    }

    const ImVec2 position = clamp_position(work_area, submenu_size, {screen_x, item_rect.min.y});
    const ImVec2 window_position = ImGui::GetWindowPos();
    const ImVec2 content_offset = ImGui::GetCursorStartPos();
    const ImVec2 content_origin = {window_position.x + content_offset.x, window_position.y + content_offset.y};
    LayoutConfig config = submenu.layout().config();
    config.placement.offset = {position.x - content_origin.x, position.y - content_origin.y};
    submenu.set_layout(config);
    submenu.draw();
}

bool ContextMenuWidget::contains_open_menu(ImVec2 position) const {
    if (layout().visual_rect().contains(position)) {
        return true;
    }

    for (const ContextMenuItemNode* item : m_items) {
        if (item->m_submenu != nullptr && item->m_submenu->is_open() && item->m_submenu->contains_open_menu(position)) {
            return true;
        }
    }

    return false;
}

ContextMenuWidget& ContextMenuWidget::root_menu() {
    ContextMenuWidget* root = this;
    while (root->m_parent_menu != nullptr) {
        root = root->m_parent_menu;
    }

    return *root;
}
