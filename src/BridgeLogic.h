#pragma once

#include <string>
#include <string_view>

namespace nfsmw_japanese_bridge
{

bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept;
bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept;

std::string RegistryReplacement(std::string_view value_name, std::string_view current_value);
std::wstring RegistryReplacement(std::wstring_view value_name, std::wstring_view current_value);

std::string RedirectPath(std::string_view path, bool redirect_movies);
std::wstring RedirectPath(std::wstring_view path, bool redirect_movies);
bool ShouldHideLanguageFile(std::string_view filename) noexcept;

}
