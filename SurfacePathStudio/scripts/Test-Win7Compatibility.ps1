<#
.SYNOPSIS
Checks a Windows 7 SP1 x64 distribution without executing its binaries.
.DESCRIPTION
Build-machine tooling. Requires PowerShell 5.1 or newer and .NET; the product does
not require PowerShell, C#, or Python. This is a static gate, not a Win7 runtime
certification. Imports are checked against a curated list of later Windows APIs,
and module dependencies against deployed files and an explicit system DLL list.
Microsoft SDK downlevel redistributables retain newer PE version fields. They
receive an explicit metadata exception only after matching their reference files
byte for byte. No PE headers or system settings are modified.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DistDirectory,
    [string]$ReportPath,
    [string]$UcrtReferenceDirectory = "${env:ProgramFiles(x86)}\Windows Kits\10\Redist\10.0.19041.0\ucrt\DLLs\x64",
    [string]$D3DCompilerReference
)
$ErrorActionPreference = 'Stop'
$dist = (Resolve-Path -LiteralPath $DistDirectory).Path.TrimEnd('\', '/')
if (!$ReportPath) { $ReportPath = Join-Path $dist 'win7-static-audit.json' }

if (!('Win7PeAudit.Image' -as [type])) {
    # Legacy host LIB entries can make the .NET compiler reject an otherwise
    # self-contained parser. Filter only this process, then restore immediately.
    $savedLib = $env:LIB
    try {
    $env:LIB = (@($env:LIB -split ';' | Where-Object { $_ -and (Test-Path -LiteralPath $_) }) -join ';')
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

namespace Win7PeAudit {
    public class Dependency {
        public string Module;
        public string Symbol;
        public string Kind;
        public Dependency(string module, string symbol, string kind) {
            Module = module; Symbol = symbol; Kind = kind;
        }
    }
    public class Image {
        public ushort Machine;
        public Version OSVersion;
        public Version SubsystemVersion;
        public ushort Subsystem;
        public List<Dependency> Dependencies = new List<Dependency>();
        public Dictionary<string, string> Exports = new Dictionary<string, string>(StringComparer.Ordinal);
        private byte[] data;
        private List<uint[]> sections = new List<uint[]>();
        private uint headers;
        private bool plus;
        private ulong imageBase;
        private ushort U16(int p) { return BitConverter.ToUInt16(data, p); }
        private uint U32(int p) { return BitConverter.ToUInt32(data, p); }
        private int Offset(uint rva) {
            foreach (uint[] s in sections)
                if (rva >= s[0] && (ulong)rva < (ulong)s[0] + s[1])
                    return checked((int)(s[2] + rva - s[0]));
            if (rva < headers) return checked((int)rva);
            throw new InvalidDataException("Unmapped RVA: " + rva.ToString("x"));
        }
        private string Str(uint rva) {
            int p = Offset(rva), end = p;
            while (end < data.Length && data[end] != 0) end++;
            if (end == data.Length) throw new InvalidDataException("Unterminated PE string");
            return Encoding.ASCII.GetString(data, p, end - p);
        }
        private void Thunks(string module, uint rva, string kind) {
            if (rva == 0) return;
            int p = Offset(rva), width = plus ? 8 : 4;
            ulong mask = plus ? 0x8000000000000000UL : 0x80000000UL;
            while (true) {
                ulong value = plus ? BitConverter.ToUInt64(data, p) : U32(p);
                if (value == 0) break;
                string symbol = (value & mask) != 0 ? "#" + (value & 0xffff) : Str(checked((uint)value + 2));
                Dependencies.Add(new Dependency(module, symbol, kind));
                p += width;
            }
        }
        private uint DelayRva(uint value, uint attributes) {
            return (attributes & 1) != 0 ? value : checked((uint)((ulong)value - imageBase));
        }
        public static Image Read(string path) {
            Image result = new Image();
            result.data = File.ReadAllBytes(path);
            byte[] d = result.data;
            if (d.Length < 64 || result.U16(0) != 0x5a4d) throw new InvalidDataException("Not an MZ image");
            int pe = checked((int)result.U32(0x3c));
            if (result.U32(pe) != 0x4550) throw new InvalidDataException("Not a PE image");
            int opt = pe + 24;
            ushort magic = result.U16(opt);
            if (magic != 0x10b && magic != 0x20b) throw new InvalidDataException("Unsupported PE optional header");
            result.plus = magic == 0x20b;
            result.Machine = result.U16(pe + 4);
            result.headers = result.U32(opt + 60);
            result.imageBase = result.plus ? BitConverter.ToUInt64(d, opt + 24) : result.U32(opt + 28);
            result.OSVersion = new Version(result.U16(opt + 40), result.U16(opt + 42));
            result.SubsystemVersion = new Version(result.U16(opt + 48), result.U16(opt + 50));
            result.Subsystem = result.U16(opt + 68);
            int sectionStart = opt + result.U16(pe + 20);
            for (int i = 0; i < result.U16(pe + 6); i++) {
                int p = sectionStart + i * 40;
                result.sections.Add(new uint[] { result.U32(p + 12), Math.Max(result.U32(p + 8), result.U32(p + 16)), result.U32(p + 20) });
            }
            int directory = opt + (result.plus ? 112 : 96);
            uint count = result.U32(opt + (result.plus ? 108 : 92));
            uint importRva = count > 1 ? result.U32(directory + 8) : 0;
            if (importRva != 0) {
                int p = result.Offset(importRva);
                while (result.U32(p) != 0 || result.U32(p + 12) != 0 || result.U32(p + 16) != 0) {
                    uint lookup = result.U32(p);
                    result.Thunks(result.Str(result.U32(p + 12)), lookup == 0 ? result.U32(p + 16) : lookup, "direct");
                    p += 20;
                }
            }
            uint delayRva = count > 13 ? result.U32(directory + 13 * 8) : 0;
            if (delayRva != 0) {
                int p = result.Offset(delayRva);
                while (result.U32(p + 4) != 0) {
                    uint attr = result.U32(p);
                    result.Thunks(result.Str(result.DelayRva(result.U32(p + 4), attr)), result.DelayRva(result.U32(p + 16), attr), "delay");
                    p += 32;
                }
            }
            uint exportRva = count > 0 ? result.U32(directory) : 0;
            uint exportSize = count > 0 ? result.U32(directory + 4) : 0;
            if (exportRva != 0) {
                int p = result.Offset(exportRva);
                uint ordinalBase = result.U32(p + 16), functionCount = result.U32(p + 20), nameCount = result.U32(p + 24);
                int functions = result.Offset(result.U32(p + 28));
                for (uint i = 0; i < functionCount; i++) {
                    uint rva = result.U32(functions + checked((int)i * 4));
                    if (rva == 0) continue;
                    string forward = rva >= exportRva && (ulong)rva < (ulong)exportRva + exportSize ? result.Str(rva) : null;
                    result.Exports.Add("#" + (ordinalBase + i), forward);
                    if (forward != null) {
                        int dot = forward.LastIndexOf('.');
                        if (dot < 1) throw new InvalidDataException("Invalid export forwarder");
                        string module = forward.Substring(0, dot);
                        if (!module.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)) module += ".dll";
                        result.Dependencies.Add(new Dependency(module, forward.Substring(dot + 1), "forwarder"));
                    }
                }
                if (nameCount > 0) {
                    int names = result.Offset(result.U32(p + 32)), ordinals = result.Offset(result.U32(p + 36));
                    for (uint i = 0; i < nameCount; i++) {
                        string name = result.Str(result.U32(names + checked((int)i * 4)));
                        string ordinal = "#" + (ordinalBase + result.U16(ordinals + checked((int)i * 2)));
                        if (result.Exports.ContainsKey(ordinal)) result.Exports[name] = result.Exports[ordinal];
                    }
                }
            }
            return result;
        }
    }
}
'@
    } finally { $env:LIB = $savedLib }
}

