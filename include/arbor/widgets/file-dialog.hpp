#pragma once

#include "../file-dialog/file-dialog.hpp"
#include "widget.hpp"

#include <string>
#include <string_view>
#include <functional>

namespace ui {
    class TextWidget;
    class ButtonWidget;

    class FileDialogWidget final : public Widget {
    public:
        explicit FileDialogWidget(
            std::string label = {}, LabelPlacement placement = LabelPlacement::Above, std::string id = "FileDialog"
        );

        FileDialogResult show_dialog(FileDialogOperation operation, const FileDialogOptions& options = {});
        FileDialogWidget& set_label(std::string label, LabelPlacement placement = LabelPlacement::Above);
        FileDialogWidget& set_operation(FileDialogOperation operation);
        FileDialogWidget& set_placeholder(std::string placeholder);
        TextWidget& label() {
            return m_label;
        }

        const TextWidget& label() const {
            return m_label;
        }

        ButtonWidget& button() {
            return m_button;
        }

        const ButtonWidget& button() const {
            return m_button;
        }

        /// owns options and filters. clicking opens OpenFile unless another operation is configured.
        FileDialogWidget& set_options(FileDialogOptions options);
        /// receives every accepted result, including all paths from a multiple selection.
        FileDialogWidget& on_select(std::function<void(const FileDialogResult&)> callback);

        /// stores the first accepted path. cancelling leaves the previous value unchanged.
        [[nodiscard]] const std::string& value() const;
        bool set_value(std::string_view value);

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        void update_label_layout();

        std::string m_value;
        std::string m_placeholder = "select a file";
        TextWidget& m_label;
        ButtonWidget& m_button;
        LabelPlacement m_label_placement = LabelPlacement::Above;
        ImVec2 m_label_spacing = {8.0F, 10.0F};
        FileDialogOperation m_operation = FileDialogOperation::OpenFile;
        FileDialogOptions m_options;
        std::function<void(const FileDialogResult&)> m_on_select;
    };
} // namespace ui
