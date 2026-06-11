# gnw-retro-go-porting-toolkit

A minimal test firmware for the Nintendo Game & Watch (STM32H7B0) that
implements just enough of the [retro-go-sd](https://github.com/sylverb/game-and-watch-retro-go-sd)
API surface to bring up external cores without building all of retro-go:
display (LUT8 double-buffer LTDC), audio (SAI DMA ping-pong), input, the
in-game overlay, littlefs persistence, and a libc subset. The functions are
published as a `gw_firmware_abi_t` table at VTOR+0x400, so a core binds at
runtime — no `--just-symbols`, no relink against the firmware.

`deps/` carries the ST HAL drivers, CMSIS and littlefs. Built from the
consuming project (`Makefile-example`).

## Getting started

The toolkit goes in as a submodule of your port; the build runs from your
project root:

```
git submodule add git@github.com:slash-proc/gnw-retro-go-porting-toolkit.git retro-go-porting-toolkit
cp retro-go-porting-toolkit/Makefile-example Makefile
make build-firmware     # build/firmware.bin
make flash-firmware     # flash to internal bank2 (gnwmanager)
make flash-app          # flash your core's blob to external flash (gnwmanager flash ext)
```

`Makefile-example` builds the firmware out of the submodule and flashes with
[gnwmanager](https://github.com/BrianPugh/gnwmanager); add your core's build at
the bottom (`build/app.bin`). The firmware maps external flash memory-mapped at
`0x90000000` and publishes its API as a `gw_firmware_abi_t` at VTOR+0x400 — your
core reads the table at runtime instead of linking against firmware symbols.
Watch the bank caution in the Makefile if bank 1 holds a rescue bootloader.

## Debugging

`scripts/debug/` holds SWD tools built on gnwmanager's OpenOCD backend. These
are your primary instruments for introspecting hardware state — never assume a
fix works because it compiles; flash it and verify execution progresses.

- `diagnostic.py` — dumps CPU fault status (HardFault/BusFault/CFSR), register
  state, LTDC (display) status, and option-byte configuration. Run this first
  if the device appears hung.
- `trace.py` — execution control: `reset-halt` (reset + halt at the reset
  vector; crucial for recovering from hard bus hangs before reflashing),
  `resume`, `step [count]`.
- `memory.py` — read/write/dump memory. `--no-halt` forces accesses while the
  CPU is locked in a fault handler (e.g. force-disable a peripheral via its
  registers).
- `profile.py [samples] <elf>` — statistical PC-sampling profiler
  (halt → read PC → resume, tallied per function via addr2line).
- `remote_control.py` — drive the buttons over SWD via the remote-input shadow
  cell (see `src/input.c`), no hardware mod needed.
- `lfs_list.py <dump.bin>` — list the LittleFS partition from a raw flash dump
  (handles the inverted block order).
- `bank.py` — internal-flash option-byte utility (status/lock/unlock/swap).

Typical hang workflow: `diagnostic.py` → if wedged, `trace.py reset-halt` →
reflash → `memory.py` to watch state advance.

`scripts/common/` is a shared library used by some tools (plus assorted
utilities pending a cleanup). `scripts/build/gen_overlay_font.py` regenerates
the overlay menu font header (`include/font8x8.h`).
