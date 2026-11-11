# MegaMan1NESRecomp

The USA ROM supports adaptive Fit to screen and L/R weapon switching through
the preloaded Mods catalog. Fit is enabled for a fresh profile. L/R follows the
native inventory order, including empty weapons, and ignores unsafe transitions.
Existing saved choices take precedence over the package defaults.
Quick switching restores the original weapon's pattern graphics. Co-op players
retain their own weapon patterns while the main player's graphics are displayed.

The [widescreen implementation](WIDESCREEN.md) remains experimental. USA checks
cover Cut Man terrain, wider enemy activation, native/interpreter agreement and
current-build state replay. Margin combat, other stages and scripted transitions
still need qualification. The Europe prototype retains its separate hooks and
opt-in default.

The optional USA Proto Man package adds Select teleport swapping, an airborne
front shield with reflected native projectiles, two uncharged shots and double
incoming damage. Its normal-stat reskin option retains Mega Man's behavior.
Current checks cover native/interpreter agreement, Fit sprite placement,
current-build state replay and native enemy/boss collision paths.
The native inventory keeps its panel geometry and life count with the selected
character's 16x16 portrait.

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
and a survivor dying while the partner is arriving. Personal menus, full-stage
transition coverage, independent views and online play are still being
implemented; the Co-op package is not exposed in the Mods catalog yet.

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
