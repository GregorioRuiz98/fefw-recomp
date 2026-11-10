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
$script:stageIndex = 0
function Stage([string]$Message, [int]$Number = 0) {
    if ($Number -gt 0) { $script:stageIndex = $Number }
    [IO.File]::WriteAllText((Join-Path $GameDirectory 'preparation.progress'), "[preparation]`nstage=$script:stageIndex`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($status, $Message, [Text.UTF8Encoding]::new($false))
    Write-Output $Message
}
function Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Preparation failed ($LASTEXITCODE). Check preparation.log." }
}
function Action([string]$Name) {
    & (Join-Path $Project 'FireEmblemRecomp.ps1') -Action $Name -Work $Work -Dump $Source
    if ($LASTEXITCODE -ne 0) { throw "Stage $Name failed ($LASTEXITCODE)." }
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
    if (Get-Process fefw -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $GameDirectory 'fefw.exe') }) { throw 'Close the game before preparing it.' }
    New-Item -ItemType Directory -Path $GameDirectory -Force | Out-Null
    Start-Transcript -LiteralPath $log -Force | Out-Null
    foreach ($tool in @('git','cmake','ninja','python')) {
        if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool is missing. See README.md requirements, then retry preparation." }
    }
    if (!(Test-Path -LiteralPath (Join-Path $Project 'FireEmblemRecomp.ps1'))) { throw 'The public package project files are missing.' }
    . (Join-Path $Project 'tools/Initialize-BuildEnv.ps1')
    if ($CheckOnly) { Stage 'Main development tools are available.'; Stop-Transcript | Out-Null; exit 0 }
    Stage 'Preparing local tools…' 1
    if (!(Test-Path -LiteralPath (Join-Path $Work 'venv/Scripts/python.exe'))) { Action 'Bootstrap' }
    else { Checked (Join-Path $Work 'venv/Scripts/python.exe') @((Join-Path $Project 'tools/bootstrap.py'), '--work', $Work, '--engine-only') }
    Stage 'Verifying and normalizing your game copy…' 2
    Action 'Prepare'
    Stage 'Preparing libraries and the shader translator…' 3
    & (Join-Path $Project 'tools/Prepare-Dependencies.ps1') -Work $Work -Project $Project
    Stage 'Translating game code for this PC…' 4
    & (Join-Path $Work 'venv/Scripts/python.exe') (Join-Path $Project 'tools/local_preparation.py') --work $Work
    if ($LASTEXITCODE -ne 0) { Action 'Cover'; Action 'Lift' }
    Checked (Join-Path $Work 'venv/Scripts/python.exe') @((Join-Path $Project 'tools/refresh_runtime_patches.py'), '--work', $Work)
    Action 'Configure'; Action 'Build'
    $cache = Join-Path $Work 'shaders/goldoire-v015'
    Stage 'Translating and validating shaders for the upstream renderer…' 6
    Checked (Join-Path $Work 'venv/Scripts/python.exe') @((Join-Path $Project 'tools/prepare_renderer_shaders.py'),
        '--work', $Work, '--dotnet', (Join-Path $Work 'toolchains/dotnet/dotnet.exe'), '--romfs', (Join-Path $Source 'romfs'))
    Stage 'Finishing the local package…' 8
    $build = Join-Path $Work 'build/fefw'
    foreach ($name in @('fefw.exe','fefw.map','fefw.pdb')) {
        Copy-Local (Join-Path $build $name) (Join-Path $GameDirectory $name)
    }
    foreach ($pair in @(@((Join-Path $Work 'prepared/exefs'), 'exefs'), @($cache, 'shader_cache/goldoire-v015'), @((Join-Path $build 'fonts'), 'fonts'))) {
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
    Copy-Item -LiteralPath (Join-Path $Work 'renderer-engine/runtime/assets/fonts/LICENSE-DejaVu.txt') -Destination (Join-Path $GameDirectory 'licenses') -Force
    # The running configurator is deliberately not overwritten. It saves the
    # validated profile after this process exits successfully.
    [IO.File]::WriteAllText((Join-Path $GameDirectory 'fefw.features'), 'fefw-runtime-13', [Text.UTF8Encoding]::new($false))
    Stage 'Local copy prepared. Saving settings…' 9
    Stop-Transcript | Out-Null
    exit 0
} catch {
    $message = $_.Exception.Message
    try { Stage $message; Write-Error $_ -ErrorAction Continue; Stop-Transcript | Out-Null } catch {}
    exit 1
}
