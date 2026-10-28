# mapbase-fidelity: functions shared by deploy.ps1 and sound_tool.ps1
# (dot-sourced: its variables and functions live in the caller's scope).
# Reading the base game's .vpk archives, finding the mod folder, safe paths
# inside the mod, file hashes.

$ModName = 'mymod'

# Latin-1 maps every byte 1:1, so files are written back unchanged apart from our line
$Encoding = [System.Text.Encoding]::GetEncoding(28591)

function Read-Text([string]$Path) { return [System.IO.File]::ReadAllText($Path, $Encoding) }
function Write-Text([string]$Path, [string]$Text) { [System.IO.File]::WriteAllText($Path, $Text, $Encoding) }
function Get-Newline([string]$Text) { if ($Text.Contains("`r`n")) { return "`r`n" } return "`n" }

# A number from a parameter: "1.5" and "1,5" alike (powershell -File reads
# "0,02" as 2 and "1,5" as 15 - German Windows writes the comma), within
# $Min..$Max, else the run stops with a clear message
function ConvertTo-Number([string]$Text, [string]$Name, [double]$Min, [double]$Max) {
    $value = 0.0
    $ok = [double]::TryParse($Text.Trim().Replace(',', '.'), [System.Globalization.NumberStyles]::Float,
        [System.Globalization.CultureInfo]::InvariantCulture, [ref]$value)
    if (-not $ok -or [double]::IsNaN($value) -or $value -lt $Min -or $value -gt $Max) {
        throw "-$Name '$Text': expected a number from $Min to $Max (1.5 and 1,5 both work)"
    }
    return $value
}

