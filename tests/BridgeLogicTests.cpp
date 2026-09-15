#include "BridgeLogic.h"

#include <iostream>
#include <string>

namespace
{
int failures = 0;
void Check(const bool condition, const char* name)
{
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    if (!condition) ++failures;
}
}

int main()
{
    using namespace nfsmw_japanese_bridge;
    Check(RegistryReplacement("Language", "Japanese") == "English US", "ANSI language override");
    Check(RegistryReplacement(L"language", L"japanese") == L"English US", "Unicode language override");
    Check(RegistryReplacement("Locale", "ja") == "en-us", "ANSI locale override");
    Check(RegistryReplacement("Language", "English US").empty(), "English remains unchanged");
    Check(RedirectPath("LANGUAGES\\ENGLISH.BIN", true) == "LANGUAGES\\Japanese.bin", "text redirect");
    Check(RedirectPath("MOVIES\\intro_english_pal.vp6", true) ==
              "MOVIES\\intro_japanese_ntsc.vp6", "movie redirect");
    Check(RedirectPath("MOVIES\\ealogo_ENGLISH_ntsc.vp6", true) ==
              "MOVIES\\ealogo_japanese_ntsc.vp6", "NTSC movie redirect");
    Check(RedirectPath("MOVIES\\attract_movie_JAPANESE_pal.vp6", true) ==
              "MOVIES\\attract_movie_japanese_ntsc.vp6", "Japanese PAL movie redirect");
    Check(RedirectPath("MOVIES\\intro_english_pal.vp6", false).empty(), "movie redirect disabled");
    Check(RedirectPath("SOUND\\SPEECH\\copspeech.big", true).empty(), "audio path unchanged");
    Check(RedirectPath("MEMCARD\\LOCALE_ENGLISH.loc", true).empty(), "save locale unchanged");
    Check(ShouldHideLanguageFile("Japanese.bin"), "hide unsupported Japanese enumeration");
    Check(ShouldHideLanguageFile("THAI.BIN"), "hide unsupported Thai enumeration");
    Check(!ShouldHideLanguageFile("English.bin"), "keep supported English enumeration");
    return failures == 0 ? 0 : 1;
}
