<#
.SYNOPSIS
    After a pull: builds everything and brings it into the game in one go.

.DESCRIPTION
    Does by itself what otherwise takes XBLAH, Visual Studio and deploy.bat:

    1. Finds Visual Studio 2022 (with C++) through vswhere.
    2. Keeps sp\src\vpc_scripts\newer_vs_toolsets.vpc on VS2022 "1" (XBLAH
       sets it to "0" with Visual Studio 2026; then the build fails).
    3. Makes the project files (VPC, as creategameprojects.bat and
       createmaptoolsprojects.bat) - only when they are missing or a .vpc /
       .vgc file is newer than them (a pull changed the project lists) -
       and adds the Release|Win32 build lines VPC leaves out (without them
       MSBuild, and XBLAH's build, build nothing and report success).
    4. Builds games.sln (Release): Client and Server (Episodic and HL2) and
       Shaders (Fidelity) - only what changed since the last build.
    5. Copies client.dll and server.dll into the mod's bin folder (the mod
       loads Mapbase's own DLLs without a word if they are missing).
    6. Builds VRAD with the baked ambient occlusion (-fidelityao) from
       maptools.sln and puts vrad.exe and vrad_dll.dll into the bin folder of
       Source SDK Base 2013 Singleplayer. The files there before (Mapbase's)
       are kept once in bin\fidelity_backup; -RestoreVrad puts them back.
    7. Runs deploy.ps1 (shaders, cfg, shader DLL, generated materials).

    The full build log is in fidelity\update.log. Close Hammer and running
    map compiles first (VRAD's files can not be replaced while in use).

.PARAMETER GamePath
    The mod folder (with gameinfo.txt). Optional: found in Steam like deploy.bat.

.PARAMETER Game
    episodic or hl2: whose client.dll / server.dll go into the mod. Default:
    from the mod's gameinfo.txt (episodic if it mounts ep2 or episodic).

.PARAMETER NoVrad
    Do not build or install VRAD.

.PARAMETER RestoreVrad
    Put back the vrad.exe and vrad_dll.dll that were there before (from
    bin\fidelity_backup), and nothing else.

.EXAMPLE
    update.bat
    update.bat -NoVrad
    update.bat -RestoreVrad
#>
param(
    [string]$GamePath = '',
    [ValidateSet('', 'episodic', 'hl2')][string]$Game = '',
    [switch]$NoVrad,
    [switch]$RestoreVrad
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
. (Join-Path $Root 'fidelity_common.ps1')

# sp\src and sp\game (FIDELITY_SRC_DIR replaces them, for the tests)
$Src = [IO.Path]::GetFullPath((Join-Path $Root '../sp/src'))
if ($env:FIDELITY_SRC_DIR) { $Src = [IO.Path]::GetFullPath($env:FIDELITY_SRC_DIR) }
$GameDir = [IO.Path]::GetFullPath((Join-Path $Src '../game'))
$Log = Join-Path $Root 'update.log'
$VradFiles = @('vrad.exe', 'vrad_dll.dll')

# Stops with a message that says what to do
function Stop-Update([string]$Message) {
    Write-Host ""
    Write-Host "  FAILED   $Message"
    exit 1
}

# ---------------------------------------------------------------- locate ---
# (a path ending in \" arrives with the quote: "D:\...\mymod\" in a .bat)
if ($GamePath) { $GamePath = $GamePath.Trim().Trim('"').TrimEnd('\', '/') }
if (-not $GamePath) { $GamePath = Find-ModFolder }
if (-not $GamePath) { $GamePath = ([string](Read-Host "Mod folder (steamapps\sourcemods\<mod>, the one with gameinfo.txt)")).Trim().Trim('"') }
if (-not $GamePath -or -not (Test-Path -LiteralPath (Join-Path $GamePath 'gameinfo.txt'))) {
    throw "'$GamePath' has no gameinfo.txt. Pass the mod folder as -GamePath."
}
$Mod = (Resolve-Path -LiteralPath $GamePath).Path
Write-Host "mapbase-fidelity update -> $Mod"

# The bin folder of Source SDK Base 2013 Singleplayer, where the map compile
# tools are (FIDELITY_SDK_BIN replaces the search, for the tests), or $null
function Find-SdkBin {
    if ($env:FIDELITY_SDK_BIN) { return $env:FIDELITY_SDK_BIN }
    foreach ($library in Get-SteamLibraries $Mod) {
        $bin = Join-Path $library 'common/Source SDK Base 2013 Singleplayer/bin'
        if (Test-Path -LiteralPath (Join-Path $bin 'vbsp.exe')) { return $bin }
    }
    return $null
}

# ---------------------------------------------------------- restore VRAD ---
# The backup (bin\fidelity_backup) lists what it holds in $BackupList: "saved
# <file>" for a file kept there, "ours <file>" for one that was already this
# repository's VRAD (copied by hand) - there is nothing older to put back.
$BackupList = 'fidelity_backup.txt'
if ($RestoreVrad) {
    $bin = Find-SdkBin
    $backup = $(if ($bin) { Join-Path $bin 'fidelity_backup' } else { $null })
    $list = $(if ($backup) { Join-Path $backup $BackupList } else { $null })
    if (-not $list -or -not (Test-Path -LiteralPath $list)) { Stop-Update "no saved VRAD found (bin\fidelity_backup of Source SDK Base 2013 Singleplayer)" }
    $entries = @((Read-Text $list) -split "`r?`n" | Where-Object { $_ -match '^(saved|ours) (vrad\.exe|vrad_dll\.dll)$' })
    # every saved file must be there before anything is put back
    foreach ($entry in $entries) {
        $name = $entry.Split(' ')[1]
        if ($entry.StartsWith('saved ') -and -not (Test-Path -LiteralPath (Join-Path $backup $name))) { Stop-Update "bin\fidelity_backup\$name is missing - nothing was changed" }
    }
    foreach ($entry in $entries) {
        $name = $entry.Split(' ')[1]
        if ($entry.StartsWith('ours ')) { Write-Host "  kept     $name (it was already ours when saved - reinstall Mapbase's compile tools for theirs)"; continue }
        try { Copy-Item -LiteralPath (Join-Path $backup $name) (Join-Path $bin $name) -Force }
        catch { Stop-Update "could not put back $name ($($_.Exception.Message)) - close Hammer and running compiles" }
        Write-Host "  restored $name"
    }
    Remove-Item -LiteralPath $backup -Recurse -Force
    Write-Host "Done."
    exit 0
}

# --------------------------------------------------------- Visual Studio ---
# MSBuild of Visual Studio 2022 with the C++ tools (FIDELITY_MSBUILD replaces it, for the tests)
function Find-MSBuild {
    if ($env:FIDELITY_MSBUILD) { return $env:FIDELITY_MSBUILD }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $found = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -find 'MSBuild\**\Bin\MSBuild.exe'
    return @($found | Where-Object { $_ }) | Select-Object -First 1
}
$MSBuild = Find-MSBuild
if (-not $MSBuild) { Stop-Update "Visual Studio 2022 with 'Desktop development with C++' not found (Visual Studio Installer)" }
Write-Host "  found    $MSBuild"

# ------------------------------------------------------------- toolset ---
$toolsets = Join-Path $Src 'vpc_scripts/newer_vs_toolsets.vpc'
$toolsetChanged = $false
if (Test-Path -LiteralPath $toolsets) {
    $text = Read-Text $toolsets
    $fixed = [regex]::Replace($text, '(?m)^(\$Conditional[ \t]+VS2022[ \t]+)"0"', '$1"1"')
    if ($fixed -ne $text) {
        Write-Text $toolsets $fixed
        $toolsetChanged = $true
        Write-Host "  fixed    newer_vs_toolsets.vpc: VS2022 back to 1 (XBLAH had set it to 0)"
    }
}

# -------------------------------------------------------- project files ---
# Made again when missing, after the toolset fix, or when a .vpc / .vgc file
# is newer (VPC itself skips projects that did not change)
$script:VpcNewest = $null
function Test-ProjectsOutdated([string]$Solution) {
    if ($toolsetChanged -or -not (Test-Path -LiteralPath $Solution)) { return $true }
    if ($null -eq $script:VpcNewest) {
        $files = @(Get-ChildItem -LiteralPath $Src -Recurse -File -Filter '*.vpc') + @(Get-ChildItem -LiteralPath $Src -Recurse -File -Filter '*.vgc')
        $script:VpcNewest = ($files | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
    }
    return $script:VpcNewest -gt (Get-Item -LiteralPath $Solution).LastWriteTimeUtc
}

# VPC (FIDELITY_VPC replaces devtools\bin\vpc.exe, for the tests)
function Update-Projects([string]$Solution, [string[]]$Arguments) {
    $path = Join-Path $Src $Solution
    if (-not (Test-ProjectsOutdated $path)) {
        Write-Host "  ok       $Solution is up to date"
        return
    }
    $vpc = $(if ($env:FIDELITY_VPC) { $env:FIDELITY_VPC } else { Join-Path $Src 'devtools/bin/vpc.exe' })
    if (-not (Test-Path -LiteralPath $vpc)) { Stop-Update "$vpc is missing - is this the mymod repository (sp\src)?" }
    Write-Host "  making   $Solution (VPC)"
    Push-Location -LiteralPath $Src
    try { & $vpc @Arguments /mksln $Solution } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $path)) {
        Stop-Update "VPC could not make $Solution - run XBLAH Project > Configure once, then update.bat again"
    }
}

# --------------------------------------------------------------- build ---
# The solution's platform for Release (VPC writes Win32; one upgraded in
# Visual Studio may say x86)
function Get-SolutionPlatform([string]$Solution) {
    $m = [regex]::Match((Read-Text $Solution), 'Release\|([A-Za-z0-9]+)\s*=')
    if ($m.Success) { return $m.Groups[1].Value }
    return 'Win32'
}

# VPC writes a solution with the projects only, without the "Global" part
# that says which projects build in Release|Win32. Visual Studio adds it when
# it opens the solution; MSBuild (here and in XBLAH) does not, builds nothing
# and reports success in a split second. So it is added here, the way Visual
# Studio writes it - only when a C++ project of the solution has no Release
# build line (a complete solution, e.g. one saved by Visual Studio, stays).
function Repair-Solution([string]$Solution) {
    $path = Join-Path $Src $Solution
    # as UTF-8, keeping the byte order mark VPC writes (ReadAllText would drop it silently)
    $bytes = [IO.File]::ReadAllBytes($path)
    $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $skip = $(if ($bom) { 3 } else { 0 })
    $text = [Text.Encoding]::UTF8.GetString($bytes, $skip, $bytes.Length - $skip)
    $nl = Get-Newline $text
    $guids = @([regex]::Matches($text, 'Project\("\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942\}"\)\s*=\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"(\{[0-9A-Fa-f-]+\})"') |
        ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Select-Object -Unique)
    if ($guids.Count -eq 0) { return }
    $platform = Get-SolutionPlatform $path
    $missing = @($guids | Where-Object { $text -notmatch ([regex]::Escape($_) + '\.Release\|' + [regex]::Escape($platform) + '\.Build\.0\s*=') })
    if ($missing.Count -eq 0) { return }

    $lines = @('Global', "`tGlobalSection(SolutionConfigurationPlatforms) = preSolution",
        "`t`tDebug|Win32 = Debug|Win32", "`t`tRelease|Win32 = Release|Win32", "`tEndGlobalSection",
        "`tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    foreach ($guid in $guids) {
        foreach ($config in @('Debug', 'Release')) {
            $lines += "`t`t$guid.$config|Win32.ActiveCfg = $config|Win32"
            $lines += "`t`t$guid.$config|Win32.Build.0 = $config|Win32"
        }
    }
    $lines += @("`tEndGlobalSection", "`tGlobalSection(SolutionProperties) = preSolution", "`t`tHideSolutionNode = FALSE",
        "`tEndGlobalSection", 'EndGlobal')
    # an incomplete Global part (an older solution, a project added since) is replaced as a whole
    $body = [regex]::Replace($text, '(?ms)^Global\s*$.*?^EndGlobal\s*$', '').TrimEnd()
    [IO.File]::WriteAllText($path, ($body + $nl + ($lines -join $nl) + $nl), (New-Object System.Text.UTF8Encoding($bom)))
    Write-Host "  fixed    $Solution`: Release|Win32 build lines added for $($guids.Count) projects (VPC leaves them out, MSBuild built nothing)"
}

# Builds a solution (or some of its projects); the full log goes to update.log
function Invoke-Build([string]$Solution, [string[]]$Projects = @()) {
    $path = Join-Path $Src $Solution
    $platform = Get-SolutionPlatform $path
    $arguments = @($path, '-m', '-nologo', '-v:minimal', '-clp:ErrorsOnly;Summary',
        "-p:Configuration=Release;Platform=$platform", '-fl', "-flp:LogFile=$Log;Verbosity=normal;Append")
    if ($Projects.Count -gt 0) {
        # sln targets are the project names ('.' and a few others become '_')
        $arguments += '-t:' + (($Projects | ForEach-Object { $_ -replace '[.%$@;'']', '_' }) -join ';')
    }
    Write-Host "  building $Solution$(if ($Projects.Count) { ' (' + ($Projects -join ', ') + ')' })"
    & $MSBuild @arguments
    if ($LASTEXITCODE -ne 0) { Stop-Update "the build of $Solution failed - the errors are above, the whole log is fidelity\update.log" }
}

# The projects of a solution whose name matches a pattern
function Get-SolutionProjects([string]$Solution, [string]$Pattern) {
    $found = @()
    foreach ($m in [regex]::Matches((Read-Text (Join-Path $Src $Solution)), 'Project\("\{[^}]+\}"\)\s*=\s*"([^"]+)"')) {
        if ($m.Groups[1].Value -match $Pattern) { $found += $m.Groups[1].Value }
    }
    return $found
}

if (Test-Path -LiteralPath $Log) { Remove-Item -LiteralPath $Log -Force }

Update-Projects 'games.sln' @('/hl2', '/episodic', '+game')
Repair-Solution 'games.sln'
Invoke-Build 'games.sln'

# ------------------------------------------------------ DLLs into the mod ---
# Where a build put a file: the project's publish folder (OUTBINDIR: sp\game\...
# as in Mapbase - but XBLAH's Configure may point it at the mod's bin, as on
# Julius' PC) or the linker's own output next to the project (Release_mod_
# episodic and the like, always there after a build). The newest one wins.
function Find-Built([string]$Name, [string[]]$Folders) {
    $found = @($Folders | Where-Object { $_ } | ForEach-Object { Join-Path $_ $Name } | Where-Object { Test-Path -LiteralPath $_ })
    return $found | Sort-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } -Descending | Select-Object -First 1
}

# The linker's Release folders of a project (Pattern: which of them, e.g. only this game's)
function Get-ReleaseFolders([string]$Project, [string]$Pattern = '^Release') {
    $folder = Join-Path $Src $Project
    if (-not (Test-Path -LiteralPath $folder)) { return @() }
    return @(Get-ChildItem -LiteralPath $folder -Directory | Where-Object { $_.Name -match $Pattern } | ForEach-Object { $_.FullName })
}

if (-not $Game) {
    # (without // comments: a commented-out ep2 line is no episodic mod)
    $info = (Read-Text (Join-Path $Mod 'gameinfo.txt')) -replace '//[^\r\n]*', ''
    $Game = $(if ($info -match '(?i)\b(ep2|episodic)\b') { 'episodic' } else { 'hl2' })
}
$built = Join-Path $GameDir "mod_$Game/bin"
$modBin = Join-Path $Mod 'bin'
New-Item -ItemType Directory -Force -Path $modBin | Out-Null
foreach ($name in @('client.dll', 'server.dll')) {
    $target = Join-Path $modBin $name
    # (only this game's Release_mod_episodic / Release_mod_hl2: the other game's DLL must not win)
    $outputs = Get-ReleaseFolders ('game/' + $name.Replace('.dll', '')) "^Release_?(mod_)?$Game$"
    $source = Find-Built $name (@($built) + $outputs)
    if (-not $source) { Stop-Update "$name was not built (looked in sp\game\mod_$Game\bin and sp\src\game\...\Release_mod_$Game) - see fidelity\update.log" }
    if ((Test-Path -LiteralPath $target) -and (Get-BytesHash ([IO.File]::ReadAllBytes($target))) -eq (Get-BytesHash ([IO.File]::ReadAllBytes($source)))) {
        Write-Host "  ok       bin\$name is current ($Game)"
        continue
    }
    try { Copy-Item -LiteralPath $source $target -Force } catch { Stop-Update "could not copy bin\$name ($($_.Exception.Message)) - close the mod, then update.bat again" }
    Write-Host "  copied   bin\$name ($Game)"
}

# ----------------------------------------------------------------- VRAD ---
if ($NoVrad) {
    Write-Host "  skipped  VRAD (-NoVrad)"
} else {
    $sdkBin = Find-SdkBin
    if (-not $sdkBin) {
        Write-Host "  skipped  VRAD - Source SDK Base 2013 Singleplayer not found (Steam library)"
    } else {
        Update-Projects 'maptools.sln' @('+maptools')
        Repair-Solution 'maptools.sln'
        $vradProjects = @(Get-SolutionProjects 'maptools.sln' '^(?i)vrad_(dll|launcher)$')
        if ($vradProjects.Count -lt 2) { Stop-Update "maptools.sln has no Vrad_dll / Vrad_launcher projects" }
        Invoke-Build 'maptools.sln' $vradProjects
        # what the build made: sp\game\bin, or the linker's Release folder of each project
        $vradBuilt = @{
            'vrad_dll.dll' = Find-Built 'vrad_dll.dll' (@(Join-Path $GameDir 'bin') + (Get-ReleaseFolders 'utils/vrad'))
            'vrad.exe'     = Find-Built 'vrad.exe' (@(Join-Path $GameDir 'bin') + (Get-ReleaseFolders 'utils/vrad_launcher'))
        }
        foreach ($name in $VradFiles) {
            if (-not $vradBuilt[$name]) { Stop-Update "$name was not built (looked in sp\game\bin and the project's Release folder) - see fidelity\update.log" }
        }

        # Mapbase's (or Valve's) files are kept once, before ours replace them:
        # first into a folder of its own, which becomes the backup only when
        # complete (a copy that failed halfway lost Mapbase's DLL on the next
        # run); a file that already is ours (copied by hand) is only listed
        $backup = Join-Path $sdkBin 'fidelity_backup'
        if (-not (Test-Path -LiteralPath (Join-Path $backup $BackupList))) {
            $temp = Join-Path $sdkBin 'fidelity_backup_new'
            try {
                if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
                if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }	# left incomplete by an older run
                New-Item -ItemType Directory -Force -Path $temp | Out-Null
                $lines = @()
                foreach ($name in $VradFiles) {
                    $original = Join-Path $sdkBin $name
                    if (-not (Test-Path -LiteralPath $original)) { continue }
                    $built = $vradBuilt[$name]
                    if ((Test-Path -LiteralPath $built) -and (Get-BytesHash ([IO.File]::ReadAllBytes($original))) -eq (Get-BytesHash ([IO.File]::ReadAllBytes($built)))) {
                        $lines += "ours $name"
                        continue
                    }
                    Copy-Item -LiteralPath $original (Join-Path $temp $name)
                    $lines += "saved $name"
                }
                Write-Text (Join-Path $temp $BackupList) (($lines -join "`r`n") + "`r`n")
                Move-Item -LiteralPath $temp $backup
            } catch {
                Stop-Update "could not save the VRAD there before ($($_.Exception.Message)) - nothing was replaced"
            }
            Write-Host "  saved    the VRAD there before -> bin\fidelity_backup (update.bat -RestoreVrad puts it back)"
        }
        foreach ($name in $VradFiles) {
            $source = $vradBuilt[$name]
            $target = Join-Path $sdkBin $name
            if ((Test-Path -LiteralPath $target) -and (Get-BytesHash ([IO.File]::ReadAllBytes($target))) -eq (Get-BytesHash ([IO.File]::ReadAllBytes($source)))) { continue }
            try { Copy-Item -LiteralPath $source $target -Force } catch { Stop-Update "could not copy $name ($($_.Exception.Message)) - close Hammer and running compiles, then update.bat again" }
            Write-Host "  copied   $name -> $sdkBin"
        }
        Write-Host "  vrad     ready: baked ambient occlusion and fidelity_lights.rad by itself (nothing to add in XBLAH / Hammer)"
    }
}

# --------------------------------------------------------------- deploy ---
Write-Host ""
# (with the brick textures in fidelity_preview: report.bat sends them along - the new brick
# relief is worked out from them; Julius 26.10.2026: nothing to type himself)
& (Join-Path $Root 'deploy.ps1') -GamePath $Mod -ParallaxPreview 'brick'
Write-Host ""
Write-Host "All set. Start the mod, or compile a map in XBLAH / Hammer as usual - nothing else to do."
