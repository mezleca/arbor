#include <arbor/layout/scroll.hpp>

#include <arbor/transition.hpp>

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>

using namespace ui;

void Scroll::set_axes(bool vertical, bool horizontal) {
    m_vertical = vertical;
    m_horizontal = horizontal;
}

Scroll& Scroll::set_behaviour(ScrollBehavior behavior) {
    m_behaviour = behavior == ScrollBehavior::Default ? ScrollBehavior::Smooth : behavior;
    return *this;
}

void Scroll::seek_to(ImVec2 position, ScrollBehavior behavior) {
    // input can request a seek between frames, so advance applies it after beginchild opens the imgui window.
    m_request = Request{
        {std::max(0.0F, position.x), std::max(0.0F, position.y)},
        m_position,
        behavior == ScrollBehavior::Default ? m_behaviour : behavior,
    };
}

void Scroll::seek_by(ImVec2 distance, ScrollBehavior behavior) {
    // repeated seeks add to the pending destination instead of restarting from the last rendered frame.
    const ImVec2 from = destination();
    seek_to({from.x + distance.x, from.y + distance.y}, behavior);
}

bool Scroll::wheel(ImVec2 delta) {
    // accumulate wheel movement against the pending destination, then clamp it to the content bounds.
    // returning false leaves the event available to a scrollable parent.
    const ImVec2 from = destination();
    ImVec2 target = from;
    if (m_horizontal && delta.x != 0.0F) target.x = std::clamp(from.x - (delta.x * m_wheel_step.x), 0.0F, m_max.x);
    if (m_vertical && delta.y != 0.0F) target.y = std::clamp(from.y - (delta.y * m_wheel_step.y), 0.0F, m_max.y);
    if (target.x == from.x && target.y == from.y) return false;
    seek_to(target);
    return true;
}

void Scroll::advance(ImGuiWindow& window) {
    // beginchild has resolved native scroll and content bounds for this frame.
    // use imgui's wheel distances so routed wheel input moves the same amount as native wheel input.
    m_position = window.Scroll;
    m_max = window.ScrollMax;
    m_wheel_step = {
        std::trunc(std::min(2.0F * window.FontRefSize, window.InnerRect.GetWidth() * 0.67F)),
        std::trunc(std::min(5.0F * window.FontRefSize, window.InnerRect.GetHeight() * 0.67F)),
    };
    if (!m_request) return;

    // a native scrollbar drag changes window.Scroll directly, so discard the previous seek destination.
    if (GImGui->ActiveId != 0 && (GImGui->ActiveId == ImGui::GetWindowScrollbarID(&window, ImGuiAxis_X) ||
                                  GImGui->ActiveId == ImGui::GetWindowScrollbarID(&window, ImGuiAxis_Y))) {
        m_request.reset();
        return;
    }

    const ImVec2 target = {
        m_horizontal ? std::clamp(m_request->target.x, 0.0F, m_max.x) : m_position.x,
        m_vertical ? std::clamp(m_request->target.y, 0.0F, m_max.y) : m_position.y,
    };
    // interpolate from the offset captured by seek_to and queue the next offset through ImGui's scroll API.
    constexpr float duration = 0.2F;
    m_request->elapsed = std::min(duration, m_request->elapsed + ImGui::GetIO().DeltaTime);
    const float progress =
        m_request->behavior == ScrollBehavior::Smooth ? easing::out_cubic(m_request->elapsed / duration) : 1.0F;
    const ImVec2 next = {
        std::lerp(m_request->start.x, target.x, progress),
        std::lerp(m_request->start.y, target.y, progress),
    };
    if (m_horizontal && next.x != m_position.x) ImGui::SetScrollX(next.x);
    if (m_vertical && next.y != m_position.y) ImGui::SetScrollY(next.y);

    // a request made before the first draw stays pending while imgui still reports zero content range.
    if (progress >= 1.0F && (!m_horizontal || m_max.x > 0.0F || m_request->target.x == 0.0F) &&
        (!m_vertical || m_max.y > 0.0F || m_request->target.y == 0.0F)) {
        m_request.reset();
    }
}
