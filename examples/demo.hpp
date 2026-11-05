#pragma once

#include <imgui-ui/runtime.hpp>
#include <string_view>

namespace ui {
    class Surface;
}

void configure_demo_runtime(ui::RuntimeConfig& config);

// requires the surface's initialized imgui context. the demo uses one surface per process.
void setup_demo(ui::Surface& surface, std::string_view backend);

// runs the demo until the surface closes.
void run_demo(ui::Surface& surface, std::string_view backend);
