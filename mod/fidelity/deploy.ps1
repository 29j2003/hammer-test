<#
.SYNOPSIS
    Copies the fidelity game files (shaders, cfg) into the mod folder, or removes them again.

.DESCRIPTION
    The fidelity code is part of the mod's source (sp\src\game\client\fidelity) and
    ends up in client.dll. The game files live in this repository under
    fidelity\game and have to be in the mod folder (steamapps\sourcemods\<mod>):

    - shaders\fxc\fidelity_*.vcs
    - cfg\fidelity.cfg, plus the line "exec fidelity" in cfg\autoexec.cfg
    - bin\game_shader_dx6.dll (our own shaders), if it was built
      (sp\game\mod_episodic\bin, project "Shaders (Fidelity)" in games.sln).
      The engine only loads shader DLLs with fixed names; the old name
      game_shader_dx9_fidelity.dll was never loaded and is removed
    - glass bottles: every glass bottle, jug, jar ... material of Half-Life 2
      and its episodes (read from the game's .vpk files) gets a copy in the
      mod's materials folder that uses the Fidelity_Liquid shader, so all
      those props show liquid inside without any work in Hammer. Only with
      the shader DLL: without it those props would be invisible
    - wind: every foliage material (models/props_foliage) gets a copy with
      Mapbase's tree sway switched on (trees and bushes with fitting values),
      so foliage moves in env_wind - or in the light breeze client.dll adds
      to maps without one
    - water: the game's water seen from above (shader Water) gets a copy with
      Water_Fidelity: the same water, with the sun glinting on the waves and
      foam where it is shallow at the shore. Its Water_DX9_HDR values are
      merged in (such blocks only count for Valve's shader). Only with the
      shader DLL
    - dead eyes: the glowing materials of Combine soldiers, metro police and
      hunters get a copy with the FidelityDeadEyes proxy: their eyes go dark
      when they die
    - parallax: the game's bump-mapped walls and floors (LightmappedGeneric,
      also with reflections, detail textures or $ssbump; not blended, not
      see-through), and those without a bump map whose name tells of relief
      (brick, stone, cobble, rock, tile, block, plank, roof ...: height from
      the base texture), and bullet hole decals get the Fidelity_Parallax shader
      (real depth: parallax occlusion mapping) with a height map made from
      their normal map. Only with the shader DLL
    - steeper bump maps: the game's normal maps (of every material with a
      $bumpmap) get copies in the mod with steeper slopes (-BumpStrength,
      default 1.5), so relief shows more everywhere - walls and models (not
      people and creatures: their highlights glared)
    - sheen: model materials with a bump map but no specular highlights get a
      copy with a light Phong sheen, so their relief shows in the light
      (masked by the bump map's alpha, as Valve's own Phong materials are)
    - baked light: fidelity_lights.rad for VRAD (compile with
      -lights fidelity_lights.rad -textureshadows, see docs/ENTWICKLUNG.md):
      every self-illuminated wall texture ($selfillum on LightmappedGeneric /
      WorldVertexTransition, from the game and the mod) that the game's own
      lights.rad does not list gives off light in its colour, and every
      foliage model casts leaf shadows instead of solid blocks
    - distant sounds: far-away versions of the game's gunshots and an
      explosion (sound\fidelity\distant\*.wav: highs gone, echoes off walls,
      a tail - classic filters on the game's own sounds), which client.dll
      plays for shots far away, delayed as sound is (listed in
      fidelity_distant.txt)
    The mod's own materials and models are never overwritten; generated ones
    start with "// mapbase-fidelity: generated" (models: listed with their
    checksum in fidelity_models.txt) and are updated on every run. The
    fidelity files themselves (shaders, fidelity.cfg, the shader DLL) are
    always replaced with the current ones. Nothing is written or deleted
    outside the mod folder, whatever names the game's archives hold.

    Without -GamePath the script looks for steamapps\sourcemods\mymod in the
    Steam folder from the registry and asks if it can't find it. Running it
    twice is safe. The autoexec line ends with "// mapbase-fidelity", so
    -Remove takes out exactly that line again.
    Works with Windows PowerShell 5.1 and PowerShell 7.

.PARAMETER GamePath
    The mod folder: the one that contains gameinfo.txt. Optional.

.PARAMETER Remove
    Remove the fidelity game files from the mod folder again.

.PARAMETER NoBottles
    Do not generate the liquid bottle materials (removes ones made earlier).

.PARAMETER NoWind
    Do not generate the swaying foliage materials (removes ones made earlier).

.PARAMETER NoSoftParticles
    Do not give the old particles (smoke, dust) the soft shader (removes the materials made earlier).

.PARAMETER NoWater
    Do not give the water the sun's glint and foam (removes the materials made earlier).

.PARAMETER NoParallax
    Do not give walls and floors the parallax shader (removes materials and height maps made earlier).

.PARAMETER ParallaxDepth
    Depth of the relief, as a share of the texture's size: 0.01 (default; 0.02 was too strong -
    Julius: a tiled floor looked right at about 0.006), up to 0.1. Bump-mapped walls and bricks and
    tiles laid in courses get 0.6 of it, wood 0.7. "0,01" works as well as "0.01".

.PARAMETER ParallaxWood
    Wood, planks and boards get a little relief too (off by default: the grooves looked odd in the
    game; wood keeps its bump map).

.PARAMETER ParallaxPreview
    Writes pictures of the height maps made from base textures whose path contains this text
    (e.g. brick): fidelity_preview\<texture>.bmp in the mod, the texture on the left, its height on
    the right, and the texture itself (.vtf) - report.bat packs those to send when a wall looks wrong.
    update.bat passes "brick" (the textures for the new brick relief).

.PARAMETER BumpStrength
    How much steeper the normal maps get: 1.5 (default), up to 4; 1 = as in the game
    (removes copies made earlier). Copies are only remade when it changes.
    "1,5" works as well as "1.5".

.PARAMETER NoSheen
    Do not generate the model materials with a Phong sheen (removes ones made earlier).

.PARAMETER NoLights
    Do not generate fidelity_lights.rad (removes one made earlier).

.PARAMETER NoDistantSounds
    Do not make the far-away gunshot sounds (removes ones made earlier).

.EXAMPLE
    .\deploy.ps1
    .\deploy.ps1 -GamePath "D:\Games\Steam\steamapps\sourcemods\mymod"
#>
param(
    [string]$GamePath = '',
    [switch]$Remove,
    [switch]$NoBottles,
    [switch]$NoWind,
    [switch]$NoSoftParticles,
    [switch]$NoWater,
    [switch]$NoSheen,
    [string]$BumpStrength = '1.5',
    [switch]$NoParallax,
    [string]$ParallaxDepth = '0.01',
    [switch]$ParallaxWood,
    [string]$ParallaxPreview = '',
    [switch]$NoLights,
    [switch]$NoDistantSounds
)

$ErrorActionPreference = 'Stop'

$Root = $PSScriptRoot
$LineMarker = '// mapbase-fidelity'
# Shared with sound_tool.ps1: reading the game's archives, safe paths in the mod
. (Join-Path $PSScriptRoot 'fidelity_common.ps1')

# The numbers of the parameters ("1,5" as well as "1.5")
$BumpFactor = ConvertTo-Number $BumpStrength 'BumpStrength' 0 4
$ReliefDepth = ConvertTo-Number $ParallaxDepth 'ParallaxDepth' 0 0.1

# ------------------------------------------------------------- bottles ---
$ShaderDll = 'game_shader_dx6.dll'
# Old name of the DLL; the engine never loaded it
$OldShaderDll = 'game_shader_dx9_fidelity.dll'
$GeneratedMarker = '// mapbase-fidelity: generated by deploy.ps1'
$BottleNames = '(bottle|jug|jar|flask|vial|decanter|carafe|beaker|healthkit|medkit)'

# A .vmt's text without byte order mark and // comments, for the regexes below
# (one pass over the text, and kept: every wall material went through here
# about fifteen times a run)
$script:CleanVmt = @{}
function Remove-VmtComments([string]$Text) {
    $clean = $script:CleanVmt[$Text]
    if ($null -eq $clean) {
        $clean = (($Text -replace '^\xEF\xBB\xBF', '') -replace '//[^\n]*', '') -replace "`r`n", "`n"
        $script:CleanVmt[$Text] = $clean
    }
    return $clean
}

# The .vmt files below materials/models/ the generated materials come from:
# containers, foliage, the characters with glowing eyes and - for the sheen,
# unless -NoSheen - all of them, as @{ 'materials/models/...vmt' = text }.
function Read-VpkModelMaterials([string]$DirFile) {
    $result = @{}
    $files = Read-VpkFiles $DirFile -Ext 'vmt' -Prefix 'materials/models' {
        param($ext, $dir, $name)
        -not $NoSheen -or $dir.StartsWith('materials/models/props_foliage') -or $name -match $BottleNames -or $dir -match $EyesFolders
    }
    foreach ($key in $files.Keys) { $result[$key] = $Encoding.GetString($files[$key]) }
    return $result
}

# ------------------------------------------- containers on shared textures ---
# Some containers use a material they share with other things: Half-Life 2's
# garbage_glassbottle... models are drawn from one texture atlas with cans and
# cartons, and the material is named after the atlas, not a bottle. Changing
# that material would turn the cans into glass. So such a container gets its
# own copy of the model in the mod, whose material name (in the .mdl, changed
# in place, same length) points to a liquid material of its own. The .vvd,
# .vtx and .phy stay the game's: their checksum is in the .mdl header, which
# does not change. Dark glass (Julius: "the grey/brown ones").
$OwnMaterialSuffix = '_flq'
$ModelsManifest = 'fidelity_models.txt'		# the model copies, for updates and -Remove

# Names of the materials an .mdl uses: @{ Textures = @(@{ Name; Offset }); Cd = @(folders) }, or $null
# Every count and offset is checked: a damaged model is skipped, not a crash.
function Read-MdlMaterials([byte[]]$Mdl) {
    if ($Mdl.Length -lt 220 -or $Encoding.GetString($Mdl, 0, 4) -ne 'IDST') { return $null }
    $textures = @()
    $count = [BitConverter]::ToInt32($Mdl, 204)
    $index = [BitConverter]::ToInt32($Mdl, 208)
    if ($count -lt 0 -or $count -gt 1024 -or $index -lt 0) { return $null }
    for ($i = 0; $i -lt $count; $i++) {
        $at = [int64]$index + 64 * $i
        if ($at + 4 -gt $Mdl.Length) { return $null }
        $nameAt = $at + [BitConverter]::ToInt32($Mdl, $at)
        if ($nameAt -le 0 -or $nameAt -ge $Mdl.Length) { return $null }
        $end = [Array]::IndexOf($Mdl, [byte]0, [int]$nameAt)
        if ($end -lt 0) { return $null }
        $textures += @{ Name = $Encoding.GetString($Mdl, $nameAt, $end - $nameAt); Offset = [int]$nameAt }
    }
    $cd = @()
    $count = [BitConverter]::ToInt32($Mdl, 212)
    $index = [BitConverter]::ToInt32($Mdl, 216)
    if ($count -lt 0 -or $count -gt 64 -or $index -lt 0) { return $null }
    for ($i = 0; $i -lt $count; $i++) {
        $slot = [int64]$index + 4 * $i
        if ($slot + 4 -gt $Mdl.Length) { return $null }
        $at = [BitConverter]::ToInt32($Mdl, $slot)
        if ($at -le 0 -or $at -ge $Mdl.Length) { return $null }
        $end = [Array]::IndexOf($Mdl, [byte]0, $at)
        if ($end -lt 0) { return $null }
        $folder = ($Encoding.GetString($Mdl, $at, $end - $at) -replace '\\', '/').ToLowerInvariant()
        if ($folder -and -not $folder.EndsWith('/')) { $folder += '/' }
        $cd += $folder
    }
    return @{ Textures = $textures; Cd = $cd }
}

# Removes the model copies listed in the manifest (and the manifest). Each
# line is "path<TAB>SHA-256 of the copy": a file that changed since (the mod's
# own model put there later) stays. Lines without a hash (older deploy.ps1)
# count as ours when the file still holds our material name.
function Remove-GeneratedModels([string]$Game) {
    $manifest = Join-Path $Game $ModelsManifest
    if (-not (Test-Path -LiteralPath $manifest)) { return }
    foreach ($line in (Read-Text $manifest) -split "`r?`n") {
        if (-not $line -or $line.StartsWith('//')) { continue }
        $parts = $line -split "`t"
        $file = Get-SafeTarget $Game $parts[0]
        if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        $bytes = [System.IO.File]::ReadAllBytes($file)
        $ours = if ($parts.Count -ge 2) { (Get-BytesHash $bytes) -eq $parts[1] } else { $Encoding.GetString($bytes).Contains($OwnMaterialSuffix) }
        if ($ours) { Remove-Item -LiteralPath $file -Force }
        else { Write-Host "  kept     $($parts[0]) (changed since deploy.ps1 made it)" }
    }
    Remove-Item -LiteralPath $manifest -Force
}

