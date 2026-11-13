#include <arbor/file-dialog/file-dialog.hpp>
#include <arbor/surface.hpp>

#include <nfd.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace ui;

static const char* optional(const std::string& value) {
    return value.empty() ? nullptr : value.c_str();
}

static std::string nfd_error() {
    const char* error = NFD::GetError();
    return error != nullptr ? error : "NFD failed";
}

class NfdSession final {
public:
    NfdSession() {
        m_initialized = NFD::Init() == NFD_OKAY;
        if (!m_initialized) m_error = nfd_error();
    }

    ~NfdSession() {
        if (m_initialized) NFD::Quit();
    }

    NfdSession(const NfdSession&) = delete;
    NfdSession& operator=(const NfdSession&) = delete;

    [[nodiscard]] bool initialized() const {
        return m_initialized;
    }

    [[nodiscard]] const std::string& error() const {
        return m_error;
    }

private:
    std::string m_error;
    bool m_initialized = false;
};

static NfdSession& nfd_session() {
    static NfdSession session;
    return session;
}

static std::vector<nfdu8filteritem_t> make_filters(const std::vector<FileDialogFilter>& filters) {
    std::vector<nfdu8filteritem_t> result;
    result.reserve(filters.size());
    for (const FileDialogFilter& filter : filters) {
        result.push_back({filter.name.c_str(), filter.spec.c_str()});
    }

    return result;
}

static FileDialogResult dialog_result(nfdresult_t result) {
    if (result == NFD_CANCEL) return {.status = FileDialogStatus::Cancelled};
    return {.status = FileDialogStatus::Error, .error = nfd_error()};
}

static FileDialogResult single_path_result(nfdresult_t result, nfdu8char_t* selected_path) {
    if (result != NFD_OKAY) return dialog_result(result);
    if (selected_path == nullptr) return {.status = FileDialogStatus::Error, .error = "NFD returned no path"};

    // take ownership of the native path before copying it into the result.
    NFD::UniquePathU8 path(selected_path);
    return {.status = FileDialogStatus::Accepted, .paths = {path.get()}};
}

static FileDialogResult multiple_paths_result(nfdresult_t result, const nfdpathset_t* selected_paths) {
    if (result != NFD_OKAY) return dialog_result(result);
    if (selected_paths == nullptr) return {.status = FileDialogStatus::Error, .error = "NFD returned no paths"};

    // keep the native path set alive until every selected path has been copied.
    NFD::UniquePathSet paths(selected_paths);
    nfdpathsetsize_t count = 0;
    if (NFD::PathSet::Count(paths, count) != NFD_OKAY) return dialog_result(NFD_ERROR);

    FileDialogResult dialog{.status = FileDialogStatus::Accepted};
    dialog.paths.reserve(count);
    for (nfdpathsetsize_t index = 0; index < count; ++index) {
        NFD::UniquePathSetPathU8 path;
        if (NFD::PathSet::GetPath(paths, index, path) != NFD_OKAY) return dialog_result(NFD_ERROR);
        dialog.paths.emplace_back(path.get());
    }

    return dialog;
}

class NfdFileDialogBackend final : public FileDialogBackend {
public:
    FileDialogResult show(FileDialogOperation operation, const FileDialogOptions& options) override {
        const NfdSession& session = nfd_session();
        if (!session.initialized()) return {.status = FileDialogStatus::Error, .error = session.error()};

        const std::vector<nfdu8filteritem_t> filters = make_filters(options.filters);
        const std::string default_path = options.default_path.string();
        const nfdu8filteritem_t* filter_data = filters.empty() ? nullptr : filters.data();
        const nfdfiltersize_t filter_count = static_cast<nfdfiltersize_t>(filters.size());
        switch (operation) {
            case FileDialogOperation::OpenFile: {
                nfdu8char_t* path = nullptr;
                const nfdresult_t result = NFD::OpenDialog(
                    path, filter_data, filter_count, optional(default_path), {}, optional(options.title),
                    optional(options.accept_label), optional(options.cancel_label)
                );
                return single_path_result(result, path);
            }
            case FileDialogOperation::OpenFiles: {
                const nfdpathset_t* paths = nullptr;
                const nfdresult_t result = NFD::OpenDialogMultiple(
                    paths, filter_data, filter_count, optional(default_path), {}, optional(options.title),
                    optional(options.accept_label), optional(options.cancel_label)
                );
                return multiple_paths_result(result, paths);
            }
            case FileDialogOperation::SaveFile: {
                nfdu8char_t* path = nullptr;
                const nfdresult_t result = NFD::SaveDialog(
                    path, filter_data, filter_count, optional(default_path), optional(options.default_name), {},
                    optional(options.title), optional(options.accept_label), optional(options.cancel_label)
                );
                return single_path_result(result, path);
            }
            case FileDialogOperation::SelectFolder: {
                nfdu8char_t* path = nullptr;
                const nfdresult_t result = NFD::PickFolder(
                    path, optional(default_path), {}, optional(options.title), optional(options.accept_label),
                    optional(options.cancel_label)
                );
                return single_path_result(result, path);
            }
            case FileDialogOperation::SelectFolders: {
                const nfdpathset_t* paths = nullptr;
                const nfdresult_t result = NFD::PickFolderMultiple(
                    paths, optional(default_path), {}, optional(options.title), optional(options.accept_label),
                    optional(options.cancel_label)
                );
                return multiple_paths_result(result, paths);
            }
        }

        return {.status = FileDialogStatus::Error, .error = "unknown file dialog operation"};
    }
};

std::unique_ptr<FileDialogBackend> Surface::make_file_dialog_backend() {
    return std::make_unique<NfdFileDialogBackend>();
}
