#include "platform.hpp"

#include <dwrite_2.h>
#include <wrl/client.h>
#include <stdexcept>

using namespace ui;
using Microsoft::WRL::ComPtr;

static std::string utf8(std::wstring_view value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

static UINT32 remaining_length(std::wstring_view text, UINT32 position) {
    return position < text.size() ? static_cast<UINT32>(text.size() - position) : 0;
}

static std::string family_name(IDWriteFontFamily* family) {
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(family->GetFamilyNames(&names))) {
        return {};
    }

    // prefer the English name so ids stay stable across ui languages
    UINT32 index = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &index, &exists);
    if (!exists) {
        index = 0;
    }

    UINT32 size = 0;
    if (FAILED(names->GetStringLength(index, &size))) {
        return {};
    }

    std::wstring name(size + 1, L'\0');
    if (FAILED(names->GetString(index, name.data(), size + 1))) {
        return {};
    }

    name.resize(size);
    return utf8(name);
}

static ComPtr<IDWriteFactory> font_factory() {
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf())
        ))) {
        throw std::runtime_error("cannot initialize directwrite");
    }

    return factory;
}

std::vector<FontFace> ui::enumerate_system_fonts() {
    auto factory = font_factory();
    ComPtr<IDWriteFontCollection> fonts;

    if (FAILED(factory->GetSystemFontCollection(&fonts, TRUE))) {
        throw std::runtime_error("cannot enumerate directwrite fonts");
    }

    std::vector<FontFace> result;
    const auto family_count = fonts->GetFontFamilyCount();
    result.reserve(family_count);
    for (UINT32 index = 0; index < family_count; ++index) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(fonts->GetFontFamily(index, &family))) {
            continue;
        }

        const auto name = family_name(family.Get());
        if (name.empty()) {
            continue;
        }

        const auto style_count = family->GetFontCount();
        for (UINT32 style = 0; style < style_count; ++style) {
            ComPtr<IDWriteFont> font;
            ComPtr<IDWriteFontFace> face;
            // simulated styles reuse another face's file, so they would duplicate the family
            if (FAILED(family->GetFont(style, &font)) || font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE ||
                FAILED(font->CreateFontFace(&face))) {
                continue;
            }

            // a face split across files cannot be addressed by one path
            UINT32 count = 0;
            if (FAILED(face->GetFiles(&count, nullptr)) || count != 1) {
                continue;
            }

            ComPtr<IDWriteFontFile> file;
            ComPtr<IDWriteFontFileLoader> loader;
            ComPtr<IDWriteLocalFontFileLoader> local;
            if (FAILED(face->GetFiles(&count, &file)) || FAILED(file->GetLoader(&loader)) || FAILED(loader.As(&local))) {
                continue;
            }

            const void* key = nullptr;
            UINT32 key_size = 0;
            UINT32 path_size = 0;

            if (FAILED(file->GetReferenceKey(&key, &key_size)) ||
                FAILED(local->GetFilePathLengthFromKey(key, key_size, &path_size))) {
                continue;
            }

            std::wstring path(path_size + 1, L'\0');
            if (FAILED(local->GetFilePathFromKey(key, key_size, path.data(), path_size + 1))) {
                continue;
            }

            path.resize(path_size);

            FontFace entry;
            entry.family_id = font_family_id(name);
            entry.family_name = name;
            entry.source = {std::move(path), face->GetIndex()};
            entry.weight = static_cast<FontWeight>(font->GetWeight());

            const auto slant = font->GetStyle();
            if (slant == DWRITE_FONT_STYLE_ITALIC) {
                entry.slant = FontSlant::Italic;
            } else if (slant == DWRITE_FONT_STYLE_OBLIQUE) {
                entry.slant = FontSlant::Oblique;
            }

            ComPtr<IDWriteFont1> font1;
            if (SUCCEEDED(font.As(&font1))) {
                entry.monospace = font1->IsMonospacedFont();

                UINT32 range_count = 0;
                font1->GetUnicodeRanges(0, nullptr, &range_count);
                std::vector<DWRITE_UNICODE_RANGE> ranges(range_count);
                if (SUCCEEDED(font1->GetUnicodeRanges(range_count, ranges.data(), &range_count))) {
                    for (const auto& range : ranges) {
                        entry.coverage.ranges.push_back({static_cast<char32_t>(range.first), static_cast<char32_t>(range.last)});
                    }
                }
            }

            result.push_back(std::move(entry));
        }
    }

    return result;
}

