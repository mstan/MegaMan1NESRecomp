param([string]$Rom, [string]$BuildDir='build_release', [string]$EngineRoot, [string]$RecompUi, [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) { $EngineRoot=Join-Path $root 'nesrecomp' }
if (-not $Rom) {
    $candidate=Join-Path $root 'Mega Man (USA).nes'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Rom=$candidate }
}
& (Join-Path $EngineRoot 'tools/package_cycle_windows.ps1') -ProjectRoot $root -Target 'MegaManRecomp' -Title 'Mega Man' -BuildDir $BuildDir -Rom $Rom -EngineRoot $EngineRoot -RecompUi $RecompUi -SkipBuild:$SkipBuild -GameNotes ''
