#include <imgui-ui/resources/font-registry.hpp>
#include "fonts/platform.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

using namespace ui;

FontFamilyId ui::font_family_id(std::string_view name) {
    std::string id(name);
    for (char& character : id) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    }
    return id;
}

bool FontCoverage::supports(char32_t character) const {
    const auto range = std::ranges::upper_bound(ranges, character, {}, &FontUnicodeRange::first);
    return range != ranges.begin() && character <= std::prev(range)->last;
}

bool FontCoverage::covers(std::u32string_view characters) const {
    return std::ranges::all_of(characters, [this](char32_t character) { return supports(character); });
}

bool FontCoverage::supports(FontScript script) const {
    // one sample per FontScript value, in enum order
    static constexpr std::array<std::u32string_view, 11> samples{
        U"Azaz", U"ΑΩαω", U"АЯая", U"אבשת", U"ابتع", U"अकमन", U"กขคน", U"一中文日", U"あいうえお", U"アイウエオ", U"가나다한",
    };

    const auto index = static_cast<std::size_t>(script);
    return index < samples.size() && covers(samples[index]);
}

FontSelection FontSelection::family(FontFamilyId id) {
    if (id.empty()) {
        throw std::invalid_argument("font family id must not be empty");
    }

    return {FontSelectionKind::Family, std::move(id)};
}

static void merge_coverage(FontCoverage& target, const FontCoverage& source) {
    std::vector<FontUnicodeRange> ranges;
    ranges.reserve(target.ranges.size() + source.ranges.size());
    std::ranges::merge(
        target.ranges, source.ranges, std::back_inserter(ranges), {}, &FontUnicodeRange::first, &FontUnicodeRange::first
    );

    std::size_t count = 0;
    for (const auto range : ranges) {
        if (count != 0 && range.first <= ranges[count - 1].last + 1) {
            ranges[count - 1].last = std::max(ranges[count - 1].last, range.last);
        } else {
            ranges[count++] = range;
        }
    }

    ranges.resize(count);
    target.ranges = std::move(ranges);
}

struct FontStream {
    std::ifstream file;
    FT_StreamRec stream{};
};

// the stream must outlive the face, which reads font tables without copying the whole file
static FT_Face load_face(FT_Library library, const FontSource& source, FontStream& input) {
    // the high bits encode a named instance, so the combined index must fit FT_Long
    if (std::cmp_greater(source.index, std::numeric_limits<FT_Long>::max())) {
        return nullptr;
    }

    // reading through filesystem preserves unicode paths on windows
    input.file.open(source.location, std::ios::binary | std::ios::ate);
    const auto length = input.file.tellg();

    // reject empty files and sizes that would not fit FT_Long on 32-bit builds
    if (length <= 0 || length > INT_MAX) {
        return nullptr;
    }

    input.stream.size = static_cast<unsigned long>(length);
    input.stream.descriptor.pointer = &input.file;
    input.stream.read = [](FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count) {
        auto& file = *static_cast<std::ifstream*>(stream->descriptor.pointer);
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset));
        if (count == 0) return file ? 0UL : 1UL;
        file.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(count));
        return static_cast<unsigned long>(file.gcount());
    };

    FT_Open_Args args{};
    args.flags = FT_OPEN_STREAM;
    args.stream = &input.stream;
    FT_Face raw_face = nullptr;
    if (FT_Open_Face(library, &args, static_cast<FT_Long>(source.index), &raw_face) != 0) {
        return nullptr;
    }

    return raw_face;
}

static void read_coverage(FT_Library library, FontFace& font) {
    font.coverage.ranges.clear();

    FontStream input;
    const std::unique_ptr<FT_FaceRec, decltype(&FT_Done_Face)> face(load_face(library, font.source, input), FT_Done_Face);
    if (!face || FT_Select_Charmap(face.get(), FT_ENCODING_UNICODE) != 0) {
        return;
    }

    FT_UInt glyph = 0;
    for (FT_ULong character = FT_Get_First_Char(face.get(), &glyph); glyph != 0;
         character = FT_Get_Next_Char(face.get(), character, &glyph)) {
        // ignore code points outside the unicode range
        if (character > 0x10ffff) continue;
        const auto value = static_cast<char32_t>(character);
        if (!font.coverage.ranges.empty() && font.coverage.ranges.back().last + 1 == value) {
            font.coverage.ranges.back().last = value;
        } else {
            font.coverage.ranges.push_back({value, value});
        }
    }
}