# Container models on shared materials: finds them in the game's archives, adds
# their own liquid material to $Materials (source text: the shared one) and
# returns @{ 'models/....mdl' = @{ Bytes = patched .mdl; Own = @(own materials) } }.
function Get-ContainerModels([string[]]$Folders, [hashtable]$Materials) {
    $models = @{}
    foreach ($folder in $Folders) {
        foreach ($vpk in Get-Vpks $folder) {
            $found = Read-VpkFiles $vpk.FullName -Ext 'mdl' -Prefix 'models' {
                param($ext, $dir, $name)
                -not $dir.StartsWith('models/props_foliage') -and $name -match $BottleNames
            }
            foreach ($key in $found.Keys) { if (-not $models.ContainsKey($key)) { $models[$key] = $found[$key] } }
        }
    }
    if ($models.Count -eq 0) { return @{} }

    # Where each model's materials are (first folder of $cdmaterials that has it)
    $parsed = @{}
    $wanted = @{}
    foreach ($key in $models.Keys) {
        $info = Read-MdlMaterials $models[$key]
        if (-not $info) { continue }
        $parsed[$key] = $info
        foreach ($texture in $info.Textures) {
            foreach ($cd in $info.Cd) { $wanted["materials/$cd$(($texture.Name -replace '\\', '/').ToLowerInvariant()).vmt"] = $true }
        }
    }
    $texts = @{}
    foreach ($folder in $Folders) {
        foreach ($vpk in Get-Vpks $folder) {
            $found = Read-VpkFiles $vpk.FullName -Ext 'vmt' -Prefix 'materials' { param($ext, $dir, $name) $wanted.ContainsKey("$dir/$name.vmt") }
            foreach ($key in $found.Keys) { if (-not $texts.ContainsKey($key)) { $texts[$key] = $Encoding.GetString($found[$key]) } }
        }
    }

    # Own material names: the last 4 letters of the name replaced (same length
    # in the .mdl). Names that would clash (...001a and ...001b both give
    # ..._flq) take the next free suffix.
    $ownSources = @{}		# own material -> the shared material it copies
    $suffixes = @($OwnMaterialSuffix) + @(1..9 | ForEach-Object { "_fq$_" })
    $result = @{}
    foreach ($key in ($parsed.Keys | Sort-Object)) {
        $bytes = [byte[]]$models[$key].Clone()
        $owns = @()
        foreach ($texture in $parsed[$key].Textures) {
            $name = ($texture.Name -replace '\\', '/').ToLowerInvariant()
            $material = $null
            foreach ($cd in $parsed[$key].Cd) {
                if ($texts.ContainsKey("materials/$cd$name.vmt")) { $material = "materials/$cd$name.vmt"; break }
            }
            # Not found, or a container material anyway (that one gets the liquid shader itself)
            if (-not $material -or $material -match $BottleNames -or $name.Length -le $OwnMaterialSuffix.Length + 1) { continue }
            $own = $null
            foreach ($suffix in $suffixes) {
                $candidate = $material.Substring(0, $material.Length - 4 - $suffix.Length) + $suffix + '.vmt'
                if (-not $ownSources.ContainsKey($candidate) -or $ownSources[$candidate] -eq $material) { $own = $candidate; break }
            }
            if (-not $own) { $script:BottleReport += "  kept     $key (no free name for its own material)"; continue }
            $ownSources[$own] = $material
            $ownName = $texture.Name.Substring(0, $texture.Name.Length - $suffix.Length) + $suffix
            $Materials[$own] = $texts[$material]
            $script:ForcedKinds[$own] = 'tinted'
            $patch = $Encoding.GetBytes($ownName)
            [Array]::Copy($patch, 0, $bytes, $texture.Offset, $patch.Length)
            $owns += $own
        }
        if ($owns.Count -gt 0) { $result[$key] = @{ Bytes = $bytes; Own = $owns } }
    }
    return $result
}

# ------------------------------------------------------------ baked light ---
$LightsFile = 'fidelity_lights.rad'
# VRAD's lights files have no comments: every line that is not blank is read as
# "texture R G B brightness", "noshadow <material>" or "forcetextureshadow <model>"
# (vrad.cpp ReadLightFile) - a "// ..." line became a texlight named "//"
# ("unknown light specifier type", then "ERROR: Duplication of '//'" for each
# further one). So the file carries no comments; its first line marks it as
# deploy.ps1's own: a noshadow entry no material name contains (VRAD reads it
# without a message, it changes nothing).
$LightsMarker = 'noshadow mapbase_fidelity_generated_by_deploy_ps1'

# ---------------------------------------------------------- distant sounds ---
# Far-away versions of these game sounds (sound_distant.cs); client.dll knows the names
$DistantManifest = 'fidelity_distant.txt'
$DistantVersion = 'distant1'
$DistantHeader = $GeneratedMarker + ' (distant sounds) - far-away gunshots: path, SHA-256, version'
$DistantSources = [ordered]@{
    'sound/fidelity/distant/pistol.wav' = 'sound/weapons/pistol/pistol_fire2.wav'
    'sound/fidelity/distant/smg1.wav' = 'sound/weapons/smg1/smg1_fire1.wav'
    'sound/fidelity/distant/ar2.wav' = 'sound/weapons/ar2/fire1.wav'
    'sound/fidelity/distant/shotgun.wav' = 'sound/weapons/shotgun/shotgun_fire6.wav'
    'sound/fidelity/distant/357.wav' = 'sound/weapons/357/357_fire2.wav'
    'sound/fidelity/distant/explosion.wav' = 'sound/weapons/explode3.wav'
}

# Removes the far-away sounds made earlier (unchanged ones only) but those in $Keep
function Remove-DistantSounds([string]$Game, [hashtable]$Keep) {
    $old = Read-BumpsManifest $Game $DistantManifest
    foreach ($key in $old.Keys) {
        if ($Keep.ContainsKey($key)) { continue }
        $file = Get-SafeTarget $Game $key
        if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        if ((Get-BytesHash ([System.IO.File]::ReadAllBytes($file))) -eq $old[$key].Hash) { Remove-Item -LiteralPath $file -Force }
    }
    $manifest = Join-Path $Game $DistantManifest
    if ($Keep.Count -eq 0) {
        if (Test-Path -LiteralPath $manifest) { Remove-Item -LiteralPath $manifest -Force }
        return
    }
    $lines = @($DistantHeader)
    foreach ($key in ($Keep.Keys | Sort-Object)) { $lines += "$key`t$($Keep[$key].Hash)`t$($Keep[$key].Strength)" }
    Write-Text $manifest (($lines -join "`r`n") + "`r`n")
}
# Texlight brightness (lights.rad "R G B brightness"); HL2's own light
# textures use a few hundred, a glowing screen or sign needs much less.
$TexlightBrightness = 100

# A wall material that glows: $selfillum on a lightmapped shader. Returns its
# $basetexture, or $null.
function Get-GlowingWallTexture([string]$Text) {
    $clean = Remove-VmtComments $Text
    $shader = [regex]::Match($clean, '^\s*"?([A-Za-z0-9_]+)"?\s*\{')
    if (-not $shader.Success -or $shader.Groups[1].Value -notmatch '^(?i)(SDK_)?(LightmappedGeneric|WorldVertexTransition)(_DX\d+)?$') { return $null }
    if ($clean -notmatch '(?i)"?\$selfillum"?\s+"?1') { return $null }
    $texture = Get-BaseTexture $clean
    if (-not $texture) { return $null }
    return $texture.ToLowerInvariant()
}

# Average colour of a texture from its .vtf header (reflectivity, linear), or $null
function Get-VtfReflectivity([byte[]]$Header) {
    if (-not $Header -or $Header.Length -lt 44 -or $Encoding.GetString($Header, 0, 3) -ne 'VTF') { return $null }
    return @([BitConverter]::ToSingle($Header, 32), [BitConverter]::ToSingle($Header, 36), [BitConverter]::ToSingle($Header, 40))
}

# lights.rad colour ("R G B", gamma 0..255) for a texture's average colour:
# the brightest channel at full, a bit more saturated (the glowing parts are
# the colourful ones).
function Get-TexlightColor($Reflectivity) {
    if (-not $Reflectivity) { return '255 255 255' }
    $gray = ($Reflectivity[0] + $Reflectivity[1] + $Reflectivity[2]) / 3
    $c = @(0, 1, 2 | ForEach-Object { [Math]::Max(0.0, $gray + ($Reflectivity[$_] - $gray) * 1.5) })
    $max = ($c | Measure-Object -Maximum).Maximum
    if ($max -le 0.0001) { return '255 255 255' }
    return (($c | ForEach-Object { [int][Math]::Round(255 * [Math]::Pow($_ / $max, 1 / 2.2)) }) -join ' ')
}

# deploy.ps1 made this lights file: its marker line first (earlier versions: a
# "// mapbase-fidelity: generated" comment)
function Test-OwnLightsFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $first = Get-FirstLine $Path
    return ($first -eq $LightsMarker) -or ($first -like "$GeneratedMarker*")
}

