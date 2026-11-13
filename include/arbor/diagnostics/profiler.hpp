#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ui {
    struct ProfileEvent {
        std::string_view name;
        uint64_t start = 0;
        uint64_t end = 0;
        uint64_t node_identity = 0;
    };

    struct ProfileFrameMetrics {
        // phase times are inclusive.
        std::size_t nodes_drawn = 0;
        std::size_t input_entries = 0;
        std::size_t input_entry_checks = 0;
        double update_ms = 0.0;
        double measure_ms = 0.0;
        double layout_ms = 0.0;
        double draw_ms = 0.0;
        double input_ms = 0.0;
        double render_ms = 0.0;

        // the gpu query completes asynchronously and usually describes the preceding frame.
        std::optional<double> gpu_render_ms;
    };

    struct ProfileGpuSummary {
        std::size_t samples = 0;
        double average_ms = 0.0;
        double minimum_ms = 0.0;
        double maximum_ms = 0.0;
    };

    class Profiler {
    public:
        static constexpr std::size_t EVENT_CAPACITY = 8192;

        explicit Profiler(std::filesystem::path output_directory = {});

        /// starts or stops recording. stopping also clears the rolling gpu sample window.
        /// event storage is allocated on first activation and reused between frames.
        void set_enabled(bool enabled);
        bool enabled() const;
        void set_root_node(uint64_t identity);

        void begin_frame();
        void end_frame();

        std::span<const ProfileEvent> latest_events() const;
        const ProfileFrameMetrics& latest_metrics() const;

        /// summarizes up to 60 completed gpu queries, which can describe earlier frames.
        ProfileGpuSummary gpu_render_summary() const;
        double latest_frame_ms() const;
        double node_duration_ms(uint64_t node_identity) const;
        uint32_t dropped_events() const;
        void record_frame_metrics(std::size_t input_entries, std::size_t input_entry_checks);

        /// stores the latest completed gpu query in the current frame and rolling summary.
        void record_gpu_render_ms(std::optional<double> gpu_ms);
        void clear_report();
        bool has_report() const;

        /// writes aggregate timings and the latest frame snapshot to output_path. returns false when no report exists or
        /// writing fails.
        bool save_report() const;
        const std::filesystem::path& output_path() const;

    private:
        friend class ScopedProfileZone;
        friend class Node;

        struct ZoneToken {
            std::size_t event_index = 0;
            bool recorded = false;
        };

        struct FrameBuffer {
            std::vector<ProfileEvent> events;
            uint64_t start = 0;
            uint64_t end = 0;
            uint32_t dropped = 0;
            ProfileFrameMetrics metrics;
            mutable std::unordered_map<uint64_t, double> node_durations;
            mutable bool node_durations_ready = false;
        };

        struct MetricSummary {
            uint64_t samples = 0;
            double total = 0.0;
            double minimum = 0.0;
            double maximum = 0.0;
            double last = 0.0;

            void record(double value);
            double average() const;
        };

        ZoneToken begin_zone(std::string_view name, uint64_t node_identity);
        void end_zone(ZoneToken token);
        void record_node_draw();
        void record_root_phase_times(FrameBuffer& frame) const;

        std::array<FrameBuffer, 2> m_frames;
        std::array<double, 60> m_gpu_samples{};
        std::filesystem::path m_output_path;
        MetricSummary m_frame_metric;
        std::size_t m_gpu_sample_count = 0;
        std::size_t m_gpu_sample_next = 0;
        std::size_t m_write_index = 0;
        std::size_t m_read_index = 1;
        uint64_t m_root_identity = 0;
        bool m_enabled = false;
        bool m_frame_open = false;
    };

    class ScopedProfileZone {
    public:
        ScopedProfileZone(Profiler* profiler, std::string_view name, uint64_t node_identity = 0);
        ~ScopedProfileZone();

        ScopedProfileZone(const ScopedProfileZone&) = delete;
        ScopedProfileZone& operator=(const ScopedProfileZone&) = delete;

    private:
        Profiler* m_profiler = nullptr;
        Profiler::ZoneToken m_token;
    };
} // namespace ui

#define UI_PROFILE_JOIN_INNER(left, right) left##right
#define UI_PROFILE_JOIN(left, right) UI_PROFILE_JOIN_INNER(left, right)

#ifndef ARBOR_ENABLE_PROFILING
#define ARBOR_ENABLE_PROFILING 1
#endif

#if ARBOR_ENABLE_PROFILING
#define UI_PROFILE_SCOPE(profiler, name)                                                                                         \
    ::ui::ScopedProfileZone UI_PROFILE_JOIN(profile_scope_, __LINE__) {                                                          \
        profiler, name                                                                                                           \
    }
#define UI_PROFILE_NODE(profiler, name, node_identity)                                                                           \
    ::ui::ScopedProfileZone UI_PROFILE_JOIN(profile_scope_, __LINE__) {                                                          \
        profiler, name, node_identity                                                                                            \
    }
#else
#define UI_PROFILE_SCOPE(profiler, name)
#define UI_PROFILE_NODE(profiler, name, node_identity)
#endif
