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

Proto Man's airborne Buster follows the canonical firing sprite, including inverted poses. Both directions and both co-op seats have native/interpreter and current-build cold-state checks; the grounded control retains its pixels and audio. Native gravity triggers remain part of stage qualification.

The optional Co-op package enables Proto Man as a dependency and assigns complementary characters. Its settings select the first player's character, shared/split/individual view and local online binding row. Shared view keeps both bodies visible; independent views use two native 256x240 rectangles. `--coop on`, `--coop-roster megaman|protoman` and `--coop-view shared|split|p1|p2` provide explicit development controls. Native READY text and hurt effects remain intact. Ordinary enemies use native AI once, targeting the nearer living player while checking both players' shots and personal damage. A survivor continues without spending a life; a full wipe uses one native checkpoint retry and revives both players.

In ordinary horizontal and vertical rooms, holding Select for 90 frames withdraws a player through the native departure animation. A fresh Select press rejoins beside a grounded partner, preserving personal health and equipment. The last visible player stays in the game. A newly reached native checkpoint revives a fallen partner without spending a life. Returning beams pass overhead platforms before opening at the checked floor, including floors above the original stage-entry height limit.

The follower departs and returns through native animations when the leading player triggers a room transition. Either player can lead qualified Napalm Man downward or horizontal exits, including independent-view exits several pages beyond the source camera. These cases have native/interpreter and current-build cold-state checks, including exact warm/cold arrival screenshots. Unsafe inventory and weapon presses during these transitions are discarded. Idle vertical rooms retain weapon switching, solo character swapping and airborne shield behavior after scrolling ends.

Shared-view co-op displays both players' health and selected-weapon energy using the cartridge's native meter routine and tiles. Proto Man's health meter has the blueprint's red tint. The meters have personal resource, inventory-resume and exact cold-load screenshot checks; isolated HUD construction preserves the accepted combat simulation.

Shared view shows the initiating player's native inventory. Separate views show both personal native pages, built from the cartridge's original layout, packet and cursor routines. Both players can choose their own weapons while paused; the initiating player resumes the world. Native E/M-tank actions retain shared stock and heal/refill their user on the original clock. Simultaneous requests, empty items, nonpauser use, pause ownership, cold loading and PCM continuation have native/interpreter checks. The pauser's 256x240 panel matches the native inventory pixel for pixel.

Independent views activate the union of their complete rectangles and retain terrain by actual screen. Split panels match the individual views pixel for pixel. The rollback network path seals the exact build, ROM, artwork, timing, roster and simulation settings before frame zero, using the shared CPU/backend capability exchange. The current Windows profile has two-process UDP checks from reset through gameplay, including different local views and mixed native/interpreter rollback under delayed packets. Native abilities need no second ROM.

Independent views preserve crumbling blocks on distant screens that share the cartridge's two-screen bitmap parity. Visible distant actors use their personal view for native AI camera checks; authored scene changes keep native camera ownership. Actual native break, leave-view restoration and re-entry behavior has interpreter and cold-state/PCM checks. Broader mutable-terrain visuals remain under qualification.

Defeated authored enemies stay gone while their spawn location remains in either view. The native spawn cursor respects the same lifetime when it later reaches that location; leaving both views permits the original respawn. A distant enemy has native charged-Buster kill, cursor-arrival, leave/re-entry and current-state checks, including matching interpreter execution, PCM and cold-load pictures. Body and camera fixtures bypass traversal; broader pickups and generator coverage remains in progress.

Split and individual views retain their native logical grids through aspect changes, window resizing and current-build state loading. Returning to shared view or disabling co-op restores the selected aspect mode. Switching between individual players also refreshes a paused picture. The split halves match the corresponding individual pictures pixel for pixel after real Fit and resize changes.

A bounded extension retains independent gravity panels and straight projectiles beyond the cartridge's sixteen ordinary actor slots. Referenced children and weapon targets keep their native slots. The original turret initializer can create its two-shot volley even when those slots are occupied; its shots then execute the original native AI. A separated-view case retains all fifteen authored objects without duplicates and reaches twenty-three active actors, with interpreter, cold-state, PCM, mod-reapply and disable/drain checks. Some dense native generators still exhaust the pinned slots; broader pressure remains under qualification.

The independent Slowdown reduction package defaults on in a fresh catalog and uses the engine's extra CPU budget. `--slowdown-reduction on|off` selects it explicitly; `off` restores original timing. Controls, native charge and pause ownership pass native/interpreter and cold-state checks with the enhancement enabled. Muted captures retain the same PCM sample count at both settings. Demanding-stage timing and audio review remain part of final qualification.

No sprite flicker is a separate default-on package, with `--no-flicker on|off` for explicit selection. It retains native actor graphics beyond the scanline and scene OAM budgets in stable priority. A dense case captures585 native pieces while preserving the original simulation; dense and co-op cold-state screenshots match. Boss effects and full-stage rendering remain under qualification.

A Napalm Man checkpoint-to-boss route now exercises the native retry, real boss door, second-player Buster damage and kill, paired departure, password menu and selection of another stage. Both character assignments retain the shared unlock. Narrow authored rooms stay centered in Fit. Interpreter and combat/outro cold-state checks cover this route; position and protection fixtures bypass combat difficulty, and the intervening stage remains unqualified.

The same boss route also covers a lone P2 survivor after P1 falls into a pit. The survivor clears the boss and enters the next stage without spending another shared life; both players retain the unlock. Native departure keeps ownership of its camera until the stage changes, including when the partner has died.

The controls, ordinary combat, basic retry and transport slices have native/interpreter and current-build cold-state checks, including separate Buster damage, character damage amounts, survivor control, native leaper targeting and rejoining below an overhead platform. Pit deaths transfer control without spending a life; a fallen player's Start cannot open the survivor's inventory. A projectile held in contact cannot hurt during a solo Select swap, and native vulnerability returns afterward. Full stage and visual coverage, other bosses and special enemies, gravity, shared rides, terrain/pickup and generator pressure, additional transition policies, broader network coverage and final performance/portability checks remain in progress. This branch is not a finished parity release.
