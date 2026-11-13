#pragma once

#include "tween/animator.hpp"

#include <cstdint>
#include <functional>

namespace ui {
    class VisualState;
    struct StyleAnimationSlot;
    enum class StyleAnimationProperty : uint8_t;

    class StyleAnimationSequence final {
    public:
        explicit StyleAnimationSequence(VisualState& state, AnimationSequence sequence) : m_state(state), m_sequence(sequence) {}

        /// schedules an absolute target. calls before then() start together, with durations measured in seconds.
        StyleAnimationSequence& to(StyleAnimationProperty property, AnimationValue value, TransitionSpec transition = {});

        /// schedules a delta from the value read when the track starts, rather than an absolute target.
        StyleAnimationSequence& by(StyleAnimationProperty property, AnimationValue value, TransitionSpec transition = {});

        /// animates a property back to its configured style value, then removes the override.
        StyleAnimationSequence& release(StyleAnimationProperty property, TransitionSpec transition = {});
        StyleAnimationSequence& release_all(TransitionSpec transition = {});

        /// starts the next phase after the longest scheduled track finishes, plus a nonnegative delay in seconds.
        StyleAnimationSequence& then(float delay = 0.0F);

        /// advances the scheduling cursor in seconds without waiting for parallel tracks to finish.
        StyleAnimationSequence& delay(float duration);

        /// runs after the latest track or cursor delay. captured objects must remain alive until the callback runs.
        StyleAnimationSequence& end(std::function<void()> callback);

    private:
        VisualState& m_state;
        AnimationSequence m_sequence;
    };
} // namespace ui
