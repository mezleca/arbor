#include <arbor/backends/opengl/gpu-timer.hpp>

#include <glad/gl.h>

using namespace ui;

std::optional<double> OpenGLGpuTimer::begin(bool enabled) {
    std::optional<double> elapsed;
    if (m_pending) {
        GLint available = GL_FALSE;
        glGetQueryObjectiv(m_query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(m_query, GL_QUERY_RESULT, &nanoseconds);
            elapsed = static_cast<double>(nanoseconds) / 1'000'000.0;
            m_pending = false;
        }
    }

    // keep an unfinished query pending until the gpu finishes.
    // rendering continues without waiting for its result.
    if (enabled && !m_pending) {
        if (m_query == 0) {
            glGenQueries(1, &m_query);
        }
        if (m_query != 0) {
            glBeginQuery(GL_TIME_ELAPSED, m_query);
            m_active = true;
        }
    }
    return elapsed;
}

void OpenGLGpuTimer::end() {
    if (m_active) {
        glEndQuery(GL_TIME_ELAPSED);
        m_active = false;
        m_pending = true;
    }
}

void OpenGLGpuTimer::shutdown() {
    if (m_query != 0) {
        glDeleteQueries(1, &m_query);
        m_query = 0;
    }
    m_pending = false;
    m_active = false;
}
