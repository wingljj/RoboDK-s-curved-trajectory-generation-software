param(
    [string]$SourceImage = (Join-Path $PSScriptRoot '..\resources\casc-logo.jpg'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\resources')
)

# Generate Windows/Qt branding from the supplied official artwork. The source
# image is copied byte-for-byte; only derived images are resized or composed.
# Requires Windows PowerShell 5.1 or PowerShell 7 on Windows; no Python runtime.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path -LiteralPath $SourceImage).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null
$logoPath = Join-Path $outputPath 'casc-logo.jpg'
if (-not [string]::Equals($sourcePath, $logoPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    Copy-Item -LiteralPath $sourcePath -Destination $logoPath
}

function Set-ImageQuality {
    param([System.Drawing.Graphics]$Graphics)
    $Graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $Graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $Graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $Graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $Graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
}

function Draw-Logo {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Image]$Source,
        [System.Drawing.RectangleF]$Bounds
    )
    $ratio = [Math]::Min($Bounds.Width / $Source.Width, $Bounds.Height / $Source.Height)
    $drawWidth = [single]($Source.Width * $ratio)
    $drawHeight = [single]($Source.Height * $ratio)
    $destination = [System.Drawing.RectangleF]::new(
        [single]($Bounds.X + ($Bounds.Width - $drawWidth) / 2),
        [single]($Bounds.Y + ($Bounds.Height - $drawHeight) / 2),
        $drawWidth,
        $drawHeight
    )
    $attributes = [System.Drawing.Imaging.ImageAttributes]::new()
    try {
        $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $Graphics.DrawImage($Source, [System.Drawing.Rectangle]::Round($destination), 0, 0, $Source.Width, $Source.Height,
            [System.Drawing.GraphicsUnit]::Pixel, $attributes)
    }
    finally { $attributes.Dispose() }
}

function New-LogoBitmap {
    param([System.Drawing.Image]$Source, [int]$Size)
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        Set-ImageQuality $graphics
        $graphics.Clear([System.Drawing.Color]::White)
        Draw-Logo $graphics $Source ([System.Drawing.RectangleF]::new(0, 0, $Size, $Size))
    }
    finally { $graphics.Dispose() }
    return $bitmap
}

function Write-MultiSizeIcon {
    param([System.Drawing.Image]$Source, [string]$Path)
    $sizes = @(16, 32, 48, 64, 128, 256)
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $bitmap = New-LogoBitmap $Source $size
        $imageStream = [System.IO.MemoryStream]::new()
        try {
            if ($size -eq 256) {
                # PNG is the standard high-resolution Windows icon entry.
                $bitmap.Save($imageStream, [System.Drawing.Imaging.ImageFormat]::Png)
            }
            else {
                # Opaque 32-bit Windows DIB, including an empty transparency mask.
                $writer = [System.IO.BinaryWriter]::new($imageStream)
                $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
                $writer.Write([uint32]40)
                $writer.Write([int]$size)
                $writer.Write([int]($size * 2))
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]0)
                $writer.Write([uint32]($size * $size * 4 + $maskStride * $size))
                $writer.Write([int]0)
                $writer.Write([int]0)
                $writer.Write([uint32]0)
                $writer.Write([uint32]0)
                for ($y = $size - 1; $y -ge 0; $y--) {
                    for ($x = 0; $x -lt $size; $x++) {
                        $pixel = $bitmap.GetPixel($x, $y)
                        $writer.Write([byte]$pixel.B)
                        $writer.Write([byte]$pixel.G)
                        $writer.Write([byte]$pixel.R)
                        $writer.Write([byte]255)
                    }
                }
                $writer.Write([byte[]]::new($maskStride * $size))
                $writer.Flush()
            }
            $images.Add($imageStream.ToArray())
        }
        finally {
            $imageStream.Dispose()
            $bitmap.Dispose()
        }
    }
    $file = [System.IO.File]::Create($Path)
    $iconWriter = [System.IO.BinaryWriter]::new($file)
    try {
        $iconWriter.Write([uint16]0)
        $iconWriter.Write([uint16]1)
        $iconWriter.Write([uint16]$sizes.Count)
        $offset = [uint32](6 + 16 * $sizes.Count)
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $dimension = if ($sizes[$i] -eq 256) { [byte]0 } else { [byte]$sizes[$i] }
            $iconWriter.Write($dimension)
            $iconWriter.Write($dimension)
            $iconWriter.Write([byte]0)
            $iconWriter.Write([byte]0)
            $iconWriter.Write([uint16]1)
            $iconWriter.Write([uint16]32)
            $iconWriter.Write([uint32]$images[$i].Length)
            $iconWriter.Write($offset)
            $offset += [uint32]$images[$i].Length
        }
        foreach ($entry in $images) { $iconWriter.Write($entry) }
    }
    finally {
        $iconWriter.Dispose()
        $file.Dispose()
    }
}

