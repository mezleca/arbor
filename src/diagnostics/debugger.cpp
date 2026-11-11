#include <imgui-ui/diagnostics/debugger.hpp>
#include <imgui-ui/diagnostics/profiler.hpp>
#include <imgui-ui/imgui/context-scope.hpp>
#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/layout/resizable-container.hpp>
#include <imgui-ui/resources/svg.hpp>
#include <imgui-ui/resources/texture-registry.hpp>
#include <imgui-ui/style/styled-node.hpp>
#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/tree/node.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/runtime.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ui;

static Node* find_node_by_identity(Node& root, uint64_t identity) {
    if (root.identity() == identity) {
        return &root;
    }

    for (const auto& child : root.children()) {
        Node* result = find_node_by_identity(*child, identity);
        if (result != nullptr) {
            return result;
        }
    }

    return nullptr;
}

static bool is_effectively_visible(const Node& node) {
    for (const Node* current = &node; current != nullptr; current = current->parent()) {
        if (!current->visible()) {
            return false;
        }

        const auto* styled = dynamic_cast<const StyledNode*>(current);
        if (styled != nullptr && !styled->visually_visible()) {
            return false;
        }
    }

    return true;
}

static Node* pick_node(Node& root, ImVec2 position) {
    const auto* styled = dynamic_cast<const StyledNode*>(&root);
    if (!root.visible() || root.removal_pending() || (styled != nullptr && !styled->visually_visible())) {
        return nullptr;
    }

    for (auto it = root.children().rbegin(); it != root.children().rend(); ++it) {
        Node* candidate = pick_node(**it, position);
        if (candidate != nullptr) {
            return candidate;
        }
    }

    if (!root.layout().visual_rect().contains(position)) {
        return nullptr;
    }

    // skip the full-area layer when no descendant was hit, so inspection does not select an overlay instead of its content.
    if (dynamic_cast<const LayerContainer*>(&root) != nullptr) {
        return nullptr;
    }

    if (styled == nullptr) {
        return nullptr;
    }

    return &root;
}

static constexpr const char* ALIGNMENT_NAMES[] = {
    "top-left",     "top-center",  "top-right",     "center-left",  "center",
    "center-right", "bottom-left", "bottom-center", "bottom-right", "custom",
};

static constexpr ImVec2 ALIGNMENTS[] = {
    Anchor::TopLeft,     Anchor::TopCenter,  Anchor::TopRight,     Anchor::CenterLeft,  Anchor::Center,
    Anchor::CenterRight, Anchor::BottomLeft, Anchor::BottomCenter, Anchor::BottomRight,
};

static constexpr const char* STYLE_NAMES[] = {"all", "default", "hover", "active", "focus", "disabled"};
static constexpr const char* BORDER_STYLE_NAMES[] = {"solid", "dashed", "dotted", "none"};
static constexpr const char* BOX_SIZING_NAMES[] = {"content-box", "border-box"};
static constexpr const char* OVERFLOW_NAMES[] = {"visible", "hidden", "clip"};
static constexpr const char* SIZE_MODE_NAMES[] = {"fixed", "percent", "fit", "grow"};

static constexpr float WINDOW_PADDING = 8.0F;
static constexpr ImVec2 INSPECT_ICON_SIZE = {18.0F, 18.0F};
static constexpr ImVec2 CLOSE_ICON_SIZE = {18.0F, 18.0F};
static constexpr float ITEM_SPACING = 12.0F;
static constexpr float INPUT_MAX_WIDTH = 180.0F;
static constexpr ImVec2 INPUT_PADDING = {6.0F, 3.0F};
static constexpr float INPUT_BORDER_THICKNESS = 1.0F;
static constexpr ImVec2 SECTION_PADDING = {10.0F, 8.0F};
static constexpr float PROPERTY_ITEM_SPACING = 6.0F;
static constexpr float DEBUGGER_SPLITTER_HEIGHT = 6.0F;
static constexpr float DEBUGGER_MIN_PANE_HEIGHT = 72.0F;
static constexpr uint64_t PROFILE_UPDATE_INTERVAL = 3;
static constexpr double DEBUGGER_FOCUS_DELAY = 0.1;
static constexpr int HIGHLIGHT_MIN_LINE_THICKNESS = 1;
static constexpr int HIGHLIGHT_MAX_LINE_THICKNESS = 10;

class ui::DebuggerPopupState {
public:
    void save() {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr || context->OpenPopupStack.empty()) {
            return;
        }

        // dispatch runs before the backend queues this press for imgui, leaving this stack intact for the later restore.
        m_popups.assign(context->OpenPopupStack.begin(), context->OpenPopupStack.end());
    }

    void restore() {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr || m_popups.empty()) {
            return;
        }

        context->OpenPopupStack.resize(static_cast<int>(m_popups.size()));
        std::ranges::copy(m_popups, context->OpenPopupStack.begin());
    }

    void clear() {
        m_popups.clear();
    }

private:
    std::vector<ImGuiPopupData> m_popups;
};

class ui::DebuggerProfileState {
public:
    uint64_t frame_count = 0;
    uint64_t node_identity = 0;
    double frame_ms = 0.0;
    double node_ms = 0.0;
    uint32_t dropped_events = 0;
    ProfileFrameMetrics metrics;
    ProfileGpuSummary gpu;
    std::vector<ProfileEvent> events;
    bool valid = false;
};

static bool belongs_to_debugger(const ImGuiWindow& window, const ImGuiWindow& debugger_window) {
    for (const ImGuiWindow* parent = window.ParentWindow; parent != nullptr; parent = parent->ParentWindow) {
        if (parent == &debugger_window) {
            return true;
        }
    }

    return false;
}

static bool debugger_popup_open(ImGuiID debugger_window_id) {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        return false;
    }

    ImGuiWindow* debugger_window = ImGui::FindWindowByID(debugger_window_id);
    if (debugger_window == nullptr) {
        return false;
    }

    for (const ImGuiPopupData& popup : context->OpenPopupStack) {
        if (popup.Window != nullptr && belongs_to_debugger(*popup.Window, *debugger_window)) {
            return true;
        }
    }

    return false;
}

static void bring_debugger_to_front(ImGuiWindow& debugger_window) {
    // keep the debugger above app layers while leaving its popups on top.
    ImGui::BringWindowToDisplayFront(&debugger_window);

    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        return;
    }

    for (const ImGuiPopupData& popup : context->OpenPopupStack) {
        if (popup.Window != nullptr && belongs_to_debugger(*popup.Window, debugger_window)) {
            ImGui::BringWindowToDisplayFront(popup.Window);
        }
    }
}

template <typename... Args>
static void draw_property_value(std::string_view label, std::string_view format, Args&&... args) {
    const std::string value = std::vformat(format, std::make_format_args(args...));
    ImGui::TextDisabled("%.*s:", static_cast<int>(label.size()), label.data());
    ImGui::SameLine(0.0F, ITEM_SPACING);
    ImGui::TextDisabled("%s", value.c_str());
}

template <typename Apply>
static void apply_to_styles(Style& style, std::span<Style*> targets, Apply&& apply) {
    apply(style);
    for (Style* target : targets) {
        if (target != nullptr && target != &style) {
            apply(*target);
        }
    }
}

void Debugger::end_property_section() {
    if (!m_property_section_open) {
        return;
    }

    ImGui::Unindent(SECTION_PADDING.x);
    ImGui::Dummy({0.0F, SECTION_PADDING.y});
    ImGui::EndChild();
    m_property_section_open = false;
}

