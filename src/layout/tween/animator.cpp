#include <imgui-ui/style/tween/animator.hpp>
#include <imgui-ui/style/values.hpp>

#include <algorithm>
#include <vector>

using namespace ui;

struct AnimationStep {
    AnimationTarget target;
    AnimationValue value;
    TransitionSpec transition;
    float start = 0.0F;
    bool relative = false;
    bool release = false;
};

struct AnimationTrack {
    AnimationTarget target;
    AnimationValue start;
    AnimationValue target_value;
    TransitionSpec transition;
    float started_at = 0.0F;
    bool release = false;
};

struct AnimationCallback {
    float at = 0.0F;
    std::function<void()> callback;
};

struct ui::AnimatorState {
    std::vector<AnimationStep> steps;
    std::vector<AnimationTrack> tracks;
    std::vector<AnimationCallback> callbacks;
    float time = 0.0F;
};

static AnimationValue interpolate_animation_value(const AnimationValue& start, const AnimationValue& target, float progress) {
    return std::visit(
        [&](const auto& value) -> AnimationValue {
            using T = std::decay_t<decltype(value)>;
            return interpolate_value(value, std::get<T>(target), progress);
        },
        start
    );
}

static AnimationValue add_animation_value(const AnimationValue& value, const AnimationValue& amount) {
    const auto* float_value = std::get_if<float>(&value);
    if (float_value != nullptr) {
        return *float_value + std::get<float>(amount);
    }

    const auto* vec2_value = std::get_if<ImVec2>(&value);
    if (vec2_value != nullptr) {
        const ImVec2& amount_vec2 = std::get<ImVec2>(amount);
        return ImVec2{vec2_value->x + amount_vec2.x, vec2_value->y + amount_vec2.y};
    }

    const ImVec4 color = std::get<Color>(value).rgba();
    const ImVec4 color_amount = std::get<Color>(amount).rgba();
    return rgba(
        std::clamp(color.x + color_amount.x, 0.0F, 1.0F), std::clamp(color.y + color_amount.y, 0.0F, 1.0F),
        std::clamp(color.z + color_amount.z, 0.0F, 1.0F), std::clamp(color.w + color_amount.w, 0.0F, 1.0F)
    );
}

static AnimationValue animation_track_value(const AnimationTrack& track, float time) {
    const float duration = std::max(0.0F, track.transition.duration);
    const float elapsed = std::clamp(time - track.started_at, 0.0F, duration);
    const float progress =
        duration == 0.0F ? 1.0F
                         : (track.transition.easing != nullptr ? track.transition.easing : easing::linear)(elapsed / duration);
    return interpolate_animation_value(track.start, track.target_value, progress);
}

static bool animation_track_complete(const AnimationTrack& track, float time) {
    return time + 0.00001F >= track.started_at + std::max(0.0F, track.transition.duration);
}

Animator::Animator() = default;
Animator::~Animator() = default;
Animator::Animator(Animator&&) noexcept = default;

Animator& Animator::operator=(Animator&&) noexcept = default;

AnimationSequence Animator::animate() {
    return AnimationSequence{*this, time()};
}