static int slant_distance(FontSlant current, FontSlant requested) {
    if (current == requested) return 0;
    // italic and oblique are closer to each other than to upright
    if (current != FontSlant::Normal && requested != FontSlant::Normal) return 1000;
    return 2000;
}

void ui::resolve_font_variants(
    std::vector<FontSource>& sources, std::optional<FontWeight> weight, std::optional<FontSlant> slant
) {
    FT_Library raw_library = nullptr;
    if (FT_Init_FreeType(&raw_library) != 0) throw std::runtime_error("cannot initialize freetype for font variants");

    const std::unique_ptr<FT_LibraryRec_, decltype(&FT_Done_FreeType)> library(raw_library, FT_Done_FreeType);
    for (auto& source : sources) {
        FontStream input;
        const std::unique_ptr<FT_FaceRec, decltype(&FT_Done_Face)> face(load_face(library.get(), source, input), FT_Done_Face);
        if (!face || !FT_HAS_MULTIPLE_MASTERS(face.get())) continue;

        FT_MM_Var* raw_variation = nullptr;
        if (FT_Get_MM_Var(face.get(), &raw_variation) != 0) continue;

        const auto release = [&library](FT_MM_Var* value) { FT_Done_MM_Var(library.get(), value); };
        const std::unique_ptr<FT_MM_Var, decltype(release)> variation(raw_variation, release);
        std::vector<FT_Fixed> coordinates(variation->num_axis);
        if (FT_Get_Var_Design_Coordinates(face.get(), variation->num_axis, coordinates.data()) != 0) continue;

        // read the weight and slant encoded by the given variation coordinates
        const auto read_style = [&](const FT_Fixed* values) {
            float selected_weight = static_cast<float>(FontWeight::Regular);
            FontSlant selected_slant = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0 ? FontSlant::Italic : FontSlant::Normal;
            bool oblique = false;

            for (FT_UInt axis = 0; axis < variation->num_axis; ++axis) {
                const auto tag = variation->axis[axis].tag;
                if (tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                    // variation coordinates are 16.16 fixed point
                    selected_weight = static_cast<float>(values[axis]) / 65536.0f;
                }

                if (tag == FT_MAKE_TAG('i', 't', 'a', 'l')) {
                    selected_slant = values[axis] != 0 ? FontSlant::Italic : FontSlant::Normal;
                }

                if (tag == FT_MAKE_TAG('s', 'l', 'n', 't')) {
                    oblique = values[axis] != 0;
                }
            }

            // slnt is more specific than ital and wins when both are present
            if (oblique) selected_slant = FontSlant::Oblique;

            return std::pair{selected_weight, selected_slant};
        };

        const auto base = read_style(coordinates.data());
        const float target_weight = weight ? static_cast<float>(*weight) : base.first;
        const FontSlant target_slant = slant.value_or(base.second);
        const auto cost = [&](const auto& current) {
            return std::abs(current.first - target_weight) + static_cast<float>(slant_distance(current.second, target_slant));
        };

        float best = cost(base);
        // the face index stores the named instance in bits 16-30, so 15 bits are available
        const auto instance_count = std::min(variation->num_namedstyles, FT_UInt{0x7fff});
        for (FT_UInt instance = 0; instance < instance_count; ++instance) {
            const float current = cost(read_style(variation->namedstyle[instance].coords));
            if (current >= best) continue;

            best = current;
            // keep the face index in the low 16 bits and encode the named instance above it
            source.index = (source.index & 0xffffU) | ((instance + 1U) << 16U);
        }
    }
}

void FontRegistry::refresh() {
    auto faces = enumerate_system_fonts();
    // family id first for grouping, then a stable order for the remaining fields
    std::ranges::sort(faces, [](const auto& left, const auto& right) {
        return std::tie(left.family_id, left.source.location, left.source.index, left.weight, left.slant) <
               std::tie(right.family_id, right.source.location, right.source.index, right.weight, right.slant);
    });

    FT_Library raw_library = nullptr;
    if (FT_Init_FreeType(&raw_library) != 0) {
        throw std::runtime_error("cannot initialize freetype for font discovery");
    }

    const std::unique_ptr<FT_LibraryRec_, decltype(&FT_Done_FreeType)> library(raw_library, FT_Done_FreeType);

    for (auto& face : faces) {
        if (face.coverage.ranges.empty()) read_coverage(library.get(), face);
    }

    m_faces = std::move(faces);
    build_families();
    m_loaded = true;
}

