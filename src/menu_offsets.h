// Where GTA San Andreas 1.0 US keeps its settings. FrontEndMenuManager lives at 0xBA6748; the
// numbers below are byte offsets inside it (plugin-sdk CMenuManager layout, cross-checked with an
// older SDK) plus a few function addresses (also used by MTA:SA). Every row validates the value it
// reads before it is shown or written, so a different game version just shows "Unavailable".
#pragma once
#define OFF_m_bPrefsShowHud 0x21
#define SZ_m_bPrefsShowHud 1
#define OFF_m_nPrefsRadarMode 0x24
#define SZ_m_nPrefsRadarMode 4
#define OFF_m_nPrefsBrightness 0x3C
#define SZ_m_nPrefsBrightness 4
#define OFF_m_fPrefsLOD 0x40
#define SZ_m_fPrefsLOD 4
#define OFF_m_bPrefsShowSubtitles 0x44
#define SZ_m_bPrefsShowSubtitles 1
#define OFF_m_bPrefsShowLegends 0x4A
#define SZ_m_bPrefsShowLegends 1
#define OFF_m_bPrefsUseWideScreen 0x4B
#define SZ_m_bPrefsUseWideScreen 1
#define OFF_m_bPrefsVsync 0x4C
#define SZ_m_bPrefsVsync 1
#define OFF_m_bPrefsRadioAutoSelect 0x4D
#define SZ_m_bPrefsRadioAutoSelect 1
#define OFF_m_nPrefsSfxVolume 0x4F
#define SZ_m_nPrefsSfxVolume 1
#define OFF_m_nPrefsMusicVolume 0x50
#define SZ_m_nPrefsMusicVolume 1
#define OFF_m_bPrefsRadioEq 0x51
#define SZ_m_bPrefsRadioEq 1
#define OFF_m_bPrefsMipMapping 0xC4
#define SZ_m_bPrefsMipMapping 1
#define ADDR_AUDIOENGINE 0xB6BC90
#define ADDR_SETFX 0x506E10
#define ADDR_SETMUSIC 0x506DE0
#define ADDR_SAVESETTINGS 0x57C660
#define ADDR_LODSCALE 0
