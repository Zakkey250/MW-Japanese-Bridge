#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "BridgeLogic.h"
#include "MinHook.h"

#pragma comment(lib, "bcrypt.lib")

namespace nfsmw_japanese_bridge
{
namespace
{

constexpr char kVersion[] = "1.2.2-nfspatcher";
constexpr std::uint64_t kSupportedSize = 7254894ull;
constexpr char kSupportedSha256[] =
    "87840190FEB707CFC1AB2DD7662E1028A0EDF01A03A74A595C5B06F6BD8E68C0";
constexpr char kSupportedNormalizedSha256[] =
    "7D91A43C61242BD946F7534754EA1FE1D680AF26311D6A5C3EAEC935DADBCF02";
struct MutableFileRange
{
    std::uint64_t begin;
    std::uint64_t end;
};
constexpr std::array<MutableFileRange, 2> kNoCdMutableFileRanges{{
    {0x00000F00ull, 0x00000FB8ull},
    {0x005C47A5ull, 0x005C47ADull},
}};
constexpr DWORD kSupportedTimestamp = 0x438E4C8Cu;
constexpr DWORD kSupportedImageSize = 0x00680000u;
constexpr DWORD kSupportedEntryPoint = 0x0067F06Eu;
constexpr std::uint64_t kUnpackedCandidateSize = 5926912ull;
constexpr char kUnpackedCandidateSha256[] =
    "0C5675A08CD71FD6D31CA87E992A915054BD8B80D268BFF0561D7ECC2067E342";
constexpr DWORD kUnpackedCandidateImageSize = 0x00660000u;
constexpr std::uint64_t kUnpackedStockShaderSize = 6033408ull;
constexpr char kUnpackedStockShaderSha256[] =
    "05873CF968E0BDD021C1E67FF22E9350D22E7F433F1D749323FA6AE27F504700";
// Same layout, code, and shaders; only the stock icon resources differ.
constexpr char kUnpackedStockIconSha256[] =
    "6A1E41A449751241DE3653BE6BE9750A0B087E210011375C514872789CDE0BA8";
constexpr DWORD kUnpackedStockShaderImageSize = 0x0067A000u;
constexpr DWORD kUnpackedCandidateEntryPoint = 0x003C4040u;
constexpr std::uintptr_t kBootFlowChangeAddress = 0x0057DBB0u;
constexpr std::uintptr_t kSetCurrentLanguageAddress = 0x0057E6F0u;
constexpr std::uintptr_t kLanguageTableAddress = 0x008F40F8u;
constexpr std::uintptr_t kCurrentLanguageAddress = 0x008F41C0u;
constexpr SIZE_T kUnpackedJapaneseLanguageTexturesSize = 0x00050000u;
constexpr DWORD kGlobalMemoryManifestMagic = 0x53219999u;
constexpr DWORD kLanguageTexturesManifestHash = 0xDA18A301u;
constexpr DWORD kJapaneseLanguageTexturesReservedSize = 0x0004F700u;
constexpr DWORD kLanguageTexturesDescriptorOffset = 0x00000210u;
constexpr DWORD kLanguageTexturesDescriptorSize = 0x00000164u;
constexpr SIZE_T kGlobalMemoryDestinationOffset = 0x20u;
constexpr char kExpectedEnglishName[] = "ENGLISH";
constexpr char kExpectedEnglishPath[] = "LANGUAGES\\ENGLISH.BIN";
char g_japanese_path[] = "LANGUAGES\\JAPANESE.BIN";
constexpr std::array<unsigned char, 6> kBootFlowChangeSignature{
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x0C};

struct LanguageEntry
{
    int id;
    const char* name;
    const char* path;
    void* state;
    const void* locale_format;
};

using RegQueryValueExAFn = LSTATUS(WINAPI*)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
using RegQueryValueExWFn = LSTATUS(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using FindFirstFileAFn = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
using FindNextFileAFn = BOOL(WINAPI*)(HANDLE, LPWIN32_FIND_DATAA);
using BootFlowChangeFn = int(__thiscall*)(void*, int);
using SetCurrentLanguageFn = void(__cdecl*)(int);

HMODULE g_module{};
HANDLE g_log = INVALID_HANDLE_VALUE;
void** g_reg_query_a_slot{};
void** g_reg_query_w_slot{};
void** g_create_file_a_slot{};
void** g_create_file_w_slot{};
void** g_get_proc_address_slot{};
RegQueryValueExAFn g_reg_query_a{};
RegQueryValueExWFn g_reg_query_w{};
CreateFileAFn g_create_file_a{};
CreateFileWFn g_create_file_w{};
GetProcAddressFn g_get_proc_address{};
FindFirstFileAFn g_find_first_file_a{};
FindNextFileAFn g_find_next_file_a{};
BootFlowChangeFn g_boot_flow_change{};
SetCurrentLanguageFn g_set_current_language{};
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_redirect_movies{true};
std::atomic<bool> g_runtime_japanese_text{true};
std::atomic<bool> g_inline_hooks{false};
std::atomic<bool> g_external_registry_bootstrap{false};
std::atomic<unsigned> g_language_overrides{0};
std::atomic<unsigned> g_locale_overrides{0};
std::atomic<unsigned> g_text_redirects{0};
std::atomic<unsigned> g_movie_redirects{0};
std::atomic<unsigned> g_japanese_speech_opens{0};
std::atomic<unsigned> g_hidden_language_files{0};
std::atomic<unsigned> g_early_path_count{0};
std::array<std::array<char, 260>, 32> g_early_paths{};
std::atomic<unsigned> g_file_open_count{0};
std::atomic<unsigned> g_registry_query_count{0};
std::array<char, 64> g_last_registry_value{};
void* g_exception_handler{};
std::atomic<unsigned> g_exception_count{0};
std::atomic<bool> g_crash_diagnostics{false};
std::atomic<unsigned> g_runtime_text_reload_state{0};
std::atomic<unsigned> g_language_slot_patch_state{0};
std::atomic<bool> g_unpacked_candidate{false};
std::atomic<unsigned> g_global_memory_shadows{0};
void* g_unpacked_language_textures_memory{};

void RecordPath(const char* path) noexcept
{
    const unsigned index = g_early_path_count.fetch_add(1, std::memory_order_relaxed);
    if (index < g_early_paths.size() && path != nullptr)
    {
        strncpy_s(g_early_paths[index].data(), g_early_paths[index].size(), path, _TRUNCATE);
    }
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

template <typename Char>
bool IsGlobalMemoryFilePath(const Char* path) noexcept
{
    if constexpr (sizeof(Char) == sizeof(wchar_t))
    {
        return EndsWithIgnoreCase(path, L"global\\globalmemoryfile.bin") ||
               EndsWithIgnoreCase(path, L"global/globalmemoryfile.bin");
    }
    else
    {
        return EndsWithIgnoreCase(path, "global\\globalmemoryfile.bin") ||
               EndsWithIgnoreCase(path, "global/globalmemoryfile.bin");
    }
}

bool IsUnsupportedEnumeratedLanguage(const char* filename) noexcept
{
    return filename != nullptr && ShouldHideLanguageFile(filename);
}

void Log(const char* level, const char* format, ...) noexcept
{
    if (g_log == INVALID_HANDLE_VALUE || g_log == nullptr)
    {
        return;
    }
    std::array<char, 2048> message{};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(message.data(), message.size(), _TRUNCATE, format, arguments);
    va_end(arguments);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::array<char, 2304> line{};
    const int length = _snprintf_s(
        line.data(), line.size(), _TRUNCATE,
        "%04u-%02u-%02uT%02u:%02u:%02u.%03u [%s] %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, level, message.data());
    if (length > 0)
    {
        DWORD written = 0;
        WriteFile(g_log, line.data(), static_cast<DWORD>(length), &written, nullptr);
        FlushFileBuffers(g_log);
    }
}

bool TryCopyMemory(const void* source, void* destination, const SIZE_T size) noexcept
{
    __try
    {
        memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void LogHexBytes(const char* label, const void* address, const SIZE_T size) noexcept
{
    std::array<unsigned char, 64> bytes{};
    const SIZE_T count = (std::min)(size, static_cast<SIZE_T>(bytes.size()));
    if (!TryCopyMemory(address, bytes.data(), count))
    {
        Log("CRASH", "%s address=%p unreadable", label, address);
        return;
    }
    std::array<char, 3 * 64 + 1> text{};
    char* output = text.data();
    SIZE_T remaining = text.size();
    for (SIZE_T i = 0; i < count && remaining > 1; ++i)
    {
        const int written = _snprintf_s(output, remaining, _TRUNCATE, "%02X%s", bytes[i], i + 1 == count ? "" : " ");
        if (written <= 0)
        {
            break;
        }
        output += written;
        remaining -= static_cast<SIZE_T>(written);
    }
    Log("CRASH", "%s address=%p bytes=%s", label, address, text.data());
}

LONG CALLBACK RecordUnhandledException(EXCEPTION_POINTERS* pointers) noexcept
{
    // A vectored handler observes first-chance exceptions, including exceptions that the
    // game intentionally recovers from.  Never recurse while probing an invalid code or
    // stack window, and bound diagnostics so a repeated FE exception cannot turn into
    // synchronous per-frame disk I/O.
    static thread_local bool recording_exception = false;
    if (recording_exception)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr || pointers->ContextRecord == nullptr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = pointers->ExceptionRecord->ExceptionCode;
    const unsigned exception_index = g_exception_count.fetch_add(1, std::memory_order_relaxed);
    constexpr unsigned kMaximumDetailedExceptions = 4;
    if (exception_index >= kMaximumDetailedExceptions)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    recording_exception = true;
    if (exception_index < 32 && code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION)
    {
        Log("EXCEPTION", "index=%u code=0x%08lX address=%p flags=0x%08lX parameters=%lu",
            exception_index, code, pointers->ExceptionRecord->ExceptionAddress,
            pointers->ExceptionRecord->ExceptionFlags, pointers->ExceptionRecord->NumberParameters);
    }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION)
    {
        recording_exception = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const void* address = pointers->ExceptionRecord->ExceptionAddress;
    MEMORY_BASIC_INFORMATION memory{};
    std::array<char, MAX_PATH> module_path{};
    if (VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory) && memory.AllocationBase != nullptr)
    {
        GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), module_path.data(),
                           static_cast<DWORD>(module_path.size()));
    }
#if defined(_M_IX86)
    const CONTEXT* context = pointers->ContextRecord;
    Log("CRASH", "code=0x%08lX address=%p module=%s eip=0x%08lX esp=0x%08lX ebp=0x%08lX eax=0x%08lX ebx=0x%08lX ecx=0x%08lX edx=0x%08lX esi=0x%08lX edi=0x%08lX",
        code, address, module_path.data(), context->Eip, context->Esp, context->Ebp, context->Eax,
        context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi);
    if (code == EXCEPTION_ACCESS_VIOLATION && pointers->ExceptionRecord->NumberParameters >= 2)
    {
        Log("CRASH", "access=%lu target=0x%08lX (0=read 1=write 8=execute)",
            static_cast<DWORD>(pointers->ExceptionRecord->ExceptionInformation[0]),
            static_cast<DWORD>(pointers->ExceptionRecord->ExceptionInformation[1]));
    }
    const auto instruction_start = reinterpret_cast<const void*>(context->Eip >= 16 ? context->Eip - 16 : context->Eip);
    LogHexBytes("code-window", instruction_start, 64);
    LogHexBytes("stack-window", reinterpret_cast<const void*>(context->Esp), 64);

    std::array<DWORD, 64> stack{};
    if (TryCopyMemory(reinterpret_cast<const void*>(context->Esp), stack.data(), sizeof(stack)))
    {
        std::array<char, 1024> candidates{};
        SIZE_T used = 0;
        for (SIZE_T i = 0; i < stack.size(); ++i)
        {
            if (stack[i] < 0x00400000u || stack[i] >= 0x00A80000u)
            {
                continue;
            }
            const int written = _snprintf_s(candidates.data() + used, candidates.size() - used, _TRUNCATE,
                "%s[%02u]=0x%08lX", used == 0 ? "" : " ", static_cast<unsigned>(i), stack[i]);
            if (written <= 0)
            {
                break;
            }
            used += static_cast<SIZE_T>(written);
        }
        Log("CRASH", "main-image stack candidates: %s", used == 0 ? "none" : candidates.data());
        unsigned windows = 0;
        for (SIZE_T i = 0; i < stack.size() && windows < 12; ++i)
        {
            if (stack[i] < 0x00400020u || stack[i] >= 0x00A80000u)
            {
                continue;
            }
            std::array<char, 48> label{};
            _snprintf_s(label.data(), label.size(), _TRUNCATE, "stack[%02u]-code", static_cast<unsigned>(i));
            LogHexBytes(label.data(), reinterpret_cast<const void*>(stack[i] - 32u), 64);
            ++windows;
        }
    }
#else
    Log("CRASH", "code=0x%08lX address=%p module=%s", code, address, module_path.data());
#endif
    recording_exception = false;
    return EXCEPTION_CONTINUE_SEARCH;
}

std::filesystem::path ModulePath(const HMODULE module)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    return length == 0 || length >= buffer.size() ? std::filesystem::path{} :
                                                   std::filesystem::path(buffer.data(), buffer.data() + length);
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
    const bool protected_no_cd = nt->FileHeader.TimeDateStamp == kSupportedTimestamp &&
                                 nt->OptionalHeader.SizeOfImage == kSupportedImageSize &&
                                 nt->OptionalHeader.AddressOfEntryPoint == kSupportedEntryPoint;
    const bool unpacked_candidate = nt->FileHeader.TimeDateStamp == kSupportedTimestamp &&
                                    (nt->OptionalHeader.SizeOfImage == kUnpackedCandidateImageSize ||
                                     nt->OptionalHeader.SizeOfImage == kUnpackedStockShaderImageSize ||
                                     nt->OptionalHeader.SizeOfImage == 0x00678E4Eu) &&
                                    nt->OptionalHeader.AddressOfEntryPoint == kUnpackedCandidateEntryPoint;
    return protected_no_cd || unpacked_candidate;
}

bool InstallUnpackedJapaneseLanguageSlot() noexcept
{
    auto entry = reinterpret_cast<LanguageEntry*>(kLanguageTableAddress);
    int current = -999;
    __try
    {
        current = *reinterpret_cast<volatile int*>(kCurrentLanguageAddress);
        if (entry->id != 0 || entry->name == nullptr || entry->path == nullptr ||
            strcmp(entry->name, kExpectedEnglishName) != 0 ||
            strcmp(entry->path, kExpectedEnglishPath) != 0 || current != -1)
        {
            g_language_slot_patch_state.store(3, std::memory_order_release);
            Log("ERROR", "UNPACKED_LANGUAGE_SLOT_PATCH_REJECTED id=%d name=%s path=%s current=%d",
                entry->id, entry->name != nullptr ? entry->name : "(null)",
                entry->path != nullptr ? entry->path : "(null)", current);
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_language_slot_patch_state.store(3, std::memory_order_release);
        Log("ERROR", "UNPACKED_LANGUAGE_SLOT_PATCH_REJECTED unreadable_table=true current=%d", current);
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &previous))
    {
        g_language_slot_patch_state.store(3, std::memory_order_release);
        Log("ERROR", "UNPACKED_LANGUAGE_SLOT_PATCH_REJECTED virtual_protect_error=%lu", GetLastError());
        return false;
    }
    // Keep the ENGLISH slot identity so the executable follows its native EN-UK
    // frontend/rendering path. Only the string table is redirected to Japanese.
    entry->path = g_japanese_path;
    DWORD ignored = 0;
    VirtualProtect(entry, sizeof(*entry), previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), entry, sizeof(*entry));
    g_language_slot_patch_state.store(2, std::memory_order_release);
    Log("INFO", "UNPACKED_LANGUAGE_SLOT_PATCH_INSTALLED id=0 name=%s path=%s current=%d",
        entry->name, g_japanese_path, current);
    return true;
}

bool ReserveUnpackedJapaneseLanguageTexturesMemory() noexcept
{
    void* const allocated = VirtualAlloc(
        nullptr, kUnpackedJapaneseLanguageTexturesSize,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (allocated == nullptr)
    {
        const DWORD error = GetLastError();
        Log("ERROR", "UNPACKED_LANGUAGE_TEXTURES_RESERVE_FAILED size=0x%zX error=%lu",
            kUnpackedJapaneseLanguageTexturesSize, error);
        return false;
    }
    g_unpacked_language_textures_memory = allocated;
    Log("INFO", "UNPACKED_LANGUAGE_TEXTURES_RESERVED address=%p size=0x%zX",
        allocated, kUnpackedJapaneseLanguageTexturesSize);
    return true;
}

bool ValidateAndPatchGlobalMemoryFile(std::vector<unsigned char>& bytes) noexcept
{
    if (bytes.size() <= kGlobalMemoryDestinationOffset + sizeof(DWORD) ||
        bytes.size() <= kLanguageTexturesDescriptorOffset + kLanguageTexturesDescriptorSize)
    {
        return false;
    }
    auto read_dword = [&bytes](const SIZE_T offset) noexcept
    {
        DWORD value = 0;
        memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    if (read_dword(0x08u) != kGlobalMemoryManifestMagic ||
        read_dword(0x10u) != kLanguageTexturesManifestHash ||
        read_dword(0x14u) != kLanguageTexturesDescriptorOffset ||
        read_dword(0x18u) != kJapaneseLanguageTexturesReservedSize ||
        read_dword(0x1Cu) != kLanguageTexturesDescriptorSize ||
        g_unpacked_language_textures_memory == nullptr)
    {
        return false;
    }
    const DWORD destination = static_cast<DWORD>(
        reinterpret_cast<std::uintptr_t>(g_unpacked_language_textures_memory));
    memcpy(bytes.data() + kGlobalMemoryDestinationOffset, &destination, sizeof(destination));
    return true;
}

template <typename Char, typename CreateFileFn>
HANDLE OpenRelocatedGlobalMemoryFile(const Char* path, const CreateFileFn create_file) noexcept
{
    const HANDLE source = create_file(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (source == INVALID_HANDLE_VALUE)
    {
        return INVALID_HANDLE_VALUE;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(source, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024)
    {
        CloseHandle(source);
        return INVALID_HANDLE_VALUE;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));
    SIZE_T used = 0;
    while (used < bytes.size())
    {
        DWORD read = 0;
        const SIZE_T remaining = bytes.size() - used;
        const DWORD requested = remaining > 1024u * 1024u
                                    ? 1024u * 1024u
                                    : static_cast<DWORD>(remaining);
        if (!ReadFile(source, bytes.data() + used, requested, &read, nullptr) || read == 0)
        {
            CloseHandle(source);
            return INVALID_HANDLE_VALUE;
        }
        used += read;
    }
    CloseHandle(source);

    if (!ValidateAndPatchGlobalMemoryFile(bytes))
    {
        Log("ERROR", "UNPACKED_GLOBAL_MEMORY_SHADOW_REJECTED validation_failed=true");
        return INVALID_HANDLE_VALUE;
    }

    std::array<Char, MAX_PATH> temporary_directory{};
    std::array<Char, MAX_PATH> temporary_path{};
    DWORD directory_length = 0;
    UINT generated = 0;
    if constexpr (sizeof(Char) == sizeof(wchar_t))
    {
        directory_length = GetTempPathW(static_cast<DWORD>(temporary_directory.size()),
                                        reinterpret_cast<wchar_t*>(temporary_directory.data()));
        if (directory_length != 0 && directory_length < temporary_directory.size())
        {
            generated = GetTempFileNameW(
                reinterpret_cast<const wchar_t*>(temporary_directory.data()), L"MWJ", 0,
                reinterpret_cast<wchar_t*>(temporary_path.data()));
        }
    }
    else
    {
        directory_length = GetTempPathA(static_cast<DWORD>(temporary_directory.size()),
                                        reinterpret_cast<char*>(temporary_directory.data()));
        if (directory_length != 0 && directory_length < temporary_directory.size())
        {
            generated = GetTempFileNameA(
                reinterpret_cast<const char*>(temporary_directory.data()), "MWJ", 0,
                reinterpret_cast<char*>(temporary_path.data()));
        }
    }
    if (generated == 0)
    {
        return INVALID_HANDLE_VALUE;
    }

    const HANDLE shadow = create_file(
        temporary_path.data(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (shadow == INVALID_HANDLE_VALUE)
    {
        if constexpr (sizeof(Char) == sizeof(wchar_t))
            DeleteFileW(reinterpret_cast<const wchar_t*>(temporary_path.data()));
        else
            DeleteFileA(reinterpret_cast<const char*>(temporary_path.data()));
        return INVALID_HANDLE_VALUE;
    }

    used = 0;
    while (used < bytes.size())
    {
        DWORD written = 0;
        const SIZE_T remaining = bytes.size() - used;
        const DWORD requested = remaining > 1024u * 1024u
                                    ? 1024u * 1024u
                                    : static_cast<DWORD>(remaining);
        if (!WriteFile(shadow, bytes.data() + used, requested, &written, nullptr) || written == 0)
        {
            CloseHandle(shadow);
            return INVALID_HANDLE_VALUE;
        }
        used += written;
    }
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(shadow, beginning, nullptr, FILE_BEGIN))
    {
        CloseHandle(shadow);
        return INVALID_HANDLE_VALUE;
    }
    const unsigned index = g_global_memory_shadows.fetch_add(1, std::memory_order_relaxed);
    Log("INFO", "UNPACKED_GLOBAL_MEMORY_SHADOW_READY index=%u bytes=%zu destination=%p",
        index, bytes.size(), g_unpacked_language_textures_memory);
    return shadow;
}

bool PatchPointer(void** slot, void* replacement, void** original) noexcept
{
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
    {
        return false;
    }
    if (original != nullptr)
    {
        *original = *slot;
    }
    *slot = replacement;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

void** FindImportSlot(const char* module_name, const char* function_name) noexcept
{
    const auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
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

LSTATUS WINAPI HookRegQueryValueExA(
    HKEY key, LPCSTR value_name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD data_size)
{
    const DWORD capacity = data_size != nullptr ? *data_size : 0;
    const LSTATUS result = g_reg_query_a(key, value_name, reserved, type, data, data_size);
    g_registry_query_count.fetch_add(1, std::memory_order_relaxed);
    if (value_name != nullptr)
    {
        strncpy_s(g_last_registry_value.data(), g_last_registry_value.size(), value_name, _TRUNCATE);
    }
    if (!g_enabled.load(std::memory_order_relaxed) || result != ERROR_SUCCESS || value_name == nullptr ||
        data == nullptr || data_size == nullptr || (type != nullptr && *type != REG_SZ && *type != REG_EXPAND_SZ))
    {
        return result;
    }
    const auto replacement = RegistryReplacement(value_name, reinterpret_cast<const char*>(data));
    if (replacement.empty())
    {
        return result;
    }
    const DWORD required = static_cast<DWORD>(replacement.size() + 1);
    if (capacity < required)
    {
        *data_size = required;
        return ERROR_MORE_DATA;
    }
    memcpy(data, replacement.c_str(), required);
    *data_size = required;
    if (EqualsIgnoreCase(value_name, "Language"))
    {
        g_language_overrides.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        g_locale_overrides.fetch_add(1, std::memory_order_relaxed);
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI HookRegQueryValueExW(
    HKEY key, LPCWSTR value_name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD data_size)
{
    const DWORD capacity = data_size != nullptr ? *data_size : 0;
    const LSTATUS result = g_reg_query_w(key, value_name, reserved, type, data, data_size);
    if (!g_enabled.load(std::memory_order_relaxed) || result != ERROR_SUCCESS || value_name == nullptr ||
        data == nullptr || data_size == nullptr || (type != nullptr && *type != REG_SZ && *type != REG_EXPAND_SZ))
    {
        return result;
    }
    const auto replacement = RegistryReplacement(value_name, reinterpret_cast<const wchar_t*>(data));
    if (replacement.empty())
    {
        return result;
    }
    const DWORD required = static_cast<DWORD>((replacement.size() + 1) * sizeof(wchar_t));
    if (capacity < required)
    {
        *data_size = required;
        return ERROR_MORE_DATA;
    }
    memcpy(data, replacement.c_str(), required);
    *data_size = required;
    if (EqualsIgnoreCase(value_name, L"Language"))
    {
        g_language_overrides.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        g_locale_overrides.fetch_add(1, std::memory_order_relaxed);
    }
    return ERROR_SUCCESS;
}

HANDLE WINAPI HookCreateFileA(
    LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation,
    DWORD attributes, HANDLE template_file)
{
    RecordPath(path);
    const unsigned trace_index = g_file_open_count.fetch_add(1, std::memory_order_relaxed);
    if (g_enabled.load(std::memory_order_relaxed) && path != nullptr)
    {
        if (g_unpacked_candidate.load(std::memory_order_acquire) &&
            g_unpacked_language_textures_memory != nullptr && IsGlobalMemoryFilePath(path))
        {
            const HANDLE shadow = OpenRelocatedGlobalMemoryFile(path, g_create_file_a);
            if (shadow != INVALID_HANDLE_VALUE)
            {
                return shadow;
            }
            Log("ERROR", "UNPACKED_GLOBAL_MEMORY_SHADOW_FAILED_A path=%s", path);
        }
        const auto redirected = RedirectPath(path, g_redirect_movies.load(std::memory_order_relaxed));
        if (!redirected.empty())
        {
            const HANDLE handle = g_create_file_a(
                redirected.c_str(), access, share, security, creation, attributes, template_file);
            if (handle != INVALID_HANDLE_VALUE)
            {
                if (redirected.find("Japanese.bin") != std::string::npos)
                {
                    g_text_redirects.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    g_movie_redirects.fetch_add(1, std::memory_order_relaxed);
                }
                if (trace_index < 512)
                {
                    Log("TRACE", "CREATE_FILE_A index=%u redirected=%s source=%s handle=%p",
                        trace_index, redirected.c_str(), path, handle);
                }
                return handle;
            }
        }
    }
    const HANDLE handle = g_create_file_a(path, access, share, security, creation, attributes, template_file);
    const DWORD error = GetLastError();
    if (trace_index < 512)
    {
        Log("TRACE", "CREATE_FILE_A index=%u path=%s handle=%p error=%lu", trace_index,
            path != nullptr ? path : "(null)", handle, error);
    }
    if (handle != INVALID_HANDLE_VALUE && IsJapaneseSpeechPath(path))
    {
        g_japanese_speech_opens.fetch_add(1, std::memory_order_relaxed);
        Log("TRACE", "JAPANESE_SPEECH_OPEN_A path=%s", path);
    }
    SetLastError(error);
    return handle;
}

HANDLE WINAPI HookCreateFileW(
    LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation,
    DWORD attributes, HANDLE template_file)
{
    const unsigned trace_index = g_file_open_count.fetch_add(1, std::memory_order_relaxed);
    if (g_enabled.load(std::memory_order_relaxed) && path != nullptr)
    {
        if (g_unpacked_candidate.load(std::memory_order_acquire) &&
            g_unpacked_language_textures_memory != nullptr && IsGlobalMemoryFilePath(path))
        {
            const HANDLE shadow = OpenRelocatedGlobalMemoryFile(path, g_create_file_w);
            if (shadow != INVALID_HANDLE_VALUE)
            {
                return shadow;
            }
            Log("ERROR", "UNPACKED_GLOBAL_MEMORY_SHADOW_FAILED_W path=%ls", path);
        }
        const auto redirected = RedirectPath(path, g_redirect_movies.load(std::memory_order_relaxed));
        if (!redirected.empty())
        {
            const HANDLE handle = g_create_file_w(
                redirected.c_str(), access, share, security, creation, attributes, template_file);
            if (handle != INVALID_HANDLE_VALUE)
            {
                if (redirected.find(L"Japanese.bin") != std::wstring::npos)
                {
                    g_text_redirects.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    g_movie_redirects.fetch_add(1, std::memory_order_relaxed);
                }
                if (trace_index < 512)
                {
                    Log("TRACE", "CREATE_FILE_W index=%u redirected=%ls source=%ls handle=%p",
                        trace_index, redirected.c_str(), path, handle);
                }
                return handle;
            }
        }
    }
    const HANDLE handle = g_create_file_w(path, access, share, security, creation, attributes, template_file);
    const DWORD error = GetLastError();
    if (trace_index < 512)
    {
        Log("TRACE", "CREATE_FILE_W index=%u path=%ls handle=%p error=%lu", trace_index,
            path != nullptr ? path : L"(null)", handle, error);
    }
    if (handle != INVALID_HANDLE_VALUE && IsJapaneseSpeechPath(path))
    {
        g_japanese_speech_opens.fetch_add(1, std::memory_order_relaxed);
        Log("TRACE", "JAPANESE_SPEECH_OPEN_W path=%ls", path);
    }
    SetLastError(error);
    return handle;
}

HANDLE WINAPI HookFindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
{
    HANDLE handle = g_find_first_file_a(pattern, data);
    if (handle == INVALID_HANDLE_VALUE || data == nullptr || !IsUnsupportedEnumeratedLanguage(data->cFileName))
    {
        return handle;
    }
    if (g_find_next_file_a == nullptr)
    {
        g_find_next_file_a = reinterpret_cast<FindNextFileAFn>(
            ::GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FindNextFileA"));
        if (g_find_next_file_a == nullptr)
        {
            return handle;
        }
    }
    do
    {
        g_hidden_language_files.fetch_add(1, std::memory_order_relaxed);
        if (!g_find_next_file_a(handle, data))
        {
            FindClose(handle);
            return INVALID_HANDLE_VALUE;
        }
    } while (IsUnsupportedEnumeratedLanguage(data->cFileName));
    return handle;
}

BOOL WINAPI HookFindNextFileA(HANDLE handle, LPWIN32_FIND_DATAA data)
{
    BOOL result = FALSE;
    do
    {
        result = g_find_next_file_a(handle, data);
        if (!result || data == nullptr || !IsUnsupportedEnumeratedLanguage(data->cFileName))
        {
            return result;
        }
        g_hidden_language_files.fetch_add(1, std::memory_order_relaxed);
    } while (result);
    return result;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR procedure_name)
{
    const FARPROC resolved = g_get_proc_address(module, procedure_name);
    if (procedure_name == nullptr || IS_INTRESOURCE(procedure_name))
    {
        return resolved;
    }
    if (strcmp(procedure_name, "RegQueryValueExA") == 0 && resolved != nullptr)
    {
        if (g_external_registry_bootstrap.load(std::memory_order_acquire))
        {
            return resolved;
        }
        if (!g_inline_hooks.load(std::memory_order_acquire))
        {
            g_reg_query_a = reinterpret_cast<RegQueryValueExAFn>(resolved);
        }
        return reinterpret_cast<FARPROC>(&HookRegQueryValueExA);
    }
    if (strcmp(procedure_name, "RegQueryValueExW") == 0 && resolved != nullptr)
    {
        if (g_external_registry_bootstrap.load(std::memory_order_acquire))
        {
            return resolved;
        }
        if (!g_inline_hooks.load(std::memory_order_acquire))
        {
            g_reg_query_w = reinterpret_cast<RegQueryValueExWFn>(resolved);
        }
        return reinterpret_cast<FARPROC>(&HookRegQueryValueExW);
    }
    if (strcmp(procedure_name, "CreateFileA") == 0 && resolved != nullptr)
    {
        g_create_file_a = reinterpret_cast<CreateFileAFn>(resolved);
        return reinterpret_cast<FARPROC>(&HookCreateFileA);
    }
    if (strcmp(procedure_name, "CreateFileW") == 0 && resolved != nullptr)
    {
        g_create_file_w = reinterpret_cast<CreateFileWFn>(resolved);
        return reinterpret_cast<FARPROC>(&HookCreateFileW);
    }
    if (strcmp(procedure_name, "FindFirstFileA") == 0 && resolved != nullptr)
    {
        g_find_first_file_a = reinterpret_cast<FindFirstFileAFn>(resolved);
        return reinterpret_cast<FARPROC>(&HookFindFirstFileA);
    }
    if (strcmp(procedure_name, "FindNextFileA") == 0 && resolved != nullptr)
    {
        g_find_next_file_a = reinterpret_cast<FindNextFileAFn>(resolved);
        return reinterpret_cast<FARPROC>(&HookFindNextFileA);
    }
    return resolved;
}

bool TryReloadJapaneseText(int* before, int* after) noexcept
{
    __try
    {
        *before = *reinterpret_cast<volatile int*>(kCurrentLanguageAddress);
        g_set_current_language(-1);
        g_set_current_language(0);
        *after = *reinterpret_cast<volatile int*>(kCurrentLanguageAddress);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

int __fastcall HookBootFlowChange(void* self, void*, const int next_screen)
{
    unsigned expected = 0;
    if (g_runtime_japanese_text.load(std::memory_order_acquire) &&
        g_runtime_text_reload_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
    {
        int before = -999;
        int after = -999;
        Log("INFO", "RUNTIME_JAPANESE_TEXT_RELOAD_BEGIN next_screen=%d", next_screen);
        const bool reloaded = TryReloadJapaneseText(&before, &after);
        g_runtime_text_reload_state.store(reloaded ? 2u : 3u, std::memory_order_release);
        Log(reloaded ? "INFO" : "ERROR",
            "RUNTIME_JAPANESE_TEXT_RELOAD_%s current_before=%d current_after=%d text_redirects=%u",
            reloaded ? "COMPLETE" : "FAILED", before, after, g_text_redirects.load());
    }
    return g_boot_flow_change(self, next_screen);
}

bool InstallInlineHooks(const bool install_runtime_text_hook) noexcept
{
    const bool external_registry = g_external_registry_bootstrap.load(std::memory_order_acquire);
    if (MH_Initialize() != MH_OK)
    {
        return false;
    }

    MH_STATUS registry_a = MH_OK;
    MH_STATUS registry_w = MH_OK;
    if (!external_registry)
    {
        registry_a = MH_CreateHookApi(
            L"advapi32.dll", "RegQueryValueExA", reinterpret_cast<void*>(&HookRegQueryValueExA),
            reinterpret_cast<void**>(&g_reg_query_a));
        registry_w = MH_CreateHookApi(
            L"advapi32.dll", "RegQueryValueExW", reinterpret_cast<void*>(&HookRegQueryValueExW),
            reinterpret_cast<void**>(&g_reg_query_w));
    }
    const MH_STATUS file_a = MH_CreateHookApi(
        L"kernel32.dll", "CreateFileA", reinterpret_cast<void*>(&HookCreateFileA),
        reinterpret_cast<void**>(&g_create_file_a));
    const MH_STATUS file_w = MH_CreateHookApi(
        L"kernel32.dll", "CreateFileW", reinterpret_cast<void*>(&HookCreateFileW),
        reinterpret_cast<void**>(&g_create_file_w));
    const MH_STATUS find_first_a = MH_CreateHookApi(
        L"kernel32.dll", "FindFirstFileA", reinterpret_cast<void*>(&HookFindFirstFileA),
        reinterpret_cast<void**>(&g_find_first_file_a));
    const MH_STATUS find_next_a = MH_CreateHookApi(
        L"kernel32.dll", "FindNextFileA", reinterpret_cast<void*>(&HookFindNextFileA),
        reinterpret_cast<void**>(&g_find_next_file_a));
    MH_STATUS boot_flow = MH_OK;
    if (install_runtime_text_hook)
    {
        if (memcmp(reinterpret_cast<const void*>(kBootFlowChangeAddress),
                   kBootFlowChangeSignature.data(), kBootFlowChangeSignature.size()) != 0)
        {
            MH_Uninitialize();
            return false;
        }
        g_set_current_language = reinterpret_cast<SetCurrentLanguageFn>(kSetCurrentLanguageAddress);
        boot_flow = MH_CreateHook(
            reinterpret_cast<void*>(kBootFlowChangeAddress), reinterpret_cast<void*>(&HookBootFlowChange),
            reinterpret_cast<void**>(&g_boot_flow_change));
    }
    if (registry_a != MH_OK || registry_w != MH_OK || file_a != MH_OK || file_w != MH_OK ||
        find_first_a != MH_OK || find_next_a != MH_OK || boot_flow != MH_OK)
    {
        MH_Uninitialize();
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        MH_Uninitialize();
        return false;
    }
    g_inline_hooks.store(true, std::memory_order_release);
    return true;
}

bool InstallHooks() noexcept
{
    g_get_proc_address_slot = FindImportSlot("KERNEL32.dll", "GetProcAddress");
    g_reg_query_a_slot = FindImportSlot("ADVAPI32.dll", "RegQueryValueExA");
    g_reg_query_w_slot = FindImportSlot("ADVAPI32.dll", "RegQueryValueExW");
    g_create_file_a_slot = FindImportSlot("KERNEL32.dll", "CreateFileA");
    g_create_file_w_slot = FindImportSlot("KERNEL32.dll", "CreateFileW");
    bool installed = false;
    if (g_get_proc_address_slot != nullptr)
    {
        installed |= PatchPointer(
            g_get_proc_address_slot, reinterpret_cast<void*>(&HookGetProcAddress),
            reinterpret_cast<void**>(&g_get_proc_address));
    }
    if (g_reg_query_a_slot != nullptr)
    {
        installed |= PatchPointer(
            g_reg_query_a_slot, reinterpret_cast<void*>(&HookRegQueryValueExA),
            reinterpret_cast<void**>(&g_reg_query_a));
    }
    if (g_reg_query_w_slot != nullptr)
    {
        installed |= PatchPointer(
            g_reg_query_w_slot, reinterpret_cast<void*>(&HookRegQueryValueExW),
            reinterpret_cast<void**>(&g_reg_query_w));
    }
    if (g_create_file_a_slot != nullptr)
    {
        installed |= PatchPointer(
            g_create_file_a_slot, reinterpret_cast<void*>(&HookCreateFileA),
            reinterpret_cast<void**>(&g_create_file_a));
    }
    if (g_create_file_w_slot != nullptr)
    {
        installed |= PatchPointer(
            g_create_file_w_slot, reinterpret_cast<void*>(&HookCreateFileW),
            reinterpret_cast<void**>(&g_create_file_w));
    }
    return installed;
}

void RestoreHooks() noexcept
{
    if (g_get_proc_address_slot != nullptr && g_get_proc_address != nullptr)
    {
        PatchPointer(g_get_proc_address_slot, reinterpret_cast<void*>(g_get_proc_address), nullptr);
    }
    if (g_reg_query_a_slot != nullptr && g_reg_query_a != nullptr)
    {
        PatchPointer(g_reg_query_a_slot, reinterpret_cast<void*>(g_reg_query_a), nullptr);
    }
    if (g_reg_query_w_slot != nullptr && g_reg_query_w != nullptr)
    {
        PatchPointer(g_reg_query_w_slot, reinterpret_cast<void*>(g_reg_query_w), nullptr);
    }
    if (g_create_file_a_slot != nullptr && g_create_file_a != nullptr)
    {
        PatchPointer(g_create_file_a_slot, reinterpret_cast<void*>(g_create_file_a), nullptr);
    }
    if (g_create_file_w_slot != nullptr && g_create_file_w != nullptr)
    {
        PatchPointer(g_create_file_w_slot, reinterpret_cast<void*>(g_create_file_w), nullptr);
    }
}

bool ReadFileSha256(const std::filesystem::path& path, std::string& result,
                    std::string& normalized_result, std::uint64_t& size)
{
    result.clear();
    normalized_result.clear();
    size = 0;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER file_size{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr, normalized_hash = nullptr;
    DWORD object_length = 0, hash_length = 0, received = 0;
    std::uint64_t file_offset = 0;
    std::vector<unsigned char> object, normalized_object, digest, normalized_digest,
        buffer(64 * 1024), normalized_buffer(64 * 1024);
    bool success = false;
    if (!GetFileSizeEx(file, &file_size) ||
        !BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length), &received, 0)) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length), &received, 0)))
    {
        goto cleanup;
    }
    size = static_cast<std::uint64_t>(file_size.QuadPart);
    object.resize(object_length);
    normalized_object.resize(object_length);
    digest.resize(hash_length);
    normalized_digest.resize(hash_length);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(), object_length, nullptr, 0, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &normalized_hash, normalized_object.data(),
                                        object_length, nullptr, 0, 0)))
    {
        goto cleanup;
    }
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            goto cleanup;
        }
        if (read == 0)
        {
            break;
        }
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0)))
        {
            goto cleanup;
        }
        std::copy_n(buffer.begin(), read, normalized_buffer.begin());
        const std::uint64_t chunk_end = file_offset + read;
        for (const auto& range : kNoCdMutableFileRanges)
        {
            const std::uint64_t overlap_begin = (std::max)(file_offset, range.begin);
            const std::uint64_t overlap_end = (std::min)(chunk_end, range.end);
            if (overlap_begin < overlap_end)
            {
                std::fill(normalized_buffer.begin() + static_cast<std::ptrdiff_t>(overlap_begin - file_offset),
                          normalized_buffer.begin() + static_cast<std::ptrdiff_t>(overlap_end - file_offset),
                          static_cast<unsigned char>(0));
            }
        }
        if (!BCRYPT_SUCCESS(BCryptHashData(normalized_hash, normalized_buffer.data(), read, 0)))
        {
            goto cleanup;
        }
        file_offset = chunk_end;
    }
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), hash_length, 0)) ||
        !BCRYPT_SUCCESS(BCryptFinishHash(normalized_hash, normalized_digest.data(), hash_length, 0)))
    {
        goto cleanup;
    }
    result.resize(digest.size() * 2);
    normalized_result.resize(normalized_digest.size() * 2);
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        result[index * 2] = hex[digest[index] >> 4u];
        result[index * 2 + 1] = hex[digest[index] & 15u];
        normalized_result[index * 2] = hex[normalized_digest[index] >> 4u];
        normalized_result[index * 2 + 1] = hex[normalized_digest[index] & 15u];
    }
    success = true;
