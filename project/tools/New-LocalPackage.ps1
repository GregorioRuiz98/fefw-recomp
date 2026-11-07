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
    if (!(Test-Path -LiteralPath $required)) { throw "Falta el recurso local: $required" }
}
$shaderFiles = @(Get-ChildItem -LiteralPath $shaders -Filter '*.spv' -File)
if ($shaderFiles.Count -ne 11977) { throw "La cache no contiene los 11977 shaders esperados: $($shaderFiles.Count)" }
New-Item -ItemType Directory -Path $Out -Force | Out-Null
if (Get-Process fefw -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $Out 'fefw.exe') }) { throw 'Cierra el juego antes de actualizar el paquete.' }
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
; Configuracion local experimental. No modifica el volcado original.
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
if (!(Test-Path -LiteralPath $iniPath)) {
    [IO.File]::WriteAllText($iniPath, $ini, [Text.UTF8Encoding]::new($false))
}
$readme = @"
FIRE EMBLEM RECOMP - PROTOTIPO NATIVO EXPERIMENTAL

Abre fefw.exe con doble clic. La primera vez muestra el configurador.
Elige carpeta del juego, resolucion de ventana, pantalla completa e idioma.
Activa "No mostrar la configuracion al iniciar" para abrir luego el juego directamente.
fefw_setup.exe permite cambiar los ajustes cuando quieras, incluso con esa opcion activa.
El paquete local ya preparado no necesita herramientas de compilacion para cambiar sus ajustes.
El juego abre sin consola, sin limite de tiempo y sin pulsaciones automaticas.
El primer arranque puede tardar mientras el controlador prepara las tuberias graficas.

CONTROLES
Enter o Espacio: A (confirmar / avanzar)
Escape: B (volver)
X, Y: botones X e Y
P: PLUS (pausa / saltar video cuando el juego lo permite)
Tab: MINUS
Flechas: cruceta; WASD: stick izquierdo; IJKL: stick derecho
Q / E: L / R; 1 / 3: ZL / ZR; C / V: pulsar sticks
F1: ajustes del runtime; F2: contador de FPS; Alt+Enter: pantalla completa
Mando: deteccion automatica, incluso al conectarlo con el juego abierto.
Xbox: A confirma, B vuelve, Menu/Start es PLUS; LB/RB y LT/RT son L/R y ZL/ZR.
PlayStation: Cruz confirma, Circulo vuelve, Options es PLUS.
Nintendo Pro: A confirma, B vuelve, + es PLUS.
Ambos sticks, sus pulsaciones y la cruceta estan mapeados. Detalles en CONTROLES-MANDO.md.
Cierra con la X de la ventana o Alt+F4.
Los controles y la ventana se pueden ajustar en controls.ini, creado al primer arranque.

OPCIONES ACTIVAS
Audio REV16 experimental, sustituto espacial de DLSS y guardado local aislado de 64 MiB.
Los guardados quedan en la subcarpeta save; el diagnostico queda en fefw.log.
No se ha comprobado todavia guardar/cargar una partida ni el combate completo.
Se ha observado una perdida de dispositivo Vulkan durante un video largo.
No contiene reconstruccion DLSS temporal ni garantiza estabilidad o fidelidad completa.

ARCHIVOS
Manten fefw.exe, fefw.ini, las DLL, exefs, fonts y shader_cache juntos.
La carpeta romfs original sigue siendo necesaria en:
$romfs
El resto de los datos necesarios esta copiado en esta carpeta privada.
Requiere Windows x64 y un controlador grafico compatible con Vulkan 1.3.
No es un paquete publicado: contiene datos derivados de tu copia local del juego.
Codigo y modificaciones utilizados: $source
Motor base: drakolordx7/pokopia-recomp, commit 2c29515b23a6d78d967775fd7e9f24f50402cca7.
"@
[IO.File]::WriteAllText((Join-Path $Out 'LEEME.txt'), $readme, [Text.UTF8Encoding]::new($false))
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
Write-Host "Paquete local listo: $(Join-Path $Out 'fefw.exe')"