void Debugger::draw_property_section(std::string_view label) {
    end_property_section();

    ImGui::Spacing();

    const ImVec4 section_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, section_color);
    ImGui::BeginChild(
        ImGui::GetID(label.data(), label.data() + label.size()), {0.0F, 0.0F}, ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    m_property_section_open = true;

    const ImVec2 item_spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {item_spacing.x, PROPERTY_ITEM_SPACING});
    ImGui::Indent(SECTION_PADDING.x);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
    ImGui::Unindent(SECTION_PADDING.x);
    ImGui::Separator();
    ImGui::PopStyleVar();
    ImGui::Indent(SECTION_PADDING.x);
}

static void push_input_style(ImVec4 frame_background = {}) {
    if (frame_background.w == 0.0F) {
        frame_background = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        frame_background.x *= 0.72F;
        frame_background.y *= 0.72F;
        frame_background.z *= 0.72F;
    }

    ImVec4 border_color = ImGui::GetStyle().Colors[ImGuiCol_Border];
    border_color.w *= 0.7F;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frame_background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frame_background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frame_background);
    ImGui::PushStyleColor(ImGuiCol_Border, border_color);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, INPUT_PADDING);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, INPUT_BORDER_THICKNESS);
}

static void pop_input_style() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

template <typename DrawInput>
static bool draw_labeled_input(std::string_view label, DrawInput draw_input, ImVec4 frame_background = {}) {
    ImGui::PushID(label.data(), label.data() + label.size());
    push_input_style(frame_background);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.*s:", static_cast<int>(label.size()), label.data());
    ImGui::SameLine(0.0F, ITEM_SPACING);
    const float input_width = std::min(INPUT_MAX_WIDTH, std::max(0.0F, ImGui::GetContentRegionAvail().x));
    ImGui::SetNextItemWidth(input_width);
    const bool changed = draw_input();
    pop_input_style();
    ImGui::PopID();
    return changed;
}

static bool draw_text_input(std::string_view label, std::string& value) {
    return draw_labeled_input(label, [&value] { return ImGui::InputText("##value", &value); });
}

