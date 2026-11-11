param([string]$Rom, [string]$BuildDir='build_release', [string]$EngineRoot, [string]$RecompUi)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) { $EngineRoot=Join-Path $root 'nesrecomp' }
if (-not $Rom) {
    $candidate=Join-Path $root 'Mega Man (USA).nes'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Rom=$candidate }
}
if (-not $Rom) { throw 'Supply -Rom with the original USA ROM path (EU requires an explicit path).' }
& (Join-Path $EngineRoot 'tools/build_cycle_windows.ps1') -ProjectRoot $root -Rom $Rom -BuildDir $BuildDir -EngineRoot $EngineRoot -RecompUi $RecompUi
