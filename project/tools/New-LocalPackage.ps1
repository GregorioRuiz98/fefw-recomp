param(
    [string]$Work = (Join-Path $PSScriptRoot '../../../work'),
    [string]$Dump = '',
    [string]$Out = ''
)
$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
if (!$Out) { $Out = Join-Path $Work 'Fire Emblem Recomp' }
$Out = [IO.Path]::GetFullPath($Out)
if (!$Dump) {
    $Dump = (Get-Content -LiteralPath (Join-Path $Work 'fire-emblem-local.json') -Raw | ConvertFrom-Json).dump_root
}
$romfs = Join-Path ([IO.Path]::GetFullPath($Dump)) 'romfs'
$build = Join-Path $Work 'build/fefw'
$exefs = Join-Path $Work 'prepared/exefs'
$shaders = Join-Path $Work 'shaders/validated-sdk22'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
foreach ($required in @((Join-Path $build 'fefw.exe'), (Join-Path $exefs 'main'), $romfs, $shaders)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing local resource: $required" }
}
$shaderFiles = @(Get-ChildItem -LiteralPath $shaders -Filter '*.spv' -File)
if ($shaderFiles.Count -ne 11977) { throw "La cache no contiene los 11977 shaders esperados: $($shaderFiles.Count)" }
New-Item -ItemType Directory -Path $Out -Force | Out-Null
if (Get-Process fefw -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $Out 'fefw.exe') }) { throw 'Close the game before updating the package.' }
foreach ($name in @('exefs', 'shader_cache/spirv', 'fonts', 'save', 'licenses', 'tools')) {
    New-Item -ItemType Directory -Path (Join-Path $Out $name) -Force | Out-Null
}
foreach ($name in @('fefw.exe', 'fefw_setup.exe', 'fefw.map', 'fefw.pdb', 'astcdec-avx2-shared.dll', 'astcdec-sse2-shared.dll', 'astcdec-sse4.1-shared.dll')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $Out $name) -Force
}
Copy-Item -LiteralPath (Join-Path $build 'fonts/DejaVuSans.ttf') -Destination (Join-Path $Out 'fonts') -Force
foreach ($file in Get-ChildItem -LiteralPath $exefs -File) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Out 'exefs') -Force
}
foreach ($file in Get-ChildItem -LiteralPath $shaders -File) {
    $target = Join-Path $Out "shader_cache/spirv/$($file.Name)"
    if (!(Test-Path -LiteralPath $target) -or (Get-Item -LiteralPath $target).Length -ne $file.Length -or
        (Get-Item -LiteralPath $target).LastWriteTimeUtc -ne $file.LastWriteTimeUtc) {
        [IO.File]::Copy($file.FullName, $target, $true)
        [IO.File]::SetLastWriteTimeUtc($target, $file.LastWriteTimeUtc)
    }
}
foreach ($name in @('Prepare-Game.ps1', 'Prepare-Dependencies.ps1')) {
    $script = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name))
    [IO.File]::WriteAllText((Join-Path $Out "tools/$name"), $script, [Text.UTF8Encoding]::new($true))
}
@{ work = $Work; project = $source } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Out 'tools/local-build.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $Out 'LICENSE') -Force
Copy-Item -LiteralPath (Join-Path $source 'CONTROLES-MANDO.md') -Destination (Join-Path $Out 'CONTROLES-MANDO.md') -Force
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $source 'licenses') -File) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Out 'licenses') -Force
}
foreach ($dir in @('runtime/assets/fonts', 'tools/textures/licenses', 'tools/tiling/licenses')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Work "engine/$dir") -File -Filter 'LICENSE*') {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Out 'licenses') -Force
    }
}
foreach ($dir in @('sdl3-src', 'imgui-src')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $build "_deps/$dir") -File -Filter 'LICENSE*') {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Out "licenses/$dir-$($file.Name)") -Force
    }
}
$ini = @"
; Experimental local configuration. Does not modify the original dump.
[fs]
exefs = exefs
romfs = $romfs
save_dir = save

[boot]
gfx = window
max_seconds = 0
link = rtld