void Animator::update(float dt) {
    if (m_state == nullptr) return;

    AnimatorState& state = *m_state;
    state.time += std::max(0.0F, dt);

    // start due steps before writing tracks. replacing a target reads the displaced track at the current timeline
    // time before erasing it.
    std::erase_if(state.steps, [&](const AnimationStep& step) {
        if (step.start > state.time) return false;

        const auto track_it = std::ranges::find_if(state.tracks, [&step](const AnimationTrack& track) {
            return track.target.identity == step.target.identity;
        });
        AnimationValue start =
            track_it == state.tracks.end() ? step.target.read(step.target.context) : animation_track_value(*track_it, state.time);
        AnimationValue target;

        if (step.release) {
            target = step.target.base(step.target.context);
        } else if (step.relative) {
            target = add_animation_value(start, step.value);
        } else {
            target = step.value;
        }

        if (track_it != state.tracks.end()) {
            state.tracks.erase(track_it);
        }

        state.tracks.push_back({step.target, std::move(start), std::move(target), step.transition, step.start, step.release});
        return true;
    });

    std::erase_if(state.tracks, [&](const AnimationTrack& track) {
        const AnimationValue value = animation_track_value(track, state.time);
        track.target.write(track.target.context, value);

        if (!animation_track_complete(track, state.time)) return false;

        if (track.release) {
            track.target.release(track.target.context);
        }
        return true;
    });

    // remove due callbacks before invoking user code because a callback may schedule more work on this animator.
    std::vector<std::function<void()>> callbacks;
    std::erase_if(state.callbacks, [&](AnimationCallback& scheduled) {
        if (scheduled.at > state.time) return false;

        callbacks.push_back(std::move(scheduled.callback));
        return true;
    });

    for (const auto& callback : callbacks) {
        callback();
    }
}

void Animator::cancel() {
    if (m_state == nullptr) return;

    AnimatorState& state = *m_state;
    state.steps.clear();
    state.tracks.clear();
    state.callbacks.clear();
}

bool Animator::transitioning() const {
    return m_state != nullptr && (!m_state->steps.empty() || !m_state->tracks.empty() || !m_state->callbacks.empty());
}

void Animator::schedule(AnimationTarget target, AnimationValue value, bool relative, float start, TransitionSpec transition) {
    if (target.read == nullptr || target.write == nullptr || target.identity == nullptr) {
        return;
    }

    ensure_state().steps.push_back({target, std::move(value), transition, std::max(start, time()), relative, false});
}

void Animator::schedule_release(AnimationTarget target, float start, TransitionSpec transition) {
    if (target.read == nullptr || target.base == nullptr || target.write == nullptr || target.release == nullptr ||
        target.identity == nullptr) {
        return;
    }

    ensure_state().steps.push_back({target, 0.0F, transition, std::max(start, time()), false, true});
}

void Animator::schedule_callback(float at, std::function<void()> callback) {
    if (callback) {
        ensure_state().callbacks.push_back({std::max(at, time()), std::move(callback)});
    }
}

float Animator::time() const {
    return m_state != nullptr ? m_state->time : 0.0F;
}

AnimatorState& Animator::ensure_state() {
    if (m_state == nullptr) m_state = std::make_unique<AnimatorState>();
    return *m_state;
}

AnimationSequence& AnimationSequence::to(AnimationTarget target, AnimationValue value, TransitionSpec transition) {
    transition.duration = std::max(0.0F, transition.duration);
    m_animator.schedule(target, std::move(value), false, m_cursor, transition);
    m_end = std::max(m_end, m_cursor + transition.duration);
    return *this;
}

AnimationSequence& AnimationSequence::by(AnimationTarget target, AnimationValue value, TransitionSpec transition) {
    transition.duration = std::max(0.0F, transition.duration);
    m_animator.schedule(target, std::move(value), true, m_cursor, transition);
    m_end = std::max(m_end, m_cursor + transition.duration);
    return *this;
}

AnimationSequence& AnimationSequence::release(AnimationTarget target, TransitionSpec transition) {
    transition.duration = std::max(0.0F, transition.duration);
    m_animator.schedule_release(target, m_cursor, transition);
    m_end = std::max(m_end, m_cursor + transition.duration);
    return *this;
}

AnimationSequence& AnimationSequence::then(float delay) {
    m_cursor = std::max(m_cursor, m_end) + std::max(0.0F, delay);
    return *this;
}

AnimationSequence& AnimationSequence::delay(float duration) {
    m_cursor += std::max(0.0F, duration);
    return *this;
}

AnimationSequence& AnimationSequence::end(std::function<void()> callback) {
    m_animator.schedule_callback(std::max(m_cursor, m_end), std::move(callback));
    return *this;
}
