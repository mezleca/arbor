#pragma once

#include "border.hpp"

#include <array>
#include <deque>
#include <utility>
#include <vector>

namespace ui {
    class EffectRegistry;

    struct RoundedClip {
        Rect rect;
        float rounding = 0.0f;
        const RoundedClip* parent = nullptr;
        uint64_t id = 0;
        int depth = 0;
    };

    struct ClipCommand {
        const RoundedClip* clip = nullptr;
        bool restore = false;
    };

    class PaintState {
    public:
        PaintState() = default;
        PaintState(const PaintState&) = delete;
        PaintState& operator=(const PaintState&) = delete;

        void begin_frame();
        void finish_frame(EffectRegistry& effects);
        void push_clip(Rect rect, float rounding, bool intersect);
        void pop_clip();
        void apply_clip(ImDrawList& draw_list, EffectRegistry* effects);

        ImVec4 effect_clip(ImVec4 fallback) const;
        void push_effect_clip(ImVec4 clip);
        void pop_effect_clip();

        const BorderPath& border_path(Rect rect, float rounding);

    private:
        struct BorderEntry {
            Rect rect;
            float rounding = 0.0F;
            BorderPath path;
            bool valid = false;
        };

        std::vector<ImVec4> m_effect_clips;
        std::array<BorderEntry, 64> m_border_paths{};
        std::deque<RoundedClip> m_clip_regions;
        std::vector<const RoundedClip*> m_clip_stack;
        std::vector<std::pair<ImDrawList*, const RoundedClip*>> m_clipped_lists;
        const RoundedClip* m_clip = nullptr;
        std::size_t m_clip_cursor = 0;
        uint64_t m_clip_id = 0;
    };
} // namespace ui
