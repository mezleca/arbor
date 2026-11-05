#include "../demo.hpp"

#include <imgui-ui/backends/sdl/backend.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/runtime.hpp>
#include <SDL3/SDL.h>
#include <memory>
#include <utility>

using namespace ui;

int main() {
#ifdef __linux__
    // try wayland before falling back to x11 on linux.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("imgui-ui sdl", 1120, 920, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    // destroy the surface before destroying the application-owned graphics context.
    {
        RuntimeConfig runtime_config;
        configure_demo_runtime(runtime_config);
        Runtime runtime(std::move(runtime_config));

        Surface surface(
            runtime, {
                         .backend = std::make_unique<SdlBackend>(window, context),
                         .enable_debugger = true,
                     }
        );

        run_demo(surface, "sdl");
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
