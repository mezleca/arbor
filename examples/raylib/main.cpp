#include "../demo.hpp"

#include <arbor/backends/raylib/backend.hpp>
#include <arbor/surface.hpp>
#include <arbor/runtime.hpp>
#include <raylib.h>
#include <memory>
#include <utility>

using namespace ui;

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1120, 920, "arbor raylib");

    // destroy the surface before destroying the application-owned graphics context.
    {
        RuntimeConfig runtime_config;
        configure_demo_runtime(runtime_config);
        Runtime runtime(std::move(runtime_config));

        Surface surface(
            runtime, {
                         .backend = std::make_unique<RaylibBackend>(),
                         .enable_debugger = true,
                     }
        );

        run_demo(surface, "raylib");
    }

    CloseWindow();
    return 0;
}
