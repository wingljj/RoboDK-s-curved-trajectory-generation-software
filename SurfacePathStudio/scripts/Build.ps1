[CmdletBinding()]
param(
    [string]$QtRoot = 'D:\Software\QT\content\6.5.3\msvc2019_64',
    [string]$VcVars64 = 'D:\Software\visual2019\content\VC\Auxiliary\Build\vcvars64.bat',
    [string]$OcctInclude = 'E:\surfacepath-poc-20261002\.deps\occt-static-build\inc',
    [string]$OcctLib = 'E:\surfacepath-poc-20261002\.deps\occt-static-build\win64\vc14\lib',
    [string]$OcctSource = 'E:\surfacepath-poc-20261002\.deps\OCCT-7_6_0',
    [string]$BuildName = 'native-release',
    [string]$DeployDirectory = '',
    [string]$Target = '',
    [int]$Jobs = 8,
    [string]$UcrtRoot = '',
    [switch]$Windows7,
    [switch]$Clean,
    [switch]$RunTests,
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
& chcp 65001 | Out-Null
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Windows7) {
    if (-not $PSBoundParameters.ContainsKey('QtRoot')) { $QtRoot = 'D:\Software\QT\content\5.15.0\msvc2019_64' }
    if (-not $PSBoundParameters.ContainsKey('BuildName')) { $BuildName = 'win7-x64' }
    if (-not $DeployDirectory) { $DeployDirectory = Join-Path $projectRoot 'dist-win7-x64' }
}
foreach ($required in @($VcVars64, (Join-Path $QtRoot 'bin\qmake.exe'), (Join-Path $OcctInclude 'Standard_Version.hxx'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing configured dependency: $required" }
}
$qtVersion = (& (Join-Path $QtRoot 'bin\qmake.exe') -query QT_VERSION).Trim()
if ($Windows7 -and $qtVersion -notmatch '^5\.15\.') { throw 'Windows 7 compatibility builds require Qt 5.15; Qt 6 cannot target Windows 7.' }
# Capture a child compiler environment; never alter the user's saved environment.
$compilerEnvironment = @(& $env:ComSpec /d /s /c ('call "' + $VcVars64 + '" >nul && set'))
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the x64 MSVC environment' }
foreach ($line in $compilerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
if ($Windows7 -and $env:VCToolsVersion -notmatch '^14\.2\d\.') { throw 'Windows 7 compatibility builds require the MSVC 2019 v142 toolset.' }
$ucrtDirectory = ''
$ucrtFiles = @()
if ($Windows7) {
    if ($UcrtRoot) { $ucrtDirectory = (Resolve-Path -LiteralPath $UcrtRoot).ProviderPath }
    else { $ucrtDirectory = Join-Path $env:UniversalCRTSdkDir ('Redist\' + $env:UCRTVersion + '\ucrt\DLLs\x64') }
    if (-not (Test-Path -LiteralPath (Join-Path $ucrtDirectory 'ucrtbase.dll') -PathType Leaf)) { throw 'Windows 7 deployment requires the official Windows SDK app-local UCRT x64 redistributables. Supply -UcrtRoot.' }
    $ucrtFiles = @(Get-ChildItem -LiteralPath $ucrtDirectory -File | Where-Object { $_.Name -eq 'ucrtbase.dll' -or $_.Name -like 'api-ms-win-*.dll' })
    if ($ucrtFiles.Count -lt 40) { throw 'The Windows SDK app-local UCRT directory is incomplete; include all core and CRT forwarders.' }
    $d3dCompilerSource = Join-Path $QtRoot 'bin\d3dcompiler_47.dll'
    if (-not (Test-Path -LiteralPath $d3dCompilerSource)) { throw 'The Win7 profile requires the Qt 5 SDK downlevel d3dcompiler_47.dll.' }
}
$compilerResourceRoot = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64'
if (Test-Path -LiteralPath (Join-Path $compilerResourceRoot '1033')) {
    $env:VSLANG = '1033'
    $env:SURFACEPATH_MSVC_INCLUDE_PREFIX = 'Note: including file: '
} elseif (Test-Path -LiteralPath (Join-Path $compilerResourceRoot '2052')) {
    $env:VSLANG = '2052'
    $env:SURFACEPATH_MSVC_INCLUDE_PREFIX = '注意: 包含文件: '
} else { $env:SURFACEPATH_MSVC_INCLUDE_PREFIX = $null }
$cmake = 'D:\Software\QT\content\Tools\CMake_64\bin\cmake.exe'
$ninja = 'D:\Software\QT\content\Tools\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
$env:Path = (Join-Path $QtRoot 'bin') + ';' + (Split-Path $ninja) + ';' + $env:Path
# Old moc/MSVC tools are more reliable with an ASCII path. The junction is checked,
# never replaced or deleted, and points at this exact project.
$buildSource = $projectRoot
if ($projectRoot -match '[^\x00-\x7F]') {
    $buildSource = Join-Path ([IO.Path]::GetPathRoot($projectRoot)) 'surfacepathstudio-20261003'
    if (Test-Path -LiteralPath $buildSource) {
        $junction = Get-Item -LiteralPath $buildSource -Force
        if ($junction.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath([string]@($junction.Target)[0]) -ne $projectRoot) {
            throw "Build alias already exists with a different target: $buildSource"
        }
    } else { New-Item -ItemType Junction -Path $buildSource -Target $projectRoot | Out-Null }
}
$buildDirectory = Join-Path $buildSource ('build\' + $BuildName)
& $cmake -S $buildSource -B $buildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DBUILD_TESTING=ON' `
    ('-DCMAKE_MAKE_PROGRAM=' + $ninja) ('-DCMAKE_PREFIX_PATH=' + $QtRoot) `
    ('-DSURFACEPATH_OCCT_INCLUDE_DIR=' + $OcctInclude.Replace('\','/')) `
    ('-DSURFACEPATH_OCCT_LIBRARY_DIR=' + $OcctLib.Replace('\','/')) `
    ('-DSURFACEPATH_OCCT_SOURCE_DIR=' + $OcctSource.Replace('\','/')) '-DSURFACEPATH_OCCT_STATIC=ON' `
    ('-DSURFACEPATH_WINDOWS7=' + $(if ($Windows7) { 'ON' } else { 'OFF' }))
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
$buildArguments = @('--build', $buildDirectory, '--parallel', [string]$Jobs)
if ($Clean) { $buildArguments += '--clean-first' }
if ($Target) { $buildArguments += @('--target', $Target) }
& $cmake @buildArguments
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation or linking failed' }
if ($RunTests) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }
}
if ($Deploy) {
    $distDirectory = if ($DeployDirectory) { [IO.Path]::GetFullPath($DeployDirectory) } else { Join-Path $projectRoot 'dist' }
    New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildDirectory 'SurfacePathStudio.exe') -Destination $distDirectory
    & (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --no-compiler-runtime (Join-Path $distDirectory 'SurfacePathStudio.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
    # Include the installed toolchain's x64 Release redistributable runtime.
    $runtimeDirectory = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC142.CRT'
    if (-not (Test-Path -LiteralPath $runtimeDirectory)) {
        $runtimeDirectory = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
    }
    if (Test-Path -LiteralPath $runtimeDirectory) {
        Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' -File | Where-Object { -not $Windows7 -or $_.Name -ne 'vccorlib140.dll' } | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $distDirectory
        }
    }
    if ($Windows7) {
        # windeployqt can select the host SDK's newer D3DCompiler. Use Qt 5's
        # downlevel redistributable and the complete SDK app-local UCRT instead.
        Copy-Item -LiteralPath $d3dCompilerSource -Destination $distDirectory -Force
        foreach ($ucrtFile in $ucrtFiles) { Copy-Item -LiteralPath $ucrtFile.FullName -Destination $distDirectory -Force }
        if (Test-Path -LiteralPath (Join-Path $distDirectory 'vccorlib140.dll')) { throw 'Use a clean Win7 deployment directory without unused WinRT runtime DLLs.' }
    }
    $licenseDirectory = Join-Path $distDirectory 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
    if ($Windows7) {
        $sdkLicenseRoot = Join-Path $env:UniversalCRTSdkDir ('Licenses\' + $env:UCRTVersion)
        foreach ($license in @('sdk_license.rtf','sdk_third_party_notices.rtf')) {
            $sdkLicense = Join-Path $sdkLicenseRoot $license
            if (Test-Path -LiteralPath $sdkLicense) { Copy-Item -LiteralPath $sdkLicense -Destination $licenseDirectory -Force }
        }
    }
    foreach ($license in @('LICENSE_LGPL_21.txt','OCCT_LGPL_EXCEPTION.txt')) {
        $sourceLicense = Join-Path $OcctSource $license
        if (Test-Path -LiteralPath $sourceLicense) { Copy-Item -LiteralPath $sourceLicense -Destination $licenseDirectory }
    }
    foreach ($license in @('LICENSE.LGPLv3','LICENSE.GPLv3','LICENSE.GPLv2')) {
        $sourceLicense = Join-Path $QtRoot $license
        if (Test-Path -LiteralPath $sourceLicense) { Copy-Item -LiteralPath $sourceLicense -Destination $licenseDirectory }
    }
    $qtInstallerLicense = Join-Path (Split-Path (Split-Path $QtRoot -Parent) -Parent) 'Licenses\LICENSE'
    if (Test-Path -LiteralPath $qtInstallerLicense) {
        Copy-Item -LiteralPath $qtInstallerLicense -Destination (Join-Path $licenseDirectory 'Qt-LICENSE.txt')
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $distDirectory
    Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $distDirectory
    $distDocs = Join-Path $distDirectory 'docs'
    New-Item -ItemType Directory -Force -Path $distDocs | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'docs') -Filter '*.md' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $distDocs
    }
    $validationAssets = Join-Path $projectRoot 'docs\validation-assets'
    if (Test-Path -LiteralPath $validationAssets) {
        $distAssets = Join-Path $distDocs 'validation-assets'
        New-Item -ItemType Directory -Force -Path $distAssets | Out-Null
        Get-ChildItem -LiteralPath $validationAssets -File | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $distAssets
        }
    }
    $exampleDirectory = Join-Path $projectRoot 'examples\models'
    if (Test-Path -LiteralPath $exampleDirectory) {
        $distExamples = Join-Path $distDirectory 'examples'
        New-Item -ItemType Directory -Force -Path $distExamples | Out-Null
        Copy-Item -LiteralPath $exampleDirectory -Destination $distExamples -Recurse -Force
    }
    $applicationVersion = (Select-String -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$').Matches.Groups[1].Value
    $buildManifest = [ordered]@{
        ApplicationVersion=$applicationVersion
        ApplicationName='上海卫星装备研究所曲面轨迹生成软件'
        Organization='上海卫星装备研究所'; Department='总装中心'
        CreatedUtc=[DateTime]::UtcNow.ToString('o'); Qt=$qtVersion
        Compiler=$env:VCToolsVersion; Architecture='x64'; Configuration='Release'; CppStandard=17
        OcctInclude=$OcctInclude; OcctLibrary=$OcctLib; OcctOpenGlSource=$OcctSource
        Sha256=(Get-FileHash -LiteralPath (Join-Path $distDirectory 'SurfacePathStudio.exe') -Algorithm SHA256).Hash
    }
    $buildManifest['TargetWindows'] = if ($Windows7) { 'Windows7SP1' } else { 'Windows10' }
    $buildManifest['MinimumWindowsVersion'] = if ($Windows7) { '6.1sp1' } else { '10.0' }
    $buildManifest['ApplicationLocalUcrt'] = [bool]$Windows7
    if ($Windows7) {
        $version = (Get-Item -LiteralPath (Join-Path $ucrtDirectory 'ucrtbase.dll')).VersionInfo
        $buildManifest['UcrtVersion'] = '{0}.{1}.{2}.{3}' -f $version.FileMajorPart,$version.FileMinorPart,$version.FileBuildPart,$version.FilePrivatePart
        $buildManifest['UcrtSourceDirectory'] = $ucrtDirectory
        $buildManifest['UcrtDllCount'] = $ucrtFiles.Count
        $buildManifest['ApplicationLocalUcrtFiles'] = @($ucrtFiles | ForEach-Object {
            $fileVersion = $_.VersionInfo
            [ordered]@{Path=$_.Name;Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash;Version=('{0}.{1}.{2}.{3}' -f $fileVersion.FileMajorPart,$fileVersion.FileMinorPart,$fileVersion.FileBuildPart,$fileVersion.FilePrivatePart)}
        })
        $buildManifest['QtRoot'] = $QtRoot
        $buildManifest['D3DCompilerSource'] = $d3dCompilerSource
        $buildManifest['D3DCompilerSha256'] = (Get-FileHash -LiteralPath $d3dCompilerSource -Algorithm SHA256).Hash
    }
    $buildManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $distDirectory 'build-manifest.json') -Encoding UTF8
}
Write-Host "Build output: $buildDirectory"
