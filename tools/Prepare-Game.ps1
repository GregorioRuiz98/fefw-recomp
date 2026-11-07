param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$Source,
    [string]$Work = '',
    [string]$Project = '',
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$GameDirectory = [IO.Path]::GetFullPath($GameDirectory)
$Source = [IO.Path]::GetFullPath($Source)
$profile = Join-Path $PSScriptRoot 'local-build.json'
if (Test-Path -LiteralPath $profile) {
    $private = Get-Content -LiteralPath $profile -Raw | ConvertFrom-Json
    if (!$Work) { $Work = $private.work }
    if (!$Project) { $Project = $private.project }
}
if (!$Work) { $Work = Join-Path $GameDirectory 'work' }
if (!$Project) { $Project = Join-Path $GameDirectory 'project' }
$Work = [IO.Path]::GetFullPath($Work)
$Project = [IO.Path]::GetFullPath($Project)
$temporaryDirectory = Join-Path $Work 'tmp'
New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
$env:TEMP = $temporaryDirectory; $env:TMP = $temporaryDirectory
$log = Join-Path $GameDirectory 'preparation.log'
$status = Join-Path $GameDirectory 'preparation.status'
if (-not ('FEFWPrepareNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FEFWPrepareNative {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool MoveFileEx(string existing, string replacement, uint flags);
}
'@
}
function Stage([string]$Message) {
    [IO.File]::WriteAllText($status, $Message, [Text.UTF8Encoding]::new($false))
    Write-Output $Message
}
function Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "La preparacion fallo ($LASTEXITCODE). Revisa preparation.log." }
}
function Action([string]$Name) {
    & (Join-Path $Project 'FireEmblemRecomp.ps1') -Action $Name -Work $Work -Dump $Source
    if ($LASTEXITCODE -ne 0) { throw "Fallo la fase $Name ($LASTEXITCODE)." }
}
function Copy-Local([string]$SourceFile, [string]$Target) {
    $temporary = $Target + '.preparing'
    [IO.File]::Copy($SourceFile, $temporary, $true)
    if (![FEFWPrepareNative]::MoveFileEx($temporary, $Target, 9)) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    [IO.File]::SetLastWriteTimeUtc($Target, [IO.File]::GetLastWriteTimeUtc($SourceFile))
}
try {
    if (Get-Process fefw -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $GameDirectory 'fefw.exe') }) { throw 'Cierra el juego antes de prepararlo.' }
    New-Item -ItemType Directory -Path $GameDirectory -Force | Out-Null
    Start-Transcript -LiteralPath $log -Force | Out-Null
    foreach ($tool in @('git','cmake','ninja','python')) {
        if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Falta $tool. Consulta los requisitos de README.md y vuelve a preparar." }
    }
    if (!(Test-Path -LiteralPath (Join-Path $Project 'FireEmblemRecomp.ps1'))) { throw 'Faltan los archivos project del paquete publico.' }
    . (Join-Path $Project 'tools/Initialize-BuildEnv.ps1')
    if ($CheckOnly) { Stage 'Herramientas principales disponibles.'; Stop-Transcript | Out-Null; exit 0 }
    Stage 'Preparando las herramientas locales…'
    if (!(Test-Path -LiteralPath (Join-Path $Work 'venv/Scripts/python.exe'))) { Action 'Bootstrap' }
    Stage 'Verificando y normalizando tu copia del juego…'
    Action 'Prepare'
    Stage 'Preparando las bibliotecas y el traductor de shaders…'
    & (Join-Path $Project 'tools/Prepare-Dependencies.ps1') -Work $Work -Project $Project
    Stage 'Traduciendo el codigo del juego para este PC…'
    & (Join-Path $Work 'venv/Scripts/python.exe') (Join-Path $Project 'tools/local_preparation.py') --work $Work
    if ($LASTEXITCODE -ne 0) { Action 'Cover'; Action 'Lift' }
    Action 'Configure'; Action 'Build'
    $cache = Join-Path $Work 'shaders/validated-sdk22'
    if (!(Test-Path -LiteralPath (Join-Path $cache 'results.tsv')) -or
        @(Get-ChildItem -LiteralPath $cache -Filter '*.spv' -File -ErrorAction SilentlyContinue).Count -ne 11977) {
        Stage 'Extrayendo y comprobando los shaders de tu copia…'
        Action 'ExtractAllShaders'; Action 'BuildShaders'
        foreach ($check in @('ShaderTest','ComputeTest','NumericTest','ArrayTest','LiteralHandleTest','PoolTest','RootConstantsTest','GpuValidationTest')) { Action $check }
        Stage 'Traduciendo y validando la cache completa de shaders…'
        Action 'TranslateAllShaders'; Action 'ValidateAllShaders'
    }
    $validatedCount = @(Get-ChildItem -LiteralPath $cache -Filter '*.spv' -File -ErrorAction SilentlyContinue).Count
    if ($validatedCount -ne 11977) {
        throw "La cache contiene $validatedCount de 11977 shaders. Consulta los informes shader-validation y shader-variants de project/reports antes de iniciar el juego."
    }
    Stage 'Terminando el paquete local…'
    $build = Join-Path $Work 'build/fefw'
    foreach ($name in @('fefw.exe','fefw.map','fefw.pdb','astcdec-avx2-shared.dll','astcdec-sse2-shared.dll','astcdec-sse4.1-shared.dll')) {
        Copy-Local (Join-Path $build $name) (Join-Path $GameDirectory $name)
    }
    foreach ($pair in @(@((Join-Path $Work 'prepared/exefs'), 'exefs'), @($cache, 'shader_cache/spirv'), @((Join-Path $build 'fonts'), 'fonts'))) {
        $destination = Join-Path $GameDirectory $pair[1]
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $pair[0] -File) {
            $target = Join-Path $destination $file.Name
            if (!(Test-Path -LiteralPath $target) -or (Get-Item -LiteralPath $target).Length -ne $file.Length -or
                (Get-Item -LiteralPath $target).LastWriteTimeUtc -ne $file.LastWriteTimeUtc) {
                Copy-Local $file.FullName $target
            }
        }
    }
    New-Item -ItemType Directory -Path (Join-Path $GameDirectory 'licenses') -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Project 'licenses') -File) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $GameDirectory 'licenses') -Force
    }
    Copy-Item -LiteralPath (Join-Path $Project 'LICENSE') -Destination (Join-Path $GameDirectory 'LICENSE') -Force
    Copy-Item -LiteralPath (Join-Path $Work 'engine/runtime/assets/fonts/LICENSE-DejaVu.txt') -Destination (Join-Path $GameDirectory 'licenses') -Force
    # The running configurator is deliberately not overwritten. It saves the
    # validated profile after this process exits successfully.
    Stage 'Copia local preparada. Guardando la configuracion…'
    Stop-Transcript | Out-Null
    exit 0
} catch {
    $message = $_.Exception.Message
    try { Stage $message; Write-Error $_ -ErrorAction Continue; Stop-Transcript | Out-Null } catch {}
    exit 1
}
