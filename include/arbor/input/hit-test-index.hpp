#pragma once

#include "../layout/geometry.hpp"
#include "event.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ui {
    class Node;

    class HitTestIndex {
    public:
        enum class EntryKind : uint8_t {
            Target,
            Blocker,
        };

        struct Entry {
            Node* node = nullptr;
            Rect rect;
            std::shared_ptr<InputCallback> callback;
            EventMask events = EventMask::Pointer;
            EntryKind kind = EntryKind::Target;
        };

        void begin_frame(std::size_t expected_entries);
        void add(Node* node, EntryKind kind, Rect rect, EventMask events, InputCallback callback);
        void register_node(Node& node, bool blocker, Rect input_rect, Rect visual_rect);
        void erase(Node& node);
        void erase_subtree(Node& subtree);

        struct Route {
            std::vector<Entry> targets;
            std::optional<Entry> blocker;
        };

        /// snapshots the deepest hit nodes of each branch front to back, restricted to the active blocker's subtree.
        Route route_at(ImVec2 position, EventType type) const;

        std::size_t size() const;
        std::size_t checks() const;

    private:
        std::vector<Entry> m_entries;
        mutable std::size_t m_checks = 0;
    };

} // namespace ui
