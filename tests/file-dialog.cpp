#include <catch2/catch_test_macros.hpp>

#include <arbor/surface.hpp>
#include <arbor/input/router.hpp>
#include <arbor/widgets/file-dialog.hpp>

#include "imgui-context.hpp"

#include <memory>
#include <vector>

using namespace ui;

class TestFileDialogBackend final : public FileDialogBackend {
public:
    FileDialogResult show(FileDialogOperation operation, const FileDialogOptions& options) override {
        received_operation = operation;
        received_options = options;
        ++calls;
        return {
            .status = status,
            .paths = status == FileDialogStatus::Accepted ? std::vector<std::filesystem::path>{"selected.osu"}
                                                          : std::vector<std::filesystem::path>{},
        };
    }

    FileDialogOperation received_operation = FileDialogOperation::OpenFile;
    FileDialogOptions received_options;
    FileDialogStatus status = FileDialogStatus::Accepted;
    int calls = 0;
};

TEST_CASE("Surface uses the configured file dialog backend") {
    Runtime runtime;
    auto backend = std::make_unique<TestFileDialogBackend>();
    TestFileDialogBackend* backend_ptr = backend.get();
    Surface surface(runtime, {.backend = ui_test::make_backend(), .file_dialog_backend = std::move(backend)});

    const std::vector<FileDialogFilter> filters{{"beatmaps", "osu"}};
    const FileDialogResult result = surface.file_dialog().show(
        FileDialogOperation::SaveFile,
        {.filters = filters, .default_path = "songs", .default_name = "collection.osu", .title = "Save collection"}
    );

    REQUIRE(result.accepted());
    REQUIRE(result.paths == std::vector<std::filesystem::path>{"selected.osu"});
    REQUIRE(backend_ptr->received_operation == FileDialogOperation::SaveFile);
    REQUIRE(backend_ptr->received_options.filters.size() == 1);
    REQUIRE(backend_ptr->received_options.filters.front().name == "beatmaps");
    REQUIRE(backend_ptr->received_options.default_path == "songs");
    REQUIRE(backend_ptr->received_options.default_name == "collection.osu");
    REQUIRE(backend_ptr->received_options.title == "Save collection");

    const auto context = ui_test::prepare_surface(surface, {400.0F, 200.0F});
    auto& widget = surface.root().add<FileDialogWidget>("file", LabelPlacement::Inline);
    int selections = 0;
    int changes = 0;
    widget.on_select([&](const FileDialogResult& selection) {
        REQUIRE(selection.paths == std::vector<std::filesystem::path>{"selected.osu"});
        REQUIRE(widget.value() == "selected.osu");
        ++selections;
    });
    widget.on_change([&] { ++changes; });
    {
        const std::vector<FileDialogFilter> temporary_filters{{"audio", "wav,ogg"}};
        widget.set_options({.filters = temporary_filters});
    }
    ui_test::draw_surface(surface);

    UiEvent click{.type = EventType::Click, .button = PointerButton::Left};
    InputRouter::dispatch_to(*widget.children().back(), click);
    CHECK(backend_ptr->received_operation == FileDialogOperation::OpenFile);
    REQUIRE(backend_ptr->received_options.filters.size() == 1);
    CHECK(backend_ptr->received_options.filters.front().name == "audio");
    CHECK(selections == 1);
    CHECK(changes == 1);

    widget.set_options({.default_path = "other", .title = "Choose folder"});
    widget.set_operation(FileDialogOperation::SelectFolder);
    backend_ptr->status = FileDialogStatus::Cancelled;
    UiEvent cancelled_click{.type = EventType::Click, .button = PointerButton::Left};
    InputRouter::dispatch_to(*widget.children().back(), cancelled_click);
    CHECK(backend_ptr->received_operation == FileDialogOperation::SelectFolder);
    CHECK(backend_ptr->received_options.filters.empty());
    CHECK(backend_ptr->received_options.default_path == "other");
    CHECK(backend_ptr->received_options.title == "Choose folder");
    CHECK(widget.value() == "selected.osu");
    CHECK(selections == 1);
    CHECK(changes == 1);
    CHECK(backend_ptr->calls == 3);
}
