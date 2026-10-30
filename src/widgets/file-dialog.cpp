#include <imgui-ui/widgets/file-dialog.hpp>

#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/text.hpp>
#include <imgui-ui/widgets/button.hpp>

using namespace ui;

class FileDialogButton final : public ButtonWidget {
public:
    FileDialogButton() : ButtonWidget("select a file", {grow(), fit()}) {}

protected:
    void apply_theme_defaults(const Theme& theme) override {
        ButtonWidget::apply_theme_defaults(theme);
        configure_all_styles([&theme](Style& style) {
            style.background_color(theme.background_tertiary_color)
                .border(BorderEdge{theme.controls.border_thickness, BorderType::Dashed})
                .border_color(theme.controls.border_color);
        });
        style(StyleType::HOVER).background_color(theme.background_secondary_color);
    }
};

FileDialogWidget::FileDialogWidget(std::string label, LabelPlacement placement, std::string id)
    : Widget(std::move(id), "FileDialog", InputMode::None), m_label(add<TextWidget>(std::move(label))),
      m_button(add<FileDialogButton>()) {
    set_size({grow(), fit()});
    m_button.set_text_alignment({0.0F, 0.5F}).set_text_overflow(TextOverflow::Ellipsis);
    m_button.on_click([this] { show_dialog(m_operation, m_options); });
    m_label_placement = placement;
    update_label_layout();
}

void FileDialogWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    set_font(surface().get_primary_font(), theme.controls.font_size);
    configure_all_styles([](Style& style) { style.padding({}).border(BorderEdge{0.0F, BorderType::None}); });
    m_label_spacing = {theme.metrics.item_inner_spacing.x, theme.metrics.item_spacing.y};
    update_label_layout();
}

FileDialogResult FileDialogWidget::show_dialog(FileDialogOperation operation, const FileDialogOptions& options) {
    FileDialogResult result = surface().file_dialog().show(operation, options);
    if (result.accepted()) {
        if (!result.paths.empty()) set_value(result.paths.front().string());
        if (m_on_select) m_on_select(result);
    }
    return result;
}

FileDialogWidget& FileDialogWidget::set_label(std::string label, LabelPlacement placement) {
    m_label.set_text(std::move(label));
    m_label_placement = placement;
    update_label_layout();
    return *this;
}

void FileDialogWidget::update_label_layout() {
    const bool inline_label = m_label_placement == LabelPlacement::Inline;
    set_content_alignment({0.0F, inline_label ? 0.5F : 0.0F});
    m_label.set_visible(!m_label.empty());
    set_label_layout(m_label_placement, m_label_spacing);
}

FileDialogWidget& FileDialogWidget::set_operation(FileDialogOperation operation) {
    m_operation = operation;
    return *this;
}

FileDialogWidget& FileDialogWidget::set_options(FileDialogOptions options) {
    m_options = std::move(options);
    return *this;
}

FileDialogWidget& FileDialogWidget::on_select(std::function<void(const FileDialogResult&)> callback) {
    m_on_select = std::move(callback);
    return *this;
}

const std::string& FileDialogWidget::value() const {
    return m_value;
}

bool FileDialogWidget::set_value(std::string_view value) {
    if (m_value == value) return false;

    m_value = value;
    m_button.set_text(m_value.empty() ? "select a file" : m_value);
    notify_change();
    return true;
}
