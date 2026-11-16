#pragma once

#include "animation.hpp"
#include "style.hpp"

#include <array>
#include <memory>
#include <optional>

namespace ui {
    static constexpr float OPACITY_TRANSITION_DURATION = 0.15F;
    static constexpr float VISIBILITY_OPACITY_THRESHOLD = 0.002f;
    inline constexpr TransitionSpec OPACITY_TRANSITION{OPACITY_TRANSITION_DURATION, easing::linear};

    enum class StyleAnimationProperty : uint8_t {
        PaddingX,
        PaddingY,
        MarginX,
        MarginY,
        Rotation,
        Scale,
        Color,
        BorderColor,
        BackgroundColor,
        Count,
    };

    struct StyleAnimationSlot {
        std::optional<AnimationValue> override;
        AnimationValue base = 0.0F;

        /// points to the owner's invalidation flag only for properties that change padding or margin.
        bool* layout_dirty = nullptr;
    };

    struct StyleAnimationState {
        Style presentation;
        std::array<StyleAnimationSlot, static_cast<size_t>(StyleAnimationProperty::Count)> slots;
        bool layout_dirty = false;
    };

    class VisualState {
    public:
        VisualState();
        void set_change_callback(void* owner, Style::ChangeCallback callback);

        Style& style(StyleType type = StyleType::DEFAULT) {
            return styles[static_cast<size_t>(type)];
        }

        const Style& style(StyleType type = StyleType::DEFAULT) const {
            return styles[static_cast<size_t>(type)];
        }

        const ComputedStyle& computed_style() const {
            return has_animation_overrides() ? m_animation_state->presentation : active_style();
        }

        template <typename Func>
        VisualState& configure_all_styles(Func&& func) {
            for (auto& style : styles) {
                func(style);
            }

            return *this;
        }

        void set_style(StyleType type);
        void snap_to_style(StyleType type);
        void set_item_state(bool hovered, bool active, bool focused = false);

        /// applies a size request to every configured style and the active transition snapshot.
        void set_size(LayoutSize size);
        StyleType style_type() const {
            return m_target_style;
        }

        bool is_visible() const {
            return visible &&
                   (m_opacity >= VISIBILITY_OPACITY_THRESHOLD || current_opacity.value >= VISIBILITY_OPACITY_THRESHOLD);
        }

        bool accepts_input() const {
            return visible && m_opacity >= VISIBILITY_OPACITY_THRESHOLD;
        }

        void set_opacity(float value, TransitionSpec transition = OPACITY_TRANSITION);
        void fade_in(TransitionSpec transition = OPACITY_TRANSITION);
        void fade_out(TransitionSpec transition = OPACITY_TRANSITION) {
            set_opacity(0.0F, transition);
        }

        float opacity() const {
            return first_frame ? 0.0F : current_opacity.value;
        }

        StyleAnimationSequence animate();
        void cancel_animations();
        Animator& animator() {
            return m_animator;
        }

        const Animator& animator() const {
            return m_animator;
        }

        void update(float dt);
        bool transitioning() const;

    private:
        friend class StyleAnimationSequence;

        const Style& active_style() const {
            return m_style_transitioning ? *m_transition_style : style(m_target_style);
        }

        void update_animations(float dt);
        StyleAnimationSlot& slot(StyleAnimationProperty property) {
            return m_animation_state->slots[static_cast<size_t>(property)];
        }

        static AnimationTarget target(StyleAnimationSlot& slot);
        bool has_animation_overrides() const;

        FloatValue current_opacity;
        Style styles[static_cast<size_t>(StyleType::COUNT)];
        std::unique_ptr<Style> m_transition_style;
        std::unique_ptr<StyleAnimationState> m_animation_state;
        Animator m_style_animator;
        Animator m_animator;
        float m_opacity = 1.0f;
        TransitionSpec m_opacity_transition = OPACITY_TRANSITION;
        bool visible = true;
        bool first_frame = true;
        bool m_style_transitioning = false;
        StyleType m_target_style = StyleType::DEFAULT;
        void* m_change_owner = nullptr;
        Style::ChangeCallback m_change_callback = nullptr;
    };

} // namespace ui
