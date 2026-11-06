#include <imgui-ui/backends/opengl/texture-loader.hpp>

#include <glad/gl.h>
#include <lunasvg.h>
#include <plutovg.h>

extern "C" {
#include <nsgif.h>
}

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace ui;

static GLuint create_opengl_texture(GLsizei width, GLsizei height, GLenum source_format, const void* pixels) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, source_format, GL_UNSIGNED_BYTE, pixels);
    return texture;
}

struct GifBitmap {
    explicit GifBitmap(std::size_t size) : pixels(size) {}

    std::vector<uint8_t> pixels;
};

struct GifContextTexture {
    GLuint id = 0;
    uint64_t revision = 0;
};

static constexpr std::size_t maximum_gif_bitmap_bytes = std::size_t{512} * 1024U * 1024U;

static nsgif_bitmap_t* create_gif_bitmap(int width, int height) {
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    const std::size_t bitmap_width = static_cast<std::size_t>(width);
    const std::size_t bitmap_height = static_cast<std::size_t>(height);
    if (bitmap_width > maximum_gif_bitmap_bytes / 4U / bitmap_height) {
        return nullptr;
    }

    try {
        return new GifBitmap(bitmap_width * bitmap_height * 4U);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

static void destroy_gif_bitmap(nsgif_bitmap_t* bitmap) {
    delete static_cast<GifBitmap*>(bitmap);
}

static uint8_t* gif_bitmap_buffer(nsgif_bitmap_t* bitmap) {
    return static_cast<GifBitmap*>(bitmap)->pixels.data();
}

static bool has_area(nsgif_rect_t area) {
    return area.x0 < area.x1 && area.y0 < area.y1;
}

template <typename Map, typename Release>
static void release_context_entry(Map& entries, ImGuiContext* context, Release&& release) {
    const auto found = entries.find(context);
    if (found == entries.end()) return;

    release(found->second);
    entries.erase(found);
}

static void update_gif_texture(GLuint texture, const GifBitmap& bitmap, int bitmap_width, nsgif_rect_t area) {
    const int width = static_cast<int>(area.x1 - area.x0);
    const int height = static_cast<int>(area.y1 - area.y0);
    const std::size_t pixel_offset = ((static_cast<std::size_t>(area.y0) * bitmap_width) + area.x0) * 4U;
    const uint8_t* pixels = bitmap.pixels.data() + pixel_offset;

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bitmap_width);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, static_cast<int>(area.x0), static_cast<int>(area.y0), width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels
    );
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static const nsgif_bitmap_cb_vt gif_bitmap_callbacks = {
    .create = create_gif_bitmap,
    .destroy = destroy_gif_bitmap,
    .get_buffer = gif_bitmap_buffer,
    .set_opaque = nullptr,
    .test_opaque = nullptr,
    .modified = nullptr,
    .get_rowspan = nullptr,
};

class OpenGLTexture final : public Texture {
public:
    OpenGLTexture(std::unique_ptr<lunasvg::Document> document, std::size_t source_bytes)
        : m_document(std::move(document)), m_source_bytes(source_bytes) {}

    ImVec2 size() const override {
        return {m_document->width(), m_document->height()};
    }

    ImTextureID get(ImVec2 size) override {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr) {
            return {};
        }

        const int width = static_cast<int>(size.x);
        const int height = static_cast<int>(size.y);
        const uint64_t size_key = (static_cast<uint64_t>(static_cast<uint32_t>(width)) << 32U) | static_cast<uint32_t>(height);
        auto& cache = m_textures[context];
        const auto existing = cache.find(size_key);
        if (existing != cache.end()) {
            return static_cast<ImTextureID>(existing->second);
        }

        lunasvg::Bitmap bitmap = m_document->renderToBitmap(width, height);
        bitmap.convertToRGBA();

        // the upload copies these pixels. the size cache only needs the gpu texture id afterward.
        const GLuint texture = create_opengl_texture(width, height, GL_RGBA, bitmap.data());

        cache.emplace(size_key, texture);
        return static_cast<ImTextureID>(texture);
    }

    void release_context(ImGuiContext* context) override {
        release_context_entry(m_textures, context, [](const auto& cache) {
            for (const auto& entry : cache) {
                const GLuint id = entry.second;
                glDeleteTextures(1, &id);
            }
        });
    }

    TextureMemoryUsage memory_usage(ImGuiContext* context) const override {
        // lunasvg does not expose document allocation sizes. use source bytes as the cpu estimate.
        TextureMemoryUsage usage{m_source_bytes, 0};
        for (const auto& [owner, cache] : m_textures) {
            if (context != nullptr && owner != context) {
                continue;
            }
            for (const auto& [size, id] : cache) {
                usage.gpu_bytes += static_cast<std::size_t>(size >> 32U) * static_cast<uint32_t>(size) * 4U;
            }
        }
        return usage;
    }

private:
    std::unordered_map<ImGuiContext*, std::unordered_map<uint64_t, GLuint>> m_textures;
    std::unique_ptr<lunasvg::Document> m_document;
    std::size_t m_source_bytes;
};

