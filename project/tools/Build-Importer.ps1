param([Parameter(Mandatory)][string]$Work,[Parameter(Mandatory)][string]$Output,[string]$DotNet='')
$ErrorActionPreference='Stop'
$Work=[IO.Path]::GetFullPath($Work)
$Output=[IO.Path]::GetFullPath($Output)
$env:DOTNET_CLI_HOME=Join-Path $Work 'tmp/dotnet-cli'
$env:NUGET_PACKAGES=Join-Path $Work 'downloads/nuget'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE='false'
if (!$DotNet) { $DotNet = & (Join-Path $PSScriptRoot 'Get-DotNet.ps1') -Work $Work }
$pin='9cd476ee21fcb5b9bc4f91c869a7363c52187238'
$native=Join-Path $Work 'downloads/importer-native'
New-Item -ItemType Directory -Path $native -Force | Out-Null
$files=@{'nxformat.dll'='76c28056d22887a991b55f7f329cb34159b572f98fb2c3b6a54200bf6ff055c1'}
foreach($name in $files.Keys) {
    $file=Join-Path $native $name
    if(!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ine $files[$name]) {
        Invoke-WebRequest "https://raw.githubusercontent.com/siriusgoldsmith/NxExtractor/$pin/native/$name" -OutFile $file -UseBasicParsing -TimeoutSec 120
    }
    if((Get-FileHash -LiteralPath $file).Hash -ine $files[$name]){throw "Importer native library integrity failure: $name"}
}
$buildRoot=Join-Path $Work 'build/importer'
& $DotNet publish (Join-Path $PSScriptRoot 'importer/FEFW.Importer.csproj') -c Release --no-self-contained -o $Output "-p:BaseIntermediateOutputPath=$buildRoot/obj/" "-p:BaseOutputPath=$buildRoot/bin/" '-p:UseAppHost=false'
if($LASTEXITCODE){throw 'Importer build failed.'}
New-Item -ItemType Directory -Path (Join-Path $Output 'native') -Force | Out-Null
foreach($name in $files.Keys){Copy-Item -LiteralPath (Join-Path $native $name) -Destination (Join-Path $Output ('native/'+$name)) -Force}
