[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$builder = Join-Path $projectRoot 'scripts\Build-Installer.ps1'
$testRoot = Join-Path $env:TEMP ('SurfacePathStudio-InstallerValidation-' + [Guid]::NewGuid().ToString('N'))
$fixtureRoot = Join-Path $testRoot 'fixture'
$outputRoot = Join-Path $testRoot 'output'
$encoding = [System.Text.UTF8Encoding]::new($true)
$results = [System.Collections.Generic.List[string]]::new()

# Deliberately small synthetic fixtures: this validates package selection, not application execution.
$x64Header = [byte[]]::new(72)
$x64Header[0] = 0x4D; $x64Header[1] = 0x5A
$x64Header[0x3C] = 64
$x64Header[64] = 0x50; $x64Header[65] = 0x45
$x64Header[68] = 0x64; $x64Header[69] = 0x86
$fixtureFiles = @(
    'SurfacePathStudio.exe', 'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll',
    'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'imageformats\qico.dll',
    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'README.md', 'THIRD_PARTY_NOTICES.md',
    'licenses\Qt-LICENSE.txt', 'licenses\OCCT_LGPL_EXCEPTION.txt', 'licenses\LICENSE_LGPL_21.txt',
    'examples\models\plane.step', 'examples\models\cylinder.step',
    'examples\models\cylinder_full.step', 'examples\models\sphere_trimmed.step',
    'examples\models\bspline_trimmed.step', 'examples\models\adjacent_bspline.step',
    'examples\models\inner_hole.step', 'docs\validation.md',
    'docs\validation-assets\surface-path.csv', 'docs\validation-assets\surface-path-xyz-normal.csv',
    'SurfacePath_999.csv', 'customer-private.csv', 'random-user.dll', 'notes.txt', 'unrelated.exe'
)
foreach ($relative in $fixtureFiles) {
    $target = Join-Path $fixtureRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    if ($relative -in @('random-user.dll', 'unrelated.exe')) {
        [System.IO.File]::WriteAllText($target, 'unapproved non-binary file', $encoding)
    }
    elseif ([System.IO.Path]::GetExtension($relative) -in @('.dll', '.exe')) {
        [System.IO.File]::WriteAllBytes($target, $x64Header)
    }
    else { [System.IO.File]::WriteAllText($target, 'installer validation fixture', $encoding) }
}
$manifestPath = Join-Path $fixtureRoot 'build-manifest.json'
$manifest = [ordered]@{
    ApplicationVersion='0.1.2'; Qt='5.15.0'; Architecture='x64'; Configuration='Release'
    Sha256=(Get-FileHash -LiteralPath (Join-Path $fixtureRoot 'SurfacePathStudio.exe') -Algorithm SHA256).Hash
}
function Save-Manifest { [System.IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json), $encoding) }
function Assert-Rejection([string]$ExpectedMessage, [int]$SelectedQt = 5) {
    $rejected = $false
    try { & $builder -QtMajor $SelectedQt -DistDirectory $fixtureRoot -OutputDirectory $outputRoot -ValidateOnly }
    catch {
        if ($_.Exception.Message -notlike "*$ExpectedMessage*") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Expected validation rejection: $ExpectedMessage" }
    $results.Add("PASS: $ExpectedMessage")
}
Save-Manifest
& $builder -QtMajor 5 -DistDirectory $fixtureRoot -OutputDirectory $outputRoot -ValidateOnly
$payloadPath = Join-Path $outputRoot '上海卫星装备研究所曲面轨迹生成软件-0.1.2-Qt5-x64-Setup-payload.json'
$payload = Get-Content -LiteralPath $payloadPath -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($excluded in @('SurfacePath_999.csv', 'customer-private.csv', 'random-user.dll', 'notes.txt', 'unrelated.exe')) {
    if ($payload.Files.Path -contains $excluded) { throw "Unapproved file was selected: $excluded" }
}
foreach ($included in @('docs\validation-assets\surface-path.csv', 'docs\validation-assets\surface-path-xyz-normal.csv')) {
    if ($payload.Files.Path -notcontains $included) { throw "Approved validation asset was omitted: $included" }
}
$results.Add('PASS: user exports and unknown executables excluded; approved validation CSV retained')
Assert-Rejection 'does not match manifest Qt' 6
$manifest.ApplicationVersion = '0.1.1'; Save-Manifest
Assert-Rejection 'ApplicationVersion must be 0.1.2'
$manifest.ApplicationVersion = '0.1.2'
$manifest.Configuration = 'Debug'; Save-Manifest
Assert-Rejection 'Only x64 Release deployments'
$manifest.Configuration = 'Release'
$manifest.Sha256 = '0' * 64; Save-Manifest
Assert-Rejection 'SHA256 does not match'
$manifest.Sha256 = (Get-FileHash -LiteralPath (Join-Path $fixtureRoot 'SurfacePathStudio.exe') -Algorithm SHA256).Hash
Save-Manifest
$jpegPath = Join-Path $fixtureRoot 'imageformats\qjpeg.dll'
[System.IO.File]::Move($jpegPath, "$jpegPath.omitted")
Assert-Rejection 'Required deployment file is missing: imageformats\qjpeg.dll'
[System.IO.File]::Move("$jpegPath.omitted", $jpegPath)
$x86Header = [byte[]]$x64Header.Clone()
$x86Header[68] = 0x4C; $x86Header[69] = 0x01
[System.IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'SurfacePathStudio.exe'), $x86Header)
$manifest.Sha256 = (Get-FileHash -LiteralPath (Join-Path $fixtureRoot 'SurfacePathStudio.exe') -Algorithm SHA256).Hash
Save-Manifest
Assert-Rejection 'Expected an x64 PE binary'
$results | Write-Output
Write-Host "Passed $($results.Count) installer validation checks."
Write-Host "Small synthetic fixtures retained for inspection: $testRoot"
