#include <Windows.h>
#include <winver.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "MinHook.h"

#pragma warning(disable : 4127 4191)

extern "C" FARPROC g_version_by_handle = nullptr;

namespace
{
constexpr DWORD kSupportedTimestamp = 0x438E4C8Cu;
constexpr DWORD kSupportedImageSize = 0x00680000u;
constexpr DWORD kSupportedEntryPoint = 0x0067F06Eu;
constexpr DWORD kRetailTimestamp = 0x4366ECEBu;
constexpr DWORD kRetailImageSize = 0x0067F000u;
constexpr DWORD kRetailEntryPoint = 0x0067E06Eu;
constexpr DWORD kUnpackedImageSize = 0x00660000u;
constexpr DWORD kUnpackedStockShaderImageSize = 0x0067A000u;
constexpr DWORD kUnpackedEntryPoint = 0x003C4040u;
constexpr std::uintptr_t kLanguageTableAddress = 0x008F40F8u;
constexpr std::uintptr_t kCurrentLanguageAddress = 0x008F41C0u;
constexpr char kExpectedEnglishName[] = "ENGLISH";
constexpr char kExpectedEnglishPath[] = "LANGUAGES\\ENGLISH.BIN";
char g_japanese_name[] = "JAPANESE";
char g_japanese_path[] = "LANGUAGES\\JAPANESE.BIN";

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using RegQueryValueExAFn = LSTATUS(WINAPI*)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
using RegQueryValueExWFn = LSTATUS(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

HMODULE g_module{};
HMODULE g_system_version{};
GetProcAddressFn g_real_get_proc{};
RegQueryValueExAFn g_reg_query_a{};
RegQueryValueExWFn g_reg_query_w{};
CreateFileAFn g_create_file_a{};
CreateFileWFn g_create_file_w{};
void** g_get_proc_slot{};
HANDLE g_log = INVALID_HANDLE_VALUE;
volatile LONG g_language_overrides{};
volatile LONG g_locale_overrides{};
volatile LONG g_install_path_overrides{};
volatile LONG g_resolution_overrides{};
volatile LONG g_registry_resolutions{};
volatile LONG g_inline_active{};
volatile LONG g_text_redirects{};
volatile LONG g_movie_redirects{};
volatile LONG g_japanese_text_opens{};
volatile LONG g_japanese_speech_opens{};
volatile LONG g_language_slot_installed{};
volatile LONG g_target_accepted{};
std::array<char, 32768> g_game_directory_a{};
std::array<wchar_t, 32768> g_game_directory_w{};
bool g_widescreen_fix_present{};
constexpr bool kForceEnglishCompatibilityLanguage = false;

void Log(const char* format, ...) noexcept;

struct LanguageEntry
{
    int id;
    const char* name;
    const char* path;
    void* state;
    const void* locale_format;
};

bool IsLanguageTableCompatibleImage() noexcept
{
    const auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr)
    {
        return false;
    }
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->FileHeader.TimeDateStamp != kSupportedTimestamp)
    {
        return false;
    }
    const bool protected_no_cd = nt->OptionalHeader.SizeOfImage == kSupportedImageSize &&
                                 nt->OptionalHeader.AddressOfEntryPoint == kSupportedEntryPoint;
    const bool unpacked_mod_target =
                                     (nt->OptionalHeader.SizeOfImage == kUnpackedImageSize ||
                                      nt->OptionalHeader.SizeOfImage == kUnpackedStockShaderImageSize ||
                                      nt->OptionalHeader.SizeOfImage == 0x00678E4Eu) &&
                                     nt->OptionalHeader.AddressOfEntryPoint == kUnpackedEntryPoint;
    return protected_no_cd || unpacked_mod_target;
}

bool IsUnpackedModTargetImage() noexcept
{
    const auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr)
    {
        return false;
    }
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->FileHeader.TimeDateStamp == kSupportedTimestamp &&
           (nt->OptionalHeader.SizeOfImage == kUnpackedImageSize ||
            nt->OptionalHeader.SizeOfImage == kUnpackedStockShaderImageSize ||
                                      nt->OptionalHeader.SizeOfImage == 0x00678E4Eu) &&
           nt->OptionalHeader.AddressOfEntryPoint == kUnpackedEntryPoint;
}

