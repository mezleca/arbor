#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <type_traits>
#include <vector>

struct ImDrawList;
struct ImDrawCmd;

namespace ui {
    using EffectInitialize = bool (*)(void* user_data);
    using EffectFrame = void (*)(void* user_data);
    using EffectShutdown = void (*)(void* user_data);
    using EffectRender = void (*)(void* user_data, const ImDrawList*, const ImDrawCmd*, const void* command);

    class EffectRegistry;

    enum class EffectSlot : uint8_t {
        Blur,
        BoxShadow,
        Gradient,
        Clip,
    };

    class EffectPass {
    public:
        EffectPass() = default;

        /// copies a trivially copyable command into the frame queue for deferred rendering.
        template <typename Command>
        bool submit(ImDrawList& draw_list, const Command& command) const;

    private:
        friend class EffectRegistry;

        EffectPass(EffectRegistry* registry, uint32_t id) : m_registry(registry), m_id(id) {}

        EffectRegistry* m_registry = nullptr;
        uint32_t m_id = 0;
    };

    struct EffectDefinition {
        /// renders one copied command at its original draw-list position.
        EffectRender render = nullptr;

        /// creates backend resources after the imgui context is ready.
        EffectInitialize initialize = nullptr;

        /// selects or resets per-frame backend state before node drawing.
        EffectFrame begin_frame = nullptr;

        /// releases backend resources before the imgui context is destroyed.
        EffectShutdown shutdown = nullptr;
        void* user_data = nullptr;
    };

    class EffectRegistry {
    public:
        EffectRegistry() = default;

        EffectRegistry(const EffectRegistry&) = delete;
        EffectRegistry& operator=(const EffectRegistry&) = delete;

        template <typename Command>
        EffectPass register_effect(EffectDefinition definition) {
            static_assert(std::is_trivially_copyable_v<Command>);
            return register_effect(definition, sizeof(Command), alignof(Command));
        }

        template <typename Command>
        EffectPass register_effect(EffectSlot slot, EffectDefinition definition) {
            EffectPass pass = register_effect<Command>(definition);
            m_slots[static_cast<std::size_t>(slot)] = pass;
            return pass;
        }

        const EffectPass& effect(EffectSlot slot) const {
            return m_slots[static_cast<std::size_t>(slot)];
        }

        bool unregister_effect(EffectPass pass);

        bool initialize();
        void begin_frame();
        void shutdown();

    private:
        friend class EffectPass;

        struct QueuedCommand {
            EffectRender render = nullptr;
            void* user_data = nullptr;
            const void* command = nullptr;
        };

        struct Entry {
            EffectDefinition definition{};
            std::size_t command_size = 0;
            std::size_t command_alignment = 0;
            uint32_t id = 0;
            bool initialized = false;
        };

        EffectPass register_effect(EffectDefinition definition, std::size_t command_size, std::size_t command_alignment);
        bool submit(ImDrawList& draw_list, uint32_t id, const void* command, std::size_t size) const;
        static void render_command(const ImDrawList* draw_list, const ImDrawCmd* draw_command);
        const Entry* find(uint32_t id) const;

        std::vector<Entry> m_entries;
        mutable std::pmr::monotonic_buffer_resource m_command_memory;
        std::array<EffectPass, 4> m_slots;
        uint32_t m_next_id = 1;
        bool m_initialized = false;
    };

    template <typename Command>
    bool EffectPass::submit(ImDrawList& draw_list, const Command& command) const {
        static_assert(std::is_trivially_copyable_v<Command>);
        return m_registry != nullptr && m_registry->submit(draw_list, m_id, &command, sizeof(Command));
    }

} // namespace ui