static bool draw_color_input(std::string_view label, ImVec4& value) {
    return draw_labeled_input(label, [&value] {
        return ImGui::ColorEdit4("##value", &value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    });
}

static bool draw_highlight_option(std::string_view label, bool& enabled, Color& color) {
    ImGui::PushID(label.data(), label.data() + label.size());
    push_input_style();
    bool changed = ImGui::Checkbox("##enabled", &enabled);
    ImGui::SameLine(0.0F, ITEM_SPACING);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    if (enabled) {
        ImGui::SameLine(0.0F, ITEM_SPACING);
        ImVec4 solid = color.rgba();
        if (ImGui::ColorEdit4("##color", &solid.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            color = solid;
            changed = true;
        }
    }
    pop_input_style();
    ImGui::PopID();
    return changed;
}

static bool draw_inline_combo(std::string_view label, int& selected, const char* const items[], int item_count) {
    return draw_labeled_input(label, [&selected, items, item_count] {
        const bool valid_selection = selected >= 0 && selected < item_count;
        const char* preview = valid_selection ? items[selected] : "select";
        bool changed = false;

        if (ImGui::BeginCombo("##value", preview, ImGuiComboFlags_NoArrowButton)) {
            for (int index = 0; index < item_count; ++index) {
                const bool is_selected = selected == index;
                if (ImGui::Selectable(items[index], is_selected)) {
                    selected = index;
                    changed = true;
                }

                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        return changed;
    });
}

static bool draw_number_input(
    std::string_view label, ImGuiDataType type, void* values, int components, float speed, const void* minimum,
    const void* maximum, const char* format
) {
    if (components == 2) {
        return draw_labeled_input(label, [=] {
            const float total_width = std::min(INPUT_MAX_WIDTH, ImGui::GetContentRegionAvail().x);
            const float component_width = std::max(0.0F, (total_width - ITEM_SPACING - 20.0F) * 0.5F);
            const size_t value_size = type == ImGuiDataType_S32 ? sizeof(int) : sizeof(float);
            auto* raw_values = static_cast<unsigned char*>(values);
            bool changed = false;

            for (int index = 0; index < 2; ++index) {
                if (index != 0) {
                    ImGui::SameLine(0.0F, ITEM_SPACING);
                }

                ImGui::PushID(index);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(index == 0 ? "x" : "y");
                ImGui::SameLine(0.0F, 2.0F);
                ImGui::SetNextItemWidth(component_width);
                void* component = raw_values + (value_size * static_cast<size_t>(index));
                changed = ImGui::DragScalar(
                              "##component", type, component, speed, minimum != nullptr && maximum != nullptr ? minimum : nullptr,
                              minimum != nullptr && maximum != nullptr ? maximum : nullptr, format
                          ) ||
                          changed;
                ImGui::PopID();
            }

            return changed;
        });
    }

    return draw_labeled_input(label, [=] {
        return minimum != nullptr && maximum != nullptr
                   ? ImGui::SliderScalarN("##value", type, values, components, minimum, maximum, format)
                   : ImGui::DragScalarN("##value", type, values, components, speed, minimum, maximum, format);
    });
}

static bool draw_number_input(
    std::string_view label, float* values, int components = 1, float speed = 0.1F, float minimum = 0.0F, float maximum = 0.0F
) {
    const bool limited = maximum > minimum;
    return draw_number_input(
        label, ImGuiDataType_Float, values, components, speed, limited ? &minimum : nullptr, limited ? &maximum : nullptr, "%.3f"
    );
}

static bool draw_slider(std::string_view label, float* value, float minimum, float maximum) {
    return draw_labeled_input(label, [=] {
        return ImGui::SliderFloat("##value", value, minimum, maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    });
}

static void draw_fallback_inspect_icon(ImDrawList& draw_list, ImVec2 min, ImVec2 max, ImU32 color) {
    const float scale = std::min(max.x - min.x, max.y - min.y) / 24.0F;
    const auto point = [min, scale](float x, float y) { return ImVec2{min.x + (x * scale), min.y + (y * scale)}; };

    draw_list.AddRect(point(2.0F, 2.0F), point(19.0F, 19.0F), color, 2.0F * scale, 0, 1.8F * scale);
    draw_list.AddLine(point(12.0F, 12.0F), point(16.2F, 22.0F), color, 1.8F * scale);
    draw_list.AddLine(point(16.2F, 22.0F), point(17.7F, 17.6F), color, 1.8F * scale);
    draw_list.AddLine(point(17.7F, 17.6F), point(22.0F, 16.2F), color, 1.8F * scale);
    draw_list.AddLine(point(22.0F, 16.2F), point(12.0F, 12.0F), color, 1.8F * scale);
    draw_list.AddLine(point(18.0F, 18.0F), point(21.0F, 21.0F), color, 1.8F * scale);
}

static void draw_fallback_close_icon(ImDrawList& draw_list, ImVec2 min, ImVec2 max, ImU32 color) {
    const float inset = std::min(max.x - min.x, max.y - min.y) * 0.28F;
    draw_list.AddLine({min.x + inset, min.y + inset}, {max.x - inset, max.y - inset}, color, 1.8F);
    draw_list.AddLine({max.x - inset, min.y + inset}, {min.x + inset, max.y - inset}, color, 1.8F);
}

static bool
draw_number_input(std::string_view label, int* values, int components = 1, float speed = 1.0F, int minimum = 0, int maximum = 0) {
    const bool limited = maximum > minimum;
    return draw_number_input(
        label, ImGuiDataType_S32, values, components, speed, limited ? &minimum : nullptr, limited ? &maximum : nullptr, "%d"
    );
}

Debugger::Debugger(Surface& target)
    : Container("ui debugger", "Debugger"), m_target(target), m_popup_state(std::make_unique<DebuggerPopupState>()),
      m_profile_state(std::make_unique<DebuggerProfileState>()) {
    set_size({grow(), grow()});
    set_visible(false);

    m_inspect_icon = m_target.runtime().textures().add("debugger-inspect", INSPECT_SVG);
    m_close_icon = m_target.runtime().textures().add("debugger-close", CLOSE_SVG);
}

void Debugger::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    configure_all_styles([&theme](Style& style) {
        style.color(theme.text_color)
            .background_color(theme.background_secondary_color)
            .border_color(theme.controls.border_color)
            .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
            .border_radius(0.0F)
            .padding({WINDOW_PADDING, WINDOW_PADDING});
    });
}

bool Debugger::paint() {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0F);
    const bool draw_content = Container::paint();
    ImGui::PopStyleVar();
    return draw_content;
}

void Debugger::draw_children() {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m_target.theme().controls.rounding);
    render();
    ImGui::PopStyleVar();
}

Debugger::~Debugger() {
    if (m_target.profiler().enabled() && m_target.profiler().has_report()) {
        m_target.profiler().save_report();
    }
}

void Debugger::set_hotkey(ImGuiKeyChord hotkey) {
    m_hotkey = hotkey;
}

void Debugger::set_target(Node* target) {
    if (target != m_node_target) {
        m_highlight_selected = false;
    }

    m_node_target = target;
    m_target_identity = target == nullptr ? 0 : target->identity();
    m_target_was_flow_position = target != nullptr && target->layout().in_flow();
    m_select_properties = target != nullptr;
    m_profile_state->valid = false;

    m_inspected_style = 0;

    if (m_node_target == nullptr) {
        m_highlight_valid = false;
        return;
    }

    refresh_highlight();
}

void Debugger::synchronize_targets() {
    Node* target = m_target_identity == 0 ? nullptr : find_node_by_identity(m_target.root(), m_target_identity);
    if (target != m_node_target) {
        set_target(target);
        if (target == nullptr) {
            m_scroll_to_target = false;
        }
    }

    if (m_hover_identity != 0) {
        m_hover_target = find_node_by_identity(m_target.root(), m_hover_identity);
        if (m_hover_target == nullptr) {
            m_hover_identity = 0;
        }
    } else {
        m_hover_target = nullptr;
    }
}

void Debugger::remove_target() {
    if (m_node_target == nullptr || m_node_target->parent() == nullptr) {
        return;
    }

    Node* target = m_node_target;
    Node* parent = target->parent();
    set_target(nullptr);
    m_hover_target = nullptr;
    m_scroll_to_target = false;

    if (std::unique_ptr<Node> detached = parent->detach(*target); detached != nullptr) {
        detached->set_visible(false);
        detached->set_input_enabled(false);
        m_detached_nodes.push_back(std::move(detached));
    }
}

void Debugger::set_open(bool open) {
    if (m_open == open) {
        return;
    }

    m_open = open;
    set_visible(open);

    auto* content = dynamic_cast<ResizableContainer*>(&m_target.root());
    if (content != nullptr) {
        content->set_resize(open ? ResizeAxes::X : ResizeAxes::None);
        if (!open) {
            content->set_size({grow(), grow()});
        }
    }

    if (open) {
        m_overlay_hover_started = -1.0;
        set_overlay_focus(false);
        return;
    }

    m_target.profiler().set_enabled(false);
    m_target.profiler().save_report();
    m_profile_state->valid = false;
    m_profile_state->frame_count = 0;
    m_profile_state->events.clear();

    m_overlay_rect = {};
    m_overlay_window_id = 0;
    set_overlay_focus(false);
    m_overlay_hover_started = -1.0;
    m_overlay_pointer_capture = false;
    m_inspect_pointer_capture = false;
    m_popup_state->clear();
    set_inspect_mode(false);
}

void Debugger::set_style(const ImGuiStyle& style) {
    if (m_target.imgui_context() == nullptr) {
        return;
    }

    const ImGuiContextScope scope(m_target.imgui_context());
    ImGui::GetStyle() = style;
}

void Debugger::set_font(std::string_view id, int size) {
    if (m_target.imgui_context() == nullptr) {
        return;
    }

    StyledNode::set_font(m_target.runtime().fonts().find(id), static_cast<float>(size));
}

bool Debugger::overlay_contains(ImVec2 position) const {
    return m_open && m_overlay_rect.valid() && m_overlay_rect.contains(position);
}

void Debugger::set_overlay_focus(bool focused) {
    if (m_overlay_focused == focused) {
        return;
    }

    m_overlay_focused = focused;
    m_target.input_router().set_debug_pointer_blocked(focused);
    if (focused) {
        m_target.input_router().set_focus(nullptr);
    }
}

void Debugger::update_overlay_focus(ImVec2 position) {
    if (m_inspect_mode || m_overlay_pointer_capture) {
        m_overlay_hover_started = -1.0;
        return;
    }

    if (!overlay_contains(position)) {
        m_overlay_hover_started = -1.0;
        set_overlay_focus(false);
        return;
    }

    if (m_overlay_focused) {
        return;
    }

    const double now = ImGui::GetTime();
    if (m_overlay_hover_started < 0.0) {
        m_overlay_hover_started = now;
    } else if (now - m_overlay_hover_started >= DEBUGGER_FOCUS_DELAY) {
        set_overlay_focus(true);
    }
}

bool Debugger::handles_content_resize(const UiEvent& event) const {
    if (!ui::contains(EventMask::Pointer, event_mask(event.type))) {
        return false;
    }

    const auto* content = dynamic_cast<const ResizableContainer*>(&m_target.root());
    if (content == nullptr) {
        return false;
    }

    if (content->resizing()) {
        return true;
    }

    return event.type == EventType::PointerDown && event.button == PointerButton::Left &&
           content->resize_handle_contains(event.position);
}

bool Debugger::handle_inspect_event(UiEvent& event) {
    const bool pointer_event = ui::contains(EventMask::Pointer, event_mask(event.type));
    const bool keyboard_event = ui::contains(EventMask::Keyboard, event_mask(event.type));

    if (m_inspect_mode && keyboard_event) {
        event.mark_handled();
        return true;
    }

    if (!pointer_event) {
        return false;
    }

    if (m_inspect_pointer_capture) {
        event.block_native_input();
        event.mark_handled();
        if (event.type == EventType::PointerUp) {
            m_inspect_pointer_capture = false;
        }
        return true;
    }

    if (m_overlay_pointer_capture) {
        event.mark_handled();
        if (event.type == EventType::PointerUp) {
            m_overlay_pointer_capture = false;
        }
        return true;
    }

    if (overlay_contains(event.position)) {
        if (event.type == EventType::PointerDown) {
            m_overlay_pointer_capture = true;
            // allow a debugger popup to close normally. otherwise save unrelated popups before imgui processes this panel press.
            if (debugger_popup_open(m_overlay_window_id)) {
                m_popup_state->clear();
            } else {
                m_popup_state->save();
            }
        }
        event.mark_handled();
        return true;
    }

    if (m_overlay_focused) {
        set_overlay_focus(false);
    }

    if (!m_inspect_mode) {
        return false;
    }

    event.block_native_input();
    event.mark_handled();
    if (event.type != EventType::PointerMove && event.type != EventType::PointerDown) {
        return true;
    }

    // popup blockers receive their final screen bounds after native popup windows draw, so resolve them before retained tree
    // order.
    Node* inspect_node = m_target.input_router().node_at(event.position, event.type);
    if (inspect_node == nullptr && is_effectively_visible(m_target.root())) {
        inspect_node = pick_node(m_target.root(), event.position);
    }
    if (inspect_node == nullptr) {
        m_hover_target = nullptr;
        m_hover_identity = 0;
    } else if (event.type == EventType::PointerDown && event.button == PointerButton::Left) {
        m_inspect_pointer_capture = true;
        set_target(inspect_node);
        m_hover_target = nullptr;
        m_hover_identity = 0;
        set_inspect_mode(false);
        m_scroll_to_target = true;
    } else {
        m_hover_target = inspect_node;
        m_hover_identity = inspect_node->identity();
    }

    return true;
}

bool Debugger::handle_input(UiEvent& event) {
    if (!m_open) {
        return false;
    }

    if (handles_content_resize(event)) {
        return false;
    }

    if (ui::contains(EventMask::Pointer, event_mask(event.type))) {
        update_overlay_focus(event.position);
    }

    if (handle_inspect_event(event)) {
        return true;
    }

    if (m_overlay_focused && ui::contains(EventMask::Keyboard, event_mask(event.type))) {
        event.mark_handled();
        return true;
    }

    if (event.type == EventType::PointerDown && event.button == PointerButton::Left && m_highlight_selected) {
        m_highlight_selected = false;
        m_highlight_valid = false;
    }

    return false;
}

void Debugger::set_inspect_mode(bool enabled) {
    m_inspect_mode = enabled;
    if (enabled) {
        set_overlay_focus(false);
        m_overlay_hover_started = -1.0;
        m_overlay_pointer_capture = false;
    }
    m_target.input_router().set_debug_inspect_mode(enabled);

    if (!enabled) {
        m_hover_target = nullptr;
        m_hover_identity = 0;
    }
}

void Debugger::handle_hotkey() {
    if (m_target.imgui_context() == nullptr) {
        return;
    }

    const ImGuiContextScope scope(m_target.imgui_context());
    // a new frame consumes the queued debugger press before popup widgets draw, so restore unrelated popups closed by focus
    // change.
    m_popup_state->restore();
    if (ImGui::IsKeyChordPressed(m_hotkey)) {
        toggle();
    }
}

void Debugger::finish_popup_restore() {
    // rendering can close the stack again when debugger widgets claim focus, so restore it before the backend reads draw data.
    m_popup_state->restore();
    m_popup_state->clear();
}

void Debugger::refresh_highlight() {
    uint64_t target_identity = 0;
    if (m_inspect_mode) {
        target_identity = m_hover_identity;
    } else if (m_highlight_selected) {
        target_identity = m_target_identity;
    }

    Node* target = target_identity == 0 ? nullptr : find_node_by_identity(m_target.root(), target_identity);
    if (target == nullptr || !is_effectively_visible(*target)) {
        m_highlight_valid = false;
        return;
    }

    const Rect rect = target->layout().visual_rect();
    if (rect.max.x <= rect.min.x || rect.max.y <= rect.min.y) {
        m_highlight_valid = false;
        return;
    }

    m_highlight = rect;
    m_highlight_valid = true;
}

bool Debugger::should_restore_flow_position(const LayoutConfig& config) const {
    if (m_node_target == nullptr || !m_target_was_flow_position) {
        return false;
    }

    const Placement& placement = config.placement;
    return placement.anchor.x == 0.0F && placement.anchor.y == 0.0F && placement.origin.x == 0.0F && placement.origin.y == 0.0F &&
           placement.offset.x == 0.0F && placement.offset.y == 0.0F;
}

void Debugger::draw_highlight() {
    synchronize_targets();
    refresh_highlight();

    if (!m_highlight_valid) {
        return;
    }

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->AddRect(m_highlight.min, m_highlight.max, ImColor(m_target.theme().accent_color), 0.0F, 0, 2.0F);

    const Node* target = m_inspect_mode ? m_hover_target : m_node_target;
    if (target == nullptr) {
        return;
    }

    const NodeLayout& target_layout = target->layout();
    const Rect local_rect = target_layout.local_rect();
    const Rect layout_rect = target_layout.layout_rect();
    const Placement& placement = target_layout.placement();
    ImVec2 origin_factor;
    ImVec2 anchor_factor;
    if (!target_layout.in_flow()) {
        origin_factor = placement.origin;
        anchor_factor = placement.anchor;
    }

    const Rect parent_rect = target_layout.parent_content_rect();
    const ImVec2 anchor_local = parent_rect.valid()
                                  ? ImVec2{parent_rect.min.x + (parent_rect.size().x * anchor_factor.x),
                                           parent_rect.min.y + (parent_rect.size().y * anchor_factor.y)}
                                  : local_rect.min;
    const ImVec2 origin_local = {
        local_rect.min.x + (local_rect.size().x * origin_factor.x), local_rect.min.y + (local_rect.size().y * origin_factor.y)
    };
    const ImVec2 screen_offset = {layout_rect.min.x - local_rect.min.x, layout_rect.min.y - local_rect.min.y};
    const auto to_screen = [screen_offset](ImVec2 position) {
        return ImVec2{position.x + screen_offset.x, position.y + screen_offset.y};
    };
    const ImVec2 anchor = to_screen(anchor_local);
    const ImVec2 origin = to_screen(origin_local);
    const ImVec2 node_top = {(m_highlight.min.x + m_highlight.max.x) * 0.5F, m_highlight.min.y};
    const float line_thickness = static_cast<float>(m_highlight_line_thickness);
    const auto draw_marker = [draw_list, node_top, line_thickness](ImVec2 position, ImVec4 color) {
        constexpr float MARKER_SIZE = 3.0F;
        const ImU32 marker_color = ImColor(color);
        draw_list->AddLine(position, node_top, marker_color, line_thickness);
        draw_list->AddRectFilled(
            {position.x - MARKER_SIZE, position.y - MARKER_SIZE}, {position.x + MARKER_SIZE, position.y + MARKER_SIZE},
            marker_color
        );
    };

    if (m_show_anchor) {
        draw_marker(anchor, m_anchor_color);
    }
    if (m_show_origin) {
        draw_marker(origin, m_origin_color);
    }
}

void Debugger::render_node_tree(Node& node, int depth) {
    const bool effectively_visible = is_effectively_visible(node);
    ImGui::PushID(&node);
    ImGui::PushStyleColor(
        ImGuiCol_Text, effectively_visible ? m_target.theme().text_color : m_target.theme().text_secondary_color
    );

    ImGuiTreeNodeFlags flags = depth < 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

    if (m_node_target != nullptr && m_node_target != &node && node.contains(m_node_target)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    if (m_target_identity != 0 && node.identity() == m_target_identity) {
        flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushStyleColor(ImGuiCol_Header, m_target.theme().accent_color);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, m_target.theme().accent_hover_color);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, m_target.theme().accent_color);
    }

    if (node.children().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    } else {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;
    }

    std::string_view node_id = node.id();
    if (node_id.starts_with("##")) {
        node_id.remove_prefix(2);
    }

    std::string node_label{node.type_name()};
    if (!node_id.empty()) {
        node_label += " (";
        node_label += node_id;
        node_label += ')';
    }

    if (node_label.empty()) {
        node_label = node_id.empty() ? "Unknown" : std::string(node_id);
    }

    const bool expanded = ImGui::TreeNodeEx(&node, flags, "%s", node_label.c_str());
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    if (m_target_identity != 0 && node.identity() == m_target_identity) {
        ImGui::PopStyleColor(3);
    }

    ImGui::PopStyleColor();
    ImGui::PopID();

    if (clicked) {
        if (m_node_target == &node) {
            m_highlight_selected = !m_highlight_selected;
        }
        set_target(&node);
        m_scroll_to_target = true;
    }

    if (&node == m_node_target && m_scroll_to_target) {
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        if (!ImGui::IsRectVisible(item_min, item_max)) ImGui::SetScrollHereY(0.5F);
    }

    if (expanded) {
        for (const auto& child : node.children()) {
            render_node_tree(*child, depth + 1);
        }

        if (!node.children().empty()) {
            ImGui::TreePop();
        }
    }
}

void Debugger::render_node_properties() {
    draw_property_section("node");

    bool visible = m_node_target->visible();
    if (draw_labeled_input("visible", [&visible] { return ImGui::Checkbox("##value", &visible); })) {
        m_node_target->set_visible(visible);
    }

    bool enabled = m_node_target->input_enabled();
    if (draw_labeled_input("input enabled", [&enabled] { return ImGui::Checkbox("##value", &enabled); })) {
        m_node_target->set_input_enabled(enabled);
    }

    bool node_enabled = m_node_target->enabled();
    if (draw_labeled_input("enabled", [&node_enabled] { return ImGui::Checkbox("##value", &node_enabled); })) {
        m_node_target->set_enabled(node_enabled);
    }

    draw_property_value("id", "{}", m_node_target->id().empty() ? "unnamed" : m_node_target->id().c_str());
    const std::string_view type = m_node_target->type_name();
    draw_property_value("type", "{}", type);
}

void Debugger::update_profile_snapshot() {
    DebuggerProfileState& profile = *m_profile_state;
    ++profile.frame_count;

    if (profile.valid && profile.frame_count % PROFILE_UPDATE_INTERVAL != 0) {
        return;
    }

    const Profiler& profiler = m_target.profiler();
    profile.frame_ms = profiler.latest_frame_ms();
    profile.metrics = profiler.latest_metrics();
    profile.gpu = profiler.gpu_render_summary();
    profile.dropped_events = profiler.dropped_events();
    profile.node_identity = m_node_target == nullptr ? 0 : m_node_target->identity();
    profile.node_ms = 0.0;
    profile.events.clear();

    if (profile.node_identity != 0) {
        profile.node_ms = profiler.node_duration_ms(profile.node_identity);
        for (const ProfileEvent& event : profiler.latest_events()) {
            if (event.node_identity == profile.node_identity) {
                profile.events.push_back(event);
            }
        }
    }

    profile.valid = true;
}

void Debugger::render_profiling() {
    const Profiler& profiler = m_target.profiler();
    if (!profiler.enabled()) {
        m_profile_state->valid = false;
        m_profile_state->frame_count = 0;
        ImGui::TextDisabled("enable frame time to collect profiling data");
        return;
    }

    update_profile_snapshot();
    const DebuggerProfileState& profile = *m_profile_state;
    const ProfileFrameMetrics& metrics = profile.metrics;

    const auto draw_event = [](const ProfileEvent& event) {
        draw_property_value(event.name, "{:.3f} ms", static_cast<double>(event.end - event.start) / 1'000'000.0);
    };

    if (m_node_target != nullptr) {
        draw_property_section("selected node");
        for (const ProfileEvent& event : profile.events) {
            if (event.name == "Node::update") {
                draw_event(event);
            }
        }
        draw_property_value("node total", "{:.3f} ms", profile.node_ms);
        for (const ProfileEvent& event : profile.events) {
            if (event.name != "Node::update") {
                draw_event(event);
            }
        }
        end_property_section();
    }

    draw_property_section("frame");
    draw_property_value("frame", "{:.3f} ms", profile.frame_ms);
    draw_property_value("update", "{:.3f} ms", metrics.update_ms);
    draw_property_value("measure", "{:.3f} ms", metrics.measure_ms);
    draw_property_value("layout", "{:.3f} ms", metrics.layout_ms);
    draw_property_value("draw", "{:.3f} ms", metrics.draw_ms);
    draw_property_value("input", "{:.3f} ms", metrics.input_ms);
    draw_property_value("render call", "{:.3f} ms", metrics.render_ms);
    if (profile.gpu.samples > 0) {
        draw_property_value("render gpu avg", "{:.3f} ms ({} samples)", profile.gpu.average_ms, profile.gpu.samples);
        draw_property_value("render gpu range", "{:.3f}–{:.3f} ms", profile.gpu.minimum_ms, profile.gpu.maximum_ms);
    } else {
        draw_property_value("render gpu avg", "{}", "n/a");
    }
    end_property_section();

    draw_property_section("work");
    draw_property_value("nodes", "{}", metrics.nodes_drawn);
    draw_property_value("input work", "{} entries / {} checks", metrics.input_entries, metrics.input_entry_checks);
    draw_property_value("dropped events", "{}", profile.dropped_events);
    end_property_section();
}

void Debugger::render_highlight_properties() {
    draw_property_section("highlight");
    draw_highlight_option("show anchor", m_show_anchor, m_anchor_color);
    draw_highlight_option("show origin", m_show_origin, m_origin_color);
    if (m_show_anchor || m_show_origin) {
        draw_number_input(
            "line thickness", &m_highlight_line_thickness, 1, 1.0F, HIGHLIGHT_MIN_LINE_THICKNESS, HIGHLIGHT_MAX_LINE_THICKNESS
        );
    }
    end_property_section();
}

void Debugger::render_layout_properties() {
    draw_property_section("layout");

    const NodeLayout& layout = m_node_target->layout();
    const LayoutConfig& request = layout.config();
    const auto update_request = [&](auto&& update) {
        LayoutConfig config = request;
        update(config);
        m_node_target->set_layout(config);
    };
    draw_property_value("placement", "{}", request.in_flow ? "flow" : "explicit");

    const ImVec2 measured = layout.measured_size();
    const ImVec2 intrinsic = layout.intrinsic_size();
    const ImVec2 available = layout.available_size();
    const LayoutSize& size_spec = layout.size_spec();
    const auto update_size_axis = [this, &size_spec, &layout](bool width, LayoutSizeMode mode) {
        LayoutSize size = size_spec;
        LayoutAxis& axis = width ? size.width : size.height;
        const float resolved = width ? layout.size().x : layout.size().y;
        axis.mode = mode;
        if (mode == LayoutSizeMode::Fixed) {
            axis.value = resolved;
        } else if (mode == LayoutSizeMode::Percent) {
            axis.value = 100.0f;
        } else if (mode == LayoutSizeMode::Grow) {
            axis.value = 1.0f;
        } else {
            axis.value = 0.0f;
        }
        m_node_target->set_size(size);
    };

    int width_mode = static_cast<int>(size_spec.width.mode);
    if (draw_inline_combo("width rule", width_mode, SIZE_MODE_NAMES, IM_ARRAYSIZE(SIZE_MODE_NAMES))) {
        update_size_axis(true, static_cast<LayoutSizeMode>(width_mode));
    }

    int height_mode = static_cast<int>(size_spec.height.mode);
    if (draw_inline_combo("height rule", height_mode, SIZE_MODE_NAMES, IM_ARRAYSIZE(SIZE_MODE_NAMES))) {
        update_size_axis(false, static_cast<LayoutSizeMode>(height_mode));
    }

    const auto draw_size_value = [this, &size_spec](bool width, LayoutSizeMode mode) {
        const LayoutAxis& axis = width ? size_spec.width : size_spec.height;
        float value = axis.value;
        const bool limited = mode == LayoutSizeMode::Percent;
        if (!draw_number_input(width ? "width" : "height", &value, 1, 0.1F, 0.0F, limited ? 100.0F : 0.0F)) {
            return;
        }

        LayoutSize updated = size_spec;
        LayoutAxis& updated_axis = width ? updated.width : updated.height;
        updated_axis.value = value;
        m_node_target->set_size(updated);
    };

    if (size_spec.width.mode == LayoutSizeMode::Fixed) draw_size_value(true, LayoutSizeMode::Fixed);
    if (size_spec.height.mode == LayoutSizeMode::Fixed) draw_size_value(false, LayoutSizeMode::Fixed);
    if (size_spec.width.mode == LayoutSizeMode::Percent) draw_size_value(true, LayoutSizeMode::Percent);
    if (size_spec.height.mode == LayoutSizeMode::Percent) draw_size_value(false, LayoutSizeMode::Percent);

    const auto draw_limits = [this](const char* minimum_label, const char* maximum_label, const LayoutAxis& axis) {
        draw_property_value(minimum_label, "{:.1f}{}", axis.minimum.value, axis.minimum.percentage ? "%" : "px");
        if (std::isfinite(axis.maximum.value)) {
            draw_property_value(maximum_label, "{:.1f}{}", axis.maximum.value, axis.maximum.percentage ? "%" : "px");
        } else {
            draw_property_value(maximum_label, "{}", "unbounded");
        }
    };
    draw_limits("minimum width", "maximum width", size_spec.width);
    draw_limits("minimum height", "maximum height", size_spec.height);

    draw_property_value("measured", "{:.1f} x {:.1f}", measured.x, measured.y);
    draw_property_value("intrinsic", "{:.1f} x {:.1f}", intrinsic.x, intrinsic.y);
    draw_property_value("available", "{:.1f} x {:.1f}", available.x, available.y);

    const Rect arranged = layout.layout_rect();
    const Rect visual = layout.visual_rect();
    draw_property_value(
        "layout rect", "({:.1f}, {:.1f}) - ({:.1f}, {:.1f})", arranged.min.x, arranged.min.y, arranged.max.x, arranged.max.y
    );
    draw_property_value(
        "visual rect", "({:.1f}, {:.1f}) - ({:.1f}, {:.1f})", visual.min.x, visual.min.y, visual.max.x, visual.max.y
    );

    ImVec2 offset = request.placement.offset;
    if (draw_number_input("offset", &offset.x, 2)) {
        update_request([&](LayoutConfig& config) {
            config.placement.offset = offset;
            config.in_flow = false;
        });
    }

    const auto edit_anchor = [&](const char* label, const char* point_label, ImVec2 Placement::* member) {
        ImVec2 point = request.placement.*member;
        int selected = IM_ARRAYSIZE(ALIGNMENTS);
        for (int index = 0; index < IM_ARRAYSIZE(ALIGNMENTS); ++index) {
            if (point.x == ALIGNMENTS[index].x && point.y == ALIGNMENTS[index].y) selected = index;
        }

        bool changed = draw_inline_combo(label, selected, ALIGNMENT_NAMES, IM_ARRAYSIZE(ALIGNMENT_NAMES));
        if (changed && selected < IM_ARRAYSIZE(ALIGNMENTS)) point = ALIGNMENTS[selected];
        changed |= draw_number_input(point_label, &point.x, 2, 0.01F);
        if (changed) {
            update_request([&](LayoutConfig& config) {
                config.placement.*member = point;
                config.in_flow = should_restore_flow_position(config);
            });
        }
    };

    edit_anchor("anchor (parent)", "anchor point", &Placement::anchor);
    edit_anchor("origin (node)", "origin point", &Placement::origin);
}

void Debugger::render_style_variables(Style& style, std::span<Style*> all_styles) {
    StyleVariableStore& variables = style.variables();
    m_variable_names.clear();

    const auto collect_names = [this](Style& candidate) {
        for (const auto& [name, value] : candidate.variables()) {
            m_variable_names.push_back(name);
        }
    };

    collect_names(style);
    for (Style* candidate : all_styles) {
        if (candidate != nullptr && candidate != &style) {
            collect_names(*candidate);
        }
    }

    std::ranges::sort(m_variable_names);
    m_variable_names.erase(std::unique(m_variable_names.begin(), m_variable_names.end()), m_variable_names.end());

    if (m_variable_names.empty()) {
        return;
    }

    draw_property_section("variables");
    for (const std::string& name : m_variable_names) {
        StyleValue* variable = variables.find(name);
        if (variable == nullptr) {
            for (Style* candidate : all_styles) {
                if (candidate == nullptr) {
                    continue;
                }

                variable = candidate->variables().find(name);
                if (variable != nullptr) {
                    break;
                }
            }
        }

        if (variable == nullptr) {
            continue;
        }

        const auto apply_variable = [&style, all_styles, &name](auto value) {
            apply_to_styles(style, all_styles, [&name, &value](Style& target) { target.variables().set(name, value); });
        };

        std::visit(
            [&](auto& value) {
                using ValueType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<ValueType, FloatValue>) {
                    float current = value.value;
                    if (draw_number_input(name, &current, 1, 0.01F)) {
                        apply_variable(FloatValue{current});
                    }
                } else if constexpr (std::is_same_v<ValueType, IntValue>) {
                    int current = value.value;
                    if (draw_number_input(name, &current)) {
                        apply_variable(IntValue{current});
                    }
                } else if constexpr (std::is_same_v<ValueType, BoolValue>) {
                    bool current = value.value;
                    if (draw_labeled_input(name, [&current] { return ImGui::Checkbox("##value", &current); })) {
                        apply_variable(BoolValue{current});
                    }
                } else if constexpr (std::is_same_v<ValueType, StringValue>) {
                    std::string current = value.value;
                    if (draw_text_input(name, current)) {
                        apply_variable(StringValue{std::move(current)});
                    }
                } else if constexpr (std::is_same_v<ValueType, ColorValue>) {
                    ImVec4 current = value.value.rgba();
                    if (draw_color_input(name, current)) {
                        apply_variable(ColorValue{ImColor{current}});
                    }
                } else if constexpr (std::is_same_v<ValueType, Vec2Value>) {
                    ImVec2 current = value.value;
                    if (draw_number_input(name, &current.x, 2, 0.01F)) {
                        apply_variable(Vec2Value{current});
                    }
                }
            },
            *variable
        );
    }
}

