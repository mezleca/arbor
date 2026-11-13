#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ui {
    struct FileDialogFilter {
        std::string name;
        std::string spec;
    };

    struct FileDialogOptions {
        std::vector<FileDialogFilter> filters;
        std::filesystem::path default_path;
        std::string default_name;
        std::string title;
        std::string accept_label;
        std::string cancel_label;
    };

    enum class FileDialogOperation {
        OpenFile,
        OpenFiles,
        SaveFile,
        SelectFolder,
        SelectFolders,
    };

    enum class FileDialogStatus {
        Accepted,
        Cancelled,
        Unavailable,
        Error,
    };

    struct FileDialogResult {
        FileDialogStatus status = FileDialogStatus::Unavailable;
        std::vector<std::filesystem::path> paths;
        std::string error;

        [[nodiscard]] bool accepted() const {
            return status == FileDialogStatus::Accepted;
        }
    };

    class FileDialogBackend {
    public:
        virtual ~FileDialogBackend() = default;

        virtual FileDialogResult show(FileDialogOperation operation, const FileDialogOptions& options = {}) = 0;
    };

} // namespace ui
