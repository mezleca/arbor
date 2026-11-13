#include "platform.hpp"

#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <bit>
#include <memory>
#include <stdexcept>

using namespace ui;

static auto font_config() {
    std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config(FcInitLoadConfigAndFonts(), FcConfigDestroy);
    if (!config) throw std::runtime_error("cannot initialize fontconfig");
    return config;
}

static std::string family_name(FcPattern* pattern) {
    FcChar8* family = nullptr;
    if (FcPatternGetString(pattern, FC_FAMILY, 0, &family) != FcResultMatch) return {};
    return reinterpret_cast<const char*>(family);
}

std::vector<FontFace> ui::enumerate_system_fonts() {
    auto config = font_config();

    const std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> pattern(FcPatternCreate(), FcPatternDestroy);
    // bitmap-only faces cannot be scaled to arbitrary ui sizes
    FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);

    const std::unique_ptr<FcObjectSet, decltype(&FcObjectSetDestroy)> objects(
        FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX, FC_WEIGHT, FC_SLANT, FC_SPACING, FC_CHARSET, nullptr), FcObjectSetDestroy
    );

    const std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)> fonts(
        FcFontList(config.get(), pattern.get(), objects.get()), FcFontSetDestroy
    );

    if (!fonts) {
        throw std::runtime_error("cannot enumerate fontconfig fonts");
    }

    std::vector<FontFace> result;
    result.reserve(static_cast<std::size_t>(fonts->nfont));
    for (int index = 0; index < fonts->nfont; ++index) {
        auto* font = fonts->fonts[index];
        FcChar8* file = nullptr;
        FontFace face;

        face.family_name = family_name(font);
        if (face.family_name.empty() || FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch) {
            continue;
        }

        face.family_id = font_family_id(face.family_name);
        face.source.location = reinterpret_cast<const char*>(file);

        int value = 0;
        const auto read = [font](const char* key, int& value) {
            return FcPatternGetInteger(font, key, 0, &value) == FcResultMatch;
        };

        if (read(FC_INDEX, value)) {
            face.source.index = static_cast<std::uint32_t>(value);
        }

        if (read(FC_WEIGHT, value)) {
            // fontconfig reports weights on its own scale, mapped into the OpenType 1..1000 range
            face.weight = static_cast<FontWeight>(std::clamp(FcWeightToOpenType(value), 1, 1000));
        }

        if (read(FC_SLANT, value)) {
            if (value == FC_SLANT_ITALIC) {
                face.slant = FontSlant::Italic;
            } else if (value == FC_SLANT_OBLIQUE) {
                face.slant = FontSlant::Oblique;
            }
        }

        if (read(FC_SPACING, value)) {
            // charcell covers fixed-pitch terminal faces
            face.monospace = value == FC_MONO || value == FC_CHARCELL;
        }

        FcCharSet* charset = nullptr;
        if (FcPatternGetCharSet(font, FC_CHARSET, 0, &charset) == FcResultMatch) {
            FcChar32 map[FC_CHARSET_MAP_SIZE];
            FcChar32 next = 0;
            for (auto page = FcCharSetFirstPage(charset, map, &next); page != FC_CHARSET_DONE;
                 page = FcCharSetNextPage(charset, map, &next)) {
                for (FcChar32 word = 0; word < FC_CHARSET_MAP_SIZE; ++word) {
                    auto bits = map[word];
                    while (bits != 0) {
                        const auto first_bit = std::countr_zero(bits);
                        const auto end_bit = first_bit + std::countr_one(bits >> first_bit);
                        const auto first = static_cast<char32_t>(page + (word * 32U) + first_bit);
                        const auto last = static_cast<char32_t>(page + (word * 32U) + end_bit - 1);
                        if (!face.coverage.ranges.empty() && face.coverage.ranges.back().last + 1 == first) {
                            face.coverage.ranges.back().last = last;
                        } else {
                            face.coverage.ranges.push_back({first, last});
                        }
                        bits = end_bit < 32 ? bits & (~FcChar32{0} << end_bit) : 0;
                    }
                }
            }
        }

        result.push_back(std::move(face));
    }

    return result;
}

std::vector<std::string> ui::system_font_preferences(std::string_view language) {
    auto config = font_config();

    const std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> pattern(FcPatternCreate(), FcPatternDestroy);
    // resolve the configured sans-serif through the user's fontconfig rules
    FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>("sans-serif"));
    const std::string locale(language);

    if (!locale.empty()) {
        FcPatternAddString(pattern.get(), FC_LANG, reinterpret_cast<const FcChar8*>(locale.c_str()));
    }

    FcConfigSubstitute(config.get(), pattern.get(), FcMatchPattern);
    // newer fontconfig defaults must use our owned config instead of initializing a second global one.
#if FC_VERSION >= 21700
    FcConfigSetDefaultSubstitute(config.get(), pattern.get());
#else
    FcDefaultSubstitute(pattern.get());
#endif

    FcResult status;
    // trim drops fonts that do not match the pattern
    const std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)> fonts(
        FcFontSort(config.get(), pattern.get(), FcTrue, nullptr, &status), FcFontSetDestroy
    );

    std::vector<std::string> result;
    if (fonts) {
        for (int index = 0; index < fonts->nfont; ++index) {
            result.push_back(family_name(fonts->fonts[index]));
        }
    }

    return result;
}