# A list from a parameter: "weapons,physics" (powershell -File hands it over
# as one text) or separate values
function Split-List([string[]]$Values) {
    return @($Values | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# steamapps\sourcemods\<mod> below the Steam folder from the registry, or $null
# (FIDELITY_NO_STEAM_LOOKUP=1 switches the lookup off, for the tests)
function Find-ModFolder {
    if ($env:FIDELITY_NO_STEAM_LOOKUP -eq '1') { return $null }
    try {
        $steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath
    } catch {
        return $null
    }
    $candidate = Join-Path $steam "steamapps/sourcemods/$ModName"
    if (Test-Path -LiteralPath (Join-Path $candidate 'gameinfo.txt')) { return $candidate }
    return $null
}

# Folders with the base game's .vpk files: Source SDK Base 2013 Singleplayer
# (or, without it, Half-Life 2) in every Steam library next to the mod, in the order the
# game searches them (gameinfo.txt: ep2, then episodic, then hl2 - the scans
# keep the first file of a name they find, as the game does). Each library
# once (libraryfolders.vdf lists the main one too: every archive was read
# twice). The file is UTF-8 (a library below "C:\Users\Juergen" was not found).
# FIDELITY_VPK_DIRS=dir;dir replaces the search, for the tests.
# The steamapps folders of every Steam library, the mod's own first, each
# once (libraryfolders.vdf lists the main one too)
function Get-SteamLibraries([string]$Game) {
    $steamapps = Split-Path (Split-Path $Game -Parent) -Parent
    $libraries = @($steamapps)
    $vdf = Join-Path $steamapps 'libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        $text = [System.IO.File]::ReadAllText($vdf, [System.Text.Encoding]::UTF8)
        foreach ($m in [regex]::Matches($text, '"path"\s+"([^"]+)"')) {
            $libraries += Join-Path ($m.Groups[1].Value -replace '\\\\', '\') 'steamapps'
        }
    }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $result = @()
    foreach ($lib in $libraries) {
        try { $full = [IO.Path]::GetFullPath($lib).TrimEnd('\', '/') } catch { continue }
        if ($seen.Add($full)) { $result += $full }
    }
    return $result
}

$script:VpkFolders = $null
function Get-VpkFolders([string]$Game) {
    if ($env:FIDELITY_VPK_DIRS) { return @($env:FIDELITY_VPK_DIRS -split ';' | Where-Object { $_ }) }
    if ($null -ne $script:VpkFolders) { return $script:VpkFolders }
    # Half-Life 2's own folders only without Source SDK Base 2013 Singleplayer:
    # the mod mounts the SDK's (reading both read every archive twice)
    $folders = @()
    foreach ($app in @('Source SDK Base 2013 Singleplayer', 'Half-Life 2')) {
        foreach ($full in Get-SteamLibraries $Game) {
            foreach ($sub in @('ep2', 'episodic', 'hl2')) {
                $folder = Join-Path $full "common/$app/$sub"
                if (Test-Path -LiteralPath $folder) { $folders += $folder }
            }
        }
        if ($folders.Count -gt 0) { break }
    }
    $script:VpkFolders = $folders
    return $folders
}

# The .vpk archives of a folder, the _pak ones first (gameinfo.txt mounts
# them before _misc: a file in both is the _pak one in the game); not the
# low-violence one (hl2_lv: gameinfo.txt mounts it only as game_lv)
function Get-VpkArchives([string]$Folder) {
    return @(Get-ChildItem -LiteralPath $Folder -Filter '*_dir.vpk' | Where-Object { $_.Name -notmatch '_lv_dir\.vpk$' } |
        Sort-Object { $_.Name -notmatch '_pak_dir\.vpk$' }, Name)
}

# The .vpk archives of a folder that can hold materials and models (the sound
# and texture archives can not; -WithTextures keeps the texture ones, for .vtf)
function Get-Vpks([string]$Folder, [switch]$WithTextures) {
    $skip = '(sound|textures)'
    if ($WithTextures) { $skip = 'sound' }
    return @(Get-VpkArchives $Folder | Where-Object { $_.Name -notmatch $skip })
}

# The .vpk archives of a folder that hold sounds
function Get-SoundVpks([string]$Folder) {
    return @(Get-VpkArchives $Folder | Where-Object { $_.Name -match 'sound' })
}

# The directory tree of a .vpk (v1/v2), read once per run and kept
# (vpk_reader.cs: the whole tree in arrays, read in C# - PowerShell took most
# of deploy.bat's time with it). A damaged archive (cut short by an update,
# say) gives the entries before the damage.
if (-not ('FidelityVpk' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'vpk_reader.cs') }
$script:VpkIndex = @{}
function Get-VpkIndex([string]$DirFile) {
    if ($script:VpkIndex.ContainsKey($DirFile)) { return $script:VpkIndex[$DirFile] }
    $index = New-Object FidelityVpk $DirFile
    if ($index.Error) { Write-Host "  skipped  $DirFile ($($index.Error))" }
    $script:VpkIndex[$DirFile] = $index
    return $index
}

# Files from a .vpk: the entries with extension -Ext in the folders -Prefix
# (picked in C#, before any PowerShell runs; both optional) for which $Want
# ($ext, $dir, $name; $null = all of them) returns $true, as
# @{ 'dir/name.ext' = bytes }. $Limit > 0 reads only that many bytes of each
# file (headers), -NamesOnly no data at all. With -Each, every file is handed
# to that script block ($key, $bytes) instead and nothing is kept (big files,
# one at a time). Entries whose data is not where the tree says (damaged
# archive) or larger than 64 MB are left out.
function Read-VpkFiles([string]$DirFile, [scriptblock]$Want, [int]$Limit = 0, [switch]$NamesOnly, [scriptblock]$Each = $null,
    [string]$Ext = '', [string[]]$Prefix = @()) {
    $result = @{}
    $index = Get-VpkIndex $DirFile
    if (-not $index.Bytes) { return $result }
    $streams = New-Object 'System.Collections.Generic.Dictionary[int,System.IO.FileStream]'
    try {
        foreach ($i in $index.Find($Ext, [string[]]$Prefix)) {
            if ($Want -and -not (& $Want $index.Ext[$i] $index.Dir[$i] $index.Name[$i])) { continue }
            $key = $index.Key[$i]
            if ($NamesOnly) { $result[$key] = $null; continue }
            $data = $index.Read($i, $Limit, $streams)
            if ($null -eq $data) { continue }
            if ($Each) { & $Each $key $data } else { $result[$key] = $data }
        }
    } finally {
        [FidelityVpk]::Close($streams)
    }
    return $result
}

# ------------------------------------------------------------ mod files ---
# The full path of a file in the mod for a relative path from an archive, a
# model or the manifest, or $null if it would end up outside the mod folder
# ("..", a drive, wildcards): nothing is ever written or deleted elsewhere.
function Get-SafeTarget([string]$Game, [string]$Relative) {
    if (-not $Relative -or $Relative -match '(^|[\\/])\.\.([\\/]|$)' -or $Relative -match '[:*?\[\]<>|"]' -or [IO.Path]::IsPathRooted($Relative)) { return $null }
    $full = [IO.Path]::GetFullPath((Join-Path $Game $Relative))
    $root = [IO.Path]::GetFullPath($Game).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $full
}

# The first line of a text file (our generated files say so there), or ''
# (read directly: it runs for every material in the mod, several times a run)
function Get-FirstLine([string]$Path) {
    if (-not [System.IO.File]::Exists($Path)) { return '' }
    $reader = New-Object System.IO.StreamReader($Path, $Encoding, $true)
    try { return [string]$reader.ReadLine() } finally { $reader.Dispose() }
}

# SHA-256 of bytes, as text (the manifest keeps one per model copy)
function Get-BytesHash([byte[]]$Bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-', '') } finally { $sha.Dispose() }
}

# Adds one line to a list of generated files (path, SHA-256, version) right
# after the file is written, starting the list with $Header: if a run stops
# halfway, the next one still knows these files are ours (written at the end
# only, they counted as the mod's own and were never updated or removed).
# The full list is written again at the end of the run.
function Add-ManifestLine([string]$Game, [string]$Manifest, [string]$Header, [string]$Key, [string]$Hash, [string]$Version) {
    $path = Join-Path $Game $Manifest
    $text = "$Key`t$Hash`t$Version`r`n"
    if (-not (Test-Path -LiteralPath $path)) { $text = "$Header`r`n" + $text }
    [System.IO.File]::AppendAllText($path, $text, $Encoding)
}
