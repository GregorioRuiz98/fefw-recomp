# SPDX-License-Identifier: GPL-3.0-or-later
# Shared by the standalone updater and its offline transaction tests. Windows PowerShell 5.1.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Get-UpdateHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path); $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}
function Compare-ReleaseVersion([string]$Left, [string]$Right) {
    $pattern = '^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?$'
    if ($Left -notmatch $pattern) { throw 'Invalid release version.' }; $a = @($Matches[1], $Matches[2], $Matches[3], $Matches[4])
    if ($Right -notmatch $pattern) { throw 'Invalid installed version.' }; $b = @($Matches[1], $Matches[2], $Matches[3], $Matches[4])
    for ($i = 0; $i -lt 3; $i++) { if ([int]$a[$i] -ne [int]$b[$i]) { return [Math]::Sign([int]$a[$i] - [int]$b[$i]) } }
    if (!$a[3] -and !$b[3]) { return 0 }; if (!$a[3]) { return 1 }; if (!$b[3]) { return -1 }
    $ap = $a[3].Split('.'); $bp = $b[3].Split('.')
    for ($i = 0; $i -lt [Math]::Min($ap.Count, $bp.Count); $i++) {
        if ($ap[$i] -ceq $bp[$i]) { continue }
        if ($ap[$i] -match '^\d+$' -and $bp[$i] -match '^\d+$') { return [Math]::Sign([long]$ap[$i] - [long]$bp[$i]) }
        if ($ap[$i] -match '^\d+$') { return -1 }; if ($bp[$i] -match '^\d+$') { return 1 }
        return [Math]::Sign([string]::CompareOrdinal($ap[$i], $bp[$i]))
    }
    return [Math]::Sign($ap.Count - $bp.Count)
}
function Select-Release($Releases, [string]$Current) {
    $best = $null
    foreach ($release in $Releases) {
        if ($release.draft) { continue }
        $tag = [string]$release.tag_name
        try { if ((Compare-ReleaseVersion $tag $Current) -le 0) { continue } } catch { continue }
        $name = 'FEFW-Recomp-Windows-x64-' + $tag + '.zip'
        $assets = @($release.assets | Where-Object { $_.name -ceq $name })
        if ($assets.Count -ne 1) { continue }; $asset = $assets[0]
        $url = 'https://github.com/GregorioRuiz98/fefw-recomp/releases/download/' + $tag + '/' + $name
        if ($asset.browser_download_url -cne $url -or $asset.digest -notmatch '^sha256:[0-9a-fA-F]{64}$') { continue }
        if ($asset.size -le 0 -or $asset.size -gt 32MB) { continue }
        if ($null -eq $best -or (Compare-ReleaseVersion $tag $best.tag) -gt 0) {
            $best = [pscustomobject]@{tag=$tag; url=$url; digest=$asset.digest.Substring(7); notes=[string]$release.body; size=[long]$asset.size}
        }
    }
    return $best
}
function Assert-UpdatePath([string]$Name) {
    # Never allow an update to name a local game, profile, save, cache or build artifact.
    if (!$Name -or $Name -match '[\\:\x00-\x1f]' -or $Name.StartsWith('/') -or @($Name.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count) { throw "Unsafe update path: $Name" }
    if ($Name -cin @('fefw_setup.exe','README.md','.gitignore','public-manifest.json','LICENSE','LEGAL.md','NOTICE.md','THIRD_PARTY_NOTICES.md','CONTRIBUTING.md','GITHUB-PUBLISH.md')) { return }
    if ($Name -cin @('tools/Prepare-Game.ps1','tools/Prepare-Dependencies.ps1')) { return }
    if ($Name -notmatch '^project/(?:src|tools|tests|licenses|patches)/' -and
        $Name -cnotin @('project/CMakeLists.txt','project/config.py','project/requirements.txt','project/supported-build.json','project/LICENSE','project/DEPENDENCIAS.md','project/CONTROLES-MANDO.md','project/INCIDENCIAS-Y-CONFIGURADOR.md','project/.gitignore','project/FireEmblemRecomp.ps1','project/PUBLIC-README.md','project/LEGAL.md','project/NOTICE.md','project/THIRD_PARTY_NOTICES.md','project/CONTRIBUTING.md','project/GITHUB-PUBLISH.md','project/UPSTREAM-INTEGRATION.md')) { throw "File is outside update allowlist: $Name" }
    if ($Name -notmatch '^project/(?:src|tools|tests|licenses|patches)/') { return }
    if ([IO.Path]::GetExtension($Name) -cnotin @('.cpp','.h','.in','.rc','.manifest','.py','.ps1','.md','.txt','.json','.patch','.tt','.comp','.vert','.frag','.glsl','.spvasm','.sites') -and $Name -cne 'project/.gitignore') { throw "Unsupported update file: $Name" }
    if ($Name -match '(?i)(?:^|/)(?:exefs|romfs|lift|shader_cache|save|work|prepared|reports|\.git)(?:/|$)' -or $Name -match '(?i)(?:^|/)(?:c_\d+|module_(?:main|sdk|rtld|subsdk\d+))\.cpp$') { throw "Private data in update: $Name" }
}
function Expand-VerifiedUpdate([string]$Archive, [string]$Digest, [string]$Destination) {
    if ($Digest -notmatch '^[0-9a-fA-F]{64}$' -or (Get-UpdateHash $Archive) -ine $Digest) { throw 'Update SHA256 verification failed.' }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $Destination) { throw 'Update staging directory already exists.' }
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $total = 0L
        foreach ($entry in $zip.Entries) {
            Assert-UpdatePath $entry.FullName
            if (!$names.Add($entry.FullName)) { throw 'Duplicate archive path.' }
            $total += $entry.Length
            if ($entry.Length -gt 16MB -or $total -gt 64MB -or $names.Count -gt 2000 -or (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw 'Update archive limits or file type rejected.' }
        }
        if (!$names.Contains('public-manifest.json') -or !$names.Contains('fefw_setup.exe') -or !$names.Contains('project/CMakeLists.txt')) { throw 'Incomplete update archive.' }
        [IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)
    } finally { $zip.Dispose() }
    $manifest = Get-Content -LiteralPath (Join-Path $Destination 'public-manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.kind -cne 'public-tools-and-configurator-only' -or $manifest.game_content_included -ne $false) { throw 'Unsupported update package.' }
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$expected.Add('public-manifest.json')
    foreach ($file in $manifest.files) {
        Assert-UpdatePath $file.path
        if (!$expected.Add($file.path)) { throw 'Duplicate manifest path.' }
        $path = Join-Path $Destination $file.path
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-UpdateHash $path) -ine $file.sha256) { throw 'Update file does not match manifest.' }
    }
    if (!$expected.SetEquals($names)) { throw 'Update manifest does not cover the entire archive.' }
    $exe = [IO.File]::ReadAllBytes((Join-Path $Destination 'fefw_setup.exe'))
    if ($exe.Length -lt 2 -or $exe.Length -gt 10MB -or $exe[0] -ne 77 -or $exe[1] -ne 90) { throw 'Invalid standalone configurator.' }
    return $manifest
}
function Install-VerifiedUpdate([string]$Stage, [string]$Destination, [string]$Backup) {
    $Destination = [IO.Path]::GetFullPath($Destination)
    $manifest = Get-Content -LiteralPath (Join-Path $Stage 'public-manifest.json') -Raw | ConvertFrom-Json
    if (Test-Path -LiteralPath $Backup) { throw 'Update backup already exists.' }
    New-Item -ItemType Directory -Path $Backup | Out-Null
    $done = [Collections.Generic.List[object]]::new()
    $temp = ''
    try {
        foreach ($name in @($manifest.files.path) + @('public-manifest.json')) {
            Assert-UpdatePath $name
            $target = Join-Path $Destination $name; $source = Join-Path $Stage $name; $saved = Join-Path $Backup $name
            # Reject junctions/symlinks in every destination component before writing.
            $part = $target
            while ($part -and $part.StartsWith($Destination, [StringComparison]::OrdinalIgnoreCase)) {
                if ((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked update destination rejected.' }
                $part = [IO.Path]::GetDirectoryName($part)
            }
            $exists = Test-Path -LiteralPath $target
            if ($exists) { [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($saved)) | Out-Null; [IO.File]::Copy($target, $saved, $false) }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            $temp = $target + '.updating'; [IO.File]::Copy($source, $temp, $true)
            if ($exists) { [IO.File]::Replace($temp, $target, [NullString]::Value) } else { [IO.File]::Move($temp, $target) }
            $done.Add([pscustomobject]@{target=$target; saved=$saved; existed=$exists})
        }
    } catch {
        $reason = $_
        if ($temp -and (Test-Path -LiteralPath $temp -PathType Leaf)) { [IO.File]::Delete($temp) }
        for ($i = $done.Count - 1; $i -ge 0; $i--) {
            $item = $done[$i]
            if ($item.existed) { [IO.File]::Copy($item.saved, $item.target, $true) }
            elseif (Test-Path -LiteralPath $item.target -PathType Leaf) { [IO.File]::Delete($item.target) }
            if (Test-Path -LiteralPath ($item.target + '.updating')) { [IO.File]::Delete($item.target + '.updating') }
        }
        throw $reason
    }
    # Retain the backup; local game data and even the old game binary are never in the update manifest.
}
