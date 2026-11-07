param(
    [ValidateSet('Bootstrap','Inspect','Prepare','Cover','Lift','ExtractShaders','ExtractAllShaders','BuildShaders','TranslateShaders','TranslateAllShaders','ValidateShaders','ValidateAllShaders','ShaderTest','ComputeTest','NumericTest','ArrayTest','RefreshLightingArrays','RefreshUniformArrays','LiteralHandleTest','PoolTest','VideoTest','RootConstantsTest','GpuValidationTest','RefreshPoolShaders','RefreshLiteralHandles','RefreshComputeShaders','Checks','ProbeAudioSdk','Test','NativeTest','FloatingPointTest','BranchReturnTest','Configure','Build','Package','Run')]
    [string]$Action = 'Test',
    [string]$Dump = '',
    [switch]$Revision16Probe,
    [ValidateRange(0, 1024)][int]$ProbeSaveMiB = 0,
    [ValidateRange(0, 29)][int]$ScreenshotAt = 0,
    [switch]$DlssSpatialFallback,
    [string]$InputScript = '',
    [ValidateRange(1, 600)][int]$MaxSeconds = 30,
    [string]$Work = (Join-Path $PSScriptRoot '../../work')
)
$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
$engine = Join-Path $Work 'engine'
$python = Join-Path $Work 'venv/Scripts/python.exe'
$exefs = Join-Path $Work 'prepared/exefs'
$reports = Join-Path $PSScriptRoot 'reports'
$local = Join-Path $Work 'fire-emblem-local.json'
$dotnetExe = Join-Path $Work 'toolchains/dotnet/dotnet.exe'
if (!(Test-Path -LiteralPath $dotnetExe)) {
    $installedDotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if ($installedDotnet) { $dotnetExe = $installedDotnet.Source }
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "La operación falló con código $LASTEXITCODE. Revisa la salida anterior." }
}

if ($Action -eq 'Bootstrap') {
    Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'tools/bootstrap.py'), '--work', $Work)
    exit 0
}
if (!(Test-Path -LiteralPath $python)) { throw 'Falta el entorno privado. Ejecuta primero -Action Bootstrap.' }
if (!$Dump -and (Test-Path -LiteralPath $local)) {
    $Dump = (Get-Content -LiteralPath $local -Raw | ConvertFrom-Json).dump_root
}
if ($Dump) {
    $Dump = [IO.Path]::GetFullPath($Dump)
    if (!(Test-Path -LiteralPath (Join-Path $Dump 'exefs/main'))) {
        $versions = @(Get-ChildItem -LiteralPath (Join-Path $Dump 'updates') -Directory |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'exefs/main') })
        if ($versions.Count -ne 1) { throw 'Indica una versión concreta con exefs/main y romfs.' }
        $Dump = $versions[0].FullName
    }
    @{ dump_root = $Dump } | ConvertTo-Json | Set-Content -LiteralPath $local -Encoding utf8
}

