#pragma once

#include <cstddef>
#include <imgui.h>
#include <memory>

namespace ui {
    struct TextureMemoryUsage {
        std::size_t cpu_bytes = 0;
        std::size_t gpu_bytes = 0;
    };

    class Texture {
    public:
        virtual ~Texture() = default;
        virtual ImVec2 size() const = 0;

        // the opengl loader creates gpu data lazily for each imgui context and reuses it later.
        virtual ImTextureID get(ImVec2 size) = 0;

        // release context-local data while retaining the source for later contexts.
        virtual void release_context(ImGuiContext* context) = 0;

        // estimated payload bytes, excluding allocator/driver overhead. nullptr sums all gpu contexts.
        // custom loaders should override this to participate in byte budgets.
        virtual TextureMemoryUsage memory_usage(ImGuiContext* = nullptr) const {
            return {};
        }
    };

    using TextureHandle = std::shared_ptr<Texture>;
} // namespace ui