$failures = New-Object 'System.Collections.Generic.List[object]'
$warnings = New-Object 'System.Collections.Generic.List[object]'
$metadataExceptions = New-Object 'System.Collections.Generic.List[object]'
$inventory = New-Object 'System.Collections.Generic.List[object]'
$peByName = @{}
$pathByName = @{}
$hashByName = @{}
$maxVersion = [version]'6.1'

# Names present in Windows 7 SP1. CRT DLLs and arbitrary API sets are deliberately
# excluded: those must be application-local rather than inherited from this host.
$systemDlls = @{}
@('advapi32.dll','avrt.dll','bcrypt.dll','cabinet.dll','cfgmgr32.dll','comctl32.dll',
  'comdlg32.dll','crypt32.dll','cryptbase.dll','d3d9.dll','d3d10.dll','d3d10_1.dll',
  'd3d11.dll','dbghelp.dll','dhcpcsvc.dll','dnsapi.dll','dwmapi.dll','dxgi.dll',
  'gdi32.dll','hid.dll','imagehlp.dll','imm32.dll','iphlpapi.dll','kernel32.dll',
  'kernelbase.dll','mpr.dll','msimg32.dll','msvcrt.dll','mswsock.dll','netapi32.dll',
  'normaliz.dll','ntdll.dll','ole32.dll','oleacc.dll','oleaut32.dll','opengl32.dll',
  'powrprof.dll','propsys.dll','psapi.dll','rasapi32.dll','rpcrt4.dll','secur32.dll',
  'setupapi.dll','shell32.dll','shlwapi.dll','user32.dll','userenv.dll','usp10.dll',
  'uxtheme.dll','version.dll','winhttp.dll','wininet.dll','winmm.dll','winspool.drv',
  'wintrust.dll','wldap32.dll','ws2_32.dll','wtsapi32.dll') | ForEach-Object { $systemDlls[$_] = $true }

