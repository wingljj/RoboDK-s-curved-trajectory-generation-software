[CmdletBinding()]
param(
    [string]$UcrtRoot = 'C:\Program Files (x86)\Windows Kits\10\Redist\10.0.19041.0\ucrt\DLLs\x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$builder = Join-Path $projectRoot 'scripts\Build-Installer.ps1'
if (-not (Get-Command $builder).Parameters.ContainsKey('TargetWindows')) {
    throw 'Build-Installer.ps1 must support the TargetWindows parameter.'
}
$testRoot = Join-Path $env:TEMP ('SurfacePathStudio-Win7InstallerValidation-' + [Guid]::NewGuid().ToString('N'))
$fixtureRoot = Join-Path $testRoot 'fixture'
$outputRoot = Join-Path $testRoot 'output'
$encoding = [System.Text.UTF8Encoding]::new($true)
$results = [System.Collections.Generic.List[string]]::new()

# Synthetic application headers test selection; genuine SDK DLLs test UCRT metadata.
$x64Header = [byte[]]::new(72)
$x64Header[0] = 0x4D; $x64Header[1] = 0x5A; $x64Header[0x3C] = 64
$x64Header[64] = 0x50; $x64Header[65] = 0x45
$x64Header[68] = 0x64; $x64Header[69] = 0x86
$fixtureFiles = @(
    'SurfacePathStudio.exe', 'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll',
    'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'imageformats\qico.dll',
    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'README.md', 'THIRD_PARTY_NOTICES.md', 'licenses\Qt-LICENSE.txt',
    'licenses\OCCT_LGPL_EXCEPTION.txt', 'licenses\LICENSE_LGPL_21.txt',
    'examples\models\plane.step', 'examples\models\cylinder.step',
    'examples\models\cylinder_full.step', 'examples\models\sphere_trimmed.step',
    'examples\models\bspline_trimmed.step', 'examples\models\adjacent_bspline.step',
    'examples\models\inner_hole.step', 'docs\validation.md',
    'customer-private.csv', 'random-user.dll', 'api-ms-win-unknown-l1-1-0.dll',
    'nested\api-ms-win-crt-runtime-l1-1-0.dll'
)
foreach ($relative in $fixtureFiles) {
    $target = Join-Path $fixtureRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    if ([System.IO.Path]::GetExtension($relative) -in @('.dll', '.exe')) {
        [System.IO.File]::WriteAllBytes($target, $x64Header)
    } else { [System.IO.File]::WriteAllText($target, 'Win7 installer validation fixture', $encoding) }
}
$ucrtFiles = @(Get-ChildItem -LiteralPath $UcrtRoot -File | Where-Object {
    $_.Name -eq 'ucrtbase.dll' -or $_.Name -like 'api-ms-win-*.dll'
})
if ($ucrtFiles.Count -lt 40) { throw "A complete Microsoft Windows SDK x64 UCRT directory is required: $UcrtRoot" }
$ucrtEntries = foreach ($file in $ucrtFiles) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $fixtureRoot $file.Name)
    $version = $file.VersionInfo
    [ordered]@{
        Path = $file.Name
        Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        Version = '{0}.{1}.{2}.{3}' -f $version.FileMajorPart, $version.FileMinorPart, $version.FileBuildPart, $version.FilePrivatePart
    }
}
$manifestPath = Join-Path $fixtureRoot 'build-manifest.json'
$manifest = [ordered]@{
    ApplicationVersion='0.1.2'; Qt='5.15.0'; Architecture='x64'; Configuration='Release'
    Sha256=(Get-FileHash -LiteralPath (Join-Path $fixtureRoot 'SurfacePathStudio.exe') -Algorithm SHA256).Hash
    TargetWindows='Windows7SP1'; MinimumWindowsVersion='6.1sp1'; ApplicationLocalUcrt=$true
    UcrtVersion=($ucrtEntries | Where-Object Path -eq 'ucrtbase.dll').Version
    UcrtDllCount=$ucrtFiles.Count; UcrtSourceDirectory=$UcrtRoot; ApplicationLocalUcrtFiles=@($ucrtEntries)
}
function Save-Manifest { [System.IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6), $encoding) }
function Assert-Rejection([string]$ExpectedMessage, [int]$SelectedQt = 5, [string]$SelectedTarget = 'Windows7SP1') {
    $rejected = $false
    try { & $builder -QtMajor $SelectedQt -TargetWindows $SelectedTarget -DistDirectory $fixtureRoot -OutputDirectory $outputRoot -ValidateOnly }
    catch {
        if ($_.Exception.Message -notlike "*$ExpectedMessage*") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Expected validation rejection: $ExpectedMessage" }
    $results.Add("PASS: $ExpectedMessage")
}
Save-Manifest
Assert-Rejection 'Windows7SP1 requires QtMajor 5' 6
$manifest.TargetWindows = 'Windows10'; Save-Manifest
Assert-Rejection 'manifest.TargetWindows must be Windows7SP1'
$manifest.TargetWindows = 'Windows7SP1'
$manifest.MinimumWindowsVersion = '10.0'; Save-Manifest
Assert-Rejection 'manifest.MinimumWindowsVersion must be 6.1sp1'
$manifest.MinimumWindowsVersion = '6.1sp1'
$manifest.ApplicationLocalUcrt = $false; Save-Manifest
Assert-Rejection 'manifest.ApplicationLocalUcrt must be true'
$manifest.ApplicationLocalUcrt = $true; Save-Manifest
$manifest.UcrtDllCount = $ucrtFiles.Count - 1; Save-Manifest
Assert-Rejection 'matching UcrtDllCount'
$manifest.UcrtDllCount = $ucrtFiles.Count
$originalVersion = $manifest.ApplicationLocalUcrtFiles[0].Version
$manifest.ApplicationLocalUcrtFiles[0].Version = '10.0.0.0'; Save-Manifest
Assert-Rejection 'UCRT file version does not match'
$manifest.ApplicationLocalUcrtFiles[0].Version = $originalVersion
Save-Manifest
$ucrtPath = Join-Path $fixtureRoot 'ucrtbase.dll'
[System.IO.File]::Move($ucrtPath, "$ucrtPath.omitted")
Assert-Rejection 'Required UCRT deployment file is missing: ucrtbase.dll'
[System.IO.File]::Move("$ucrtPath.omitted", $ucrtPath)
$corePath = Join-Path $fixtureRoot 'api-ms-win-core-synch-l1-2-0.dll'
[System.IO.File]::Move($corePath, "$corePath.omitted")
Assert-Rejection 'Required UCRT deployment file is missing: api-ms-win-core-synch-l1-2-0.dll'
[System.IO.File]::Move("$corePath.omitted", $corePath)
$originalHash = $manifest.ApplicationLocalUcrtFiles[0].Sha256
$manifest.ApplicationLocalUcrtFiles[0].Sha256 = '0' * 64; Save-Manifest
Assert-Rejection 'UCRT SHA256 does not match'
$manifest.ApplicationLocalUcrtFiles[0].Sha256 = $originalHash
$manifest.UcrtVersion = '10.0.0.0'; Save-Manifest
Assert-Rejection 'UcrtVersion does not match'
$manifest.UcrtVersion = ($ucrtEntries | Where-Object Path -eq 'ucrtbase.dll').Version
Save-Manifest
& $builder -QtMajor 5 -TargetWindows Windows7SP1 -DistDirectory $fixtureRoot -OutputDirectory $outputRoot -ValidateOnly
$outputBaseName = '上海卫星装备研究所曲面轨迹生成软件-0.1.2-Win7SP1-Qt5-x64-Setup'
$payload = Get-Content -LiteralPath (Join-Path $outputRoot "$outputBaseName-payload.json") -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($file in $ucrtFiles) {
    if ($payload.Files.Path -notcontains $file.Name) { throw "UCRT DLL was omitted: $($file.Name)" }
}
foreach ($excluded in @('customer-private.csv', 'random-user.dll', 'api-ms-win-unknown-l1-1-0.dll', 'nested\api-ms-win-crt-runtime-l1-1-0.dll')) {
    if ($payload.Files.Path -contains $excluded) { throw "Unapproved file was selected: $excluded" }
}
$introEntry = @($payload.Files | Where-Object Path -eq '软件介绍.txt')
if ($introEntry.Count -ne 1 -or $introEntry[0].Sha256 -ne (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '软件介绍-Win7SP1.txt') -Algorithm SHA256).Hash) {
    throw 'The Win7 introduction must be installed as 软件介绍.txt.'
}
$fileList = Get-Content -LiteralPath (Join-Path $outputRoot '_packaging\Windows7SP1-Qt5\payload-files.iss') -Raw -Encoding UTF8
if ($fileList -notmatch 'api-ms-win-core-synch-l1-2-0.dll' -or $fileList -notmatch 'ucrtbase.dll') {
    throw 'Generated Inno include omits the application-local UCRT.'
}
$results.Add('PASS: complete x64 UCRT and Win7 introduction selected; unrelated and nested DLLs excluded')
$fakeCompiler = Join-Path $testRoot 'Inno7-HelpOnly.ps1'
[System.IO.File]::WriteAllText($fakeCompiler, "param([string[]]`$Arguments)`r`nWrite-Output 'Inno Setup 7 Command-Line Compiler'`r`n", $encoding)
$rejected = $false
try { & $builder -QtMajor 5 -TargetWindows Windows7SP1 -DistDirectory $fixtureRoot -OutputDirectory $outputRoot -InnoCompiler $fakeCompiler }
catch {
    if ($_.Exception.Message -notlike '*requires an Inno Setup 6 compiler*') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'Win7 packaging must reject an Inno Setup 7 compiler.' }
$results.Add('PASS: Inno Setup 7 compiler is rejected before compilation')
$results | Write-Output
Write-Host "Passed $($results.Count) Win7 installer validation checks."
Write-Host "Small fixtures retained for inspection: $testRoot"
