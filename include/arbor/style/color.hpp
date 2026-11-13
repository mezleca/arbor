#pragma once

#include <cstdint>
#include <initializer_list>
#include <imgui.h>
#include <memory>

namespace ui {
    enum class GradientType : uint8_t {
        Linear,
        Radial,
    };

    struct GradientData;
    struct GradientStop;

    class Color {
    public:
        Color() = default;
        Color(float red, float green, float blue, float alpha = 1.0F) : m_rgba(red, green, blue, alpha) {}

        // native imgui colors convert directly to style colors and back for imgui drawing calls.
        Color(ImVec4 color) : m_rgba(color) {} // NOLINT(misc-explicit-constructor)

        Color(ImColor color) : m_rgba(color.Value) {} // NOLINT(misc-explicit-constructor)

        operator ImVec4() const { // NOLINT(misc-explicit-constructor)
            return m_rgba;
        }

        /// rgba value, or midpoint fallback for native imgui drawing and editors.
        ImVec4 rgba() const {
            return m_rgba;
        }

        /// immutable gradient data, null for a plain rgba color.
        const GradientData* gradient() const {
            return m_gradient.get();
        }

        float max_alpha() const;
        bool operator==(const Color& other) const;

    private:
        friend Color gradient(GradientType, std::initializer_list<GradientStop>, ImVec2, ImVec2);

        ImVec4 m_rgba;
        std::shared_ptr<const GradientData> m_gradient;
    };

    struct GradientStop {
        float position = 0.0F;
        ImVec4 color;

        GradientStop() = default;
        GradientStop(float position, const Color& color);
    };

    /// accepts normalized channels in [0, 1], clamping values outside that range.
    Color rgb(float red, float green, float blue);

    /// accepts normalized channels and alpha in [0, 1], clamping values outside that range.
    Color rgba(float red, float green, float blue, float alpha);

    /// accepts integer channels in [0, 255] and clamps values outside that range.
    Color rgb(int red, int green, int blue);

    /// accepts integer channels and alpha in [0, 255] and clamps values outside that range.
    Color rgba(int red, int green, int blue, int alpha);
    Color gradient(GradientType type, std::initializer_list<GradientStop> stops);
    Color gradient(GradientType type, std::initializer_list<GradientStop> stops, ImVec2 start, ImVec2 end);
    Color interpolate_color(const Color& start, const Color& end, float progress);
} // namespace ui