# Known-late API scan, NOT an exhaustive Windows 7 export baseline. These APIs
# first appeared after Windows 7. Exact symbol matching avoids C++ name matches.
$laterApis = @{}
@('AddDllDirectory','RemoveDllDirectory','SetDefaultDllDirectories',
  'GetSystemTimePreciseAsFileTime','GetTempPath2A','GetTempPath2W',
  'WaitOnAddress','WakeByAddressSingle','WakeByAddressAll','DiscardVirtualMemory',
  'OfferVirtualMemory','ReclaimVirtualMemory','GetCurrentThreadStackLimits',
  'SetThreadDescription','GetThreadDescription','GetThreadInformation',
  'SetThreadInformation','GetProcessInformation','SetProcessInformation',
  'GetOverlappedResultEx','CreateFile2','CreateFile2FromAppW','CopyFile2',
  'PrefetchVirtualMemory','IsWow64Process2','SetThreadSelectedCpuSets',
  'GetSystemCpuSetInformation','GetDpiForWindow','GetDpiForSystem',
  'GetSystemMetricsForDpi','AdjustWindowRectExForDpi','SystemParametersInfoForDpi',
  'SetThreadDpiAwarenessContext','SetProcessDpiAwarenessContext',
  'GetThreadDpiAwarenessContext','GetWindowDpiAwarenessContext',
  'GetAwarenessFromDpiAwarenessContext','AreDpiAwarenessContextsEqual',
  'SetProcessDpiAwareness','GetProcessDpiAwareness','GetDpiForMonitor',
  'EnableNonClientDpiScaling','GetPointerInfo','GetPointerType',
  'GetPointerPenInfo','GetPointerTouchInfo','GetPointerFrameInfo',
  'RegisterPointerInputTarget','CreateDXGIFactory2','D3D12CreateDevice',
  'RoInitialize','RoUninitialize','RoGetActivationFactory','RoActivateInstance',
  'WindowsCreateString','WindowsDeleteString','WindowsGetStringRawBuffer') | ForEach-Object { $laterApis[$_] = $true }

$referenceHashes = @{}
$referenceVersions = @{}
if (Test-Path -LiteralPath $UcrtReferenceDirectory -PathType Container) {
    Get-ChildItem -LiteralPath $UcrtReferenceDirectory -Filter '*.dll' -File | ForEach-Object {
        $key = $_.Name.ToLowerInvariant()
        $referenceHashes[$key] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        $referenceVersions[$key] = $_.VersionInfo.FileVersion
    }
} else {
    $failures.Add([pscustomobject]@{code='MissingUcrtReference';file=$UcrtReferenceDirectory;message='SDK UCRT reference is required to verify exact downlevel redistributables.'})
}

$requiredUcrt = @('ucrtbase.dll','api-ms-win-crt-conio-l1-1-0.dll','api-ms-win-crt-convert-l1-1-0.dll',
 'api-ms-win-crt-environment-l1-1-0.dll','api-ms-win-crt-filesystem-l1-1-0.dll','api-ms-win-crt-heap-l1-1-0.dll',
 'api-ms-win-crt-locale-l1-1-0.dll','api-ms-win-crt-math-l1-1-0.dll','api-ms-win-crt-multibyte-l1-1-0.dll',
 'api-ms-win-crt-private-l1-1-0.dll','api-ms-win-crt-process-l1-1-0.dll','api-ms-win-crt-runtime-l1-1-0.dll',
 'api-ms-win-crt-stdio-l1-1-0.dll','api-ms-win-crt-string-l1-1-0.dll','api-ms-win-crt-time-l1-1-0.dll','api-ms-win-crt-utility-l1-1-0.dll')
