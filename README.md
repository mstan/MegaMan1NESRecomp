# MegaMan1NESRecomp

The USA ROM supports adaptive Fit to screen and L/R weapon switching through
the preloaded Mods catalog. Fit is enabled for a fresh profile. L/R follows the
native inventory order, including empty weapons, and ignores unsafe transitions.
Existing saved choices take precedence over the package defaults.
Quick switching restores the original weapon's pattern graphics. Co-op players
retain their own weapon patterns while the main player's graphics are displayed.

The [widescreen implementation](WIDESCREEN.md) remains experimental. USA checks
cover Cut Man terrain, wider enemy activation, native/interpreter agreement and
current-build state replay. Other stages and scripted transitions still need
qualification. The Europe prototype retains its separate hooks and
opt-in default.

The optional USA Proto Man package adds Select teleport swapping, an airborne
front shield with reflected native projectiles, two uncharged shots and double
incoming damage. Its normal-stat reskin option retains Mega Man's behavior.
Current checks cover native/interpreter agreement, Fit sprite placement,
current-build state replay and native enemy/boss collision paths.
The native inventory keeps its panel geometry and life count with the selected
character's 16x16 portrait.
Airborne firing uses the corrected shared cannon baseline and matching native
Buster origin. Solo and second-player checks cover both directions, exact
presented pixels, unchanged movement/audio and current-build continuation.

The optional slide and charged Buster read graphics, timing and sounds from your
Mega Man 4 (USA) ROM at launch. Both characters share these abilities. Down + A
starts a low slide using the original game's terrain, collision and camera.
Half shots use normal Buster damage; full shots triple it and continue through
lethal enemy hits.
The donor image and its artwork are never written into the package. Charging,
active-shot state/audio replay, slide movement/ceilings/contact and native
damage paths have targeted checks. Multiplayer parity remains under development.

The development `--coop on` slice separates personal movement, equipment state
and native actors while the original frame loop advances the world once.
`--coop-p1 megaman|protoman` chooses the complementary character roster.
Each character uses its own native parts, colors, slide and charged shots.
Targeted checks cover movement, native enemy/boss damage and defeat, personal
health/weapon refills and shared extra lives. Both sprite priority orders also
work while the main character blinks. Current-build charge/shot and native NMI
continuations replay exactly, including audio, with Fit margins retained.
Both characters appear during native arrival. A personal death plays its native
particles while the survivor keeps control, equipment and charge. When both die,
the original death script takes one shared life and restarts both players with
their own weapon energy. Either controller can continue from native Game Over.
Death handoffs retain the displayed character and its weapon colors through
lagged frames and current-build save/load.
Hold Select for 90 gameplay ticks to withdraw while a partner remains active;
press it again to rejoin beside a safely grounded partner. Rejoining retains
health, weapon selection and ammo. Reaching a new native checkpoint revives a
dead partner with full health, and a team retry uses that checkpoint.
Targeted Cut Man checks cover these paths, low ceilings, simultaneous Select,
and a survivor dying while the partner is arriving. Start opens the pausing
player's native inventory in the shared view. Up/Down moves through acquired
weapons, A equips without closing, and only that player can resume with Start.
Health and ammo remain personal. Targeted checks compare the complete panel
with the original menu and replay a paused current-build state, including audio.
Guest Elec Beams and Guts pickup also change the shared stage's native blocks;
targeted checks compare the resulting tiles and native weapon artwork.
Either player can ride a partner's Magnet Beam with another weapon selected.
Targeted checks cover both directions, jumping off, native beam expiration,
Fit to screen and current-build state/audio continuation.
Both players also use the same native moving-platform buffers and carry speed;
targeted Foot Holder checks cover both rosters and state/audio replay.
Ordinary native enemy AI targets the nearest living player, with withdrawn
players excluded. Damage reactions retain the actual attacker's equipment.
Dense native AI checks cover palette preservation and an interrupted update's
current-build state, immediate image and audio continuation.
`--coop-view shared|split|p1|p2` chooses the development view. Independent views
use one native 256x240 grid per player, with that player's health, weapon meter
and usable native inventory. Only the player who paused can resume. Their
simulation activates ordinary enemies in the union of both views, retaining the
original enemy pool and retrying when a slot becomes available. Shots can cross
into the partner's view. Targeted checks compare full native images, both native
menu panels, local view modes, distant survivor handoffs, native death scripts
and current-build state/audio continuation.
Independent layouts keep their fixed grid widths through window resizing while
retaining the selected aspect for the shared view. Paused P1/P2 view changes
preserve the loaded background and each player's own native panel.
Native contact and weapon overlap tests also use the distant player's local
coordinates. Targeted checks cover both characters' damage, personal refills,
shared extra lives, charged shots and reflected shots beyond the main camera.
Either player can initiate the native Cut Man ladder link into the next room;
the partner follows the original scroll while keeping personal health and ammo.
Targeted checks cover both leaders, a distant initiator at the original aspect,
the native terrain ring, and mid-scroll state/audio continuation. A pit kills
only that character while the survivor continues, without spending a shared life.
The native Cut Man shutters also accept a distant second-player initiator and
bring both characters through the corridor and into the boss room. Their
original shutter animation and mid-scroll state/audio continuation are checked.
`--coop-wait-both on` optionally holds a horizontal boundary until the partner
arrives, and hides the first vertical arrival until both reach the same exit.
It defaults to off. A waiting survivor can proceed after the partner dies.
Targeted Cut Man checks cover both player orders, pending-state audio replay,
vertical return links and the original horizontal scripts.
Guest movement keeps native collision without streaming another background into
the main player's view; native weapon changes still update the shared blocks.
Cut's return acceleration and Bomb's bounce damping stay with each player's
weapon pool. Simultaneous throws match the original solo weapon paths, with
current-build state and audio continuation checked.
The second player can defeat Cut Man with the native Buster, collect the real
reward and complete the native clear script. Stage selection then returns to
P1 controls and starts the next stage with both characters. Targeted checks
cover the original menu picture and mid-clear state/audio continuation.
The optional Reduce slowdown feature gives busy scenes extra CPU time, with
the sound chip paused during that extra work. It defaults to on in a fresh
Mods catalog; `--slowdown off` retains the original timing. A dense native
co-op fixture reduces skipped updates from 43 to zero over 100 frames, with
the audio sample rate and current-build state/audio continuation checked.
No sprite flicker also defaults to on in a fresh Mods catalog. It draws
complete native actor art with stable player priority. A crowded scene needs
96 parts beyond the console's 64-part page; the completed object matches its
uncrowded native image. Targeted checks cover both character rosters, charged
shots and slides, all independent views, native HUD placement and current-build
state/audio continuation.
Co-op is available in Mods with character roster, view and room-transition
choices. It retains the required character support regardless of command-line
option order, and disabling co-op restores the requested solo character choice.
Targeted catalog checks cover both default enhancements, the selected split
view and roster, and current-build state/audio continuation. Full-stage
transition coverage and online play are still being implemented.