function Write-WizardBitmap {
    param([System.Drawing.Image]$Source, [int]$Scale, [string]$Path)
    $bitmap = [System.Drawing.Bitmap]::new((164 * $Scale), (314 * $Scale), [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $blue = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(0, 77, 158))
    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
    $lightBlue = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(196, 219, 242))
    $titleFont = [System.Drawing.Font]::new('Microsoft YaHei', 13, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $departmentFont = [System.Drawing.Font]::new('Microsoft YaHei', 12, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $versionFont = [System.Drawing.Font]::new('Microsoft YaHei', 11, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $format = [System.Drawing.StringFormat]::new()
    $format.Alignment = [System.Drawing.StringAlignment]::Near
    $format.LineAlignment = [System.Drawing.StringAlignment]::Near
    $format.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap
    $rulePen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(90, 151, 211), 1)
    try {
        Set-ImageQuality $graphics
        $graphics.ScaleTransform($Scale, $Scale)
        $graphics.Clear([System.Drawing.Color]::White)
        Draw-Logo $graphics $Source ([System.Drawing.RectangleF]::new(14, 18, 136, 136))
        $graphics.FillRectangle($blue, 0, 174, 164, 140)
        $graphics.DrawString('上海卫星装备', $titleFont, $white, [System.Drawing.RectangleF]::new(16, 193, 132, 20), $format)
        $graphics.DrawString('研究所', $titleFont, $white, [System.Drawing.RectangleF]::new(16, 216, 132, 20), $format)
        $graphics.DrawLine($rulePen, 16, 249, 148, 249)
        $graphics.DrawString('总装中心', $departmentFont, $white, [System.Drawing.RectangleF]::new(16, 263, 132, 20), $format)
        $graphics.DrawString('0.1.2', $versionFont, $lightBlue, [System.Drawing.RectangleF]::new(16, 289, 132, 17), $format)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    }
    finally {
        $rulePen.Dispose()
        $format.Dispose()
        $versionFont.Dispose()
        $departmentFont.Dispose()
        $titleFont.Dispose()
        $lightBlue.Dispose()
        $white.Dispose()
        $blue.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$source = [System.Drawing.Image]::FromFile($logoPath)
try {
    Write-MultiSizeIcon $source (Join-Path $outputPath 'casc.ico')
    Write-WizardBitmap $source 1 (Join-Path $outputPath 'installer-wizard.bmp')
    Write-WizardBitmap $source 2 (Join-Path $outputPath 'installer-wizard@2x.bmp')
    foreach ($entry in @(@(55, 'installer-small.bmp'), @(165, 'installer-small@3x.bmp'))) {
        $small = New-LogoBitmap $source $entry[0]
        try { $small.Save((Join-Path $outputPath $entry[1]), [System.Drawing.Imaging.ImageFormat]::Bmp) }
        finally { $small.Dispose() }
    }
}
finally { $source.Dispose() }

$qrc = @'
<RCC>
  <qresource prefix="/branding">
    <file alias="casc-logo.jpg">casc-logo.jpg</file>
    <file alias="casc.ico">casc.ico</file>
  </qresource>
</RCC>
'@
$resource = @'
#pragma code_page(65001)
#include <windows.h>

1 ICON "casc.ico"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION 0,1,2,0
 PRODUCTVERSION 0,1,2,0
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0x0L
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "080404B0"
        BEGIN
            VALUE "CompanyName", "上海卫星装备研究所总装中心\0"
            VALUE "FileDescription", "上海卫星装备研究所曲面轨迹生成软件\0"
            VALUE "FileVersion", "0.1.2.0\0"
            VALUE "InternalName", "SurfacePathStudio.exe\0"
            VALUE "OriginalFilename", "SurfacePathStudio.exe\0"
            VALUE "ProductName", "上海卫星装备研究所曲面轨迹生成软件\0"
            VALUE "ProductVersion", "0.1.2.0\0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0804, 1200
    END
END
'@
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $outputPath 'branding.qrc'), $qrc + [Environment]::NewLine, $utf8)
[System.IO.File]::WriteAllText((Join-Path $outputPath 'application.rc'), $resource + [Environment]::NewLine, $utf8)
Write-Host "Branding resources generated in $outputPath"
