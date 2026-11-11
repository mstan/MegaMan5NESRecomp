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

In ordinary horizontal and vertical rooms, holding Select for 90 frames withdraws a player through the native departure animation. A fresh Select press rejoins beside a grounded partner, preserving personal health and equipment. The last visible player stays in the game. A newly reached native checkpoint revives a fallen partner without spending a life. Returning beams pass overhead platforms before opening at the checked floor, including floors above the original stage-entry height limit.

The follower departs and returns through native animations when the leading player triggers a room transition. Either player can lead a qualified Napalm Man downward or horizontal exit from the native camera's current page, in either character assignment. These cases have native/interpreter and current-build cold-state checks, including exact warm/cold arrival screenshots. Unsafe inventory and weapon presses during these transitions are discarded. Idle vertical rooms retain weapon switching, solo character swapping and airborne shield behavior after scrolling ends.

Shared-view co-op displays both players' health and selected-weapon energy using the cartridge's native meter routine and tiles. Proto Man's health meter has the blueprint's red tint. The meters have personal resource, inventory-resume and exact cold-load screenshot checks; isolated HUD construction preserves the accepted combat simulation.

Native E/M-tanks use the shared item counts and heal/refill the player who owns the paused inventory. Simultaneous use requests, empty items, pause ownership and cold loading during both refill animations have dedicated native/interpreter checks.

The independent Slowdown reduction package defaults on in a fresh catalog and uses the engine's extra CPU budget. `--slowdown-reduction on|off` selects it explicitly; `off` restores original timing. Controls, native charge and pause ownership pass native/interpreter and cold-state checks with the enhancement enabled. Muted captures retain the same PCM sample count at both settings. Demanding-stage timing and audio review remain part of final qualification.

No sprite flicker is a separate default-on package, with `--no-flicker on|off` for explicit selection. It retains native actor graphics beyond the scanline and scene OAM budgets in stable priority. A dense case captures585 native pieces while preserving the original simulation; dense and co-op cold-state screenshots match. Boss effects and full-stage rendering remain under qualification.

A Napalm Man checkpoint-to-boss route now exercises the native retry, real boss door, second-player Buster damage and kill, paired departure, password menu and selection of another stage. Both character assignments retain the shared unlock. Narrow authored rooms stay centered in Fit. Interpreter and combat/outro cold-state checks cover this route; position and protection fixtures bypass combat difficulty, and the intervening stage remains unqualified.

The same boss route also covers a lone P2 survivor after P1 falls into a pit. The survivor clears the boss and enters the next stage without spending another shared life; both players retain the unlock. Native departure keeps ownership of its camera until the stage changes, including when the partner has died.

The controls, ordinary combat, basic retry and transport slices have native/interpreter and current-build cold-state checks, including separate Buster damage, character damage amounts, survivor control, native leaper targeting and rejoining below an overhead platform. Pit deaths transfer control without spending a life; a fallen player's Start cannot open the survivor's inventory. A projectile held in contact cannot hurt during a solo Select swap, and native vulnerability returns afterward. Other bosses and special enemies, distant camera handoff, other transition types, gravity, shared rides, separate co-op views and menus, and online play still need implementation or qualification. Complete stage and visual coverage also remains in progress; this branch is not a finished parity release.
