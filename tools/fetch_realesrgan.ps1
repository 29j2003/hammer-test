# fetch_realesrgan.ps1 - Puts the AI upscaler of "Enhance textures" (Real-ESRGAN ncnn Vulkan,
# xinntao/Real-ESRGAN release v0.2.5.0) into a Mapsmith folder: <Out>/tools/realesrgan with the
# program, its OpenMP runtime and the one model Mapsmith uses (realesrgan-x4plus). The download is
# checked against a fixed SHA-256 before anything is unpacked. Used by ci.yml and release.yml.
#
#   pwsh tools/fetch_realesrgan.ps1 -Out dist/Mapsmith-0.2.0-windows

param(
	[Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'
$url = 'https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip'
$sha256 = 'abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d'

$work = Join-Path ([System.IO.Path]::GetTempPath()) ('realesrgan-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
try {
	$zip = Join-Path $work 'realesrgan.zip'
	Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
	$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
	if ($hash -ne $sha256) { throw "Real-ESRGAN download has SHA-256 $hash, expected $sha256" }
	$unpacked = Join-Path $work 'unpacked'
	Expand-Archive -Path $zip -DestinationPath $unpacked
	$target = Join-Path $Out 'tools/realesrgan'
	New-Item -ItemType Directory -Force (Join-Path $target 'models') | Out-Null
	# Only what Mapsmith runs: not the demo pictures, not the debug runtime (vcomp140d.dll may not be
	# passed on), not the anime models.
	Copy-Item (Join-Path $unpacked 'realesrgan-ncnn-vulkan.exe') $target
	Copy-Item (Join-Path $unpacked 'vcomp140.dll') $target
	foreach ($file in 'realesrgan-x4plus.bin', 'realesrgan-x4plus.param') {
		Copy-Item (Join-Path $unpacked "models/$file") (Join-Path $target 'models')
	}
	Write-Host "Real-ESRGAN in $target"
} finally {
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
