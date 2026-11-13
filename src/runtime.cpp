#include <arbor/runtime.hpp>

#include <utility>

using namespace ui;

Runtime::Runtime(RuntimeConfig config)
    : m_theme(std::move(config.theme)), m_textures(std::move(config.texture_loader), config.texture_cache),
      m_performance_directory(std::move(config.performance_directory)) {}
