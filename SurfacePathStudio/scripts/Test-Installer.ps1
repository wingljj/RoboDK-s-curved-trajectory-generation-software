[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Installer,
    [ValidateSet(5,6)][int]$QtMajor = 5,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$productName = '上海卫星装备研究所曲面轨迹生成软件'
$publisher = '上海卫星装备研究所总装中心'
$buildRoot = $projectRoot
$buildAlias = 'E:\surfacepathstudio-20261003'
if (Test-Path -LiteralPath $buildAlias) {
    $junction = Get-Item -LiteralPath $buildAlias -Force
    if ($junction.LinkType -eq 'Junction' -and [IO.Path]::GetFullPath([string]@($junction.Target)[0]) -eq $projectRoot) { $buildRoot = $buildAlias }
}
$registryPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{DF364F38-EF55-4A1C-84A2-8990C00E7AD4}_is1'
if (Test-Path -LiteralPath $registryPath) {
    throw 'An existing user installation is registered. This test will not replace its uninstall record.'
}
$installerFile = (Resolve-Path -LiteralPath $Installer).ProviderPath
$payloadFile = Join-Path (Split-Path $installerFile -Parent) ([IO.Path]::GetFileNameWithoutExtension($installerFile) + '-payload.json')
if (-not (Test-Path -LiteralPath $payloadFile)) { throw 'Installer payload manifest is missing.' }
$payload = Get-Content -LiteralPath $payloadFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($payload.ApplicationVersion -ne '0.1.2' -or [string]$payload.Qt -notmatch "^$QtMajor\.") { throw 'Unexpected installer variant.' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $buildRoot ('build\installer-tests\Qt' + $QtMajor + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$workspacePrefix = $projectRoot.TrimEnd('\') + '\'
# The checked build junction is also an allowed project path.
$aliasPrefix = $buildRoot.TrimEnd('\') + '\'
if (-not $outputRoot.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase) -and
    -not $outputRoot.StartsWith($aliasPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Test output must stay inside the project.' }
$physicalOutputRoot = $outputRoot
if ($buildRoot -ne $projectRoot -and $outputRoot.StartsWith($aliasPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    $physicalOutputRoot = Join-Path $projectRoot $outputRoot.Substring($aliasPrefix.Length)
}
# Inno/Windows RedirectionGuard rejects install targets traversing a junction.
# Keep the OCCT smoke output on the ASCII alias, but install on the real path.
$installRoot = Join-Path $physicalOutputRoot 'installed'
if (Test-Path -LiteralPath $installRoot) { throw 'Test install directory already exists; select a fresh output directory.' }
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$groupName = 'SurfacePathStudio QA Qt' + $QtMajor + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$groupPath = Join-Path ([Environment]::GetFolderPath('Programs')) $groupName
if (Test-Path -LiteralPath $groupPath) { throw 'Test shortcut group already exists.' }

function Invoke-CheckedProcess([string]$File, [string[]]$Arguments) {
    $child = Start-Process -FilePath $File -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $child.WaitForExit(60000)) { throw "Test process has not finished: PID $($child.Id)." }
    if ($child.ExitCode -ne 0) { throw "Test process failed, exit $($child.ExitCode): $File" }
}

$report = [ordered]@{
    ProductName=$productName; Version='0.1.2'; Qt=$payload.Qt
    Installer=$installerFile; InstallerSha256=(Get-FileHash -LiteralPath $installerFile -Algorithm SHA256).Hash
    TestInstallDirectory=$installRoot; Passed=$false
}
try {
    Invoke-CheckedProcess $installerFile @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',
        ('/DIR="' + $installRoot + '"'), ('/GROUP="' + $groupName + '"'),
        '/MERGETASKS="!desktopicon"', ('/LOG="' + (Join-Path $outputRoot 'install.log') + '"'))
    $executable = Join-Path $installRoot 'SurfacePathStudio.exe'
    $uninstaller = Join-Path $installRoot 'unins000.exe'
    if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $uninstaller)) { throw 'Installation did not produce application and uninstaller.' }
    $verifiedFiles = 0
    foreach ($file in $payload.Files) {
        $installedFile = [IO.Path]::GetFullPath((Join-Path $installRoot $file.Path))
        if (-not $installedFile.StartsWith($installRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Payload path is outside the test installation.' }
        if (-not (Test-Path -LiteralPath $installedFile -PathType Leaf)) { throw "Missing installed file: $($file.Path)" }
        if ((Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash -ne $file.Sha256) { throw "Installed file hash mismatch: $($file.Path)" }
        $verifiedFiles++
    }
    $report['PayloadVerifiedFiles'] = $verifiedFiles
    $versionInfo = (Get-Item -LiteralPath $executable).VersionInfo
    if ($versionInfo.ProductName -ne $productName -or $versionInfo.CompanyName -ne $publisher -or $versionInfo.FileVersion -ne '0.1.2.0') { throw 'Installed executable metadata lost its branding.' }
    $report['ExecutableBranding'] = $true
    $introduction = Get-Content -LiteralPath (Join-Path $installRoot '软件介绍.txt') -Raw -Encoding UTF8
    foreach ($text in @($productName,'中国航天','上海卫星装备研究所','总装中心','STEP','CSV')) {
        if (-not $introduction.Contains($text)) { throw "Introduction is missing: $text" }
    }
    $report['IntroductionBranding'] = $true
    $registration = Get-ItemProperty -LiteralPath $registryPath
    if ($registration.DisplayName -ne $productName -or $registration.DisplayVersion -ne '0.1.2' -or $registration.Publisher -ne $publisher -or
        [IO.Path]::GetFullPath($registration.InstallLocation).TrimEnd('\') -ne $installRoot.TrimEnd('\')) { throw 'Installed program registration is unexpected.' }
    $report['UninstallRegistration'] = $true
    $shortcut = Join-Path $groupPath ($productName + '.lnk')
    if (-not (Test-Path -LiteralPath $shortcut)) { throw 'Branded Start Menu shortcut is missing.' }
    $shell = New-Object -ComObject WScript.Shell
    try {
        $link = $shell.CreateShortcut($shortcut)
        if ($link.TargetPath -ne $executable -or $link.Description -notlike '*总装中心*') { throw 'Shortcut target or institution description is incorrect.' }
    } finally { [Runtime.InteropServices.Marshal]::ReleaseComObject($shell) | Out-Null }
    $report['StartMenuShortcut'] = $true
    & (Join-Path $PSScriptRoot 'SmokeTest.ps1') -Executable $executable -OutputDirectory (Join-Path $outputRoot 'desktop-smoke') -IsolatedDistribution
    $report['InstalledDesktopSmoke'] = $true
    # A new user export must survive uninstall; no existing user document is involved.
    $userExport = Join-Path $installRoot 'validation-user-export.csv'
    $sampleExport = "X,Y,Z`r`n1,2,3`r`n"
    [IO.File]::WriteAllText($userExport, $sampleExport, [Text.UTF8Encoding]::new($false))
    $registration = Get-ItemProperty -LiteralPath $registryPath
    if ([IO.Path]::GetFullPath($registration.InstallLocation).TrimEnd('\') -ne $installRoot.TrimEnd('\')) { throw 'Registration no longer belongs to this test; uninstall cancelled.' }
    Invoke-CheckedProcess $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="' + (Join-Path $outputRoot 'uninstall.log') + '"'))
    if (Test-Path -LiteralPath $executable) { throw 'Uninstall left the application executable.' }
    if (Test-Path -LiteralPath $registryPath) { throw 'Uninstall left its registration.' }
    if (Test-Path -LiteralPath $shortcut) { throw 'Uninstall left its Start Menu shortcut.' }
    if (-not (Test-Path -LiteralPath $userExport) -or [IO.File]::ReadAllText($userExport) -ne $sampleExport) { throw 'Uninstall removed or changed the user export.' }
    $report['UninstallCleanup'] = $true
    $report['UserExportPreserved'] = $true
    $report['Passed'] = $true
} catch {
    $report['Error'] = $_.Exception.Message
    throw
} finally {
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'installer-test.json') -Encoding UTF8
}
$report | ConvertTo-Json -Depth 5
