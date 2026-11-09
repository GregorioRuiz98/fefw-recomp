# SPDX-License-Identifier: GPL-3.0-or-later
param([Parameter(Mandatory=$true)][string]$Work)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/Update-Core.ps1')
Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Join-Path ([IO.Path]::GetFullPath($Work)) ('tests/updates-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($root) | Out-Null
$script:checks = 0
function Check($Ok, [string]$Label) { $script:checks++; if (!$Ok) { throw "FAIL: $Label" } }
function Reject([scriptblock]$Action, [string]$Label) { $caught = $false; try { & $Action | Out-Null } catch { $caught = $true }; Check $caught $Label }
Check ((Compare-ReleaseVersion 'v0.1.0-preview.10' 'v0.1.0-preview.9') -gt 0) 'numeric preview ordering'
Check ((Compare-ReleaseVersion 'v0.1.0' 'v0.1.0-preview.10') -gt 0) 'stable follows preview'
Check ((Compare-ReleaseVersion 'v0.1.0-preview.6' 'v0.1.0-preview.6') -eq 0) 'same version'
Check ((Compare-ReleaseVersion 'v0.2.0' 'v0.10.0') -lt 0) 'numeric core ordering'
foreach ($name in @('../fefw.ini','project/../fefw.ini','project/tools/fefw.exe','fefw.exe','controls.ini','save/a','project/save/a.py','project/tools/c_001.cpp','project/tools/a:bad.py','/project/tools/a.py')) {
    Reject { Assert-UpdatePath $name } "reject $name"
}
$releases = @()
foreach ($n in @(5, 7, 10)) {
    $tag = "v0.1.0-preview.$n"; $name = "FEFW-Recomp-Windows-x64-$tag.zip"
    $releases += [pscustomobject]@{draft=$false;tag_name=$tag;body='changes';assets=@([pscustomobject]@{name=$name;browser_download_url="https://github.com/GregorioRuiz98/fefw-recomp/releases/download/$tag/$name";digest=('sha256:' + ('a' * 64));size=1000})}
}
Check ((Select-Release $releases 'v0.1.0-preview.6').tag -eq 'v0.1.0-preview.10') 'preview list picks newest'
Check ($null -eq (Select-Release $releases 'v0.1.0-preview.11')) 'never downgrade'
$releases[-1].assets[0].browser_download_url = 'https://example.com/bad.zip'
Check ((Select-Release $releases 'v0.1.0-preview.6').tag -eq 'v0.1.0-preview.7') 'reject foreign download'
function Archive([string]$Name, [string]$Extra = '') {
    $zipPath = Join-Path $root $Name
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $files = [ordered]@{'README.md'='new readme';'fefw_setup.exe'='MZfixture';'project/CMakeLists.txt'='new project';'project/LICENSE'='fixture license'}
        $list = @()
        foreach ($key in $files.Keys) {
            $bytes = [Text.Encoding]::UTF8.GetBytes($files[$key]); $entry = $zip.CreateEntry($key); $stream = $entry.Open(); $stream.Write($bytes,0,$bytes.Length); $stream.Dispose()
            $hash = [Security.Cryptography.SHA256]::Create(); try { $digest = [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-','').ToLowerInvariant() } finally { $hash.Dispose() }
            $list += [pscustomobject]@{path=$key;bytes=$bytes.Length;sha256=$digest}
        }
        $manifest = @{kind='public-tools-and-configurator-only';game_content_included=$false;files=$list} | ConvertTo-Json -Depth 5
        $entry = $zip.CreateEntry('public-manifest.json'); $stream = [IO.StreamWriter]::new($entry.Open()); $stream.Write($manifest); $stream.Dispose()
        if ($Extra) { $entry = $zip.CreateEntry($Extra); $stream = [IO.StreamWriter]::new($entry.Open()); $stream.Write('bad'); $stream.Dispose() }
    } finally { $zip.Dispose() }
    return $zipPath
}
$archive = Archive 'valid.zip'; $digest = (Get-UpdateHash $archive)
Reject { Expand-VerifiedUpdate $archive ('b' * 64) (Join-Path $root 'wrong-hash') } 'wrong digest'
$stage = Join-Path $root 'stage'; $null = Expand-VerifiedUpdate $archive $digest $stage
Check (Test-Path -LiteralPath (Join-Path $stage 'fefw_setup.exe')) 'verified extraction'
$bad = Archive 'traversal.zip' '../escape.txt'
Reject { Expand-VerifiedUpdate $bad (Get-UpdateHash $bad) (Join-Path $root 'traversal') } 'archive traversal'
Check (!(Test-Path -LiteralPath (Join-Path $root 'escape.txt'))) 'traversal writes nothing'
$extra = Archive 'extra.zip' 'project/tools/unknown.py'
Reject { Expand-VerifiedUpdate $extra (Get-UpdateHash $extra) (Join-Path $root 'extra') } 'manifest rejects unlisted file'
$dest = Join-Path $root 'install'; [IO.Directory]::CreateDirectory((Join-Path $dest 'project')) | Out-Null
foreach ($name in @('README.md','fefw_setup.exe','project/CMakeLists.txt','fefw.exe','fefw.ini','controls.ini','fefw.features','shader_cache/keep.spv','save/user.sav','work/build/keep.obj')) {
    $file = Join-Path $dest $name; [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null; [IO.File]::WriteAllText($file, ('original ' + $name))
}
# The third replacement is locked. The first two must be restored, not partially installed.
$locked = [IO.File]::Open((Join-Path $dest 'project/CMakeLists.txt'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try { Reject { Install-VerifiedUpdate $stage $dest (Join-Path $root 'failed-backup') } 'locked file aborts transaction' } finally { $locked.Dispose() }
Check ([IO.File]::ReadAllText((Join-Path $dest 'README.md')) -eq 'original README.md') 'rollback restores earlier file'
Check ([IO.File]::ReadAllText((Join-Path $dest 'fefw_setup.exe')) -eq 'original fefw_setup.exe') 'rollback restores configurator'
Install-VerifiedUpdate $stage $dest (Join-Path $root 'backup')
Check ([IO.File]::ReadAllText((Join-Path $dest 'README.md')) -eq 'new readme') 'valid update installed'
foreach ($name in @('fefw.exe','fefw.ini','controls.ini','fefw.features','shader_cache/keep.spv','save/user.sav','work/build/keep.obj')) {
    Check ([IO.File]::ReadAllText((Join-Path $dest $name)) -eq ('original ' + $name)) "preserves $name"
}
Check ([IO.File]::ReadAllText((Join-Path $root 'backup/README.md')) -eq 'original README.md') 'backup kept'
Write-Output ("Updater: $script:checks checks passed; fixtures in $root")