void Debugger::render_style_controls(Style& style, bool is_line, std::span<Style*> all_styles) {
    const auto apply = [&style, all_styles](auto&& update) {
        apply_to_styles(style, all_styles, std::forward<decltype(update)>(update));
    };

    const auto edit_color = [&](std::string_view label, const ColorValue& value, Style& (Style::*setter)(Color, TransitionSpec)) {
        ImVec4 color = value.get();
        if (draw_color_input(label, color)) {
            apply([&](Style& target) { (target.*setter)(ImColor{color}, {}); });
        }
    };

    edit_color("color", style.color(), &Style::color);

    float alpha = style.alpha();
    if (draw_number_input("alpha", &alpha, 1, 0.01F, 0.0F, 1.0F)) {
        apply([alpha](Style& target) { target.alpha(alpha); });
    }

    float thickness = style.border_thickness();
    if (draw_number_input("border thickness", &thickness, 1, 0.1F, 0.0F, 16.0F)) {
        apply([thickness](Style& target) { target.border_thickness(thickness); });
    }

    if (!is_line) {
        edit_color("background", style.background_color(), &Style::background_color);
        edit_color("border color", style.border_color(), &Style::border_color);

        const auto edit_border = [&](const char* label, BorderEdge edge,
                                     Style& (Style::*setter)(float, BorderType, TransitionSpec)) {
            ImGui::PushID(label);
            const bool size_changed = draw_number_input(label, &edge.size, 1, 0.1F, 0.0F, 128.0F);
            int type = static_cast<int>(edge.type);
            const bool type_changed = draw_inline_combo("type", type, BORDER_STYLE_NAMES, IM_ARRAYSIZE(BORDER_STYLE_NAMES));
            if (size_changed || type_changed) {
                apply([edge, type, setter](Style& target) { (target.*setter)(edge.size, static_cast<BorderType>(type), {}); });
            }
            ImGui::PopID();
        };
        edit_border("border left", style.border_left(), &Style::border_left);
        edit_border("border top", style.border_top(), &Style::border_top);
        edit_border("border right", style.border_right(), &Style::border_right);
        edit_border("border bottom", style.border_bottom(), &Style::border_bottom);

        float radius = style.border_radius();
        if (draw_number_input("border radius", &radius, 1, 0.1F, 0.0F, 64.0F)) {
            apply([radius](Style& target) { target.border_radius(radius); });
        }

        int blur = style.blur();
        if (draw_number_input("blur", &blur, 1, 1.0F, 0, MAX_BLUR_STRENGTH)) {
            apply([blur](Style& target) { target.blur(blur); });
        }

        BoxShadow shadow = style.box_shadow();
        if (draw_number_input("shadow offset", &shadow.offset.x, 2, 0.1F)) {
            apply([shadow](Style& target) { target.box_shadow(shadow); });
        }

        if (draw_slider("shadow blur", &shadow.blur, 0.0F, 256.0F)) {
            apply([shadow](Style& target) { target.box_shadow(shadow); });
        }

        if (draw_slider("shadow spread", &shadow.spread, -128.0F, 256.0F)) {
            apply([shadow](Style& target) { target.box_shadow(shadow); });
        }

        ImVec4 shadow_color = shadow.color.rgba();
        if (draw_color_input("shadow color", shadow_color)) {
            shadow.color = shadow_color;
            apply([shadow](Style& target) { target.box_shadow(shadow); });
        }

        BoxInsets padding = style.padding();
        if (draw_number_input("padding (left/top/right/bottom)", &padding.left, 4, 0.1F, 0.0F, 128.0F)) {
            apply([padding](Style& target) { target.padding(padding); });
        }

        int box_sizing = static_cast<int>(style.box_sizing());
        if (draw_inline_combo("box sizing", box_sizing, BOX_SIZING_NAMES, IM_ARRAYSIZE(BOX_SIZING_NAMES))) {
            apply([box_sizing](Style& target) { target.box_sizing(static_cast<BoxSizing>(box_sizing)); });
        }

        int overflow = static_cast<int>(style.overflow());
        if (draw_inline_combo("overflow", overflow, OVERFLOW_NAMES, IM_ARRAYSIZE(OVERFLOW_NAMES))) {
            apply([overflow](Style& target) { target.overflow(static_cast<Overflow>(overflow)); });
        }

        BoxInsets margin = style.margin();
        if (draw_number_input("margin (left/top/right/bottom)", &margin.left, 4, 0.1F, 0.0F, 128.0F)) {
            apply([margin](Style& target) { target.margin(margin); });
        }

        float scrollbar_size = style.scrollbar_size();
        if (draw_number_input("scrollbar size", &scrollbar_size, 1, 0.1F, 0.0F, 64.0F)) {
            apply([scrollbar_size](Style& target) { target.scrollbar_size(scrollbar_size); });
        }

        float scrollbar_rounding = style.scrollbar_rounding();
        if (draw_number_input("scrollbar rounding", &scrollbar_rounding, 1, 0.1F, 0.0F, 64.0F)) {
            apply([scrollbar_rounding](Style& target) { target.scrollbar_rounding(scrollbar_rounding); });
        }

        float scrollbar_grab_size = style.scrollbar_minimum_grab_size();
        if (draw_number_input("scrollbar grab size", &scrollbar_grab_size, 1, 0.1F, 1.0F, 64.0F)) {
            apply([scrollbar_grab_size](Style& target) { target.scrollbar_minimum_grab_size(scrollbar_grab_size); });
        }

        float scrollbar_grab_rounding = style.scrollbar_grab_rounding();
        if (draw_number_input("scrollbar grab rounding", &scrollbar_grab_rounding, 1, 0.1F, 0.0F, 64.0F)) {
            apply([scrollbar_grab_rounding](Style& target) { target.scrollbar_grab_rounding(scrollbar_grab_rounding); });
        }

        edit_color("scrollbar background", style.scrollbar_background_color(), &Style::scrollbar_background_color);
        edit_color("scrollbar grab", style.scrollbar_grab_color(), &Style::scrollbar_grab_color);
        edit_color("scrollbar grab hovered", style.scrollbar_grab_hovered_color(), &Style::scrollbar_grab_hovered_color);
        edit_color("scrollbar grab active", style.scrollbar_grab_active_color(), &Style::scrollbar_grab_active_color);
    }
}

