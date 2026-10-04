$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'run_sprite_desktop.ps1'
foreach ($expected in @(7, 0)) {
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $runner -ProbeExitCode $expected
    if ($LASTEXITCODE -ne $expected) { throw "Expected child exit $expected, received $LASTEXITCODE" }
}
Write-Output 'PASS desktop launcher propagates failure and success without desktop API calls.'
