#pragma once

#include "widget.hpp"
#include "../resources/texture.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ui {
    class Texture;
    class ContextMenuItemNode;
    class ContextMenuWidget;

    using ContextMenuCallback = std::function<void(ContextMenuWidget&)>;

    struct ContextMenuItem {
        std::string label;
        std::vector<ContextMenuItem> children;
        ContextMenuCallback callback;

        static ContextMenuItem action(std::string label, ContextMenuCallback callback = {}) {
            return {.label = std::move(label), .children = {}, .callback = std::move(callback)};
        }

        static ContextMenuItem submenu(std::string label, std::vector<ContextMenuItem> children) {
            return {.label = std::move(label), .children = std::move(children), .callback = {}};
        }
    };

    using ContextMenuItems = std::vector<ContextMenuItem>;

    class ContextMenuWidget : public Widget {
    public:
        explicit ContextMenuWidget(ContextMenuItems items = {}, Texture* submenu_icon = nullptr);
        ContextMenuWidget(ContextMenuItems items, TextureHandle submenu_icon);

        ContextMenuWidget& set_items(ContextMenuItems items);
        ContextMenuWidget& set_submenu_icon(Texture* icon);
        ContextMenuWidget& set_submenu_icon(TextureHandle icon);

        /// sets the delay counted from menu activation before an outside pointer position can close it.
        ContextMenuWidget& set_hover_close_delay(float seconds);

        void open();

        /// opens the root menu near screen_position, clamped to the viewport work area.
        void open_at(ImVec2 screen_position);

        /// starts a fade out. the menu remains visible until its opacity reaches the visibility threshold.
        void close();
        void cancel_close();

        bool is_open() const {
            return m_open;
        }

        bool accepts_input() const override {
            return m_open && Widget::accepts_input();
        }

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        friend class ContextMenuItemNode;

        void on_update(float) override;
        void on_measure() override;
        void mouse_move_event(UiEvent& event) override;
        void draw_children() override;
        void on_draw_end() override;

        void activate_item(ContextMenuItemNode& item);
        void open_submenu(ContextMenuItemNode& item);
        void update_pointer_hover(ImVec2 position);
        void update_submenu_hover(ImVec2 position);
        void activate();
        static void position_submenu(ContextMenuWidget& submenu, const ContextMenuItemNode& item);
        bool contains_open_menu(ImVec2 position) const;
        ContextMenuWidget& root_menu();

        Texture* m_submenu_icon = nullptr;
        TextureHandle m_submenu_icon_handle;
        std::vector<ContextMenuItemNode*> m_items;
        ContextMenuWidget* m_parent_menu = nullptr;
        bool m_open = false;
        bool m_closing = false;
        bool m_pointer_was_inside = false;
        float m_hover_close_delay_duration = 1.0F;
        float m_hover_close_delay = 0.0F;
    };
} // namespace ui
