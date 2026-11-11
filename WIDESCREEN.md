# Experimental adaptive widescreen

This opt-in experiment reveals horizontal stage terrain, extends ordinary enemy
activation/culling, repairs wrapped sprite copies and anchors the health meters
at the left edge. It remains disabled by default.

**Known limits:** combat outside the original 4:3 bounds is incomplete, and scene
eligibility can fall back to 4:3 during progression. This is an experimental
preview, not a completed widescreen conversion.

## Supported ROM

The hooks target the **Europe payload CRC32 94476A70 (PAL)**.
The normal **USA CRC32 6EE4BB0A (NTSC)**
build remains supported without this mod. Build configuration selects the actual
payload and enrolls the hooks, host additions and package only for Europe.
Filenames and iNES region flags do not determine the payload identity.

## Build and enable

Initialize the pinned engine/UI submodules as described in README.md, then:

```powershell
cmake -S . -B build-wide -DNESRECOMP_ROM='path/to/Mega Man (Europe).nes'
cmake --build build-wide --config Release --parallel 2
```

Open the normal launcher and enable **Adaptive Widescreen (Experimental)** in
Mods. Choose Fit, 16:9, 21:9 or 32:9. Fit follows the window size.

Viewport enemy activation loads ordinary enemies throughout the wider view.
Classic retains original activation/culling. The native 16-slot major-enemy
pool remains, so wider views can delay activation and alter difficulty. Special
generator and room-control records retain their native behavior.

For a separate disposable playtest profile:

```powershell
.\play-widescreen.ps1 -Rom 'path/to/Mega Man (Europe).nes'
.\play-widescreen.ps1 -Rom 'path/to/Mega Man (Europe).nes' -Aspect '16:9' -Enemies classic
```

The CLI equivalents are --widescreen fit|16:9|21:9|32:9|off and
--widescreen-enemies viewport|classic.

## Verification and remaining work

The existing regression commands exercise disabled/compiled/interpreted/classic
hash parity, catalog default-off and activation, terrain alignment, viewport
activation/culling, live Fit resizing, sprite/HUD rendering and current-build
save/load:

```powershell
python tests/widescreen/validate.py --rom 'path/to/Mega Man (Europe).nes'
python tests/widescreen/validate_window.py --rom 'path/to/Mega Man (Europe).nes'
python tests/widescreen/validate_rendering.py --rom 'path/to/Mega Man (Europe).nes'
```

Generated code, ROMs, binaries, captures and saves remain outside version control.
Tests use hidden native processes and retain their evidence locally.
The rendering checks require Pillow.

These checks do not qualify margin combat, stable full-stage eligibility,
vertical/room transitions, bosses/Wily, mutable Guts terrain/shutters, platforms,
projectiles/generators, deaths/checkpoints or full-game progression.

## Implementation

ROM-verified hooks collect signed sprite packets and project native ordinary
enemy initialization through an isolated call. Native CPU/mapper/timing state
is restored; only permitted enemy fields leave that call. The host compositor
repairs wrapped native sprite copies and draws the margins. Presentation
metadata participates in current-build saves.

Layout interpretation uses [TheRealQuantam's Mega Man disassembly](https://github.com/TheRealQuantam/mm1dasm)
and [Bisqwit's Mega Man source](https://bisqwit.iki.fi/jutut/megamansource/);
neither reference is asserted byte-identical to the supported payload.