void FontRegistry::build_families() {
    m_families.clear();
    // refresh sorts faces by family id, so one pass groups every style of a family
    for (const auto& face : m_faces) {
        if (face.coverage.ranges.empty()) {
            continue;
        }

        if (m_families.empty() || m_families.back().id != face.family_id) {
            m_families.push_back({face.family_id, face.family_name, FontCoverage(), face.monospace});
        }

        m_families.back().monospace &= face.monospace;
        merge_coverage(m_families.back().coverage, face.coverage);
    }
}

std::vector<FontFamily> FontRegistry::families(const FontQuery& query) {
    ensure_loaded();

    std::vector<FontFamily> result;
    for (const auto& family : m_families) {
        if (query.monospace_only && !family.monospace) {
            continue;
        }

        if (!family.coverage.covers(query.characters)) {
            continue;
        }

        if (!query.supports_any.empty() &&
            !std::ranges::any_of(query.supports_any, [&](FontScript script) { return family.coverage.supports(script); })) {
            continue;
        }

        result.push_back(family);
    }

    return result;
}

static std::size_t uncovered_characters(const FontCoverage& candidate, const FontCoverage& covered) {
    std::size_t count = 0;
    std::size_t position = 0;

    // both inputs are sorted, so the covered cursor only moves forward
    for (const auto range : candidate.ranges) {
        count += range.last - range.first + 1;
        while (position < covered.ranges.size() && covered.ranges[position].last < range.first) {
            ++position;
        }
        for (auto index = position; index < covered.ranges.size() && covered.ranges[index].first <= range.last; ++index) {
            count -= std::min(range.last, covered.ranges[index].last) - std::max(range.first, covered.ranges[index].first) + 1;
        }
    }

    return count;
}

std::vector<FontSource> FontRegistry::resolve(const FontRequest& request) {
    if (request.main.kind == FontSelectionKind::None) throw std::invalid_argument("main font cannot be none");
    ensure_loaded();

    std::map<FontFamilyId, const FontFace*> candidates;
    const auto distance = [&](const FontFace& face) {
        // slant mismatches outweigh the full weight range
        return std::abs(static_cast<int>(face.weight) - static_cast<int>(request.weight)) +
               slant_distance(face.slant, request.slant);
    };

    for (const auto& face : m_faces) {
        if (face.coverage.ranges.empty()) continue;
        auto& best = candidates[face.family_id];
        if (best == nullptr || distance(face) < distance(*best)) best = &face;
    }

    const auto preferences = system_font_preferences(request.language);
    const auto find = [&](std::string_view name) -> const FontFace* {
        const auto found = candidates.find(font_family_id(name));
        return found != candidates.end() ? found->second : nullptr;
    };

    const FontFace* main = request.main.kind == FontSelectionKind::Family ? find(request.main.family_id) : nullptr;
    if (main == nullptr) {
        for (const auto& name : preferences) {
            main = find(name);
            if (main != nullptr) break;
        }
    }

    // no system preference is available, so use any loadable family
    if (main == nullptr && !candidates.empty()) {
        main = candidates.begin()->second;
    }

    if (main == nullptr) {
        return {};
    }

    std::vector<FontSource> sources{main->source};
    if (request.fallback.kind == FontSelectionKind::None) {
        return sources;
    }

    const auto& covered = main->coverage;
    const auto append = [&](const FontFace* face, std::size_t added) {
        if (added == 0) return;
        sources.push_back(face->source);
    };

    const auto* fallback = request.fallback.kind == FontSelectionKind::Family ? find(request.fallback.family_id) : nullptr;
    if (request.fallback.kind == FontSelectionKind::System && !preferences.empty()) {
        fallback = find(preferences.front());
    }

    // explicit and automatic selections both contribute one complementary face
    if (fallback != nullptr) {
        append(fallback, uncovered_characters(fallback->coverage, covered));
        return sources;
    }

    const FontFace* best = nullptr;
    std::size_t most = 0;
    const auto consider = [&](const FontFace* face) {
        if (face == nullptr) return;
        const auto count = uncovered_characters(face->coverage, covered);
        if (count > most) {
            best = face;
            most = count;
        }
    };

    for (const auto& name : preferences) {
        consider(find(name));
    }

    for (const auto& entry : candidates) {
        consider(entry.second);
    }

    if (best != nullptr) append(best, most);

    return sources;
}
