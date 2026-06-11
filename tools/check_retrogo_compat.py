#!/usr/bin/env python3
"""
retro-go-porting-toolkit CI gate: verify a port relies ONLY on what retro-go provides.

A port built against this toolkit is meant to drop into retro-go
(retro-go-sd/external/<port>) and bind to the firmware purely through the published
ABI. So two things must hold for the port's source:

  (1) it includes no firmware-internal / raw-hardware header — only standard libc,
      its own tree, and the vendored ABI contract.
  (2) every function the ABI contract exposes corresponds to something retro-go's
      gw_firmware_abi actually offers (libc subset, or an odroid_*/rg_* API).

Anything flagged is a reliance on a symbol retro-go does NOT provide — code that would
fail to link/run as a real retro-go core. Exits non-zero on any violation.

Usage (CI passes explicit paths; local dev falls back to this repo's layout):
    check_retrogo_compat.py [APP_SRC_DIR] [RETROGO_GW_FIRMWARE_ABI_H]
    env: PORT_APP_SRC, RETROGO_ABI
"""
import os, re, sys

TOOLKIT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # retro-go-porting-toolkit/
REPO = os.path.dirname(TOOLKIT)
OUR_ABI = os.path.join(TOOLKIT, "include", "gnw_firmware_abi.h")

# App source to check + retro-go's real gw_firmware_abi.h: args/env for CI, with this
# repo's layout as the local default (examples/ is git-ignored reference material).
APP_SRC = (sys.argv[1] if len(sys.argv) > 1
           else os.environ.get("PORT_APP_SRC", os.path.join(REPO, "gnw-doom", "src")))
RETROGO_ABI = (sys.argv[2] if len(sys.argv) > 2
               else os.environ.get("RETROGO_ABI", ""))

# Firmware-internal / raw-hardware headers a port must never include.
FORBIDDEN_HEADERS = re.compile(
    r'#\s*include\s*[<"]('
    r'regs|mm|flash|usart|gw_lcd|gw_audio|gw_buttons|gw_malloc|'
    r'audio_sai|ltdc|odroid_system|odroid_input|odroid_display|odroid_overlay|odroid_sdcard'
    r')\.h[>"]')

# What each flagged ABI entry must become for real-retro-go compatibility:
#   HW   = gnw-specific hardware API -> retro-go's odroid_*/rg_* equivalent
#   LIBC = libc retro-go's ABI omits -> the port brings its own (NOT the firmware)
#   FWINT= firmware-internal (the firmware triggers it); the core must not call it
RETROGO_EQUIV = {
    "gnw_input_read":   ("HW",   "odroid_input_read_gamepad / odroid_input_read_raw"),
    "power_off":        ("HW",   "firmware-owned standby; retro-go rg_system_halt / odroid power"),
    "audio_start":      ("HW",   "odroid_audio_submit (+ backend start)"),
    "audio_stop":       ("HW",   "odroid_audio_stop"),
    "audio_pos":        ("HW",   "retro-go owns the ring; no plugin-visible pos"),
    "audio_clean_range":("HW",   "retro-go owns DMA cache mgmt"),
    "ltdc_set_clut":    ("HW",   "odroid_display_set_palette / rg_display CLUT"),
    "frame_buffer":     ("HW",   "the core's odroid_video_frame_t.buffer (handed in)"),
    "gnw_overlay_run":  ("HW",   "odroid_overlay_game_menu / rg_gui"),
    "systick_cnt":      ("HW",   "rg_system_timer / get_elapsed_time"),
    "rg_heap_stats":    ("HW",   "rg_system meminfo (or drop the perf heap readout)"),
    "printf":           ("LIBC", "port's own libc (or RG_LOGx); retro-go ABI omits it"),
    "snprintf":         ("LIBC", "port's own libc; retro-go ABI omits it"),
    "sscanf":           ("LIBC", "port's own libc; retro-go ABI omits it"),
    "strcpy":           ("LIBC", "port's own libc; retro-go ABI omits it"),
    "strcspn":          ("LIBC", "port's own libc; retro-go ABI omits it"),
    "strdup":           ("LIBC", "port's own libc; retro-go ABI omits it"),
    "abs":              ("LIBC", "port's own libc; retro-go ABI omits it"),
    "malloc":           ("LIBC", "port's own libc / rg_alloc; retro-go ABI omits malloc"),
    "odroid_system_emu_save_state": ("FWINT", "firmware triggers save; core only registers handlers"),
    "odroid_system_emu_load_state": ("FWINT", "firmware triggers load; core only registers handlers"),
    "odroid_system_get_path":       ("FWINT", "firmware-internal path mapping"),
    "odroid_system_sram_save":      ("FWINT", "firmware-internal; core registers an sram handler"),
}

def find_gw_abi():
    if RETROGO_ABI:
        return RETROGO_ABI if os.path.exists(RETROGO_ABI) else None
    examples = os.path.join(REPO, "examples")
    for d, _, fs in os.walk(examples):
        if "gw_firmware_abi.h" in fs:
            return os.path.join(d, "gw_firmware_abi.h")
    return None

def fn_names(path):
    """Function-pointer / data member names from a gw_firmware_abi-style struct."""
    names = set()
    with open(path) as f:
        for line in f:
            m = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', line)      # T (*name)(...)
            if m: names.add(m.group(1))
            m = re.search(r'\*\s*(\w+)\s*;\s*(//|$)', line)        # T *name;  (data ptr)
            if m and m.group(1) not in ("version", "size"): names.add(m.group(1))
    return names

def main():
    violations = 0
    print(f"port:    {APP_SRC}")
    print(f"toolkit: {OUR_ABI}")

    bad_inc = []
    for d, _, fs in os.walk(APP_SRC):
        for fn in fs:
            if not fn.endswith((".c", ".h", ".cpp")):
                continue
            p = os.path.join(d, fn)
            with open(p, errors="ignore") as f:
                for i, line in enumerate(f, 1):
                    if FORBIDDEN_HEADERS.search(line):
                        bad_inc.append(f"{os.path.relpath(p, REPO)}:{i}: {line.strip()}")
    print("\n== (1) firmware-internal / raw-hardware headers in the port ==")
    if bad_inc:
        for b in bad_inc: print("  VIOLATION", b)
        violations += len(bad_inc)
    else:
        print("  ok — none")

    ours = fn_names(OUR_ABI)
    gw = find_gw_abi()
    provided = fn_names(gw) if gw else set()
    print(f"\n== (2) ABI functions not provided by retro-go's gw_firmware_abi ==")
    print(f"   (toolkit contract: {len(ours)}; retro-go: {len(provided)}"
          + ("" if gw else "; gw_firmware_abi.h NOT FOUND — pass it as arg 2 / $RETROGO_ABI") + ")")
    for g in sorted(ours - provided):
        cat, hint = RETROGO_EQUIV.get(g, ("UNKNOWN", "no known retro-go mapping"))
        print(f"  [{cat:5}] {g:30} -> {hint}")
        violations += 1
    if gw and not (ours - provided):
        print("  ok — every ABI function exists in retro-go")

    print(f"\n== summary: {violations} reliance(s) on non-retro-go symbols ==")
    return 1 if violations else 0

if __name__ == "__main__":
    sys.exit(main())
