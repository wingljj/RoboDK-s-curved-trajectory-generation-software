[CmdletBinding()]
param(
    [string]$QtRoot = 'D:\Software\QT\content\6.5.3\msvc2019_64',
    [string]$BuildName = 'native-release',
    [string]$Executable = '',
    [string]$OutputDirectory = '',
    [switch]$IsolatedDistribution
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = if ($projectRoot -match '[^\x00-\x7F]' -and (Test-Path -LiteralPath 'E:\surfacepathstudio-20261003')) { 'E:\surfacepathstudio-20261003' } else { $projectRoot }
if (-not $Executable) { $Executable = Join-Path $buildRoot ('build\' + $BuildName + '\SurfacePathStudio.exe') }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $buildRoot ('build\' + $BuildName + '\desktop-smoke') }
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Application not found: $Executable" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
if ($IsolatedDistribution) {
    $env:Path = $env:SystemRoot + '\System32;' + $env:SystemRoot
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $env:QML2_IMPORT_PATH = $null
} else { $env:Path = (Join-Path $QtRoot 'bin') + ';' + $env:Path }
$reportPath = Join-Path $OutputDirectory 'desktop-smoke.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }
$probe = Start-Process -FilePath $Executable -ArgumentList @('--smoke-test',('"' + $OutputDirectory + '"')) -WindowStyle Hidden -PassThru
if (-not $probe.WaitForExit(45000)) {
    $probe.Kill()
    throw "Smoke test did not finish within 45 seconds (PID $($probe.Id))"
}
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Application produced no report (exit $($probe.ExitCode))" }
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if ($probe.ExitCode -ne 0 -or -not $report.passed) { throw "Desktop smoke failed: $($report.error) (exit $($probe.ExitCode))" }
foreach ($feature in @('productBranding', 'aboutDialog', 'generatedTrajectoryAndNormals', 'continuousCsv', 'stepDragDrop')) {
    if ($report.$feature -ne $true) { throw "Desktop smoke missing current feature verification: $feature" }
}
$report | ConvertTo-Json -Depth 8
Write-Host "Screenshots and CSV: $OutputDirectory"
