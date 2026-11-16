#include <arbor/style/state.hpp>

#include <algorithm>

using namespace ui;

static AnimationValue read_style_property(const ComputedStyle& style, StyleAnimationProperty property) {
    switch (property) {
        case StyleAnimationProperty::PaddingX:
            return style.padding().left;
        case StyleAnimationProperty::PaddingY:
            return style.padding().top;
        case StyleAnimationProperty::MarginX:
            return style.margin().left;
        case StyleAnimationProperty::MarginY:
            return style.margin().top;
        case StyleAnimationProperty::Rotation:
            return style.rotation();
        case StyleAnimationProperty::Scale:
            return style.scale();
        case StyleAnimationProperty::Color:
            return style.color().value;
        case StyleAnimationProperty::BorderColor:
            return style.border_color().value;
        case StyleAnimationProperty::BackgroundColor:
            return style.background_color().value;
        case StyleAnimationProperty::Count:
            return 0.0F;
    }

    return 0.0F;
}

static void apply_style_property(Style& style, StyleAnimationProperty property, const AnimationValue& value) {
    switch (property) {
        case StyleAnimationProperty::PaddingX:
            style.padding_left(std::get<float>(value)).padding_right(std::get<float>(value));
            return;
        case StyleAnimationProperty::PaddingY:
            style.padding_top(std::get<float>(value)).padding_bottom(std::get<float>(value));
            return;
        case StyleAnimationProperty::MarginX:
            style.margin_left(std::get<float>(value)).margin_right(std::get<float>(value));
            return;
        case StyleAnimationProperty::MarginY:
            style.margin_top(std::get<float>(value)).margin_bottom(std::get<float>(value));
            return;
        case StyleAnimationProperty::Rotation:
            style.rotation(std::get<float>(value));
            return;
        case StyleAnimationProperty::Scale:
            style.scale(std::get<ImVec2>(value));
            return;
        case StyleAnimationProperty::Color:
            style.color(std::get<Color>(value));
            return;
        case StyleAnimationProperty::BorderColor:
            style.border_color(std::get<Color>(value));
            return;
        case StyleAnimationProperty::BackgroundColor:
            style.background_color(std::get<Color>(value));
            return;
        case StyleAnimationProperty::Count:
            return;
    }
}

static AnimationValue read_style_animation_slot(void* context) {
    const auto& slot = *static_cast<StyleAnimationSlot*>(context);
    return slot.override.value_or(slot.base);
}

static AnimationValue read_style_animation_slot_base(void* context) {
    return static_cast<StyleAnimationSlot*>(context)->base;
}

static void write_style_animation_slot(void* context, const AnimationValue& value) {
    auto& slot = *static_cast<StyleAnimationSlot*>(context);
    slot.override = value;
    if (slot.layout_dirty != nullptr) *slot.layout_dirty = true;
}

static void release_style_animation_slot(void* context) {
    auto& slot = *static_cast<StyleAnimationSlot*>(context);
    slot.override.reset();
    if (slot.layout_dirty != nullptr) *slot.layout_dirty = true;
}

VisualState::VisualState() {
    current_opacity.value = m_opacity;
    snap_to_style(StyleType::DEFAULT);
}

void VisualState::set_change_callback(void* owner, Style::ChangeCallback callback) {
    m_change_owner = owner;
    m_change_callback = callback;

    // both configured styles and the active transition must invalidate the same owner after a change.
    for (Style& style : styles) {
        style.set_change_callback(owner, callback);
    }
    if (m_transition_style) m_transition_style->set_change_callback(owner, callback);
}

void VisualState::set_opacity(float value, TransitionSpec transition) {
    m_opacity_transition = transition;
    m_opacity = std::clamp(value, 0.0f, 1.0f);
}

void VisualState::fade_in(TransitionSpec transition) {
    visible = true;
    if (first_frame) current_opacity.value = 0.0F;
    set_opacity(1.0f, transition);
}

bool VisualState::transitioning() const {
    return current_opacity.value != m_opacity || m_style_transitioning || m_style_animator.transitioning() ||
           m_animator.transitioning();
}

