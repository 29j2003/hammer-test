<#
.SYNOPSIS
    mapbase-fidelity report: packs what the game's fidelity_report command
    wrote, to send when something looks wrong.

.DESCRIPTION
    In the game: look at the spot (a wall, a floor, a window, or hold the
    weapon), open the console and type
        fidelity_report
    (or with a note: fidelity_report Fugen stehen raus). Repeat for each
    spot. The game writes the material, its textures, what the surface is
    and the weapon's events into <mod>\fidelity_report and takes a
    screenshot.

    Then double-click report.bat: it packs the reports made since the last
    time, the screenshots taken meanwhile and the textures update.bat put
    into <mod>\fidelity_preview (the game's brick walls, in textures\) into
    <mod>\fidelity_report.zip - in parts of at most 25 MB
    (fidelity_report_2.zip ...) when it is more. Send those files. Each
    texture goes in once (as the game has it, .vtf - a texture used by
    several reports, and every report holds the decal atlas, only the first
    time; duplicates.txt lists the rest): a whole folder with pictures of
    each came to over 70 MB (Julius 26.10.2026). What was packed (reports,
    screenshots, wall textures) is noted in
    fidelity_report\packed.txt; nothing new: the zips of last time stay.

.PARAMETER GamePath
    The mod folder (with gameinfo.txt). Optional: found in Steam like deploy.bat.

.PARAMETER All
    Packs all reports and textures again, also those packed before.

.PARAMETER Clear
    Deletes the folder fidelity_report and the zips (after sending).
#>
param(
    [string]$GamePath = '',
    [switch]$All,
    [switch]$Clear
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
. (Join-Path $PSScriptRoot 'fidelity_common.ps1')

# (a path ending in \" arrives with the quote: "D:\...\mymod\" in a .bat)
if ($GamePath) { $GamePath = $GamePath.Trim().Trim('"').TrimEnd('\', '/') }
if (-not $GamePath) { $GamePath = Find-ModFolder }
if (-not $GamePath) { $GamePath = ([string](Read-Host "Mod folder (steamapps\sourcemods\<mod>, the one with gameinfo.txt)")).Trim().Trim('"') }
if (-not $GamePath -or -not (Test-Path -LiteralPath (Join-Path $GamePath 'gameinfo.txt'))) {
    throw "'$GamePath' has no gameinfo.txt. Pass the mod folder as -GamePath."
}
$Game = (Resolve-Path -LiteralPath $GamePath).Path
$Folder = Join-Path $Game 'fidelity_report'
$Packed = Join-Path $Folder 'packed.txt'
$PartLimit = 25MB
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# The zips of earlier runs (fidelity_report.zip, fidelity_report_2.zip ...)
function Get-ReportZips { @(Get-ChildItem -LiteralPath $Game -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^fidelity_report(_\d+)?\.zip$' }) }

Write-Host "mapbase-fidelity report -> $Game"
if ($Clear) {
    if (Test-Path -LiteralPath $Folder) { Remove-Item -LiteralPath $Folder -Recurse -Force }
    Get-ReportZips | Remove-Item -Force
    Write-Host "  cleared  fidelity_report and its zips"
    exit 0
}

# what was packed before (report numbers, wall textures), unless -All
$done = @{}
if (-not $All -and (Test-Path -LiteralPath $Packed)) {
    foreach ($line in [IO.File]::ReadAllLines($Packed)) { if ($line) { $done[$line] = $true } }
}
$infos = @(Get-ChildItem -LiteralPath $Folder -Filter '*_info.txt' -File -ErrorAction SilentlyContinue |
    Where-Object { -not $done.ContainsKey('report ' + $_.Name.Substring(0, 2)) } | Sort-Object Name)
# the textures update.bat collected (fidelity_preview: the game's brick walls), to work their relief out
$previews = @(Get-ChildItem -LiteralPath (Join-Path $Game 'fidelity_preview') -Filter '*.vtf' -File -ErrorAction SilentlyContinue |
    Where-Object { -not $done.ContainsKey('texture ' + $_.Name) } | Sort-Object Name)
if ($infos.Count -eq 0 -and $previews.Count -eq 0) {
    $old = Get-ReportZips
    if ($old.Count -gt 0) {
        Write-Host "  nothing new since the last time - send these (if not done yet):"
        foreach ($zip in $old) { Write-Host "           $($zip.FullName)" }
        exit 0
    }
    Write-Host "  nothing to pack: in the game look at the spot, open the console and type  fidelity_report"
    exit 1
}

# What goes in: the new reports' files (each texture once), the screenshots
# taken since the first of them, the new wall textures. Pictures (.png) of
# earlier versions stay out.
$entries = New-Object System.Collections.Generic.List[object]
$hashes = @{}
$duplicates = @()
foreach ($info in $infos) {
    $number = $info.Name.Substring(0, 2)
    foreach ($file in @(Get-ChildItem -LiteralPath $Folder -Filter ($number + '_*') -File | Where-Object { $_.Extension -ne '.png' } | Sort-Object Name)) {
        if ($file.Extension -eq '.vtf') {
            $hash = Get-BytesHash ([IO.File]::ReadAllBytes($file.FullName))
            if ($hashes.ContainsKey($hash)) { $duplicates += "$($file.Name) = $($hashes[$hash])"; continue }
            $hashes[$hash] = $file.Name
        }
        $entries.Add(@{ Path = $file.FullName; Name = $file.Name })
    }
}
$shots = 0
$shotNames = @()
if ($infos.Count -gt 0) {
    $first = ($infos | Sort-Object LastWriteTimeUtc | Select-Object -First 1).LastWriteTimeUtc.AddMinutes(-1)
    $screenshots = Join-Path $Game 'screenshots'
    $taken = @(Get-ChildItem -LiteralPath $screenshots -Filter '*.jpg' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTimeUtc -ge $first -and -not $done.ContainsKey('screenshot ' + $_.Name) } | Sort-Object Name)
    foreach ($shot in $taken) {
        $entries.Add(@{ Path = $shot.FullName; Name = 'screenshot_' + $shot.Name })
        $shotNames += 'screenshot ' + $shot.Name
        $shots++
    }
}
foreach ($vtf in $previews) { $entries.Add(@{ Path = $vtf.FullName; Name = 'textures/' + $vtf.Name }) }

# into parts of at most $PartLimit (counted before packing: textures hardly pack smaller)
Get-ReportZips | Remove-Item -Force
$zips = @()
$archive = $null
$size = 0
$extra = ''
if ($duplicates.Count -gt 0) { $extra = "the same texture as the one named after =:`r`n" + ($duplicates -join "`r`n") + "`r`n" }
foreach ($entry in $entries) {
    $length = (Get-Item -LiteralPath $entry.Path).Length
    if ($null -eq $archive -or ( $size + $length -gt $PartLimit -and $size -gt 0 )) {
        if ($archive) { $archive.Dispose() }
        $name = if ($zips.Count -eq 0) { 'fidelity_report.zip' } else { "fidelity_report_$($zips.Count + 1).zip" }
        $path = Join-Path $Game $name
        $archive = [System.IO.Compression.ZipFile]::Open($path, [System.IO.Compression.ZipArchiveMode]::Create)
        $zips += $path
        $size = 0
        if ($extra) {
            $note = $archive.CreateEntry('duplicates.txt')
            $writer = New-Object System.IO.StreamWriter($note.Open())
            $writer.Write($extra)
            $writer.Dispose()
            $extra = ''
        }
    }
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $entry.Path, $entry.Name, [System.IO.Compression.CompressionLevel]::Optimal)
    $size += $length
}
if ($archive) { $archive.Dispose() }

# noted as packed (the next run takes only what is new)
$lines = @($done.Keys)
foreach ($info in $infos) { $lines += 'report ' + $info.Name.Substring(0, 2) }
foreach ($vtf in $previews) { $lines += 'texture ' + $vtf.Name }
$lines += $shotNames
New-Item -ItemType Directory -Force -Path $Folder | Out-Null
[IO.File]::WriteAllLines($Packed, [string[]]($lines | Sort-Object -Unique))

Write-Host ("  packed   {0} reports, {1} screenshots, {2} wall textures ({3} textures already in, left out)" -f $infos.Count, $shots, $previews.Count, $duplicates.Count)
Write-Host ""
Write-Host "Send:"
foreach ($zip in $zips) { Write-Host ("  {0}  ({1:N1} MB)" -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB)) }
Write-Host "(afterwards: report.bat -Clear empties the folder)"
# show it (not in the tests)
if ($env:OS -eq 'Windows_NT' -and $env:FIDELITY_NO_EXPLORER -ne '1') { Start-Process explorer.exe "/select,`"$($zips[0])`"" }
exit 0
