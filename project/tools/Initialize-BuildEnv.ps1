$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'No se encuentra Visual C++ Build Tools (vswhere).' }
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsInstall) { throw 'Falta el componente C++ x64 de Visual Studio Build Tools.' }
$vcvars = Join-Path $vsInstall 'VC/Auxiliary/Build/vcvars64.bat'
if (!(Test-Path -LiteralPath $vcvars)) { throw 'No se encuentra vcvars64.bat.' }
if ($env:FEFW_BUILD_ENV_READY -ne $vsInstall) {
    $buildCmd = Join-Path $env:SystemRoot 'System32/cmd.exe'
    $vcEnvironment = & $buildCmd /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo preparar el entorno C++ de Visual Studio.' }
    foreach ($vcLine in $vcEnvironment) {
        $vcEq = $vcLine.IndexOf('=')
        if ($vcEq -gt 0) { Set-Item -Path ('Env:' + $vcLine.Substring(0, $vcEq)) -Value $vcLine.Substring($vcEq + 1) }
    }
    $env:FEFW_BUILD_ENV_READY = $vsInstall
}
$buildPaths = @('C:\Program Files\LLVM\bin', 'C:\Program Files\CMake\bin', "$env:LOCALAPPDATA\Microsoft\WinGet\Links") + ($env:PATH -split ';')
$env:PATH = ($buildPaths | Where-Object { $_ } | Select-Object -Unique) -join ';'