## Cycle backend migration branch

Default cycle controls: arrows move, Z is A, X is B, Enter is Start and
Backslash is Select. Escape or both controller sticks opens the system menu;
Tab fast-forwards. Use Controls to remap inputs in `config.ini`.
Older `keybinds.ini` and F5/F6/F7 instructions below apply to the legacy host.

The normal build now uses the cycle CPU backend. Code is generated from the
original ROM during configure and stays in the build directory. Run `setup.bat`
or `setup.sh` to initialize the pinned engine/UI submodules, then configure:

```sh
cmake -S . -B build-cycle -DNESRECOMP_ROM="/path/to/Game.nes"
cmake --build build-cycle --config Release --parallel 4
```

Run the executable with that same ROM path. The launcher verifies the image;
the runtime also verifies cartridge metadata and PRG identity. `NESRECOMP_ROOT`
and `NESRECOMP_RECOMP_UI` can name other checkouts, and cross builds accept
`NESRECOMP_HOST_COMPILER` pointing to a runnable host compiler.

Keep the old generated sources and select them explicitly with
`-DNESRECOMP_BACKEND=legacy` in a separate build directory. Legacy save states
cannot be loaded by the cycle backend; its cartridge shortcut slot uses
`saves/<ROM stem>.cycstate`. Preserve existing saves and settings during rollout.
Cycle-native comparisons use `nesrecomp/tools/cyc/cyc_verify.py`; `--interp-only`
runs the cycle interpreter for diagnosis. Earlier `--verify` / `--emulated`
Nestopia entry points remain in the legacy build.

Desktop Windows checks cover short gameplay routes and runtime
menu/input/save-state behavior. Other platforms and full-game completion
remain outside this validation.

Cycle Windows builds use `tools/build.ps1 -Rom <original-ROM>`. Create a ROM-free
ZIP with `tools/make_release.ps1 -Rom <original-ROM>`; `-SkipBuild -BuildDir
build-cycle` packages an existing production cycle build. Legacy C remains
available through explicit CMake selection.

## License

Project code uses the [PolyForm Noncommercial License 1.0.0](LICENSE), with the
additional donation and advertising permissions in [PERMISSIONS.md](PERMISSIONS.md).
Third-party components retain their own terms. No original game ROM is included.
