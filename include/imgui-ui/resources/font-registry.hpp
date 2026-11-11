#pragma once

#include "asset-registry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ui {
    using FontFamilyId = std::string;

    enum class FontWeight : int {
        Thin = 100,
        ExtraLight = 200,
        Light = 300,
        Regular = 400,
        Medium = 500,
        Semibold = 600,
        Bold = 700,
        ExtraBold = 800,
        Black = 900,
    };

    enum class FontSlant {
        Normal,
        Italic,
        Oblique,
    };

    enum class FontScript {
        Latin,
        Greek,
        Cyrillic,
        Hebrew,
        Arabic,
        Devanagari,
        Thai,
        Han,
        Hiragana,
        Katakana,
        Hangul,
    };

    struct FontUnicodeRange {
        char32_t first;
        char32_t last;

        bool operator==(const FontUnicodeRange&) const = default;
    };

    struct FontCoverage {
        /// sorted, non-overlapping unicode ranges
        std::vector<FontUnicodeRange> ranges;

        bool supports(char32_t character) const;
        bool covers(std::u32string_view characters) const;
        bool supports(FontScript script) const;
    };

    struct FontFamily {
        FontFamilyId id;
        std::string name;
        FontCoverage coverage;
        bool monospace = false;
    };

    struct FontQuery {
        std::vector<FontScript> supports_any;
        std::u32string characters;
        bool monospace_only = false;
    };

    enum class FontSelectionKind {
        /// the configured system default
        System,
        /// as fallback, choose one complementary installed face
        Automatic,
        /// the family id stored in FontSelection
        Family,
        /// no font, valid only as a fallback
        None,
    };

    struct FontSelection {
        FontSelectionKind kind = FontSelectionKind::System;

        /// only used when kind is Family
        FontFamilyId family_id;

        static FontSelection system() {
            return {};
        }
        static FontSelection automatic() {
            return {FontSelectionKind::Automatic, {}};
        }
        static FontSelection family(FontFamilyId id);
        static FontSelection none() {
            return {FontSelectionKind::None, {}};
        }
    };

    struct FontRequest {
        FontSelection main = FontSelection::system();
        FontSelection fallback = FontSelection::automatic();
        FontWeight weight = FontWeight::Regular;
        FontSlant slant = FontSlant::Normal;
        std::string language; /// locale used to rank system fonts, such as "ja"
    };

    /// a local, loadable face, including its index inside a font collection
    struct FontSource {
        std::filesystem::path location;
        std::uint32_t index = 0;
        bool operator==(const FontSource&) const = default;
    };

    /// an installed face with its family metadata and coverage
    struct FontFace {
        FontFamilyId family_id;
        std::string family_name;
        FontSource source;
        FontCoverage coverage;
        FontWeight weight = FontWeight::Regular;
        FontSlant slant = FontSlant::Normal;
        bool monospace = false;
    };

    /// loaded imgui fonts for one weight/slant pair, cached per context
    struct FontVariant {
        std::vector<FontSource> sources;
        std::unordered_map<ImGuiContext*, ImFont*> contexts;
    };

    struct FontFileData {
        void* data;
        int size;
    };

    class FontRegistry;

    class Font final {
    public:
        Font(std::filesystem::path location, ImFontConfig config);
        explicit Font(std::vector<FontSource> sources);
        Font(FontRegistry& registry, FontRequest request, std::vector<FontSource> sources);
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;
        Font(Font&&) = default;
        Font& operator=(Font&&) = default;

        /// loads the requested weight/slant variant once per imgui context
        /// managed fonts fill unset attributes from the registered defaults, sizes are selected when pushing
        ImFont* get(std::optional<FontWeight> weight = {}, std::optional<FontSlant> slant = {});

        void release_context(ImGuiContext* context);

    private:
        // registry updates preserve the Font address used by inherited styles
        friend class FontRegistry;
        void unload();
        void replace(Font next);

        std::vector<FontSource> m_sources;
        std::map<std::pair<std::optional<FontWeight>, std::optional<FontSlant>>, FontVariant> m_variants;
        std::unordered_map<ImGuiContext*, std::map<std::filesystem::path, FontFileData>> m_files;
        ImFontConfig m_config;
        FontRegistry* m_registry = nullptr;
        FontRequest m_request;
    };

    /// a managed family, or a context-local imgui font for native interoperability
    class FontReference {
    public:
        FontReference() = default;
        explicit(false) FontReference(std::nullptr_t) {}
        explicit(false) FontReference(Font* font) : m_font(font) {}
        explicit(false) FontReference(ImFont* font) : m_native_font(font) {}

        explicit operator bool() const {
            return m_font != nullptr || m_native_font != nullptr;
        }

        bool operator==(const FontReference&) const = default;
        ImFont* resolve(std::optional<FontWeight> weight = {}, std::optional<FontSlant> slant = {}) const;

    private:
        Font* m_font = nullptr;
        ImFont* m_native_font = nullptr;
    };

    class FontRegistry final : public AssetRegistry<Font> {
    public:
        Font* add(std::string id, std::filesystem::path location);
        Font* add(std::string id, std::filesystem::path location, ImFontConfig config);

        /// returns nullptr if no system face is available.
        Font* add(std::string id, const FontRequest& request);
        /// replaces a family without changing its Font reference, call between frames and reapply the theme
        Font* set(std::string id, const FontRequest& request);

        /// rescans installed families without changing registered fonts
        void refresh();

        /// lazily discovers installed fonts when refresh has not been called
        std::vector<FontFamily> families(const FontQuery& query = {});

        /// returns main and, when available, one complementary fallback face
        /// unavailable families use the system selection instead
        std::vector<FontSource> resolve(const FontRequest& request);

    private:
        void ensure_loaded() {
            if (!m_loaded) {
                refresh();
            }
        }

        void build_families();

        std::vector<FontFace> m_faces;
        std::vector<FontFamily> m_families;
        bool m_loaded = false;
    };
} // namespace ui