bool InstallJapaneseLanguageSlot() noexcept
{
    if (InterlockedCompareExchange(&g_language_slot_installed, 0, 0) != 0)
    {
        return true;
    }
    if (IsUnpackedModTargetImage())
    {
        // The unpacked 1.3 executable can use its native English slot safely.
        // CreateFile hooks redirect only English.bin to Japanese.bin, avoiding a
        // permanent language-table mutation that breaks this target's 3D layer.
        Log("LANGUAGE_SLOT_PATCH skipped=unpacked_file_redirect_mode");
        return false;
    }
    if (!IsLanguageTableCompatibleImage())
    {
        Log("LANGUAGE_SLOT_PATCH skipped=incompatible_image_layout");
        return false;
    }
    auto entry = reinterpret_cast<LanguageEntry*>(kLanguageTableAddress);
    __try
    {
        if (entry->id != 0 || entry->name == nullptr || entry->path == nullptr ||
            strcmp(entry->name, kExpectedEnglishName) != 0 ||
            strcmp(entry->path, kExpectedEnglishPath) != 0 ||
            *reinterpret_cast<volatile int*>(kCurrentLanguageAddress) != -1)
        {
            Log("LANGUAGE_SLOT_PATCH rejected=id_or_signature current=%d",
                *reinterpret_cast<volatile int*>(kCurrentLanguageAddress));
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("LANGUAGE_SLOT_PATCH rejected=unreadable_table");
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &previous))
    {
        Log("LANGUAGE_SLOT_PATCH rejected=virtual_protect error=%lu", GetLastError());
        return false;
    }
    entry->name = g_japanese_name;
    entry->path = g_japanese_path;
    DWORD ignored = 0;
    VirtualProtect(entry, sizeof(*entry), previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), entry, sizeof(*entry));
    InterlockedExchange(&g_language_slot_installed, 1);
    Log("LANGUAGE_SLOT_PATCH installed=id0 path=%s", g_japanese_path);
    return true;
}

template <typename Char>
Char LowerAscii(const Char value) noexcept
{
    return value >= static_cast<Char>('A') && value <= static_cast<Char>('Z')
               ? static_cast<Char>(value + static_cast<Char>('a' - 'A'))
               : value;
}

template <typename Char>
bool EndsWithIgnoreCase(const Char* value, const Char* suffix) noexcept
{
    if (value == nullptr || suffix == nullptr)
    {
        return false;
    }
    const std::size_t value_length = std::char_traits<Char>::length(value);
    const std::size_t suffix_length = std::char_traits<Char>::length(suffix);
    if (value_length < suffix_length)
    {
        return false;
    }
    const Char* tail = value + value_length - suffix_length;
    for (std::size_t index = 0; index < suffix_length; ++index)
    {
        if (LowerAscii(tail[index]) != LowerAscii(suffix[index]))
        {
            return false;
        }
    }
    return true;
}

template <typename Char>
bool IsJapaneseSpeechPath(const Char* path) noexcept
{
    if constexpr (sizeof(Char) == sizeof(wchar_t))
    {
        return EndsWithIgnoreCase(path, L"sound\\speech\\copspeech.big") ||
               EndsWithIgnoreCase(path, L"sound/speech/copspeech.big") ||
               EndsWithIgnoreCase(path, L"sound\\speech\\copspeech.csi") ||
               EndsWithIgnoreCase(path, L"sound/speech/copspeech.csi") ||
               EndsWithIgnoreCase(path, L"sound\\speech\\copspeech.evt") ||
               EndsWithIgnoreCase(path, L"sound/speech/copspeech.evt") ||
               EndsWithIgnoreCase(path, L"sound\\speech\\copspeech.idx") ||
               EndsWithIgnoreCase(path, L"sound/speech/copspeech.idx");
    }
    else
    {
        return EndsWithIgnoreCase(path, "sound\\speech\\copspeech.big") ||
               EndsWithIgnoreCase(path, "sound/speech/copspeech.big") ||
               EndsWithIgnoreCase(path, "sound\\speech\\copspeech.csi") ||
               EndsWithIgnoreCase(path, "sound/speech/copspeech.csi") ||
               EndsWithIgnoreCase(path, "sound\\speech\\copspeech.evt") ||
               EndsWithIgnoreCase(path, "sound/speech/copspeech.evt") ||
               EndsWithIgnoreCase(path, "sound\\speech\\copspeech.idx") ||
               EndsWithIgnoreCase(path, "sound/speech/copspeech.idx");
    }
}

