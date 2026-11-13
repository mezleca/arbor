#include <arbor/style/animation.hpp>

#include <arbor/style/state.hpp>

#include <utility>

using namespace ui;

StyleAnimationSequence&
StyleAnimationSequence::to(StyleAnimationProperty property, AnimationValue value, TransitionSpec transition) {
    m_sequence.to(VisualState::target(m_state.slot(property)), std::move(value), transition);
    return *this;
}

StyleAnimationSequence&
StyleAnimationSequence::by(StyleAnimationProperty property, AnimationValue value, TransitionSpec transition) {
    m_sequence.by(VisualState::target(m_state.slot(property)), std::move(value), transition);
    return *this;
}

StyleAnimationSequence& StyleAnimationSequence::release(StyleAnimationProperty property, TransitionSpec transition) {
    m_sequence.release(VisualState::target(m_state.slot(property)), transition);
    return *this;
}

StyleAnimationSequence& StyleAnimationSequence::release_all(TransitionSpec transition) {
    for (StyleAnimationSlot& slot : m_state.m_animation_slots) {
        m_sequence.release(VisualState::target(slot), transition);
    }
    return *this;
}

StyleAnimationSequence& StyleAnimationSequence::then(float delay) {
    m_sequence.then(delay);
    return *this;
}

StyleAnimationSequence& StyleAnimationSequence::delay(float duration) {
    m_sequence.delay(duration);
    return *this;
}

StyleAnimationSequence& StyleAnimationSequence::end(std::function<void()> callback) {
    m_sequence.end(std::move(callback));
    return *this;
}
