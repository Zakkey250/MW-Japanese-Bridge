#include "BridgeLogic.h"

#include <algorithm>
#include <cctype>
#include <cwctype>

namespace nfsmw_japanese_bridge
{
namespace
{

template <typename Char>
Char Lower(const Char value) noexcept;

template <>
char Lower(const char value) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

template <>
wchar_t Lower(const wchar_t value) noexcept
{
    return static_cast<wchar_t>(std::towlower(value));
}

template <typename Char>
bool EqualsIgnoreCaseImpl(
    const std::basic_string_view<Char> left, const std::basic_string_view<Char> right) noexcept
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](const Char a, const Char b) {
               return Lower(a) == Lower(b);
           });
}

template <typename Char>
bool EndsWithIgnoreCase(
    const std::basic_string_view<Char> value, const std::basic_string_view<Char> suffix) noexcept
{
    return value.size() >= suffix.size() &&
           EqualsIgnoreCaseImpl(value.substr(value.size() - suffix.size()), suffix);
}

template <typename Char>
std::basic_string<Char> ReplaceSuffix(
    const std::basic_string_view<Char> value,
    const std::basic_string_view<Char> suffix,
    const std::basic_string_view<Char> replacement)
{
    if (!EndsWithIgnoreCase(value, suffix))
    {
        return {};
    }
    std::basic_string<Char> result(value.substr(0, value.size() - suffix.size()));
    result.append(replacement);
    return result;
}

template <typename Char>
std::basic_string<Char> RegistryReplacementImpl(
    const std::basic_string_view<Char> value_name, const std::basic_string_view<Char> current_value)
{
    const auto language = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"Language");
        }
        else
        {
            return std::basic_string_view<Char>("Language");
        }
    }();
    const auto locale = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"Locale");
        }
        else
        {
            return std::basic_string_view<Char>("Locale");
        }
    }();
    const auto japanese = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"Japanese");
        }
        else
        {
            return std::basic_string_view<Char>("Japanese");
        }
    }();
    const auto ja = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"ja");
        }
        else
        {
            return std::basic_string_view<Char>("ja");
        }
    }();
    if (EqualsIgnoreCaseImpl(value_name, language) && EqualsIgnoreCaseImpl(current_value, japanese))
    {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string<Char>(L"English US");
        }
        else
        {
            return std::basic_string<Char>("English US");
        }
    }
    if (EqualsIgnoreCaseImpl(value_name, locale) && EqualsIgnoreCaseImpl(current_value, ja))
    {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string<Char>(L"en-us");
        }
        else
        {
            return std::basic_string<Char>("en-us");
        }
    }
    return {};
}

template <typename Char>
std::basic_string<Char> RedirectPathImpl(
    const std::basic_string_view<Char> path, const bool redirect_movies)
{
    const auto english_bin = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"languages\\english.bin");
        }
        else
        {
            return std::basic_string_view<Char>("languages\\english.bin");
        }
    }();
    const auto english_filename = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"english.bin");
        }
        else
        {
            return std::basic_string_view<Char>("english.bin");
        }
    }();
    const auto japanese_filename = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"Japanese.bin");
        }
        else
        {
            return std::basic_string_view<Char>("Japanese.bin");
        }
    }();
    std::basic_string<Char> result;
    if (EndsWithIgnoreCase(path, english_bin))
    {
        result.assign(path.substr(0, path.size() - english_filename.size()));
        result.append(japanese_filename);
    }
    if (!result.empty() || !redirect_movies)
    {
        return result;
    }
    const auto english_pal_movie = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"_english_pal.vp6");
        }
        else
        {
            return std::basic_string_view<Char>("_english_pal.vp6");
        }
    }();
    const auto english_ntsc_movie = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"_english_ntsc.vp6");
        }
        else
        {
            return std::basic_string_view<Char>("_english_ntsc.vp6");
        }
    }();
    const auto japanese_pal_movie = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"_japanese_pal.vp6");
        }
        else
        {
            return std::basic_string_view<Char>("_japanese_pal.vp6");
        }
    }();
    const auto japanese_movie = [] {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
        {
            return std::basic_string_view<Char>(L"_japanese_ntsc.vp6");
        }
        else
        {
            return std::basic_string_view<Char>("_japanese_ntsc.vp6");
        }
    }();
    auto movie = ReplaceSuffix(path, english_pal_movie, japanese_movie);
    if (movie.empty())
    {
        movie = ReplaceSuffix(path, english_ntsc_movie, japanese_movie);
    }
    return movie.empty() ? ReplaceSuffix(path, japanese_pal_movie, japanese_movie) : movie;
}

}

bool EqualsIgnoreCase(const std::string_view left, const std::string_view right) noexcept
{
    return EqualsIgnoreCaseImpl(left, right);
}

bool EqualsIgnoreCase(const std::wstring_view left, const std::wstring_view right) noexcept
{
    return EqualsIgnoreCaseImpl(left, right);
}

std::string RegistryReplacement(const std::string_view value_name, const std::string_view current_value)
{
    return RegistryReplacementImpl(value_name, current_value);
}

std::wstring RegistryReplacement(const std::wstring_view value_name, const std::wstring_view current_value)
{
    return RegistryReplacementImpl(value_name, current_value);
}

std::string RedirectPath(const std::string_view path, const bool redirect_movies)
{
    return RedirectPathImpl(path, redirect_movies);
}

std::wstring RedirectPath(const std::wstring_view path, const bool redirect_movies)
{
    return RedirectPathImpl(path, redirect_movies);
}

bool ShouldHideLanguageFile(const std::string_view filename) noexcept
{
    return EqualsIgnoreCase(filename, "Japanese.bin") || EqualsIgnoreCase(filename, "Thai.bin");
}

}
