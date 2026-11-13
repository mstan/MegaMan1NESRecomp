param([string]$Rom, [string]$BuildDir='build_release', [string]$EngineRoot, [string]$RecompUi,
      [string[]]$CMakeArgs=@(), [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) { $EngineRoot=Join-Path $root 'nesrecomp' }
if (-not $Rom) {
    $candidate=Join-Path $root 'Mega Man (USA).nes'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Rom=$candidate }
}
$notes=@'
Fit to screen, L/R weapon switching, Reduce slowdown and No sprite flicker
start enabled for a fresh profile. Existing saved choices take precedence.
Keyboard defaults are arrows to move, Z jump, X fire, Q/E weapons, Enter Start
and Backslash Select. Controls can be remapped in the system menu.

Enable Proto Man in Mods to swap characters with Select. Its normal setting
includes the airborne front shield, two Buster shots and double damage taken.
The optional normal-stat reskin keeps Mega Man's behavior.
Solo swaps retain separate health for each character. Health pickups heal
the active character, and a native death/retry restores both health pools.

Enable Co-op in Mods and assign each player's controls in Controls. Start
opens your native inventory; only the pausing player can resume. Hold Select
to withdraw while your partner remains, then release and press it to rejoin
beside a safely grounded partner. Health, weapons and ammo remain personal.
Shared, split and single-player views are available in Mods.

For online play, enable Co-op before creating a room. Shared and independent
gameplay policies stay fixed for that match. Within an independent match,
each player can choose split screen or either player's view locally.

Charge shots and sliding are optional Mods shared by both characters. They
require your original Mega Man 4 (USA) ROM. Hold B with the Buster to charge;
press Down + A to slide. No donor ROM or extracted donor data is included.
Each online player selects their own donor image when these abilities are on.

Save states belong to this build. Preserve your existing saves and settings
when replacing an older installation.
'@
& (Join-Path $EngineRoot 'tools/package_cycle_windows.ps1') -ProjectRoot $root -Target 'MegaManRecomp' -Title 'Mega Man' -BuildDir $BuildDir -Rom $Rom -EngineRoot $EngineRoot -RecompUi $RecompUi -CMakeArgs $CMakeArgs -SkipBuild:$SkipBuild -GameNotes $notes