# Folders whose lights.rad VRAD may read as the game's: the game folders, the
# mod, the shared Mapbase content (VRAD finds "lights.rad" on the mod's search
# paths) and the SDK's bin folder (VRAD's fallback next to vrad.exe)
function Get-LightsRadFolders([string]$Game, [string[]]$VpkFolders) {
    $folders = @($VpkFolders) + @($Game)
    if ($env:FIDELITY_VPK_DIRS) { return $folders }
    foreach ($library in Get-SteamLibraries $Game) {
        foreach ($app in @('Source SDK Base 2013 Singleplayer', 'Half-Life 2')) {
            $bin = Join-Path $library "common/$app/bin"
            if (Test-Path -LiteralPath $bin) { $folders += $bin }
        }
        $shared = Join-Path $library 'sourcemods/mapbase_shared'
        if (Test-Path -LiteralPath $shared) {
            $folders += $shared
            $folders += @(Get-ChildItem -LiteralPath $shared -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        }
    }
    return $folders
}

# Texture names a lights.rad in these folders already lists (they stay as they are)
function Get-KnownTexlights([string[]]$Folders) {
    $known = @{}
    foreach ($folder in $Folders) {
        $file = Join-Path $folder 'lights.rad'
        if (-not (Test-Path -LiteralPath $file)) { continue }
        foreach ($line in (Read-Text $file) -split "`r?`n") {
            $t = (($line -replace '//.*$', '').Trim()) -replace '^(?i)(hdr|ldr):', ''
            $first = ($t.Trim() -split '\s+')[0]
            if ($first -and $first -notmatch '^(?i)(noshadow|forcetextureshadow)$') { $known[$first.ToLowerInvariant()] = $true }
        }
    }
    return $known
}

# A liquid colour that fits the name (gamma space, like $color)
function Get-LiquidColor([string]$Name) {
    if ($Name -match 'wine') { return '[0.32 0.03 0.07]' }
    if ($Name -match 'beer') { return '[0.62 0.38 0.06]' }
    if ($Name -match '(whisk|liquor|alcohol|rum)') { return '[0.58 0.32 0.08]' }
    if ($Name -match 'milk') { return '[0.92 0.92 0.88]' }
    if ($Name -match 'oil') { return '[0.45 0.34 0.06]' }
    return ''
}

# What happened to each container material, printed after the run
$script:BottleReport = @()

# Materials that are containers although their name does not say so (own
# copies for containers on shared textures, see Get-ContainerModels): path -> kind
$script:ForcedKinds = @{}

# Average colours (linear, from the .vtf headers) of the container textures,
# filled before the liquid materials are written
$script:BottleTextureColors = @{}

# Kinds of container, each with its own liquid and glass:
#   medical  health vial and kit: only their green parts hold liquid (the
#            rest stays as it is), nearly full (82-95 %), thick and slurpy (few big
#            slow bubbles that sway, slow heavy sloshing)
#   tinted   dark glass (brown, grey): the full part dark, the empty part see-through, like the
#            plastic bottles in Half-Life: Alyx
#   drink    clear glass: fizzy, many small fast bubbles
# Every prop holds its own amount between $fillmin and $fillmax: drinks
# 45-95 %, only the open green bottle (glassbottle01) a rest of 2-25 %. Bottles splash when they break.
$MedicalNames = '(vial|medkit|health|syringe|medic)'
$TintedBrightness = 0.15		# texture brightness (linear) below which glass counts as dark

function Get-ContainerKind([string]$Path, $Reflectivity) {
    if ($Path -match $MedicalNames) { return 'medical' }
    if ($Reflectivity) {
        $brightness = 0.2126 * $Reflectivity[0] + 0.7152 * $Reflectivity[1] + 0.0722 * $Reflectivity[2]
        if ($brightness -lt $TintedBrightness) { return 'tinted' }
    }
    return 'drink'
}

# The $basetexture of a material text (comments removed), or ''
function Get-BaseTexture([string]$Clean) {
    $m = [regex]::Match($Clean, '(?i)"?\$basetexture"?\s+"([^"]+)"')
    if (-not $m.Success) { $m = [regex]::Match($Clean, '(?i)"?\$basetexture"?\s+(\S+)') }
    if ($m.Success) { return $m.Groups[1].Value.Replace('\', '/') }
    return ''
}

# Average colours of textures ("materials/....vtf" -> r g b) from the .vtf
# headers: the mod's own files first, then the game's archives.
function Get-VtfColors([string]$Game, [string[]]$Folders, [hashtable]$Needed) {
    $colors = @{}
    foreach ($key in @($Needed.Keys)) {
        $loose = Get-SafeTarget $Game $key
        if ($loose -and (Test-Path -LiteralPath $loose -PathType Leaf)) {
            $header = New-Object byte[] 48
            $stream = [System.IO.File]::OpenRead($loose)
            try { $null = $stream.Read($header, 0, 48) } finally { $stream.Dispose() }
            $colors[$key] = Get-VtfReflectivity $header
        }
    }
    if ($Needed.Count -gt $colors.Count) {
        $wantTexture = { param($ext, $dir, $name) ($ext -eq 'vtf') -and $Needed.ContainsKey("$dir/$name.vtf") }
        foreach ($folder in $Folders) {
            foreach ($vpk in Get-Vpks $folder -WithTextures) {
                $headers = Read-VpkFiles $vpk.FullName $wantTexture -Limit 48 -Ext 'vtf' -Prefix 'materials'
                foreach ($key in $headers.Keys) { if (-not $colors.ContainsKey($key)) { $colors[$key] = Get-VtfReflectivity $headers[$key] } }
            }
        }
    }
    return $colors
}

# The Fidelity_Liquid version of a container material (bottle, jug, jar ... on
# a model), or $null. The texture's alpha decides what stays solid (labels).
function New-LiquidMaterial([string]$Path, [string]$Text) {
    $forced = $script:ForcedKinds[$Path]
    if (-not $forced -and ($Path -notmatch $BottleNames -or $Path.StartsWith('materials/models/props_foliage'))) { return $null }
    $clean = Remove-VmtComments $Text
    $shader = [regex]::Match($clean, '^\s*"?([A-Za-z0-9_]+)"?\s*\{')
    if (-not $shader.Success -or $shader.Groups[1].Value -notmatch '^(SDK_)?(VertexLitGeneric|UnlitGeneric)(_DX\d+)?$') {
        $script:BottleReport += "  kept     $Path (shader $($shader.Groups[1].Value))"
        return $null
    }
    $texture = Get-BaseTexture $clean
    $kind = $forced
    if (-not $kind) { $kind = Get-ContainerKind $Path $(if ($texture) { $script:BottleTextureColors["materials/$texture.vtf".ToLowerInvariant()] }) }
    $script:BottleReport += "  liquid   $Path ($kind)"
    # Parentheses: in a list the comma binds before +, the shader name would end up in the comment line.
    $lines = @(($GeneratedMarker + " (liquid) from $Path (delete this file to keep the original)"), '"Fidelity_Liquid"', '{')
    if ($texture) { $lines += "`t`"`$basetexture`" `"$texture`"" }
    # Only see-through originals have meaningful alpha (glass vs. label); an opaque
    # texture would count as one big label, so the whole bottle becomes glass.
    $seeThrough = ($clean -match '(?i)"?\$translucent"?\s+"?1') -or ($clean -match '(?i)"?\$alpha"?\s+"?0?\.\d')
    if (-not $seeThrough) { $lines += "`t`"`$labelthreshold`" `"2`"" }
    $lines += "`t`"`$liquidplane`" `"[0 0 1 -100000]`""	# set per prop by the proxy
    $color = Get-LiquidColor $Path
    if ($color) { $lines += "`t`"`$liquidcolor`" `"$color`"" }
    switch ($kind) {
        'medical' {
            $lines += @("`t`"`$liquidkey`" `"[0.3 0.8 0.15]`"", "`t`"`$fillmin`" `"0.82`"", "`t`"`$fillmax`" `"0.95`"",
                "`t`"`$bubbles`" `"[0.5 1.1 0.5 1]`"", "`t`"`$viscosity`" `"0.7`"", "`t`"`$liquidalpha`" `"0.95`"")
        }
        'tinted' { $lines += @("`t`"`$tintedglass`" `"0.8`"", "`t`"`$bubbles`" `"[0.3 0.3 4 0.2]`"", "`t`"`$splash`" `"1`"") }
        default { $lines += @("`t`"`$bubbles`" `"[0.35 0.3 4 0.2]`"", "`t`"`$splash`" `"1`"") }
    }
    if ($kind -ne 'medical') {
        # the open green bottle: a rest, and it pours out when tipped over
        if ($Path -match 'glassbottle01') { $lines += @("`t`"`$fillmin`" `"0.02`"", "`t`"`$fillmax`" `"0.25`"", "`t`"`$spill`" `"1`"") }
        else { $lines += @("`t`"`$fillmin`" `"0.45`"", "`t`"`$fillmax`" `"0.95`"") }
    }
    $lines += @("`t`"Proxies`"", "`t{", "`t`t`"FidelityLiquid`" { }", "`t}", '}')
    return ($lines -join "`r`n") + "`r`n"
}

# The foliage material with Mapbase's tree sway on, or $null if it is not a
# VertexLitGeneric model material (or a tree's trunk). Every material of a
# kind gets the same values.
function New-WindMaterial([string]$Path, [string]$Text) {
    if (-not $Path.StartsWith('materials/models/props_foliage')) { return $null }
    $m = [regex]::Match($Text, '(?i)^(?<head>(?:\s*//[^\n]*\n)*\s*)"?(?:SDK_)?VertexLitGeneric"?(?<rest>\s*\{)')
    if (-not $m.Success) { return $null }
    if ($Text -match '(?i)\$treesway\b') { return $null }	# already swaying
    if ($Path -match '(bush|shrub|hedge|fern|ivy|plant|flower|grass|weed|cattail|vine)') {
        # bushes: the whole bush moves
        $values = @('1', '80', '0.1', '60', '0.1', '1.5', '2', '1', '3', '12')
    } else {
        # Trees: only the leaves, needles and twigs move (the see-through
        # materials: $alphatest / $translucent), all of them, low ones too;
        # the trunk and the bark stay still (Julius 26.10.2026: "der nimmt
        # immer noch den ganzen Baum als Sway ... Es dürfen nur die Blätter").
        # Mapbase's trunk sway (by height) is off: $treeswayheight so large
        # that it stays near 0, $treeswaystartheight about 0 - its branch sway and
        # flutter then work at every height (a start height of 0.4 kept the
        # lower leaves still). Branch sway grows with the distance from the
        # trunk ($treeswaystartradius 0: with more, tree_sway.h's
        # length(xy - start) moved the trunk's own vertices, the bark
        # "breathed"). The flutter is calmer (strength 0.6, speed 2 instead
        # of 1.2, 3 - "die Blätter flackern"): the engine draws the shadow
        # maps without the sway, so leaves moving far pass through their
        # own still shadows. Start height 0.0005 x 100000 = the lowest 50 units
        # stay still: the moss on the foot of tree_dead01 (see-through, on the
        # trunk) slid over the bark ("wie die Textur sich unten bewegt ... am
        # Stumpf" - 26.10.2026); branches start higher.
        $clean = Remove-VmtComments $Text
        if ($Path -match '(bark|trunk|stump|log)' -or $clean -notmatch '(?i)"?\$(alphatest|translucent)"?\s+"?1') { return $null }
        $values = @('1', '100000', '0.0005', '250', '0', '1', '2', '0.6', '2', '12')
    }
    $names = @('$treesway', '$treeswayheight', '$treeswaystartheight', '$treeswayradius', '$treeswaystartradius',
        '$treeswayspeed', '$treeswaystrength', '$treeswayscrumblestrength', '$treeswayscrumblespeed', '$treeswayscrumblefrequency')
    $params = for ($i = 0; $i -lt $names.Count; $i++) { "`t`"$($names[$i])`" `"$($values[$i])`"" }
    $body = $Text.Substring($m.Index + $m.Length)
    return $GeneratedMarker + " (wind) from $Path (delete this file to keep the original)`r`n" +
        '"SDK_VertexLitGeneric"' + $m.Groups['rest'].Value + "`r`n" + ($params -join "`r`n") + $body
}

# Soft particles: the old particle system's materials (smoke, dust, the
# game's and client.dll's impact puffs) are UnlitGeneric with vertex colour
# and alpha and cut into walls along a hard line. A copy with
# Fidelity_SoftParticle fades them where they meet the scene. Materials a
# .pcf names are left: the new particles draw them with SpriteCard (soft
# already, and other vertices). Only plain ones: no texture transform,
# alpha test, detail or envmap (the shader has none of them).
$script:PcfMaterials = @{}
function New-SoftParticleMaterial([string]$Path, [string]$Text) {
    if (-not $Path.StartsWith('materials/particle')) { return $null }
    $name = $Path.Substring(10, $Path.Length - 14)	# without materials/ and .vmt
    if ($script:PcfMaterials.ContainsKey($name)) { return $null }
    $clean = Remove-VmtComments $Text
    $m = [regex]::Match($clean, '(?i)^\s*"?(?:SDK_)?UnlitGeneric"?(?<rest>\s*\{)')
    if (-not $m.Success) { return $null }
    if ($clean -notmatch '(?i)"?\$vertex(alpha|color)"?\s+"?1') { return $null }
    if ($clean -match '(?i)"?\$(basetexturetransform|alphatest|detail|envmap|bumpmap)"?\s') { return $null }
    return $GeneratedMarker + " (soft) from $Path (delete this file to keep the original)`r`n" +
        '"Fidelity_SoftParticle"' + $m.Groups['rest'].Value + $clean.Substring($m.Index + $m.Length)
}

# Water: the game's water seen from above (shader Water / SDK_Water,
# $abovewater 1) gets Water_Fidelity - the same water with the sun's glint
# and foam at the shore; the name starts with "Water", so VBSP still gives it
# no lightmap. The Water_DX9_HDR block's values replace the ones above it
# (the engine applies such a block only to Valve's shader; HDR is what the
# mod runs with). The other fallback blocks (Water_DX81 ...) stay: they
# never count for this shader.
function New-WaterMaterial([string]$Path, [string]$Text) {
    $clean = Remove-VmtComments $Text
    $m = [regex]::Match($clean, '(?i)^\s*"?(?:SDK_)?Water"?(?<rest>\s*\{)')
    if (-not $m.Success) { return $null }
    if ($clean -notmatch '(?i)"?\$abovewater"?\s+"?1') { return $null }
    $body = $clean.Substring($m.Index + $m.Length)
    $hdr = [regex]::Match($body, '(?is)[ \t]*"?(?:SDK_)?Water_DX9_HDR"?\s*\{(?<params>(?:"[^"]*"|[^{}"])*)\}[ \t]*\r?\n?')
    $params = @()
    if ($hdr.Success) {
        $body = $body.Remove($hdr.Index, $hdr.Length)
        foreach ($p in [regex]::Matches($hdr.Groups['params'].Value, '"?(?<key>\$[A-Za-z0-9_]+)"?[ \t]+(?<value>"[^"\r\n]*"|[^\s"]+)')) {
            $key = $p.Groups['key'].Value
            # the value above the block goes (wherever it stood: a fallback block never counts here)
            $body = [regex]::Replace($body, '(?im)^[ \t]*"?' + [regex]::Escape($key) + '"?[ \t]+[^\r\n]*\r?\n?', '')
            $params += "`t`"$key`" $($p.Groups['value'].Value)"
        }
    }
    $lines = ''
    if ($params.Count -gt 0) { $lines = "`r`n" + ($params -join "`r`n") }
    return $GeneratedMarker + " (water) from $Path (delete this file to keep the original)`r`n" +
        '"Water_Fidelity"' + $m.Groups['rest'].Value + $lines + $body
}

# Material names a .pcf mentions (strings in the file, lower case, / and without .vmt)
function Get-PcfMaterialNames([byte[]]$Pcf) {
    $names = @{}
    foreach ($m in [regex]::Matches($Encoding.GetString($Pcf), '[A-Za-z0-9_/\\.\-]{4,}')) {
        $text = $m.Value.Replace('\', '/').ToLowerInvariant()
        if ($text.EndsWith('.vmt')) { $text = $text.Substring(0, $text.Length - 4) }
        if ($text.StartsWith('materials/')) { $text = $text.Substring(10) }
        $names[$text] = $true
    }
    return $names
}

# Characters whose glowing eyes ($selfillum) go dark when they die
$EyesFolders = '^materials/models/(combine_soldier|combine_super_soldier|combine_elite|police|hunter)(/|$)'

# A copy of a glowing character material with the FidelityDeadEyes proxy
# (client.dll: fades $selfillumtint to black on dead bodies). The tint must be
# in the file: the proxy is set up before the shader adds its defaults.
function New-EyesMaterial([string]$Path, [string]$Text) {
    if ($Path -notmatch $EyesFolders) { return $null }
    $clean = Remove-VmtComments $Text
    if ($clean -notmatch '(?i)^\s*"?(?:SDK_)?VertexLitGeneric"?\s*\{') { return $null }
    if ($clean -notmatch '(?i)"?\$selfillum"?\s+"?1') { return $null }
    if ($clean -match '(?i)FidelityDeadEyes') { return $null }
    $open = $clean.IndexOf('{')
    $close = $clean.LastIndexOf('}')
    if ($open -lt 0 -or $close -le $open) { return $null }
    $body = $clean.Substring($open + 1, $close - $open - 1).TrimEnd()
    if ($body -notmatch '(?i)\$selfillumtint') { $body = "`r`n`t`"`$selfillumtint`" `"[1 1 1]`"" + $body }
    $proxy = "`t`t`"FidelityDeadEyes`" { }"
    $m = [regex]::Match($body, '(?i)"?Proxies"?\s*\{')
    if ($m.Success) {
        $body = $body.Substring(0, $m.Index + $m.Length) + "`r`n" + $proxy + $body.Substring($m.Index + $m.Length)
    } else {
        $body += "`r`n`t`"Proxies`"`r`n`t{`r`n" + $proxy + "`r`n`t}"
    }
    return $GeneratedMarker + " (eyes) from $Path (delete this file to keep the original)`r`n" +
        $clean.Substring(0, $open + 1).Trim() + $body + "`r`n}`r`n"
}

# A copy of a model material with a bump map and no specular highlights,
# with a light Phong sheen: the relief of the bump map shows in the light.
# The sheen is masked by the bump map's alpha (Valve's default for Phong):
# where that is black, nothing changes. Not for see-through, glowing-only or
# skin materials, plants, bottles and the characters done by 'eyes'.
function New-SheenMaterial([string]$Path, [string]$Text) {
    if ($Path -notmatch '^materials/models/') { return $null }
    # a container's own liquid material (named after a shared atlas, not a bottle):
    # the sheen would overwrite it and the model copy lose its glass
    if ($script:ForcedKinds.ContainsKey($Path)) { return $null }
    if ($Path -match '(props_foliage|/player/|/humans/|/alyx|/barney|/kleiner|/breen|/gman|/eli|/mossman|/vortigaunt|/monk|/odessa)') { return $null }
    if ($Path -match $BottleNames -or $Path -match $EyesFolders) { return $null }
    $clean = Remove-VmtComments $Text
    $m = [regex]::Match($clean, '(?i)^\s*"?(?:SDK_)?VertexLitGeneric"?\s*\{')
    if (-not $m.Success) { return $null }
    if ($clean -notmatch '(?i)"?\$bumpmap"?\s') { return $null }
    if ($clean -match '(?i)"?\$(phong|translucent|additive|selfillum|nodiffusebumplighting|lightwarptexture)"?\s+"?1') { return $null }
    if ($clean -match '(?i)"?\$phong"?\s') { return $null }
    $params = @('"$phong" "1"', '"$phongboost" "0.6"', '"$phongexponent" "12"', '"$phongfresnelranges" "[0.3 0.65 1]"') |
        ForEach-Object { "`t$_" }
    $body = $clean.Substring($m.Index + $m.Length)
    return $GeneratedMarker + " (sheen) from $Path (delete this file to keep the original)`r`n" +
        $clean.Substring($m.Index, $m.Length).Trim() + "`r`n" + ($params -join "`r`n") + $body
}

# The generated materials in the mod (our first line), read once per run and
# kept up to date as they are written and removed: @{ full path in lower case
# = @{ Path; Kind ('liquid' / 'wind' / 'eyes' / 'sheen' / 'soft' / 'parallax') } }.
# (Each kind read every .vmt of the mod again - with thousands of generated
# ones a few seconds each time.)
$script:GeneratedIndex = $null
function Get-GeneratedIndex([string]$Game) {
    if ($null -ne $script:GeneratedIndex) { return $script:GeneratedIndex }
    $index = @{}
    $folder = Join-Path $Game 'materials'
    if ([System.IO.Directory]::Exists($folder)) {
        $kindOf = '^' + [regex]::Escape($GeneratedMarker) + ' \((\w+)\)'
        foreach ($file in [System.IO.Directory]::EnumerateFiles($folder, '*.vmt', [System.IO.SearchOption]::AllDirectories)) {
            $m = [regex]::Match((Get-FirstLine $file), $kindOf)
            if ($m.Success) { $index[[System.IO.Path]::GetFullPath($file).ToLowerInvariant()] = @{ Path = $file; Kind = $m.Groups[1].Value } }
        }
    }
    $script:GeneratedIndex = $index
    return $index
}

# Removes the generated materials (all, or of one kind) but those in $Keep (lower-case full paths)
function Remove-GeneratedMaterials([string]$Game, [string]$Kind = '', [hashtable]$Keep = @{}) {
    $index = Get-GeneratedIndex $Game
    foreach ($key in @($index.Keys)) {
        if (($Kind -and $index[$key].Kind -ne $Kind) -or $Keep.ContainsKey($key)) { continue }
        if ([System.IO.File]::Exists($index[$key].Path)) { [System.IO.File]::Delete($index[$key].Path) }
        $index.Remove($key)
    }
}

# ------------------------------------------------------ steeper bump maps ---
# Copies of the game's normal maps with steeper slopes (normal_boost.cs, see
# there). The manifest lists each copy with the strength and its SHA-256: a
# rerun with the same strength leaves them as they are, a copy changed by
# hand is never removed.
$BumpsManifest = 'fidelity_bumps.txt'
# Not for people and creatures: their faces and gear are made for their own
# highlights (Phong, lightwarp), steeper they glared - the Combine soldier's
# respirator shone yellow (Julius: "die Nase ... sehr gelblich am Leuchten")
$NotBoosted = '^materials/models/(player|humans|alyx|barney|kleiner|breen|gman|eli|mossman|vortigaunt|monk|odessa|combine_soldier|combine_super_soldier|combine_elite|police|hunter|zombie|zombie_classic|zombie_fast|zombie_poison|headcrab\w*|antlion\w*|stalker|synth|gunship|combine_strider|strider|dog|advisor|characters|survivors|citizens?)(/|$)'

# The normal maps of a material text ($bumpmap, $bumpmap2), as
# 'materials/....vtf'; none for self-shadowed bump maps ($ssbump)
function Get-BumpTextures([string]$Text) {
    $clean = Remove-VmtComments $Text
    if ($clean -match '(?i)"?\$ssbump"?\s+"?1') { return @() }
    $found = @()
    foreach ($m in [regex]::Matches($clean, '(?i)"?\$bumpmap2?"?\s+"?([^"\s]+)"?')) {
        $name = $m.Groups[1].Value.Replace('\', '/').ToLowerInvariant()
        if ($name.EndsWith('.vtf')) { $name = $name.Substring(0, $name.Length - 4) }
        if ($name -and $name -notmatch '^_rt_' -and $name -notmatch '\.\.') { $found += "materials/$name.vtf" }
    }
    return $found
}

# The copies listed in the manifest: @{ path = @{ Hash; Strength } }
function Read-BumpsManifest([string]$Game, [string]$Manifest = $BumpsManifest) {
    $list = @{}
    $manifest = Join-Path $Game $Manifest
    if (-not (Test-Path -LiteralPath $manifest)) { return $list }
    foreach ($line in (Read-Text $manifest) -split "`r?`n") {
        if (-not $line -or $line.StartsWith('//')) { continue }
        $parts = $line -split "`t"
        if ($parts.Count -ge 3) { $list[$parts[0]] = @{ Hash = $parts[1]; Strength = $parts[2] } }
    }
    return $list
}

# Removes the copies of the manifest that are not in $Keep (unchanged ones only)
function Remove-GeneratedBumps([string]$Game, [hashtable]$Keep, [string]$Manifest = $BumpsManifest) {
    $list = Read-BumpsManifest $Game $Manifest
    foreach ($key in $list.Keys) {
        if ($Keep.ContainsKey($key)) { continue }
        $file = Get-SafeTarget $Game $key
        if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        if ((Get-BytesHash ([System.IO.File]::ReadAllBytes($file))) -eq $list[$key].Hash) { Remove-Item -LiteralPath $file -Force }
        else { Write-Host "  kept     $key (changed since deploy.ps1 made it)" }
    }
    if ($Keep.Count -eq 0) {
        $manifest = Join-Path $Game $Manifest
        if (Test-Path -LiteralPath $manifest) { Remove-Item -LiteralPath $manifest -Force }
    }
}

function Remove-GeneratedHeights([string]$Game, [hashtable]$Keep) { Remove-GeneratedBumps $Game $Keep $HeightsManifest }

# ------------------------------------------------------------- parallax ---
# Walls and floors with real depth: Fidelity_Parallax with a height map made
# from the normal map (height_map.cs). The manifest lists the height maps
# (path, SHA-256), like the steeper bump maps.
$HeightsManifest = 'fidelity_heights.txt'
$HeightSuffix = '_fidh'
$NotParallax = '^materials/(models|skybox|tools|decals|overlays|effects|sprites|console|vgui|dev|editor|particle|water|glass|liquids|nature/water)(/|$)'

# The normal map of a wall material that can get parallax ('materials/....vtf'), or $null;
# $script:ParallaxWhy says why not (only for bump-mapped walls, else $null).
# Reflections ($envmap), detail textures (modes 0 and 1), self-shadowed bump
# maps ($ssbump), self-illumination ($selfillum) and blended surfaces
# (WorldVertexTransition, $basetexture2) are drawn by the shader as by
# LightmappedGeneric: most of the game's bump-mapped walls have one of them
# (Julius saw no parallax at all while they were left out).
function Get-ParallaxBump([string]$Path, [string]$Text) {
    $script:ParallaxWhy = $null
    if ($Path -match $NotParallax) { return $null }
    $clean = Remove-VmtComments $Text
    if (-not (Test-WallShader $clean)) { return $null }
    $bumps = @([regex]::Matches($clean, '(?i)"?\$bumpmap"?\s+"?([^"\s]+)"?') | ForEach-Object { $_.Groups[1].Value })
    if ($bumps.Count -eq 0) { return $null }
    $script:ParallaxWhy = Get-ParallaxBlocker $clean
    if ($script:ParallaxWhy) { return $null }
    $name = $bumps[0].Replace('\', '/').ToLowerInvariant()
    if ($name.EndsWith('.vtf')) { $name = $name.Substring(0, $name.Length - 4) }
    if ($bumps.Count -ne 1 -or -not $name -or $name -match '^_rt_' -or $name -match '\.\.') { $script:ParallaxWhy = 'bump map'; return $null }
    return "materials/$name.vtf"
}

# Is it a (SDK_)LightmappedGeneric material (text without comments)?
function Test-LightmappedGeneric([string]$Clean) {
    $shader = [regex]::Match($Clean, '^\s*"?([A-Za-z0-9_]+)"?\s*\{')
    return $shader.Success -and $shader.Groups[1].Value -match '^(?i)(SDK_)?LightmappedGeneric$'
}

# Is it a wall shader the parallax shader stands in for: (SDK_)LightmappedGeneric
# or (SDK_)WorldVertexTransition (blended displacements)?
function Test-WallShader([string]$Clean) {
    $shader = [regex]::Match($Clean, '^\s*"?([A-Za-z0-9_]+)"?\s*\{')
    return $shader.Success -and $shader.Groups[1].Value -match '^(?i)(SDK_)?(LightmappedGeneric|WorldVertexTransition)$'
}

# Why a wall material can not get parallax (see-through, three-way texture
# mapping, a fixed half blend, a detail mode the shader does not draw), or $null
function Get-ParallaxBlocker([string]$Clean) {
    $flag = [regex]::Match($Clean, '(?i)"?\$(alphatest|translucent|additive|decal|nodiffusebumplighting)"?\s+"?1')
    if ($flag.Success) { return $flag.Groups[1].Value.ToLowerInvariant() }
    if ($Clean -match '(?i)"?\$seamless_scale"?\s' -or $Clean -match '(?i)"?\$maskedblending"?\s+"?1') { return 'blending' }
    if ($Clean -match '(?i)"?\$detailblendmode"?\s+"?([2-9]|[1-9][0-9])') { return 'detail mode' }
    return $null
}

# A texture name from a material parameter ('materials/....vtf'), or $null
function Get-MaterialTexture([string]$Clean, [string]$Param) {
    $found = @([regex]::Matches($Clean, '(?i)"?\$' + $Param + '"?\s+"?([^"\s]+)"?') | ForEach-Object { $_.Groups[1].Value })
    if ($found.Count -ne 1) { return $null }
    $name = $found[0].Replace('\', '/').ToLowerInvariant()
    if ($name.EndsWith('.vtf')) { $name = $name.Substring(0, $name.Length - 4) }
    if (-not $name -or $name -match '^_rt_' -or $name -match '\.\.') { return $null }
    return "materials/$name.vtf"
}

# The second layer of a blended surface ($basetexture2): where its height map
# comes from - $bumpmap2, or $basetexture2 when its name tells of relief - as
# @{ Texture; Kind }; @{ Texture = $null } when the
# layer stays flat; $null without a second layer
function Get-ParallaxSource2([string]$Path, [string]$Text) {
    $clean = Remove-VmtComments $Text
    $base2 = Get-MaterialTexture $clean 'basetexture2'
    if (-not $base2) { return $null }
    if (-not $ParallaxWood -and $base2 -match $MinimalNames) { return @{ Texture = $null } }
    $bump2 = Get-MaterialTexture $clean 'bumpmap2'
    if ($bump2) { return @{ Texture = $bump2; Kind = $(if (Test-SSBump $Text) { 'ssbump' } else { 'normal' }) } }
    # (by the second texture's own name: the material's names the first layer, or both)
    if ($base2 -notmatch $ReliefNames) { return @{ Texture = $null } }
    return @{ Texture = $base2; Kind = $(if ($base2 -match $CourseNames) { 'courses' } elseif ($base2 -match $MinimalNames) { 'planks' } else { 'color' }) }
}

# The relief depth of one layer ($Name: the material's path, or the texture
# of a second layer)
function Get-ParallaxDepth([string]$Name, $Source) {
    switch ($Source.Kind) {
        'courses' { return $ReliefDepth * $CourseDepth * $(if ($Source.Texture -match $FacadeNames) { $FacadeDepth } else { 1.0 }) }
        'decal' { return $DecalDepth * $(if ($script:DecalScale.ContainsKey($Source.Texture)) { $script:DecalScale[$Source.Texture] } else { 1.0 }) }
        default {
            if ($Name -match $MinimalNames) { return $ReliefDepth * $MinimalDepth }
            if ($Source.Kind -eq 'normal' -or $Source.Kind -eq 'ssbump') { return $ReliefDepth * $BumpDepth }
            return $ReliefDepth
        }
    }
}

# Walls without a bump map whose folder or name tells of relief: their
# height comes from the base texture (most of a map's bricks and stones have
# no bump map - Julius' map had a single bump-mapped wall)
$ReliefNames = '(brick|stone|cobble|rock|tile|block|cinder|paver|slate|plank|roof|shingle|wood|board)'
# Wood only a little: dark grooves between boards (Julius: "minimale Parallax, aber wirklich minimal");
# and only with -ParallaxWood: in the game the grooves' walls caught the
# boards' texture stretched and the grooves seemed to stand out (Julius,
# 25.10.2026: "sieht weird aus") - by default wood keeps its own look (bump map)
$MinimalNames = '(wood|plank|board)'
$MinimalDepth = 0.7		# share of -ParallaxDepth (with or without a bump map): 0.007, as before -ParallaxDepth went from 0.02 to 0.01
# Heights from a normal map: the deepest groove of the normal map becomes the
# full depth - at 0.02 the joints of a tiled floor stood like walls and threw a
# black strip along every tile (Julius, 29.10.2026: about 0.006 looked right)
$BumpDepth = 0.6		# share of -ParallaxDepth
# Wood without a bump map: boards with their gaps low (the brightness made
# the dark gaps stand out - Julius: "das Schwarze wird raus statt das Holz")
$PlankHeightVersion = 'planks1'
# Version of the height maps from base textures (in the manifest): older ones are made again
# (color2: stones shaped from their joints, no longer every grain a bump)
$ColorHeightVersion = 'color2'
# Bricks, tiles and blocks are laid in courses: their height maps come from
# the repeating joints (clean rectangles; by brightness a stained brick wall
# got bumps - Julius: "an meisten Stellen passt es absolut nicht")
$CourseNames = '(brick|tile|block|cinder|paver)'
$CourseHeightVersion = 'courses7'	# courses7: façades measured out (brickwall014b: cornice, dentils, sill, panel, window, pilasters - height_map.cs Facades); courses6: joints found where they are (lighter and greyer lines), no grid over the whole texture - a façade got upright grooves; courses5: joints only where bricks show (façades), thin joints only, bevel from the bricks (courses4: streaks taken off, bond offsets; courses3: upright grooves; courses2: half bricks; courses1: cushions)
$CourseDepth = 0.6		# share of -ParallaxDepth (0.006): mortar sits only a little deeper than the bricks
# The façades height_map.cs measured out (its Facades - keep both lists the
# same): their bricks take only 0.15 .. 0.7 of the height (cornice, sill and
# pilasters stand out above them), so the relief is deeper, the bricks about
# as deep as on other walls
$FacadeNames = '(?i)brick/brickwall014b(\.vtf)?$'
$FacadeDepth = 1.4

# Bullet holes: world decals (LightmappedGeneric, $decal) named shot / bullet /
# hole, not glass - drawn with depth, the height from the decal itself
$BulletHoles = '^materials/decals/(?!.*glass).*(shot|bullet|hole)[^/]*\.vmt$'
$DecalHeightVersion = 'decal2'
$DecalDepth = 0.2		# share of the decal's size (0.12: Julius wanted them "einen Ticken tiefer")

# The game draws most bullet holes from a decal atlas: "Subrect" materials
# ($Material - some write $Parent -, $Pos, $Size) cut them out of one big decal material - usually
# DecalModulate (multiplied onto the wall). The atlas gets the parallax
# shader, its height map holes only inside the bullet holes' rectangles
# (Julius: no hole had depth - the single shot materials were never used).
$script:AtlasRects = @{}	# 'materials/<parent>.vmt' = x, y, width, height, ...
$script:DecalScale = @{}	# decal texture = share of it one hole takes (for the depth)

# A Subrect material's parent ('materials/....vmt') and rectangle, or $null
function Get-Subrect([string]$Text) {
    $clean = Remove-VmtComments $Text
    if ($clean -notmatch '(?i)^\s*"?subrect"?\s*\{') { return $null }
    $parent = [regex]::Match($clean, '(?i)"?\$(material|parent)"?\s+"?([^"\r\n]+?)"?\s*(\r?\n|$)')
    $pos = [regex]::Match($clean, '(?i)"?\$pos"?\s+"\s*(\d+)\s+(\d+)\s*"')
    $size = [regex]::Match($clean, '(?i)"?\$size"?\s+"\s*(\d+)\s+(\d+)\s*"')
    if (-not $parent.Success -or -not $pos.Success -or -not $size.Success) { return $null }
    $name = $parent.Groups[2].Value.Trim().Replace('\', '/').ToLowerInvariant()
    if ($name.EndsWith('.vmt')) { $name = $name.Substring(0, $name.Length - 4) }
    if (-not $name -or $name -match '\.\.') { return $null }
    return @{ Parent = "materials/$name.vmt"; Rect = @([int]$pos.Groups[1].Value, [int]$pos.Groups[2].Value, [int]$size.Groups[1].Value, [int]$size.Groups[2].Value) }
}

# Is it a decal the parallax shader can draw: LightmappedGeneric with $decal,
# or DecalModulate (then 'modulate')? $null otherwise
function Get-DecalShader([string]$Clean) {
    if ($Clean -match '(?i)^\s*"?(SDK_)?DecalModulate"?\s*\{') { return 'modulate' }
    if ((Test-LightmappedGeneric $Clean) -and $Clean -match '(?i)"?\$decal"?\s+"?1') { return 'blend' }
    return $null
}

# Where a bullet hole's height map comes from (its base texture), or $null
function Get-BulletHoleSource([string]$Text, [object]$Rects = $null) {
    $script:ParallaxWhy = $null
    $clean = Remove-VmtComments $Text
    $shader = Get-DecalShader $clean
    if (-not $shader) { return $null }
    if ($clean -match '(?i)"?\$(selfillum|additive|basetexture2)"?\s') { return $null }
    $bases = @([regex]::Matches($clean, '(?i)"?\$basetexture"?\s+"?([^"\s]+)"?') | ForEach-Object { $_.Groups[1].Value })
    if ($bases.Count -ne 1) { return $null }
    $name = $bases[0].Replace('\', '/').ToLowerInvariant()
    if ($name.EndsWith('.vtf')) { $name = $name.Substring(0, $name.Length - 4) }
    if (-not $name -or $name -match '^_rt_' -or $name -match '\.\.') { return $null }
    return @{ Texture = "materials/$name.vtf"; Kind = 'decal'; Modulate = ($shader -eq 'modulate'); Rects = $Rects }
}

# Things on or in a wall that get no relief, by the material's and its base texture's names -
# the same rules as Mapsmith's (relief_pattern.cpp, reliefFitByName; keep both alike): windows
# and glass (also with bricks in the name: a "brickwall_window" is a window), signs, posters,
# grates, fences, doors, screens, lamps, pipes, cloth; metal sheets and plaster unless the name
# tells of bricks or stones. Julius (29.10.2026): a window between brick pillars got relief.
$NoReliefObjects = '(window|glass|windshield|mirror|stainedglass|skylight|storefront|shopfront|poster|billboard|advert|banner|graffiti|grating|chainlink|railing|fence|shutter|garagedoor|monitor|computer|keyboard|curtain|wallpaper|carpet|fabric)'
$NoReliefParts = '(^|[^a-z])(label|logo|sticker|grate|grill|mesh|vent|door|gate|screen|lamp|lights/|pipe|wire|cable|cloth|paper|blind)'
$StonyNames = '(brick|stone|cobble|rock|tile|block|cinder|paver|slate|shingle)'

# Why a wall gets no relief by its names (see above), or $null
function Get-NoReliefReason([string]$Path, [string]$Text) {
    $base = Get-MaterialTexture (Remove-VmtComments $Text) 'basetexture'
    $name = ($Path.Substring(10) + ' ' + $(if ($base) { $base.Substring(10) } else { '' })).ToLowerInvariant()
    if ($name -match $NoReliefObjects) { return 'window, sign or grate' }
    if ($name -match '(?<!de)sign') { return 'window, sign or grate' }
    if ($name -match $NoReliefParts) { return 'window, sign or grate' }
    if ($name -match $StonyNames) { return $null }
    if ($name -match '(metal|steel|corrugated|aluminium|aluminum)' -or $name -match '(^|[^a-z])(sheet|plate|iron)') { return 'metal or plaster' }
    if ($name -match '(plaster|stucco|drywall)' -or $name -match '(^|[^a-z])(paint|rubber|plastic)') { return 'metal or plaster' }
    return $null
}

# Where a wall material's height map comes from: @{ Texture = 'materials/....vtf';
# Kind = 'normal' / 'ssbump' (its bump map) or 'color' / 'courses' / 'planks' (its base texture) }, or $null
function Get-ParallaxSource([string]$Path, [string]$Text) {
    if ($script:AtlasRects.ContainsKey($Path)) {
        # Only an atlas multiplied onto the wall (DecalModulate, the game's decals_mod2x of the
        # common bullet holes): the lit one (decals_lit, LightmappedGeneric) holds the big
        # holes but also the scorch marks, splashes, paint and cracks - with the parallax
        # shader the grenades' scorch marks came out grey (Julius 25.10.2026: "zu weiss")
        $atlas = Get-BulletHoleSource $Text $script:AtlasRects[$Path]
        if ($atlas -and -not $atlas.Modulate) { return $null }
        return $atlas
    }
    if ($Path -match $BulletHoles) { return Get-BulletHoleSource $Text }
    $bump = Get-ParallaxBump $Path $Text
    if ($bump -and (Test-Wood $Path $Text)) { $script:ParallaxWhy = 'wood'; return $null }
    if ($bump) {
        $why = Get-NoReliefReason $Path $Text
        if ($why) { $script:ParallaxWhy = $why; return $null }
    }
    if ($bump) { return @{ Texture = $bump; Kind = $(if (Test-SSBump $Text) { 'ssbump' } else { 'normal' }) } }
    if ($script:ParallaxWhy -or $Path -match $NotParallax -or $Path -notmatch $ReliefNames) { return $null }
    if (Test-Wood $Path $Text) { return $null }
    if (Get-NoReliefReason $Path $Text) { return $null }
    $clean = Remove-VmtComments $Text
    if (-not (Test-WallShader $clean) -or $clean -match '(?i)"?\$bumpmap"?\s') { return $null }
    if (Get-ParallaxBlocker $clean) { return $null }
    $bases = @([regex]::Matches($clean, '(?i)"?\$basetexture"?\s+"?([^"\s]+)"?') | ForEach-Object { $_.Groups[1].Value })
    if ($bases.Count -ne 1) { return $null }
    $name = $bases[0].Replace('\', '/').ToLowerInvariant()
    if ($name.EndsWith('.vtf')) { $name = $name.Substring(0, $name.Length - 4) }
    if (-not $name -or $name -match '^_rt_' -or $name -match '\.\.') { return $null }
    return @{ Texture = "materials/$name.vtf"; Kind = $(if ($Path -match $CourseNames) { 'courses' } elseif ($Path -match $MinimalNames) { 'planks' } else { 'color' }) }
}

# Wood that stays without relief (no -ParallaxWood): by its (first) base
# texture's name - a blend's material name tells of both layers - else by the path
function Test-Wood([string]$Path, [string]$Text) {
    if ($ParallaxWood) { return $false }
    $base = Get-MaterialTexture (Remove-VmtComments $Text) 'basetexture'
    return $(if ($base) { $base } else { $Path }) -match $MinimalNames
}

# Is the material's bump map self-shadowed ($ssbump)?
function Test-SSBump([string]$Text) {
    return (Remove-VmtComments $Text) -match '(?i)"?\$ssbump"?\s+"?1'
}

# The copy of a wall material with the parallax shader, or $null (no height map made for it)
function New-ParallaxMaterial([string]$Path, [string]$Text) {
    $source = Get-ParallaxSource $Path $Text
    if (-not $source -or -not $script:Heights.ContainsKey($source.Texture)) { return $null }
    $bump = $source.Texture
    $clean = Remove-VmtComments $Text
    if ($source.Kind -eq 'decal') {
        # a decal's own bump map goes: the normal comes from the height map, and
        # the decal keeps the plain lightmap it has on every wall
        $clean = [regex]::Replace($clean, '(?im)^[ \t]*"?\$(bumpmap|bumpframe|bumptransform|ssbump|normalmapalphaenvmapmask)"?[ \t]+[^\r\n]*\r?\n?', '')
    }
    $m = [regex]::Match($clean, '^\s*"?[A-Za-z0-9_]+"?(?<rest>\s*\{)')
    $height = $bump.Substring(10, $bump.Length - 14) + $HeightSuffix	# without materials/ and .vtf
    $depthValue = Get-ParallaxDepth $Path $source
    $params = @("`t`"`$parallaxmap`" `"$height`"")
    # Blended (WorldVertexTransition): the second layer's height map, when it
    # has one; the shader's name keeps WorldVertexTransition in it (VBSP gives
    # such a material on a brush a plain copy only then)
    $shader = 'Fidelity_Parallax'
    $source2 = Get-ParallaxSource2 $Path $Text
    if ($source2 -and $source.Kind -ne 'decal') {
        $shader = 'Fidelity_WorldVertexTransition'
        if ($source2.Texture -and $script:Heights.ContainsKey($source2.Texture)) {
            $params += "`t`"`$parallaxmap2`" `"$($source2.Texture.Substring(10, $source2.Texture.Length - 14) + $HeightSuffix)`""
            # one depth for both: the shallower layer's (wood stays minimal)
            $depthValue = [Math]::Min($depthValue, (Get-ParallaxDepth $source2.Texture $source2))
        }
    }
    $depth = $depthValue.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)
    $params += "`t`"`$parallaxdepth`" `"$depth`""
    if ($source.Modulate) { $params += "`t`"`$parallaxmodulate`" `"1`"" }	# multiplied onto the wall, as DecalModulate
    # Without a reflection of its own: the map's cubemap, shown only where the wall is wet from rain
    # (puddles mirror the place; dry it looks as before). VBSP puts the nearest env_cubemap in when
    # the map is compiled.
    if ($source.Kind -ne 'decal' -and $clean -notmatch '(?i)"?\$envmap"?\s') {
        $params += "`t`"`$envmap`" `"env_cubemap`""
        $params += "`t`"`$envmapwetonly`" `"1`""
    }
    return $GeneratedMarker + " (parallax) from $Path (delete this file to keep the original)`r`n" +
        "`"$shader`"" + $m.Groups['rest'].Value + "`r`n" + ($params -join "`r`n") + $clean.Substring($m.Index + $m.Length)
}

# The pictures and textures of -ParallaxPreview (only .bmp and .vtf files there are ours; the folder goes when empty)
function Remove-Previews([string]$Game) {
    $folder = Join-Path $Game 'fidelity_preview'
    if (-not (Test-Path -LiteralPath $folder)) { return }
    Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.Extension -in '.bmp', '.vtf' } | Remove-Item -Force
    if (@(Get-ChildItem -LiteralPath $folder -Force).Count -eq 0) { Remove-Item -LiteralPath $folder -Force }
}

# ---------------------------------------------------------------- locate ---
# (a path ending in \" arrives with the quote: "D:\...\mymod\" in a .bat)
if ($GamePath) { $GamePath = $GamePath.Trim().Trim('"').TrimEnd('\', '/') }
if (-not $GamePath) { $GamePath = Find-ModFolder }
if (-not $GamePath) {
    $GamePath = ([string](Read-Host "Mod folder (steamapps\sourcemods\<mod>, the one with gameinfo.txt)")).Trim().Trim('"')
}
if (-not $GamePath) { throw "No mod folder given." }
if (-not (Test-Path -LiteralPath (Join-Path $GamePath 'gameinfo.txt'))) {
    throw "'$GamePath' has no gameinfo.txt. Pass the mod folder as -GamePath."
}
$Game = (Resolve-Path -LiteralPath $GamePath).Path
$AutoExec = Join-Path $Game 'cfg/autoexec.cfg'
Write-Host "mapbase-fidelity game files -> $Game"

# ---------------------------------------------------------------- remove ---
if ($Remove) {
    if (Test-Path -LiteralPath $AutoExec) {
        $text = Read-Text $AutoExec
        $lines = $text -split "`r?`n"
        $kept = @($lines | Where-Object { -not $_.EndsWith($LineMarker) })
        if ($kept.Count -ne $lines.Count) {
            $rest = $kept -join (Get-Newline $text)
            if ($rest.Trim().Length -eq 0) {
                # only our line was in it (deploy made the file)
                Remove-Item -LiteralPath $AutoExec -Force
                Write-Host "  removed  cfg\autoexec.cfg (only our line was in it)"
            } else {
                Write-Text $AutoExec $rest
                Write-Host "  cleaned  cfg\autoexec.cfg"
            }
        }
    }
    Remove-Previews $Game
    Get-ChildItem -LiteralPath (Join-Path $Game 'shaders/fxc') -Filter 'fidelity_*.vcs' -ErrorAction SilentlyContinue | Remove-Item -Force
    $cfg = Join-Path $Game 'cfg/fidelity.cfg'
    if (Test-Path -LiteralPath $cfg) { Remove-Item -LiteralPath $cfg -Force }
    Remove-GeneratedMaterials $Game
    Remove-GeneratedModels $Game
    Remove-GeneratedBumps $Game @{}
    Remove-GeneratedHeights $Game @{}
    Remove-DistantSounds $Game @{}
    $lights = Join-Path $Game $LightsFile
    if (Test-OwnLightsFile $lights) { Remove-Item -LiteralPath $lights -Force }
    foreach ($name in @($ShaderDll, $OldShaderDll)) {
        $dll = Join-Path $Game "bin/$name"
        if (Test-Path -LiteralPath $dll) { Remove-Item -LiteralPath $dll -Force }
    }
    Write-Host "Done. The fidelity game files are gone."
    exit 0
}

# ------------------------------------------------------------------ copy ---
$fxcTarget = Join-Path $Game 'shaders/fxc'
New-Item -ItemType Directory -Force -Path $fxcTarget | Out-Null
Get-ChildItem -LiteralPath (Join-Path $Root 'game/shaders/fxc') -Filter '*.vcs' | Copy-Item -Destination $fxcTarget -Force
Write-Host "  copied   shaders -> shaders\fxc"

$cfgTarget = Join-Path $Game 'cfg'
New-Item -ItemType Directory -Force -Path $cfgTarget | Out-Null
Copy-Item -LiteralPath (Join-Path $Root 'game/cfg/fidelity.cfg') $cfgTarget -Force
Write-Host "  copied   cfg\fidelity.cfg"

$autoText = ''
if (Test-Path -LiteralPath $AutoExec) { $autoText = Read-Text $AutoExec }
if ($autoText -match '(?m)^\s*exec\s+"?fidelity') {
    Write-Host "  ok       autoexec.cfg already runs fidelity.cfg"
} else {
    $nl = "`r`n"
    if ($autoText.Length -gt 0) { $nl = Get-Newline $autoText }
    $prefix = ''
    if ($autoText.Length -gt 0 -and -not $autoText.EndsWith("`n")) { $prefix = $nl }
    Write-Text $AutoExec ($autoText + $prefix + "exec fidelity`t$LineMarker" + $nl)
    Write-Host "  patched  cfg\autoexec.cfg"
}

# ------------------------------------------------------------ shader DLL ---
# Built next to client.dll, or still in the project's output folder
# (FIDELITY_BIN_DIR replaces the search, for the tests)
$candidates = @(
    (Join-Path $Root "../sp/game/mod_episodic/bin/$ShaderDll"),
    (Join-Path $Root "../sp/game/mod_hl2/bin/$ShaderDll")
)
$projectFolder = Join-Path $Root '../sp/src/materialsystem/fidelity'
if (Test-Path -LiteralPath $projectFolder) {
    $candidates += @(Get-ChildItem -LiteralPath $projectFolder -Recurse -Filter $ShaderDll | ForEach-Object { $_.FullName })
}
if ($env:FIDELITY_BIN_DIR) { $candidates = @(Join-Path $env:FIDELITY_BIN_DIR $ShaderDll) }
$dllSource = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Sort-Object { (Get-Item -LiteralPath $_).LastWriteTime } -Descending | Select-Object -First 1
$haveShaderDll = $true
if ($dllSource) {
    $binTarget = Join-Path $Game 'bin'
    New-Item -ItemType Directory -Force -Path $binTarget | Out-Null
    Copy-Item -LiteralPath $dllSource $binTarget -Force
    Write-Host "  copied   bin\$ShaderDll"
} elseif (Test-Path -LiteralPath (Join-Path $Game "bin/$ShaderDll")) {
    Write-Host "  ok       bin\$ShaderDll is in the mod (copied there by the build)"
} else {
    $haveShaderDll = $false
    Write-Host "  missing  $ShaderDll - run XBLAH Configure once, then build the whole solution games.sln"
    Write-Host "           in Visual Studio (it holds the project 'Shaders (Fidelity)') and run deploy.bat again"
}
$oldDll = Join-Path $Game "bin/$OldShaderDll"
if (Test-Path -LiteralPath $oldDll) {
    Remove-Item -LiteralPath $oldDll -Force
    Write-Host "  removed  bin\$OldShaderDll (old name, the engine never loaded it)"
}

# Without the game's archives (Steam library not found, moved, offline
# drive) everything made from them would count as gone and be removed:
# it stays as it is instead
if (@(Get-VpkFolders $Game).Count -eq 0) {
    Write-Host "  warning  no Half-Life 2 / Source SDK Base 2013 Singleplayer found next to the mod (steamapps\common):"
    Write-Host "           glass bottles, wind, sheen, baked light, bump maps, parallax walls and distant sounds stay as they are"
    Write-Host "Done. Start the mod; the console shows lines starting with [fidelity] if something is wrong."
    exit 0
}

# ------------------------------------------- bottles, wind and dead eyes ---
$materials = @{}
foreach ($folder in Get-VpkFolders $Game) {
    # Materials are not in the sound and texture archives (skipping them saves time).
    foreach ($vpk in Get-Vpks $folder) {
        Write-Host "  scanning $($vpk.Name)"
        $found = Read-VpkModelMaterials $vpk.FullName
        foreach ($key in $found.Keys) { if (-not $materials.ContainsKey($key)) { $materials[$key] = $found[$key] } }
    }
}

# Writes the generated materials of one kind ('liquid' / 'wind' / 'eyes') and removes
# generated ones of that kind this run no longer makes (a material that no
# longer qualifies, a Steam library that is gone); returns how many it wrote.
# Files the mod has itself (without our first line) are never touched.
function Write-Generated([string]$Kind, [scriptblock]$Make, [hashtable]$From = $null) {
    if ($null -eq $From) { $From = $materials }
    $index = Get-GeneratedIndex $Game
    $written = @{}
    foreach ($key in ($From.Keys | Sort-Object)) {
        $vmt = & $Make $key $From[$key]
        if (-not $vmt) { continue }
        $target = Get-SafeTarget $Game $key
        if (-not $target) { continue }
        $lower = $target.ToLowerInvariant()
        if ([System.IO.File]::Exists($target)) {
            if (-not $index.ContainsKey($lower)) { continue }	# the mod's own
            # unchanged since the last run: left as it is (writing thousands of
            # files every run took most of the time)
            if ([System.IO.File]::ReadAllText($target, $Encoding) -ceq $vmt) {
                $index[$lower].Kind = $Kind
                $written[$lower] = $true
                continue
            }
        } else {
            [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($target))
        }
        Write-Text $target $vmt
        $index[$lower] = @{ Path = $target; Kind = $Kind }
        $written[$lower] = $true
    }
    Remove-GeneratedMaterials $Game $Kind $written
    return $written.Count
}

Remove-GeneratedModels $Game	# made again below if still wanted
if ($NoBottles) {
    Remove-GeneratedMaterials $Game 'liquid'
    Write-Host "  skipped  glass bottles (-NoBottles)"
} elseif (-not $haveShaderDll) {
    # Without the shader the engine cannot draw these materials: the props would vanish.
    Remove-GeneratedMaterials $Game 'liquid'
    Write-Host "  skipped  glass bottles - they need $ShaderDll (see 'missing' above)"
} else {
    $containerModels = Get-ContainerModels @(Get-VpkFolders $Game) $materials
    # How dark each container's texture is (dark glass gets the tinted look)
    $needed = @{}
    foreach ($key in $materials.Keys) {
        if ($key -notmatch $BottleNames) { continue }
        $texture = Get-BaseTexture (Remove-VmtComments $materials[$key])
        if ($texture) { $needed["materials/$texture.vtf".ToLowerInvariant()] = $true }
    }
    if ($needed.Count -gt 0) { $script:BottleTextureColors = Get-VtfColors $Game @(Get-VpkFolders $Game) $needed }
    $count = Write-Generated 'liquid' ${function:New-LiquidMaterial}

    # The model copies, where their own material was written (never over the
    # mod's own models). The manifest is written first: if the run stops
    # halfway, the next one still knows the copies are ours.
    $copies = @()
    foreach ($key in ($containerModels.Keys | Sort-Object)) {
        $target = Get-SafeTarget $Game $key
        if (-not $target) { continue }
        if (Test-Path -LiteralPath $target) { $script:BottleReport += "  kept     $key (the mod has its own)"; continue }
        # Only if every own material is there (else the copy would show the error texture)
        $missing = @($containerModels[$key].Own | Where-Object {
            $file = Get-SafeTarget $Game $_
            -not $file -or -not ((Get-FirstLine $file) -like "$GeneratedMarker (liquid)*") })
        if ($missing.Count -gt 0) { $script:BottleReport += "  kept     $key (its material is no VertexLitGeneric one)"; continue }
        $copies += @{ Key = $key; Target = $target }
    }
    if ($copies.Count -gt 0) {
        $lines = @($GeneratedMarker + ' (models) - copies of container models with their own liquid material: path, SHA-256')
        foreach ($copy in $copies) { $lines += "$($copy.Key)`t$(Get-BytesHash $containerModels[$copy.Key].Bytes)" }
        Write-Text (Join-Path $Game $ModelsManifest) (($lines -join "`r`n") + "`r`n")
    }
    foreach ($copy in $copies) {
        New-Item -ItemType Directory -Force -Path (Split-Path $copy.Target -Parent) | Out-Null
        [System.IO.File]::WriteAllBytes($copy.Target, $containerModels[$copy.Key].Bytes)
        $script:BottleReport += "  model    $($copy.Key) -> own material $($containerModels[$copy.Key].Own -join ', ')"
    }
    $script:BottleReport | ForEach-Object { Write-Host $_ }
    Write-Host "  bottles  $count glass container materials use the liquid shader, $($copies.Count) models got their own"
}
if ($NoWind) {
    Remove-GeneratedMaterials $Game 'wind'
    Write-Host "  skipped  foliage wind (-NoWind)"
} else {
    $count = Write-Generated 'wind' ${function:New-WindMaterial}
    Write-Host "  wind     $count foliage materials sway in the wind"
}
$count = Write-Generated 'eyes' ${function:New-EyesMaterial}
Write-Host "  eyes     $count character materials: glowing eyes go dark on death"
if ($NoSoftParticles) {
    Remove-GeneratedMaterials $Game 'soft'
    Write-Host "  skipped  soft particles (-NoSoftParticles)"
} elseif (-not $haveShaderDll) {
    # Without the shader the engine cannot draw these materials: smoke and dust would vanish.
    Remove-GeneratedMaterials $Game 'soft'
    Write-Host "  skipped  soft particles - they need $ShaderDll (see 'missing' above)"
} else {
    $particles = @{}
    foreach ($folder in Get-VpkFolders $Game) {
        foreach ($vpk in Get-Vpks $folder) {
            $found = Read-VpkFiles $vpk.FullName -Ext 'vmt' -Prefix 'materials/particle'
            foreach ($key in $found.Keys) { if (-not $particles.ContainsKey($key)) { $particles[$key] = $Encoding.GetString($found[$key]) } }
            $null = Read-VpkFiles $vpk.FullName -Ext 'pcf' -Prefix 'particles' -Each {
                param($key, $data)
                foreach ($name in (Get-PcfMaterialNames $data).Keys) { $script:PcfMaterials[$name] = $true }
            }
        }
    }
    # the mod's own .pcf files too (its particles_manifest may name them)
    foreach ($pcf in @(Get-ChildItem -LiteralPath (Join-Path $Game 'particles') -Filter '*.pcf' -File -ErrorAction SilentlyContinue)) {
        foreach ($name in (Get-PcfMaterialNames ([System.IO.File]::ReadAllBytes($pcf.FullName))).Keys) { $script:PcfMaterials[$name] = $true }
    }
    $count = Write-Generated 'soft' ${function:New-SoftParticleMaterial} $particles
    Write-Host "  soft     $count particle materials fade where they meet walls"
}
if ($NoWater) {
    Remove-GeneratedMaterials $Game 'water'
    Write-Host "  skipped  water (-NoWater)"
} elseif (-not $haveShaderDll) {
    # Without the shader the engine cannot draw these materials: the water would vanish.
    Remove-GeneratedMaterials $Game 'water'
    Write-Host "  skipped  water - it needs $ShaderDll (see 'missing' above)"
} else {
    $water = @{}
    $wantWater = { param($ext, $dir, $name) ($ext -eq 'vmt') -and $dir.StartsWith('materials/') -and -not $dir.StartsWith('materials/models') -and -not $dir.StartsWith('materials/skybox') }
    $keepWater = {
        param($key, $bytes)
        if ($water.ContainsKey($key)) { return }	# the first one found counts (as the game loads it)
        $text = $Encoding.GetString($bytes)
        if ($text -match '(?i)^\s*(?:\xEF\xBB\xBF|\uFEFF)?\s*"?(?:SDK_)?Water"?\s*\{') { $water[$key] = $text } else { $water[$key] = '' }
    }
    foreach ($folder in Get-VpkFolders $Game) {
        foreach ($vpk in Get-Vpks $folder) { Read-VpkFiles $vpk.FullName $wantWater -Each $keepWater -Ext 'vmt' -Prefix 'materials' | Out-Null }
    }
    $count = Write-Generated 'water' ${function:New-WaterMaterial} $water
    Write-Host "  water    $count water materials glint in the sun and foam at the shore"
}
if ($NoSheen) {
    Remove-GeneratedMaterials $Game 'sheen'
    Write-Host "  skipped  sheen on bump-mapped models (-NoSheen)"
} else {
    $count = Write-Generated 'sheen' ${function:New-SheenMaterial}
    Write-Host "  sheen    $count bump-mapped model materials got a light sheen"
}

# ------------------------------------------------------------ baked light ---
$lightsTarget = Join-Path $Game $LightsFile
$lightsOurs = -not (Test-Path -LiteralPath $lightsTarget) -or (Test-OwnLightsFile $lightsTarget)
if ($NoLights) {
    if ((Test-Path -LiteralPath $lightsTarget) -and $lightsOurs) { Remove-Item -LiteralPath $lightsTarget -Force }
    Write-Host "  skipped  baked light (-NoLights)"
} elseif (-not $lightsOurs) {
    Write-Host "  kept     $LightsFile (not made by deploy.ps1)"
} else {
    $vpkFolders = @(Get-VpkFolders $Game)
    $glowing = @{}		# material name -> base texture
    $foliage = @{}		# model path
    $wantWall = {
        param($ext, $dir, $name)
        ($ext -eq 'vmt') -and $dir.StartsWith('materials/') -and -not $dir.StartsWith('materials/models') -and -not $dir.StartsWith('materials/skybox')
    }
    $seenWalls = @{}
    foreach ($folder in $vpkFolders) {
        foreach ($vpk in Get-Vpks $folder) {
            $walls = Read-VpkFiles $vpk.FullName $wantWall -Ext 'vmt' -Prefix 'materials'
            foreach ($key in $walls.Keys) {
                $texture = Get-GlowingWallTexture ($Encoding.GetString($walls[$key]))
                $name = $key.Substring(10, $key.Length - 14).ToLowerInvariant()
                if ($texture -and -not $seenWalls.ContainsKey($name)) { $glowing[$name] = $texture }
                $seenWalls[$name] = $true	# the first one found counts (as the game loads it)
            }
            foreach ($key in (Read-VpkFiles $vpk.FullName $null -NamesOnly -Ext 'mdl' -Prefix 'models/props_foliage').Keys) { $foliage[$key] = $true }
        }
    }
    # The mod's own files count too (and win)
    $modMaterials = Join-Path $Game 'materials'
    if (Test-Path -LiteralPath $modMaterials) {
        foreach ($file in Get-ChildItem -LiteralPath $modMaterials -Recurse -Filter '*.vmt') {
            $relative = $file.FullName.Substring($modMaterials.Length + 1).Replace('\', '/')
            if ($relative.StartsWith('models/') -or $relative.StartsWith('skybox/')) { continue }
            $texture = Get-GlowingWallTexture (Read-Text $file.FullName)
            if ($texture) { $glowing[$relative.Substring(0, $relative.Length - 4).ToLowerInvariant()] = $texture }
        }
    }
    $modFoliage = Join-Path $Game 'models/props_foliage'
    if (Test-Path -LiteralPath $modFoliage) {
        foreach ($file in Get-ChildItem -LiteralPath $modFoliage -Recurse -Filter '*.mdl') {
            $foliage['models/props_foliage/' + $file.FullName.Substring($modFoliage.Length + 1).Replace('\', '/')] = $true
        }
    }

    # Textures the game's lights.rad already lights stay as they are
    $known = Get-KnownTexlights (Get-LightsRadFolders $Game $vpkFolders)
    foreach ($name in @($glowing.Keys)) { if ($known.ContainsKey($name)) { $glowing.Remove($name) } }

    # Colours from the .vtf headers (the mod's own first, then the game's archives)
    $needed = @{}
    foreach ($texture in $glowing.Values) { $needed["materials/$texture.vtf"] = $true }
    $colors = Get-VtfColors $Game $vpkFolders $needed

    # No comments (see $LightsMarker): glowing wall textures ($selfillum) not in the
    # game's lights.rad as "texture R G B brightness", then leaf shadows for foliage.
    # Delete the first line to keep own changes; deploy.ps1 then leaves the file alone.
    $lines = @($LightsMarker)
    foreach ($name in ($glowing.Keys | Sort-Object)) {
        $lines += "$name $(Get-TexlightColor $colors["materials/$($glowing[$name]).vtf"]) $TexlightBrightness"
    }
    foreach ($model in ($foliage.Keys | Sort-Object)) { $lines += "forcetextureshadow $model" }
    Write-Text $lightsTarget (($lines -join "`r`n") + "`r`n")
    Write-Host "  lights   $($glowing.Count) glowing wall textures, $($foliage.Count) foliage models -> $LightsFile"
    Write-Host "           VRAD from update.bat reads it by itself (Mapbase's own VRAD: -lights $LightsFile -textureshadows)"
}

# ------------------------------------------------------ steeper bump maps ---
$strengthText = $BumpFactor.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)
# In the manifest: the strength and the version of normal_boost.cs (copies of
# an older version are made again; 2: even DXT1 blocks are made steeper too)
$bumpVersion = "$strengthText boost2"
$BumpsHeader = $GeneratedMarker + ' (bumps) - steeper copies of the game''s normal maps: path, SHA-256, strength'
if ($BumpFactor -le 1.001) {
    Remove-GeneratedBumps $Game @{}
    Write-Host "  skipped  steeper bump maps (-BumpStrength $strengthText)"
} else {
    # The normal maps of all the game's materials
    $bumps = @{}
    $wantMaterial = { param($ext, $dir, $name) ($ext -eq 'vmt') -and $dir.StartsWith('materials/') -and -not $dir.StartsWith('materials/skybox') -and "$dir/" -notmatch $NotBoosted }
    $collect = { param($key, $bytes) foreach ($texture in Get-BumpTextures ($Encoding.GetString($bytes))) { $bumps[$texture] = $true } }
    $vpkFolders = @(Get-VpkFolders $Game)
    foreach ($folder in $vpkFolders) {
        foreach ($vpk in Get-Vpks $folder) { Read-VpkFiles $vpk.FullName $wantMaterial -Each $collect -Ext 'vmt' -Prefix 'materials' | Out-Null }
    }
    if ($bumps.Count -gt 0 -and -not ('FidelityNormalBoost' -as [type])) { Add-Type -Path (Join-Path $Root 'normal_boost.cs') }
    # Made one at a time (the first archive that has it wins); copies of this
    # strength that are still there stay
    $old = Read-BumpsManifest $Game
    $made = @{}
    $script:BumpBytes = [int64]0
    $script:BumpSkipped = 0
    $script:BumpNew = 0
    $wantTexture = { param($ext, $dir, $name) ($ext -eq 'vtf') -and $bumps.ContainsKey("$dir/$name.vtf") -and -not $made.ContainsKey("$dir/$name.vtf") }
    $boost = {
        param($key, $bytes)
        if ($made.ContainsKey($key)) { return }
        $target = Get-SafeTarget $Game $key
        if (-not $target) { return }
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            $hash = Get-BytesHash ([System.IO.File]::ReadAllBytes($target))
            if (-not $old.ContainsKey($key) -or $old[$key].Hash -ne $hash) { return }	# the mod's own, or changed by hand
            if ($old[$key].Strength -eq $bumpVersion) { $made[$key] = $old[$key]; return }
        }
        $reason = $null
        $out = [FidelityNormalBoost]::Boost($bytes, [float]$BumpFactor, [ref]$reason)
        if (-not $out) { $script:BumpSkipped++; return }
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        [System.IO.File]::WriteAllBytes($target, $out)
        $script:BumpBytes += $out.Length
        $script:BumpNew++
        $made[$key] = @{ Hash = (Get-BytesHash $out); Strength = $bumpVersion }
        Add-ManifestLine $Game $BumpsManifest $BumpsHeader $key $made[$key].Hash $bumpVersion
    }
    foreach ($folder in $vpkFolders) {
        foreach ($vpk in Get-Vpks $folder -WithTextures) { Read-VpkFiles $vpk.FullName $wantTexture -Each $boost -Ext 'vtf' -Prefix 'materials' | Out-Null }
    }
    Remove-GeneratedBumps $Game $made
    if ($made.Count -gt 0) {
        $lines = @($BumpsHeader)
        foreach ($key in ($made.Keys | Sort-Object)) { $lines += "$key`t$($made[$key].Hash)`t$($made[$key].Strength)" }
        Write-Text (Join-Path $Game $BumpsManifest) (($lines -join "`r`n") + "`r`n")
    }
    Write-Host ("  bumps    {0} normal maps {1}x steeper ({2} new, {3:N0} MB), {4} left as they are (format)" -f
        $made.Count, $strengthText, $script:BumpNew, ($script:BumpBytes / 1MB), $script:BumpSkipped)
}

# ------------------------------------------------------------- parallax ---
$script:Heights = @{}
$HeightsHeader = $GeneratedMarker + ' (heights) - height maps for the parallax walls: path, SHA-256, version'
if ($NoParallax -or -not $haveShaderDll) {
    Remove-GeneratedMaterials $Game 'parallax'
    Remove-GeneratedHeights $Game @{}
    Remove-Previews $Game
    if ($NoParallax) { Write-Host "  skipped  parallax walls (-NoParallax)" }
    else { Write-Host "  skipped  parallax walls - they need $ShaderDll (see 'missing' above)" }
} else {
    # The game's wall materials and the normal maps the parallax ones need
    $walls = @{}
    $leftOut = @{}	# why bump-mapped walls get no parallax: reason = count
    $wantWall = { param($ext, $dir, $name) ($ext -eq 'vmt') -and $dir.StartsWith('materials/') -and ("$dir/" -notmatch $NotParallax -or $dir.StartsWith('materials/decals')) }
    $seenWall = @{}
    $decalTexts = @{}
    $keepWall = {
        param($key, $bytes)
        if ($seenWall.ContainsKey($key)) { return }	# the first one found counts (as the game loads it)
        $seenWall[$key] = $true
        $text = $Encoding.GetString($bytes)
        if ($key.StartsWith('materials/decals/')) {
            $decalTexts[$key] = $text
            if ($key -match $BulletHoles) {
                $subrect = Get-Subrect $text
                if ($subrect) {
                    if (-not $script:AtlasRects.ContainsKey($subrect.Parent)) { $script:AtlasRects[$subrect.Parent] = @() }
                    $script:AtlasRects[$subrect.Parent] += $subrect.Rect
                    return
                }
            }
            if ($key -notmatch $BulletHoles) { return }
        }
        if (Get-ParallaxSource $key $text) { $walls[$key] = $text }
        elseif ($script:ParallaxWhy) { $leftOut[$script:ParallaxWhy] = 1 + [int]$leftOut[$script:ParallaxWhy] }
    }
    $vpkFolders = @(Get-VpkFolders $Game)
    foreach ($folder in $vpkFolders) {
        foreach ($vpk in Get-Vpks $folder) { Read-VpkFiles $vpk.FullName $wantWall -Each $keepWall -Ext 'vmt' -Prefix 'materials' | Out-Null }
    }
    # the atlases the bullet holes are cut from (found in the same scan; the
    # subrects decide, so they are looked at once all are known)
    foreach ($parent in @($script:AtlasRects.Keys)) {
        if ($decalTexts.ContainsKey($parent) -and (Get-ParallaxSource $parent $decalTexts[$parent])) { $walls[$parent] = $decalTexts[$parent] }
    }
    $needed = @{}	# texture = 'normal' / 'ssbump' / 'color' / 'courses' / 'planks' / 'decal'
    $neededRects = @{}	# decal atlas texture = its bullet holes' rectangles
    # (in a fixed order, the first material of a texture decides its kind:
    # hashtable order changed from run to run, and so did the height map)
    foreach ($key in ($walls.Keys | Sort-Object)) {
        $source = Get-ParallaxSource $key $walls[$key]
        if (-not $needed.ContainsKey($source.Texture)) { $needed[$source.Texture] = $source.Kind }
        $source2 = Get-ParallaxSource2 $key $walls[$key]
        if ($source2 -and $source2.Texture -and -not $needed.ContainsKey($source2.Texture)) { $needed[$source2.Texture] = $source2.Kind }
        if ($source.Rects) { $neededRects[$source.Texture] = [int[]]$source.Rects }
    }
    if ($needed.Count -gt 0 -and -not ('FidelityHeightMap' -as [type])) { Add-Type -Path (Join-Path $Root 'height_map.cs') }

    # Height maps, one at a time; ones still there from an earlier run stay
    $oldHeights = Read-BumpsManifest $Game $HeightsManifest
    $madeHeights = @{}
    $script:HeightNew = 0
    $script:HeightBytes = [int64]0
    $wantNormal = { param($ext, $dir, $name) ($ext -eq 'vtf') -and $needed.ContainsKey("$dir/$name.vtf") -and -not $script:Heights.ContainsKey("$dir/$name.vtf") }
    $makeHeight = {
        param($key, $bytes)
        if ($script:Heights.ContainsKey($key)) { return }
        $heightKey = $key.Substring(0, $key.Length - 4) + $HeightSuffix + '.vtf'
        $target = Get-SafeTarget $Game $heightKey
        if (-not $target) { return }
        $version = switch ($needed[$key]) { 'color' { $ColorHeightVersion } 'courses' { $CourseHeightVersion } 'planks' { $PlankHeightVersion } 'decal' { $DecalHeightVersion } default { '1' } }
        if ($neededRects.ContainsKey($key)) {
            # an atlas: the version names its holes (other holes, a new height map); the depth follows a hole's size
            $rects = $neededRects[$key]
            $version += '-' + (Get-BytesHash ([System.Text.Encoding]::ASCII.GetBytes(($rects -join ',')))).Substring(0, 8)
            $atlasWidth = [Math]::Max([BitConverter]::ToUInt16($bytes, 16), 1)
            $sum = 0
            for ($r = 0; $r + 3 -lt $rects.Count; $r += 4) { $sum += $rects[$r + 2] }
            $script:DecalScale[$key] = [Math]::Min(1.0, $sum / [Math]::Max(1, [int]($rects.Count / 4)) / $atlasWidth)
        }
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            $hash = Get-BytesHash ([System.IO.File]::ReadAllBytes($target))
            if (-not $oldHeights.ContainsKey($heightKey) -or $oldHeights[$heightKey].Hash -ne $hash) { return }	# the mod's own
            if ($oldHeights[$heightKey].Strength -eq $version) {
                $madeHeights[$heightKey] = $oldHeights[$heightKey]
                $script:Heights[$key] = $true
                return
            }
            # ours, made the old way: made again below
        }
        $reason = $null
        if ($needed[$key] -eq 'decal' -and $neededRects.ContainsKey($key)) { $out = [FidelityHeightMap]::MakeFromDecal($bytes, $neededRects[$key], [ref]$reason) }
        elseif ($needed[$key] -eq 'decal') { $out = [FidelityHeightMap]::MakeFromDecal($bytes, [ref]$reason) }
        elseif ($needed[$key] -eq 'courses') { $out = [FidelityHeightMap]::MakeFromCourses($bytes, $key, [ref]$reason) }
        elseif ($needed[$key] -eq 'planks') { $out = [FidelityHeightMap]::MakeFromPlanks($bytes, [ref]$reason) }
        elseif ($needed[$key] -eq 'color') { $out = [FidelityHeightMap]::MakeFromColor($bytes, [ref]$reason) }
        else { $out = [FidelityHeightMap]::Make($bytes, ($needed[$key] -eq 'ssbump'), [ref]$reason) }
        if (-not $out) { $leftOut["height map: $reason"] = 1 + [int]$leftOut["height map: $reason"]; return }
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        [System.IO.File]::WriteAllBytes($target, $out)
        $script:HeightNew++
        $script:HeightBytes += $out.Length
        $madeHeights[$heightKey] = @{ Hash = (Get-BytesHash $out); Strength = $version }
        Add-ManifestLine $Game $HeightsManifest $HeightsHeader $heightKey $madeHeights[$heightKey].Hash $version
        $script:Heights[$key] = $true
    }
    foreach ($folder in $vpkFolders) {
        foreach ($vpk in Get-Vpks $folder -WithTextures) { Read-VpkFiles $vpk.FullName $wantNormal -Each $makeHeight -Ext 'vtf' -Prefix 'materials' | Out-Null }
    }
    Remove-GeneratedHeights $Game $madeHeights

    # Pictures of the height maps from base textures, to look at (-ParallaxPreview brick)
    if ($ParallaxPreview) {
        $previewFolder = Join-Path $Game 'fidelity_preview'
        $wantPreview = { param($ext, $dir, $name) ($ext -eq 'vtf') -and $needed.ContainsKey("$dir/$name.vtf") -and @('color', 'courses', 'planks') -contains $needed["$dir/$name.vtf"] -and "$dir/$name.vtf" -like "*$ParallaxPreview*" }
        $script:Previews = 0
        $seenPreview = @{}
        $writePreview = {
            param($key, $bytes)
            if ($seenPreview.ContainsKey($key)) { return }
            $seenPreview[$key] = $true
            $reason = $null
            $picture = [FidelityHeightMap]::PreviewFromColor($bytes, $needed[$key], $key, [ref]$reason)
            if (-not $picture) { return }
            New-Item -ItemType Directory -Force -Path $previewFolder | Out-Null
            $file = Join-Path $previewFolder $key.Substring(10, $key.Length - 14).Replace('/', '_')
            [System.IO.File]::WriteAllBytes($file + '.bmp', $picture)
            # and the texture itself: report.bat packs these to send (the heights are worked out from them)
            [System.IO.File]::WriteAllBytes($file + '.vtf', $bytes)
            $script:Previews++
        }
        foreach ($folder in $vpkFolders) {
            foreach ($vpk in Get-Vpks $folder -WithTextures) { Read-VpkFiles $vpk.FullName $wantPreview -Each $writePreview -Ext 'vtf' -Prefix 'materials' | Out-Null }
        }
        Write-Host ("  preview  {0} pictures (texture | height) -> {1}" -f $script:Previews, $previewFolder)
    }
    if ($madeHeights.Count -gt 0) {
        $lines = @($HeightsHeader)
        foreach ($key in ($madeHeights.Keys | Sort-Object)) { $lines += "$key`t$($madeHeights[$key].Hash)`t$($madeHeights[$key].Strength)" }
        Write-Text (Join-Path $Game $HeightsManifest) (($lines -join "`r`n") + "`r`n")
    }
    $count = Write-Generated 'parallax' ${function:New-ParallaxMaterial} $walls
    $fromColor = @($madeHeights.Values | Where-Object { $_.Strength -like 'color*' -or $_.Strength -like 'courses*' -or $_.Strength -like 'planks*' }).Count
    $holes = @($madeHeights.Values | Where-Object { $_.Strength -like 'decal*' }).Count
    Write-Host ("  parallax {0} wall and floor materials with depth ({1} height maps, {4} of them from the base texture, {5} bullet holes, {2} new, {3:N0} MB)" -f
        $count, $madeHeights.Count, $script:HeightNew, ($script:HeightBytes / 1MB), $fromColor, $holes)
    foreach ($parent in ($script:AtlasRects.Keys | Sort-Object)) {
        $state = $(if ($walls.ContainsKey($parent)) { 'with depth' } elseif ($decalTexts.ContainsKey($parent)) { 'left as it is (shader)' } else { 'not found' })
        Write-Host ("           bullet hole atlas {0}: {1} holes, {2}" -f $parent, [int]($script:AtlasRects[$parent].Count / 4), $state)
    }
    if ($script:AtlasRects.Count -eq 0) { Write-Host "           no bullet hole atlas found (Subrect materials named shot / bullet / hole)" }
    if ($leftOut.Count -gt 0) {
        $why = @($leftOut.Keys | Sort-Object | ForEach-Object { "$_ $($leftOut[$_])" }) -join ', '
        Write-Host "           bump-mapped walls left out: $why"
    }
}

# ---------------------------------------------------------- distant sounds ---
if ($NoDistantSounds) {
    Remove-DistantSounds $Game @{}
    Write-Host "  skipped  distant sounds (-NoDistantSounds)"
} else {
    $oldDistant = Read-BumpsManifest $Game $DistantManifest
    $madeDistant = @{}
    $wantSource = @{}
    foreach ($target in $DistantSources.Keys) {
        $file = Get-SafeTarget $Game $target
        if (-not $file) { continue }
        if (Test-Path -LiteralPath $file -PathType Leaf) {
            $hash = Get-BytesHash ([System.IO.File]::ReadAllBytes($file))
            if (-not $oldDistant.ContainsKey($target) -or $oldDistant[$target].Hash -ne $hash) { continue }	# the mod's own
            if ($oldDistant[$target].Strength -eq $DistantVersion) { $madeDistant[$target] = $oldDistant[$target]; continue }
        }
        $wantSource[$DistantSources[$target]] = $target
    }
    if ($wantSource.Count -gt 0) {
        if (-not ('FidelitySoundDistant' -as [type])) { Add-Type -Path (Join-Path $Root 'sound_distant.cs') }
        $wantWav = { param($ext, $dir, $name) ($ext -eq 'wav') -and $wantSource.ContainsKey("sound/$dir/$name.wav".Replace('sound/sound/', 'sound/')) }
        $makeDistant = {
            param($key, $bytes)
            $source = $(if ($key.StartsWith('sound/')) { $key } else { "sound/$key" })
            if (-not $wantSource.ContainsKey($source)) { return }
            $target = $wantSource[$source]
            if ($madeDistant.ContainsKey($target)) { return }
            $reason = $null
            $out = [FidelitySoundDistant]::Make($bytes, [ref]$reason)
            if (-not $out) { Write-Host "  skipped  $source ($reason)"; return }
            $file = Get-SafeTarget $Game $target
            New-Item -ItemType Directory -Force -Path (Split-Path $file -Parent) | Out-Null
            [System.IO.File]::WriteAllBytes($file, $out)
            $madeDistant[$target] = @{ Hash = (Get-BytesHash $out); Strength = $DistantVersion }
            Add-ManifestLine $Game $DistantManifest $DistantHeader $target $madeDistant[$target].Hash $DistantVersion
        }
        foreach ($folder in @(Get-VpkFolders $Game)) {
            foreach ($vpk in Get-SoundVpks $folder) { Read-VpkFiles $vpk.FullName $wantWav -Each $makeDistant -Ext 'wav' | Out-Null }
        }
    }
    Remove-DistantSounds $Game $madeDistant
    Write-Host ("  distant  {0} far-away gunshot and explosion sounds -> sound\fidelity\distant" -f $madeDistant.Count)
}

Write-Host "Done. Start the mod; the console shows lines starting with [fidelity] if something is wrong."
