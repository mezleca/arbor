#pragma once

#include "../transition.hpp"
#include "color.hpp"
#include "../layout/geometry.hpp"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace ui {
    struct BoxShadow {
        ImVec2 offset;
        float blur = 0.0F;
        float spread = 0.0F;
        Color color = rgba(0.0F, 0.0F, 0.0F, 0.0F);
    };

    float interpolate_value(float start, float target, float progress);
    int interpolate_value(int start, int target, float progress);
    ImVec2 interpolate_value(ImVec2 start, ImVec2 target, float progress);
    BoxInsets interpolate_value(BoxInsets start, BoxInsets target, float progress);
    Color interpolate_value(const Color& start, const Color& target, float progress);
    BoxShadow interpolate_value(const BoxShadow& start, const BoxShadow& target, float progress);

    template <typename T>
    bool transition_values_equal(const T& left, const T& right) {
        return left == right;
    }

    inline bool transition_values_equal(const ImVec2& left, const ImVec2& right) {
        return left.x == right.x && left.y == right.y;
    }

    inline bool transition_values_equal(const BoxShadow& left, const BoxShadow& right) {
        return transition_values_equal(left.offset, right.offset) && left.blur == right.blur && left.spread == right.spread &&
               transition_values_equal(left.color, right.color);
    }

    struct TransitionStep {
        float progress = 1.0F;
        bool changed = false;
    };

    template <typename T>
    struct Value {
        Value() = default;

        // a plain style value converts to an immediate value with zero transition duration.
        Value(T initial_value, float transition_duration = 0.0F) // NOLINT(misc-explicit-constructor)
            : value(std::move(initial_value)), duration(std::max(0.0F, transition_duration)) {}

        Value(T initial_value, TransitionSpec transition)
            : value(std::move(initial_value)), duration(std::max(0.0F, transition.duration)),
              easing(transition.easing != nullptr ? transition.easing : easing::linear) {}

        T value{};
        float duration = 0.0F;
        EasingFunction easing = easing::linear;

        void set(T new_value) {
            value = std::move(new_value);
            m_has_target = false;
        }

        void set_transition(TransitionSpec transition) {
            duration = std::max(0.0F, transition.duration);
            easing = transition.easing != nullptr ? transition.easing : easing::linear;
        }

        bool is_transitioning() const {
            return m_has_target && m_elapsed < m_duration;
        }

        bool tick(const Value& target, float dt) {
            if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>) {
                const bool changed = value != target.value;
                value = target.value;
                return changed;
            } else {
                const TransitionStep step = transition_progress(target, dt);
                value = is_transitioning() ? interpolate_value(m_start, target.value, step.progress) : target.value;
                return step.changed;
            }
        }

    protected:
        TransitionStep transition_progress(const Value& target, float dt) {
            // capture the displayed value when the target or transition changes. advance elapsed time before
            // interpolating so retargeting continues from that captured value.
            const float target_duration = std::max(0.0F, target.duration);
            const EasingFunction target_easing = target.easing != nullptr ? target.easing : easing::linear;
            const bool target_changed = !m_has_target || !transition_values_equal(m_target, target.value) ||
                                        m_duration != target_duration || m_easing != target_easing;
            const float previous_elapsed = m_elapsed;
            if (target_changed) {
                m_start = value;
                m_target = target.value;
                m_duration = target_duration;
                m_easing = target_easing;
                m_elapsed = 0.0F;
                m_has_target = true;
            }

            if (m_duration == 0.0F) {
                return {1.0F, target_changed};
            }

            m_elapsed = std::min(m_duration, m_elapsed + std::max(0.0F, dt));
            if (m_elapsed >= m_duration) {
                return {1.0F, target_changed || m_elapsed != previous_elapsed};
            }

            return {m_easing(m_elapsed / m_duration), target_changed || m_elapsed != previous_elapsed};
        }

    private:
        T m_start{};
        T m_target{};
        float m_duration = 0.0F;
        EasingFunction m_easing = easing::linear;
        float m_elapsed = 0.0F;
        bool m_has_target = false;
    };

    using FloatValue = Value<float>;

    struct ColorValue : Value<Color> {
        using Value::Value;

        ImVec4 get() const {
            return value.rgba();
        }

        ImU32 get_col() const {
            return ImGui::GetColorU32(value.rgba());
        }
    };

    using BoxShadowValue = Value<BoxShadow>;
    using Vec2Value = Value<ImVec2>;
    using IntValue = Value<int>;
    using BoolValue = Value<bool>;
    using StringValue = Value<std::string>;
    using StyleValue = std::variant<IntValue, FloatValue, StringValue, BoolValue, ColorValue, Vec2Value>;
} // namespace ui
