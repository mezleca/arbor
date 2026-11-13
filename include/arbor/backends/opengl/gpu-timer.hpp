#pragma once

#include <cstdint>
#include <optional>

namespace ui {
    class OpenGLGpuTimer {
    public:
        OpenGLGpuTimer() = default;
        OpenGLGpuTimer(const OpenGLGpuTimer&) = delete;
        OpenGLGpuTimer& operator=(const OpenGLGpuTimer&) = delete;

        std::optional<double> begin(bool enabled);
        void end();
        void shutdown();

    private:
        uint32_t m_query = 0;
        bool m_pending = false;
        bool m_active = false;
    };
} // namespace ui
