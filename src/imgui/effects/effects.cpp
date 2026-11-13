#include <arbor/imgui/effects/effects.hpp>
#include <arbor/imgui/paint-state.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstring>

using namespace ui;

void EffectRegistry::render_command(const ImDrawList* draw_list, const ImDrawCmd* draw_command) {
    // imgui copies callback bytes without alignment padding. read the header into an aligned local before dispatch.
    QueuedCommand command;
    std::memcpy(&command, draw_command->UserCallbackData, sizeof(command));
    command.render(command.user_data, draw_list, draw_command, command.command);
}

const EffectRegistry::Entry* EffectRegistry::find(uint32_t id) const {
    const auto iterator = std::find_if(m_entries.begin(), m_entries.end(), [id](const Entry& entry) { return entry.id == id; });
    return iterator == m_entries.end() ? nullptr : &*iterator;
}

EffectPass EffectRegistry::register_effect(EffectDefinition definition, std::size_t command_size, std::size_t command_alignment) {
    if (definition.render == nullptr || command_size == 0 || command_alignment == 0) {
        return {};
    }

    if (m_initialized && definition.initialize != nullptr && !definition.initialize(definition.user_data)) {
        return {};
    }

    if (m_next_id == 0) {
        m_next_id = 1;
    }

    const uint32_t id = m_next_id++;
    m_entries.push_back({definition, command_size, command_alignment, id, m_initialized});
    return {this, id};
}

bool EffectRegistry::unregister_effect(EffectPass pass) {
    if (pass.m_registry != this) {
        return false;
    }

    const uint32_t id = pass.m_id;
    const auto iterator = std::find_if(m_entries.begin(), m_entries.end(), [id](const Entry& entry) { return entry.id == id; });
    if (iterator == m_entries.end()) {
        return false;
    }

    if (iterator->initialized && iterator->definition.shutdown != nullptr) {
        iterator->definition.shutdown(iterator->definition.user_data);
    }
    for (EffectPass& slot : m_slots) {
        if (slot.m_id == id) {
            slot = {};
        }
    }
    m_entries.erase(iterator);
    return true;
}

bool EffectRegistry::initialize() {
    if (m_initialized) {
        return true;
    }

    for (Entry& entry : m_entries) {
        // release the effects that initialized before this failure in reverse registration order.
        if (entry.definition.initialize != nullptr && !entry.definition.initialize(entry.definition.user_data)) {
            shutdown();
            return false;
        }

        entry.initialized = true;
    }

    m_initialized = true;
    return true;
}

void EffectRegistry::begin_frame() {
    if (!m_initialized) {
        return;
    }

    m_command_memory.release();

    for (Entry& entry : m_entries) {
        if (entry.initialized && entry.definition.begin_frame != nullptr) {
            entry.definition.begin_frame(entry.definition.user_data);
        }
    }
}

void EffectRegistry::shutdown() {
    for (auto iterator = m_entries.rbegin(); iterator != m_entries.rend(); ++iterator) {
        if (!iterator->initialized) {
            continue;
        }

        if (iterator->definition.shutdown != nullptr) {
            iterator->definition.shutdown(iterator->definition.user_data);
        }
        iterator->initialized = false;
    }

    m_initialized = false;
}

bool EffectRegistry::submit(ImDrawList& draw_list, uint32_t id, const void* command, std::size_t size) const {
    if (!m_initialized || command == nullptr) {
        return false;
    }

    const Entry* entry = find(id);
    if (entry == nullptr || !entry->initialized || entry->definition.render == nullptr || size != entry->command_size) {
        return false;
    }

    void* copied_command = m_command_memory.allocate(size, entry->command_alignment);
    std::memcpy(copied_command, command, size);
    QueuedCommand queued{entry->definition.render, entry->definition.user_data, copied_command};
    draw_list.AddCallback(render_command, &queued, sizeof(queued));
    if (id != m_slots[static_cast<std::size_t>(EffectSlot::Clip)].m_id) {
        draw_list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        // effect shaders replace imgui's program. restore the active rounded clip after the native reset.
        effect(EffectSlot::Clip).submit(draw_list, ClipCommand{.restore = true});
    }
    return true;
}
