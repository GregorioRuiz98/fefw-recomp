param([Parameter(Mandatory)][string]$Stage,[Parameter(Mandatory)][string]$GameDirectory,[Parameter(Mandatory)][string]$PayloadHash)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Update-Core.ps1')
$Stage=[IO.Path]::GetFullPath($Stage)
$GameDirectory=[IO.Path]::GetFullPath($GameDirectory)
if($PayloadHash -notmatch '^[0-9a-f]{64}$'){throw 'Invalid embedded package identity.'}
$manifest=Get-Content -LiteralPath (Join-Path $Stage 'public-manifest.json') -Raw | ConvertFrom-Json
if($manifest.kind -cne 'public-tools-and-configurator-only' -or $manifest.game_content_included -ne $false){throw 'Invalid bootstrap payload.'}
foreach($file in $manifest.files){
    Assert-UpdatePath $file.path
    $path=Join-Path $Stage $file.path
    if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-UpdateHash $path) -ine $file.sha256){throw 'Embedded package file integrity failure.'}
}
New-Item -ItemType Directory -Path $GameDirectory -Force | Out-Null
Install-VerifiedUpdate $Stage $GameDirectory ($Stage+'.backup')
[IO.File]::WriteAllText((Join-Path $GameDirectory 'bootstrap.version'),$PayloadHash,[Text.UTF8Encoding]::new($false))
