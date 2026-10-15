[CmdletBinding()]
param(
    [ValidateSet(5, 6)]
    [int]$QtMajor = 5,
    [ValidateSet('Windows10', 'Windows7SP1')]
    [string]$TargetWindows = 'Windows10',
    [string]$DistDirectory,
    [string]$OutputDirectory,
    [string]$InnoCompiler,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path $PSScriptRoot -Parent
$applicationVersion = '0.1.2'
$applicationName = '上海卫星装备研究所曲面轨迹生成软件'
$isWindows7 = $TargetWindows -eq 'Windows7SP1'
if ($isWindows7 -and $QtMajor -ne 5) { throw 'Windows7SP1 requires QtMajor 5.' }
$introductionFile = '软件介绍.txt'
if ($isWindows7) { $introductionFile = '软件介绍-Win7SP1.txt' }
$introductionPath = Join-Path $projectRoot "installer\$introductionFile"
if ([string]::IsNullOrWhiteSpace($DistDirectory)) {
    if ($isWindows7) { $DistDirectory = Join-Path $projectRoot 'dist-win7-x64' }
    else { $DistDirectory = Join-Path $projectRoot "dist-branding-qt$QtMajor" }
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot 'releases'
}
$distRoot = (Resolve-Path -LiteralPath $DistDirectory).ProviderPath.TrimEnd('\', '/')
$outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$manifestPath = Join-Path $distRoot 'build-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Missing build-manifest.json in $distRoot. Build a clean Release deployment first."
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($field in @('ApplicationVersion', 'Qt', 'Architecture', 'Configuration', 'Sha256')) {
    if (-not ($manifest.PSObject.Properties.Name -contains $field)) {
        throw "build-manifest.json is missing $field."
    }
}
if ($manifest.ApplicationVersion -ne $applicationVersion) {
    throw "ApplicationVersion must be $applicationVersion; found $($manifest.ApplicationVersion)."
}
if ([string]$manifest.Qt -notmatch "^$QtMajor\.\d+\.\d+") {
    throw "QtMajor $QtMajor does not match manifest Qt $($manifest.Qt)."
}
if ($manifest.Architecture -ne 'x64' -or $manifest.Configuration -ne 'Release') {
    throw 'Only x64 Release deployments can be packaged.'
}
$ucrtEntries = @()
if ($isWindows7) {
    foreach ($field in @('TargetWindows', 'MinimumWindowsVersion', 'ApplicationLocalUcrt', 'UcrtVersion', 'UcrtDllCount', 'UcrtSourceDirectory', 'ApplicationLocalUcrtFiles')) {
        if (-not ($manifest.PSObject.Properties.Name -contains $field)) { throw "Win7 build-manifest.json is missing $field." }
    }
    if ($manifest.TargetWindows -ne 'Windows7SP1') { throw 'manifest.TargetWindows must be Windows7SP1.' }
    if ($manifest.MinimumWindowsVersion -ne '6.1sp1') { throw 'manifest.MinimumWindowsVersion must be 6.1sp1.' }
    if ($manifest.ApplicationLocalUcrt -isnot [bool] -or -not $manifest.ApplicationLocalUcrt) { throw 'manifest.ApplicationLocalUcrt must be true.' }
    $ucrtEntries = @($manifest.ApplicationLocalUcrtFiles)
    if ($ucrtEntries.Count -lt 40 -or $manifest.UcrtDllCount -ne $ucrtEntries.Count) {
        throw 'Win7 requires the complete Windows SDK UCRT file set and a matching UcrtDllCount.'
    }
    $ucrtNames = @($ucrtEntries | ForEach-Object {
        foreach ($field in @('Path', 'Sha256', 'Version')) {
            if (-not ($_.PSObject.Properties.Name -contains $field)) { throw "ApplicationLocalUcrtFiles entry is missing $field." }
        }
        if ($_.Path -notmatch '^(ucrtbase\.dll|api-ms-win-(core|crt)-[a-z0-9]+-l[0-9]+-[0-9]+-[0-9]+\.dll)$') {
            throw "Unapproved UCRT deployment path: $($_.Path)"
        }
        [string]$_.Path
    })
    if (@($ucrtNames | Sort-Object -Unique).Count -ne $ucrtNames.Count) { throw 'Duplicate UCRT deployment paths in build-manifest.json.' }
    foreach ($essential in @('ucrtbase.dll', 'api-ms-win-crt-runtime-l1-1-0.dll', 'api-ms-win-core-file-l2-1-0.dll', 'api-ms-win-core-synch-l1-2-0.dll')) {
        if ($ucrtNames -notcontains $essential) { throw "Required UCRT manifest entry is missing: $essential" }
    }
    # Compare the exact trusted SDK architecture directory, including its core forwarders.
    if (-not (Test-Path -LiteralPath $manifest.UcrtSourceDirectory -PathType Container)) {
        throw 'UcrtSourceDirectory must identify the original Microsoft Windows SDK UCRT directory.'
    }
    $sdkNames = @(Get-ChildItem -LiteralPath $manifest.UcrtSourceDirectory -File | Where-Object {
        $_.Name -eq 'ucrtbase.dll' -or $_.Name -like 'api-ms-win-*.dll'
    } | ForEach-Object Name)
    if (@(Compare-Object $sdkNames $ucrtNames).Count -ne 0) { throw 'UCRT file set does not match the source Windows SDK directory.' }
    foreach ($entry in $ucrtEntries) {
        $path = Join-Path $distRoot $entry.Path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required UCRT deployment file is missing: $($entry.Path)" }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Sha256) { throw "UCRT SHA256 does not match: $($entry.Path)" }
        if ((Get-FileHash -LiteralPath (Join-Path $manifest.UcrtSourceDirectory $entry.Path) -Algorithm SHA256).Hash -ne $entry.Sha256) {
            throw "UCRT SHA256 does not match the source Windows SDK: $($entry.Path)"
        }
        $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($path)
        $numericVersion = '{0}.{1}.{2}.{3}' -f $version.FileMajorPart, $version.FileMinorPart, $version.FileBuildPart, $version.FilePrivatePart
        if ($numericVersion -ne $entry.Version) { throw "UCRT file version does not match: $($entry.Path)" }
        if ($entry.Path -eq 'ucrtbase.dll' -and ($numericVersion -ne $manifest.UcrtVersion -or [version]$numericVersion -lt [version]'10.0.10240.16390')) {
            throw 'UcrtVersion does not match a supported ucrtbase.dll.'
        }
    }
} elseif (($manifest.PSObject.Properties.Name -contains 'TargetWindows') -and $manifest.TargetWindows -ne 'Windows10') {
    throw 'The deployment target does not match TargetWindows Windows10.'
}

# Explicit runtime and plugin names; never include dist\* or recurse through user files.
$requiredFiles = @(
    'SurfacePathStudio.exe', "Qt${QtMajor}Core.dll", "Qt${QtMajor}Gui.dll", "Qt${QtMajor}Widgets.dll",
    'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'imageformats\qico.dll',
    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'README.md', 'THIRD_PARTY_NOTICES.md', 'build-manifest.json',
    'licenses\Qt-LICENSE.txt', 'licenses\OCCT_LGPL_EXCEPTION.txt', 'licenses\LICENSE_LGPL_21.txt',
    'examples\models\plane.step', 'examples\models\cylinder.step',
    'examples\models\cylinder_full.step', 'examples\models\sphere_trimmed.step',
    'examples\models\bspline_trimmed.step', 'examples\models\adjacent_bspline.step',
    'examples\models\inner_hole.step'
)
if ($isWindows7) { $requiredFiles += @($ucrtEntries | ForEach-Object Path) }
$optionalFiles = @(
    "Qt${QtMajor}Svg.dll", "Qt${QtMajor}Network.dll", "Qt${QtMajor}Pdf.dll",
    'concrt140.dll', 'vccorlib140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll',
    'd3dcompiler_47.dll', 'opengl32sw.dll', 'libEGL.dll', 'libGLESv2.dll',
    'styles\qwindowsvistastyle.dll', 'styles\qmodernwindowsstyle.dll',
    'iconengines\qsvgicon.dll', 'generic\qtuiotouchplugin.dll',
    'networkinformation\qnetworklistmanager.dll',
    'tls\qcertonlybackend.dll', 'tls\qopensslbackend.dll', 'tls\qschannelbackend.dll',
    'imageformats\qgif.dll', 'imageformats\qicns.dll',
    'imageformats\qpdf.dll', 'imageformats\qsvg.dll',
    'imageformats\qtga.dll', 'imageformats\qtiff.dll', 'imageformats\qwbmp.dll',
    'imageformats\qwebp.dll', 'examples\README.md',
    'licenses\LICENSE.LGPLv3', 'licenses\LICENSE.GPLv3', 'licenses\LICENSE.GPLv2',
    'licenses\sdk_license.rtf', 'licenses\sdk_third_party_notices.rtf',
    'docs\validation-assets\surface-path.csv',
    'docs\validation-assets\surface-path-xyz-normal.csv'
)
foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $distRoot $relativePath) -PathType Leaf)) {
        throw "Required deployment file is missing: $relativePath"
    }
}
$executableHash = (Get-FileHash -LiteralPath (Join-Path $distRoot 'SurfacePathStudio.exe') -Algorithm SHA256).Hash
if ($manifest.Sha256 -ne $executableHash) {
    throw 'SurfacePathStudio.exe SHA256 does not match build-manifest.json. Rebuild the deployment.'
}
$selectedFiles = [System.Collections.Generic.List[string]]::new()
foreach ($relativePath in $requiredFiles + $optionalFiles) {
    if (Test-Path -LiteralPath (Join-Path $distRoot $relativePath) -PathType Leaf) {
        $selectedFiles.Add($relativePath)
    }
}
$docsRoot = Join-Path $distRoot 'docs'
$documentation = @(Get-ChildItem -LiteralPath $docsRoot -Filter '*.md' -File)
if ($documentation.Count -eq 0) { throw 'The deployment must include docs\*.md.' }
foreach ($file in $documentation) { $selectedFiles.Add("docs\$($file.Name)") }
$validationRoot = Join-Path $docsRoot 'validation-assets'
if (Test-Path -LiteralPath $validationRoot -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $validationRoot -File) {
        if ($file.Extension -in @('.png', '.json')) {
            $selectedFiles.Add("docs\validation-assets\$($file.Name)")
        }
    }
}
$selectedFiles = @($selectedFiles | Sort-Object -Unique)

