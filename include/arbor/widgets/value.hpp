#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace ui {
    /// stores a widget value either by reference or in its own storage.
    /// binding an lvalue keeps a non-owning pointer, so the lvalue must outlive the widget.
    template <typename T>
    class WidgetValue {
    public:
        WidgetValue() : m_owned(std::in_place) {}
        explicit(false) WidgetValue(T& value) : m_ref(std::addressof(value)) {}
        explicit(false) WidgetValue(T&& value) : m_owned(std::move(value)) {}
        explicit(false) WidgetValue(const T& value) : m_owned(value) {}

        WidgetValue(const WidgetValue&) = default;
        WidgetValue(WidgetValue&&) = default;
        WidgetValue& operator=(const WidgetValue&) = delete;
        WidgetValue& operator=(WidgetValue&&) = delete;

        [[nodiscard]] const T& value() const {
            return m_owned ? *m_owned : *m_ref;
        }

        bool set(T value) {
            if (this->value() == value) {
                return false;
            }

            target() = std::move(value);
            return true;
        }

        template <std::predicate<T&> F>
        bool edit(F&& mutate) {
            return std::invoke(std::forward<F>(mutate), target());
        }

    private:
        T& target() {
            return m_owned ? *m_owned : *m_ref;
        }

        std::optional<T> m_owned;
        T* m_ref = nullptr;
    };
} // namespace ui