switch ($Action) {
    'Checks' {
        $checkResults = @()
        foreach ($checkAction in @('Test','BuildShaders','ShaderTest','ComputeTest','NumericTest','ArrayTest','LiteralHandleTest','PoolTest','VideoTest','RootConstantsTest','GpuValidationTest','NativeTest','FloatingPointTest','BranchReturnTest','Build')) {
            $checkLog = Join-Path $reports ('check-' + $checkAction.ToLowerInvariant() + '.log')
            try {
                & $PSCommandPath -Action $checkAction -Work $Work *> $checkLog
                $checkResults += @{ action = $checkAction; passed = $true; log = [IO.Path]::GetFileName($checkLog) }
                Write-Host "$checkAction : PASS"
            } catch {
                $_ | Out-String | Add-Content -LiteralPath $checkLog
                $checkResults += @{ action = $checkAction; passed = $false; log = [IO.Path]::GetFileName($checkLog) }
                Write-Host "$checkAction : FAIL"
            }
        }
        $failedChecks = @($checkResults | Where-Object { !$_.passed })
        $gpuCheck = Get-Content -LiteralPath (Join-Path $reports 'texture-arrays-gpu.json') -Raw | ConvertFrom-Json
        @{ executable_checks = $checkResults; all_executable_checks_passed = ($failedChecks.Count -eq 0)
           vulkan_validation_layer_available = $gpuCheck.validation_layer_enabled
           correct_game_frame_verified = $false; all_acceptance_checks_passed = $false
           remaining = @('Full rendering equivalence and interactive gameplay; prologue 3D is visible with the experimental spatial fallback', 'Full DLSS reconstruction, video device-loss investigation and remaining NVN2 operations')
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $reports 'checks.json') -Encoding utf8
        if ($failedChecks.Count) { throw 'Hay checks fallidos. Consulta reports/checks.json.' }
    }
    'Inspect' {
        if (!$Dump) { throw 'Indica -Dump con la carpeta extraída.' }
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/inspect_dump.py'), '--dump', $Dump, '--engine', $engine,
            '--out', (Join-Path $reports 'dump-inventory.json'))
    }
    'Prepare' {
        if (!$Dump) { throw 'Indica -Dump con la carpeta extraída.' }
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/prepare.py'), '--dump', $Dump, '--engine', $engine, '--out', $exefs)
    }
    'Cover' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/translate.py'), 'cover', '--engine', $engine, '--exefs', $exefs,
            '--out', (Join-Path $reports 'coverage'))
    }
    'Lift' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/translate.py'), 'lift', '--engine', $engine, '--exefs', $exefs,
            '--out', (Join-Path $Work 'lift'))
    }
    'ExtractShaders' {
        if (!$Dump) { throw 'Indica -Dump con la carpeta extraída.' }
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/extract_shaders.py'), '--romfs', (Join-Path $Dump 'romfs'),
            '--exefs', $exefs, '--engine', $engine, '--out', (Join-Path $Work 'shaders/corpus'), '--report', (Join-Path $reports 'shaders.json'))
    }
    'ExtractAllShaders' {
        if (!$Dump) { throw 'Indica -Dump con la carpeta extraída.' }
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/extract_shaders.py'), '--romfs', (Join-Path $Dump 'romfs'),
            '--exefs', $exefs, '--engine', $engine, '--include-extra-containers',
            '--out', (Join-Path $Work 'shaders/corpus-expanded'), '--report', (Join-Path $reports 'shaders-expanded.json'))
    }
    'Test' {
        $env:FEFW_ENGINE = $engine
        $testTemp = Join-Path $Work 'tests/tmp'
        New-Item -ItemType Directory -Path $testTemp -Force | Out-Null
        $env:TEMP = $testTemp
        $env:TMP = $testTemp
        Invoke-Checked $python @('-m', 'unittest', 'discover', '-s', (Join-Path $PSScriptRoot 'tests'), '-v')
    }
    'ProbeAudioSdk' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/probe_audio_sdk.py'), '--engine', $engine,
            '--exefs', $exefs, '--report', (Join-Path $reports 'rev16-sdk-oracle.json'))
    }
    'BuildShaders' {
        $env:DOTNET_CLI_HOME = Join-Path $Work 'dotnet-home'
        $env:NUGET_PACKAGES = Join-Path $Work 'nuget'
        $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
        Invoke-Checked $dotnetExe @(
            'build', (Join-Path $engine 'tools/shaders/harness/SpirvHarness.csproj'), '-c', 'Release',
            '--no-restore', '-m:1', '-nodeReuse:false', '-p:Platform=AnyCPU', '-p:UseSharedCompilation=false', "-p:PatchPython=$python")
    }
    'ShaderTest' {
        Invoke-Checked $dotnetExe @(
            (Join-Path $engine 'tools/shaders/build/bin/Release/net10.0/spirv_harness.dll'), '--test-cbuf-bindings')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_shader_textures.py'),
            '--work', $Work, '--report', (Join-Path $reports 'shader-texture-tests.json'))
    }
    { $_ -in @('TranslateShaders', 'TranslateAllShaders') } {
        $expanded = $Action -eq 'TranslateAllShaders'
        $corpus = Join-Path $Work $(if ($expanded) { 'shaders/corpus-expanded' } else { 'shaders/corpus' })
        $translated = Join-Path $Work $(if ($expanded) { 'shaders/translated-sdk22' } else { 'shaders/translated' })
        $dotnet = $dotnetExe
        $harness = Join-Path $engine 'tools/shaders/build/bin/Release/net10.0/spirv_harness.dll'
        if (!(Test-Path $dotnet) -or !(Test-Path $harness)) { throw 'Falta preparar .NET 10 y el traductor de shaders. Consulta README.md.' }
        $env:DOTNET_CLI_HOME = Join-Path $Work 'dotnet-home'
        $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
        $poolProofPath = Join-Path $reports 'texture-pools-gpu.json'
        if ($expanded -and (Test-Path -LiteralPath $poolProofPath)) {
            $poolProof = Get-Content -LiteralPath $poolProofPath -Raw | ConvertFrom-Json
            if ($poolProof.passed) { $env:FEFW_SHADER_POOL_CONTRACT = '32768x512' }
        }
        Invoke-Checked $dotnet @($harness, '--batch', $corpus, '--out', $translated, '--jobs', '1', '--no-val')
    }
    { $_ -in @('ValidateShaders', 'ValidateAllShaders') } {
        $expanded = $Action -eq 'ValidateAllShaders'
        $corpus = Join-Path $Work $(if ($expanded) { 'shaders/corpus-expanded' } else { 'shaders/corpus' })
        $translated = Join-Path $Work $(if ($expanded) { 'shaders/translated-sdk22' } else { 'shaders/translated' })
        $cache = Join-Path $Work $(if ($expanded) { 'shaders/validated-sdk22' } else { 'shaders/validated' })
        $variantWork = Join-Path $Work $(if ($expanded) { 'shaders/variant-check-sdk22' } else { 'shaders/variant-check' })
        $validationReport = Join-Path $reports $(if ($expanded) { 'shader-validation-sdk22.json' } else { 'shader-validation.json' })
        $variantReport = Join-Path $reports $(if ($expanded) { 'shader-variants-sdk22.json' } else { 'shader-variants.json' })
        $validator = Join-Path $Work 'build/spirv-tools/tools/spirv-val.exe'
        if (!(Test-Path $validator)) { throw 'Falta spirv-val. Consulta README.md.' }
        $arrayArguments = @()
        if ($expanded) { $arrayArguments = @('--verified-uniform-array-hash', 'bb2933bb746192a8', '--verified-uniform-array-hash', 'e8d644717c8e4c1e') }
        $matrixReport = Join-Path $reports 'texture-arrays-gpu.json'
        if ($expanded -and (Test-Path -LiteralPath $matrixReport)) {
            $matrix = Get-Content -LiteralPath $matrixReport -Raw | ConvertFrom-Json
            if ($matrix.exit_code -eq 0 -and $matrix.cases.Count -eq 96 -and
                @($matrix.cases | Where-Object { $_.exit_code -ne 0 -or $_.passed -ne 1 }).Count -eq 0) {
                $arrayArguments += '--verified-uniform-array-matrix'
            }
        }
        $poolProofPath = Join-Path $reports 'texture-pools-gpu.json'
        if ($expanded -and (Test-Path -LiteralPath $poolProofPath)) {
            $poolProof = Get-Content -LiteralPath $poolProofPath -Raw | ConvertFrom-Json
            if ($poolProof.passed -and $poolProof.cases.Count -eq 2) { $arrayArguments += '--verified-pool-arrays' }
        }
        Invoke-Checked $python (@((Join-Path $PSScriptRoot 'tools/validate_shaders.py'),
            '--input', $translated, '--out', $cache, '--validator', $validator,
            '--report', $validationReport, '--runtime-single-descriptors') + $arrayArguments)
        $variantArrayArguments = @($arrayArguments | Where-Object { $_ -eq '--verified-uniform-array-matrix' })
        Invoke-Checked $python (@((Join-Path $PSScriptRoot 'tools/check_shader_variants.py'),
            '--corpus', $corpus, '--work', $variantWork, '--cache', $cache,
            '--dotnet', $dotnetExe,
            '--harness', (Join-Path $engine 'tools/shaders/build/bin/Release/net10.0/spirv_harness.dll'),
            '--validator', $validator, '--report', $variantReport, '--runtime-single-descriptors') + $variantArrayArguments)
    }
    'VideoTest' {
        if (!$Dump) { throw 'VideoTest requiere el volcado local para su fixture privado.' }
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_av1.py'), '--work', $Work,
            '--movie', (Join-Path $Dump 'romfs/Movie/Product/MV00_00.webm'),
            '--report', (Join-Path $reports 'av1-native.json'))
    }
    'NativeTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked 'cmake' @('--build', (Join-Path $Work 'build/fefw'), '--target', 'fefw_runtime_tests', 'fefw_input_tests', 'fefw_config_tests', '--parallel', '1')
        Invoke-Checked 'ctest' @('--test-dir', (Join-Path $Work 'build/fefw'), '--output-on-failure')
    }
    'ComputeTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_compute_constants.py'), '--work', $Work,
            '--report', (Join-Path $reports 'compute-constants-gpu.json'))
    }
    'NumericTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_compute_numeric.py'), '--work', $Work,
            '--report', (Join-Path $reports 'compute-numeric-gpu.json'))
    }
    'ArrayTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_texture_arrays.py'), '--work', $Work,
            '--report', (Join-Path $reports 'texture-arrays-gpu.json'))
    }
    'RefreshComputeShaders' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/refresh_compute_shaders.py'), '--work', $Work,
            '--report', (Join-Path $reports 'compute-five-validation.json'))
    }
    'RootConstantsTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_root_constants.py'), '--work', $Work,
            '--report', (Join-Path $reports 'root-constants-gpu.json'))
    }
    'PoolTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_texture_pools.py'), '--work', $Work,
            '--report', (Join-Path $reports 'texture-pools-gpu.json'))
    }
    'RefreshPoolShaders' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/refresh_pool_shaders.py'), '--work', $Work,
            '--report', (Join-Path $reports 'pool-shaders-validation.json'))
    }
    'LiteralHandleTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_texture_arrays.py'), '--work', $Work,
            '--report', (Join-Path $reports 'literal-handles-gpu.json'), '--literal-handles')
    }
    'RefreshLiteralHandles' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/refresh_literal_handles.py'), '--work', $Work,
            '--report', (Join-Path $reports 'literal-handles-validation.json'))
    }
    'RefreshUniformArrays' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/refresh_uniform_arrays.py'), '--work', $Work,
            '--report', (Join-Path $reports 'uniform-arrays-validation.json'))
    }
    'RefreshLightingArrays' {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools/refresh_lighting_arrays.py'), '--work', $Work,
            '--report', (Join-Path $reports 'lighting-arrays-validation.json'))
    }
    'FloatingPointTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_fp_sign_native.py'), '--engine', $engine,
            '--work', (Join-Path $Work 'tests/fp-sign'), '--report', (Join-Path $reports 'fp-sign-native.json'))
    }
    'BranchReturnTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_br_return.py'), '--engine', $engine,
            '--work', (Join-Path $Work 'tests/br-return'), '--report', (Join-Path $reports 'br-return-native.json'))
    }
    'Configure' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked 'cmake' @('-S', $PSScriptRoot, '-B', (Join-Path $Work 'build/fefw'), '-G', 'Ninja',
            '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl', '-DCMAKE_BUILD_TYPE=Release',
            "-DFEFW_ENGINE=$engine", "-DFEFW_LIFT_DIR=$(Join-Path $Work 'lift')", "-DFEFW_EXEFS=$exefs", "-DFEFW_WORK=$Work")
    }
    'Build' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked 'cmake' @('--build', (Join-Path $Work 'build/fefw'), '--target', 'fefw', '--parallel', '1')
    }
    'Package' {
        & (Join-Path $PSScriptRoot 'tools/New-LocalPackage.ps1') -Work $Work -Dump $Dump
    }
    'GpuValidationTest' {
        . (Join-Path $PSScriptRoot 'tools/Initialize-BuildEnv.ps1')
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tests/check_gpu_validation.py'), '--work', $Work,
            '--report', (Join-Path $reports 'gpu-validation.json'))
    }
    'Run' {
        $exe = Join-Path $Work 'build/fefw/fefw.exe'
        if (!(Test-Path -LiteralPath $exe)) { throw 'No existe fefw.exe. La compilación nativa todavía no se ha completado.' }
        if (!$Dump) { throw 'Indica -Dump con la carpeta extraída.' }
        $runDir = Join-Path $Work 'run'
        New-Item -ItemType Directory -Force -Path $runDir | Out-Null
        $ini = Join-Path $runDir 'fefw.ini'
        if ($InputScript) {
            $InputScript = (Resolve-Path -LiteralPath $InputScript -ErrorAction Stop).Path
            if (!(Test-Path -LiteralPath $InputScript -PathType Leaf)) { throw 'InputScript debe ser un archivo.' }
        }
        $shaderCache = Join-Path $Work 'shaders/validated'
        if (Test-Path -LiteralPath (Join-Path $Work 'shaders/validated-sdk22/results.tsv')) {
            $shaderCache = Join-Path $Work 'shaders/validated-sdk22'
        }
        @"
[fs]
exefs = $exefs
romfs = $(Join-Path $Dump 'romfs')
save_dir = $(Join-Path $runDir 'save')
[boot]
gfx = headless
max_seconds = $MaxSeconds
screenshot_at = $ScreenshotAt
screenshot_every = $(if ($ScreenshotAt -gt 0) { '2' } else { '0' })
[nvn]
shader_cache = $shaderCache
dlss_spatial_fallback = $(if ($DlssSpatialFallback) { '1' } else { '0' })
[audio]
revision16_probe = $(if ($Revision16Probe) { '1' } else { '0' })
[title]
account_save_size = $([long]$ProbeSaveMiB * 1024 * 1024)
"@ | Set-Content -LiteralPath $ini -Encoding utf8
        if ($InputScript) {
            @"
[test]
script = $InputScript
frame_source = tick
freeze_exit = false
out = $(Join-Path $runDir 'input-test')
"@ | Add-Content -LiteralPath $ini -Encoding utf8
        }
        Push-Location $runDir
        try { Invoke-Checked $exe @('--ini', $ini, '--exefs', $exefs, '--headless', '--max-seconds', "$MaxSeconds") }
        finally { Pop-Location }
    }
}
