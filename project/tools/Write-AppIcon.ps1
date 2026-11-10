# SPDX-License-Identifier: GPL-3.0-or-later
# Decode the project's original icon during CMake configuration; no downloads.
param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
$icon = Get-Content -LiteralPath $Source -Raw | ConvertFrom-Json
$bytes = [Convert]::FromBase64String($icon.ico_base64)
$hash = [Security.Cryptography.SHA256]::Create()
try { $digest = [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
finally { $hash.Dispose() }
if ($digest -cne $icon.ico_sha256) { throw 'Application icon integrity check failed.' }
[IO.File]::WriteAllBytes($Output, $bytes)