$requiredUcrt = @($requiredUcrt + @($referenceHashes.Keys) | Sort-Object -Unique)
foreach ($name in $requiredUcrt) {
    $path = Join-Path $dist $name
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        $failures.Add([pscustomobject]@{code='MissingUcrt';file=$name;message='Complete application-local UCRT must be beside the executable.'})
    } elseif ($referenceHashes.ContainsKey($name) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $referenceHashes[$name]) {
        $failures.Add([pscustomobject]@{code='UcrtReferenceMismatch';file=$name;message='UCRT file does not match the selected SDK set.'})
    }
}

$d3dHash = if ($D3DCompilerReference -and (Test-Path -LiteralPath $D3DCompilerReference -PathType Leaf)) { (Get-FileHash -LiteralPath $D3DCompilerReference -Algorithm SHA256).Hash } else { $null }
$files = @(Get-ChildItem -LiteralPath $dist -File -Recurse | Where-Object { $_.Extension -in '.dll','.exe' })
if ($files.Count -eq 0) { $failures.Add([pscustomobject]@{code='NoBinaries';file=$dist;message='Distribution contains no EXE or DLL.'}) }
foreach ($file in $files) {
    $relative = $file.FullName.Substring($dist.Length + 1)
    try { $pe = [Win7PeAudit.Image]::Read($file.FullName) } catch {
        $failures.Add([pscustomobject]@{code='InvalidPe';file=$relative;message=$_.Exception.Message})
        continue
    }
    $key = $file.Name.ToLowerInvariant()
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    if ($hashByName.ContainsKey($key) -and $hashByName[$key] -ne $hash) {
        $failures.Add([pscustomobject]@{code='AmbiguousModule';file=$relative;message='Different deployed files share the same DLL name.'})
    }
    $peByName[$key] = $pe
    $pathByName[$key] = $relative
    $hashByName[$key] = $hash
    if ($pe.Machine -ne 0x8664) { $failures.Add([pscustomobject]@{code='WrongArchitecture';file=$relative;message=('Expected x64; machine=0x{0:x4}' -f $pe.Machine)}) }
    $exception = $null
    if ($referenceHashes.ContainsKey($key) -and $referenceHashes[$key] -eq $hash) {
        $exception = 'Exact Microsoft Windows SDK application-local UCRT redistributable; PE version fields retained.'
    } elseif ($key -eq 'd3dcompiler_47.dll' -and $d3dHash -and $d3dHash -eq $hash -and $pe.SubsystemVersion -le $maxVersion) {
        $exception = 'Exact supplied downlevel D3DCompiler reference; subsystem <= 6.1; PE OS field retained.'
    }
    if ($pe.OSVersion -gt $maxVersion -or $pe.SubsystemVersion -gt $maxVersion) {
        if ($exception) { $metadataExceptions.Add([pscustomobject]@{file=$relative;osVersion=$pe.OSVersion.ToString();subsystemVersion=$pe.SubsystemVersion.ToString();reason=$exception}) }
        else { $failures.Add([pscustomobject]@{code='NewerPeVersion';file=$relative;message=('OS={0}; subsystem={1}; maximum 6.1 without a verified downlevel redistributable exception.' -f $pe.OSVersion,$pe.SubsystemVersion)}) }
    }
    $inventory.Add([pscustomobject]@{
        file=$relative;sha256=$hash;fileVersion=$file.VersionInfo.FileVersion;machine=('0x{0:x4}' -f $pe.Machine)
        osVersion=$pe.OSVersion.ToString();subsystemVersion=$pe.SubsystemVersion.ToString()
        importCount=@($pe.Dependencies | Where-Object {$_.Kind -eq 'direct'}).Count
        delayImportCount=@($pe.Dependencies | Where-Object {$_.Kind -eq 'delay'}).Count
        forwarderCount=@($pe.Dependencies | Where-Object {$_.Kind -eq 'forwarder'}).Count
        modules=@($pe.Dependencies | ForEach-Object {[pscustomobject]@{module=$_.Module;kind=$_.Kind}} | Sort-Object module,kind -Unique)
    })
}