class OpenGLRasterTexture final : public Texture {
public:
    explicit OpenGLRasterTexture(plutovg_surface_t* surface) : m_surface(surface) {}

    ~OpenGLRasterTexture() override {
        plutovg_surface_destroy(m_surface);
    }

    OpenGLRasterTexture(const OpenGLRasterTexture&) = delete;
    OpenGLRasterTexture& operator=(const OpenGLRasterTexture&) = delete;

    ImVec2 size() const override {
        return {
            static_cast<float>(plutovg_surface_get_width(m_surface)),
            static_cast<float>(plutovg_surface_get_height(m_surface)),
        };
    }

    ImTextureID get(ImVec2) override {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr) {
            return {};
        }

        const auto existing = m_textures.find(context);
        if (existing != m_textures.end()) {
            return static_cast<ImTextureID>(existing->second);
        }

        const GLuint texture = create_opengl_texture(
            plutovg_surface_get_width(m_surface), plutovg_surface_get_height(m_surface), GL_BGRA,
            plutovg_surface_get_data(m_surface)
        );

        m_textures.emplace(context, texture);
        return static_cast<ImTextureID>(texture);
    }

    void release_context(ImGuiContext* context) override {
        release_context_entry(m_textures, context, [](GLuint texture) { glDeleteTextures(1, &texture); });
    }

    TextureMemoryUsage memory_usage(ImGuiContext* context) const override {
        const auto bytes =
            static_cast<std::size_t>(plutovg_surface_get_stride(m_surface)) * plutovg_surface_get_height(m_surface);
        const auto copies = context == nullptr ? m_textures.size() : static_cast<std::size_t>(m_textures.contains(context));
        return {bytes, bytes * copies};
    }

private:
    plutovg_surface_t* m_surface = nullptr;
    std::unordered_map<ImGuiContext*, GLuint> m_textures;
};

class OpenGLGifTexture final : public Texture {
public:
    explicit OpenGLGifTexture(std::vector<uint8_t> data) : m_data(std::move(data)), m_gif(nullptr, nsgif_destroy) {
        nsgif_t* gif = nullptr;
        const nsgif_error create_error = nsgif_create(&gif_bitmap_callbacks, NSGIF_BITMAP_FMT_R8G8B8A8, &gif);
        if (create_error != NSGIF_OK) {
            throw std::runtime_error(std::format("failed to create gif decoder: {}", nsgif_strerror(create_error)));
        }

        m_gif.reset(gif);
        const nsgif_error scan_error = nsgif_data_scan(m_gif.get(), m_data.size(), m_data.data());
        nsgif_data_complete(m_gif.get());

        const nsgif_info_t* info = nsgif_get_info(m_gif.get());
        // a malformed trailing frame can still leave earlier frames drawable.
        if (info == nullptr || info->frame_count == 0 || info->width == 0 || info->height == 0) {
            throw std::runtime_error(std::format("failed to load gif texture: {}", nsgif_strerror(scan_error)));
        }

        m_size = {static_cast<float>(info->width), static_cast<float>(info->height)};
    }

    ImVec2 size() const override {
        return m_size;
    }

    ImTextureID get(ImVec2) override {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr) {
            return {};
        }

        advance(ImGui::GetTime());
        if (m_bitmap == nullptr) {
            return {};
        }

        GifContextTexture& texture = m_textures[context];
        if (texture.id == 0) {
            texture.id = create_opengl_texture(
                static_cast<GLsizei>(m_size.x), static_cast<GLsizei>(m_size.y), GL_RGBA, m_bitmap->pixels.data()
            );
            texture.revision = m_revision;
        } else if (texture.revision != m_revision) {
            // upload only the dirty area for consecutive revisions. skipped revisions require the complete decoded bitmap.
            if (texture.revision + 1 == m_revision && has_area(m_dirty)) {
                update_gif_texture(texture.id, *m_bitmap, static_cast<int>(m_size.x), m_dirty);
            } else {
                update_gif_texture(
                    texture.id, *m_bitmap, static_cast<int>(m_size.x),
                    {.x0 = 0, .y0 = 0, .x1 = static_cast<uint32_t>(m_size.x), .y1 = static_cast<uint32_t>(m_size.y)}
                );
            }
            texture.revision = m_revision;
        }

