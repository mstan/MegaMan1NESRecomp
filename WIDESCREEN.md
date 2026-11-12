# Adaptive widescreen

The USA build enables Fit to screen for a fresh profile. Fit adapts the
horizontal view to the window; 16:9, 21:9, 32:9 and the original aspect are also
available. Existing saved choices take precedence over package defaults.

The conversion remains experimental. Targeted USA checks cover Cut Man terrain,
ordinary enemy activation, native sprite placement and current-build replay.
Other stages, special generators and complete scripted progression still need
qualification.

## Supported ROMs

The USA hooks target payload CRC32 **6EE4BB0A (NTSC)**. The earlier Europe
prototype targets **94476A70 (PAL)** and retains its separate hooks and opt-in
default. Build configuration selects hooks by the actual payload identity.

Initialize the pinned engine/UI submodules as described in README.md, then:

```powershell
cmake -S . -B build-wide -DNESRECOMP_ROM='path/to/Mega Man (USA).nes'
cmake --build build-wide --config Release --parallel 2
```

The normal launcher's Mods catalog exposes the aspect and enemy activation
options. The CLI equivalents are `--widescreen fit|16:9|21:9|32:9|off` and
`--widescreen-enemies viewport|classic`.

Viewport activation loads ordinary enemies throughout the wider view. Classic
retains the original activation/culling. The original 16-slot major-enemy pool
remains, so wider views can delay activation and alter difficulty. Special
generator and room-control records retain their original activation.

## Development co-op views

The USA development `--coop on` profile supports
`--coop-view shared|split|p1|p2`. Split uses two native 256x240 grids; P1 and P2
show the corresponding single grid. Each player has a personal native HUD and
inventory. Shared view gives the pausing player the usable inventory;
independent views give each player their own usable inventory. Only the pausing
player can resume. The paused backdrop contains stage background without actors.

Independent simulation uses the union of both player views, rather than the
rectangle spanning the gap between them. Ordinary authored enemies use their
original constructors and ID reservations; a full pool retries after a slot is
free. Personal native sprite captures use each actor's actual world position,
so a shot can leave one view and appear in the other. Both characters retain
their own weapon graphics, colors, slide and charged shots. Native team-death
scripts display their original particles and HUD policy in both grids.
Original native overlap tests run in the distant player's local coordinates,
so contacts, personal refills, shared pickups and weapon hits work beyond the
primary camera as well.

Targeted Cut Man checks cover full native grid images for both characters,
both complete inventory panels, peer-only ordinary spawning, gap exclusion,
full-pool retry, cross-view shots and distant survivor handoff. Split, P1 and P2
share canonical simulation hashes. Current-build state, immediate presentation,
audio continuation and native/interpreter agreement are also checked.

## Verification

Run the targeted USA view and inventory checks against a built executable:

```powershell
python tests/coop/views.py --rom 'path/to/Mega Man (USA).nes' --exe build-wide/Release/MegaManRecomp.exe --out qa/views
python tests/coop/views.py --rom 'path/to/Mega Man (USA).nes' --exe build-wide/Release/MegaManRecomp.exe --out qa/native-grids --native-oracle
python tests/coop/views.py --rom 'path/to/Mega Man (USA).nes' --exe build-wide/Release/MegaManRecomp.exe --out qa/pool --pool-retry
python tests/coop/menu.py --rom 'path/to/Mega Man (USA).nes' --exe build-wide/Release/MegaManRecomp.exe --out qa/menus --view split
```

The original Europe prototype checks remain in `tests/widescreen/`. Their
default-off expectations apply to that profile. Generated code, ROMs, binaries,
captures and states stay outside version control. Automated runtime tests use
hidden processes with dummy audio/video and isolated profiles.

## Implementation

ROM-verified hooks collect signed sprite packets and invoke original ordinary
enemy constructors in isolation. Only the new actor's permitted fields leave
that call. The host compositor repairs wrapped sprite copies and draws stage
terrain, private player parts and native HUD/menu captures. Presentation
metadata participates in current-build saves and follows actual OAM DMA.

Layout interpretation uses [TheRealQuantam's Mega Man disassembly](https://github.com/TheRealQuantam/mm1dasm)
and [Bisqwit's Mega Man source](https://bisqwit.iki.fi/jutut/megamansource/).
Hook addresses and behavior are verified independently against the supported
ROM; neither reference is asserted byte-identical to it.
