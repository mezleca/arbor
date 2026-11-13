#include <arbor/imgui/paint-state.hpp>
#include <arbor/imgui/effects/effects.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <iterator>
#include <stdexcept>

using namespace ui;

void PaintState::begin_frame() {
    assert(m_clip_stack.empty()); // NOLINT(cert-dcl03-c,misc-static-assert): the clip stack changes at runtime.
    m_clip_cursor = 0;
    m_clipped_lists.clear();
}

void PaintState::push_clip(Rect rect, float rounding, bool intersect) {
    const RoundedClip* parent = intersect ? m_clip : nullptr;
    rounding = std::clamp(rounding, 0.0f, std::max(0.0f, std::min(rect.size().x, rect.size().y) * 0.5f));
    if (rounding > 0.0f && parent != nullptr && parent->depth == 64) {
        throw std::length_error("rounded clip nesting exceeds 64 regions");
    }

    m_clip_stack.push_back(m_clip);
    m_clip = parent;
    if (rounding == 0.0f) return;

    // queued callbacks keep these addresses until rendering ends. reuse stable deque entries on the next frame.
    if (m_clip_cursor == m_clip_regions.size()) m_clip_regions.emplace_back();
    RoundedClip& clip = m_clip_regions[m_clip_cursor++];
    clip = {rect, rounding, parent, ++m_clip_id, parent != nullptr ? parent->depth + 1 : 1};
    m_clip = &clip;
}

void PaintState::pop_clip() {
    assert(!m_clip_stack.empty()); // NOLINT(cert-dcl03-c,misc-static-assert): the clip stack changes at runtime.
    m_clip = m_clip_stack.back();
    m_clip_stack.pop_back();
}

void PaintState::apply_clip(ImDrawList& draw_list, EffectRegistry* effects) {
    if (m_clip_cursor == 0 || effects == nullptr) return;

    auto found = std::ranges::find(m_clipped_lists, &draw_list, &std::pair<ImDrawList*, const RoundedClip*>::first);
    if (found == m_clipped_lists.end()) {
        if (m_clip == nullptr) return;
        m_clipped_lists.emplace_back(&draw_list, nullptr);
        found = std::prev(m_clipped_lists.end());
    }
    if (found->second == m_clip) return;

    effects->effect(EffectSlot::Clip).submit(draw_list, ClipCommand{m_clip});
    found->second = m_clip;
}

void PaintState::finish_frame(EffectRegistry& effects) {
    assert(m_clip_stack.empty()); // NOLINT(cert-dcl03-c,misc-static-assert): the clip stack changes at runtime.

    // imgui renders child windows as separate draw lists. no list may leave its mask active for the next window.
    for (auto& [list, clip] : m_clipped_lists) {
        if (clip != nullptr) effects.effect(EffectSlot::Clip).submit(*list, ClipCommand{});
        clip = nullptr;
    }
}

ImVec4 PaintState::effect_clip(ImVec4 fallback) const {
    return m_effect_clips.empty() ? fallback : m_effect_clips.back();
}

void PaintState::push_effect_clip(ImVec4 clip) {
    m_effect_clips.push_back(clip);
}

void PaintState::pop_effect_clip() {
    assert(!m_effect_clips.empty()); // NOLINT(cert-dcl03-c,misc-static-assert): the clip stack changes at runtime.
    m_effect_clips.pop_back();
}

const BorderPath& PaintState::border_path(Rect rect, float rounding) {
    // mix the full bit patterns so repeated integer coordinates do not collide on their low mantissa bits.
    const auto bits = [](float value) { return static_cast<uint64_t>(std::bit_cast<uint32_t>(value)); };
    const uint64_t hash = (bits(rect.min.x) * 0x9e3779b97f4a7c15ULL) ^ (bits(rect.min.y) * 0xc2b2ae3d27d4eb4fULL) ^
                          (bits(rect.max.x) * 0x165667b19e3779f9ULL) ^ (bits(rect.max.y) * 0x27d4eb2f165667c5ULL) ^
                          (bits(rounding) * 0x85ebca77c2b2ae63ULL);
    BorderEntry& entry = m_border_paths[hash % m_border_paths.size()];

    if (entry.valid && entry.rect.min.x == rect.min.x && entry.rect.min.y == rect.min.y && entry.rect.max.x == rect.max.x &&
        entry.rect.max.y == rect.max.y && entry.rounding == rounding) {
        return entry.path;
    }

    entry.rect = rect;
    entry.rounding = rounding;
    entry.path = rounded_rect_border_path(rect, rounding);
    entry.valid = true;
    return entry.path;
}