cleanup:
    if (normalized_hash != nullptr) BCryptDestroyHash(normalized_hash);
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return success;
}

DWORD WINAPI Bootstrap(void*)
{
    const bool inline_hooks = g_inline_hooks.load(std::memory_order_acquire);
    const auto module_path = ModulePath(g_module);
    auto log_path = module_path;
    log_path.replace_extension(L".log");
    g_log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Log("INFO", "NFSMWJapaneseBridge v%s bootstrap resolver_hook=%s inline_hooks=%s external_registry=%s registry_a=%s file_a=%s", kVersion,
        g_get_proc_address != nullptr ? "true" : "false", inline_hooks ? "true" : "false",
        g_external_registry_bootstrap.load(std::memory_order_acquire) ? "true" : "false",
        g_reg_query_a != nullptr ? "true" : "false", g_create_file_a != nullptr ? "true" : "false");
    const unsigned early_count = (std::min)(g_early_path_count.load(std::memory_order_relaxed),
                                            static_cast<unsigned>(g_early_paths.size()));
    for (unsigned index = 0; index < early_count; ++index)
    {
        Log("TRACE", "EARLY_CREATE_FILE_A index=%u path=%s", index, g_early_paths[index].data());
    }

    if (!inline_hooks && !g_external_registry_bootstrap.load(std::memory_order_acquire) &&
        g_get_proc_address == nullptr)
    {
        g_enabled.store(false, std::memory_order_release);
        RestoreHooks();
        Log("ERROR", "INLINE_HOOK_INSTALL_FAILED");
        return 0;
    }

    std::string hash, normalized_hash;
    std::uint64_t size = 0;
    const auto executable = ModulePath(nullptr);
    const bool hashed = ReadFileSha256(executable, hash, normalized_hash, size);
    const bool original_no_cd = hashed && size == kSupportedSize && hash == kSupportedSha256;
    const bool bootstrap_validated = g_external_registry_bootstrap.load(std::memory_order_acquire);
    const bool normalized_no_cd = hashed && size == kSupportedSize &&
                                  normalized_hash == kSupportedNormalizedSha256 && bootstrap_validated;
    const bool protected_no_cd = original_no_cd || normalized_no_cd;
    const bool unpacked_original = hashed && size == kUnpackedCandidateSize && hash == kUnpackedCandidateSha256;
    const bool unpacked_stock_shaders = hashed && size == kUnpackedStockShaderSize &&
                                        (hash == kUnpackedStockShaderSha256 ||
                                         hash == kUnpackedStockIconSha256);
    const bool nfspatcher = hashed && size == 6029312ull && (hash == "80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C" || hash == "B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06");
    const bool unpacked_candidate = unpacked_original || unpacked_stock_shaders || nfspatcher;
    g_unpacked_candidate.store(unpacked_candidate, std::memory_order_release);
    if (!protected_no_cd && !unpacked_candidate)
    {
        g_enabled.store(false, std::memory_order_release);
        RestoreHooks();
        Log("ERROR", "UNSUPPORTED_EXECUTABLE size=%llu sha256=%s normalized_sha256=%s bootstrap_validated=%s",
            static_cast<unsigned long long>(size), hash.c_str(), normalized_hash.c_str(),
            bootstrap_validated ? "true" : "false");
        return 0;
    }
    Log("INFO", "EXECUTABLE_ACCEPTED size=%llu sha256=%s normalized_sha256=%s identity=%s",
        static_cast<unsigned long long>(size), hash.c_str(), normalized_hash.c_str(),
        original_no_cd ? "original_no_cd" :
            (normalized_no_cd ? "self_modified_no_cd" :
                (unpacked_stock_shaders ? "unpacked_stock_shaders" : "unpacked_candidate")));
    if (unpacked_candidate && !ReserveUnpackedJapaneseLanguageTexturesMemory())
    {
        g_enabled.store(false, std::memory_order_release);
        RestoreHooks();
        Log("ERROR", "UNPACKED_LANGUAGE_TEXTURES_RESERVE_REQUIRED");
        return 0;
    }
    if (unpacked_candidate && !InstallUnpackedJapaneseLanguageSlot())
    {
        g_runtime_japanese_text.store(false, std::memory_order_release);
        Log("ERROR", "UNPACKED_LANGUAGE_SLOT_PATCH_FAILED runtime_reload_disabled=true");
    }
    const auto ini_path = module_path.parent_path() / L"NFSMWJapaneseBridge.ini";
    g_enabled.store(GetPrivateProfileIntW(L"Main", L"Enabled", 1, ini_path.c_str()) != 0,
                    std::memory_order_release);
    g_redirect_movies.store(GetPrivateProfileIntW(L"Main", L"RedirectMovies", 1, ini_path.c_str()) != 0,
                            std::memory_order_release);
    g_runtime_japanese_text.store(
        !unpacked_candidate &&
            GetPrivateProfileIntW(L"Main", L"RuntimeJapaneseText", 0, ini_path.c_str()) != 0,
        std::memory_order_release);
    g_crash_diagnostics.store(
        GetPrivateProfileIntW(L"Diagnostics", L"CrashDiagnostics", 0, ini_path.c_str()) != 0,
        std::memory_order_release);
    if (g_crash_diagnostics.load(std::memory_order_acquire) && g_exception_handler == nullptr)
    {
        g_exception_handler = AddVectoredExceptionHandler(1, &RecordUnhandledException);
        Log("INFO", "CRASH_DIAGNOSTICS enabled=true max_detailed_exceptions=4 handler=%s",
            g_exception_handler != nullptr ? "installed" : "failed");
    }
    if (!g_enabled.load(std::memory_order_acquire))
    {
        RestoreHooks();
        Log("INFO", "DISABLED_BY_CONFIG");
        return 0;
    }
    const auto japanese_bin = module_path.parent_path().parent_path() / L"LANGUAGES" / L"Japanese.bin";
    if (GetFileAttributesW(japanese_bin.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        g_enabled.store(false, std::memory_order_release);
        RestoreHooks();
        Log("ERROR", "JAPANESE_BIN_MISSING path=%ls", japanese_bin.c_str());
        return 0;
    }
    Sleep(2000);
    const bool delayed_inline_hooks = InstallInlineHooks(g_runtime_japanese_text.load(std::memory_order_acquire));
    if (!delayed_inline_hooks && g_runtime_japanese_text.load(std::memory_order_acquire))
    {
        g_runtime_text_reload_state.store(3, std::memory_order_release);
    }
    Log("INFO", "EXECUTABLE_ACCEPTED size=%llu sha256=%s movies=%s runtime_japanese_text=%s",
        static_cast<unsigned long long>(size), hash.c_str(), g_redirect_movies.load() ? "true" : "false",
        g_runtime_japanese_text.load() ? "true" : "false");
    Log("INFO", "DELAYED_INLINE_HOOKS installed=%s", delayed_inline_hooks ? "true" : "false");
    Log("INFO", "HOOK_ACTIVITY registry_queries=%u last_registry_value=%s language=%u locale=%u text=%u speech=%u movies=%u hidden_languages=%u global_memory_shadows=%u",
        g_registry_query_count.load(), g_last_registry_value.data(), g_language_overrides.load(),
        g_locale_overrides.load(), g_text_redirects.load(), g_japanese_speech_opens.load(),
        g_movie_redirects.load(),
        g_hidden_language_files.load(), g_global_memory_shadows.load());
    return 0;
}

}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    using namespace nfsmw_japanese_bridge;
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        if (LooksLikeSupportedImage())
        {
            const HMODULE version = GetModuleHandleW(L"version.dll");
            using BootstrapActiveFn = BOOL(WINAPI*)();
            const auto bootstrap_active = version == nullptr ? nullptr :
                reinterpret_cast<BootstrapActiveFn>(::GetProcAddress(version, "NFSMWJapaneseBootstrapActive"));
            g_external_registry_bootstrap.store(bootstrap_active != nullptr && bootstrap_active() != FALSE,
                                                std::memory_order_release);
            InstallHooks();
            const HANDLE thread = CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
            if (thread != nullptr) CloseHandle(thread);
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log("INFO", "PROCESS_DETACH mode=%s language=%u locale=%u text=%u speech=%u movies=%u hidden_languages=%u",
            reserved != nullptr ? "termination" : "unload", g_language_overrides.load(),
            g_locale_overrides.load(), g_text_redirects.load(), g_japanese_speech_opens.load(),
            g_movie_redirects.load(),
            g_hidden_language_files.load());
        if (reserved == nullptr) RestoreHooks();
        if (reserved == nullptr && g_exception_handler != nullptr)
        {
            RemoveVectoredExceptionHandler(g_exception_handler);
        }
        if (reserved == nullptr && g_inline_hooks.load(std::memory_order_acquire))
        {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
        }
        if (g_log != INVALID_HANDLE_VALUE && g_log != nullptr) CloseHandle(g_log);
    }
    return TRUE;
}