class FontFallbackText : public IDWriteTextAnalysisSource {
public:
    explicit FontFallbackText(std::string_view language) {
        GetUserDefaultLocaleName(m_locale, LOCALE_NAME_MAX_LENGTH);
        if (!language.empty()) {
            // leave room for the terminator that MultiByteToWideChar does not write
            const auto size = MultiByteToWideChar(
                CP_UTF8, 0, language.data(), static_cast<int>(language.size()), m_locale, LOCALE_NAME_MAX_LENGTH - 1
            );

            if (size > 0) {
                m_locale[size] = L'\0';
            }
        }
    }

    FontFallbackText(const FontFallbackText&) = delete;
    FontFallbackText& operator=(const FontFallbackText&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        *object = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(IDWriteTextAnalysisSource)) return E_NOINTERFACE;
        *object = static_cast<IDWriteTextAnalysisSource*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_references);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = InterlockedDecrement(&m_references);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text, UINT32* length) override {
        *length = remaining_length(m_text, position);
        *text = *length != 0 ? m_text.data() + position : nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text, UINT32* length) override {
        *length = position <= m_text.size() ? position : 0;
        *text = *length != 0 ? m_text.data() : nullptr;
        return S_OK;
    }

    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }

    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length, const WCHAR** locale) override {
        *length = remaining_length(m_text, position);
        *locale = m_locale;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    GetNumberSubstitution(UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) override {
        *length = remaining_length(m_text, position);
        *substitution = nullptr;
        return S_OK;
    }

    // one or more characters per script, probed through the system fallback
    static constexpr std::wstring_view text() {
        return L"中あア한ΩЯאعअก★";
    }

private:
    std::wstring_view m_text = text();
    wchar_t m_locale[LOCALE_NAME_MAX_LENGTH]{};
    LONG m_references = 1;
};

std::vector<std::string> ui::system_font_preferences(std::string_view language) {
    std::vector<std::string> result;
    NONCLIENTMETRICSW metrics{};

    metrics.cbSize = sizeof(metrics);
    // start with the shell's configured message font
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        result.push_back(utf8(metrics.lfMessageFont.lfFaceName));
    }

    auto factory = font_factory();

    ComPtr<IDWriteFactory2> factory2;
    ComPtr<IDWriteFontFallback> fallback;
    // IDWriteFactory2 exists from Windows 8.1; without it the message font is the only preference
    if (FAILED(factory.As(&factory2)) || FAILED(factory2->GetSystemFontFallback(&fallback))) {
        return result;
    }

    ComPtr<IDWriteTextAnalysisSource> source;
    source.Attach(new FontFallbackText(language));

    const auto text = FontFallbackText::text();
    // map one run per call and advance by the number of characters resolved
    for (UINT32 position = 0; position < text.size();) {
        UINT32 mapped = 0;
        FLOAT scale = 1.0f;
        ComPtr<IDWriteFont> font;
        if (FAILED(fallback->MapCharacters(
                source.Get(), position, static_cast<UINT32>(text.size() - position), nullptr, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &mapped, &font, &scale
            )) ||
            mapped == 0) {
            break;
        }

        ComPtr<IDWriteFontFamily> family;
        if (font && SUCCEEDED(font->GetFontFamily(&family))) {
            result.push_back(family_name(family.Get()));
        }

        position += mapped;
    }

    return result;
}
