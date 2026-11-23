param([Parameter(Mandatory)][string]$Work)
$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
$runtime = Join-Path $Work 'toolchains/dotnet/dotnet.exe'
if (Test-Path -LiteralPath $runtime) { return $runtime }
$installed = Get-Command dotnet.exe -ErrorAction SilentlyContinue
if ($installed -and ((& $installed.Source --list-sdks) -match '^10\.')) { return $installed.Source }
$download = Join-Path $Work 'downloads/dotnet-sdk-10.0.403-win-x64.zip'
$sha = '7a114c318fa7f1d9822991b534b2814d0e78386ceda816b371f5141acefe741d87ccb606ede06cadac8f9ac5070e32b708a67bbd1619ff5e5a7858a27874ef48'
New-Item -ItemType Directory -Path (Split-Path -Parent $download) -Force | Out-Null
if (!(Test-Path -LiteralPath $download) -or (Get-FileHash -LiteralPath $download -Algorithm SHA512).Hash -ine $sha) {
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest 'https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.403/dotnet-sdk-10.0.403-win-x64.zip' -OutFile $download -UseBasicParsing -TimeoutSec 600
}
if ((Get-FileHash -LiteralPath $download -Algorithm SHA512).Hash -ine $sha) { throw '.NET SDK integrity check failed.' }
$destination = Join-Path $Work 'toolchains/dotnet'
if (Test-Path -LiteralPath $destination) { throw 'The private .NET folder is incomplete; existing files were preserved.' }
$stage = Join-Path $Work ('toolchains/dotnet-stage-' + [Guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $download -DestinationPath $stage
if (!((& (Join-Path $stage 'dotnet.exe') --list-sdks) -match '^10\.0\.403 ')) { throw 'The downloaded SDK did not validate.' }
[IO.Directory]::Move($stage, $destination)
Write-Output $runtime