void Debugger::render_decoration_properties(StyledNode& node) {
    const auto render = [&](std::string_view label, bool before) {
        draw_property_section(label);

        bool enabled = before ? node.has_before() : node.has_after();
        if (draw_labeled_input("enabled", [&enabled] { return ImGui::Checkbox("##value", &enabled); })) {
            if (before) {
                if (enabled) {
                    node.before();
                } else {
                    node.remove_before();
                }
            } else {
                if (enabled) {
                    node.after();
                } else {
                    node.remove_after();
                }
            }
        }

        if (!enabled) {
            return;
        }

        PaintSlot& slot = before ? node.before() : node.after();
        render_style_controls(slot.style());
    };

    render("before", true);
    render("after", false);
}

void Debugger::render_style_properties() {
    auto* styled = dynamic_cast<StyledNode*>(m_node_target);
    if (styled == nullptr) {
        return;
    }

    draw_property_section("style");

    int style_index = m_inspected_style;
    if (draw_inline_combo("state", style_index, STYLE_NAMES, IM_ARRAYSIZE(STYLE_NAMES))) {
        m_inspected_style = style_index;
    }

    const bool all_styles = style_index == 0;
    const int selected_style = std::clamp(style_index - 1, 0, static_cast<int>(StyleType::COUNT) - 1);
    Style& style = styled->style(static_cast<StyleType>(selected_style));

    std::array<Style*, static_cast<std::size_t>(StyleType::COUNT)> style_targets{};
    std::span<Style*> all_style_targets;
    if (all_styles) {
        for (std::size_t index = 0; index < style_targets.size(); ++index) {
            style_targets[index] = &styled->style(static_cast<StyleType>(index));
        }
        all_style_targets = std::span<Style*>(style_targets);
    }

    render_style_controls(style, styled->type_name() == "Line", all_style_targets);
    render_style_variables(style, all_style_targets);
    render_decoration_properties(*styled);
}

