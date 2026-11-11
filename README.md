# Mega Man 5 — NESRecomp

Development port of the original USA cartridge using NESRecomp's cycle backend. Supply your own ROM; the project and releases contain no cartridge data. Native gameplay is the baseline for character, co-op, controls and adaptive widescreen development.

Configure with CMake 3.20+, Python 3.11+ and a C/C++ toolchain:

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 5 (USA).nes"
cmake --build build --config Release --parallel 2
```

`NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` can select existing engine and launcher checkouts. Runtime tests use isolated configuration and explicit ROM paths. The cartridge has native slide and charged-buster mechanics and uses MMC3 CHR-ROM banking.
