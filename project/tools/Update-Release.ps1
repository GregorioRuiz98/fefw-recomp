# SPDX-License-Identifier: GPL-3.0-or-later
param([ValidateSet('Check','Install')][string]$Action = 'Check', [Parameter(Mandatory=$true)][string]$GameDirectory,
      [string]$Current = 'v0.1.0-preview.11', [int]$ParentId = 0)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Update-Core.ps1')
$GameDirectory = [IO.Path]::GetFullPath($GameDirectory)
function Status([string]$State, [string]$Message, [string]$Tag = '') {
    $message = $Message -replace '[\r\n]', ' '
    $body = "[update]`nstate=$State`nmessage=$message`ntag=$Tag`n"
    $path = Join-Path $GameDirectory 'update.status'
    [IO.File]::WriteAllText(($path + '.tmp'), $body, [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $path) { [IO.File]::Replace(($path + '.tmp'), $path, [NullString]::Value) } else { [IO.File]::Move(($path + '.tmp'), $path) }
}
try {
    Status 'checking' 'Checking the project releases on GitHub...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    # /latest excludes our preview releases; select by semantic version from the release list.
    $releases = Invoke-RestMethod -Uri 'https://api.github.com/repos/GregorioRuiz98/fefw-recomp/releases?per_page=100' -Headers @{'User-Agent'='FEFW-Recomp-Updater';'Accept'='application/vnd.github+json'} -TimeoutSec 30
    $release = Select-Release $releases $Current
    if ($null -eq $release) { Status 'current' ('No newer compatible package found. Installed tools: ' + $Current); exit 0 }
    [IO.File]::WriteAllText((Join-Path $GameDirectory 'update-notes.txt'), ($release.tag + "`r`n`r`n" + $release.notes), [Text.UTF8Encoding]::new($false))
    if ($Action -eq 'Check') { Status 'available' ('Update available: ' + $release.tag + '. Review release notes before installing.') $release.tag; exit 0 }
    Status 'downloading' ('Downloading and verifying ' + $release.tag + '...') $release.tag
    $root = Join-Path $GameDirectory ('work/updates/' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $archive = Join-Path $root 'package.zip'; $stage = Join-Path $root 'package'; $backup = Join-Path $root 'backup'
    Invoke-WebRequest -UseBasicParsing -Uri $release.url -OutFile $archive -TimeoutSec 60
    $null = Expand-VerifiedUpdate $archive $release.digest $stage
    if ($ParentId -gt 0) {
        $parent = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
        if ($parent -and !$parent.WaitForExit(60000)) { throw 'Close the configurator before updating.' }
    }
    if (Get-Process fefw -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $GameDirectory 'fefw.exe') }) { throw 'Close the game before updating.' }
    Status 'installing' 'Installing verified project tools. Your game build, settings, saves and caches are preserved.' $release.tag
    Install-VerifiedUpdate $stage $GameDirectory $backup
    Status 'done' ('Updated to ' + $release.tag + '. Save from setup to refresh the local game build if requested. Backup: ' + $backup) $release.tag
    if ($ParentId -gt 0) { Start-Process -FilePath (Join-Path $GameDirectory 'fefw_setup.exe') -ArgumentList @('--game', ('"' + $GameDirectory + '"')) }
} catch {
    Status 'error' $_.Exception.Message
    exit 1
}
