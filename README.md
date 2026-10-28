# imgui-ui

A small ui framework built on top of [Dear ImGui](https://github.com/ocornut/imgui).

imgui-ui provides:
- node tree's
- layouts (containers)
- tween (animator)
- styling
- input routing
- custom widgets

while keeping full compatibility with imgui internals.

## requirements

- **C++23**
- **Dear ImGui ≥ 1.92.8** (bundled: 1.93.0)
- **CMake ≥ 3.25**
- **FreeType**

## usage

```cmake
set(IMGUI_UI_BUILD_SDL ON)

# use custom imgui (needs to be set BEFORE including imgui-ui)
set(IMGUI_UI_IMGUI_DIR "...")

add_subdirectory(vendor/imgui-ui)
target_link_libraries(my-app PRIVATE imgui-ui::sdl)
```

```cpp
#include <imgui-ui/backends/sdl/backend.hpp>
#include <imgui-ui/runtime.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/tree/node.hpp>
#include <imgui-ui/widgets/button.hpp>

#include <SDL3/SDL.h>
#include <memory>
#include <utility>

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* window = SDL_CreateWindow("example", 900, 600, SDL_WINDOW_OPENGL);
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

    {
        ui::Runtime runtime;
        auto backend = std::make_unique<ui::SdlBackend>(window, context);
        ui::Surface surface(runtime, {.backend = std::move(backend)});
        surface.root().add<ui::ButtonWidget>("hello");

        while (!surface.is_done()) {
            surface.process_events();
            surface.frame();
        }
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```

<img src="https://github.com/mezleca/imgui-ui/blob/main/assets/images/preview.png?raw=true" width="60%"/><br>

see `examples/` for the complete demo.
