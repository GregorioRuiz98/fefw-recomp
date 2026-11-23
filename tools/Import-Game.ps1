param([Parameter(Mandatory)][string]$GameDirectory,[Parameter(Mandatory)][string]$Request)
$ErrorActionPreference='Stop'
$GameDirectory=[IO.Path]::GetFullPath($GameDirectory)
$log=Join-Path $GameDirectory 'preparation.log'
$status=Join-Path $GameDirectory 'preparation.status'
try {
    Start-Transcript -LiteralPath $log -Force | Out-Null
    [IO.File]::WriteAllText($status,'Preparing the file importer. The first import may download the shared .NET SDK.')
    $project=Join-Path $PSScriptRoot '../project'
    $work=Join-Path $GameDirectory 'work'
    $sdkScript=Join-Path $project 'tools/Get-DotNet.ps1'
    $dotnet = & $sdkScript -Work $work
    $importer=Join-Path $GameDirectory 'importer/FEFW.Importer.dll'
    if(!(Test-Path -LiteralPath $importer)){throw 'The importer is missing. Extract the complete release or run the lightweight launcher.'}
    [IO.File]::WriteAllText($status,'Importing your files. Originals and saved games are preserved; large files can take several minutes.')
    & $dotnet $importer $Request
    if($LASTEXITCODE){throw "File import failed ($LASTEXITCODE). Review Open log for the file or key error."}
    [IO.File]::WriteAllText($status,'Files imported. Checking the game version before preparing shaders.')
    Stop-Transcript | Out-Null
    exit 0
} catch {
    try{[IO.File]::WriteAllText($status,$_.Exception.Message); Write-Error $_ -ErrorAction Continue; Stop-Transcript | Out-Null}catch{}
    exit 1
}