void Debugger::render_properties() {
    if (m_node_target == nullptr) {
        end_property_section();
        ImGui::TextUnformatted("select a node from the list");
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ITEM_SPACING, 8.0F});
    render_node_properties();
    render_layout_properties();
    render_style_properties();
    ImGui::PopStyleVar();
    end_property_section();

    if (ImGui::Button("clear selection")) {
        set_target(nullptr);
        m_scroll_to_target = false;
    }

    ImGui::SameLine();
    const bool removable = m_node_target->parent() != nullptr;
    ImGui::BeginDisabled(!removable);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45F, 0.12F, 0.14F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58F, 0.16F, 0.18F, 1.0F));
    if (ImGui::Button("remove node")) {
        remove_target();
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("the root node cannot be removed");
    }
}

static bool draw_icon_button(
    const char* id, Texture* texture, ImVec2 icon_size, ImU32 icon_color,
    void (*draw_fallback)(ImDrawList&, ImVec2, ImVec2, ImU32)
) {
    const float button_size = ImGui::GetFrameHeight();
    const bool clicked = ImGui::InvisibleButton(id, {button_size, button_size});
    const ImVec2 button_min = ImGui::GetItemRectMin();
    const ImVec2 button_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (ImGui::IsItemHovered()) {
        draw_list->AddRectFilled(
            button_min, button_max, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), ImGui::GetStyle().FrameRounding
        );
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const ImVec2 center = {(button_min.x + button_max.x) * 0.5F, (button_min.y + button_max.y) * 0.5F};
    const ImVec2 icon_min = {center.x - (icon_size.x * 0.5F), center.y - (icon_size.y * 0.5F)};
    const ImVec2 icon_max = {icon_min.x + icon_size.x, icon_min.y + icon_size.y};
    if (texture != nullptr) {
        draw_list->AddImage(texture->get(icon_size), icon_min, icon_max, {0.0F, 0.0F}, {1.0F, 1.0F}, icon_color);
    } else if (draw_fallback != nullptr) {
        draw_fallback(*draw_list, icon_min, icon_max, icon_color);
    }

    return clicked;
}

