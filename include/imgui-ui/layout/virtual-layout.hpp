#pragma once

#include "container.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <utility>

namespace ui {
    class VirtualLayout : public Container {
    public:
        /// returns the direct child representing a source index. the provider may be called again for the same index,
        /// so retain and return the same node when its state should persist. the node must remain alive through the draw.
        using ItemProvider = std::function<Node&(size_t)>;

        explicit VirtualLayout(std::string id, float item_height);

        /// sets the source count. a supplied provider replaces the previous one, and a nonzero count requires a provider.
        VirtualLayout& set_items(size_t count, ItemProvider provider = {});
        VirtualLayout& set_item_height(float height);
        VirtualLayout& set_spacing(float spacing) override;

        /// requests up to count extra rows on each side of the visible range, clipped to the source bounds.
        /// these rows are requested from the provider before they enter the viewport.
        VirtualLayout& set_overscan(size_t count) {
            m_overscan = count;
            return *this;
        }

        /// adds nonnegative pixels to this row's normal height. zero removes the extra extent.
        /// later row positions and the total scroll extent include this offset.
        /// throws for an index outside the source range, or a negative or nonfinite offset.
        VirtualLayout& set_extra_offset(size_t index, float offset);
        float extra_offset(size_t index) const;
        VirtualLayout& clear_extra_offsets();

        size_t item_count() const {
            return m_item_count;
        }

    protected:
        void on_measure() override;
        ImVec2 measure_content(ImVec2 available) const override;
        void draw_children() override;
        bool paint() override;

    private:
        using ItemRange = std::pair<size_t, size_t>;

        float content_height() const;
        size_t item_boundary(float position, bool end) const;
        void draw_range(size_t first, size_t count, float height, float width, ItemRange buffer);

        float m_item_height = 1.0F;
        float m_spacing = 0.0F;
        float m_extra_height = 0.0F;
        size_t m_item_count = 0;
        size_t m_overscan = 0;
        ItemProvider m_item_provider;
        std::map<size_t, float> m_extra_offsets;
    };
} // namespace ui