template <typename Char, std::size_t Size>
bool ReplaceSuffix(const Char* value, const Char* suffix, const Char* replacement,
                   std::array<Char, Size>& output) noexcept
{
    if (!EndsWithIgnoreCase(value, suffix))
    {
        return false;
    }
    const std::size_t value_length = std::char_traits<Char>::length(value);
    const std::size_t suffix_length = std::char_traits<Char>::length(suffix);
    const std::size_t replacement_length = std::char_traits<Char>::length(replacement);
    const std::size_t prefix_length = value_length - suffix_length;
    if (prefix_length + replacement_length + 1 > output.size())
    {
        return false;
    }
    std::char_traits<Char>::copy(output.data(), value, prefix_length);
    std::char_traits<Char>::copy(output.data() + prefix_length, replacement, replacement_length);
    output[prefix_length + replacement_length] = static_cast<Char>(0);
    return true;
}

template <typename Char, std::size_t Size>
LONG MakeJapanesePath(const Char* value, std::array<Char, Size>& output) noexcept
{
    if constexpr (sizeof(Char) == sizeof(wchar_t))
    {
        if (ReplaceSuffix(value, L"languages\\english.bin", L"languages\\Japanese.bin", output) ||
            ReplaceSuffix(value, L"languages/english.bin", L"languages/Japanese.bin", output))
        {
            return 1;
        }
        if (ReplaceSuffix(value, L"_english_pal.vp6", L"_japanese_ntsc.vp6", output) ||
            ReplaceSuffix(value, L"_english_ntsc.vp6", L"_japanese_ntsc.vp6", output) ||
            ReplaceSuffix(value, L"_japanese_pal.vp6", L"_japanese_ntsc.vp6", output))
        {
            return 2;
        }
    }
    else
    {
        if (ReplaceSuffix(value, "languages\\english.bin", "languages\\Japanese.bin", output) ||
            ReplaceSuffix(value, "languages/english.bin", "languages/Japanese.bin", output))
        {
            return 1;
        }
        if (ReplaceSuffix(value, "_english_pal.vp6", "_japanese_ntsc.vp6", output) ||
            ReplaceSuffix(value, "_english_ntsc.vp6", "_japanese_ntsc.vp6", output) ||
            ReplaceSuffix(value, "_japanese_pal.vp6", "_japanese_ntsc.vp6", output))
        {
            return 2;
        }
    }
    return 0;
}

