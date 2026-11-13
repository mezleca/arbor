#pragma once

#include <imgui.h>

namespace ui {
    /// temporarily makes one ImGui context current and restores the caller's context on scope exit.
    class ImGuiContextScope {
    public:
        explicit ImGuiContextScope(ImGuiContext* context) : m_previous(ImGui::GetCurrentContext()) {
            ImGui::SetCurrentContext(context);
        }

        ImGuiContextScope(const ImGuiContextScope&) = delete;
        ImGuiContextScope& operator=(const ImGuiContextScope&) = delete;

        ~ImGuiContextScope() {
            ImGui::SetCurrentContext(m_previous);
        }

    private:
        ImGuiContext* m_previous;
    };

} // namespace ui
