// Built and run by the GitHub workflow against the cloned plugin-sdk.
// Prints the byte offset/size of the CMenuManager settings fields (same names the Definitive UI uses).
#define GTASA
#define PLUGIN_SGV_10US
#define RW
#define _DX9_SDK_INSTALLED
#define _CRT_NON_CONFORMING_SWPRINTFS
#define GTAGAME_NAME "San Andreas"
#define GTAGAME_ABBR "SA"
#define GTAGAME_ABBRLOW "sa"
#define GTAGAME_PROTAGONISTNAME "CJ"
#define GTAGAME_CITYNAME "San Andreas"
#define TARGET_NAME R"(offsets)"
#include "plugin.h"
#include "CMenuManager.h"
#include <cstdio>
#include <cstddef>
#include <utility>

#define PROBE(m)                                                                          \
    template <class T> concept Has_##m = requires(T t) { t.m; };                           \
    template <class T> void P_##m()                                                       \
    {                                                                                     \
        if constexpr (Has_##m<T>)                                                         \
            std::printf("#define OFF_%s 0x%X\n#define SZ_%s %d\n", #m, (unsigned)offsetof(T, m), #m, \
                        (int)sizeof(std::declval<T&>().m));                               \
        else                                                                              \
            std::printf("#define OFF_%s -1\n#define SZ_%s 0\n", #m, #m);                   \
    }

PROBE(m_nPrefsSfxVolume)
PROBE(m_nPrefsMusicVolume)
PROBE(m_nPrefsBrightness)
PROBE(m_fPrefsLOD)
PROBE(m_bPrefsVsync)
PROBE(m_bPrefsMipMapping)
PROBE(m_bPrefsUseWideScreen)
PROBE(m_bPrefsShowHud)
PROBE(m_nPrefsRadarMode)
PROBE(m_bPrefsShowSubtitles)
PROBE(m_bPrefsRadioEq)
PROBE(m_bPrefsRadioAutoSelect)

int main()
{
    std::printf("#pragma once\n");
    P_m_nPrefsSfxVolume<CMenuManager>();
    P_m_nPrefsMusicVolume<CMenuManager>();
    P_m_nPrefsBrightness<CMenuManager>();
    P_m_fPrefsLOD<CMenuManager>();
    P_m_bPrefsVsync<CMenuManager>();
    P_m_bPrefsMipMapping<CMenuManager>();
    P_m_bPrefsUseWideScreen<CMenuManager>();
    P_m_bPrefsShowHud<CMenuManager>();
    P_m_nPrefsRadarMode<CMenuManager>();
    P_m_bPrefsShowSubtitles<CMenuManager>();
    P_m_bPrefsRadioEq<CMenuManager>();
    P_m_bPrefsRadioAutoSelect<CMenuManager>();
    return 0;
}
