param([string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'run_acceptance_desktop.ps1'
foreach ($expected in @(7, 0)) {
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $runner -ProbeExitCode $expected
    if ($LASTEXITCODE -ne $expected) { throw "Expected child exit $expected, received $LASTEXITCODE" }
}
& powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $runner -ProbeExitCode 0 -ProbeDelayMilliseconds 2000 -ProcessTimeoutMilliseconds 100
if ($LASTEXITCODE -ne 2) { throw "A timeout must fail with exit 2, received $LASTEXITCODE" }
if ($BuildDirectory) {
    foreach ($case in @('success', 'budget', 'input', 'sharp', 'stop', 'missing', 'incomplete')) {
        & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $runner -Performance -Configuration Release -BuildDirectory $BuildDirectory -PerformanceControl $case
        $expected = if ($case -eq 'success') { 0 } else { 1 }
        if ($LASTEXITCODE -ne $expected) { throw "Controlled $case expected $expected, received $LASTEXITCODE" }
    }
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $runner -Performance -Configuration Release -BuildDirectory $BuildDirectory -PerformanceControl timeout -ProcessTimeoutMilliseconds 100
    if ($LASTEXITCODE -ne 2) { throw "Performance timeout expected 2, received $LASTEXITCODE" }
}
Write-Output 'PASS acceptance supervisor propagates child success, failure, and timeout without desktop API calls.'