function Assert-X64Binary([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE binary: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt $stream.Length - 6) { throw "Invalid PE header: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Expected an x64 PE binary: $Path"
        }
    }
    finally { $reader.Dispose(); $stream.Dispose() }
}
foreach ($relativePath in $selectedFiles) {
    if ([System.IO.Path]::GetExtension($relativePath) -in @('.exe', '.dll')) {
        Assert-X64Binary (Join-Path $distRoot $relativePath)
    }
}

$resourceRoot = Join-Path $projectRoot 'resources'
foreach ($file in @('casc.ico', 'installer-wizard.bmp', 'installer-small.bmp')) {
    if (-not (Test-Path -LiteralPath (Join-Path $resourceRoot $file) -PathType Leaf)) {
        throw "Missing branding resource: resources\$file"
    }
}
if (-not (Test-Path -LiteralPath $introductionPath -PathType Leaf)) {
    throw "Missing installer\$introductionFile."
}

$outputBaseName = "$applicationName-$applicationVersion-Qt$QtMajor-x64-Setup"
if ($isWindows7) { $outputBaseName = "$applicationName-$applicationVersion-Win7SP1-Qt$QtMajor-x64-Setup" }
$packagingSubdirectory = "Qt$QtMajor"
if ($isWindows7) { $packagingSubdirectory = "$TargetWindows-Qt$QtMajor" }
$packagingRoot = Join-Path $outputRoot "_packaging\$packagingSubdirectory"
New-Item -ItemType Directory -Force -Path $packagingRoot | Out-Null
$fileListPath = Join-Path $packagingRoot 'payload-files.iss'
$utf8Bom = [System.Text.UTF8Encoding]::new($true)
function ConvertTo-InnoValue([string]$Value) { return $Value.Replace('"', '""').Replace('{', '{{') }
$entries = foreach ($relativePath in $selectedFiles) {
    $source = ConvertTo-InnoValue (Join-Path $distRoot $relativePath)
    $parent = Split-Path $relativePath -Parent
    $destination = '{app}'
    if (-not [string]::IsNullOrWhiteSpace($parent)) { $destination += '\' + (ConvertTo-InnoValue $parent) }
    'Source: "{0}"; DestDir: "{1}"; Flags: ignoreversion' -f $source, $destination
}
[System.IO.File]::WriteAllLines($fileListPath, [string[]]$entries, $utf8Bom)
$payloadFiles = foreach ($relativePath in $selectedFiles) {
    $source = Join-Path $distRoot $relativePath
    [ordered]@{ Path=$relativePath; Bytes=(Get-Item -LiteralPath $source).Length; Sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash }
}
foreach ($extra in @(
    @{ Source=(Join-Path $resourceRoot 'casc.ico'); Destination='resources\casc.ico' },
    @{ Source=$introductionPath; Destination='软件介绍.txt' }
)) {
    $payloadFiles += [ordered]@{ Path=$extra.Destination; Bytes=(Get-Item -LiteralPath $extra.Source).Length; Sha256=(Get-FileHash -LiteralPath $extra.Source -Algorithm SHA256).Hash }
}
$payloadManifestPath = Join-Path $outputRoot "$outputBaseName-payload.json"
$payloadManifest = [ordered]@{
    Application=$applicationName; ApplicationVersion=$applicationVersion; Qt=$manifest.Qt
    Architecture='x64'; Configuration='Release'; CreatedUtc=[DateTime]::UtcNow.ToString('o')
    FileCount=$payloadFiles.Count; Files=$payloadFiles
}
if ($isWindows7) {
    $payloadManifest.TargetWindows = 'Windows7SP1'
    $payloadManifest.MinimumWindowsVersion = '6.1sp1'
    $payloadManifest.ApplicationLocalUcrt = $true
    $payloadManifest.UcrtVersion = $manifest.UcrtVersion
    $payloadManifest.UcrtDllCount = $ucrtEntries.Count
}
[System.IO.File]::WriteAllText($payloadManifestPath, ($payloadManifest | ConvertTo-Json -Depth 5), $utf8Bom)
Write-Host "Validated $($payloadFiles.Count) approved payload files ($($manifest.Qt), x64 Release)."
Write-Host "Payload manifest: $payloadManifestPath"
if ($ValidateOnly) { return }

if ([string]::IsNullOrWhiteSpace($InnoCompiler)) {
    $knownCompilers = @('D:\Software\innosetup\content\ISCC.exe')
    if (${env:ProgramFiles(x86)}) { $knownCompilers += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    if ($env:ProgramFiles) { $knownCompilers += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe' }
    $InnoCompiler = $knownCompilers | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $InnoCompiler) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $InnoCompiler = $command.Source }
    }
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup compiler not found. Supply -InnoCompiler with the existing ISCC.exe path.'
}
$compiler = (Resolve-Path -LiteralPath $InnoCompiler).ProviderPath
if ($isWindows7) {
    # ISCC's PE version is 0.0.0.0; its own help banner identifies the major version.
    $compilerHelp = (& $compiler '/?' 2>&1 | Out-String)
    if ($compilerHelp -notmatch '(?m)^Inno Setup ([0-9]+) Command-Line Compiler\s*$' -or $Matches[1] -ne '6') {
        throw 'Windows7SP1 packaging requires an Inno Setup 6 compiler.'
    }
    foreach ($field in @('D3DCompilerSource', 'D3DCompilerSha256')) {
        if (-not ($manifest.PSObject.Properties.Name -contains $field)) { throw "Win7 build-manifest.json is missing $field." }
    }
    if (-not (Test-Path -LiteralPath $manifest.D3DCompilerSource -PathType Leaf)) {
        throw 'D3DCompilerSource must identify the original Qt 5 downlevel d3dcompiler_47.dll.'
    }
    if ((Get-FileHash -LiteralPath $manifest.D3DCompilerSource -Algorithm SHA256).Hash -ne $manifest.D3DCompilerSha256 -or
        (Get-FileHash -LiteralPath (Join-Path $distRoot 'd3dcompiler_47.dll') -Algorithm SHA256).Hash -ne $manifest.D3DCompilerSha256) {
        throw 'D3DCompilerSha256 does not match the deployment and original Qt 5 source.'
    }
    $auditScript = Join-Path $projectRoot 'scripts\Test-Win7Compatibility.ps1'
    $auditReport = Join-Path $packagingRoot 'win7-static-audit.json'
    $powerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
    & $powerShell -NoLogo -NoProfile -ExecutionPolicy Bypass -File $auditScript `
        -DistDirectory $distRoot -ReportPath $auditReport `
        -UcrtReferenceDirectory $manifest.UcrtSourceDirectory -D3DCompilerReference $manifest.D3DCompilerSource
    if ($LASTEXITCODE -ne 0) { throw "Windows 7 static compatibility audit failed. See $auditReport" }
}
$installerPath = Join-Path $outputRoot "$outputBaseName.exe"
$arguments = @(
    '/Qp', "/DProjectRoot=$projectRoot", "/DPayloadFileList=$fileListPath",
    "/DQtMajor=$QtMajor", "/DTargetWindows=$TargetWindows", "/DInstallerOutputDirectory=$outputRoot",
    "/DInstallerOutputBaseName=$outputBaseName", (Join-Path $projectRoot 'installer\SurfacePathStudio.iss')
)
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) { throw "Expected installer was not produced: $installerPath" }
$installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
[System.IO.File]::WriteAllText("$installerPath.sha256", "$installerHash  $([System.IO.Path]::GetFileName($installerPath))`r`n", $utf8Bom)
Write-Host "Installer: $installerPath"
Write-Host "SHA256: $installerHash"