$seenProblems = @{}
$resolvedForwarders = New-Object 'System.Collections.Generic.List[object]'
foreach ($name in $peByName.Keys) {
    $queue = New-Object 'System.Collections.Generic.Queue[object]'
    # An exported forwarder is resolved only when that export is requested.
    # SDK shims intentionally also export newer optional APIs; merely exporting
    # them does not make those APIs load-time requirements on Windows 7.
    foreach ($dependency in $peByName[$name].Dependencies | Where-Object { $_.Kind -ne 'forwarder' }) {
        $queue.Enqueue([pscustomobject]@{Module=$dependency.Module;Symbol=$dependency.Symbol;Kind=$dependency.Kind;IsDelay=($dependency.Kind -eq 'delay');Chain=@()})
    }
    while ($queue.Count -gt 0) {
        $dependency = $queue.Dequeue()
        $module = $dependency.Module.ToLowerInvariant()
        $symbol = $dependency.Symbol
        $isDelay = $dependency.IsDelay
        $problem = $null
        if ($peByName.ContainsKey($module)) {
            if (!$peByName[$module].Exports.ContainsKey($symbol)) { $problem = 'MissingLocalExport' }
            else {
                $forward = $peByName[$module].Exports[$symbol]
                if ($forward) {
                    $link = "$module!$symbol"
                    if ($dependency.Chain -contains $link) { $problem = 'ExportForwarderCycle' }
                    else {
                        $dot = $forward.LastIndexOf('.')
                        $nextModule = $forward.Substring(0, $dot)
                        if (!$nextModule.EndsWith('.dll', [StringComparison]::OrdinalIgnoreCase)) { $nextModule += '.dll' }
                        $nextSymbol = $forward.Substring($dot + 1)
                        $resolvedForwarders.Add([pscustomobject]@{file=$pathByName[$name];via=$link;target=$forward;delay=$isDelay})
                        $queue.Enqueue([pscustomobject]@{Module=$nextModule;Symbol=$nextSymbol;Kind='forwarder';IsDelay=$isDelay;Chain=@($dependency.Chain) + $link})
                    }
                }
            }
        } elseif (!$systemDlls.ContainsKey($module)) { $problem = 'UnresolvedDependency' }
        if ($problem) {
            $problemKey = "$name|$problem|$module|$($dependency.Kind)"
            if (!$seenProblems.ContainsKey($problemKey)) {
                $seenProblems[$problemKey] = $true
                $item = [pscustomobject]@{code=$problem;file=$pathByName[$name];dependency=$module;symbol=$symbol;kind=$dependency.Kind;message='Dependency is neither resolved locally nor covered by the Windows 7 SP1 system DLL list (local export mismatches are also rejected).'}
                if ($isDelay) {$warnings.Add($item)} else {$failures.Add($item)}
            }
        }
        if ($systemDlls.ContainsKey($module) -and $laterApis.ContainsKey($symbol)) {
            $item = [pscustomobject]@{code='KnownLaterWindowsApi';file=$pathByName[$name];dependency=$module;symbol=$symbol;kind=$dependency.Kind;message='Known API introduced after Windows 7; no mandatory dependency is allowed.'}
            if ($isDelay) {$warnings.Add($item)} else {$failures.Add($item)}
        }
    }
}

$report = [ordered]@{
    schemaVersion=1;target='Windows 7 SP1 x64';distDirectory=$dist
    passed=($failures.Count -eq 0);verification='static-known-api-scan-and-dependency-closure'
    runtimeVerified=$false
    limitations=@('Known-late API scan is not a complete Windows 7 system export baseline.',
      'GetProcAddress/LoadLibrary dynamic resolution and feature fallbacks require runtime testing.',
      'No Win7 SP1 execution, graphics-driver validation, or physical-machine certification is implied.')
    ucrtReferenceDirectory=$UcrtReferenceDirectory;d3dCompilerReference=$D3DCompilerReference
    ucrtRequiredFileCount=$requiredUcrt.Count;peFileCount=$files.Count
    knownLaterApiCount=$laterApis.Count;systemDllAllowlist=@($systemDlls.Keys | Sort-Object)
    metadataExceptions=$metadataExceptions.ToArray();failures=$failures.ToArray();warnings=$warnings.ToArray();files=$inventory.ToArray()
    resolvedImportForwarders=$resolvedForwarders.ToArray()
    documentation=@('https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment',
      'https://learn.microsoft.com/en-us/windows/win32/directx-sdk--august-2009-')
}
$parent = Split-Path -Parent ([IO.Path]::GetFullPath($ReportPath))
if (!(Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Host ('Win7 static gate: {0}; PE files={1}; failures={2}; warnings={3}; metadata exceptions={4}' -f $report.passed,$files.Count,$failures.Count,$warnings.Count,$metadataExceptions.Count)
Write-Host "Report: $ReportPath"
if ($failures.Count -gt 0) {
    $failures | Select-Object -First 15 | ForEach-Object { Write-Host ('  {0}: {1} {2} {3}' -f $_.code,$_.file,$_.dependency,$_.symbol) }
    exit 1
}
exit 0