[play]
service_input_fallback = 1
capture_mouse = 0
quit_on_close = 1

[nvn]
shader_cache = shader_cache/spirv
dlss_spatial_fallback = 1

[audio]
revision16_probe = 1

[title]
account_save_size = 67108864

[diag]
watchdog_seconds = -1
"@
# Keep the player's settings and saves when refreshing the executable.
$iniPath = Join-Path $Out 'fefw.ini'
[IO.File]::WriteAllText((Join-Path $Out 'fefw.features'), 'fefw-runtime-4', [Text.UTF8Encoding]::new($false))
if (!(Test-Path -LiteralPath $iniPath)) {
    [IO.File]::WriteAllText($iniPath, $ini, [Text.UTF8Encoding]::new($false))
}
$readme = @"
FIRE EMBLEM RECOMP - EXPERIMENTAL NATIVE PROTOTYPE

Double-click fefw.exe. On the first launch it opens the setup tool.
Choose the game folder, window resolution, fullscreen mode and game language.
Enable "Skip setup on startup" to open the game directly afterward.
fefw_setup.exe always lets you change settings, even with that option enabled.
The prepared local package needs no build tools for settings changes.
The game opens without a console, time limit or automatic button presses.
The first launch may take time while the driver prepares graphics pipelines.

CONTROLS
A controller is required for gameplay. Keyboard and mouse do not send game actions.
F1: runtime settings; F2: FPS counter; Alt+Enter: fullscreen
Controller: automatic detection, including when connected during gameplay.
Xbox: A confirms, B goes back, Menu/Start is PLUS; LB/RB and LT/RT are L/R and ZL/ZR.
PlayStation: Cross confirms, Circle goes back, Options is PLUS.
Nintendo Pro: A confirms, B goes back, + is PLUS.
Both sticks, stick clicks and the D-pad are mapped. See CONTROLES-MANDO.md.
Use F1 > Settings > Controls > Gamepad > Rescan controllers if needed.
Close the settings overlay to return controller input to the game.
Close the game with the window X or Alt+F4.
Controller mappings and window options are stored in controls.ini after the first launch.

OPTIONS
Experimental REV16 audio, spatial DLSS substitute and an isolated 64 MiB local save.
Optional 60 FPS (Kompas9, experimental) is available in fefw_setup.exe.
Save and restart to apply it. It is disabled by default and does not guarantee 60 FPS.
Saves are stored in the save subfolder; diagnostics are in fefw.log.
Saving/loading and complete combat have not yet been verified.
A Vulkan device loss has been observed during extended video playback.
Temporal DLSS reconstruction, complete stability and fidelity are not guaranteed.

FILES
Keep fefw.exe, fefw.ini, fefw.features, DLLs, exefs, fonts and shader_cache together.
The original romfs folder is still required at:
$romfs
The other required data is copied into this private folder.
Requires Windows x64 and a graphics driver supporting Vulkan 1.3.
This is a private package containing data derived from your local game copy.
Source and modifications used: $source
Base engine: drakolordx7/pokopia-recomp, commit 2c29515b23a6d78d967775fd7e9f24f50402cca7.
"@
[IO.File]::WriteAllText((Join-Path $Out 'README.txt'), $readme, [Text.UTF8Encoding]::new($false))
$manifest = [ordered]@{
    created_utc = [DateTime]::UtcNow.ToString('o')
    executable = 'fefw.exe'
    configurator = 'fefw_setup.exe'
    configurator_sha256 = (Get-FileHash -LiteralPath (Join-Path $Out 'fefw_setup.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $Out 'fefw.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    architecture = 'Windows x64'; subsystem = 'Windows GUI'
    shader_modules = $shaderFiles.Count
    romfs_external = $romfs
    settings_preserved_if_existing = $true
    controller_detection = 'automatic SDL gamepad'
    controller_instructions = 'CONTROLES-MANDO.md'
    gameplay_verified = $false
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'package.json') -Encoding utf8
Write-Host "Local package ready: $(Join-Path $Out 'fefw.exe')"
