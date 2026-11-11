# Mega Man 5 — NESRecomp

Development port of the original USA cartridge using NESRecomp's cycle backend. Supply your own ROM; the project and releases contain no cartridge data. Native gameplay is the baseline for character, co-op, controls and adaptive widescreen development.

Configure with CMake 3.20+, Python 3.11+ and a C/C++ toolchain:

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 5 (USA).nes"
cmake --build build --config Release --parallel 2
```

`NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` can select existing engine and launcher checkouts. Runtime tests use isolated configuration and explicit ROM paths. The cartridge has native slide and charged-buster mechanics and uses MMC3 CHR-ROM banking.

The development branch adds L/R selection of acquired weapons and helpers, plus an optional Proto Man character with Select teleport swapping. Proto Man defaults to two small Buster shots, double incoming damage and airborne front reflection; a normal-stat appearance mode remains available. Native movement, charge sizes, slide and weapon data remain cartridge-driven. His art is bundled with credits and rendered without modifying CHR-ROM.

These controls have native/interpreter and current-build cold-state checks. Native inventory portraits and previews are implemented. Adaptive Fit extends horizontal terrain and activates native actors across the view, with a qualified Napalm Man traversal and cold-state continuation.

The development-only `--coop on` option adds independent native movement, weapons, charge and drawing for two local players. Either player can open the native inventory; its controls and selection belong to the player who paused. `--coop-roster megaman` or `protoman` selects the first player's character, with the other character assigned to player two. Native READY text and hurt effects remain intact. Ordinary enemies use native AI once, targeting the nearer living player while checking both players' shots and personal damage. A survivor continues without spending a life; a full wipe uses one native checkpoint retry and revives both players.

The controls, ordinary combat and basic retry slices have native/interpreter and current-build cold-state checks, including separate Buster damage, character damage amounts, survivor control and native leaper targeting. A projectile held in contact cannot hurt during a solo Select swap, and native vulnerability returns afterward. Special enemy and boss behavior, checkpoint revival, shared rides, co-op views, HUD and online play still need implementation or qualification. Vertical transitions and complete stage and visual coverage also remain in progress; this branch is not a finished parity release.
