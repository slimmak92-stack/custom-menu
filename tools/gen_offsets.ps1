# Generates src/menu_offsets.h from the plugin-sdk checkout in $env:PLUGIN_SDK_DIR.
# Needs an MSVC developer environment (cl.exe on PATH).
$ErrorActionPreference = "Continue"
$sdk = $env:PLUGIN_SDK_DIR
$out = "src/menu_offsets.h"
Write-Host "plugin-sdk: $sdk"

$inc = @("plugin_sa","plugin_sa\game_sa","plugin_sa\game_sa\enums","plugin_sa\game_sa\rw","shared","shared\game","stb","safetyhook") |
       ForEach-Object { "/I$sdk\$_" }
New-Item -ItemType Directory -Force build_tools | Out-Null
$cl = @("/nologo","/std:c++latest","/EHsc","/utf-8","/permissive-") + $inc + @("tools\offsets.cpp","/Fo:build_tools\","/Fe:build_tools\offsets.exe")
& cl.exe @cl 2>&1 | Select-Object -Last 25 | ForEach-Object { Write-Host $_ }

$header = @()
if (Test-Path build_tools\offsets.exe) {
    $header = & build_tools\offsets.exe
} else {
    Write-Host "offsets.exe was not built - keeping the default (all unavailable) header"
    exit 0
}

function FindAddr([string]$pattern) {
    foreach ($f in Get-ChildItem -Recurse -Path "$sdk\plugin_sa" -Include *.cpp,*.h -ErrorAction SilentlyContinue) {
        $t = Get-Content $f.FullName -Raw
        $m = [regex]::Match($t, $pattern, "Singleline")
        if ($m.Success) { Write-Host ("  {0} -> {1} ({2})" -f $pattern.Substring(0, [Math]::Min(40,$pattern.Length)), $m.Groups[1].Value, $f.Name); return $m.Groups[1].Value }
    }
    Write-Host "  not found: $pattern"
    return "0"
}
$call = '\s*\([^)]*\)\s*\{\s*(?:return\s+)?plugin::CallMethod(?:AndReturn)?\s*<\s*(?:[A-Za-z_]+\s*,\s*)?(0x[0-9A-Fa-f]+)'
$header += "#define ADDR_AUDIOENGINE " + (FindAddr 'AudioEngine\s*=\s*\*\s*\(\s*CAudioEngine\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)')
$header += "#define ADDR_SETFX " + (FindAddr ('CAudioEngine::SetEffectsMasterVolume' + $call))
$header += "#define ADDR_SETMUSIC " + (FindAddr ('CAudioEngine::SetMusicMasterVolume' + $call))
$header += "#define ADDR_LODSCALE " + (FindAddr 'ms_lodDistScale\s*=\s*\*\s*\(\s*float\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)')
$header += "#define ADDR_SAVESETTINGS " + (FindAddr ('CMenuManager::SaveSettings' + $call))
$header | Set-Content $out -Encoding ascii
Get-Content $out | ForEach-Object { Write-Host $_ }
