[CmdletBinding()]
param(
    [ValidateSet(5, 6)]
    [int]$QtMajor = 5,
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
if ([string]::IsNullOrWhiteSpace($DistDirectory)) {
    $DistDirectory = Join-Path $projectRoot "dist-branding-qt$QtMajor"
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
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'installer\软件介绍.txt') -PathType Leaf)) {
    throw 'Missing installer\软件介绍.txt.'
}

$outputBaseName = "$applicationName-$applicationVersion-Qt$QtMajor-x64-Setup"
$packagingRoot = Join-Path $outputRoot "_packaging\Qt$QtMajor"
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
    @{ Source=(Join-Path $projectRoot 'installer\软件介绍.txt'); Destination='软件介绍.txt' }
)) {
    $payloadFiles += [ordered]@{ Path=$extra.Destination; Bytes=(Get-Item -LiteralPath $extra.Source).Length; Sha256=(Get-FileHash -LiteralPath $extra.Source -Algorithm SHA256).Hash }
}
$payloadManifestPath = Join-Path $outputRoot "$outputBaseName-payload.json"
$payloadManifest = [ordered]@{
    Application=$applicationName; ApplicationVersion=$applicationVersion; Qt=$manifest.Qt
    Architecture='x64'; Configuration='Release'; CreatedUtc=[DateTime]::UtcNow.ToString('o')
    FileCount=$payloadFiles.Count; Files=$payloadFiles
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
$installerPath = Join-Path $outputRoot "$outputBaseName.exe"
$arguments = @(
    '/Qp', "/DProjectRoot=$projectRoot", "/DPayloadFileList=$fileListPath",
    "/DQtMajor=$QtMajor", "/DInstallerOutputDirectory=$outputRoot",
    "/DInstallerOutputBaseName=$outputBaseName", (Join-Path $projectRoot 'installer\SurfacePathStudio.iss')
)
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) { throw "Expected installer was not produced: $installerPath" }
$installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
[System.IO.File]::WriteAllText("$installerPath.sha256", "$installerHash  $([System.IO.Path]::GetFileName($installerPath))`r`n", $utf8Bom)
Write-Host "Installer: $installerPath"
Write-Host "SHA256: $installerHash"