void VisualState::update(float dt) {
    if (!first_frame && !transitioning()) return;

    const FloatValue target_opacity{m_opacity, m_opacity_transition};
    current_opacity.tick(target_opacity, dt);

    // animation overrides take precedence over the style transition
    if (m_style_transitioning) {
        const Style& target_style = styles[static_cast<size_t>(m_target_style)];
        m_style_transitioning = Style::lerp(*m_transition_style, target_style, dt);
    }

    first_frame = false;
    update_animations(dt);
}

void VisualState::set_style(StyleType type) {
    if (m_target_style == type) return;
    if (!m_style_transitioning) {
        if (!m_transition_style)
            m_transition_style = std::make_unique<Style>(style(m_target_style));
        else
            *m_transition_style = style(m_target_style);
    }
    m_style_transitioning = true;
    m_target_style = type;
}

void VisualState::snap_to_style(StyleType type) {
    m_target_style = type;
    m_style_transitioning = false;
}

void VisualState::set_item_state(bool hovered, bool active, bool focused) {
    if (active) {
        set_style(StyleType::ACTIVE);
        return;
    }
    if (focused) {
        set_style(StyleType::FOCUS);
        return;
    }
    set_style(hovered ? StyleType::HOVER : StyleType::DEFAULT);
}

void VisualState::set_size(LayoutSize size) {
    configure_all_styles([size](Style& style) { style.size(size); });
    if (m_style_transitioning) m_transition_style->size(size);

    if (!has_animation_overrides()) return;

    // presentation changes need an explicit layout invalidation
    m_animation_state->presentation.size(size);
    if (m_change_callback != nullptr) m_change_callback(m_change_owner, false);
}

StyleAnimationSequence VisualState::animate() {
    if (!m_animation_state) {
        m_animation_state = std::make_unique<StyleAnimationState>();
        for (std::size_t index = 0; index < static_cast<std::size_t>(StyleAnimationProperty::Rotation); ++index) {
            m_animation_state->slots[index].layout_dirty = &m_animation_state->layout_dirty;
        }
    }
    m_style_animator.cancel();
    return StyleAnimationSequence{*this, m_style_animator.animate()};
}

void VisualState::cancel_animations() {
    m_style_animator.cancel();
    m_animator.cancel();

    bool layout_changed = false;
    if (m_animation_state) {
        for (StyleAnimationSlot& slot : m_animation_state->slots) {
            layout_changed |= slot.layout_dirty != nullptr && slot.override.has_value();
            slot.override.reset();
        }
    }

    // removing inset overrides changes measured bounds
    if (layout_changed && m_change_callback != nullptr) m_change_callback(m_change_owner, false);
}

void VisualState::update_animations(float dt) {
    m_animator.update(dt);

    const bool style_animating = m_style_animator.transitioning();
    if (!style_animating && !has_animation_overrides()) return;

    // release tracks interpolate back to the current base style
    auto& animation = *m_animation_state;
    for (std::size_t index = 0; index < animation.slots.size(); ++index) {
        StyleAnimationSlot& slot = animation.slots[index];
        slot.base = read_style_property(active_style(), static_cast<StyleAnimationProperty>(index));
    }

    animation.layout_dirty = false;
    m_style_animator.update(dt);

    // inset overrides share one layout invalidation
    animation.presentation = active_style();

    animation.presentation.set_change_callback(nullptr, nullptr);
    for (std::size_t index = 0; index < animation.slots.size(); ++index) {
        StyleAnimationSlot& slot = animation.slots[index];
        if (slot.override.has_value()) {
            apply_style_property(animation.presentation, static_cast<StyleAnimationProperty>(index), *slot.override);
        }
    }

    if (animation.layout_dirty && m_change_callback != nullptr) m_change_callback(m_change_owner, false);
}

AnimationTarget VisualState::target(StyleAnimationSlot& slot) {
    return {
        .read = &read_style_animation_slot,
        .base = &read_style_animation_slot_base,
        .write = &write_style_animation_slot,
        .release = &release_style_animation_slot,
        .context = &slot,
        .identity = &slot,
    };
}

bool VisualState::has_animation_overrides() const {
    return m_animation_state &&
           std::ranges::any_of(
               m_animation_state->slots, &std::optional<AnimationValue>::has_value, &StyleAnimationSlot::override
           );
}