        return static_cast<ImTextureID>(texture.id);
    }

    void release_context(ImGuiContext* context) override {
        release_context_entry(m_textures, context, [](const auto& texture) { glDeleteTextures(1, &texture.id); });
    }

    TextureMemoryUsage memory_usage(ImGuiContext* context) const override {
        const auto canvas_bytes = static_cast<std::size_t>(m_size.x) * static_cast<std::size_t>(m_size.y) * 4U;
        const auto copies = context == nullptr ? m_textures.size() : static_cast<std::size_t>(m_textures.contains(context));
        // decoder metadata and internal frame caches are not exposed by libnsgif.
        return {m_data.size() + (m_bitmap != nullptr ? m_bitmap->pixels.size() : 0), canvas_bytes * copies};
    }

private:
    void advance(double time) {
        if (m_finished || (m_started && time < m_next_frame_time)) {
            return;
        }

        if (!m_started) {
            m_started = true;
            m_next_frame_time = time;
        }

        uint32_t delay = 0;
        uint32_t frame = 0;
        nsgif_rect_t area{};
        const nsgif_error prepare_error = nsgif_frame_prepare(m_gif.get(), &area, &delay, &frame);
        if (prepare_error == NSGIF_ERR_ANIMATION_END) {
            m_finished = true;
            return;
        }
        if (prepare_error != NSGIF_OK) {
            throw std::runtime_error(std::format("failed to prepare gif frame: {}", nsgif_strerror(prepare_error)));
        }

        nsgif_bitmap_t* bitmap = nullptr;
        const nsgif_error decode_error = nsgif_frame_decode(m_gif.get(), frame, &bitmap);
        if (decode_error != NSGIF_OK) {
            throw std::runtime_error(std::format("failed to decode gif frame: {}", nsgif_strerror(decode_error)));
        }

        m_bitmap = static_cast<GifBitmap*>(bitmap);
        m_dirty = area;
        ++m_revision;

        if (delay == NSGIF_INFINITE) {
            m_finished = true;
        } else {
            m_next_frame_time += static_cast<double>(delay) / 100.0;
        }
    }

    // libnsgif reads this source while frames are decoded.
    std::vector<uint8_t> m_data;
    std::unique_ptr<nsgif_t, decltype(&nsgif_destroy)> m_gif;
    std::unordered_map<ImGuiContext*, GifContextTexture> m_textures;
    GifBitmap* m_bitmap = nullptr;
    ImVec2 m_size;
    nsgif_rect_t m_dirty{};
    double m_next_frame_time = 0.0;
    uint64_t m_revision = 0;
    bool m_started = false;
    bool m_finished = false;
};

static std::vector<uint8_t> load_binary_file(const std::filesystem::path& location) {
    std::ifstream file(location, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::format("failed to load texture {}", location.string()));
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error(std::format("failed to load texture {}", location.string()));
    }

    std::vector<uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error(std::format("failed to load texture {}", location.string()));
    }

    return data;
}

static bool is_gif(std::string_view content) {
    return content.starts_with("GIF87a") || content.starts_with("GIF89a");
}

std::unique_ptr<Texture> OpenGLTextureLoader::load(const std::filesystem::path& location, std::string) {
    // decode the source here without allocating gpu objects. get uploads per context and rasterizes svg at the requested size.
    const std::filesystem::path extension = location.extension();
    if (extension == ".gif") {
        return std::make_unique<OpenGLGifTexture>(load_binary_file(location));
    }

    if (extension != ".svg") {
        plutovg_surface_t* surface = plutovg_surface_load_from_image_file(location.string().c_str());

        if (surface == nullptr) {
            throw std::runtime_error(std::format("failed to load texture {}", location.string()));
        }

        return std::make_unique<OpenGLRasterTexture>(surface);
    }

    std::unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromFile(location.string());

    if (document == nullptr) {
        throw std::runtime_error(std::format("failed to load texture {}", location.string()));
    }

    return std::make_unique<OpenGLTexture>(std::move(document), std::filesystem::file_size(location));
}

std::unique_ptr<Texture> OpenGLTextureLoader::load(std::string_view content, std::string) {
    if (is_gif(content)) {
        return std::make_unique<OpenGLGifTexture>(std::vector<uint8_t>(content.begin(), content.end()));
    }

    if (std::in_range<int>(content.size())) {
        if (auto* surface = plutovg_surface_load_from_image_data(content.data(), static_cast<int>(content.size()))) {
            return std::make_unique<OpenGLRasterTexture>(surface);
        }
    }

    std::unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(std::string{content});
    if (document == nullptr) {
        throw std::runtime_error("failed to load texture data");
    }
    return std::make_unique<OpenGLTexture>(std::move(document), content.size());
}
