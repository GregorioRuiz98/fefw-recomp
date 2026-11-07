param(
    [string]$Work = (Join-Path $PSScriptRoot '../../../work'),
    [string]$Out = (Join-Path $PSScriptRoot '../../FireEmblemRecomp-public')
)
$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
$Out = [IO.Path]::GetFullPath($Out)
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $Work 'build/setup-public'
if ((Test-Path -LiteralPath $Out) -and @(Get-ChildItem -LiteralPath $Out -Force).Count) {
    throw 'La carpeta publica de salida no esta vacia. Elige otra para conservar sus archivos.'
}
. (Join-Path $PSScriptRoot 'Initialize-BuildEnv.ps1')
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Fallo $Exe ($LASTEXITCODE)" }
}
Run 'cmake' @('-S', $project, '-B', $build, '-G', 'Ninja', '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl', '-DCMAKE_BUILD_TYPE=Release', '-DFEFW_BUILD_GAME=OFF')
Run 'cmake' @('--build', $build, '--target', 'fefw_setup', 'fefw_config_tests', 'fefw_mods_tests', '--parallel', '1')
Run 'ctest' @('--test-dir', $build, '--output-on-failure')
$python = Join-Path $Work 'venv/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { $python = 'python' }
Run $python @((Join-Path $PSScriptRoot 'package_public.py'), '--source', $project, '--setup-exe', (Join-Path $build 'fefw_setup.exe'), '--out', $Out, '--zip', ($Out + '.zip'))
Write-Host "Entrega publica preparada y comprobada: $Out.zip"