void Debugger::render_toolbar() {
    const bool inspect_clicked = draw_icon_button(
        "##debug-inspect", m_inspect_icon, INSPECT_ICON_SIZE,
        ImGui::GetColorU32(m_inspect_mode ? m_target.theme().accent_color : m_target.theme().text_secondary_color),
        draw_fallback_inspect_icon
    );
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(m_inspect_mode ? "stop inspect" : "inspect");
    }

    if (inspect_clicked) {
        set_inspect_mode(!m_inspect_mode);
    }

    ImGui::SameLine(0.0F, 6.0F);
    Profiler& profiler = m_target.profiler();
    if (profiler.enabled()) {
        ImGui::PushStyleColor(ImGuiCol_Button, m_target.theme().accent_color);
    }
    const bool profiling_clicked = ImGui::Button("frame time");
    if (profiler.enabled()) {
        ImGui::PopStyleColor();
    }
    if (profiling_clicked) {
        profiler.set_enabled(!profiler.enabled());
    }

    ImGui::SameLine(0.0F, 10.0F);
    ImGui::TextDisabled("%zu nodes", m_target.root().children().size());

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
    const bool close_clicked = draw_icon_button(
        "##debug-close", m_close_icon, CLOSE_ICON_SIZE, ImGui::GetColorU32(m_target.theme().text_secondary_color),
        draw_fallback_close_icon
    );
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("close debugger");
    }
    if (close_clicked) {
        set_open(false);
    }

    ImGui::Separator();
}

