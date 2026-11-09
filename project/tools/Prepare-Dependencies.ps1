param([Parameter(Mandatory)][string]$Work, [Parameter(Mandatory)][string]$Project)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Work = [IO.Path]::GetFullPath($Work)
$temporary = Join-Path $Work 'tmp'
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$env:TEMP = $temporary; $env:TMP = $temporary
$engine = Join-Path $Work 'engine'
$renderer = Join-Path $Work 'renderer-engine'
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}
function Checkout([string]$Url, [string]$Pin, [string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        Run 'git' @('-c', 'core.longpaths=true', 'clone', '--filter=blob:none', '--no-checkout', $Url, $Path)
        Run 'git' @('-C', $Path, '-c', 'core.longpaths=true', 'checkout', '--detach', $Pin)
    }
    $head = & git -C $Path rev-parse HEAD
    if ($LASTEXITCODE -or $head -ne $Pin) { throw "Dependency $Path does not match $Pin. Existing files are preserved." }
}
. (Join-Path $Project 'tools/Initialize-BuildEnv.ps1')
$python = Join-Path $Work 'venv/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw 'The Python environment must be prepared.' }
# The upstream runtime builds its matching ASTC/BC7 converter from source.
# Do not link the older converter: its encoder API differs.
$textures = Join-Path $renderer 'tools/textures'
Checkout 'https://github.com/ARM-software/astc-encoder.git' 'baff485b0ff36d2f95d28961605106502c653966' (Join-Path $textures 'third_party/astc-encoder')
$validator = Join-Path $Work 'build/spirv-tools/tools/spirv-val.exe'
if (!(Test-Path -LiteralPath $validator)) {
    $spv = Join-Path $Work 'refs/spirv-tools'
    Checkout 'https://github.com/KhronosGroup/SPIRV-Tools.git' 'ef96ed763b43b59b33b31b362f09a02b729fa1c9' $spv
    Checkout 'https://github.com/KhronosGroup/SPIRV-Headers.git' '496543121ce6419f23d6fa5d7194ba66c36212d2' (Join-Path $spv 'external/spirv-headers')
    Run 'cmake' @('-S', $spv, '-B', (Join-Path $Work 'build/spirv-tools'), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl', '-DSPIRV_SKIP_TESTS=ON', '-DSPIRV_WERROR=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
    Run 'cmake' @('--build', (Join-Path $Work 'build/spirv-tools'), '--target', 'spirv-val', 'spirv-as', 'spirv-dis', '--parallel', '1')
}
$vcpkg = Join-Path $Work 'toolchains/vcpkg'
if (!(Test-Path -LiteralPath (Join-Path $vcpkg 'installed/x64-windows-static/lib/dav1d.lib')) -or
    !(Test-Path -LiteralPath (Join-Path $vcpkg 'installed/x64-windows-static/lib/vpx.lib'))) {
    Checkout 'https://github.com/microsoft/vcpkg.git' '661e42cd1600603016ced1d76a15359e7dade9f5' $vcpkg
    Run (Join-Path $vcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
    $videoOverlay = Join-Path $Work 'toolchains/video-ports'
    Run $python @((Join-Path $Project 'tools/prepare_video_port.py'), '--vcpkg', $vcpkg, '--out', $videoOverlay)
    Run (Join-Path $vcpkg 'vcpkg.exe') @('install', 'libvpx:x64-windows-static', 'dav1d:x64-windows-static', "--overlay-ports=$videoOverlay", '--disable-metrics')
}
$dotnet = Join-Path $Work 'toolchains/dotnet/dotnet.exe'
if (!(Test-Path -LiteralPath $dotnet)) {
    $found = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if ($found -and ((& $found.Source --list-sdks) -match '^10\.')) {
        $dotnet = $found.Source
    } else {
        # A private, pinned SDK also supports machines whose system SDK is older.
        # Nothing is installed into Program Files and PATH is not changed.
        $archiveDir = Join-Path $Work 'downloads'
        New-Item -ItemType Directory -Path $archiveDir -Force | Out-Null
        $sdkZip = Join-Path $archiveDir 'dotnet-sdk-10.0.403-win-x64.zip'
        $sdkHash = '7a114c318fa7f1d9822991b534b2814d0e78386ceda816b371f5141acefe741d87ccb606ede06cadac8f9ac5070e32b708a67bbd1619ff5e5a7858a27874ef48'
        if (!(Test-Path -LiteralPath $sdkZip) -or (Get-FileHash -LiteralPath $sdkZip -Algorithm SHA512).Hash -ne $sdkHash) {
            Invoke-WebRequest 'https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.403/dotnet-sdk-10.0.403-win-x64.zip' -OutFile $sdkZip -UseBasicParsing
        }
        if ((Get-FileHash -LiteralPath $sdkZip -Algorithm SHA512).Hash -ne $sdkHash) { throw '.NET SDK: SHA-512 mismatch.' }
        $sdkDirectory = [IO.Path]::GetFullPath((Join-Path $Work 'toolchains/dotnet'))
        $sdkStage = [IO.Path]::GetFullPath((Join-Path $Work 'toolchains/dotnet-10.0.403.preparing'))
        $sdkBoundary = $Work.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
        if (!$sdkDirectory.StartsWith($sdkBoundary, [StringComparison]::OrdinalIgnoreCase) -or
            !$sdkStage.StartsWith($sdkBoundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'SDK destination is outside the workspace.' }
        if (Test-Path -LiteralPath $sdkDirectory) { throw 'The private .NET folder already exists but is incomplete. Existing files are preserved.' }
        Expand-Archive -LiteralPath $sdkZip -DestinationPath $sdkStage -Force
        $stagedDotnet = Join-Path $sdkStage 'dotnet.exe'
        if (!((& $stagedDotnet --list-sdks) -match '^10\.0\.403 ')) { throw 'Could not verify the private SDK.' }
        [IO.Directory]::Move($sdkStage, $sdkDirectory)
    }
}
if (!((& $dotnet --list-sdks) -match '^10\.')) { throw '.NET SDK 10 is required to translate shaders.' }
# Legacy translator checks use the private runtime path. A junction reuses the
# installed SDK read-only instead of duplicating its files or changing PATH.
if (!(Test-Path -LiteralPath (Join-Path $Work 'toolchains/dotnet/dotnet.exe'))) {
    New-Item -ItemType Directory -Path (Join-Path $Work 'toolchains') -Force | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $Work 'toolchains/dotnet') -Target (Split-Path -Parent $dotnet) | Out-Null
}
foreach ($shaderEngine in @($engine, $renderer)) {
$staged = Join-Path $shaderEngine 'tools/shaders/third_party/ryubing'
if (!(Test-Path -LiteralPath (Join-Path $staged 'src/Ryujinx.Graphics.Shader/Ryujinx.Graphics.Shader.csproj'))) {
    $ryu = Join-Path $Work 'refs/ryubing'
    Checkout 'https://git.ryujinx.app/projects/Ryubing.git' 'a82350bb774f70fcbd41c9987bf67a3775409963' $ryu
    New-Item -ItemType Directory -Path (Join-Path $staged 'src') -Force | Out-Null
    foreach ($name in @('Ryujinx.Common','Spv.Generator','Ryujinx.Graphics.Shader')) {
        Copy-Item -LiteralPath (Join-Path $ryu "src/$name") -Destination (Join-Path $staged "src/$name") -Recurse -Force
    }
    foreach ($name in @('Directory.Build.props','Directory.Packages.props','global.json','nuget.config','LICENSE.txt')) {
        Copy-Item -LiteralPath (Join-Path $ryu $name) -Destination (Join-Path $staged $name) -Force
    }
}
$env:DOTNET_CLI_HOME = Join-Path $Work 'dotnet-home'
$env:NUGET_PACKAGES = Join-Path $Work 'nuget'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
Run $dotnet @('restore', (Join-Path $shaderEngine 'tools/shaders/harness/SpirvHarness.csproj'), "-p:PatchPython=$python", '-p:Platform=AnyCPU', '--disable-parallel', '-m:1', '-nodeReuse:false', '--source', 'https://api.nuget.org/v3/index.json')

}
