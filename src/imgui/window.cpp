#include <arbor/imgui/window.hpp>

#include <imgui_internal.h>

void ui::apply_window_insets(BoxInsets insets) {
    const ImVec2 shift = {(insets.left - insets.right) * 0.5F, (insets.top - insets.bottom) * 0.5F};
    if (shift.x == 0.0F && shift.y == 0.0F) return;

    // begin reserves the sum of both sides through symmetric padding. shift its content origin
    // to the leading sides without changing that sum or the content extent used by scrollbars.
    ImGuiWindow& window = *ImGui::GetCurrentWindow();
    const auto move = [shift](ImVec2& position) {
        position.x += shift.x;
        position.y += shift.y;
    };
    move(window.DC.CursorStartPos);
    move(window.DC.CursorPos);
    move(window.DC.CursorPosPrevLine);
    move(window.DC.CursorMaxPos);
    move(window.DC.IdealMaxPos);
    window.DC.Indent.x += shift.x;
    window.WorkRect.Translate(shift);
    window.ContentRegionRect.Translate(shift);
}