void Log(const char* format, ...) noexcept
{
    if (g_log == INVALID_HANDLE_VALUE || g_log == nullptr)
    {
        return;
    }
    std::array<char, 1024> message{};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(message.data(), message.size(), _TRUNCATE, format, arguments);
    va_end(arguments);
    const int length = static_cast<int>(strnlen_s(message.data(), message.size()));
    if (length > 0)
    {
        DWORD written = 0;
        WriteFile(g_log, message.data(), static_cast<DWORD>(length), &written, nullptr);
        WriteFile(g_log, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(g_log);
    }
}

void OpenLog() noexcept
{
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return;
    }
    wchar_t* slash = wcsrchr(path.data(), L'\\');
    if (slash == nullptr)
    {
        return;
    }
    wcscpy_s(slash + 1, path.size() - static_cast<std::size_t>(slash + 1 - path.data()),
             L"NFSMWJapaneseBootstrap.log");
    g_log = CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void ResolveGameDirectory() noexcept
{
    const DWORD wide_length = GetModuleFileNameW(nullptr, g_game_directory_w.data(),
                                                 static_cast<DWORD>(g_game_directory_w.size()));
    if (wide_length == 0 || wide_length >= g_game_directory_w.size())
    {
        g_game_directory_w[0] = L'\0';
        g_game_directory_a[0] = '\0';
        return;
    }
    wchar_t* wide_slash = wcsrchr(g_game_directory_w.data(), L'\\');
    if (wide_slash == nullptr)
    {
        g_game_directory_w[0] = L'\0';
        g_game_directory_a[0] = '\0';
        return;
    }
    wide_slash[1] = L'\0';
    const int ansi_length = WideCharToMultiByte(CP_ACP, 0, g_game_directory_w.data(), -1,
                                                g_game_directory_a.data(),
                                                static_cast<int>(g_game_directory_a.size()), nullptr, nullptr);
    if (ansi_length == 0)
    {
        g_game_directory_a[0] = '\0';
    }

    std::array<wchar_t, 32768> widescreen_fix_path{};
    wcscpy_s(widescreen_fix_path.data(), widescreen_fix_path.size(), g_game_directory_w.data());
    wcscat_s(widescreen_fix_path.data(), widescreen_fix_path.size(),
             L"scripts\\NFSMostWanted.WidescreenFix.asi");
    std::array<wchar_t, 32768> asi_loader_path{};
    wcscpy_s(asi_loader_path.data(), asi_loader_path.size(), g_game_directory_w.data());
    wcscat_s(asi_loader_path.data(), asi_loader_path.size(), L"dinput8.dll");
    g_widescreen_fix_present =
        GetFileAttributesW(widescreen_fix_path.data()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(asi_loader_path.data()) != INVALID_FILE_ATTRIBUTES;
}

template <typename Char>
bool IsResolutionValue(const Char* name) noexcept;

template <>
bool IsResolutionValue<char>(const char* name) noexcept
{
    return name != nullptr && _stricmp(name, "g_RacingResolution") == 0;
}

template <>
bool IsResolutionValue<wchar_t>(const wchar_t* name) noexcept
{
    return name != nullptr && _wcsicmp(name, L"g_RacingResolution") == 0;
}

template <typename Char>
LSTATUS ReplaceResolutionValue(const Char* name, LPDWORD type, LPBYTE data, LPDWORD size,
                               const DWORD capacity) noexcept
{
    if (!IsResolutionValue(name) || size == nullptr ||
        (type != nullptr && *type != REG_DWORD))
    {
        return ERROR_NOT_FOUND;
    }
    if (IsUnpackedModTargetImage())
    {
        // WideFix stores the actual widescreen dimensions in this DWORD. Replacing
        // that packed value with the legacy index 1 makes the unpacked 1.3 target
        // create an invalid 3D render path while its 2D HUD continues to draw.
        Log("REGISTRY_PASSTHROUGH_DWORD name=g_RacingResolution target=unpacked_1_3");
        return ERROR_NOT_FOUND;
    }
    constexpr DWORD replacement = 1;
    constexpr DWORD required = sizeof(replacement);
    if (type != nullptr)
    {
        *type = REG_DWORD;
    }
    *size = required;
    if (data == nullptr)
    {
        return ERROR_SUCCESS;
    }
    if (capacity < required)
    {
        return ERROR_MORE_DATA;
    }
    memcpy(data, &replacement, required);
    InterlockedIncrement(&g_resolution_overrides);
    Log("REGISTRY_OVERRIDE_DWORD name=g_RacingResolution value=1");
    return ERROR_SUCCESS;
}

bool LooksLikeSupportedImage() noexcept
{
    const auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr)
    {
        return false;
    }
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        return false;
    }
    const bool no_cd = nt->FileHeader.TimeDateStamp == kSupportedTimestamp &&
                       nt->OptionalHeader.SizeOfImage == kSupportedImageSize &&
                       nt->OptionalHeader.AddressOfEntryPoint == kSupportedEntryPoint;
    const bool retail = nt->FileHeader.TimeDateStamp == kRetailTimestamp &&
                        nt->OptionalHeader.SizeOfImage == kRetailImageSize &&
                        nt->OptionalHeader.AddressOfEntryPoint == kRetailEntryPoint;
    const bool unpacked = nt->FileHeader.TimeDateStamp == kSupportedTimestamp &&
                          (nt->OptionalHeader.SizeOfImage == kUnpackedImageSize ||
                           nt->OptionalHeader.SizeOfImage == kUnpackedStockShaderImageSize ||
                                      nt->OptionalHeader.SizeOfImage == 0x00678E4Eu) &&
                          nt->OptionalHeader.AddressOfEntryPoint == kUnpackedEntryPoint;
    return no_cd || retail || unpacked;
}

void** FindMainImportSlot(const char* module_name, const char* function_name) noexcept
{
    const auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr)
    {
        return nullptr;
    }
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0)
    {
        return nullptr;
    }
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor)
    {
        const char* imported_module = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(imported_module, module_name) != 0)
        {
            continue;
        }
        auto names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            base + (descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk));
        auto addresses = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++addresses)
        {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
            {
                continue;
            }
            const auto imported = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(imported->Name), function_name) == 0)
            {
                return reinterpret_cast<void**>(&addresses->u1.Function);
            }
        }
    }
    return nullptr;
}

bool PatchPointer(void** slot, void* replacement, void** original) noexcept
{
    DWORD protection = 0;
    if (slot == nullptr || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection))
    {
        return false;
    }
    if (original != nullptr)
    {
        *original = *slot;
    }
    *slot = replacement;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

