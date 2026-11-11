#include <imgui-ui/resources/font-registry.hpp>
#include "fonts/platform.hpp"
#include <imgui-ui/imgui/context-scope.hpp>

#include <misc/freetype/imgui_freetype.h>
#include <stdexcept>
#include <utility>

using namespace ui;

static ImFontConfig default_font_config() {
    ImFontConfig config;
    config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    return config;
}

Font::Font(std::filesystem::path location, ImFontConfig config)
    : m_sources{{std::move(location), config.FontNo}}, m_config(config) {}

Font::Font(std::vector<FontSource> sources) : m_sources(std::move(sources)), m_config(default_font_config()) {}

Font::Font(FontRegistry& registry, FontRequest request, std::vector<FontSource> sources)
    : m_sources(std::move(sources)), m_config(default_font_config()), m_registry(&registry), m_request(std::move(request)) {}

ImFont* Font::get(std::optional<FontWeight> weight, std::optional<FontSlant> slant) {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        return nullptr;
    }

    if (m_registry != nullptr) {
        weight = weight.value_or(m_request.weight);
        slant = slant.value_or(m_request.slant);
    }

    auto [variant_it, inserted] = m_variants.try_emplace(std::pair{weight, slant});
    auto& variant = variant_it->second;

    if (inserted) {
        const bool customized = m_registry != nullptr && (weight != m_request.weight || slant != m_request.slant);

        // a different style may select another face in the same family
        if (customized) {
            auto request = m_request;
            request.weight = *weight;
            request.slant = *slant;
            variant.sources = m_registry->resolve(request);
        } else {
            variant.sources = m_sources;
        }

        // variable fonts expose the requested style as named instances
        if (weight || slant) {
            resolve_font_variants(variant.sources, weight, slant);
        }
    }

    const auto font_it = variant.contexts.find(context);
    if (font_it != variant.contexts.end()) {
        return font_it->second;
    }

    // different requests can resolve to the same available face
    for (const auto& entry : m_variants) {
        const auto& other = entry.second;
        if (&other == &variant || other.sources != variant.sources) continue;

        const auto existing = other.contexts.find(context);
        if (existing != other.contexts.end()) {
            variant.contexts.emplace(context, existing->second);
            return existing->second;
        }
    }

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    auto& files = m_files[context];
    ImFont* font = nullptr;
    for (const auto& source : variant.sources) {
        std::error_code error;
        // skip missing files before imgui hits its load failure assertion
        if (!std::filesystem::is_regular_file(source.location, error)) continue;

        auto config = m_config;
        config.FontNo = source.index;
        // later sources only contribute glyphs to the first loaded face
        if (font != nullptr) {
            config.MergeMode = true;
            config.DstFont = font;
        }

        ImFont* loaded = nullptr;
        const auto file = files.find(source.location);
        if (file != files.end()) {
            config.FontDataOwnedByAtlas = false;
            loaded = atlas->AddFontFromMemoryTTF(file->second.data, file->second.size, 0.0f, &config);
        } else {
            config.FontDataOwnedByAtlas = true;
            // imgui expects utf-8 paths, which keeps non-ascii names intact on windows
            const auto path = source.location.u8string();
            loaded = atlas->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), 0.0f, &config);
            if (loaded != nullptr) {
                const auto& input = atlas->Sources.back();
                files.emplace(source.location, FontFileData{input.FontData, input.FontDataSize});
            }
        }
        if (font == nullptr) font = loaded;
    }

    if (font != nullptr) {
        variant.contexts.emplace(context, font);
    }

    return font;
}

void Font::release_context(ImGuiContext* context) {
    m_files.erase(context);
    for (auto& entry : m_variants) {
        entry.second.contexts.erase(context);
    }
}

void Font::unload() {
    for (const auto& [key, variant] : m_variants) {
        for (const auto& [context, font] : variant.contexts) {
            const ImGuiContextScope scope(context);
            auto* atlas = ImGui::GetIO().Fonts;
            if (!atlas->Fonts.contains(font)) continue;

            // a shared file must remain owned by a source which is still alive
            for (auto* source : font->Sources) {
                if (!source->FontDataOwnedByAtlas) continue;
                for (auto& other : atlas->Sources) {
                    if (other.DstFont != font && other.FontData == source->FontData) {
                        other.FontDataOwnedByAtlas = true;
                        source->FontDataOwnedByAtlas = false;
                        break;
                    }
                }
            }
            atlas->RemoveFont(font);
        }
    }
    m_variants.clear();
    m_files.clear();
}

void Font::replace(Font next) {
    if (m_config.MergeMode) throw std::logic_error("cannot replace a merged font registration");
    // load replacements before deleting old fonts so measurement caches cannot see recycled pointers
    for (const auto& [style, variant] : m_variants) {
        for (const auto& [context, font] : variant.contexts) {
            const ImGuiContextScope scope(context);
            next.get(style.first, style.second);
        }
    }
    unload();
    *this = std::move(next);
}

ImFont* FontReference::resolve(std::optional<FontWeight> weight, std::optional<FontSlant> slant) const {
    return m_font != nullptr ? m_font->get(weight, slant) : m_native_font;
}

Font* FontRegistry::add(std::string id, std::filesystem::path location) {
    return add(std::move(id), std::move(location), default_font_config());
}

Font* FontRegistry::add(std::string id, std::filesystem::path location, ImFontConfig config) {
    if (Font* existing = find(id)) return existing;

    return add_asset(std::move(id), std::make_unique<Font>(std::move(location), config));
}

Font* FontRegistry::add(std::string id, const FontRequest& request) {
    if (Font* existing = find(id)) return existing;

    return set(std::move(id), request);
}

Font* FontRegistry::set(std::string id, const FontRequest& request) {

    auto sources = resolve(request);
    if (sources.empty()) return nullptr;

    // keep later variants in the family that supplied the main face
    auto resolved = request;
    const auto& main_source = sources.front();
    for (const auto& face : m_faces) {
        if (face.source == main_source) {
            resolved.main = FontSelection::family(face.family_id);
            break;
        }
    }

    Font next(*this, std::move(resolved), std::move(sources));
    if (Font* existing = find(id)) {
        existing->replace(std::move(next));
        return existing;
    }
    return add_asset(std::move(id), std::make_unique<Font>(std::move(next)));
}