void Debugger::render_node_list(float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {WINDOW_PADDING, 4.0F});
    ImGui::BeginChild("##debugger-nodes", {0.0F, height}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    for (const auto& child : m_target.root().children()) {
        render_node_tree(*child, 0);
    }

    m_scroll_to_target = false;
    ImGui::EndChild();
}

void Debugger::render_sections() {
    ImGui::BeginChild("##debugger-sections", {0.0F, 0.0F}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoBackground);

    if (ImGui::BeginTabBar("##debugger-sections-tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const ImGuiTabItemFlags properties_flags = m_select_properties ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("properties", nullptr, properties_flags)) {
            m_select_properties = false;
            ImGui::BeginChild("##debugger-properties-content", {0.0F, 0.0F}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            render_properties();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("profiling")) {
            ImGui::BeginChild("##debugger-profiling-content", {0.0F, 0.0F}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            render_profiling();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("highlight")) {
            ImGui::BeginChild("##debugger-highlight-content", {0.0F, 0.0F}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            render_highlight_properties();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();
}

void Debugger::render() {
    if (!m_open || m_target.imgui_context() == nullptr) {
        return;
    }

    const ImGuiContextScope scope(m_target.imgui_context());
    ImGuiWindow* debugger_window = ImGui::GetCurrentWindow();
    m_overlay_rect = Rect::from_position_size(ImGui::GetWindowPos(), ImGui::GetWindowSize());
    m_overlay_window_id = debugger_window == nullptr ? 0 : debugger_window->ID;
    update_overlay_focus(ImGui::GetMousePos());
    draw_highlight();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ITEM_SPACING, 4.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0F, 4.0F});
    render_toolbar();

    const float available_height = std::max(0.0F, ImGui::GetContentRegionAvail().y);
    const float splitter_height = std::min(DEBUGGER_SPLITTER_HEIGHT, available_height);
    const float panes_height = std::max(0.0F, available_height - splitter_height);
    const float min_pane_height = std::min(DEBUGGER_MIN_PANE_HEIGHT, panes_height * 0.5F);
    const float max_node_list_height = std::max(min_pane_height, panes_height - min_pane_height);
    float node_list_height = std::clamp(panes_height * m_node_list_ratio, min_pane_height, max_node_list_height);

    render_node_list(node_list_height);

    const ImVec2 splitter_size = {std::max(0.0F, ImGui::GetContentRegionAvail().x), splitter_height};
    ImGui::InvisibleButton("##debugger-node-splitter", splitter_size);
    const bool splitter_hovered = ImGui::IsItemHovered();
    const bool splitter_active = ImGui::IsItemActive();
    if (splitter_hovered || splitter_active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    const ImVec2 splitter_min = ImGui::GetItemRectMin();
    const ImVec2 splitter_max = ImGui::GetItemRectMax();
    ImGuiCol splitter_color_id = ImGuiCol_Separator;
    if (splitter_hovered) splitter_color_id = ImGuiCol_SeparatorHovered;
    if (splitter_active) splitter_color_id = ImGuiCol_SeparatorActive;
    const ImU32 splitter_color = ImGui::GetColorU32(splitter_color_id);
    ImGui::GetWindowDrawList()->AddRectFilled(
        {splitter_min.x, splitter_min.y + (splitter_height * 0.5F) - 0.5F},
        {splitter_max.x, splitter_min.y + (splitter_height * 0.5F) + 0.5F}, splitter_color
    );

    if (splitter_active) {
        node_list_height = std::clamp(node_list_height + ImGui::GetIO().MouseDelta.y, min_pane_height, max_node_list_height);
        m_node_list_ratio = panes_height > 0.0F ? node_list_height / panes_height : 0.6F;
    }

    render_sections();
    ImGui::PopStyleVar(2);

    if (debugger_window != nullptr) {
        bring_debugger_to_front(*debugger_window);
    }
}