bool ReplaceAnsiValue(const char* name, LPBYTE data, LPDWORD size, const DWORD capacity) noexcept
{
    if (name == nullptr || size == nullptr)
    {
        return false;
    }
    const char* replacement = nullptr;
    volatile LONG* counter = nullptr;
    if (kForceEnglishCompatibilityLanguage && _stricmp(name, "Language") == 0)
    {
        replacement = "English US";
        counter = &g_language_overrides;
    }
    else if (kForceEnglishCompatibilityLanguage && _stricmp(name, "Locale") == 0)
    {
        replacement = "en-us";
        counter = &g_locale_overrides;
    }
    else if ((_stricmp(name, "Install Dir") == 0 || _stricmp(name, "InstallDir") == 0 ||
              _stricmp(name, "Path") == 0) && g_game_directory_a[0] != '\0')
    {
        replacement = g_game_directory_a.data();
        counter = &g_install_path_overrides;
    }
    else
    {
        return false;
    }
    const DWORD required = static_cast<DWORD>(strlen(replacement) + 1);
    if (data == nullptr || capacity < required)
    {
        *size = required;
        return true;
    }
    memcpy(data, replacement, required);
    *size = required;
    InterlockedIncrement(counter);
    Log("REGISTRY_OVERRIDE_A name=%s value=%s", name, replacement);
    return true;
}

bool ReplaceWideValue(const wchar_t* name, LPBYTE data, LPDWORD size, const DWORD capacity) noexcept
{
    if (name == nullptr || size == nullptr)
    {
        return false;
    }
    const wchar_t* replacement = nullptr;
    volatile LONG* counter = nullptr;
    if (kForceEnglishCompatibilityLanguage && _wcsicmp(name, L"Language") == 0)
    {
        replacement = L"English US";
        counter = &g_language_overrides;
    }
    else if (kForceEnglishCompatibilityLanguage && _wcsicmp(name, L"Locale") == 0)
    {
        replacement = L"en-us";
        counter = &g_locale_overrides;
    }
    else if ((_wcsicmp(name, L"Install Dir") == 0 || _wcsicmp(name, L"InstallDir") == 0 ||
              _wcsicmp(name, L"Path") == 0) && g_game_directory_w[0] != L'\0')
    {
        replacement = g_game_directory_w.data();
        counter = &g_install_path_overrides;
    }
    else
    {
        return false;
    }
    const DWORD required = static_cast<DWORD>((wcslen(replacement) + 1) * sizeof(wchar_t));
    if (data == nullptr || capacity < required)
    {
        *size = required;
        return true;
    }
    memcpy(data, replacement, required);
    *size = required;
    InterlockedIncrement(counter);
    Log("REGISTRY_OVERRIDE_W name=%ls", name);
    return true;
}

LSTATUS WINAPI HookRegQueryValueExA(
    HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    const DWORD capacity = size != nullptr ? *size : 0;
    const LSTATUS result = g_reg_query_a(key, name, reserved, type, data, size);
    if (result != ERROR_SUCCESS || name == nullptr || size == nullptr)
    {
        return result;
    }
    if (_stricmp(name, "Language") == 0)
    {
        InstallJapaneseLanguageSlot();
    }
    const LSTATUS resolution_result = ReplaceResolutionValue(name, type, data, size, capacity);
    if (resolution_result != ERROR_NOT_FOUND)
    {
        return resolution_result;
    }
    if (
        (type != nullptr && *type != REG_SZ && *type != REG_EXPAND_SZ))
    {
        return result;
    }
    if (data == nullptr)
    {
        std::array<BYTE, 256> probe{};
        DWORD probe_size = static_cast<DWORD>(probe.size());
        DWORD probe_type = 0;
        if (g_reg_query_a(key, name, reserved, &probe_type, probe.data(), &probe_size) != ERROR_SUCCESS ||
            (probe_type != REG_SZ && probe_type != REG_EXPAND_SZ))
        {
            return result;
        }
        const char* current = reinterpret_cast<const char*>(probe.data());
        if ((_stricmp(name, "Language") == 0 && _stricmp(current, "japanese") != 0) ||
            (_stricmp(name, "Locale") == 0 && _stricmp(current, "ja") != 0))
        {
            return result;
        }
    }
    else
    {
        const char* current = reinterpret_cast<const char*>(data);
        if ((_stricmp(name, "Language") == 0 && _stricmp(current, "japanese") != 0) ||
            (_stricmp(name, "Locale") == 0 && _stricmp(current, "ja") != 0))
        {
            return result;
        }
    }
    ReplaceAnsiValue(name, data, size, capacity);
    return data != nullptr && capacity < *size ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

LSTATUS WINAPI HookRegQueryValueExW(
    HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    const DWORD capacity = size != nullptr ? *size : 0;
    const LSTATUS result = g_reg_query_w(key, name, reserved, type, data, size);
    if (result != ERROR_SUCCESS || name == nullptr || size == nullptr)
    {
        return result;
    }
    if (_wcsicmp(name, L"Language") == 0)
    {
        InstallJapaneseLanguageSlot();
    }
    const LSTATUS resolution_result = ReplaceResolutionValue(name, type, data, size, capacity);
    if (resolution_result != ERROR_NOT_FOUND)
    {
        return resolution_result;
    }
    if (
        (type != nullptr && *type != REG_SZ && *type != REG_EXPAND_SZ))
    {
        return result;
    }
    if (data == nullptr)
    {
        std::array<BYTE, 512> probe{};
        DWORD probe_size = static_cast<DWORD>(probe.size());
        DWORD probe_type = 0;
        if (g_reg_query_w(key, name, reserved, &probe_type, probe.data(), &probe_size) != ERROR_SUCCESS ||
            (probe_type != REG_SZ && probe_type != REG_EXPAND_SZ))
        {
            return result;
        }
        const wchar_t* current = reinterpret_cast<const wchar_t*>(probe.data());
        if ((_wcsicmp(name, L"Language") == 0 && _wcsicmp(current, L"japanese") != 0) ||
            (_wcsicmp(name, L"Locale") == 0 && _wcsicmp(current, L"ja") != 0))
        {
            return result;
        }
    }
    else
    {
        const wchar_t* current = reinterpret_cast<const wchar_t*>(data);
        if ((_wcsicmp(name, L"Language") == 0 && _wcsicmp(current, L"japanese") != 0) ||
            (_wcsicmp(name, L"Locale") == 0 && _wcsicmp(current, L"ja") != 0))
        {
            return result;
        }
    }
    ReplaceWideValue(name, data, size, capacity);
    return data != nullptr && capacity < *size ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

HANDLE WINAPI HookCreateFileA(
    LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation,
    DWORD attributes, HANDLE template_file)
{
    std::array<char, 4096> redirected{};
    const LONG kind = MakeJapanesePath(path, redirected);
    if (kind != 0)
    {
        const HANDLE handle = g_create_file_a(
            redirected.data(), access, share, security, creation, attributes, template_file);
        if (handle != INVALID_HANDLE_VALUE)
        {
            InterlockedIncrement(kind == 1 ? &g_text_redirects : &g_movie_redirects);
            Log("FILE_REDIRECT_A kind=%s source=%s target=%s", kind == 1 ? "text" : "movie",
                path, redirected.data());
            return handle;
        }
    }
    const HANDLE handle = g_create_file_a(path, access, share, security, creation, attributes, template_file);
    if (handle != INVALID_HANDLE_VALUE &&
        (EndsWithIgnoreCase(path, "languages\\japanese.bin") ||
         EndsWithIgnoreCase(path, "languages/japanese.bin")))
    {
        InterlockedIncrement(&g_japanese_text_opens);
        Log("JAPANESE_TEXT_OPEN_A path=%s", path);
    }
    if (handle != INVALID_HANDLE_VALUE && IsJapaneseSpeechPath(path))
    {
        InterlockedIncrement(&g_japanese_speech_opens);
        Log("JAPANESE_SPEECH_OPEN_A path=%s", path);
    }
    return handle;
}

HANDLE WINAPI HookCreateFileW(
    LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation,
    DWORD attributes, HANDLE template_file)
{
    std::array<wchar_t, 4096> redirected{};
    const LONG kind = MakeJapanesePath(path, redirected);
    if (kind != 0)
    {
        const HANDLE handle = g_create_file_w(
            redirected.data(), access, share, security, creation, attributes, template_file);
        if (handle != INVALID_HANDLE_VALUE)
        {
            InterlockedIncrement(kind == 1 ? &g_text_redirects : &g_movie_redirects);
            Log("FILE_REDIRECT_W kind=%s", kind == 1 ? "text" : "movie");
            return handle;
        }
    }
    const HANDLE handle = g_create_file_w(path, access, share, security, creation, attributes, template_file);
    if (handle != INVALID_HANDLE_VALUE &&
        (EndsWithIgnoreCase(path, L"languages\\japanese.bin") ||
         EndsWithIgnoreCase(path, L"languages/japanese.bin")))
    {
        InterlockedIncrement(&g_japanese_text_opens);
        Log("JAPANESE_TEXT_OPEN_W");
    }
    if (handle != INVALID_HANDLE_VALUE && IsJapaneseSpeechPath(path))
    {
        InterlockedIncrement(&g_japanese_speech_opens);
        Log("JAPANESE_SPEECH_OPEN_W path=%ls", path);
    }
    return handle;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR name)
{
    const FARPROC resolved = g_real_get_proc(module, name);
    if (name == nullptr || IS_INTRESOURCE(name))
    {
        return resolved;
    }
    if (_strnicmp(name, "Reg", 3) == 0)
    {
        InterlockedIncrement(&g_registry_resolutions);
        Log("REGISTRY_RESOLVE name=%s", name);
    }
    if (strcmp(name, "RegQueryValueExA") == 0 && resolved != nullptr)
    {
        if (InterlockedCompareExchange(&g_inline_active, 0, 0) == 0)
        {
            g_reg_query_a = reinterpret_cast<RegQueryValueExAFn>(resolved);
        }
        return reinterpret_cast<FARPROC>(&HookRegQueryValueExA);
    }
    if (strcmp(name, "RegQueryValueExW") == 0 && resolved != nullptr)
    {
        if (InterlockedCompareExchange(&g_inline_active, 0, 0) == 0)
        {
            g_reg_query_w = reinterpret_cast<RegQueryValueExWFn>(resolved);
        }
        return reinterpret_cast<FARPROC>(&HookRegQueryValueExW);
    }
    if (strcmp(name, "CreateFileA") == 0 && resolved != nullptr)
    {
        if (InterlockedCompareExchange(&g_inline_active, 0, 0) == 0)
        {
            g_create_file_a = reinterpret_cast<CreateFileAFn>(resolved);
        }
        return reinterpret_cast<FARPROC>(&HookCreateFileA);
    }
    if (strcmp(name, "CreateFileW") == 0 && resolved != nullptr)
    {
        if (InterlockedCompareExchange(&g_inline_active, 0, 0) == 0)
        {
            g_create_file_w = reinterpret_cast<CreateFileWFn>(resolved);
        }
        return reinterpret_cast<FARPROC>(&HookCreateFileW);
    }
    return resolved;
}

bool InstallEarlyHook() noexcept
{
    InterlockedExchange(&g_target_accepted, 0);
    if (!LooksLikeSupportedImage())
    {
        Log("TARGET_REJECTED");
        return false;
    }
    const bool language_slot_installed = InstallJapaneseLanguageSlot();
    g_get_proc_slot = FindMainImportSlot("KERNEL32.dll", "GetProcAddress");
    const bool resolver_installed = PatchPointer(g_get_proc_slot, reinterpret_cast<void*>(&HookGetProcAddress),
                                                 reinterpret_cast<void**>(&g_real_get_proc));
    bool inline_installed = false;
    if (MH_Initialize() == MH_OK)
    {
        const MH_STATUS query_a = MH_CreateHookApi(
            L"advapi32.dll", "RegQueryValueExA", reinterpret_cast<void*>(&HookRegQueryValueExA),
            reinterpret_cast<void**>(&g_reg_query_a));
        const MH_STATUS query_w = MH_CreateHookApi(
            L"advapi32.dll", "RegQueryValueExW", reinterpret_cast<void*>(&HookRegQueryValueExW),
            reinterpret_cast<void**>(&g_reg_query_w));
        const MH_STATUS create_a = MH_CreateHookApi(
            L"kernel32.dll", "CreateFileA", reinterpret_cast<void*>(&HookCreateFileA),
            reinterpret_cast<void**>(&g_create_file_a));
        const MH_STATUS create_w = MH_CreateHookApi(
            L"kernel32.dll", "CreateFileW", reinterpret_cast<void*>(&HookCreateFileW),
            reinterpret_cast<void**>(&g_create_file_w));
        if (query_a == MH_OK && query_w == MH_OK && create_a == MH_OK && create_w == MH_OK &&
            MH_EnableHook(MH_ALL_HOOKS) == MH_OK)
        {
            InterlockedExchange(&g_inline_active, 1);
            inline_installed = true;
        }
        else
        {
            MH_Uninitialize();
        }
    }
    Log("TARGET_ACCEPTED language_slot=%s get_proc_hook=%s inline_registry_file_hooks=%s",
        language_slot_installed ? "true" : "false", resolver_installed ? "true" : "false",
        inline_installed ? "true" : "false");
    const bool accepted = resolver_installed || inline_installed;
    InterlockedExchange(&g_target_accepted, accepted ? 1 : 0);
    return accepted;
}

template <typename T>
T VersionProc(const char* name) noexcept
{
    return g_system_version == nullptr ? nullptr : reinterpret_cast<T>(GetProcAddress(g_system_version, name));
}

bool LoadSystemVersion() noexcept
{
    std::array<wchar_t, MAX_PATH> path{};
    const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
    if (length == 0 || length + 13 >= path.size())
    {
        return false;
    }
    wcscat_s(path.data(), path.size(), L"\\version.dll");
    g_system_version = LoadLibraryW(path.data());
    if (g_system_version != nullptr)
    {
        g_version_by_handle = GetProcAddress(g_system_version, "GetFileVersionInfoByHandle");
    }
    return g_system_version != nullptr;
}
}

extern "C" __declspec(naked) void WINAPI ProxyGetFileVersionInfoByHandle()
{
    __asm { jmp dword ptr[g_version_by_handle] }
}

extern "C" BOOL WINAPI NFSMWJapaneseBootstrapActive()
{
    return InterlockedCompareExchange(&g_target_accepted, 0, 0) != 0 ? TRUE : FALSE;
}

#define FORWARD_BOOL(name, signature, arguments) \
    extern "C" BOOL WINAPI name signature { using Fn = BOOL(WINAPI*) signature; const auto fn = VersionProc<Fn>(#name); return fn != nullptr ? fn arguments : FALSE; }
#define FORWARD_DWORD(name, signature, arguments) \
    extern "C" DWORD WINAPI name signature { using Fn = DWORD(WINAPI*) signature; const auto fn = VersionProc<Fn>(#name); return fn != nullptr ? fn arguments : 0; }

FORWARD_BOOL(GetFileVersionInfoA, (LPCSTR a, DWORD b, DWORD c, LPVOID d), (a, b, c, d))
FORWARD_BOOL(GetFileVersionInfoW, (LPCWSTR a, DWORD b, DWORD c, LPVOID d), (a, b, c, d))
FORWARD_BOOL(GetFileVersionInfoExA, (DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e), (a, b, c, d, e))
FORWARD_BOOL(GetFileVersionInfoExW, (DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e), (a, b, c, d, e))
FORWARD_DWORD(GetFileVersionInfoSizeA, (LPCSTR a, LPDWORD b), (a, b))
FORWARD_DWORD(GetFileVersionInfoSizeW, (LPCWSTR a, LPDWORD b), (a, b))
FORWARD_DWORD(GetFileVersionInfoSizeExA, (DWORD a, LPCSTR b, LPDWORD c), (a, b, c))
FORWARD_DWORD(GetFileVersionInfoSizeExW, (DWORD a, LPCWSTR b, LPDWORD c), (a, b, c))
FORWARD_DWORD(VerFindFileA, (DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h), (a, b, c, d, e, f, g, h))
FORWARD_DWORD(VerFindFileW, (DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h), (a, b, c, d, e, f, g, h))
FORWARD_DWORD(VerInstallFileA, (DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h), (a, b, c, d, e, f, g, h))
FORWARD_DWORD(VerInstallFileW, (DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h), (a, b, c, d, e, f, g, h))
FORWARD_BOOL(VerQueryValueA, (LPCVOID a, LPCSTR b, LPVOID* c, PUINT d), (a, b, c, d))
FORWARD_BOOL(VerQueryValueW, (LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d), (a, b, c, d))

extern "C" DWORD WINAPI ProxyVerLanguageNameA(DWORD language, LPSTR buffer, DWORD size)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    const auto fn = VersionProc<Fn>("VerLanguageNameA");
    return fn != nullptr ? fn(language, buffer, size) : 0;
}

extern "C" DWORD WINAPI ProxyVerLanguageNameW(DWORD language, LPWSTR buffer, DWORD size)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    const auto fn = VersionProc<Fn>("VerLanguageNameW");
    return fn != nullptr ? fn(language, buffer, size) : 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        ResolveGameDirectory();
        OpenLog();
        const bool system_loaded = LoadSystemVersion();
        Log("BOOTSTRAP system_version=%s widescreen_fix=%s", system_loaded ? "true" : "false",
            g_widescreen_fix_present ? "true" : "false");
        InstallEarlyHook();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log("DETACH language_slot=%ld registry_resolutions=%ld language=%ld locale=%ld install_path=%ld resolution=%ld text=%ld japanese_text_opens=%ld speech_opens=%ld movies=%ld",
            g_language_slot_installed,
            g_registry_resolutions, g_language_overrides, g_locale_overrides, g_install_path_overrides,
            g_resolution_overrides, g_text_redirects, g_japanese_text_opens,
            g_japanese_speech_opens, g_movie_redirects);
        if (g_log != INVALID_HANDLE_VALUE && g_log != nullptr)
        {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
    }
    return TRUE;
}
